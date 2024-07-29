#include "Graphics.h"
#include <dxgi1_6.h>

#include "WICTextureLoader.h"
#include "ResourceUploadBatch.h"

using namespace DirectX;

// Tell the drivers to use high-performance GPU in multi-GPU systems (like laptops)
extern "C"
{
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; // NVIDIA
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1; // AMD
}

namespace Graphics
{
	// Annonymous namespace to hold variables
	// only accessible in this file
	namespace
	{
		bool apiInitialized = false;
		bool supportsTearing = false;
		bool vsyncDesired = false;
		BOOL isFullscreen = false;

		D3D_FEATURE_LEVEL featureLevel{};

		unsigned int currentBackBufferIndex = 0;

		// Descriptor heap management
		SIZE_T cbvSrvDescriptorHeapIncrementSize = 0;
		unsigned int cbvDescriptorOffset = 0;
		unsigned int srvDescriptorOffset = 0;

		// CBV upload heap management
		UINT64 cbUploadHeapSizeInBytes = 0;
		UINT64 cbUploadHeapOffsetInBytes = 0;
		void* cbUploadHeapStartAddress = 0;
	}
}

// Getters
bool Graphics::VsyncState() { return vsyncDesired || !supportsTearing || isFullscreen; }
unsigned int Graphics::SwapChainIndex() { return currentBackBufferIndex; }
std::wstring Graphics::APIName() 
{ 
	switch (featureLevel)
	{
	case D3D_FEATURE_LEVEL_11_0: return L"D3D11";
	case D3D_FEATURE_LEVEL_11_1: return L"D3D11.1";
	case D3D_FEATURE_LEVEL_12_0: return L"D3D12";
	case D3D_FEATURE_LEVEL_12_1: return L"D3D12.1";
	//case D3D_FEATURE_LEVEL_12_2: return L"D3D12.2";
	default: return L"Unknown";
	}
}


// --------------------------------------------------------
// Initializes the Graphics API, which requires window details.
// 
// windowWidth     - Width of the window (and our viewport)
// windowHeight    - Height of the window (and our viewport)
// windowHandle    - OS-level handle of the window
// vsyncIfPossible - Sync to the monitor's refresh rate if available?
// --------------------------------------------------------
HRESULT Graphics::Initialize(unsigned int windowWidth, unsigned int windowHeight, HWND windowHandle, bool vsyncIfPossible)
{
	// Only initialize once
	if (apiInitialized)
		return E_FAIL;

	// Save desired vsync state, though it may be stuck "on" if
	// the device doesn't support screen tearing
	vsyncDesired = vsyncIfPossible;

#if defined(DEBUG) || defined(_DEBUG)
	// If we're in debug mode in visual studio, we also
	// want to enable the DX12 debug layer to see some
	// errors and warnings in Visual Studio's output window
	// when things go wrong!
	ID3D12Debug* debugController;
	D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
	debugController->EnableDebugLayer();
#endif

	// Determine if screen tearing ("vsync off") is available
	// - This is necessary due to variable refresh rate displays
	Microsoft::WRL::ComPtr<IDXGIFactory5> factory;
	if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
	{
		// Check for this specific feature (must use BOOL typedef here!)
		BOOL tearingSupported = false;
		HRESULT featureCheck = factory->CheckFeatureSupport(
			DXGI_FEATURE_PRESENT_ALLOW_TEARING,
			&tearingSupported,
			sizeof(tearingSupported));

		// Final determination of support
		supportsTearing = SUCCEEDED(featureCheck) && tearingSupported;
	}

	// This will hold options for DirectX initialization
	unsigned int deviceFlags = 0;


	// Create the DX 12 device and check which feature level
	// we can reliably use in our application
	{
		HRESULT createResult = D3D12CreateDevice(
			0,						// Not explicitly specifying which adapter (GPU)
			D3D_FEATURE_LEVEL_11_0,	// MINIMUM feature level - NOT the level we'll necessarily turn on
			IID_PPV_ARGS(Device.GetAddressOf()));	// Macro to grab necessary IDs of device
		if (FAILED(createResult)) 
			return createResult;

		// Now that we have a device, determine the maximum
		// feature level supported by the device
		D3D_FEATURE_LEVEL levelsToCheck[] = {
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_12_1
		};
		D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {};
		levels.pFeatureLevelsRequested = levelsToCheck;
		levels.NumFeatureLevels = ARRAYSIZE(levelsToCheck);
		Device->CheckFeatureSupport(
			D3D12_FEATURE_FEATURE_LEVELS,
			&levels,
			sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS));
		featureLevel = levels.MaxSupportedFeatureLevel;
	}


