#version 450

layout(std140, set = 0, binding = 0) uniform cbuffer {
	mat4 ModelViewProjectionMatrix;
	mat4 ViewMatrix;
};

layout(location = 0) in vec4 vertex;
layout(location = 3) in vec3 vertexNormal;

layout(location = 0) out vec3 viewNormal;

void main()
{
	vec4 position = ModelViewProjectionMatrix * vertex;
	gl_Position = position;
	gl_Position.y *= -1.;
	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;

	viewNormal = mat3(ViewMatrix) * vertexNormal;
#ifdef WZ_WEBGPU
	// For the same gl_Position, a WebGPU framebuffer is upside down compared to Vulkan.
	// Negate Y, so that WebGPU renders the same image as Vulkan.
	gl_Position.y = -gl_Position.y;
#endif
}
