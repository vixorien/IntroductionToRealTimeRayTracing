#include "Material.h"
#include "Graphics.h"

Material::Material(
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState,
	DirectX::XMFLOAT3 tint, 
	DirectX::XMFLOAT2 uvScale,
	DirectX::XMFLOAT2 uvOffset) 
	:
	pipelineState(pipelineState),
	colorTint(tint),
	uvScale(uvScale),
	uvOffset(uvOffset),
	materialTexturesFinalized(false),
	highestSRVSlot(-1)
{
	// Init remaining data
	finalGPUHandleForSRVs = {};
	ZeroMemory(textureSRVsBySlot, sizeof(D3D12_CPU_DESCRIPTOR_HANDLE) * 128);
}

// Getters
Microsoft::WRL::ComPtr<ID3D12PipelineState> Material::GetPipelineState() { return pipelineState; }
DirectX::XMFLOAT2 Material::GetUVScale() { return uvScale; }
DirectX::XMFLOAT2 Material::GetUVOffset() { return uvOffset; }
DirectX::XMFLOAT3 Material::GetColorTint() { return colorTint; }
D3D12_GPU_DESCRIPTOR_HANDLE Material::GetFinalGPUHandleForTextures() { return finalGPUHandleForSRVs; }


// Setters
void Material::SetPipelineState(Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState) { this->pipelineState = pipelineState; }
void Material::SetUVScale(DirectX::XMFLOAT2 scale) { uvScale = scale; }
void Material::SetUVOffset(DirectX::XMFLOAT2 offset) { uvOffset = offset; }
void Material::SetColorTint(DirectX::XMFLOAT3 tint) { this->colorTint = tint; }


// --------------------------------------------------------
// Adds a texture (through its SRV descriptor) to the
// material for the given slot (gpu register).  This method
// does nothing if the slot is invalid or the material 
// has already been finalized.
// 
// srvDescriptorHandle - Handle to this texture's SRV
// slot - gpu register for this texture
// --------------------------------------------------------
void Material::AddTexture(D3D12_CPU_DESCRIPTOR_HANDLE srvDescriptorHandle, int slot)
{
	// Valid slot?
	if (materialTexturesFinalized || slot < 0 || slot >= 128) 
		return;

	// Save and check if this was the highest slot
	textureSRVsBySlot[slot] = srvDescriptorHandle;
	highestSRVSlot = max(highestSRVSlot, slot);
}


// --------------------------------------------------------
// Denotes that we're done adding textures to the material,
// meaning its safe to copy all of the texture SRVs from 
// their own descriptors to the final CBV/SRV descriptor
// heap so we can access them as a group while drawing.
// --------------------------------------------------------
void Material::FinalizeTextures()
{
	// Skip if we're already set up
	if (materialTexturesFinalized)
		return;

	// One by one, copy all SRVs into the shader-visible CBV/SRV heap
	// Make sure we save the FIRST texture's GPU handle, as that
	// points to the beginning of the SRV range for this material
	for (int i = 0; i <= highestSRVSlot; i++)
	{
		// Copy a single SRV at a time since they're all
		// currently in separate heaps!
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle =
			Graphics::CopySRVsToDescriptorHeapAndGetGPUDescriptorHandle(textureSRVsBySlot[i], 1);

		// Save the first resulting handle
		if (i == 0)	{ finalGPUHandleForSRVs = gpuHandle; }
	}

	// All done with texture setup
	materialTexturesFinalized = true;
}