#if defined(DEBUG) || defined(_DEBUG)
	// Set up a callback for any debug messages
	Device->QueryInterface(IID_PPV_ARGS(&InfoQueue));
#endif

	// Set up DX12 command allocator / queue / list,
	// which are necessary pieces for issuing standard API calls
	{
		// Set up allocators
		for (unsigned int i = 0; i < NumBackBuffers; i++)
		{
			Device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(CommandAllocators[i].GetAddressOf()));
		}

		// Command queue
		D3D12_COMMAND_QUEUE_DESC qDesc = {};
		qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		qDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		Device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(CommandQueue.GetAddressOf()));

		// Command list
		Device->CreateCommandList(
			0,								// Which physical GPU will handle these tasks?  0 for single GPU setup
			D3D12_COMMAND_LIST_TYPE_DIRECT,	// Type of command list - direct is for standard API calls
			CommandAllocators[0].Get(),		// The allocator for this list
			0,								// Initial pipeline state - none for now
			IID_PPV_ARGS(CommandList.GetAddressOf()));
	}

	// Swap chain creation
	{
		// Create a description of how our swap chain should work
		DXGI_SWAP_CHAIN_DESC swapDesc = {};
		swapDesc.BufferCount = NumBackBuffers;
		swapDesc.BufferDesc.Width = windowWidth;
		swapDesc.BufferDesc.Height = windowHeight;
		swapDesc.BufferDesc.RefreshRate.Numerator = 60;
		swapDesc.BufferDesc.RefreshRate.Denominator = 1;
		swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		swapDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapDesc.Flags = supportsTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		swapDesc.OutputWindow = windowHandle;
		swapDesc.SampleDesc.Count = 1;
		swapDesc.SampleDesc.Quality = 0;
		swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapDesc.Windowed = true;

		// Create a DXGI factory, which is what we use to create a swap chain
		Microsoft::WRL::ComPtr<IDXGIFactory> dxgiFactory;
		CreateDXGIFactory(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
		HRESULT swapResult = dxgiFactory->CreateSwapChain(CommandQueue.Get(), &swapDesc, SwapChain.GetAddressOf());
		if (FAILED(swapResult))
			return swapResult;
	}

	// What is the increment size between RTV descriptors in a
	// descriptor heap?  This differs per GPU so we need to 
	// get it at applications start up
	SIZE_T RTVDescriptorSize = (SIZE_T)Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	
	// Create back buffers
	{
		// First create a descriptor heap for RTVs
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = NumBackBuffers;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(RTVHeap.GetAddressOf()));

		// Now create the RTV handles for each buffer (buffers were created by the swap chain)
		for (unsigned int i = 0; i < NumBackBuffers; i++)
		{
			// Grab this buffer from the swap chain
			SwapChain->GetBuffer(i, IID_PPV_ARGS(BackBuffers[i].GetAddressOf()));

			// Make a handle for it
			RTVHandles[i] = RTVHeap->GetCPUDescriptorHandleForHeapStart();
			RTVHandles[i].ptr += RTVDescriptorSize * i;

			// Create the render target view
			Device->CreateRenderTargetView(BackBuffers[i].Get(), 0, RTVHandles[i]);
		}
	}

	// Create depth/stencil buffer
	{
		// Create a descriptor heap for DSV
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(DSVHeap.GetAddressOf()));

		// Describe the depth stencil buffer resource
		D3D12_RESOURCE_DESC depthBufferDesc = {};
		depthBufferDesc.Alignment = 0;
		depthBufferDesc.DepthOrArraySize = 1;
		depthBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthBufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		depthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthBufferDesc.Height = windowHeight;
		depthBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		depthBufferDesc.MipLevels = 1;
		depthBufferDesc.SampleDesc.Count = 1;
		depthBufferDesc.SampleDesc.Quality = 0;
		depthBufferDesc.Width = windowWidth;

		// Describe the clear value that will most often be used
		// for this buffer (which optimizes the clearing of the buffer)
		D3D12_CLEAR_VALUE clear = {};
		clear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		clear.DepthStencil.Depth = 1.0f;
		clear.DepthStencil.Stencil = 0;

		// Describe the memory heap that will house this resource
		D3D12_HEAP_PROPERTIES props = {};
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.CreationNodeMask = 1;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		props.Type = D3D12_HEAP_TYPE_DEFAULT;
		props.VisibleNodeMask = 1;

		// Actually create the resource, and the heap in which it
		// will reside, and map the resource to that heap
		Device->CreateCommittedResource(
			&props,
			D3D12_HEAP_FLAG_NONE,
			&depthBufferDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&clear,
			IID_PPV_ARGS(DepthBuffer.GetAddressOf()));

		// Get the handle to the Depth Stencil View that we'll
		// be using for the depth buffer.  The DSV is stored in
		// our DSV-specific descriptor Heap.
		DSVHandle = DSVHeap->GetCPUDescriptorHandleForHeapStart();

		// Actually make the DSV
		Device->CreateDepthStencilView(
			DepthBuffer.Get(),
			0,	// Default view (first mip)
			DSVHandle);
	}

	// Create the fences for basic synchronization and frame sync
	{
		Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(WaitFence.GetAddressOf()));
		WaitFenceEvent = CreateEventEx(0, 0, 0, EVENT_ALL_ACCESS);
		WaitFenceCounter = 0;

		Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(FrameSyncFence.GetAddressOf()));
		FrameSyncFenceEvent = CreateEventEx(0, 0, 0, EVENT_ALL_ACCESS);
	}

	// Create the CBV/SRV descriptor heap
	{
		// Ask the device for the increment size for CBV descriptor heaps
		// This can vary by GPU so we need to query for it
		cbvSrvDescriptorHeapIncrementSize = (SIZE_T)Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// Describe the descriptor heap we want to make
		D3D12_DESCRIPTOR_HEAP_DESC dhDesc = {};
		dhDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // Shaders can see these!
		dhDesc.NodeMask = 0; // Node here means physical GPU - we only have 1 so its index is 0
		dhDesc.NumDescriptors = MaxConstantBuffers + MaxTextureDescriptors; // How many descriptors will we need?
		dhDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; // This heap can store CBVs, SRVs and UAVs

		Device->CreateDescriptorHeap(&dhDesc, IID_PPV_ARGS(CBVSRVDescriptorHeap.GetAddressOf()));

		// Assume the first CBV will be at the beginning of the heap
		// This will increase as we use more CBVs and will wrap back to 0
		cbvDescriptorOffset = 0;

		// Assume the first SRV will be after all possible CBVs
		srvDescriptorOffset = MaxConstantBuffers;
	}

	// Create an upload heap for constant buffer data
	{
		// This heap MUST have a size that is a multiple of 256
		// We'll support up to the max number of CBs if they're
		// all 256 bytes or less, or fewer overall CBs if they're larger
		cbUploadHeapSizeInBytes = (UINT64)MaxConstantBuffers * 256;

		// Assume the first CB will start at the beginning of the heap
		// This offset changes as we use more CBs, and wraps around when full
		cbUploadHeapOffsetInBytes = 0;

		// Create the upload heap for our constant buffer
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.CreationNodeMask = 1;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD; // Upload heap since we'll be copying often!
		heapProps.VisibleNodeMask = 1;

		// Fill out description
		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.Alignment = 0;
		resDesc.DepthOrArraySize = 1;
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		resDesc.Format = DXGI_FORMAT_UNKNOWN;
		resDesc.Height = 1;
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resDesc.MipLevels = 1;
		resDesc.SampleDesc.Count = 1;
		resDesc.SampleDesc.Quality = 0;
		resDesc.Width = cbUploadHeapSizeInBytes; // Must be 256 byte aligned!

		// Create a constant buffer resource heap
		Device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			0,
			IID_PPV_ARGS(CBUploadHeap.GetAddressOf()));

		// Keep mapped!
		D3D12_RANGE range{ 0, 0 };
		CBUploadHeap->Map(0, &range, &cbUploadHeapStartAddress);
	}

	// Wait for the GPU before we proceed
	WaitForGPU();
	apiInitialized = true;
	return S_OK;
}


