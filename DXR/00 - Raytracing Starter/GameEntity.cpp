#include "GameEntity.h"
#include "BufferStructs.h"

using namespace DirectX;

GameEntity::GameEntity(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material) :
	mesh(mesh),
	material(material)
{
}

std::shared_ptr<Mesh> GameEntity::GetMesh() { return mesh; }
std::shared_ptr<Material> GameEntity::GetMaterial() { return material; }

void GameEntity::SetMesh(std::shared_ptr<Mesh> mesh) { this->mesh = mesh; }
void GameEntity::SetMaterial(std::shared_ptr<Material> material) { this->material = material; }

Transform* GameEntity::GetTransform() { return &transform; }
