#pragma once

#include "Mesh.h"
#include "GameEntity.h"
#include "Transform.h"
#include "Camera.h"
#include "Lights.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <memory>


class Game
{
public:
	// Basic OOP setup
	Game() = default;
	~Game() = default;
	Game(const Game&) = delete; // Remove copy constructor
	Game& operator=(const Game&) = delete; // Remove copy-assignment operator

	// Primary functions
	void Initialize();
	void Update(float deltaTime, float totalTime);
	void Draw(float deltaTime, float totalTime);
	void OnResize();
	void ShutDown();

private:

	// Note the usage of ComPtr below
	//  - This is a smart pointer for objects that abide by the
	//     Component Object Model, which DirectX objects do
	//  - More info here: https://github.com/Microsoft/DirectXTK/wiki/ComPtr

	// Pipeline
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;

	// Scene
	std::shared_ptr<Camera> camera;
	std::shared_ptr<Mesh> sphereMesh;
};

