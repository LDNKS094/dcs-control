#include "scene_events.h"
#include "release_policy.h"
#include <atomic>
#include <unordered_map>
#include <Windows.h>

namespace {
enum LifecycleEvent : uint32_t { Created = 1, Evaluated = 2, Released = 3 };
using LifecycleCallback = void (*)(uint32_t, uint64_t);
using RegisterCallback = uint32_t (*)(LifecycleCallback);
using RequestRelease = uint32_t (*)(uint64_t);
using PauseFrameGeneration = void (*)(bool);

std::atomic<uint64_t> scene{0};
RequestRelease requestRelease = nullptr;
PauseFrameGeneration pauseFrameGeneration = nullptr;
bool registered = false;
unsigned requiredViews = 1;
struct Feature {
  uint32_t createdGeneration = 0;
  uint32_t evaluatedGeneration = 0;
};
// Only the backend's serialized lifecycle callback accesses feature records.
std::unordered_map<uint64_t, Feature> features;
uint32_t reclaimedGeneration = 0;

void OnFeature(uint32_t event, uint64_t handle) {
  const auto state = scene.load(std::memory_order_acquire);
  const auto generation = static_cast<uint32_t>(state >> 32);
  switch (event) {
    case Created:
      features[handle] = Feature{generation, 0};
      break;
    case Released:
      features.erase(handle);
      break;
    case Evaluated: {
      if (!DcsRender::SimulationFrameReady(state)) break;
      features.at(handle).evaluatedGeneration = generation;
      if (reclaimedGeneration == generation) break;
      unsigned evaluatedViews = 0;
      for (const auto& [id, feature] : features)
        evaluatedViews += feature.evaluatedGeneration == generation;
      if (evaluatedViews < requiredViews) break;

      // Stereo waits for both eyes. Only reclaim handles observed in a prior mission on that path.
      for (const auto& [id, feature] : features) {
        if (feature.createdGeneration < generation && feature.evaluatedGeneration != generation &&
            (requiredViews == 1 || feature.evaluatedGeneration != 0))
          requestRelease(id);
      }
      reclaimedGeneration = generation;
      break;
    }
  }
}
void Publish() {
  const auto state = scene.load(std::memory_order_acquire);
  if (pauseFrameGeneration) pauseFrameGeneration(state != 0 && !DcsRender::SimulationFrameReady(state));
}
void AddFlag(uint32_t flag) {
  scene.fetch_or(flag, std::memory_order_release);
  Publish();
}

bool Connect(HMODULE backend, const char* registration, const char* release, unsigned views) {
  if (!backend) return false;
  const auto registerCallback = reinterpret_cast<RegisterCallback>(GetProcAddress(backend, registration));
  const auto request = reinterpret_cast<RequestRelease>(GetProcAddress(backend, release));
  if (!registerCallback || !request) return false;

  requiredViews = views;
  requestRelease = request;
  // The backend retains OnFeature after Lua releases its library reference.
  HMODULE pinned = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                    reinterpret_cast<LPCWSTR>(&OnFeature), &pinned);
  if (registerCallback(&OnFeature) != 1) {
    requestRelease = nullptr;
    return false;
  }
  return true;
}

void ConnectBackend() {
  if (registered) return;
  const auto dxgi = GetModuleHandleW(L"dxgi.dll");
  if (Connect(dxgi, "OptiScaler_RegisterReleaseLifecycleV2", "OptiScaler_RequestReleaseV2", 1)) {
    pauseFrameGeneration = reinterpret_cast<PauseFrameGeneration>(GetProcAddress(dxgi, "OptiScaler_SetFrameGenerationPausedV1"));
    registered = true;
  } else {
    registered = Connect(GetModuleHandleW(L"CheekyFoveatedDLSSRuntime.dll"),
                         "Cheeky_RegisterReleaseLifecycleV1", "Cheeky_RequestReleaseV1", 2);
  }
  Publish();
}
}

extern "C" __declspec(dllexport) int DcsRender_Initialize(void*) {
  ConnectBackend();
  return 0;
}

// Expose event ordering to the native integration test without coupling the backend to DCS flags.
extern "C" __declspec(dllexport) uint64_t DcsRender_GetSceneStateV1() {
  return scene.load(std::memory_order_acquire);
}

extern "C" __declspec(dllexport) uint32_t DcsRender_DecideNativeReleaseV1(
    uint32_t liveViews, uint64_t evaluations, uint64_t minimum, bool settled,
    bool superseded, bool reused) {
  return DcsRender::DecideNativeRelease(DcsRender::SimulationFrameReady(scene.load(std::memory_order_acquire)),
                                       liveViews, evaluations, minimum, settled, superseded, reused);
}

// DCS callbacks publish transition state; the backend owns GPU resource transitions.
extern "C" __declspec(dllexport) int DcsRender_LoadBegin(void*) {
  ConnectBackend();
  auto previous = scene.load(std::memory_order_relaxed);
  while (!scene.compare_exchange_weak(previous, (((previous >> 32) + 1) << 32) | DcsRender::Loading,
                                     std::memory_order_release, std::memory_order_relaxed)) {}
  Publish();
  return 0;
}
extern "C" __declspec(dllexport) int DcsRender_LoadEnd(void*) {
  AddFlag(DcsRender::LoadEnded);
  return 0;
}
extern "C" __declspec(dllexport) int DcsRender_SimulationStart(void*) {
  AddFlag(DcsRender::SimulationStarted);
  return 0;
}
extern "C" __declspec(dllexport) int DcsRender_FirstFrame(void*) {
  AddFlag(DcsRender::FirstSimulationFrame);
  return 0;
}
extern "C" __declspec(dllexport) int DcsRender_SimulationStop(void*) {
  auto previous = scene.load(std::memory_order_relaxed);
  while (!scene.compare_exchange_weak(previous, (previous & 0xFFFFFFFF00000000ull) | DcsRender::SimulationStopped,
                                     std::memory_order_release, std::memory_order_relaxed)) {}
  Publish();
  return 0;
}
