// DCS's original GUI draw is kept as a frozen packet. Each presentation uses
// its geometry/material state, a private GPU-patched MVP, and the actual eye RTV.
std::mutex stageShaderMutex;
ID3D11Device* stageShaderDevice=nullptr;
ID3D11ComputeShader* stageMatrixShader=nullptr;
void ReleaseStageShader(){
  std::lock_guard guard(stageShaderMutex);
  if(stageMatrixShader)stageMatrixShader->Release();stageMatrixShader=nullptr;stageShaderDevice=nullptr;
}
struct GuiStagePacket final : IUnknown {
  std::atomic<ULONG> refs{1};
  GuiDeferredProbe saved;
  std::function<void(ID3D11DeviceContext*)> draw;
  std::mutex mutex;
  ID3D11Buffer *rawInput=nullptr,*rawOutput=nullptr,*matrixOutput=nullptr,*params=nullptr;
  ID3D11ShaderResourceView* inputView=nullptr;
  ID3D11UnorderedAccessView* outputView=nullptr;
  ID3D11ComputeShader* patch=nullptr;
  ID3D11Device* device=nullptr;
  UINT matrixBytes=0;
  UINT sourceWidth=0,sourceHeight=0,viewportCount=0,scissorCount=0;
  D3D11_VIEWPORT sourceViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]={};
  D3D11_RECT sourceScissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]={};
  ~GuiStagePacket(){
    if(rawInput)rawInput->Release();if(rawOutput)rawOutput->Release();
    if(matrixOutput)matrixOutput->Release();if(params)params->Release();
    if(inputView)inputView->Release();if(outputView)outputView->Release();
    if(patch)patch->Release();if(device)device->Release();
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
    if(!out)return E_POINTER;*out=nullptr;
    if(iid!=__uuidof(IUnknown))return E_NOINTERFACE;
    *out=static_cast<IUnknown*>(this);AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override{return refs.fetch_add(1)+1;}
  ULONG STDMETHODCALLTYPE Release() override{const auto n=refs.fetch_sub(1)-1;if(!n)delete this;return n;}
  bool prepareMatrix(){
    if(!saved.matrixSource){Log("stage replay: GUI VS b0 is missing");return false;}
    D3D11_BUFFER_DESC original{};saved.matrixSource->GetDesc(&original);matrixBytes=original.ByteWidth;
    if(size_t(saved.matrixFirst)*16+64>matrixBytes){Log("stage replay: GUI VS b0 matrix is outside the bound buffer");return false;}
    D3D11_BUFFER_DESC raw{};raw.ByteWidth=matrixBytes;raw.Usage=D3D11_USAGE_DEFAULT;
    raw.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;raw.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr=device->CreateBuffer(&raw,nullptr,&rawInput);
    raw.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    if(SUCCEEDED(hr))hr=device->CreateBuffer(&raw,nullptr,&rawOutput);
    original.Usage=D3D11_USAGE_DEFAULT;original.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    original.CPUAccessFlags=original.MiscFlags=original.StructureByteStride=0;
    if(SUCCEEDED(hr))hr=device->CreateBuffer(&original,nullptr,&matrixOutput);
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R32_TYPELESS;
    sd.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;sd.BufferEx.NumElements=matrixBytes/4;sd.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;
    if(SUCCEEDED(hr))hr=device->CreateShaderResourceView(rawInput,&sd,&inputView);
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=matrixBytes/4;ud.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
    if(SUCCEEDED(hr))hr=device->CreateUnorderedAccessView(rawOutput,&ud,&outputView);
    D3D11_BUFFER_DESC pd{};pd.ByteWidth=80;pd.Usage=D3D11_USAGE_DEFAULT;pd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(SUCCEEDED(hr))hr=device->CreateBuffer(&pd,nullptr,&params);
    if(FAILED(hr)){Log("stage replay: matrix resources failed 0x%08x",unsigned(hr));return false;}
    std::lock_guard shaderGuard(stageShaderMutex);
    if(stageShaderDevice==device&&stageMatrixShader){patch=stageMatrixShader;patch->AddRef();return true;}
    static const char shader[]=R"(
ByteAddressBuffer Source:register(t0);RWByteAddressBuffer Destination:register(u0);
cbuffer Parameters:register(b0){row_major float4x4 Delta;uint Bytes;uint MatrixBase;uint2 Padding;};
[numthreads(64,1,1)]void main(uint3 tid:SV_DispatchThreadID){
 uint at=tid.x*4;if(at>=Bytes)return;uint bits=Source.Load(at);
 if(at>=MatrixBase&&at<MatrixBase+64){
   uint entry=(at-MatrixBase)/4;uint row=entry%4,column=entry/4;float value=0;
   [unroll]for(uint k=0;k<4;k++)value+=asfloat(Source.Load(MatrixBase+(k*4+row)*4))*Delta[k][column];
   bits=asuint(value);
 }
 Destination.Store(at,bits);
})";
    const auto compiler=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto compile=compiler?reinterpret_cast<pD3DCompile>(GetProcAddress(compiler,"D3DCompile")):nullptr;
    ID3DBlob *code=nullptr,*errors=nullptr;
    hr=compile?compile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&code,&errors):E_NOINTERFACE;
    if(SUCCEEDED(hr))hr=device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&patch);
    if(errors)errors->Release();if(code)code->Release();if(compiler)FreeLibrary(compiler);
    if(FAILED(hr))Log("stage replay: matrix shader failed 0x%08x",unsigned(hr));
    else {
      if(stageMatrixShader)stageMatrixShader->Release();
      stageMatrixShader=patch;stageMatrixShader->AddRef();stageShaderDevice=device;
    }
    return SUCCEEDED(hr);
  }
  bool initialize(ID3D11Texture2D* original){
    original->GetDevice(&device);
    D3D11_TEXTURE2D_DESC source{};original->GetDesc(&source);sourceWidth=source.Width;sourceHeight=source.Height;
    viewportCount=scissorCount=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    g_s.ctx->RSGetViewports(&viewportCount,sourceViewports);g_s.ctx->RSGetScissorRects(&scissorCount,sourceScissors);
    if(!saved.begin(original,false)||!prepareMatrix())return false;
    // Preserve the frozen bindings on the deferred context for subsequent
    // independent lists. The bootstrap clear is discarded, never submitted.
    ID3D11CommandList* bootstrap=nullptr;
    const auto hr=saved.context->FinishCommandList(TRUE,&bootstrap);
    if(bootstrap)bootstrap->Release();return SUCCEEDED(hr);
  }
  bool replay(ID3D11DeviceContext* immediate,ID3D11Texture2D* target,const float delta[16]){
    Scope scope;
    std::lock_guard guard(mutex);
    ID3D11Device* targetDevice=nullptr;target->GetDevice(&targetDevice);
    const bool sameDevice=targetDevice==device;if(targetDevice)targetDevice->Release();
    if(!sameDevice){Log("stage replay: target belongs to a different D3D11 device");return false;}
    D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=saved.renderFormat;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    ID3D11RenderTargetView* rtv=nullptr;auto hr=device->CreateRenderTargetView(target,&rd,&rtv);
    if(FAILED(hr)){Log("stage replay: eye RTV format %u failed 0x%08x",unsigned(rd.Format),unsigned(hr));return false;}
    auto* c=saved.context;
    struct Parameters {float delta[16];UINT bytes,base,padding[2];} values{};
    memcpy(values.delta,delta,sizeof(values.delta));values.bytes=matrixBytes;values.base=saved.matrixFirst*16;
    // The frozen CB cannot also be an SRV. Raw input/output buffers keep the
    // original encoded constants on the GPU; only the 4x4 MVP is multiplied.
    c->CopyResource(rawInput,saved.matrixSource);c->UpdateSubresource(params,0,nullptr,&values,0,0);
    c->CSSetShader(patch,nullptr,0);c->CSSetShaderResources(0,1,&inputView);
    c->CSSetUnorderedAccessViews(0,1,&outputView,nullptr);c->CSSetConstantBuffers(0,1,&params);
    c->Dispatch((matrixBytes/4+63)/64,1,1);
    ID3D11UnorderedAccessView* nullUav=nullptr;ID3D11ShaderResourceView* nullSrv=nullptr;
    c->CSSetUnorderedAccessViews(0,1,&nullUav,nullptr);c->CSSetShaderResources(0,1,&nullSrv);
    c->CopyResource(matrixOutput,rawOutput);
    ID3D11DeviceContext1* c1=nullptr;c->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&c1));
    c1->VSSetConstantBuffers1(0,1,&matrixOutput,&saved.matrixFirst,&saved.matrixCount);c1->Release();
    c->OMSetRenderTargets(1,&rtv,nullptr);rtv->Release();
    D3D11_TEXTURE2D_DESC desc{};target->GetDesc(&desc);
    // Preserve the original normalized viewport/scissor when the composed eye
    // has more pixels than its peripheral source view.
    const double sx=double(desc.Width)/sourceWidth,sy=double(desc.Height)/sourceHeight;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]={};
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]={};
    for(UINT i=0;i<viewportCount;++i){auto& v=viewports[i];v=sourceViewports[i];
      v.TopLeftX=float(v.TopLeftX*sx);v.TopLeftY=float(v.TopLeftY*sy);v.Width=float(v.Width*sx);v.Height=float(v.Height*sy);}
    const auto edge=[](LONG value,double scale,UINT limit,bool end){
      const double mapped=std::max(0.,std::min(double(limit),double(value)*scale));
      return LONG(end?std::ceil(mapped):std::floor(mapped));
    };
    for(UINT i=0;i<scissorCount;++i){const auto& s=sourceScissors[i];
      scissors[i]={edge(s.left,sx,desc.Width,false),edge(s.top,sy,desc.Height,false),
        edge(s.right,sx,desc.Width,true),edge(s.bottom,sy,desc.Height,true)};}
    c->RSSetViewports(viewportCount,viewports);c->RSSetScissorRects(scissorCount,scissors);
    draw(c);
    ID3D11CommandList* commands=nullptr;hr=c->FinishCommandList(TRUE,&commands);
    if(SUCCEEDED(hr)){immediate->ExecuteCommandList(commands,TRUE);commands->Release();}
    return SUCCEEDED(hr);
  }
  static bool __cdecl Replay(IUnknown* lease,ID3D11DeviceContext* immediate,ID3D11Texture2D* target,const float delta[16]){
    return static_cast<GuiStagePacket*>(lease)->replay(immediate,target,delta);
  }
};
struct StagePacketSlot {
  IUnknown* packet=nullptr;
  decltype(CheekyStageReplayViewV1::replay) replay=nullptr;
  uint64_t frame=~0ull;
};
std::mutex stagePacketMutex;
StagePacketSlot stagePackets[2];
std::vector<GuiCanvasPacket*> stageCanvases[2];
bool StageCanvasWanted() {
  static const bool optedIn=GetEnvironmentVariableW(L"DCS_STAGE_UI_CANVAS",nullptr,0)>0;
  return optedIn&&runtimeStageEnabled.load(std::memory_order_acquire);
}
GuiCanvasPacket* AcquireCanvasSlot(unsigned eye) {
  std::lock_guard guard(stagePacketMutex);
  auto& slot=stagePackets[eye];
  if(slot.packet)slot.packet->Release();slot={};
  auto& pool=stageCanvases[eye];
  GuiCanvasPacket* canvas=nullptr;
  for(auto* candidate:pool)if(candidate->refs.load()==1){canvas=candidate;break;}
  // Two eye layers follow the same bounded in-flight history as the stereo frames.
  if(!canvas&&pool.size()<6){canvas=new GuiCanvasPacket;pool.push_back(canvas);}
  if(canvas)canvas->AddRef();
  return canvas;
}
void PublishStagePacket(IUnknown* packet,decltype(CheekyStageReplayViewV1::replay) replay) {
  std::lock_guard guard(stagePacketMutex);
  auto& slot=stagePackets[view];
  if(slot.packet)slot.packet->Release();
  packet->AddRef();slot={packet,replay,frame};
}
void ReleaseStagePackets(){
  std::lock_guard guard(stagePacketMutex);
  for(auto& slot:stagePackets){if(slot.packet)slot.packet->Release();slot={};}
  for(auto& pool:stageCanvases){for(auto* canvas:pool)canvas->Release();pool.clear();}
}
void EnableStageReplay(bool enabled){
  runtimeStageEnabled.store(enabled,std::memory_order_release);
  if(!enabled){
    ReleaseStagePackets();ReleaseStageShader();
    {std::lock_guard guard(stageCanvasRenderer.mutex);stageCanvasRenderer.clear();}
    if(!SceneWanted())ReleaseScenes();
  }
}
bool AcquireStageReplay(uint64_t expectedFrame,uint32_t v,CheekyStageReplayViewV1* out){
  if(!out||v>3)return false;*out={};
  std::lock_guard sceneGuard(sceneMutex);
  const auto& image=preLdr[v];if(!image.texture||image.frame!=expectedFrame)return false;
  std::lock_guard packetGuard(stagePacketMutex);
  if(v<2){const auto& slot=stagePackets[v];if(!slot.packet||slot.frame!=expectedFrame)return false;
    out->lease=slot.packet;out->lease->AddRef();out->replay=slot.replay;}
  out->scene=image.texture;out->scene->AddRef();return true;
}
template<int Kind,class... Args>
void RecordStageDraw(ID3D11DeviceContext*,Args... args){
  if(!runtimeStageEnabled.load(std::memory_order_acquire)||!active||own||view<0||view>1||
     passName!="OverlayLDRPass"||draws!=1)return;
  if(StageCanvasWanted()){
    std::lock_guard guard(stagePacketMutex);
    if(stagePackets[view].packet&&stagePackets[view].frame==frame)return;
  }
  if constexpr(Kind!=fdexport::kDkDraw&&Kind!=fdexport::kDkIndexed&&Kind!=fdexport::kDkInstanced&&Kind!=fdexport::kDkIndexedInstanced){
    Log("stage replay: GUI draw kind %d needs indirect/stream-output inputs",Kind);return;
  }else{
    Scope scope;ID3D11RenderTargetView* rtv=nullptr;g_s.ctx->OMGetRenderTargets(1,&rtv,nullptr);
    ID3D11Resource* resource=nullptr;if(rtv){rtv->GetResource(&resource);rtv->Release();}
    ID3D11Texture2D* original=nullptr;if(resource){resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&original));resource->Release();}
    if(!original){Log("stage replay: selected GUI has no texture target");return;}
    LARGE_INTEGER start{},end{},frequency{};
    QueryPerformanceCounter(&start);
    auto* packet=new GuiStagePacket;
    const bool ok=packet->initialize(original);original->Release();
    QueryPerformanceCounter(&end);
    static double totalMs=0;
    static unsigned samples=0;
    QueryPerformanceFrequency(&frequency);
    totalMs+=1000.0*double(end.QuadPart-start.QuadPart)/double(frequency.QuadPart);
    if(++samples==240){
      Log("stage replay timing: draw_samples=%u freeze_cpu_ms=%.3f",samples,totalMs/samples);
      samples=0;totalMs=0;
    }
    if(!ok){packet->Release();return;}
    packet->draw=[=](ID3D11DeviceContext* c){
      if constexpr(Kind==fdexport::kDkDraw)c->Draw(args...);
      else if constexpr(Kind==fdexport::kDkIndexed)c->DrawIndexed(args...);
      else if constexpr(Kind==fdexport::kDkInstanced)c->DrawInstanced(args...);
      else if constexpr(Kind==fdexport::kDkIndexedInstanced)c->DrawIndexedInstanced(args...);
    };
    PublishStagePacket(packet,&GuiStagePacket::Replay);packet->Release();
  }
}
