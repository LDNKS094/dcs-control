// One GPU check for the production GUI view override: render with DCS-style
// camera/MVP constants, replace the view, then render again after restoration.
int GuiViewProbeTest() {
  namespace ckipd=fixdepth::ckipd;
  const auto module=LoadLibraryW(L"d3d11.dll");
  auto create=reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(module,"D3D11CreateDevice"));
  ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
  HRESULT hr=create(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
  Check(SUCCEEDED(hr),"GUI replay GPU device");if(FAILED(hr))return 1;
  auto& state=fixdepth::g_s;auto* oldDev=state.dev;auto* oldCtx=state.ctx;auto* oldCtx1=state.ctx1;
  state.dev=device;state.ctx=context;state.ctx1=nullptr;
  context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&state.ctx1));
  const auto compiler=LoadLibraryW(L"d3dcompiler_47.dll");
  auto compile=reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler,"D3DCompile"));
  const char* shader="cbuffer Effect:register(b0){float4x4 MVP;}struct V{float4 p:SV_Position;};"
    "V vs(uint n:SV_VertexID){float2 p[3]={float2(-.7,-.6),float2(0,.7),float2(.7,-.6)};"
    "V o;o.p=mul(float4(p[n],2,1),MVP);return o;}Texture2D<float4> Gui:register(t0);"
    "float4 ps(V i):SV_Target{return Gui.Load(int3(uint2(i.p.xy)%4,0));}";
  ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;
  hr=compile(shader,strlen(shader),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&code,&errors);
  if(errors)errors->Release();errors=nullptr;
  if(SUCCEEDED(hr)){hr=device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs);code->Release();}
  Check(SUCCEEDED(hr),"GUI replay vertex shader");
  hr=compile(shader,strlen(shader),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&code,&errors);
  if(errors)errors->Release();
  if(SUCCEEDED(hr)){hr=device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&ps);code->Release();}
  Check(SUCCEEDED(hr),"GUI replay pixel shader");
  ckipd::M4 view{},projection{};for(int i=0;i<4;++i)view.a[i][i]=1;
  projection.a[0][0]=1.2;projection.a[1][1]=1.1;projection.a[2][0]=.12;
  projection.a[2][1]=-.08;projection.a[2][2]=1.01;projection.a[2][3]=1;projection.a[3][2]=-.101;
  const auto vp=ckipd::MulM(view,projection);ckipd::M4 inv{};ckipd::Inv4(vp,&inv);
  std::vector<uint8_t> camera(ckipd::kCbBytes);uint8_t effect[64]{};
  ckipd::StoreM(camera.data(),ckipd::kOffView,view);ckipd::StoreM(camera.data(),ckipd::kOffViewInv,view);
  ckipd::StoreM(camera.data(),ckipd::kOffProj,projection);ckipd::StoreM(camera.data(),ckipd::kOffViewProj,vp);
  ckipd::StoreM(camera.data(),ckipd::kOffViewProjInv,inv);ckipd::StoreM(effect,0,vp);
  ID3D11Buffer* cb=nullptr;ID3D11Buffer* perView=nullptr;
  D3D11_BUFFER_DESC bd{};bd.ByteWidth=64;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.Usage=D3D11_USAGE_DEFAULT;
  D3D11_SUBRESOURCE_DATA init{effect,0,0};hr=device->CreateBuffer(&bd,&init,&cb);
  bd.ByteWidth=UINT(camera.size());init.pSysMem=camera.data();hr|=device->CreateBuffer(&bd,&init,&perView);
  Check(SUCCEEDED(hr),"GUI replay constant buffers");
  context->VSSetConstantBuffers(0,1,&cb);context->VSSetConstantBuffers(7,1,&perView);
  context->VSSetShader(vs,nullptr,0);context->PSSetShader(ps,nullptr,0);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  D3D11_DEPTH_STENCIL_DESC disabled{};ID3D11DepthStencilState* depthState=nullptr;
  device->CreateDepthStencilState(&disabled,&depthState);context->OMSetDepthStencilState(depthState,0);
  uint32_t guiPixels[16];for(unsigned i=0;i<16;++i)guiPixels[i]=0xff4090c0u+i*0x00010101u;
  D3D11_TEXTURE2D_DESC guiDesc{};guiDesc.Width=guiDesc.Height=4;guiDesc.MipLevels=guiDesc.ArraySize=guiDesc.SampleDesc.Count=1;
  guiDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;guiDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA guiInit{guiPixels,16,0};ID3D11Texture2D* gui=nullptr;ID3D11ShaderResourceView* guiSrv=nullptr;
  device->CreateTexture2D(&guiDesc,&guiInit,&gui);device->CreateShaderResourceView(gui,nullptr,&guiSrv);
  context->PSSetShaderResources(0,1,&guiSrv);
  D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=128;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;
  td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;
  ID3D11Texture2D* target=nullptr;ID3D11Texture2D* staging=nullptr;ID3D11RenderTargetView* rtv=nullptr;
  device->CreateTexture2D(&td,nullptr,&target);device->CreateRenderTargetView(target,nullptr,&rtv);
  td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  device->CreateTexture2D(&td,nullptr,&staging);
  D3D11_VIEWPORT viewport{0,0,128,128,0,1};context->RSSetViewports(1,&viewport);
  D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;rd.ScissorEnable=TRUE;
  ID3D11RasterizerState* raster=nullptr;device->CreateRasterizerState(&rd,&raster);context->RSSetState(raster);
  const D3D11_RECT scissor{0,0,128,128};context->RSSetScissorRects(1,&scissor);
  auto render=[&](){
    const float clear[4]={};context->OMSetRenderTargets(1,&rtv,nullptr);context->ClearRenderTargetView(rtv,clear);
    context->Draw(3,0);context->CopyResource(staging,target);D3D11_MAPPED_SUBRESOURCE mapped{};
    std::vector<uint8_t> bytes(128*128*4);const auto result=context->Map(staging,0,D3D11_MAP_READ,0,&mapped);
    Check(SUCCEEDED(result),"GUI replay GPU readback");
    if(SUCCEEDED(result)){for(unsigned y=0;y<128;++y)memcpy(bytes.data()+y*128*4,static_cast<uint8_t*>(mapped.pData)+y*mapped.RowPitch,128*4);context->Unmap(staging,0);}
    return bytes;
  };
  const auto baseline=render();std::vector<uint8_t> moved;
  {
    fixdepth::scapture::GuiViewProbe override;
    const bool ok=override.begin();Check(ok,"GUI replay production view override");
    if(ok)moved=render();
    override.restore();
  }
  const auto restored=render();
  Check(baseline==restored,"GUI replay restores original camera and effect bindings exactly");
  Check(!moved.empty()&&moved!=baseline,"GUI replay new view changes GPU projection");
  Check(std::any_of(baseline.begin(),baseline.end(),[](uint8_t v){return v!=0;}),"GUI replay baseline draws pixels");
  fixdepth::scapture::GuiStagePacket stage;
  const bool stageReady=stage.initialize(target);
  Check(stageReady,"GUI stage saves reusable pipeline with FinishCommandList TRUE");
  stage.draw=[](ID3D11DeviceContext* c){c->Draw(3,0);};
  {
    fixdepth::scapture::GuiDeferredProbe delayed;
    const bool ok=delayed.begin(target,false);Check(ok,"GUI deferred records production pipeline and frozen inputs");
    if(ok){
      delayed.context->Draw(3,0);Check(delayed.finish(),"GUI deferred finishes original draw");
      uint8_t empty[64]={};context->UpdateSubresource(cb,0,nullptr,empty,0,0);
      context->UpdateSubresource(gui,0,nullptr,empty,16,0);
      context->ClearState();
      context->ExecuteCommandList(delayed.commands,TRUE);
      context->CopyResource(staging,target);D3D11_MAPPED_SUBRESOURCE mapped{};
      hr=context->Map(staging,0,D3D11_MAP_READ,0,&mapped);std::vector<uint8_t> replayed(128*128*4);
      if(SUCCEEDED(hr)){for(unsigned y=0;y<128;++y)memcpy(replayed.data()+y*128*4,static_cast<uint8_t*>(mapped.pData)+y*mapped.RowPitch,128*4);context->Unmap(staging,0);}
      Check(SUCCEEDED(hr)&&replayed==baseline,"GUI deferred survives source CB/texture mutation and matches original pixels");
      ID3D11VertexShader* remaining=nullptr;context->VSGetShader(&remaining,nullptr,nullptr);
      Check(remaining==nullptr,"GUI deferred restores immediate shader state");if(remaining)remaining->Release();
    }
  }
  if(stageReady){
    float identity[16]={};for(unsigned i=0;i<4;++i)identity[i*4+i]=1;
    Check(stage.replay(context,target,identity),"GUI stage replays frozen draw onto supplied target");
    context->CopyResource(staging,target);D3D11_MAPPED_SUBRESOURCE mapped{};
    hr=context->Map(staging,0,D3D11_MAP_READ,0,&mapped);std::vector<uint8_t> replayed(128*128*4);
    if(SUCCEEDED(hr)){for(unsigned y=0;y<128;++y)memcpy(replayed.data()+y*128*4,static_cast<uint8_t*>(mapped.pData)+y*mapped.RowPitch,128*4);context->Unmap(staging,0);}
    Check(SUCCEEDED(hr)&&replayed==baseline,"GUI stage reusable list keeps frozen shader resources after source mutation");
    Check(stage.replay(context,target,identity),"GUI stage repeats a second independent list");
    const float clear[4]={};context->ClearRenderTargetView(rtv,clear);
    float shifted[16];memcpy(shifted,identity,sizeof(shifted));shifted[12]=.2f;
    const bool shiftedOk=stage.replay(context,target,shifted);context->CopyResource(staging,target);
    hr=context->Map(staging,0,D3D11_MAP_READ,0,&mapped);
    if(SUCCEEDED(hr)){for(unsigned y=0;y<128;++y)memcpy(replayed.data()+y*128*4,static_cast<uint8_t*>(mapped.pData)+y*mapped.RowPitch,128*4);context->Unmap(staging,0);}
    Check(shiftedOk&&SUCCEEDED(hr)&&replayed!=baseline,"GUI stage applies a nonidentity clip transform on the GPU");
    D3D11_TEXTURE2D_DESC largeDesc{};target->GetDesc(&largeDesc);largeDesc.Width=largeDesc.Height=256;
    ID3D11Texture2D* large=nullptr;ID3D11Texture2D* largeRead=nullptr;
    hr=device->CreateTexture2D(&largeDesc,nullptr,&large);
    largeDesc.Usage=D3D11_USAGE_STAGING;largeDesc.BindFlags=0;largeDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    hr|=device->CreateTexture2D(&largeDesc,nullptr,&largeRead);
    ID3D11RenderTargetView* largeRtv=nullptr;
    if(SUCCEEDED(hr))hr=device->CreateRenderTargetView(large,nullptr,&largeRtv);
    if(SUCCEEDED(hr))context->ClearRenderTargetView(largeRtv,clear);
    if(SUCCEEDED(hr)&&stage.replay(context,large,identity)){
      context->CopyResource(largeRead,large);hr=context->Map(largeRead,0,D3D11_MAP_READ,0,&mapped);unsigned right=0,bottom=0;
      if(SUCCEEDED(hr)){for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){const auto* px=static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch+x*4;if(px[3]){right+=x>=128;bottom+=y>=128;}}context->Unmap(largeRead,0);}
      Check(SUCCEEDED(hr)&&right&&bottom,"GUI stage scales viewport and scissor for a larger target");
    }else Check(false,"GUI stage larger target replay");
    if(largeRtv)largeRtv->Release();if(largeRead)largeRead->Release();if(large)large->Release();
  }
  {
    D3D11_TEXTURE2D_DESC desc{};target->GetDesc(&desc);
    std::vector<uint32_t> rgba(size_t(desc.Width)*desc.Height);
    for(UINT y=0;y<desc.Height;++y)for(UINT x=desc.Width/4;x<desc.Width/2;++x)
      rgba[size_t(y)*desc.Width+x]=0x800000ff;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA pixels{rgba.data(),desc.Width*4,0};
    ID3D11Texture2D* canvas=nullptr;ID3D11ShaderResourceView* source=nullptr;
    hr=device->CreateTexture2D(&desc,&pixels,&canvas);
    if(SUCCEEDED(hr))hr=device->CreateShaderResourceView(canvas,nullptr,&source);
    D3D11_BLEND_DESC blendDesc{};auto& blendTarget=blendDesc.RenderTarget[0];
    blendTarget.BlendEnable=TRUE;blendTarget.SrcBlend=D3D11_BLEND_SRC_ALPHA;
    blendTarget.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;blendTarget.BlendOp=D3D11_BLEND_OP_ADD;
    blendTarget.SrcBlendAlpha=D3D11_BLEND_ONE;blendTarget.DestBlendAlpha=D3D11_BLEND_ZERO;
    blendTarget.BlendOpAlpha=D3D11_BLEND_OP_ADD;blendTarget.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11BlendState* blend=nullptr;if(SUCCEEDED(hr))hr=device->CreateBlendState(&blendDesc,&blend);
    Check(SUCCEEDED(hr),"GUI canvas test resources");
    auto& renderer=fixdepth::scapture::stageCanvasRenderer;
    float delta[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for(unsigned variant=0;SUCCEEDED(hr)&&variant<2;++variant){
      delta[12]=variant?.5f:0.f;
      const float background[4]={0,0,1,1};
      context->ClearRenderTargetView(rtv,background);
      const bool drawn=renderer.draw(context,target,source,desc.Format,blend,background,~0u,delta);
      context->CopyResource(staging,target);
      D3D11_MAPPED_SUBRESOURCE mapped{};hr=context->Map(staging,0,D3D11_MAP_READ,0,&mapped);
      if(SUCCEEDED(hr)){
        const auto* row=static_cast<const uint8_t*>(mapped.pData)+(desc.Height/2)*mapped.RowPitch;
        const auto* outside=row+4*(desc.Width/8);
        const auto* inside=row+4*(desc.Width*(variant?5:3)/8);
        Check(drawn&&outside[0]==0&&outside[2]==255&&inside[0]>=127&&inside[0]<=129&&inside[2]>=126&&inside[2]<=128,
          variant?"GUI canvas moves with clip transform":"GUI canvas leaves transparent pixels intact and blends straight alpha");
        context->Unmap(staging,0);
      }else Check(false,"GUI canvas readback");
    }
    renderer.clear();
    if(blend)blend->Release();if(source)source->Release();if(canvas)canvas->Release();
  }
  context->ClearState();
  for(auto* b:{cb,perView})if(b)b->Release();
  if(vs)vs->Release();if(ps)ps->Release();if(raster)raster->Release();
  if(rtv)rtv->Release();if(target)target->Release();if(staging)staging->Release();
  if(depthState)depthState->Release();if(guiSrv)guiSrv->Release();if(gui)gui->Release();
  if(state.ctx1)state.ctx1->Release();state.dev=oldDev;state.ctx=oldCtx;state.ctx1=oldCtx1;
  context->Release();device->Release();FreeLibrary(compiler);FreeLibrary(module);
  PrintSummary();return g_fail?1:0;
}
