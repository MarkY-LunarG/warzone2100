#version 450

layout(location = 0) in vec2 vertex;

void main()
{
	// control points pass through to the tessellation stages. The transform happens in the TES.
	gl_Position = vec4(vertex, 0.0, 1.0);
#ifdef WZ_WEBGPU
	// For the same gl_Position, a WebGPU framebuffer is upside down compared to Vulkan.
	// Negate Y, so that WebGPU renders the same image as Vulkan.
	gl_Position.y = -gl_Position.y;
#endif
}
