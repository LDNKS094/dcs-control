#pragma once

#include <cstdint>

namespace DcsRender {
enum NativeReleaseDecision : uint32_t { Wait, Retire, Refuse };
using NativeReleasePolicy = uint32_t (*)(uint32_t, uint64_t, uint64_t, bool, bool, bool);

inline uint32_t DecideNativeRelease(bool ready, uint32_t liveViews, uint64_t evaluations,
                                    uint64_t minimum, bool settled, bool superseded, bool reused) {
  if (!ready || !liveViews || evaluations < minimum || !settled || !superseded) return Wait;
  return reused ? Refuse : Retire;
}
}
