#include "RayTracing.h"
#include "Graphics.h"
#include "BufferStructs.h"
#include "Window.h"

#include <d3dcompiler.h>
#include <DirectXMath.h>

namespace RayTracing
{
	// Annonymous namespace to hold variables
	// only accessible in this file
	namespace
	{
		bool dxrAvailable = false;
		bool dxrInitialized = false;
	}
}

// Makes use of integer division to ensure we are aligned to the proper multiple of "alignment"
#define ALIGN(value, alignment) (((value + alignment - 1) / alignment) * alignment)


// --------------------------------------------------------
// Check for raytracing support and create all necessary
// raytracing resources, pipeline states, etc.
// --------------------------------------------------------
HRESULT RayTracing::Initialize(
	unsigned int outputWidth, 
	unsigned int outputHeight, 
	std::wstring raytracingShaderLibraryFile)
{
	// Use CheckFeatureSupport to determine if ray tracing is supported
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 rtSupport = {};
	HRESULT supportResult = Graphics::Device->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS5,
		&rtSupport,
		sizeof(rtSupport));

	// Query to ensure we can get proper versions of the device and command list
	HRESULT dxrDeviceResult = Graphics::Device->QueryInterface(IID_PPV_ARGS(DXRDevice.GetAddressOf()));
	HRESULT dxrCommandListResult = Graphics::CommandList->QueryInterface(IID_PPV_ARGS(DXRCommandList.GetAddressOf()));

	// Check the results
	if (FAILED(supportResult) || rtSupport.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) { printf("Raytracing not supported.\n"); return supportResult; }
	if (FAILED(dxrDeviceResult)) { printf("DXR Device query failed - DirectX Raytracing unavailable.\n"); return dxrDeviceResult; }
	if (FAILED(dxrCommandListResult)) { printf("DXR Command List query failed - DirectX Raytracing unavailable.\n"); return dxrCommandListResult; }

	// We have DXR support
	dxrAvailable = true;
	printf("DXR initialization success!\n");

	// Proceed with setup
	CreateRaytracingRootSignatures();
	CreateRaytracingPipelineState(raytracingShaderLibraryFile);
	CreateShaderTable();
	CreateRaytracingOutputUAV(outputWidth, outputHeight);

	// All set
	dxrInitialized = true;
	return S_OK;
}


