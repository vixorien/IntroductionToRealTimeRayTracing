

// === Structs ===

// Layout of data in the vertex buffer
struct Vertex
{
    float3 localPosition;
    float2 uv;
    float3 normal;
    float3 tangent;
};

// 11 floats total per vertex * 4 bytes each
static const uint VertexSizeInBytes = 11 * 4; 


// Payload for rays (data that is "sent along" with each ray during raytrace)
// Note: This should be as small as possible, and must match our C++ size definition
struct RayPayload
{
	float3 color;
};

// Note: We'll be using the built-in BuiltInTriangleIntersectionAttributes struct
// for triangle attributes, so no need to define our own.  It contains a single float2.



// === Constant buffers ===

cbuffer SceneData : register(b0)
{
	matrix inverseViewProjection;
	float3 cameraPosition;
};


// === Resources ===

// Output UAV 
RWTexture2D<float4> OutputColor				: register(u0);

// The actual scene we want to trace through (a TLAS)
RaytracingAccelerationStructure SceneTLAS	: register(t0);

// Geometry buffers
ByteAddressBuffer IndexBuffer        		: register(t1);
ByteAddressBuffer VertexBuffer				: register(t2);


// === Helpers ===

// Loads the indices of the specified triangle from the index buffer
uint3 LoadIndices(uint triangleIndex)
{
	// What is the start index of this triangle's indices?
	uint indicesStart = triangleIndex * 3;

	// Adjust by the byte size before loading
	return IndexBuffer.Load3(indicesStart * 4); // 4 bytes per index
}


// Barycentric interpolation of data from the triangle's vertices
Vertex InterpolateVertices(uint triangleIndex, float2 barycentrics)
{
	// Calculate the barycentric data for vertex interpolation
	float3 barycentricData = float3(
		1.0f - barycentrics.x - barycentrics.y,
		barycentrics.x,
		barycentrics.y);

	// Grab the indices
	uint3 indices = LoadIndices(triangleIndex);

	// Set up the final vertex
    Vertex vert = (Vertex)0;

	// Loop through the barycentric data and interpolate
	for (uint i = 0; i < 3; i++)
	{
		// Get the index of the first piece of data for this vertex
		uint dataIndex = indices[i] * VertexSizeInBytes;

		// Grab the position and offset
		vert.localPosition += asfloat(VertexBuffer.Load3(dataIndex)) * barycentricData[i];
		dataIndex += 3 * 4; // 3 floats * 4 bytes per float

		// UV
		vert.uv += asfloat(VertexBuffer.Load2(dataIndex)) * barycentricData[i];
		dataIndex += 2 * 4; // 2 floats * 4 bytes per float

		// Normal
		vert.normal += asfloat(VertexBuffer.Load3(dataIndex)) * barycentricData[i];
		dataIndex += 3 * 4; // 3 floats * 4 bytes per float

		// Tangent (no offset at the end, since we start over after looping)
		vert.tangent += asfloat(VertexBuffer.Load3(dataIndex)) * barycentricData[i];
	}

	// Final interpolated vertex data is ready
	return vert;
}


// Calculates an origin and direction from the camera for specific pixel indices
RayDesc CalcRayFromCamera(float2 rayIndices)
{
	// Offset to the middle of the pixel
	float2 pixel = rayIndices + 0.5f;
	float2 screenPos = pixel / DispatchRaysDimensions().xy * 2.0f - 1.0f;
	screenPos.y = -screenPos.y;

	// Unproject the coords
	float4 worldPos = mul(inverseViewProjection, float4(screenPos, 0, 1));
	worldPos.xyz /= worldPos.w;

	// Set up the ray
	RayDesc ray;
	ray.Origin = cameraPosition.xyz;
	ray.Direction = normalize(worldPos.xyz - ray.Origin);
	ray.TMin = 0.01f;
	ray.TMax = 1000.0f;
	return ray;
}


// === Shaders ===

// Ray generation shader - Launched once for each ray we want to generate
// (which is generally once per pixel of our output texture)
[shader("raygeneration")]
void RayGen()
{
	// Get the ray indices
	uint2 rayIndices = DispatchRaysIndex().xy;

	// Calculate the ray from the camera through a particular
	// pixel of the output buffer using this shader's indices
	RayDesc ray = CalcRayFromCamera(rayIndices);

	// Set up the payload for the ray
	// This initializes the struct to all zeros
	RayPayload payload = (RayPayload)0;

	// Perform the ray trace for this ray
	TraceRay(
		SceneTLAS,
		RAY_FLAG_NONE,
		0xFF,
		0,
		0,
		0,
		ray,
		payload);

	// Set the final color of the buffer
	OutputColor[rayIndices] = float4(payload.color, 1);
}


// Miss shader - What happens if the ray doesn't hit anything?
[shader("miss")]
void Miss(inout RayPayload payload)
{
	// Nothing was hit, so return black for now.
	// Ideally this is where we would do skybox stuff!
    payload.color = float3(0.4f, 0.6f, 0.75f);
}


// Closest hit shader - Runs the first time a ray hits anything
[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes hitAttributes)
{
	// Grab the index of the triangle we hit
	uint triangleIndex = PrimitiveIndex();

	// Get the interpolated vertex data
	Vertex interpolatedVert = InterpolateVertices(triangleIndex, hitAttributes.barycentrics);

	// Use the resulting data to set the final color
	// Note: Here is where we would do actual shading!
	payload.color = interpolatedVert.normal;
}