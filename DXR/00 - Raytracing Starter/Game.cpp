#include "Game.h"
#include "Graphics.h"
#include "Vertex.h"
#include "Input.h"
#include "PathHelpers.h"
#include "Window.h"
#include "Lights.h"
#include "BufferStructs.h"
#include "Material.h"
#include "RayTracing.h"

#include <DirectXMath.h>

// Needed for a helper function to load pre-compiled shader files
#pragma comment(lib, "d3dcompiler.lib")
#include <d3dcompiler.h>

// For the DirectX Math library
using namespace DirectX;

// --------------------------------------------------------
// Called once per program, the window and graphics API
// are initialized but before the game loop begins
// --------------------------------------------------------
void Game::Initialize()
{
	RayTracing::Initialize(
		Window::Width(),
		Window::Height(),
		FixPath(L"Raytracing.cso"));

	camera = std::make_shared<Camera>(
		XMFLOAT3(0.0f, 0.0f, -2.0f),	// Position
		5.0f,							// Move speed
		0.002f,							// Look speed
		XM_PIDIV4,						// Field of view
		Window::AspectRatio());			// Aspect ratio

	sphereMesh = std::make_shared<Mesh>(FixPath(L"../../../../Assets/Meshes/sphere.obj").c_str());

	// Last step in raytracing setup is to create the accel structures,
	// which require mesh data.  Currently just a single mesh is handled!
	RayTracing::CreateBLAS(sphereMesh);

	// Once we have all of the BLAS ready, we can make a TLAS
	RayTracing::CreateTLAS();

	// Finalize any initialization and wait for the GPU
	// before proceeding to the game loop
	// Note: NOT resetting the allocator here because
	//       that will happen at the beginning of Draw()
	Graphics::CloseAndExecuteCommandList();
	Graphics::WaitForGPU();
}


// --------------------------------------------------------
// Clean up memory or objects created by this class
// 
// Note: Using smart pointers means there probably won't
//       be much to manually clean up here!
// --------------------------------------------------------
void Game::ShutDown()
{
	// Wait for the GPU before we shut down
	Graphics::WaitForGPU();
}


// --------------------------------------------------------
// Handle resizing to match the new window size
//  - Eventually, we'll want to update our 3D camera
// --------------------------------------------------------
void Game::OnResize()
{
	// Update the camera's projection to match the new size
	if (camera)
		camera->UpdateProjectionMatrix(Window::AspectRatio());

	RayTracing::ResizeOutputUAV(Window::Width(), Window::Height());
}


// --------------------------------------------------------
// Update your game here - user input, move objects, AI, etc.
// --------------------------------------------------------
void Game::Update(float deltaTime, float totalTime)
{
	// Example input checking: Quit if the escape key is pressed
	if (Input::KeyDown(VK_ESCAPE))
		Window::Quit();

	camera->Update(deltaTime);
}


// --------------------------------------------------------
// Clear the screen, redraw everything, present to the user
// --------------------------------------------------------
void Game::Draw(float deltaTime, float totalTime)
{
	// Reset the allocator for this frame
	Graphics::ResetAllocatorAndCommandList(Graphics::SwapChainIndex());

	// Grab the current back buffer for this frame
	Microsoft::WRL::ComPtr<ID3D12Resource> currentBackBuffer = Graphics::BackBuffers[Graphics::SwapChainIndex()];

	// Ray tracing here!
	{
		RayTracing::Raytrace(camera, currentBackBuffer);
	}

	// Present
	{
		// Present the current back buffer and move to the next one
		bool vsync = Graphics::VsyncState();
		Graphics::SwapChain->Present(
			vsync ? 1 : 0,
			vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING);
		Graphics::AdvanceSwapChainIndex();
	}
}



