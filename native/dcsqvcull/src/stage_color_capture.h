// Projected UI inputs and opt-in per-pass capture. Targets are discovered from
// draw/dispatch bindings; transient references and hooks end with each pass.
namespace scapture {
std::atomic<bool> runtimeUiEnabled{false};
std::atomic<bool> runtimeStageEnabled{false};
void EnableUi(bool enabled) {
  runtimeUiEnabled.store(enabled,std::memory_order_release);
}
// Pack frame/view/selection into one atomic so readers observe one boundary token.
std::atomic<uint64_t> token{0};
void PublishToken() { token.store((g_s.frame<<8)|(uint64_t(uint32_t(g_s.hmdOrd)&127)<<1)|(symtr::g_on.load()?1:0)); }
bool CaptureState(uint64_t* f,uint32_t* v,BOOL* selected) {
  if(!f||!v||!selected)return false;
  const uint64_t t=token.load();*f=t>>8;*v=uint32_t((t>>1)&127);*selected=(t&1)?TRUE:FALSE;return t!=0;
}
bool SceneWanted() {
  static const bool enabled=GetEnvironmentVariableW(L"CHEEKY_FG_DATA_DIR",nullptr,0)>0;
  return enabled||runtimeUiEnabled.load(std::memory_order_acquire)||runtimeStageEnabled.load(std::memory_order_acquire);
}
// The post-effects scene before the teaching GUI composite, used by FG and replay.
// These snapshots retain the HUD/MFD symbology already drawn into HDR.
struct SceneImage { ID3D11Texture2D* texture=nullptr;uint64_t frame=~0ull; };
std::mutex sceneMutex;
SceneImage preLdr[4];
SceneImage projectedUi[4];
struct UiCache {
  ID3D11Texture2D* texture=nullptr;ID3D11RenderTargetView* target=nullptr;
  ID3D11BlendState* blend=nullptr;ID3D11DepthStencilState* depth=nullptr;
  D3D11_BLEND_DESC blendDesc{};D3D11_DEPTH_STENCIL_DESC depthDesc{};
  void clear(){if(target)target->Release();if(texture)texture->Release();
    if(blend)blend->Release();if(depth)depth->Release();*this={};}
};
UiCache uiCache[4];
bool sceneOnly=false;
bool uiOnly=false;
bool PreLdrScene(uint64_t expectedFrame,uint32_t v,ID3D11Texture2D** texture) {
  if(!texture||v>3)return false;*texture=nullptr;
  std::lock_guard guard(sceneMutex);const auto& image=preLdr[v];
  if(!image.texture||image.frame!=expectedFrame)return false;
  image.texture->AddRef();*texture=image.texture;return true;
}
bool ProjectedUi(uint64_t expectedFrame,uint32_t v,ID3D11Texture2D** texture) {
  if(!texture||v>3)return false;*texture=nullptr;
  std::lock_guard guard(sceneMutex);const auto& image=projectedUi[v];
  if(!image.texture||image.frame!=expectedFrame)return false;
  image.texture->AddRef();*texture=image.texture;return true;
}
void ReleaseScenes() {
  std::lock_guard guard(sceneMutex);
  for(auto& image:preLdr){if(image.texture)image.texture->Release();image={};}
  for(auto& image:projectedUi){if(image.texture)image.texture->Release();image={};}
  for(auto& cache:uiCache)cache.clear();
}
bool active=false;
thread_local bool own=false;
void* passObject=nullptr;
std::string passName;
bool DepthPass() { return passName=="DepthResolverPass"||passName=="DepthResolverPass2"; }
std::string capturePrefix;
int view=-1;
uint64_t frame=~0ull;
unsigned draws=0,dispatches=0;
unsigned stageOrdinal=0;
size_t frameBytes=0;
struct Image { ID3D11Resource* resource;UINT sub,id; };
std::vector<Image> inputs,outputs;
struct Scope { bool previous;Scope():previous(own){own=true;} ~Scope(){own=previous;} };
bool UiReplayWanted() {
  static const bool enabled=GetEnvironmentVariableW(L"DCS_UI_ALPHA_CAPTURE",nullptr,0)>0;
  return enabled;
}
bool UiFgWanted() {
  static const bool enabled=GetEnvironmentVariableW(L"DCS_UI_FG_CAPTURE",nullptr,0)>0;
  return (enabled&&SceneWanted())||runtimeUiEnabled.load(std::memory_order_acquire);
}
#include "gui_view_probe.h"
#include "gui_stage_canvas.h"
// Temporary single-draw probe. Keep DCS's projection and shader; only redirect
// its output and disable blending to retain the pixel shader's actual RGBA.
struct UiReplayState {
  GuiViewProbe* viewProbe=nullptr;
  GuiCanvasPacket* canvas=nullptr;
  ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]={};
  ID3D11DepthStencilView* depth=nullptr;
  ID3D11BlendState* blend=nullptr;
  ID3D11DepthStencilState* depthState=nullptr;
  ID3D11Texture2D* image=nullptr;
  ID3D11RenderTargetView* output=nullptr;
  ID3D11BlendState* rawBlend=nullptr;
  ID3D11DepthStencilState* readDepth=nullptr;
  FLOAT factors[4]={};UINT mask=0,stencil=0;
  ~UiReplayState() {
    delete viewProbe;
    if(canvas)canvas->Release();
    for(auto* target:targets)if(target)target->Release();
    if(depth)depth->Release();if(blend)blend->Release();if(depthState)depthState->Release();
    if(image)image->Release();if(output)output->Release();
    if(rawBlend)rawBlend->Release();if(readDepth)readDepth->Release();
  }
};
UiReplayState* uiReplay=nullptr;
#include "gui_deferred_probe.h"
#include "gui_stage_replay.h"
template<int Kind,class... Args>
void RecordGuiDraw(ID3D11DeviceContext*,Args... args){
  if(!GuiDeferredProbeWanted()||!uiReplay||!GuiViewProbeWanted()||view!=0||!symtr::g_on.load())return;
  Scope scope;auto* p=new GuiDeferredProbe;
  if(!p->begin(uiReplay->image,uiReplay->viewProbe!=nullptr)){delete p;return;}
  if constexpr(Kind==fdexport::kDkDraw)p->context->Draw(args...);
  else if constexpr(Kind==fdexport::kDkIndexed)p->context->DrawIndexed(args...);
  else if constexpr(Kind==fdexport::kDkInstanced)p->context->DrawInstanced(args...);
  else if constexpr(Kind==fdexport::kDkIndexedInstanced)p->context->DrawIndexedInstanced(args...);
  else {symtr::Line("GUI DEFER selected draw kind %d needs indirect/stream-output input",Kind);delete p;return;}
  if(p->finish()){guiDeferred.push_back(p);symtr::Line("GUI DEFER recorded frame %llu view %d kind %d yaw %d",frame,view,Kind,int(p->yaw));}
  else {symtr::Line("GUI DEFER FinishCommandList failed");delete p;}
}
bool Color(ID3D11Resource*);
bool BeginUiReplay(unsigned variant) {
  const bool viewProbe=GuiViewProbeWanted()&&symtr::g_on.load()&&view==0;
  if(variant&&(!viewProbe||variant!=1))return false;
  const bool stageCanvas=!variant&&StageCanvasWanted()&&view>=0&&view<2;
  const bool paired=stageCanvas||(!viewProbe&&UiFgWanted()&&view>=0&&view<4);
  if((!paired&&(!UiReplayWanted()||!symtr::g_on.load()||view!=0))||!active||own||
     passName!="OverlayLDRPass"||draws!=1)return false;
  auto* c=g_s.ctx;auto* probe=new UiReplayState;
  c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,probe->targets,&probe->depth);
  c->OMGetBlendState(&probe->blend,probe->factors,&probe->mask);
  c->OMGetDepthStencilState(&probe->depthState,&probe->stencil);
  if(!probe->targets[0]){delete probe;return false;}
  ID3D11Resource* resource=nullptr;UINT sub=0;
  symtr::RtvSub(probe->targets[0],&resource,&sub);
  if(!resource||!Color(resource)){if(resource)resource->Release();delete probe;return false;}
  D3D11_TEXTURE2D_DESC td{};static_cast<ID3D11Texture2D*>(resource)->GetDesc(&td);resource->Release();
  D3D11_RENDER_TARGET_VIEW_DESC rd{};probe->targets[0]->GetDesc(&rd);
  const UINT mip=sub%td.MipLevels;
  td.Width=std::max(1u,td.Width>>mip);td.Height=std::max(1u,td.Height>>mip);
  td.MipLevels=td.ArraySize=1;td.Format=rd.Format;td.Usage=D3D11_USAGE_DEFAULT;
  td.BindFlags=D3D11_BIND_RENDER_TARGET;td.CPUAccessFlags=td.MiscFlags=0;
  // Write the shader's encoded RGB through UNORM, then let the diagnostic
  // compositor use an sRGB SRV just like the original eye color. Alpha remains linear.
  if(paired){td.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;td.BindFlags|=D3D11_BIND_SHADER_RESOURCE;}
  D3D11_BLEND_DESC bd{};
  if(probe->blend)probe->blend->GetDesc(&bd);
  else {bd.RenderTarget[0].SrcBlend=bd.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend=bd.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOp=bd.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;}
  const auto& b=bd.RenderTarget[0];
  symtr::Line("UI REPLAY blend enabled %d src %u dst %u op %u src_alpha %u dst_alpha %u op_alpha %u write %u factors %.6f %.6f %.6f %.6f sample_mask %u alpha_to_coverage %d",
    int(b.BlendEnable),unsigned(b.SrcBlend),unsigned(b.DestBlend),unsigned(b.BlendOp),
    unsigned(b.SrcBlendAlpha),unsigned(b.DestBlendAlpha),unsigned(b.BlendOpAlpha),unsigned(b.RenderTargetWriteMask),
    probe->factors[0],probe->factors[1],probe->factors[2],probe->factors[3],probe->mask,int(bd.AlphaToCoverageEnable));
  D3D11_DEPTH_STENCIL_DESC dd{};
  if(probe->depthState)probe->depthState->GetDesc(&dd);
  else {dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=D3D11_COMPARISON_LESS;
    dd.StencilReadMask=dd.StencilWriteMask=D3D11_DEFAULT_STENCIL_READ_MASK;
    dd.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_ALWAYS};
    dd.BackFace=dd.FrontFace;}
  symtr::Line("UI REPLAY depth enabled %d write %u func %u stencil enabled %d write_mask %u ref %u target %ux%u format %u",
    int(dd.DepthEnable),unsigned(dd.DepthWriteMask),unsigned(dd.DepthFunc),int(dd.StencilEnable),
    unsigned(dd.StencilWriteMask),probe->stencil,td.Width,td.Height,unsigned(td.Format));
  // Preserve depth/stencil tests but keep the original scene's DSV unchanged.
  dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;dd.StencilWriteMask=0;
  D3D11_BLEND_DESC raw=bd;raw.AlphaToCoverageEnable=FALSE;raw.IndependentBlendEnable=FALSE;
  raw.RenderTarget[0].BlendEnable=FALSE;raw.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
  HRESULT hr=S_OK;
  D3D11_RENDER_TARGET_VIEW_DESC uiRtv{};
  uiRtv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;uiRtv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
  if(paired){
    if(stageCanvas){
      probe->canvas=AcquireCanvasSlot(view);
      if(!probe->canvas){delete probe;return false;}
    }
    auto& cache=probe->canvas?probe->canvas->cache:uiCache[view];D3D11_TEXTURE2D_DESC old{};
    if(cache.texture)cache.texture->GetDesc(&old);
    if(old.Width!=td.Width||old.Height!=td.Height||old.Format!=td.Format){
      if(probe->canvas)probe->canvas->clear();else cache.clear();
      hr=g_s.dev->CreateTexture2D(&td,nullptr,&cache.texture);
      if(SUCCEEDED(hr))hr=g_s.dev->CreateRenderTargetView(cache.texture,&uiRtv,&cache.target);
      if(SUCCEEDED(hr)&&probe->canvas){
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=uiRtv.Format;srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
        hr=g_s.dev->CreateShaderResourceView(cache.texture,&srv,&probe->canvas->source);
      }
      if(FAILED(hr)){if(probe->canvas)probe->canvas->clear();else cache.clear();}
    }
    if(SUCCEEDED(hr)&&(!cache.blend||memcmp(&raw,&cache.blendDesc,sizeof(raw)))){
      if(cache.blend)cache.blend->Release();cache.blend=nullptr;
      hr=g_s.dev->CreateBlendState(&raw,&cache.blend);cache.blendDesc=raw;
    }
    if(SUCCEEDED(hr)&&(!cache.depth||memcmp(&dd,&cache.depthDesc,sizeof(dd)))){
      if(cache.depth)cache.depth->Release();cache.depth=nullptr;
      hr=g_s.dev->CreateDepthStencilState(&dd,&cache.depth);cache.depthDesc=dd;
    }
    if(SUCCEEDED(hr)){probe->image=cache.texture;probe->output=cache.target;
      probe->rawBlend=cache.blend;probe->readDepth=cache.depth;
      probe->image->AddRef();probe->output->AddRef();probe->rawBlend->AddRef();probe->readDepth->AddRef();}
  }else{
    hr=g_s.dev->CreateTexture2D(&td,nullptr,&probe->image);
    if(SUCCEEDED(hr))hr=g_s.dev->CreateRenderTargetView(probe->image,nullptr,&probe->output);
    if(SUCCEEDED(hr))hr=g_s.dev->CreateBlendState(&raw,&probe->rawBlend);
    if(SUCCEEDED(hr))hr=g_s.dev->CreateDepthStencilState(&dd,&probe->readDepth);
  }
  if(FAILED(hr)){symtr::Line("UI REPLAY failed HRESULT %08x",unsigned(hr));delete probe;return false;}
  if(variant){
    probe->viewProbe=new GuiViewProbe;
    if(!probe->viewProbe->begin()){delete probe;return false;}
  }
  uiReplay=probe;own=true;
  const FLOAT clear[4]={};c->ClearRenderTargetView(probe->output,clear);
  c->OMSetRenderTargets(1,&probe->output,probe->depth);
  c->OMSetBlendState(probe->rawBlend,probe->factors,probe->mask);
  c->OMSetDepthStencilState(probe->readDepth,probe->stencil);
  return true;
}
void EndUiReplay() {
  auto* probe=uiReplay;auto* c=g_s.ctx;
  if(probe->viewProbe)probe->viewProbe->restore();
  c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,probe->targets,probe->depth);
  c->OMSetBlendState(probe->blend,probe->factors,probe->mask);
  c->OMSetDepthStencilState(probe->depthState,probe->stencil);
  if(probe->canvas){
    auto* canvas=probe->canvas;
    D3D11_RENDER_TARGET_VIEW_DESC desc{};probe->targets[0]->GetDesc(&desc);canvas->format=desc.Format;
    if(canvas->blend)canvas->blend->Release();
    canvas->blend=probe->blend;if(canvas->blend)canvas->blend->AddRef();
    memcpy(canvas->factors,probe->factors,sizeof(canvas->factors));canvas->mask=probe->mask;
    PublishStagePacket(canvas,&GuiCanvasPacket::Replay);
    static unsigned published{};
    if(published++%240==0)Log("stage canvas: projected OverlayLDR frame=%llu eye=%d",frame,view);
  } else if(UiFgWanted()&&!GuiViewProbeWanted()) {
    // Quad copies this same-frame source on the immediate context before its
    // next overwrite. Cross-frame FG leases own the compositor's private output.
    std::lock_guard guard(sceneMutex);auto& image=projectedUi[view];
    if(image.texture)image.texture->Release();
    image={probe->image,frame};probe->image=nullptr;
  } else {
    symtr::Capture(view,probe->viewProbe?"ui_replayed_yaw_rgba":"ui_projected_rgba",probe->image,0);
    symtr::Line("UI REPLAY captured frame %llu view %d",frame,view);
  }
  own=false;uiReplay=nullptr;delete probe;
}
std::map<std::pair<int,std::string>,std::vector<Image>> learned;
std::vector<Image> candidates;
void Drop(std::vector<Image>& images) { for(auto& e:images)e.resource->Release();images.clear(); }
void ReleaseLearned() { for(auto& p:learned)Drop(p.second);learned.clear(); }
void Remember(int v,const std::string& name,ID3D11Resource* r,UINT sub) {
  auto& list=learned[{v,name}];for(auto& e:list)if(e.resource==r&&e.sub==sub)return;
  r->AddRef();list.push_back({r,sub,UINT(list.size())});
}
bool Color(ID3D11Resource* r) {
  if(!r)return false;
  D3D11_RESOURCE_DIMENSION dim{};r->GetType(&dim);
  if(dim!=D3D11_RESOURCE_DIMENSION_TEXTURE2D)return false;
  D3D11_TEXTURE2D_DESC d{};static_cast<ID3D11Texture2D*>(r)->GetDesc(&d);
  switch(d.Format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:case DXGI_FORMAT_R16G16B16A16_FLOAT:case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R10G10B10A2_UNORM:case DXGI_FORMAT_R10G10B10A2_TYPELESS:case DXGI_FORMAT_R11G11B10_FLOAT:return true;
    default:return false;
  }
}
void SnapshotScene(ID3D11Resource* resource,UINT sub,int v,uint64_t f) {
  if(!SceneWanted()||v<0||v>3||!resource||!g_s.ctx||!Color(resource))return;
  LARGE_INTEGER start{};
  const bool measure=runtimeStageEnabled.load(std::memory_order_relaxed);
  if(measure)QueryPerformanceCounter(&start);
  D3D11_TEXTURE2D_DESC desc{};static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
  const UINT mip=desc.MipLevels?sub%desc.MipLevels:0;
  desc.Width=std::max(1u,desc.Width>>mip);desc.Height=std::max(1u,desc.Height>>mip);
  desc.MipLevels=desc.ArraySize=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;
  // PostEffects stores encoded eye color in UNORM. Mark its snapshot as sRGB
  // so the quad compositor decodes it before writing its sRGB output.
  if(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM)desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=0;
  std::lock_guard guard(sceneMutex);auto& image=preLdr[v];
  if(image.texture) {
    D3D11_TEXTURE2D_DESC before{};image.texture->GetDesc(&before);
    if(before.Width!=desc.Width||before.Height!=desc.Height||before.Format!=desc.Format) {
      image.texture->Release();image.texture=nullptr;
    }
  }
  if(!image.texture&&FAILED(g_s.dev->CreateTexture2D(&desc,nullptr,&image.texture)))return;
  Scope scope;g_s.ctx->CopySubresourceRegion(image.texture,0,0,0,0,resource,sub,nullptr);image.frame=f;
  if(measure){
    LARGE_INTEGER end{},frequency{};
    QueryPerformanceCounter(&end);QueryPerformanceFrequency(&frequency);
    static double totalMs=0;
    static unsigned samples=0;
    totalMs+=1000.0*double(end.QuadPart-start.QuadPart)/double(frequency.QuadPart);
    if(++samples==480){
      Log("stage replay timing: view_samples=%u scene_copy_cpu_ms=%.3f",samples,totalMs/samples);
      samples=0;totalMs=0;
    }
  }
}
void Capture(const Image& image,const char* role) {
  if(!symtr::g_on.load())return;
  if(!Color(image.resource)&&!DepthPass())return;
  D3D11_RESOURCE_DIMENSION dim{};image.resource->GetType(&dim);
  if(dim!=D3D11_RESOURCE_DIMENSION_TEXTURE2D)return;
  D3D11_TEXTURE2D_DESC d{};static_cast<ID3D11Texture2D*>(image.resource)->GetDesc(&d);
  const UINT mip=d.MipLevels?image.sub%d.MipLevels:0;
  const size_t bytes=size_t(std::max(1u,d.Width>>mip))*std::max(1u,d.Height>>mip)*symtr::BytesPerTexel(d.Format);
  const std::string tag=capturePrefix+"_"+role+std::to_string(image.id);
  if(frameBytes+bytes>symtr::kMaxQueuedBytes) {
    ++symtr::g_skipped;symtr::Line("STAGE SKIPPED %d %s cumulative_budget %zu",view,tag.c_str(),bytes);return;
  }
  Scope scope;
  if(symtr::Capture(view,tag.c_str(),image.resource,image.sub))frameBytes+=bytes;
}
void Bind(std::vector<Image>& list,ID3D11Resource* r,UINT sub,const char* role,UINT slot,bool output) {
  if(!r)return;
  if(output)Remember(view,passName,r,sub);
  if(passName=="ToneMappingPass"&&!output&&slot==0&&std::strcmp(role,"PS_SRV")==0) {
    auto& hdr=learned[{view,"HDR"}];
    if(symtr::g_on.load()) {
      const bool same=hdr.size()==1&&hdr[0].resource==r&&hdr[0].sub==sub;
      symtr::Line("STAGE HDR VALIDATION view %d frame %llu valid %d actual %s",view,frame,int(same),symtr::Describe(r,sub).c_str());
    }
    Drop(hdr);Remember(view,"HDR",r,sub);
  }
  for(const auto& e:candidates)if(e.resource==r&&e.sub==sub&&output)
    symtr::Line("STAGE CANDIDATE VALID view %d pass %s frame %llu resource %p sub %u",view,passName.c_str(),frame,r,sub);
  symtr::Line("STAGE BIND %d %s %s %u %s",view,passName.c_str(),role,slot,symtr::Describe(r,sub).c_str());
  for(const Image& e:list)if(e.resource==r&&e.sub==sub)return;
  r->AddRef();list.push_back({r,sub,UINT(list.size())});
  Capture(list.back(),output?"before":"input");
}
UINT UavSub(ID3D11UnorderedAccessView* v,ID3D11Resource** r) {
  v->GetResource(r);D3D11_UNORDERED_ACCESS_VIEW_DESC d{};v->GetDesc(&d);
  D3D11_RESOURCE_DIMENSION dim{};if(*r)(*r)->GetType(&dim);
  if(dim!=D3D11_RESOURCE_DIMENSION_TEXTURE2D)return 0;
  D3D11_TEXTURE2D_DESC td{};static_cast<ID3D11Texture2D*>(*r)->GetDesc(&td);
  if(d.ViewDimension==D3D11_UAV_DIMENSION_TEXTURE2DARRAY) {
    if(d.Texture2DArray.ArraySize>1)symtr::Line("STAGE GAP %d %s UAV array first_slice %u count %u; first subresource image only",view,passName.c_str(),d.Texture2DArray.FirstArraySlice,d.Texture2DArray.ArraySize);
    return D3D11CalcSubresource(d.Texture2DArray.MipSlice,d.Texture2DArray.FirstArraySlice,td.MipLevels);
  }
  return d.Texture2D.MipSlice;
}
void Observe(bool compute) {
  if(!active||own||!g_s.ctx||GetCurrentThreadId()!=ptiming::g_topTid.load())return;
  if(uiOnly){if(compute)++dispatches;else ++draws;return;}
  if(sceneOnly) {
    if(compute||draws++)return;
    ID3D11RenderTargetView* target=nullptr;g_s.ctx->OMGetRenderTargets(1,&target,nullptr);
    if(target){ID3D11Resource* resource=nullptr;UINT sub=0;symtr::RtvSub(target,&resource,&sub);
      if(resource&&Color(resource))outputs.push_back({resource,sub,0});else if(resource)resource->Release();target->Release();}
    return;
  }
  if(compute)++dispatches;else ++draws;
  ID3D11PixelShader* ps=nullptr;ID3D11ComputeShader* cs=nullptr;
  if(compute)g_s.ctx->CSGetShader(&cs,nullptr,nullptr);else g_s.ctx->PSGetShader(&ps,nullptr,nullptr);
  symtr::Line("STAGE CALL %d %s %s %u PS %p CS %p",view,passName.c_str(),compute?"dispatch":"draw",compute?dispatches:draws,ps,cs);
  if(ps)ps->Release();if(cs)cs->Release();
  ID3D11ShaderResourceView* srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]={};
  if(compute)g_s.ctx->CSGetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,srvs);
  else g_s.ctx->PSGetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,srvs);
  for(UINT i=0;i<D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;++i)if(srvs[i]) {
    ID3D11Resource* r=nullptr;UINT sub=0;symtr::SrvSub(srvs[i],&r,&sub);
    D3D11_SHADER_RESOURCE_VIEW_DESC d{};srvs[i]->GetDesc(&d);
    symtr::Line("STAGE SRV %d %s slot %u dim %u view_format %u",view,passName.c_str(),i,unsigned(d.ViewDimension),unsigned(d.Format));
    if(d.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2DARRAY&&d.Texture2DArray.ArraySize>1)
      symtr::Line("STAGE GAP %d %s input array first_slice %u count %u; first subresource image only",view,passName.c_str(),d.Texture2DArray.FirstArraySlice,d.Texture2DArray.ArraySize);
    Bind(inputs,r,sub,compute?"CS_SRV":"PS_SRV",i,false);if(r)r->Release();srvs[i]->Release();
  }
  if(!compute) {
    ID3D11RenderTargetView* rt[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]={};
    g_s.ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,rt,nullptr);
    for(UINT i=0;i<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++i)if(rt[i]) {
      ID3D11Resource* r=nullptr;UINT sub=0;symtr::RtvSub(rt[i],&r,&sub);
      D3D11_RENDER_TARGET_VIEW_DESC d{};rt[i]->GetDesc(&d);
      if(d.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2DARRAY&&d.Texture2DArray.ArraySize>1)
        symtr::Line("STAGE GAP %d %s RTV array first_slice %u count %u; first subresource image only",view,passName.c_str(),d.Texture2DArray.FirstArraySlice,d.Texture2DArray.ArraySize);
      Bind(outputs,r,sub,"RTV",i,true);if(r)r->Release();rt[i]->Release();
    }
    D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]={};UINT count=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    g_s.ctx->RSGetViewports(&count,vp);
    for(UINT i=0;i<count;++i)symtr::Line("STAGE VP %d %s %u %.1f %.1f %.1f %.1f",view,passName.c_str(),i,vp[i].TopLeftX,vp[i].TopLeftY,vp[i].Width,vp[i].Height);
  }
  ID3D11UnorderedAccessView* uavs[D3D11_PS_CS_UAV_REGISTER_COUNT]={};
  if(compute)g_s.ctx->CSGetUnorderedAccessViews(0,D3D11_PS_CS_UAV_REGISTER_COUNT,uavs);
  else g_s.ctx->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,D3D11_PS_CS_UAV_REGISTER_COUNT,uavs);
  for(UINT i=0;i<D3D11_PS_CS_UAV_REGISTER_COUNT;++i)if(uavs[i]) {
    ID3D11Resource* r=nullptr;const UINT sub=UavSub(uavs[i],&r);
    Bind(outputs,r,sub,compute?"CS_UAV":"OM_UAV",i,true);if(r)r->Release();uavs[i]->Release();
  }
}
int dispatchSlot[2]={-1,-1};void* dispatchOriginal[2]={};
void* DispatchHook(int);
void Rearm() {
  if(!active||!g_armTable||!g_rearmOk.load())return;
  for(int i=0;i<2;++i)if(dispatchSlot[i]>=0&&g_armTable[dispatchSlot[i]]!=DispatchHook(i)) {
    dispatchOriginal[i]=g_armTable[dispatchSlot[i]];FdWriteSlot(&g_armTable[dispatchSlot[i]],DispatchHook(i));
  }
}
void STDMETHODCALLTYPE Dispatch(ID3D11DeviceContext* c,UINT x,UINT y,UINT z) {
  HookBusy busy;const auto fn=reinterpret_cast<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT)>(dispatchOriginal[0]);
  if(c==g_s.ctx)Observe(true);fn(c,x,y,z);if(c==g_s.ctx)InPassRearm();
}
void STDMETHODCALLTYPE DispatchIndirect(ID3D11DeviceContext* c,ID3D11Buffer* args,UINT offset) {
  HookBusy busy;const auto fn=reinterpret_cast<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT)>(dispatchOriginal[1]);
  if(c==g_s.ctx)Observe(true);fn(c,args,offset);if(c==g_s.ctx)InPassRearm();
}
void* DispatchHook(int i) { return i?reinterpret_cast<void*>(&DispatchIndirect):reinterpret_cast<void*>(&Dispatch); }
bool Lost(void** vt) {
  if(!active||vt!=g_armTable)return false;
  for(int i=0;i<2;++i)if(dispatchSlot[i]>=0&&vt[dispatchSlot[i]]!=DispatchHook(i))return true;
  return false;
}
void Close(bool save) {
  if(active) {
    if(save&&passName=="PostEffectsPass"&&!outputs.empty())SnapshotScene(outputs[0].resource,outputs[0].sub,view,frame);
    unsigned present=0;
    if(g_armTable)for(int i=0;i<fdexport::kDkCount;++i)if(g_dSlot[i]>=0&&g_armTable[g_dSlot[i]]==DrawHookOf(i))++present;
    symtr::Line("STAGE END %d %s draws %u dispatches %u inputs %zu outputs %zu draw_hooks_present %u dispatch_hooks_lost %d",
      view,passName.c_str(),draws,dispatches,inputs.size(),outputs.size(),present,int(Lost(g_armTable)));
    // Stop before staging readback work can enter the observed draw path.
    active=false;
    for(int i=0;i<2;++i)if(g_armTable&&dispatchSlot[i]>=0&&g_armTable[dispatchSlot[i]]==DispatchHook(i))FdWriteSlot(&g_armTable[dispatchSlot[i]],dispatchOriginal[i]);
    DisarmUsOnly();
    if(save) {
      for(const Image& e:outputs)Capture(e,"after");
      if(DepthPass())for(const Image& e:inputs)Capture(e,"depth_source_after");
      for(const Image& e:candidates)Capture(e,"boundary_after_candidate");
      if(outputs.empty())symtr::Line("STAGE MISSING %d %s actual_output_binding; candidate images require validation",view,passName.c_str());
    }
  }
  for(const Image& e:inputs)e.resource->Release();inputs.clear();
  for(const Image& e:outputs)e.resource->Release();outputs.clear();
  Drop(candidates);
  passObject=nullptr;passName.clear();view=-1;
  sceneOnly=false;
  uiOnly=false;
}
void Boundary(void* p,bool begin) {
  if(!begin) { if(p==passObject)Close();return; }
  static bool wasRuntimeUi=false;
  const bool runtimeUi=runtimeUiEnabled.load(std::memory_order_acquire);
  if(wasRuntimeUi&&!runtimeUi&&!active)ReleaseScenes();
  wasRuntimeUi=runtimeUi;
  if(!symtr::g_learn&&!SceneWanted())return;
  char name[48];PassName(p,name,sizeof(name));
  const bool scenePass=SceneWanted()&&strcmp(name,"PostEffectsPass")==0;
  const bool uiPass=(UiFgWanted()||runtimeStageEnabled.load(std::memory_order_acquire))&&strcmp(name,"OverlayLDRPass")==0;
  if(!symtr::g_learn&&!scenePass&&!uiPass)return;
  if(strcmp(name,"OverlayHDRPass")&&strcmp(name,"ToneMappingPass")&&strcmp(name,"PostEffectsPass")&&
     strcmp(name,"OverlayLDRPass")&&strcmp(name,"CopyToHMDPass")&&strcmp(name,"GUIPass")&&
     strcmp(name,"BloomPass")&&strcmp(name,"DepthResolverPass")&&strcmp(name,"DepthResolverPass2"))return;
  if(active) { symtr::Line("STAGE GAP nested selected pass %s",name);return; }
  if(!g_s.ctx||g_s.hmdOrd<0||g_s.hmdOrd>4)return;
  if(UiReplayWanted()&&!UiFgWanted()&&(g_s.hmdOrd!=0||strcmp(name,"OverlayLDRPass")))return;
  if(frame!=g_s.frame) { frame=g_s.frame;frameBytes=0;stageOrdinal=0; }
  passName=name;passObject=p;view=g_s.hmdOrd;draws=dispatches=0;
  sceneOnly=scenePass&&!symtr::g_learn;
  uiOnly=uiPass&&!symtr::g_learn;
  capturePrefix=passName+"_s"+std::to_string(++stageOrdinal);
  LARGE_INTEGER clock{};QueryPerformanceCounter(&clock);
  symtr::Line("STAGE BEGIN %d %s frame %llu qpc %lld image_prefix %s; boundary candidates validated against actual bindings",view,name,frame,clock.QuadPart,capturePrefix.c_str());
  const bool hdrStage=passName=="OverlayHDRPass"||passName=="BloomPass";
  const auto prior=learned.find({view,hdrStage?"HDR":passName});
  if(prior!=learned.end())for(const auto& e:prior->second) {
    e.resource->AddRef();candidates.push_back(e);Capture(e,"boundary_before_candidate");
    symtr::Line("STAGE CANDIDATE %d %s %s",view,name,symtr::Describe(e.resource,e.sub).c_str());
  }
  else symtr::Line("STAGE MISSING %d %s prior_resource",view,name);
  if(DepthPass()) {
    ID3D11DepthStencilView* dsv=nullptr;g_s.ctx->OMGetRenderTargets(0,nullptr,&dsv);
    if(dsv) {
      ID3D11Resource* depth=nullptr;dsv->GetResource(&depth);
      D3D11_DEPTH_STENCIL_VIEW_DESC dd{};dsv->GetDesc(&dd);
      D3D11_TEXTURE2D_DESC td{};static_cast<ID3D11Texture2D*>(depth)->GetDesc(&td);
      const UINT sub=dd.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DARRAY?D3D11CalcSubresource(dd.Texture2DArray.MipSlice,dd.Texture2DArray.FirstArraySlice,td.MipLevels):dd.Texture2D.MipSlice;
      Bind(inputs,depth,sub,"boundary_DSV_source",0,false);depth->Release();dsv->Release();
    } else symtr::Line("STAGE MISSING %d %s bound_depth",view,name);
  }
  if(!symtr::g_on.load())Drop(learned[{view,passName}]);
  if(g_armTable||ForeignTableHooks()||!UsSlots()) { symtr::Line("STAGE GAP %d %s context arm unavailable",view,name);Close();return; }
  void** vt=*reinterpret_cast<void***>(g_s.ctx);
  if(!g_usNormal||UsToggled(vt))NormalizeTable(vt);
  if(!g_usNormal)NormalizeTable(vt);
  // Reuse the existing short upload/draw arm and its normal-entry repair.
  if(!ArmUsOnly(true)) { symtr::Line("STAGE GAP %d %s short arm failed",view,name);Close();return; }
  dispatchSlot[0]=DcsQv_ContextSlot("Dispatch");dispatchSlot[1]=DcsQv_ContextSlot("DispatchIndirect");
  active=true;Rearm();
}
} // namespace scapture
