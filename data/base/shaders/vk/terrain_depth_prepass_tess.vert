#version 450

layout(location = 0) in vec4 vertex;

void main()
{
	gl_Position = vertex;
#ifdef WZ_WEBGPU
	// For the same gl_Position, a WebGPU framebuffer is upside down compared to Vulkan.
	// Negate Y, so that WebGPU renders the same image as Vulkan.
	gl_Position.y = -gl_Position.y;
#endif
}