// --------------------------------------------------------
// When the window is resized, the underlying 
// buffers (textures) must also be resized to match.
//
// If we don't do this, the window size and our rendering
// resolution won't match up.  This can result in odd
// stretching/skewing.
// 
// width  - New width of the window (and our viewport)
// height - New height of the window (and our viewport)
// --------------------------------------------------------
void Graphics::ResizeBuffers(unsigned int width, unsigned int height)
{
	// Ensure graphics API is initialized
	if (!apiInitialized)
		return;

	// Wait for the GPU to finish all work, since we'll
	// be destroying and recreating resources
	WaitForGPU();

	// Release the back buffers using ComPtr's Reset()
	for (unsigned int i = 0; i < NumBackBuffers; i++)
		BackBuffers[i].Reset();

	// Resize the swap chain (assuming a basic color format here)
	SwapChain->ResizeBuffers(
		NumBackBuffers,
		width,
		height,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		supportsTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);

	// What is the increment size between RTV descriptors in a
	// descriptor heap?  This differs per GPU so we need to 
	// get it at applications start up
	SIZE_T RTVDescriptorSize = (SIZE_T)Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);


	// Go through the steps to setup the back buffers again
	// Note: This assumes the descriptor heap already exists
	// and that the rtvDescriptorSize was previously set
	for (unsigned int i = 0; i < NumBackBuffers; i++)
	{
		// Grab this buffer from the swap chain
		SwapChain->GetBuffer(i, IID_PPV_ARGS(BackBuffers[i].GetAddressOf()));

		// Make a handle for it
		RTVHandles[i] = RTVHeap->GetCPUDescriptorHandleForHeapStart();
		RTVHandles[i].ptr += RTVDescriptorSize * (size_t)i;

		// Create the render target view
		Device->CreateRenderTargetView(BackBuffers[i].Get(), 0, RTVHandles[i]);
	}

	// Reset the depth buffer and create it again
	{
		DepthBuffer.Reset();

		// Describe the depth stencil buffer resource
		D3D12_RESOURCE_DESC depthBufferDesc = {};
		depthBufferDesc.Alignment = 0;
		depthBufferDesc.DepthOrArraySize = 1;
		depthBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthBufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		depthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthBufferDesc.Height = height;
		depthBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		depthBufferDesc.MipLevels = 1;
		depthBufferDesc.SampleDesc.Count = 1;
		depthBufferDesc.SampleDesc.Quality = 0;
		depthBufferDesc.Width = width;

		// Describe the clear value that will most often be used
		// for this buffer (which optimizes the clearing of the buffer)
		D3D12_CLEAR_VALUE clear = {};
		clear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		clear.DepthStencil.Depth = 1.0f;
		clear.DepthStencil.Stencil = 0;

		// Describe the memory heap that will house this resource
		D3D12_HEAP_PROPERTIES props = {};
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.CreationNodeMask = 1;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		props.Type = D3D12_HEAP_TYPE_DEFAULT;
		props.VisibleNodeMask = 1;

		// Actually create the resource, and the heap in which it
		// will reside, and map the resource to that heap
		Device->CreateCommittedResource(
			&props,
			D3D12_HEAP_FLAG_NONE,
			&depthBufferDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&clear,
			IID_PPV_ARGS(DepthBuffer.GetAddressOf()));

		// Now recreate the depth stencil view
		DSVHandle = DSVHeap->GetCPUDescriptorHandleForHeapStart();
		Device->CreateDepthStencilView(
			DepthBuffer.Get(),
			0,	// Default view (first mip)
			DSVHandle);
	}

	// Advance the swap chain until we're back at zero
	// so that any pending commands finish up
	while (currentBackBufferIndex != 0)
		AdvanceSwapChainIndex();

	// Are we in a fullscreen state?
	SwapChain->GetFullscreenState(&isFullscreen, 0);

	// Wait for the GPU before we proceed
	WaitForGPU();
}


