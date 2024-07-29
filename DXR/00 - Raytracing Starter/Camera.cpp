#include "Camera.h"
#include "Input.h"

using namespace DirectX;


Camera::Camera(
	float x, 
	float y, 
	float z, 
	float moveSpeed, 
	float mouseLookSpeed, 
	float fieldOfView, 
	float aspectRatio, 
	float nearClip,
	float farClip,
	CameraProjectionType projType) :
	movementSpeed(moveSpeed),
	mouseLookSpeed(mouseLookSpeed),
	fieldOfView(fieldOfView),
	aspectRatio(aspectRatio),
	nearClip(nearClip),
	farClip(farClip),
	projectionType(projType)
{
	transform.SetPosition(x, y, z);

	UpdateViewMatrix();
	UpdateProjectionMatrix(aspectRatio);
}

Camera::Camera(
	DirectX::XMFLOAT3 position,
	float moveSpeed,
	float mouseLookSpeed,
	float fieldOfView,
	float aspectRatio,
	float nearClip,
	float farClip,
	CameraProjectionType projType) :
	movementSpeed(moveSpeed),
	mouseLookSpeed(mouseLookSpeed),
	fieldOfView(fieldOfView), 
	aspectRatio(aspectRatio),
	nearClip(nearClip),
	farClip(farClip),
	projectionType(projType)
{
	transform.SetPosition(position);

	UpdateViewMatrix();
	UpdateProjectionMatrix(aspectRatio);
}

// Nothing to really do
Camera::~Camera()
{ }


// Camera's update, which looks for key presses
void Camera::Update(float dt)
{
	// Current speed
	float speed = dt * movementSpeed;

	// Speed up or down as necessary
	if (Input::KeyDown(VK_SHIFT)) { speed *= 5; }
	if (Input::KeyDown(VK_CONTROL)) { speed *= 0.1f; }

	// Movement
	if (Input::KeyDown('W')) { transform.MoveRelative(0, 0, speed); }
	if (Input::KeyDown('S')) { transform.MoveRelative(0, 0, -speed); }
	if (Input::KeyDown('A')) { transform.MoveRelative(-speed, 0, 0); }
	if (Input::KeyDown('D')) { transform.MoveRelative(speed, 0, 0); }
	if (Input::KeyDown('X')) { transform.MoveAbsolute(0, -speed, 0); }
	if (Input::KeyDown(' ')) { transform.MoveAbsolute(0, speed, 0); }

	// Handle mouse movement only when button is down
	if (Input::MouseLeftDown())
	{
		// Calculate cursor change
		float xDiff = mouseLookSpeed * Input::GetMouseXDelta();
		float yDiff = mouseLookSpeed * Input::GetMouseYDelta();
		transform.Rotate(yDiff, xDiff, 0);

		// Clamp the X rotation
		XMFLOAT3 rot = transform.GetPitchYawRoll();
		if (rot.x > XM_PIDIV2) rot.x = XM_PIDIV2;
		if (rot.x < -XM_PIDIV2) rot.x = -XM_PIDIV2;
		transform.SetRotation(rot);
	}

	// Update the view every frame - could be optimized
	UpdateViewMatrix();

}

// Creates a new view matrix based on current position and orientation
void Camera::UpdateViewMatrix()
{
	// Get the camera's forward vector and position
	XMFLOAT3 forward = transform.GetForward();
	XMFLOAT3 pos = transform.GetPosition();

	// Make the view matrix and save
	XMMATRIX view = XMMatrixLookToLH(
		XMLoadFloat3(&pos),
		XMLoadFloat3(&forward),
		XMVectorSet(0, 1, 0, 0)); // World up axis
	XMStoreFloat4x4(&viewMatrix, view);
}

// Updates the projection matrix
void Camera::UpdateProjectionMatrix(float aspectRatio)
{
	this->aspectRatio = aspectRatio;

	XMMATRIX P;

	// Which type?
	if (projectionType == CameraProjectionType::Perspective)
	{
		P = XMMatrixPerspectiveFovLH(
			fieldOfView,		// Field of View Angle
			aspectRatio,		// Aspect ratio
			nearClip,			// Near clip plane distance
			farClip);			// Far clip plane distance
	}
	else // CameraProjectionType::ORTHOGRAPHIC
	{
		P = XMMatrixOrthographicLH(
			2.0f * aspectRatio,	// Projection width (in world units)
			2.0f,				// Projection height (in world units)
			nearClip,			// Near clip plane distance 
			farClip);			// Far clip plane distance
	}

	XMStoreFloat4x4(&projMatrix, P);
}

DirectX::XMFLOAT4X4 Camera::GetView() { return viewMatrix; }
DirectX::XMFLOAT4X4 Camera::GetProjection() { return projMatrix; }
Transform* Camera::GetTransform() { return &transform; }

float Camera::GetAspectRatio() { return aspectRatio; }

float Camera::GetFieldOfView() { return fieldOfView; }
void Camera::SetFieldOfView(float fov) { fieldOfView = fov; }

float Camera::GetMovementSpeed() { return movementSpeed; }
void Camera::SetMovementSpeed(float speed) { movementSpeed = speed; }

float Camera::GetMouseLookSpeed() { return mouseLookSpeed; }
void Camera::SetMouseLookSpeed(float speed) { mouseLookSpeed = speed; }

float Camera::GetNearClip() { return nearClip; }
void Camera::SetNearClip(float distance) { nearClip = distance; }

float Camera::GetFarClip() { return farClip; }
void Camera::SetFarClip(float distance) { farClip = distance; }


