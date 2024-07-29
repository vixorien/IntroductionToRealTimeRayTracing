#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include "Vertex.h"


class Mesh
{
public:
	Mesh(Vertex* vertArray, int numVerts, unsigned int* indexArray, int numIndices);
	Mesh(const wchar_t* objFile);

	D3D12_VERTEX_BUFFER_VIEW GetVBView() { return vbView; }
	D3D12_INDEX_BUFFER_VIEW GetIBView() { return ibView; }
	Microsoft::WRL::ComPtr<ID3D12Resource> GetVBResource() { return vertexBuffer; }
	Microsoft::WRL::ComPtr<ID3D12Resource> GetIBResource() { return indexBuffer; }
	int GetIndexCount() { return numIndices; }
	int GetVertexCount() { return numVertices; }

private:
	int numIndices;
	int numVertices;
	
	D3D12_VERTEX_BUFFER_VIEW vbView;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;

	D3D12_INDEX_BUFFER_VIEW ibView;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;

	void CalculateTangents(Vertex* verts, int numVerts, unsigned int* indices, int numIndices);
	void CreateBuffers(Vertex* vertArray, int numVerts, unsigned int* indexArray, int numIndices);
};

