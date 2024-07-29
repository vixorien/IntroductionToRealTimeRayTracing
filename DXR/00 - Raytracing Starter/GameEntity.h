#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <memory>
#include "Mesh.h"
#include "Transform.h"
#include "Camera.h"
#include "Material.h"

class GameEntity
{
public:
	GameEntity(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material);

	std::shared_ptr<Mesh> GetMesh();
	std::shared_ptr<Material> GetMaterial();

	void SetMesh(std::shared_ptr<Mesh> mesh);
	void SetMaterial(std::shared_ptr<Material> material);

	Transform* GetTransform();

private:

	std::shared_ptr<Mesh> mesh;
	std::shared_ptr<Material> material;
	Transform transform;
};