// --------------------------------------------------------
// Advances the swap chain back buffer index by 1, wrapping
// back to zero when necessary.  This should occur after
// presenting the current frame.
// --------------------------------------------------------
void Graphics::AdvanceSwapChainIndex()
{
	/*currentBackBufferIndex++;
	currentBackBufferIndex %= NumBackBuffers;*/

	// Grab the current fence value so we can adjust it for the next frame,
	// but first use it to signal this frame being done
	UINT64 currentFenceCounter = FrameSyncFenceCounters[currentBackBufferIndex];
	CommandQueue->Signal(FrameSyncFence.Get(), currentFenceCounter);

	// Calculate the next index
	unsigned int nextBuffer = currentBackBufferIndex + 1;
	nextBuffer %= NumBackBuffers;

	// Do we need to wait for the next frame?  We do this by checking the counter
	// associated with that frame's buffer and waiting if it's not complete
	if (FrameSyncFence->GetCompletedValue() < FrameSyncFenceCounters[nextBuffer])
	{
		// Not completed, so we wait
		FrameSyncFence->SetEventOnCompletion(FrameSyncFenceCounters[nextBuffer], FrameSyncFenceEvent);
		WaitForSingleObject(FrameSyncFenceEvent, INFINITE);
	}

	// Frame is done, so update the next frame's counter
	FrameSyncFenceCounters[nextBuffer] = currentFenceCounter + 1;

	// Return the new buffer index, which the caller can
	// use to track which buffer to use for the next frame
	currentBackBufferIndex = nextBuffer;
}


