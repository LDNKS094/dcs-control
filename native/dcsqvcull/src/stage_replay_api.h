#pragma once
#include <d3d11.h>

// Returned resources carry one reference each. The callback remains valid
// while the caller retains lease; rowDelta is a row-vector clip transform.
struct CheekyStageReplayViewV1 {
  ID3D11Texture2D* scene{};
  IUnknown* lease{};
  bool (__cdecl* replay)(IUnknown*,ID3D11DeviceContext*,ID3D11Texture2D*,const float rowDelta[16]){};
};
