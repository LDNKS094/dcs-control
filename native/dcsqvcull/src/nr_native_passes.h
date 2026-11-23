namespace nrpass {
bool DelayNativePassExecution(void* pass,uint64_t frame) {
    struct SavedPass { void* pass; uint64_t renderFrame; bool needsCorrectedHdr; };
    static thread_local std::array<std::vector<SavedPass>,2> pending;
    static thread_local uint64_t nativeFrame=~uint64_t(0);
    static thread_local bool wanted=false;
    using Requested=BOOL(__cdecl*)();
    using SetFrame=void(__cdecl*)(unsigned,uint64_t);
    using WritePeripheral=void(__cdecl*)(unsigned,uint64_t,ID3D11DeviceContext*);
    static Requested requested=nullptr;
    static SetFrame setFocusFrame=nullptr;
    static SetFrame setInputFrame=nullptr;
    static WritePeripheral writePeripheral=nullptr;
    static DcsRender::GetSceneStateV1 sceneState=nullptr;
    if(g_s.hmdOrd<0||g_s.hmdOrd>=4||!g_s.ctx)return false;
    if(!writePeripheral){
        const auto module=GetModuleHandleW(L"CheekyFoveatedDLSSRuntime.dll");
        if(!module)return false;
        requested=reinterpret_cast<Requested>(GetProcAddress(module,"DcsJointNr_RequestedV1"));
        setFocusFrame=reinterpret_cast<SetFrame>(GetProcAddress(module,"DcsJointNr_SetFocusFrameV1"));
        setInputFrame=reinterpret_cast<SetFrame>(GetProcAddress(module,"DcsJointNr_SetInputFrameV1"));
        if(!requested||!setFocusFrame||!setInputFrame)return false;
        writePeripheral=reinterpret_cast<WritePeripheral>(GetProcAddress(module,"DcsJointNr_WritePeripheralV1"));
        if(!writePeripheral)return false;
    }
    if(nativeFrame!=g_s.frame){
        if(!pending[0].empty()||!pending[1].empty()){
            Log("JOINT NR native passes incomplete frame=%llu left=%zu right=%zu",
                static_cast<unsigned long long>(nativeFrame),pending[0].size(),pending[1].size());
            pending[0].clear();
            pending[1].clear();
        }
        nativeFrame=g_s.frame;
        if(!sceneState){
            if(const auto adapter=GetModuleHandleW(L"DcsRender.dll"))
                sceneState=reinterpret_cast<DcsRender::GetSceneStateV1>(GetProcAddress(adapter,"DcsRender_GetSceneStateV1"));
        }
        // Menu views can reach CopyToHMD without the focus DLSS pass that drains pending passes.
        wanted=sceneState&&DcsRender::SimulationFrameReady(sceneState())&&requested()!=FALSE;
    }
    if(!wanted)return false;
    char name[48];PassName(pass,name,sizeof(name));
    const unsigned eye=static_cast<unsigned>(g_s.hmdOrd)&1u;
    auto& saved=pending[eye];
    const bool dlss=strcmp(name,"DLSSPass")==0;
    if(g_s.hmdOrd<2){
        if(dlss){
            setInputFrame(eye,nativeFrame);
            ptiming::g_orig(pass,frame);
            setInputFrame(eye,0);
            return true;
        }
        // Keep HUD symbology out of NR and preserve the native HDR pass order.
        const bool needsCorrectedHdr=!strcmp(name,"OverlayHDRPass")||!strcmp(name,"HistogramPass")||
            !strcmp(name,"BloomPass")||!strcmp(name,"ToneMappingPass");
        if(needsCorrectedHdr||!strcmp(name,"PostEffectsPass")||!strcmp(name,"DepthResolverPass2")||
           !strcmp(name,"OverlayLDRPass")||!strcmp(name,"CopyToHMDPass")){
            saved.push_back({pass,frame,needsCorrectedHdr});
            return true;
        }
    }else if(dlss&&!saved.empty()){
        setFocusFrame(eye,nativeFrame);
        ptiming::g_orig(pass,frame);
        setFocusFrame(eye,0);
        // Execute the original pass objects once, while both views still belong to this frame.
        ptiming::t_executeDelayedPass=true;
        bool corrected=false;
        for(const auto& delayed:saved){
            if(delayed.needsCorrectedHdr&&!corrected){
                writePeripheral(eye,nativeFrame,g_s.ctx);
                corrected=true;
            }
            ptiming::g_orig(delayed.pass,delayed.renderFrame);
        }
        ptiming::t_executeDelayedPass=false;
        saved.clear();
        return true;
    }
    return false;
}
} // namespace nrpass