// --------------------------------------------------------
// Helper for creating a basic buffer
// 
// size      - How big should the buffer be in bytes
// heapType  - What kind of D3D12 heap?  Default is D3D12_HEAP_TYPE_DEFAULT
// state     - What state should the resulting resource be in?  Default is D3D12_RESOURCE_STATE_COMMON
// flags     - Any special flags?  Default is D3D12_RESOURCE_FLAG_NONE
// alignment - What's the buffer alignment?  Default is 0
// --------------------------------------------------------
Microsoft::WRL::ComPtr<ID3D12Resource> Graphics::CreateBuffer(
	UINT64 size, 
	D3D12_HEAP_TYPE heapType, 
	D3D12_RESOURCE_STATES state,
	D3D12_RESOURCE_FLAGS flags, 
	UINT64 alignment)
{
	Microsoft::WRL::ComPtr<ID3D12Resource> buffer;

	// Describe the heap
	D3D12_HEAP_PROPERTIES heapDesc = {};
	heapDesc.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapDesc.CreationNodeMask = 1;
	heapDesc.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapDesc.Type = heapType;
	heapDesc.VisibleNodeMask = 1;

	// Describe the resource
	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = alignment;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = flags;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Width = size; // Size of the buffer

	// Create the buffer
	Device->CreateCommittedResource(&heapDesc, D3D12_HEAP_FLAG_NONE, &desc, state, 0, IID_PPV_ARGS(buffer.GetAddressOf()));
	return buffer;
}


// --------------------------------------------------------
// Helper for creating a static buffer that will get
// data once and remain immutable
// 
// dataStride - The size of one piece of data in the buffer (like a vertex)
// dataCount - How many pieces of data (like how many vertices)
// data - Pointer to the data itself
// --------------------------------------------------------
Microsoft::WRL::ComPtr<ID3D12Resource> Graphics::CreateStaticBuffer(size_t dataStride, size_t dataCount, void* data)
{
	// Creates a temporary command allocator and list so we don't
	// screw up any other ongoing work (since resetting a command allocator
	// cannot happen while its list is being executed).  These ComPtrs will
	// be cleaned up automatically when they go out of scope.
	// Note: This certainly isn't efficient, but hopefully this only
	//       happens during start-up.  Otherwise, refactor this to use
	//       the existing list and allocator(s).
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> localAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> localList;

	Device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(localAllocator.GetAddressOf()));

	Device->CreateCommandList(
		0,								// Which physical GPU will handle these tasks?  0 for single GPU setup
		D3D12_COMMAND_LIST_TYPE_DIRECT,	// Type of command list - direct is for standard API calls
		localAllocator.Get(),			// The allocator for this list (to start)
		0,								// Initial pipeline state - none for now
		IID_PPV_ARGS(localList.GetAddressOf()));

	// The overall buffer we'll be creating
	Microsoft::WRL::ComPtr<ID3D12Resource> buffer;

	// Describes the final heap
	D3D12_HEAP_PROPERTIES props = {};
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.CreationNodeMask = 1;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	props.Type = D3D12_HEAP_TYPE_DEFAULT;
	props.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1; // Assuming this is a regular buffer, not a texture
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Width = dataStride * dataCount; // Size of the buffer

	Device->CreateCommittedResource(
		&props,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COPY_DEST, // Will eventually be "common", but we're copying to it first!
		0,
		IID_PPV_ARGS(buffer.GetAddressOf()));

	// Now create an intermediate upload heap for copying initial data
	D3D12_HEAP_PROPERTIES uploadProps = {};
	uploadProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	uploadProps.CreationNodeMask = 1;
	uploadProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD; // Can only ever be Generic_Read state
	uploadProps.VisibleNodeMask = 1;

	Microsoft::WRL::ComPtr<ID3D12Resource> uploadHeap;
	Device->CreateCommittedResource(
		&uploadProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		0,
		IID_PPV_ARGS(uploadHeap.GetAddressOf()));

	// Do a straight map/memcpy/unmap
	void* gpuAddress = 0;
	uploadHeap->Map(0, 0, &gpuAddress);
	memcpy(gpuAddress, data, dataStride * dataCount);
	uploadHeap->Unmap(0, 0);

	// Copy the whole buffer from uploadheap to vert buffer
	localList->CopyResource(buffer.Get(), uploadHeap.Get());

	// Transition the buffer to generic read for the rest of the app lifetime (presumable)
	D3D12_RESOURCE_BARRIER rb = {};
	rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	rb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	rb.Transition.pResource = buffer.Get();
	rb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	rb.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	localList->ResourceBarrier(1, &rb);

	// Execute the local command list and wait for it to complete
	// before returning the final buffer
	localList->Close();
	ID3D12CommandList* list[] = { localList.Get() };
	CommandQueue->ExecuteCommandLists(1, list);
	
	WaitForGPU();
	return buffer;
}


