#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <string>
#include <wrl/client.h>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace Graphics
{
	// --- CONSTANTS ---
	const unsigned int NumBackBuffers = 2;

	// Maximum number of constant buffers, assuming each buffer
	// is 256 bytes or less.  Larger buffers are fine, but will
	// result in fewer buffers in use at any time
	const unsigned int MaxConstantBuffers = 1000;

	// Maximum number of texture descriptors (SRVs) we can have.
	// Each material will have a chunk of this, plus any 
	// non-material textures we may need for our program.
	// Note: If we delayed the creation of this heap until 
	//       after all textures and materials were created,
	//       we could come up with an exact amount.  The following
	//       constant ensures we (hopefully) never run out of room.
	const unsigned int MaxTextureDescriptors = 1000;

	// --- GLOBAL VARS ---

	// Primary D3D11 API objects
	inline Microsoft::WRL::ComPtr<ID3D12Device>		Device;
	inline Microsoft::WRL::ComPtr<IDXGISwapChain>	SwapChain;

	// Command submission
	inline Microsoft::WRL::ComPtr<ID3D12CommandAllocator>		CommandAllocators[NumBackBuffers];
	inline Microsoft::WRL::ComPtr<ID3D12CommandQueue>			CommandQueue;
	inline Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>	CommandList;

	// Rendering buffers & descriptors
	inline Microsoft::WRL::ComPtr<ID3D12Resource>		BackBuffers[NumBackBuffers];
	inline Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> RTVHeap;
	inline D3D12_CPU_DESCRIPTOR_HANDLE					RTVHandles[NumBackBuffers]{};

	inline Microsoft::WRL::ComPtr<ID3D12Resource>		DepthBuffer;
	inline Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DSVHeap;
	inline D3D12_CPU_DESCRIPTOR_HANDLE					DSVHandle{};

	inline Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CBVSRVDescriptorHeap;
	inline Microsoft::WRL::ComPtr<ID3D12Resource> CBUploadHeap;

	// Textures
	inline std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> Textures;
	inline std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> CPUSideTextureDescriptorHeaps;

	// Basic CPU/GPU synchronization
	inline Microsoft::WRL::ComPtr<ID3D12Fence>	WaitFence;
	inline HANDLE								WaitFenceEvent = 0;
	inline UINT64								WaitFenceCounter = 0;

	// Frame sync'ing
	inline Microsoft::WRL::ComPtr<ID3D12Fence>	FrameSyncFence;
	inline HANDLE								FrameSyncFenceEvent = 0;
	inline UINT64								FrameSyncFenceCounters[NumBackBuffers]{};

	// Debug Layer
	inline Microsoft::WRL::ComPtr<ID3D12InfoQueue> InfoQueue;

	// --- FUNCTIONS ---

	// Getters
	bool VsyncState();
	unsigned int SwapChainIndex();
	std::wstring APIName();

	// General functions
	HRESULT Initialize(unsigned int windowWidth, unsigned int windowHeight, HWND windowHandle, bool vsyncIfPossible);
	void ResizeBuffers(unsigned int width, unsigned int height);
	void AdvanceSwapChainIndex();

	// Resource creation and usage
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(
		UINT64 size,
		D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
		UINT64 alignment = 0);
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateStaticBuffer(size_t dataStride, size_t dataCount, void* data);
	D3D12_GPU_DESCRIPTOR_HANDLE FillNextConstantBufferAndGetGPUDescriptorHandle(
		void* data,
		unsigned int dataSizeInBytes);
	D3D12_CPU_DESCRIPTOR_HANDLE LoadTexture(const wchar_t* file, bool generateMips = true);
	D3D12_GPU_DESCRIPTOR_HANDLE CopySRVsToDescriptorHeapAndGetGPUDescriptorHandle(
		D3D12_CPU_DESCRIPTOR_HANDLE firstDescriptorToCopy,
		unsigned int numDescriptorsToCopy);
	void ReserveSrvUavDescriptorHeapSlot(
		D3D12_CPU_DESCRIPTOR_HANDLE* reservedCPUHandle, 
		D3D12_GPU_DESCRIPTOR_HANDLE* reservedGPUHandle);

	// Command list & synchronization
	void ResetAllocatorAndCommandList(unsigned int swapChainIndex);
	void CloseAndExecuteCommandList();
	void WaitForGPU();

	// Debug Layer
	void PrintDebugMessages();
}