// --------------------------------------------------------
// Creates the root signatures necessary for raytracing:
//  - A global signature used across all shaders
//  - A local signature used for each ray hit
// --------------------------------------------------------
void RayTracing::CreateRaytracingRootSignatures()
{
	// Don't bother if DXR isn't available
	if (dxrInitialized || !dxrAvailable)
		return;

	// Create a global root signature shared across all raytracing shaders
	{
		// Two descriptor ranges
		// 1: The output texture, which is an unordered access view (UAV)
		// 2: Two separate SRVs, which are the index and vertex data of the geometry
		D3D12_DESCRIPTOR_RANGE outputUAVRange = {};
		outputUAVRange.BaseShaderRegister = 0;
		outputUAVRange.NumDescriptors = 1;
		outputUAVRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		outputUAVRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		outputUAVRange.RegisterSpace = 0;

		D3D12_DESCRIPTOR_RANGE cbufferRange = {};
		cbufferRange.BaseShaderRegister = 0;
		cbufferRange.NumDescriptors = 1;
		cbufferRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		cbufferRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		cbufferRange.RegisterSpace = 0;

		// Set up the root parameters for the global signature (of which there are four)
		// These need to match the shader(s) we'll be using
		D3D12_ROOT_PARAMETER rootParams[3] = {};
		{
			// First param is the UAV range for the output texture
			rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
			rootParams[0].DescriptorTable.pDescriptorRanges = &outputUAVRange;

			// Second param is an SRV for the acceleration structure
			rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
			rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParams[1].Descriptor.ShaderRegister = 0;
			rootParams[1].Descriptor.RegisterSpace = 0;

			// Third is constant buffer for the overall scene (camera matrices, lights, etc.)
			rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
			rootParams[2].DescriptorTable.pDescriptorRanges = &cbufferRange;
		}

		// Create the global root signature
		Microsoft::WRL::ComPtr<ID3DBlob> blob;
		Microsoft::WRL::ComPtr<ID3DBlob> errors;
		D3D12_ROOT_SIGNATURE_DESC globalRootSigDesc = {};
		globalRootSigDesc.NumParameters = ARRAYSIZE(rootParams);
		globalRootSigDesc.pParameters = rootParams;
		globalRootSigDesc.NumStaticSamplers = 0;
		globalRootSigDesc.pStaticSamplers = 0;
		globalRootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		D3D12SerializeRootSignature(&globalRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, blob.GetAddressOf(), errors.GetAddressOf());
		DXRDevice->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(GlobalRaytracingRootSig.GetAddressOf()));
	}

	// Create a local root signature enabling shaders to have unique data from shader tables
	{
		// cbuffer for hit group data
		D3D12_DESCRIPTOR_RANGE cbufferRange = {};
		cbufferRange.BaseShaderRegister = 1;
		cbufferRange.NumDescriptors = 1;
		cbufferRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		cbufferRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		cbufferRange.RegisterSpace = 0;

		// Table of 2 starting at register(t1)
		D3D12_DESCRIPTOR_RANGE geometrySRVRange = {};
		geometrySRVRange.BaseShaderRegister = 1;
		geometrySRVRange.NumDescriptors = 2;
		geometrySRVRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		geometrySRVRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		geometrySRVRange.RegisterSpace = 0;

		// Two params: Tables for constant buffer and geometry
		D3D12_ROOT_PARAMETER rootParams[2] = {};

		// Constant buffer at register(b1)
		rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[0].DescriptorTable.pDescriptorRanges = &cbufferRange;

		// Range of SRVs for geometry (verts & indices)
		rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[1].DescriptorTable.pDescriptorRanges = &geometrySRVRange;

		// Create the local root sig (ensure we denote it as a local sig)
		Microsoft::WRL::ComPtr<ID3DBlob> blob;
		Microsoft::WRL::ComPtr<ID3DBlob> errors;
		D3D12_ROOT_SIGNATURE_DESC localRootSigDesc = {};
		localRootSigDesc.NumParameters = ARRAYSIZE(rootParams);
		localRootSigDesc.pParameters = rootParams;
		localRootSigDesc.NumStaticSamplers = 0;
		localRootSigDesc.pStaticSamplers = 0;
		localRootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE; // DENOTE AS LOCAL!

		D3D12SerializeRootSignature(&localRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, blob.GetAddressOf(), errors.GetAddressOf());
		DXRDevice->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(LocalRaytracingRootSig.GetAddressOf()));
	}
}