// --------------------------------------------------------
// Copies the given data into the next "unused" spot in
// the CBV upload heap (wrapping at the end, since we treat
// it like a ring buffer).  Then creates a CBV in the next
// "unused" spot in the CBV heap that points to the 
// aforementioned spot in the upload heap and returns that 
// CBV (a GPU descriptor handle)
// 
// data - The data to copy to the GPU
// dataSizeInBytes - The byte size of the data to copy
// --------------------------------------------------------
D3D12_GPU_DESCRIPTOR_HANDLE Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(void* data, unsigned int dataSizeInBytes)
{
	// How much space will we need?  Each CBV must point to a chunk of
// the upload heap that is a multiple of 256 bytes, so we need to 
// calculate and reserve that amount.
	SIZE_T reservationSize = (SIZE_T)dataSizeInBytes;
	reservationSize = (reservationSize + 255) / 256 * 256; // Integer division trick

	// Ensure this upload will fit in the remaining space.  If not, reset to beginning.
	if (cbUploadHeapOffsetInBytes + reservationSize >= cbUploadHeapSizeInBytes)
		cbUploadHeapOffsetInBytes = 0;

	// Where in the upload heap will this data go?
	D3D12_GPU_VIRTUAL_ADDRESS virtualGPUAddress =
		CBUploadHeap->GetGPUVirtualAddress() + cbUploadHeapOffsetInBytes;

	// === Copy data to the upload heap ===
	{
		// Calculate the actual upload address (which we got from mapping the buffer)
		// Note that this is different than the GPU virtual address needed for the CBV below
		void* uploadAddress = reinterpret_cast<void*>((SIZE_T)cbUploadHeapStartAddress + cbUploadHeapOffsetInBytes);

		// Perform the mem copy to put new data into this part of the heap
		memcpy(uploadAddress, data, dataSizeInBytes);

		// Increment the offset and loop back to the beginning if necessary,
		// allowing us to treat the upload heap like a ring buffer
		cbUploadHeapOffsetInBytes += reservationSize;
		if (cbUploadHeapOffsetInBytes >= cbUploadHeapSizeInBytes)
			cbUploadHeapOffsetInBytes = 0;
	}

	// Create a CBV for this section of the heap
	{
		// Calculate the CPU and GPU side handles for this descriptor
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = CBVSRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = CBVSRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

		// Offset each by based on how many descriptors we've used
		// Note: cbvDescriptorOffset is a COUNT of descriptors, not bytes
		//       so we need to calculate the size
		cpuHandle.ptr += (SIZE_T)cbvDescriptorOffset * cbvSrvDescriptorHeapIncrementSize;
		gpuHandle.ptr += (SIZE_T)cbvDescriptorOffset * cbvSrvDescriptorHeapIncrementSize;

		// Describe the constant buffer view that points to
		// our latest chunk of the CB upload heap
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = virtualGPUAddress;
		cbvDesc.SizeInBytes = (UINT)reservationSize;

		// Create the CBV, which is a lightweight operation in DX12
		Device->CreateConstantBufferView(&cbvDesc, cpuHandle);

		// Increment the offset and loop back to the beginning if necessary
		// which allows us to treat the descriptor heap as a ring buffer
		cbvDescriptorOffset++;
		if (cbvDescriptorOffset >= MaxConstantBuffers)
			cbvDescriptorOffset = 0;

		// Now that the CBV is ready, we return the GPU handle to it
		// so it can be set as part of the root signature during drawing
		return gpuHandle;
	}
}


