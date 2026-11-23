#include <cstdint>
#include <vector>

using LifecycleCallback = void (*)(uint32_t, uint64_t);
static LifecycleCallback callback = nullptr;
static uint32_t registrations = 0;
static bool paused = false;
static std::vector<uint64_t> requests;
#ifdef TEST_STEREO
#define REGISTER_BACKEND Cheeky_RegisterReleaseLifecycleV1
#define REQUEST_RELEASE Cheeky_RequestReleaseV1
#else
#define REGISTER_BACKEND OptiScaler_RegisterReleaseLifecycleV2
#define REQUEST_RELEASE OptiScaler_RequestReleaseV2
#endif

extern "C" __declspec(dllexport) uint32_t REGISTER_BACKEND(LifecycleCallback value) {
  callback = value;
  ++registrations;
  callback(1, 100);
  callback(1, 101);
  return 1;
}
extern "C" __declspec(dllexport) uint32_t REQUEST_RELEASE(uint64_t handle) {
  requests.push_back(handle);
  return 1;
}
extern "C" __declspec(dllexport) void OptiScaler_SetFrameGenerationPausedV1(bool value) { paused = value; }
extern "C" __declspec(dllexport) void TestLifecycleEvent(uint32_t event, uint64_t handle) { callback(event, handle); }
extern "C" __declspec(dllexport) uint32_t TestRegistrationCount() { return registrations; }
extern "C" __declspec(dllexport) uint32_t TestRequestCount() { return static_cast<uint32_t>(requests.size()); }
extern "C" __declspec(dllexport) uint64_t TestRequestAt(uint32_t index) { return requests.at(index); }
extern "C" __declspec(dllexport) bool TestPaused() { return paused; }