// --------------------------------------------------------
// Creates the raytracing pipeline state, which holds
// information about the shaders, payload, root signatures, etc.
// --------------------------------------------------------
void RayTracing::CreateRaytracingPipelineState(std::wstring raytracingShaderLibraryFile)
{
	// Don't bother if DXR isn't available
	if (dxrInitialized || !dxrAvailable)
		return;

	// Read the pre-compiled shader library to a blob
	Microsoft::WRL::ComPtr<ID3DBlob> blob;
	D3DReadFileToBlob(raytracingShaderLibraryFile.c_str(), blob.GetAddressOf());

	// There are ten subobjects that make up our raytracing pipeline object:
	// - Ray generation shader
	// - Miss shader
	// - Closest hit shader
	// - Hit group (group of all "hit"-type shaders, which is just "closest hit" for us)
	// - Payload configuration
	// - Association of payload to shaders
	// - Local root signature
	// - Association of local root sig to shader
	// - Global root signature
	// - Overall pipeline config
	D3D12_STATE_SUBOBJECT subobjects[10] = {};

	// === Ray generation shader ===
	D3D12_EXPORT_DESC rayGenExportDesc = {};
	rayGenExportDesc.Name = L"RayGen";
	rayGenExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

	D3D12_DXIL_LIBRARY_DESC	rayGenLibDesc = {};
	rayGenLibDesc.DXILLibrary.BytecodeLength = blob->GetBufferSize();
	rayGenLibDesc.DXILLibrary.pShaderBytecode = blob->GetBufferPointer();
	rayGenLibDesc.NumExports = 1;
	rayGenLibDesc.pExports = &rayGenExportDesc;

	D3D12_STATE_SUBOBJECT rayGenSubObj = {};
	rayGenSubObj.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
	rayGenSubObj.pDesc = &rayGenLibDesc;

	subobjects[0] = rayGenSubObj;

	// === Miss shader ===
	D3D12_EXPORT_DESC missExportDesc = {};
	missExportDesc.Name = L"Miss";
	missExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

	D3D12_DXIL_LIBRARY_DESC	missLibDesc = {};
	missLibDesc.DXILLibrary.BytecodeLength = blob->GetBufferSize();
	missLibDesc.DXILLibrary.pShaderBytecode = blob->GetBufferPointer();
	missLibDesc.NumExports = 1;
	missLibDesc.pExports = &missExportDesc;

	D3D12_STATE_SUBOBJECT missSubObj = {};
	missSubObj.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
	missSubObj.pDesc = &missLibDesc;

	subobjects[1] = missSubObj;

	// === Closest hit shader ===
	D3D12_EXPORT_DESC closestHitExportDesc = {};
	closestHitExportDesc.Name = L"ClosestHit";
	closestHitExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

	D3D12_DXIL_LIBRARY_DESC	closestHitLibDesc = {};
	closestHitLibDesc.DXILLibrary.BytecodeLength = blob->GetBufferSize();
	closestHitLibDesc.DXILLibrary.pShaderBytecode = blob->GetBufferPointer();
	closestHitLibDesc.NumExports = 1;
	closestHitLibDesc.pExports = &closestHitExportDesc;

	D3D12_STATE_SUBOBJECT closestHitSubObj = {};
	closestHitSubObj.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
	closestHitSubObj.pDesc = &closestHitLibDesc;

	subobjects[2] = closestHitSubObj;


	// === Hit group ===
	D3D12_HIT_GROUP_DESC hitGroupDesc = {};
	hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";
	hitGroupDesc.HitGroupExport = L"HitGroup";

	D3D12_STATE_SUBOBJECT hitGroup = {};
	hitGroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
	hitGroup.pDesc = &hitGroupDesc;

	subobjects[3] = hitGroup;

	// === Shader config (payload) ===
	D3D12_RAYTRACING_SHADER_CONFIG shaderConfigDesc = {};
	shaderConfigDesc.MaxPayloadSizeInBytes = sizeof(DirectX::XMFLOAT3);	// Assuming a float3 color for now
	shaderConfigDesc.MaxAttributeSizeInBytes = sizeof(DirectX::XMFLOAT2); // Assuming a float2 for barycentric coords for now

	D3D12_STATE_SUBOBJECT shaderConfigSubObj = {};
	shaderConfigSubObj.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
	shaderConfigSubObj.pDesc = &shaderConfigDesc;

	subobjects[4] = shaderConfigSubObj;

	// === Association - Payload and shaders ===
	// Names of shaders that use the payload
	const wchar_t* payloadShaderNames[] = { L"RayGen", L"Miss", L"HitGroup" };

	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderPayloadAssociation = {};
	shaderPayloadAssociation.NumExports = ARRAYSIZE(payloadShaderNames);
	shaderPayloadAssociation.pExports = payloadShaderNames;
	shaderPayloadAssociation.pSubobjectToAssociate = &subobjects[4]; // Payload config above!

	D3D12_STATE_SUBOBJECT shaderPayloadAssociationObject = {};
	shaderPayloadAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
	shaderPayloadAssociationObject.pDesc = &shaderPayloadAssociation;

	subobjects[5] = shaderPayloadAssociationObject;

	// === Local root signature ===
	D3D12_STATE_SUBOBJECT localRootSigSubObj = {};
	localRootSigSubObj.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
	localRootSigSubObj.pDesc = LocalRaytracingRootSig.GetAddressOf();

	subobjects[6] = localRootSigSubObj;

	// === Association - Shaders and local root sig ===
	// Names of shaders that use the root sig
	const wchar_t* rootSigShaderNames[] = { L"RayGen", L"Miss", L"HitGroup" };

	// Add a state subobject for the association between the RayGen shader and the local root signature
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION rootSigAssociation = {};
	rootSigAssociation.NumExports = ARRAYSIZE(rootSigShaderNames);
	rootSigAssociation.pExports = rootSigShaderNames;
	rootSigAssociation.pSubobjectToAssociate = &subobjects[6]; // Root sig above

	D3D12_STATE_SUBOBJECT rootSigAssociationSubObj = {};
	rootSigAssociationSubObj.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
	rootSigAssociationSubObj.pDesc = &rootSigAssociation;

	subobjects[7] = rootSigAssociationSubObj;

	// === Global root sig ===
	D3D12_STATE_SUBOBJECT globalRootSigSubObj = {};
	globalRootSigSubObj.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
	globalRootSigSubObj.pDesc = GlobalRaytracingRootSig.GetAddressOf();

	subobjects[8] = globalRootSigSubObj;

	// === Pipeline config ===
	// Add a state subobject for the ray tracing pipeline config
	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
	pipelineConfig.MaxTraceRecursionDepth = D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH;

	D3D12_STATE_SUBOBJECT pipelineConfigSubObj = {};
	pipelineConfigSubObj.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
	pipelineConfigSubObj.pDesc = &pipelineConfig;

	subobjects[9] = pipelineConfigSubObj;

	// === Finalize state ===
	D3D12_STATE_OBJECT_DESC raytracingPipelineDesc = {};
	raytracingPipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	raytracingPipelineDesc.NumSubobjects = ARRAYSIZE(subobjects);
	raytracingPipelineDesc.pSubobjects = subobjects;

	// Create the state and also query it for its properties
	DXRDevice->CreateStateObject(&raytracingPipelineDesc, IID_PPV_ARGS(RaytracingPipelineStateObject.GetAddressOf()));
	RaytracingPipelineStateObject->QueryInterface(IID_PPV_ARGS(&RaytracingPipelineProperties));
}