// --------------------------------------------------------
// Loads a texture using the DirectX Toolkit and creates a
// non-shader-visible SRV descriptor heap to hold its SRV.  
// The handle to this descriptor is returned so materials
// can copy this texture's SRV to the overall heap later.
// 
// file - The image file to attempt to load
// generateMips - Should mip maps be generated? (defaults to true)
// --------------------------------------------------------
D3D12_CPU_DESCRIPTOR_HANDLE Graphics::LoadTexture(const wchar_t* file, bool generateMips)
{
	// Helper function from DXTK for uploading a resource
	// (like a texture) to the appropriate GPU memory
	ResourceUploadBatch upload(Device.Get());
	upload.Begin();

	// Attempt to create the texture
	Microsoft::WRL::ComPtr<ID3D12Resource> texture;
	CreateWICTextureFromFile(Device.Get(), upload, file, texture.GetAddressOf(), generateMips);

	// Perform the upload and wait for it to finish before returning the texture
	auto finish = upload.End(CommandQueue.Get());
	finish.wait();

	// Now that we have the texture, add to our list and make a CPU-side descriptor heap
	// just for this texture's SRV.  Note that it would probably be better to put all 
	// texture SRVs into the same descriptor heap, but we don't know how many we'll need
	// until they're all loaded and this is a quick and dirty implementation!
	Textures.push_back(texture);

	// Create the CPU-SIDE descriptor heap for our descriptor
	D3D12_DESCRIPTOR_HEAP_DESC dhDesc = {};
	dhDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // Non-shader visible for CPU-side-only descriptor heap!
	dhDesc.NodeMask = 0;
	dhDesc.NumDescriptors = 1;
	dhDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descHeap;
	Device->CreateDescriptorHeap(&dhDesc, IID_PPV_ARGS(descHeap.GetAddressOf()));
	CPUSideTextureDescriptorHeaps.push_back(descHeap);

	// Create the SRV on this descriptor heap
	// Note: Using a null description results in the "default" SRV (same format, all mips, all array slices, etc.)
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = descHeap->GetCPUDescriptorHandleForHeapStart();
	Device->CreateShaderResourceView(texture.Get(), 0, cpuHandle);

	// Return the CPU descriptor handle, which can be used to
	// copy the descriptor to a shader-visible heap later
	return cpuHandle;
}


