// Single-frame GUI replay experiment, included inside scapture. Clone the
// bound camera/effect constants so DCS's original draw keeps its own view.
bool GuiViewProbeWanted() {
  static const bool enabled=GetEnvironmentVariableW(L"DCS_GUI_VIEW_PROBE",nullptr,0)>0;
  return enabled;
}
struct GuiViewProbe {
  static constexpr UINT slots=D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
  ID3D11Buffer* original[slots]={};
  ID3D11Buffer* replacement[slots]={};
  UINT first[slots]={},count[slots]={};
  bool bound=false;
  ~GuiViewProbe() {
    restore();
    for(auto* b:original)if(b)b->Release();
    for(auto* b:replacement)if(b)b->Release();
  }
  void restore() {
    if(bound){g_s.ctx1->VSSetConstantBuffers1(0,slots,original,first,count);bound=false;}
  }
  bool read(ID3D11Buffer* buffer,std::vector<uint8_t>& bytes) {
    D3D11_BUFFER_DESC d{};buffer->GetDesc(&d);
    d.Usage=D3D11_USAGE_STAGING;d.BindFlags=d.MiscFlags=0;
    d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.StructureByteStride=0;
    ID3D11Buffer* staging=nullptr;
    HRESULT hr=g_s.dev->CreateBuffer(&d,nullptr,&staging);
    if(FAILED(hr)){symtr::Line("GUI VIEW buffer staging failed %08x",unsigned(hr));return false;}
    g_s.ctx->CopyResource(staging,buffer);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr=g_s.ctx->Map(staging,0,D3D11_MAP_READ,0,&mapped);
    if(SUCCEEDED(hr)){
      bytes.assign(static_cast<const uint8_t*>(mapped.pData),static_cast<const uint8_t*>(mapped.pData)+d.ByteWidth);
      g_s.ctx->Unmap(staging,0);
    }else symtr::Line("GUI VIEW buffer map failed %08x",unsigned(hr));
    staging->Release();return SUCCEEDED(hr);
  }
  static ckipd::M4 transpose(const ckipd::M4& m) {
    ckipd::M4 t{};
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)t.a[r][c]=m.a[c][r];
    return t;
  }
  bool begin() {
    Scope scope;
    if(!g_s.ctx1){symtr::Line("GUI VIEW needs context1 constant ranges");return false;}
    g_s.ctx1->VSGetConstantBuffers1(0,slots,original,first,count);
    std::vector<uint8_t> bytes[slots];
    for(UINT slot=0;slot<slots;++slot)if(original[slot]&&!read(original[slot],bytes[slot]))return false;
    const UINT cameraBase=first[7]*16;
    if(bytes[7].size()<size_t(cameraBase)+ckipd::kCbBytes){
      symtr::Line("GUI VIEW missing cPerView at VS b7 bytes %zu base %u",bytes[7].size(),cameraBase);return false;
    }
    auto* camera=bytes[7].data()+cameraBase;
    const auto view=ckipd::LoadM(camera,ckipd::kOffView);
    const auto projection=ckipd::LoadM(camera,ckipd::kOffProj);
    const auto vp=ckipd::LoadM(camera,ckipd::kOffViewProj);
    const auto product=ckipd::MulM(view,projection);
    double error=0,scale=1;
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){
      error=std::max(error,std::fabs(product.a[r][c]-vp.a[r][c]));
      scale=std::max(scale,std::fabs(vp.a[r][c]));
    }
    ckipd::M4 inverse{};
    if(!std::isfinite(error)||error/scale>1e-3||!ckipd::Inv4(vp,&inverse)){
      symtr::Line("GUI VIEW cPerView camera mismatch residual %.9g",error/scale);return false;
    }
    // A known camera-space yaw makes the replacement observable. The effect
    // MVP becomes (MVP * oldVP^-1) * newVP, preserving the original geometry.
    constexpr double yaw=3.14159265358979323846/60.0;
    ckipd::M4 rotation{};for(int i=0;i<4;++i)rotation.a[i][i]=1;
    rotation.a[0][0]=rotation.a[2][2]=std::cos(yaw);
    rotation.a[0][2]=-std::sin(yaw);rotation.a[2][0]=std::sin(yaw);
    const auto newView=ckipd::MulM(view,rotation);
    const auto newVp=ckipd::MulM(newView,projection);
    const auto delta=ckipd::MulM(inverse,newVp);
    const auto viewInverse=ckipd::LoadM(camera,ckipd::kOffViewInv);
    const double eye[]{viewInverse.a[3][0],viewInverse.a[3][1],viewInverse.a[3][2]};
    unsigned matrices=0;
    for(UINT slot=0;slot<slots;++slot){
      if(!original[slot])continue;
      const UINT base=first[slot]*16;
      if(base>=bytes[slot].size())continue;
      auto* data=bytes[slot].data()+base;
      const UINT length=UINT(std::min<size_t>(bytes[slot].size()-base,size_t(count[slot])*16));
      if(slot==7){
        ckipd::M4 newInverse{};ckipd::Inv4(newView,&newInverse);
        ckipd::StoreM(data,ckipd::kOffView,newView);ckipd::StoreM(data,ckipd::kOffViewInv,newInverse);
        ckipd::StoreM(data,ckipd::kOffViewProj,newVp);ckipd::Inv4(newVp,&newInverse);
        ckipd::StoreM(data,ckipd::kOffViewProjInv,newInverse);
      }else{
        const auto found=ckipd::ScanMvps(data,length,inverse,ckipd::MaxAbs3(inverse),eye);
        if(!found.patched)continue;
        for(int i=0;i<found.n;++i){const auto& hit=found.hit[i];if(!hit.usable)continue;
          auto m=ckipd::LoadM(data,hit.off);if(hit.order)m=transpose(m);
          m=ckipd::MulM(m,delta);if(hit.order)m=transpose(m);
          ckipd::StoreM(data,hit.off,m);++matrices;
          symtr::Line("GUI VIEW MVP VS b%u base %u offset %u row_major %d residual %.9g",slot,base,hit.off,hit.order,hit.res);
        }
      }
      D3D11_BUFFER_DESC d{};original[slot]->GetDesc(&d);
      d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
      d.CPUAccessFlags=d.MiscFlags=d.StructureByteStride=0;
      D3D11_SUBRESOURCE_DATA init{bytes[slot].data(),0,0};
      const auto hr=g_s.dev->CreateBuffer(&d,&init,&replacement[slot]);
      if(FAILED(hr)){symtr::Line("GUI VIEW clone failed VS b%u %08x",slot,unsigned(hr));return false;}
    }
    ID3D11Buffer* bindings[slots]={};
    for(UINT slot=0;slot<slots;++slot)bindings[slot]=replacement[slot]?replacement[slot]:original[slot];
    g_s.ctx1->VSSetConstantBuffers1(0,slots,bindings,first,count);bound=true;
    symtr::Line("GUI VIEW replay yaw_degrees 3 effect_matrices %u camera_residual %.9g",matrices,error/scale);
    return true;
  }
};
