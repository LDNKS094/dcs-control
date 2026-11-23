// Single-frame delayed GUI draw experiment. Freeze the inputs the known GUI
// draw consumes, record the original call, and execute after the frame's passes.
bool GuiDeferredProbeWanted() {
  static const bool enabled=GetEnvironmentVariableW(L"DCS_GUI_DEFERRED_PROBE",nullptr,0)>0;
  return enabled;
}
struct GuiDeferredProbe {
  ID3D11DeviceContext* context=nullptr;
  ID3D11CommandList* commands=nullptr;
  ID3D11Texture2D* image=nullptr;
  std::vector<IUnknown*> held;
  std::map<ID3D11Buffer*,ID3D11Buffer*> buffers;
  uint64_t nativeFrame=0;int nativeView=0;bool yaw=false;
  ID3D11Buffer* matrixSource=nullptr; // Owned by held; original frozen VS b0.
  UINT matrixFirst=0,matrixCount=0;
  DXGI_FORMAT renderFormat=DXGI_FORMAT_UNKNOWN;
  ~GuiDeferredProbe(){
    if(context)context->Release();if(commands)commands->Release();if(image)image->Release();
    for(auto* object:held)object->Release();
  }
  bool buffer(ID3D11Buffer*& source) {
    if(!source)return true;
    auto prior=buffers.find(source);
    if(prior!=buffers.end()){source=prior->second;return true;}
    D3D11_BUFFER_DESC d{};source->GetDesc(&d);
    d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;
    ID3D11Buffer* copy=nullptr;const auto hr=g_s.dev->CreateBuffer(&d,nullptr,&copy);
    if(FAILED(hr)){symtr::Line("GUI DEFER buffer clone failed %08x",unsigned(hr));return false;}
    g_s.ctx->CopyResource(copy,source);buffers[source]=copy;held.push_back(copy);source=copy;return true;
  }
  bool constants(bool vertex,ID3D11DeviceContext1* target) {
    constexpr UINT n=D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
    ID3D11Buffer* originals[n]={};ID3D11Buffer* copies[n]={};UINT first[n]={},count[n]={};
    if(vertex)g_s.ctx1->VSGetConstantBuffers1(0,n,originals,first,count);
    else g_s.ctx1->PSGetConstantBuffers1(0,n,originals,first,count);
    bool ok=true;
    for(UINT i=0;i<n;++i){copies[i]=originals[i];if(!buffer(copies[i]))ok=false;}
    if(vertex){matrixSource=copies[0];matrixFirst=first[0];matrixCount=count[0];}
    if(ok){if(vertex)target->VSSetConstantBuffers1(0,n,copies,first,count);
      else target->PSSetConstantBuffers1(0,n,copies,first,count);}
    for(auto* object:originals)if(object)object->Release();return ok;
  }
  bool resources(bool vertex) {
    constexpr UINT n=D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
    ID3D11ShaderResourceView* sources[n]={};
    if(vertex)g_s.ctx->VSGetShaderResources(0,n,sources);else g_s.ctx->PSGetShaderResources(0,n,sources);
    bool ok=true;
    if(!vertex&&sources[0]){
      // The verified OverlayLDR shader samples the GUI at PS t0. Later
      // GUIPass rewrites that texture, so retaining its COM pointer is insufficient.
      ID3D11Resource* resource=nullptr;sources[0]->GetResource(&resource);
      ID3D11Texture2D* texture=nullptr;
      const auto query=resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));
      resource->Release();ID3D11Texture2D* copy=nullptr;ID3D11ShaderResourceView* srv=nullptr;
      HRESULT hr=query;
      if(SUCCEEDED(hr)){
        D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.Usage=D3D11_USAGE_DEFAULT;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.CPUAccessFlags=d.MiscFlags=0;
        hr=g_s.dev->CreateTexture2D(&d,nullptr,&copy);
        if(SUCCEEDED(hr)){
          g_s.ctx->CopyResource(copy,texture);D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sources[0]->GetDesc(&sd);
          hr=g_s.dev->CreateShaderResourceView(copy,&sd,&srv);
        }
      }
      if(texture)texture->Release();if(copy)held.push_back(copy);
      if(SUCCEEDED(hr)){sources[0]->Release();sources[0]=srv;}
      else {symtr::Line("GUI DEFER GUI texture clone failed %08x",unsigned(hr));ok=false;}
    }
    if(ok){if(vertex)context->VSSetShaderResources(0,n,sources);else context->PSSetShaderResources(0,n,sources);}
    for(auto* object:sources)if(object)object->Release();return ok;
  }
  bool begin(ID3D11Texture2D* output,bool changed) {
    image=output;image->AddRef();yaw=changed;nativeFrame=frame;nativeView=view;
    HRESULT hr=g_s.dev->CreateDeferredContext(0,&context);
    if(FAILED(hr)){symtr::Line("GUI DEFER context failed %08x",unsigned(hr));return false;}
    ID3D11DeviceContext1* c1=nullptr;hr=context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&c1));
    if(FAILED(hr))return false;
    const bool cbOk=constants(true,c1)&&constants(false,c1);c1->Release();if(!cbOk)return false;
    ID3D11InputLayout* layout=nullptr;g_s.ctx->IAGetInputLayout(&layout);context->IASetInputLayout(layout);if(layout)layout->Release();
    D3D11_PRIMITIVE_TOPOLOGY topology{};g_s.ctx->IAGetPrimitiveTopology(&topology);context->IASetPrimitiveTopology(topology);
    constexpr UINT n=D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
    ID3D11Buffer* vertices[n]={};ID3D11Buffer* originals[n]={};UINT strides[n]={},offsets[n]={};
    g_s.ctx->IAGetVertexBuffers(0,n,originals,strides,offsets);bool vbOk=true;
    for(UINT i=0;i<n;++i){vertices[i]=originals[i];if(!buffer(vertices[i]))vbOk=false;}
    if(vbOk)context->IASetVertexBuffers(0,n,vertices,strides,offsets);
    for(auto* object:originals)if(object)object->Release();if(!vbOk)return false;
    ID3D11Buffer* indices=nullptr;DXGI_FORMAT format{};UINT offset=0;
    g_s.ctx->IAGetIndexBuffer(&indices,&format,&offset);auto* frozen=indices;
    const bool ibOk=buffer(frozen);if(ibOk)context->IASetIndexBuffer(frozen,format,offset);if(indices)indices->Release();if(!ibOk)return false;
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;
    ID3D11ClassInstance* instances[256]={};UINT instanceCount=256;
    g_s.ctx->VSGetShader(&vs,instances,&instanceCount);context->VSSetShader(vs,instances,instanceCount);
    if(vs)vs->Release();for(UINT i=0;i<instanceCount;++i)instances[i]->Release();
    instanceCount=256;g_s.ctx->PSGetShader(&ps,instances,&instanceCount);context->PSSetShader(ps,instances,instanceCount);
    if(ps)ps->Release();for(UINT i=0;i<instanceCount;++i)instances[i]->Release();
    ID3D11GeometryShader* gs=nullptr;ID3D11HullShader* hs=nullptr;ID3D11DomainShader* ds=nullptr;
    g_s.ctx->GSGetShader(&gs,nullptr,nullptr);g_s.ctx->HSGetShader(&hs,nullptr,nullptr);g_s.ctx->DSGetShader(&ds,nullptr,nullptr);
    const bool extraStages=gs||hs||ds;
    if(gs)gs->Release();if(hs)hs->Release();if(ds)ds->Release();
    if(extraStages){symtr::Line("GUI DEFER unexpected GS/HS/DS on selected GUI draw");return false;}
    if(!resources(true)||!resources(false))return false;
    constexpr UINT samplers=D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;ID3D11SamplerState* ss[samplers]={};
    g_s.ctx->VSGetSamplers(0,samplers,ss);context->VSSetSamplers(0,samplers,ss);for(auto* s:ss)if(s)s->Release();
    memset(ss,0,sizeof(ss));g_s.ctx->PSGetSamplers(0,samplers,ss);context->PSSetSamplers(0,samplers,ss);for(auto* s:ss)if(s)s->Release();
    ID3D11RasterizerState* raster=nullptr;g_s.ctx->RSGetState(&raster);context->RSSetState(raster);if(raster)raster->Release();
    constexpr UINT rectangles=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT vp[rectangles]={};D3D11_RECT rect[rectangles]={};UINT nv=rectangles,nr=rectangles;
    g_s.ctx->RSGetViewports(&nv,vp);context->RSSetViewports(nv,vp);g_s.ctx->RSGetScissorRects(&nr,rect);context->RSSetScissorRects(nr,rect);
    ID3D11RenderTargetView* rtv=nullptr;g_s.ctx->OMGetRenderTargets(1,&rtv,nullptr);context->OMSetRenderTargets(1,&rtv,nullptr);
    if(rtv){D3D11_RENDER_TARGET_VIEW_DESC rd{};rtv->GetDesc(&rd);renderFormat=rd.Format;}
    const float clear[4]={};context->ClearRenderTargetView(rtv,clear);if(rtv)rtv->Release();
    ID3D11BlendState* blend=nullptr;FLOAT factors[4]={};UINT mask=0;g_s.ctx->OMGetBlendState(&blend,factors,&mask);
    context->OMSetBlendState(blend,factors,mask);if(blend)blend->Release();
    ID3D11DepthStencilState* depth=nullptr;UINT stencil=0;g_s.ctx->OMGetDepthStencilState(&depth,&stencil);
    D3D11_DEPTH_STENCIL_DESC dd{};if(depth)depth->GetDesc(&dd);
    const bool noDepth=depth&&!dd.DepthEnable&&!dd.StencilEnable;
    context->OMSetDepthStencilState(depth,stencil);if(depth)depth->Release();
    if(!noDepth){symtr::Line("GUI DEFER selected GUI needs depth/stencil input");return false;}
    return true;
  }
  bool finish(){const auto hr=context->FinishCommandList(FALSE,&commands);return SUCCEEDED(hr);}
};
std::vector<GuiDeferredProbe*> guiDeferred;
void ReleaseGuiDeferred(){for(auto* p:guiDeferred)delete p;guiDeferred.clear();}
void ExecuteGuiDeferred(){
  Scope scope;
  for(auto* p:guiDeferred){
    g_s.ctx->ExecuteCommandList(p->commands,TRUE);
    symtr::Capture(p->nativeView,p->yaw?"ui_delayed_yaw_rgba":"ui_delayed_rgba",p->image,0);
    symtr::Line("GUI DEFER executed recorded_frame %llu execution_frame %llu view %d yaw %d frozen_buffers %zu",
      p->nativeFrame,g_s.frame,p->nativeView,int(p->yaw),p->buffers.size());
  }
  ReleaseGuiDeferred();
}