// --------------------------------------------------------
// Copies one or more SRVs starting at the given CPU handle
// to the final CBV/SRV descriptor heap, and returns
// the GPU handle to the beginning of this section.
// 
// firstDescriptorToCopy - The handle to the first descriptor
// numDescriptorsToCopy - How many to copy
// --------------------------------------------------------
D3D12_GPU_DESCRIPTOR_HANDLE Graphics::CopySRVsToDescriptorHeapAndGetGPUDescriptorHandle(
	D3D12_CPU_DESCRIPTOR_HANDLE firstDescriptorToCopy,
	unsigned int numDescriptorsToCopy)
{
	// Grab the actual heap start on both sides and offset to the next open SRV portion
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = CBVSRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = CBVSRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

	cpuHandle.ptr += (SIZE_T)srvDescriptorOffset * cbvSrvDescriptorHeapIncrementSize;
	gpuHandle.ptr += (SIZE_T)srvDescriptorOffset * cbvSrvDescriptorHeapIncrementSize;

	// We know where to copy these descriptors, so copy all of them and remember the new offset
	Device->CopyDescriptorsSimple(numDescriptorsToCopy, cpuHandle, firstDescriptorToCopy, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	srvDescriptorOffset += numDescriptorsToCopy;

	// Pass back the GPU handle to the start of this section
	// in the final CBV/SRV heap so the caller can use it later
	return gpuHandle;
}

// --------------------------------------------------------
// Reserves a slot in the SRV/UAV section of the overall
// CBV/SRV/UAV descriptor heap.  Handles to CPU and/or GPU
// are set via parameters.  Pass in 0 to skip a parameter.
// --------------------------------------------------------
void Graphics::ReserveSrvUavDescriptorHeapSlot(D3D12_CPU_DESCRIPTOR_HANDLE* reservedCPUHandle, D3D12_GPU_DESCRIPTOR_HANDLE* reservedGPUHandle)
{
	// Grab the actual heap start on both sides and offset to the next open SRV/UAV portion
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = CBVSRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = CBVSRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

	cpuHandle.ptr += (SIZE_T)srvDescriptorOffset * cbvSrvDescriptorHeapIncrementSize;
	gpuHandle.ptr += (SIZE_T)srvDescriptorOffset * cbvSrvDescriptorHeapIncrementSize;

	// Set the requested handle(s)
	if (reservedCPUHandle) { *reservedCPUHandle = cpuHandle; }
	if (reservedGPUHandle) { *reservedGPUHandle = gpuHandle; }

	// Update the overall offset
	srvDescriptorOffset++;
}


// --------------------------------------------------------
// Resets the command allocator and list associated
// with a particular back buffer in the swap chain
// 
// Always wait before reseting command allocator, as it should not
// be reset while the GPU is processing a command list
// See: https://docs.microsoft.com/en-us/windows/desktop/api/d3d12/nf-d3d12-id3d12commandallocator-reset
// --------------------------------------------------------
void Graphics::ResetAllocatorAndCommandList(unsigned int swapChainIndex)
{
	CommandAllocators[swapChainIndex]->Reset();
	CommandList->Reset(CommandAllocators[swapChainIndex].Get(), 0);
}


// --------------------------------------------------------
// Closes the current command list and tells the GPU to
// start executing those commands.  We also wait for
// the GPU to finish this work so we can reset the
// command allocator (which CANNOT be reset while the
// GPU is using its commands) and the command list itself.
// --------------------------------------------------------
void Graphics::CloseAndExecuteCommandList()
{
	// Close the current list and execute it as our only list
	CommandList->Close();
	ID3D12CommandList* lists[] = { CommandList.Get() };
	CommandQueue->ExecuteCommandLists(1, lists);
}


// --------------------------------------------------------
// Makes our C++ code wait for the GPU to finish its
// current batch of work before moving on.
// --------------------------------------------------------
void Graphics::WaitForGPU()
{
	// Update our ongoing fence value (a unique index for each "stop sign")
	// and then place that value into the GPU's command queue
	WaitFenceCounter++;
	CommandQueue->Signal(WaitFence.Get(), WaitFenceCounter);

	// Check to see if the most recently completed fence value
	// is less than the one we just set.
	if (WaitFence->GetCompletedValue() < WaitFenceCounter)
	{
		// Tell the fence to let us know when it's hit, and then
		// sit and wait until that fence is hit.
		WaitFence->SetEventOnCompletion(WaitFenceCounter, WaitFenceEvent);
		WaitForSingleObject(WaitFenceEvent, INFINITE);
	}
}


// --------------------------------------------------------
// Prints graphics debug messages waiting in the queue
// --------------------------------------------------------
void Graphics::PrintDebugMessages()
{
	// Do we actually have an info queue (usually in debug mode)
	if (!InfoQueue)
		return;

	// Any messages?
	UINT64 messageCount = InfoQueue->GetNumStoredMessages();
	if (messageCount == 0)
		return;

	// Loop and print messages
	for (UINT64 i = 0; i < messageCount; i++)
	{
		// Get the size so we can reserve space
		size_t messageSize = 0;
		InfoQueue->GetMessage(i, 0, &messageSize);

		// Reserve space for this message
		D3D12_MESSAGE* message = (D3D12_MESSAGE*)malloc(messageSize);
		InfoQueue->GetMessage(i, message, &messageSize);

		// Print and clean up memory
		if (message)
		{
			printf("%s\n", message->pDescription);
			free(message);
		}
	}

	// Clear any messages we've printed
	InfoQueue->ClearStoredMessages();
}