// --------------------------------------------------------
// Sets up the shader table, which holds shader identifiers
// and local root signatures for all possible shaders
// used during raytracing.  Note that this is just a big
// chunk of GPU memory we need to manage ourselves.
// --------------------------------------------------------
void RayTracing::CreateShaderTable()
{
	// Don't bother if DXR isn't available
	if (dxrInitialized || !dxrAvailable)
		return;

	// Create the table of shaders and their data to use for rays
	// 0 - Ray generation shader
	// 1 - Miss shader
	// 2 - Closest hit shader
	// Note: All records must have the same size, so we need to calculate
	//       the size of the largest possible entry for our program
	//       - This will be the default (32) + one descriptor table pointer (8)
	//       - This also must be aligned up to D3D12_RAYTRACING_SHADER_BINDING_TABLE_RECORD_BYTE_ALIGNMENT
	UINT64 shaderTableRayGenRecordSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	UINT64 shaderTableMissRecordSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	UINT64 shaderTableHitGroupRecordSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(D3D12_GPU_DESCRIPTOR_HANDLE) * 2; // CBV & SRV

	// Align them
	shaderTableRayGenRecordSize = ALIGN(shaderTableRayGenRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
	shaderTableMissRecordSize = ALIGN(shaderTableMissRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
	shaderTableHitGroupRecordSize = ALIGN(shaderTableHitGroupRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

	// Which is largest?
	ShaderTableRecordSize = max(shaderTableRayGenRecordSize, max(shaderTableMissRecordSize, shaderTableHitGroupRecordSize));

	// How big should the table be?  Need a record for each of 3 shaders (in our simple demo)
	UINT64 shaderTableSize = ShaderTableRecordSize * 3;
	shaderTableSize = ALIGN(shaderTableSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

	// Create the shader table buffer and map it so we can write to it
	ShaderTable = Graphics::CreateBuffer(shaderTableSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
	unsigned char* shaderTableData = 0;
	ShaderTable->Map(0, 0, (void**)&shaderTableData);

	// Mem copy each record in: ray gen, miss and the overall hit group (from CreateRaytracingPipelineState() above)
	memcpy(shaderTableData, RaytracingPipelineProperties->GetShaderIdentifier(L"RayGen"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
	shaderTableData += ShaderTableRecordSize;

	memcpy(shaderTableData, RaytracingPipelineProperties->GetShaderIdentifier(L"Miss"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
	shaderTableData += ShaderTableRecordSize;

	memcpy(shaderTableData, RaytracingPipelineProperties->GetShaderIdentifier(L"HitGroup"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

	// We'll eventually need to memcpy per-object data to the shader table, but we don't have that yet

	// Unmap
	ShaderTable->Unmap(0, 0);
}


// --------------------------------------------------------
// Creates a texture & wraps it with an Unordered Access View,
// allowing shaders to directly write into this memory.  The
// data in this texture will later be directly copied to the
// back buffer after raytracing is complete.
// --------------------------------------------------------
void RayTracing::CreateRaytracingOutputUAV(unsigned int width, unsigned int height)
{
	// Default heap for output buffer
	D3D12_HEAP_PROPERTIES heapDesc = {};
	heapDesc.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapDesc.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapDesc.CreationNodeMask = 0;
	heapDesc.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapDesc.VisibleNodeMask = 0;

	// Describe the final output resource (UAV)
	D3D12_RESOURCE_DESC desc = {};
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	desc.Width = width;
	desc.Height = height;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	DXRDevice->CreateCommittedResource(
		&heapDesc,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		0,
		IID_PPV_ARGS(RaytracingOutput.GetAddressOf()));

	// Do we have a UAV alrady?
	if (!RaytracingOutputUAV_GPU.ptr)
	{
		// Nope, so reserve a spot
		Graphics::ReserveSrvUavDescriptorHeapSlot(
			&RaytracingOutputUAV_CPU,
			&RaytracingOutputUAV_GPU);
	}

	// Set up the UAV
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	DXRDevice->CreateUnorderedAccessView(
		RaytracingOutput.Get(),
		0,
		&uavDesc,
		RaytracingOutputUAV_CPU);
}


// --------------------------------------------------------
// If the window size changes, so too should the output texture
// --------------------------------------------------------
void RayTracing::ResizeOutputUAV(
	unsigned int outputWidth,
	unsigned int outputHeight)
{
	if (!dxrInitialized || !dxrAvailable)
		return;

	// Wait for the GPU to be done
	Graphics::WaitForGPU();

	// Reset and re-created the buffer
	RaytracingOutput.Reset();
	CreateRaytracingOutputUAV(outputWidth, outputHeight);
}


// --------------------------------------------------------
// Creates a BLAS for a particular mesh.  
// 
// NOTE: This demo assumes exactly one BLAS, so running this 
// method more than once is not advised!
// --------------------------------------------------------
void RayTracing::CreateBLAS(std::shared_ptr<Mesh> mesh)
{
	// Don't bother if DXR isn't available
	if (!dxrAvailable)
		return;

	// Create the Bottom Level accel structure for this mesh
	// Note: Currently, this is the one and only BLAS in our simple implementation!

	// Describe the geometry data we intend to store in this BLAS
	D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
	geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometryDesc.Triangles.VertexBuffer.StartAddress = mesh->GetVBResource()->GetGPUVirtualAddress();
	geometryDesc.Triangles.VertexBuffer.StrideInBytes = mesh->GetVBView().StrideInBytes;
	geometryDesc.Triangles.VertexCount = static_cast<UINT>(mesh->GetVertexCount());
	geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geometryDesc.Triangles.IndexBuffer = mesh->GetIBResource()->GetGPUVirtualAddress();
	geometryDesc.Triangles.IndexFormat = mesh->GetIBView().Format;
	geometryDesc.Triangles.IndexCount = static_cast<UINT>(mesh->GetIndexCount());
	geometryDesc.Triangles.Transform3x4 = 0;
	geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE; // Performance boost when dealing with opaque geometry

	// Describe our overall input so we can get sizing info
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS accelStructInputs = {};
	accelStructInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	accelStructInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	accelStructInputs.pGeometryDescs = &geometryDesc;
	accelStructInputs.NumDescs = 1;
	accelStructInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO accelStructPrebuildInfo = {};
	DXRDevice->GetRaytracingAccelerationStructurePrebuildInfo(&accelStructInputs, &accelStructPrebuildInfo);

	// Handle alignment requirements ourselves
	accelStructPrebuildInfo.ScratchDataSizeInBytes = ALIGN(accelStructPrebuildInfo.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
	accelStructPrebuildInfo.ResultDataMaxSizeInBytes = ALIGN(accelStructPrebuildInfo.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

	// Create a scratch buffer so the device has a place to temporarily store data
	BLASScratchBuffer = Graphics::CreateBuffer(
		accelStructPrebuildInfo.ScratchDataSizeInBytes,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));

	// Create the final buffer for the BLAS
	BLAS = Graphics::CreateBuffer(
		accelStructPrebuildInfo.ResultDataMaxSizeInBytes,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));

	// Describe the final BLAS and set up the build
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs = accelStructInputs;
	buildDesc.ScratchAccelerationStructureData = BLASScratchBuffer->GetGPUVirtualAddress();
	buildDesc.DestAccelerationStructureData = BLAS->GetGPUVirtualAddress();
	DXRCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, 0);

	// Set up a barrier to wait until the BLAS is actually built to proceed
	D3D12_RESOURCE_BARRIER blasBarrier = {};
	blasBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	blasBarrier.UAV.pResource = BLAS.Get();
	blasBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	DXRCommandList->ResourceBarrier(1, &blasBarrier);

	// Create two SRVs for the index and vertex buffers
	// Note: These must come one after the other in the descriptor heap, and index must come first
	//       This is due to the way we've set up the root signature (expects a table of these)
	D3D12_CPU_DESCRIPTOR_HANDLE ib_cpu, vb_cpu;
	Graphics::ReserveSrvUavDescriptorHeapSlot(&ib_cpu, &indexBufferSRV);
	Graphics::ReserveSrvUavDescriptorHeapSlot(&vb_cpu, &vertexBufferSRV);

	// Index buffer SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC indexSRVDesc = {};
	indexSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	indexSRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	indexSRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
	indexSRVDesc.Buffer.StructureByteStride = 0;
	indexSRVDesc.Buffer.FirstElement = 0;
	indexSRVDesc.Buffer.NumElements = mesh->GetIndexCount();
	indexSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	DXRDevice->CreateShaderResourceView(mesh->GetIBResource().Get(), &indexSRVDesc, ib_cpu);

	// Vertex buffer SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC vertexSRVDesc = {};
	vertexSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	vertexSRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	vertexSRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
	vertexSRVDesc.Buffer.StructureByteStride = 0;
	vertexSRVDesc.Buffer.FirstElement = 0;
	vertexSRVDesc.Buffer.NumElements = (mesh->GetVertexCount() * sizeof(Vertex)) / sizeof(float); // How many floats total?
	vertexSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	DXRDevice->CreateShaderResourceView(mesh->GetVBResource().Get(), &vertexSRVDesc, vb_cpu);


	// We need to put this mesh's SRVs into the shader table
	// - In a larger application, each unique mesh will need its own entry in the shader table!
	{
		unsigned char* tablePointer = 0;
		ShaderTable->Map(0, 0, (void**)&tablePointer);

		// Get past the raygen and miss shaders in the shader table
		tablePointer += ShaderTableRecordSize + ShaderTableRecordSize;

		// In the shader table, we need to get past the identifier and first CBV descriptor
		tablePointer += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(D3D12_GPU_DESCRIPTOR_HANDLE);

		// Memcpy the index buffer's SRV to the table
		// - This is assuming that the index buffer SRV is IMMEDIATELY followed by the vertex buffer SRV in the heap
		memcpy(
			tablePointer,
			&indexBufferSRV,
			sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));

		// All done
		ShaderTable->Unmap(0, 0);
	}
}


// --------------------------------------------------------
// Creates the top level accel structure, which can be made
// up of one or more BLAS instances, each with their own
// unique transform.  This demo uses exactly one BLAS instance.
// --------------------------------------------------------
void RayTracing::CreateTLAS()
{
	// Don't bother if DXR isn't available or the AS is finalized already
	if (!dxrAvailable)
		return;

	// Describe the BLAS instance(s) that make up the TLAS
	D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
	instanceDesc.InstanceID = 0;
	instanceDesc.InstanceContributionToHitGroupIndex = 0;
	instanceDesc.InstanceMask = 0xFF;
	instanceDesc.Transform[0][0] = 1; // Setting up a simple identity matrix here
	instanceDesc.Transform[1][1] = 1;
	instanceDesc.Transform[2][2] = 1;
	instanceDesc.AccelerationStructure = BLAS->GetGPUVirtualAddress();
	instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;

	// The instance description actually needs to be in a buffer
	// on the GPU, so we need to make that buffer and toss it in
	// there ourselves (and keep the pointer long enough to finish the work)
	TLASInstanceDescBuffer = Graphics::CreateBuffer(
		sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
		D3D12_HEAP_TYPE_UPLOAD,
		D3D12_RESOURCE_STATE_GENERIC_READ);

	// Copy the description into the new buffer
	unsigned char* mapped = 0;
	TLASInstanceDescBuffer->Map(0, 0, (void**)&mapped);
	memcpy(mapped, &instanceDesc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
	TLASInstanceDescBuffer->Unmap(0, 0);

	// Describe our overall input so we can get sizing info
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS accelStructInputs = {};
	accelStructInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	accelStructInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	accelStructInputs.InstanceDescs = TLASInstanceDescBuffer->GetGPUVirtualAddress();
	accelStructInputs.NumDescs = 1;
	accelStructInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO accelStructPrebuildInfo = {};
	DXRDevice->GetRaytracingAccelerationStructurePrebuildInfo(&accelStructInputs, &accelStructPrebuildInfo);

	// Handle alignment requirements ourselves
	accelStructPrebuildInfo.ScratchDataSizeInBytes = ALIGN(accelStructPrebuildInfo.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
	accelStructPrebuildInfo.ResultDataMaxSizeInBytes = ALIGN(accelStructPrebuildInfo.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

	// Create a scratch buffer so the device has a place to temporarily store data
	TLASScratchBuffer = Graphics::CreateBuffer(
		accelStructPrebuildInfo.ScratchDataSizeInBytes,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));

	// Create the final buffer for the TLAS
	TLAS = Graphics::CreateBuffer(
		accelStructPrebuildInfo.ResultDataMaxSizeInBytes,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));

	// Describe the final TLAS and set up the build
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs = accelStructInputs;
	buildDesc.ScratchAccelerationStructureData = TLASScratchBuffer->GetGPUVirtualAddress();
	buildDesc.DestAccelerationStructureData = TLAS->GetGPUVirtualAddress();
	DXRCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, 0);

	// Set up a barrier to wait until the TLAS is actually built to proceed
	// Note: Probably unnecessary because we're about to execute and wait below,
	//       but keeping this here in the event we adjust when we execute.
	D3D12_RESOURCE_BARRIER tlasBarrier = {};
	tlasBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	tlasBarrier.UAV.pResource = TLAS.Get();
	tlasBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	DXRCommandList->ResourceBarrier(1, &tlasBarrier);


	// All done - execute, wait and reset command list
	DXRCommandList->Close();

	ID3D12CommandList* lists[] = { DXRCommandList.Get() };
	Graphics::CommandQueue->ExecuteCommandLists(1, lists);

	Graphics::WaitForGPU();
	Graphics::ResetAllocatorAndCommandList(0);
}


// --------------------------------------------------------
// Performs the actual raytracing work
// --------------------------------------------------------
void RayTracing::Raytrace(std::shared_ptr<Camera> camera, Microsoft::WRL::ComPtr<ID3D12Resource> currentBackBuffer)
{
	if (!dxrInitialized || !dxrAvailable)
		return;

	// Transition the output-related resources to the proper states
	D3D12_RESOURCE_BARRIER outputBarriers[2] = {};
	{
		// Back buffer needs to be COPY DESTINATION (for later)
		outputBarriers[0].Transition.pResource = currentBackBuffer.Get();
		outputBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		outputBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		outputBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		// Raytracing output needs to be unordered access for raytracing
		outputBarriers[1].Transition.pResource = RaytracingOutput.Get();
		outputBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		outputBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		outputBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		DXRCommandList->ResourceBarrier(2, outputBarriers);
	}

	// Grab and fill a constant buffer
	RaytracingSceneData sceneData = {};
	sceneData.cameraPosition = camera->GetTransform()->GetPosition();

	DirectX::XMFLOAT4X4 view = camera->GetView();
	DirectX::XMFLOAT4X4 proj = camera->GetProjection();
	DirectX::XMMATRIX v = DirectX::XMLoadFloat4x4(&view);
	DirectX::XMMATRIX p = DirectX::XMLoadFloat4x4(&proj);
	DirectX::XMMATRIX vp = DirectX::XMMatrixMultiply(v, p);
	DirectX::XMStoreFloat4x4(&sceneData.inverseViewProjection, XMMatrixInverse(0, vp));

	D3D12_GPU_DESCRIPTOR_HANDLE cbuffer = Graphics::FillNextConstantBufferAndGetGPUDescriptorHandle(&sceneData, sizeof(RaytracingSceneData));

	// ACTUAL RAYTRACING HERE
	{
		// Set the CBV/SRV/UAV descriptor heap
		ID3D12DescriptorHeap* heap[] = { Graphics::CBVSRVDescriptorHeap.Get() };
		DXRCommandList->SetDescriptorHeaps(1, heap);

		// Set the pipeline state for raytracing
		// Note the "1" at the end of the function call for pipeline state
		DXRCommandList->SetPipelineState1(RaytracingPipelineStateObject.Get());

		// Set the global root sig so we can also set descriptor tables
		DXRCommandList->SetComputeRootSignature(GlobalRaytracingRootSig.Get());
		DXRCommandList->SetComputeRootDescriptorTable(0,			// First table is just output UAV
			RaytracingOutputUAV_GPU);
		DXRCommandList->SetComputeRootShaderResourceView(1,			// Second is SRV for accel structure (as root SRV, no table needed)
			TLAS->GetGPUVirtualAddress());
		DXRCommandList->SetComputeRootDescriptorTable(2, cbuffer);	// Third is CBV

		// Dispatch rays
		D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};

		// Ray gen shader location in shader table
		dispatchDesc.RayGenerationShaderRecord.StartAddress = ShaderTable->GetGPUVirtualAddress();
		dispatchDesc.RayGenerationShaderRecord.SizeInBytes = ShaderTableRecordSize;

		// Miss shader location in shader table (we could have a whole sub-table of just these, but only 1 for this demo)
		dispatchDesc.MissShaderTable.StartAddress = ShaderTable->GetGPUVirtualAddress() + ShaderTableRecordSize; // Offset by 1 record
		dispatchDesc.MissShaderTable.SizeInBytes = ShaderTableRecordSize; // Assuming sizes here (might want to verify later)
		dispatchDesc.MissShaderTable.StrideInBytes = ShaderTableRecordSize;

		// Hit group location in shader table (we could have multiple types of hit shaders, but only 1 for this demo)
		dispatchDesc.HitGroupTable.StartAddress = ShaderTable->GetGPUVirtualAddress() + ShaderTableRecordSize * 2; // Offset by 2 records
		dispatchDesc.HitGroupTable.SizeInBytes = ShaderTableRecordSize; // Assuming sizes here (might want to verify later)
		dispatchDesc.HitGroupTable.StrideInBytes = ShaderTableRecordSize;

		// Set number of rays to match screen size
		dispatchDesc.Width = Window::Width();
		dispatchDesc.Height = Window::Height();
		dispatchDesc.Depth = 1; // Can have a 3D grid, but we don't need that

		// GO!
		DXRCommandList->DispatchRays(&dispatchDesc);
	}

	// Final transitions
	{
		// Transition the raytracing output to COPY SOURCE
		outputBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		outputBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		DXRCommandList->ResourceBarrier(1, &outputBarriers[1]);

		// Copy the raytracing output into the back buffer
		DXRCommandList->CopyResource(currentBackBuffer.Get(), RaytracingOutput.Get());

		// Back buffer back to PRESENT
		outputBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		outputBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		DXRCommandList->ResourceBarrier(1, &outputBarriers[0]);
	}

	// Close and execute
	{
		DXRCommandList->Close();
		ID3D12CommandList* lists[] = { DXRCommandList.Get() };
		Graphics::CommandQueue->ExecuteCommandLists(1, lists);
	}

	// Assuming the frame sync and command list reset will happen over in Game!
}
