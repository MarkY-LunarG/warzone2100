#version 450

layout(set = 0, binding = 0) uniform cbuffer {
	mat4 ModelViewProjectionMatrix;
	vec4 color;
};

layout(location = 0)in vec4 vertex;

void main()
{
	gl_Position = ModelViewProjectionMatrix * vertex;
	gl_Position.y *= -1.;
	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
#ifdef WZ_WEBGPU
	// For the same gl_Position, a WebGPU framebuffer is upside down compared to Vulkan.
	// Negate Y, so that WebGPU renders the same image as Vulkan.
	gl_Position.y = -gl_Position.y;
#endif
}