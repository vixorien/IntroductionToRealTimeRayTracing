#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <string>

#include "Mesh.h"
#include "Camera.h"

namespace RayTracing
{
	// --- GLOBAL VARS ---
	// Raytracing-specific versions of base DX12 objects
	inline Microsoft::WRL::ComPtr<ID3D12Device5> DXRDevice;
	inline Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> DXRCommandList;

	// Root signatures for basic raytracing
	inline Microsoft::WRL::ComPtr<ID3D12RootSignature> GlobalRaytracingRootSig;
	inline Microsoft::WRL::ComPtr<ID3D12RootSignature> LocalRaytracingRootSig;

	// Overall raytracing pipeline state object
	// This is similar to a regular PSO, but without the standard
	// rasterization pipeline stuff.  Also grabbing the properties
	// so we can get shader IDs out of it later.
	inline Microsoft::WRL::ComPtr<ID3D12StateObject> RaytracingPipelineStateObject;
	inline Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> RaytracingPipelineProperties;

	// Shader table holding shaders for use during raytracing
	inline Microsoft::WRL::ComPtr<ID3D12Resource> ShaderTable;
	inline UINT64 ShaderTableRecordSize;

	// Accel structure requirements
	inline Microsoft::WRL::ComPtr<ID3D12Resource> TLASScratchBuffer;
	inline Microsoft::WRL::ComPtr<ID3D12Resource> BLASScratchBuffer;
	inline Microsoft::WRL::ComPtr<ID3D12Resource> TLASInstanceDescBuffer;
	inline Microsoft::WRL::ComPtr<ID3D12Resource> TLAS;
	inline Microsoft::WRL::ComPtr<ID3D12Resource> BLAS;

	// Actual output resource
	inline Microsoft::WRL::ComPtr<ID3D12Resource> RaytracingOutput;
	inline D3D12_CPU_DESCRIPTOR_HANDLE RaytracingOutputUAV_CPU;
	inline D3D12_GPU_DESCRIPTOR_HANDLE RaytracingOutputUAV_GPU;

	// Other SRVs for geometry
	// - Larger application will need these FOR EACH MESH
	inline D3D12_GPU_DESCRIPTOR_HANDLE indexBufferSRV;
	inline D3D12_GPU_DESCRIPTOR_HANDLE vertexBufferSRV;

	// --- FUNCTIONS ---
	HRESULT Initialize(
		unsigned int outputWidth,
		unsigned int outputHeight,
		std::wstring raytracingShaderLibraryFile);
	void ResizeOutputUAV(
		unsigned int outputWidth,
		unsigned int outputHeight);
	void Raytrace(
		std::shared_ptr<Camera> camera, 
		Microsoft::WRL::ComPtr<ID3D12Resource> currentBackBuffer);

	// Helper functions for each initalization step
	void CreateBLAS(std::shared_ptr<Mesh> mesh);
	void CreateTLAS();
	void CreateRaytracingRootSignatures();
	void CreateRaytracingPipelineState(std::wstring raytracingShaderLibraryFile);
	void CreateShaderTable();
	void CreateRaytracingOutputUAV(unsigned int width, unsigned int height);
}