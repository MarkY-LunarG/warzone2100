#version 450

layout(std140, set = 0, binding = 0) uniform cbuffer {
	mat4 ModelViewProjectionMatrix;
	mat4 ModelViewMatrix;
	vec4 color;
	vec4 fogColor;
	vec4 fogRange;
};

layout(location = 0) in vec4 vertex;
layout(location = 0) out vec3 posViewSpace;

void main()
{
	gl_Position = ModelViewProjectionMatrix * vertex;
	gl_Position.y *= -1.0;
	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
	posViewSpace = (ModelViewMatrix * vertex).xyz;
#ifdef WZ_WEBGPU
	// For the same gl_Position, a WebGPU framebuffer is upside down compared to Vulkan.
	// Negate Y, so that WebGPU renders the same image as Vulkan.
	gl_Position.y = -gl_Position.y;
#endif
}
