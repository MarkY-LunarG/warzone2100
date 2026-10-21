#version 450

layout(std140, set = 0, binding = 0) uniform cbuffer
{
	mat4 ModelViewProjectionMatrix;
	vec2 from;
	vec2 to;
};

layout(location = 0) in vec4 vertex;

void main()
{
	vec4 pos = vec4(from + (to - from)*vertex.y, 0.0, 1.0);
	gl_Position = ModelViewProjectionMatrix * pos;
	gl_Position.y *= -1.;
	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
#ifdef WZ_WEBGPU
	// For the same gl_Position, a WebGPU framebuffer is upside down compared to Vulkan.
	// Negate Y, so that WebGPU renders the same image as Vulkan.
	gl_Position.y = -gl_Position.y;
#endif
}
