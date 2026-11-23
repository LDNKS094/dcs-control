#include <Windows.h>
#include <cstdio>
#include "../src/scene_events.h"
#include "../src/release_policy.h"

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) return 2;
  const bool stereo = argc == 4;
  const auto backend = LoadLibraryA(argv[2]);
  if (!backend) return 2;
  using Count = uint32_t (*)();
  using Emit = void (*)(uint32_t, uint64_t);
  using At = uint64_t (*)(uint32_t);
  using Paused = bool (*)();
  const auto emit = reinterpret_cast<Emit>(GetProcAddress(backend, "TestLifecycleEvent"));
  const auto registrations = reinterpret_cast<Count>(GetProcAddress(backend, "TestRegistrationCount"));
  const auto count = reinterpret_cast<Count>(GetProcAddress(backend, "TestRequestCount"));
  const auto at = reinterpret_cast<At>(GetProcAddress(backend, "TestRequestAt"));
  const auto paused = reinterpret_cast<Paused>(GetProcAddress(backend, "TestPaused"));
  const auto module = LoadLibraryA(argv[1]);
  if (!module) return 2;
  const auto read = reinterpret_cast<DcsRender::GetSceneStateV1>(GetProcAddress(module, "DcsRender_GetSceneStateV1"));
  const auto decide = reinterpret_cast<DcsRender::NativeReleasePolicy>(GetProcAddress(module, "DcsRender_DecideNativeReleaseV1"));
  using Callback = int (*)(void*);
  const auto initialize = reinterpret_cast<Callback>(GetProcAddress(module, "DcsRender_Initialize"));
  const auto begin = reinterpret_cast<Callback>(GetProcAddress(module, "DcsRender_LoadBegin"));
  const auto end = reinterpret_cast<Callback>(GetProcAddress(module, "DcsRender_LoadEnd"));
  const auto start = reinterpret_cast<Callback>(GetProcAddress(module, "DcsRender_SimulationStart"));
  const auto first = reinterpret_cast<Callback>(GetProcAddress(module, "DcsRender_FirstFrame"));
  const auto stop = reinterpret_cast<Callback>(GetProcAddress(module, "DcsRender_SimulationStop"));
  if (!read || !decide || !initialize || !begin || !end || !start || !first || !stop ||
      !emit || !registrations || !count || !at || !paused) return 2;
  int failures = read() != 0;
  const auto contains = [&](uint64_t handle) {
    for (uint32_t i = 0; i < count(); ++i) if (at(i) == handle) return true;
    return false;
  };
  constexpr uint64_t left = 0x100000200ull, right = 0x100000201ull;
  initialize(nullptr);
  initialize(nullptr);
  failures += registrations() != 1 || paused();

  begin(nullptr);
  failures += decide(4, 300, 300, true, true, false) != DcsRender::Wait;
  failures += read() != ((1ull << 32) | DcsRender::Loading) || paused() == stereo;
  end(nullptr);
  start(nullptr);
  emit(1, left);
  emit(1, right);
  emit(2, left);
  failures += count() != 0 || paused() == stereo;
  first(nullptr);
  failures += decide(4, 300, 300, true, true, false) != DcsRender::Retire;
  failures += decide(4, 300, 300, true, true, true) != DcsRender::Refuse;
  failures += decide(4, 299, 300, true, true, false) != DcsRender::Wait;
  failures += decide(4, 300, 300, false, true, false) != DcsRender::Wait;
  failures += read() != ((1ull << 32) | DcsRender::Loading | DcsRender::LoadEnded |
                        DcsRender::SimulationStarted | DcsRender::FirstSimulationFrame) || paused();
  emit(2, left);
  failures += count() != (stereo ? 0u : 2u);
  if (!stereo) {
    failures += !contains(100) || !contains(101);
    emit(3, 100);
    emit(3, 101);
  }
  emit(2, right);
  const auto initial = count();

  stop(nullptr);
  failures += decide(4, 300, 300, true, true, false) != DcsRender::Wait;
  failures += read() != ((1ull << 32) | DcsRender::SimulationStopped) || paused() == stereo;
  begin(nullptr);
  emit(1, 300);
  emit(2, left);
  failures += count() != initial;
  first(nullptr);
  emit(2, left);
  if (stereo) failures += count() != initial;
  emit(2, 300);
  failures += count() != initial + 1 || !contains(right) || contains(left) || paused();
  emit(3, right);

  stop(nullptr);
  begin(nullptr);
  emit(3, 300);
  emit(1, 400);
  emit(1, 401);
  first(nullptr);
  emit(2, 400);
  if (stereo) failures += count() != initial + 1;
  emit(2, 401);
  failures += count() != initial + 2 || !contains(left) || contains(300);
  stop(nullptr);
  emit(2, 400);
  failures += count() != initial + 2 || paused() == stereo;

  FreeLibrary(module);
  FreeLibrary(backend);
  std::printf("%d failure(s)\n", failures);
  return failures ? 1 : 0;
}
