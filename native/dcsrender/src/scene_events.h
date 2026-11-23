#pragma once
#include <cstdint>

namespace DcsRender {
// DcsRender_GetSceneStateV1 returns generation in the high word and these flags in the low word.
enum SceneFlags : uint32_t {
  Loading = 1,
  LoadEnded = 2,
  SimulationStarted = 4,
  FirstSimulationFrame = 8,
  SimulationStopped = 16
};
using GetSceneStateV1 = uint64_t (*)();

inline bool SimulationFrameReady(uint64_t state) noexcept {
  return (state & FirstSimulationFrame) != 0 && (state & SimulationStopped) == 0;
}
}
