#pragma once

#include <DirectXMath.h>

// Must match the MAX_LIGHTS definition in your shaders
#define MAX_LIGHTS 128

// These should also match lights in shaders
#define LIGHT_TYPE_DIRECTIONAL	0
#define LIGHT_TYPE_POINT		1
#define LIGHT_TYPE_SPOT			2

// Defines a single light that can be sent to the GPU
// Note: This must match light struct in shaders
//       and must also be a multiple of 16 bytes!
struct Light
{
	int					Type;
	DirectX::XMFLOAT3	Direction;	// 16 bytes

	float				Range;
	DirectX::XMFLOAT3	Position;	// 32 bytes

	float				Intensity;
	DirectX::XMFLOAT3	Color;		// 48 bytes

	float				SpotFalloff;
	DirectX::XMFLOAT3	Padding;	// 64 bytes
};