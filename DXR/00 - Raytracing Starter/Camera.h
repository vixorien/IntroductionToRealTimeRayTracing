#pragma once
#include <DirectXMath.h>

#include "Transform.h"

enum class CameraProjectionType
{
	Perspective,
	Orthographic
};

class Camera
{
public:
	Camera(
		DirectX::XMFLOAT3 position, 
		float moveSpeed, 
		float mouseLookSpeed, 
		float fieldOfView, 
		float aspectRatio, 
		float nearClip = 0.01f, 
		float farClip = 100.0f, 
		CameraProjectionType projType = CameraProjectionType::Perspective);

	Camera(
		float x, float y, float z, 
		float moveSpeed, 
		float mouseLookSpeed, 
		float fieldOfView,
		float aspectRatio, 
		float nearClip = 0.01f, 
		float farClip = 100.0f, 
		CameraProjectionType projType = CameraProjectionType::Perspective);

	~Camera();

	// Updating methods
	void Update(float dt);
	void UpdateViewMatrix();
	void UpdateProjectionMatrix(float aspectRatio);

	// Getters
	DirectX::XMFLOAT4X4 GetView();
	DirectX::XMFLOAT4X4 GetProjection();
	Transform* GetTransform();
	float GetAspectRatio();

	float GetFieldOfView();
	void SetFieldOfView(float fov);

	float GetMovementSpeed();
	void SetMovementSpeed(float speed);

	float GetMouseLookSpeed();
	void SetMouseLookSpeed(float speed);

	float GetNearClip();
	void SetNearClip(float distance);

	float GetFarClip();
	void SetFarClip(float distance);

private:
	// Camera matrices
	DirectX::XMFLOAT4X4 viewMatrix;
	DirectX::XMFLOAT4X4 projMatrix;

	Transform transform;

	float movementSpeed;
	float mouseLookSpeed;

	float fieldOfView;
	float aspectRatio;
	float nearClip;
	float farClip;

	CameraProjectionType projectionType;
};

