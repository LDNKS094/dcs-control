// Reproject the captured OverlayLDR pixels with the same clip transform as replay.
struct StageCanvasRenderer {
  ID3D11Device* device=nullptr;
  ID3D11DeviceContext* deferred=nullptr;
  ID3D11VertexShader* vertex=nullptr;
  ID3D11PixelShader* pixel=nullptr;
  ID3D11Buffer* constants=nullptr;
  ID3D11SamplerState* sampler=nullptr;
  ID3D11RasterizerState* raster=nullptr;
  std::mutex mutex;

  void clear() {
    if(deferred)deferred->Release();if(vertex)vertex->Release();if(pixel)pixel->Release();
    if(constants)constants->Release();if(sampler)sampler->Release();if(raster)raster->Release();
    device=nullptr;deferred=nullptr;vertex=nullptr;pixel=nullptr;
    constants=nullptr;sampler=nullptr;raster=nullptr;
  }
  bool initialize(ID3D11Device* dev) {
    if(device==dev)return true;
    clear();
    static const char shader[]=R"(
cbuffer Reprojection:register(b0){row_major float4x4 Delta;};
struct Vertex{float4 p:SV_Position;float2 uv:TEXCOORD0;};
Vertex vs(uint id:SV_VertexID){
 Vertex o;o.uv=float2(id&1,id>>1);
 o.p=mul(float4(o.uv*float2(2,-2)+float2(-1,1),1,1),Delta);
 o.p.z=.5*o.p.w;return o;
}
Texture2D<float4> Canvas:register(t0);
SamplerState CanvasSampler:register(s0);
float4 ps(Vertex v):SV_Target{
 float4 color=Canvas.Sample(CanvasSampler,v.uv);
 if(!any(color))discard;
 return color;
})";
    const auto module=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto compile=module?reinterpret_cast<pD3DCompile>(GetProcAddress(module,"D3DCompile")):nullptr;
    ID3DBlob *vs=nullptr,*ps=nullptr;
    HRESULT hr=compile?compile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vs,nullptr):E_NOINTERFACE;
    if(SUCCEEDED(hr))hr=compile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&ps,nullptr);
    if(SUCCEEDED(hr))hr=dev->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex);
    if(SUCCEEDED(hr))hr=dev->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel);
    if(vs)vs->Release();if(ps)ps->Release();if(module)FreeLibrary(module);
    if(SUCCEEDED(hr))hr=dev->CreateDeferredContext(0,&deferred);
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=64;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(SUCCEEDED(hr))hr=dev->CreateBuffer(&cb,nullptr,&constants);
    D3D11_SAMPLER_DESC sampling{};sampling.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;
    sampling.ComparisonFunc=D3D11_COMPARISON_NEVER;
    if(SUCCEEDED(hr))hr=dev->CreateSamplerState(&sampling,&sampler);
    D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
    if(SUCCEEDED(hr))hr=dev->CreateRasterizerState(&rs,&raster);
    if(FAILED(hr)){clear();Log("stage canvas: setup failed 0x%08x",unsigned(hr));return false;}
    device=dev;return true;
  }
  bool draw(ID3D11DeviceContext* immediate,ID3D11Texture2D* target,
      ID3D11ShaderResourceView* source,DXGI_FORMAT format,ID3D11BlendState* blend,
      const float factors[4],UINT mask,const float delta[16]) {
    std::lock_guard guard(mutex);
    ID3D11Device* dev=nullptr;target->GetDevice(&dev);
    D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=format;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    ID3D11RenderTargetView* rtv=nullptr;
    const auto hr=initialize(dev)?dev->CreateRenderTargetView(target,&rd,&rtv):E_FAIL;
    dev->Release();
    if(FAILED(hr)){Log("stage canvas: output view failed 0x%08x",unsigned(hr));return false;}
    D3D11_TEXTURE2D_DESC desc{};target->GetDesc(&desc);
    const D3D11_VIEWPORT viewport{0,0,float(desc.Width),float(desc.Height),0,1};
    deferred->UpdateSubresource(constants,0,nullptr,delta,0,0);
    deferred->VSSetShader(vertex,nullptr,0);deferred->VSSetConstantBuffers(0,1,&constants);
    deferred->PSSetShader(pixel,nullptr,0);deferred->PSSetShaderResources(0,1,&source);
    deferred->PSSetSamplers(0,1,&sampler);
    deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    deferred->RSSetState(raster);deferred->RSSetViewports(1,&viewport);
    deferred->OMSetRenderTargets(1,&rtv,nullptr);deferred->OMSetBlendState(blend,factors,mask);
    deferred->Draw(4,0);
    ID3D11CommandList* commands=nullptr;
    const auto recorded=deferred->FinishCommandList(FALSE,&commands);
    rtv->Release();
    if(FAILED(recorded))return false;
    immediate->ExecuteCommandList(commands,TRUE);commands->Release();return true;
  }
};
StageCanvasRenderer stageCanvasRenderer;

struct GuiCanvasPacket final : IUnknown {
  std::atomic<ULONG> refs{1};
  UiCache cache;
  ID3D11ShaderResourceView* source=nullptr;
  ID3D11BlendState* blend=nullptr;
  DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
  float factors[4]{};
  UINT mask{};
  ~GuiCanvasPacket(){clear();if(blend)blend->Release();}
  void clear(){if(source)source->Release();source=nullptr;cache.clear();}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
    if(!out)return E_POINTER;*out=nullptr;
    if(iid!=__uuidof(IUnknown))return E_NOINTERFACE;
    *out=static_cast<IUnknown*>(this);AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override{return refs.fetch_add(1)+1;}
  ULONG STDMETHODCALLTYPE Release() override{const auto n=refs.fetch_sub(1)-1;if(!n)delete this;return n;}
  static bool __cdecl Replay(IUnknown* lease,ID3D11DeviceContext* context,ID3D11Texture2D* target,const float delta[16]) {
    const auto* canvas=static_cast<GuiCanvasPacket*>(lease);
    Scope scope;
    return stageCanvasRenderer.draw(context,target,canvas->source,canvas->format,canvas->blend,canvas->factors,canvas->mask,delta);
  }
};
