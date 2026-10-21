#version 450

// Passthrough of the tile-corner control points to the tessellation stages.
// All real work happens in terrain_combined_tess.tese.

layout(location = 0) in vec4 vertex;
layout(location = 1) in vec2 vertexTexCoord;
layout(location = 4) in vec4 vertexTangent;
layout(location = 5) in int tileNo;
layout(location = 6) in uvec4 grounds;
layout(location = 7) in vec4 groundWeights;

layout(location = 0) out vec2 tcTexCoord;
layout(location = 1) out vec4 tcTangent;
layout(location = 2) out flat int tcTileNo;
layout(location = 3) out flat uvec4 tcGrounds;
layout(location = 4) out vec4 tcGroundWeights;

void main()
{
	gl_Position = vertex;
	tcTexCoord = vertexTexCoord;
	tcTangent = vertexTangent;
	tcTileNo = tileNo;
	tcGrounds = grounds;
	tcGroundWeights = groundWeights;
#ifdef WZ_WEBGPU
	// For the same gl_Position, a WebGPU framebuffer is upside down compared to Vulkan.
	// Negate Y, so that WebGPU renders the same image as Vulkan.
	gl_Position.y = -gl_Position.y;
#endif
}
