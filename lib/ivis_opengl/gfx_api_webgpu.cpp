/*
	This file is part of Warzone 2100.
	Copyright (C) 2026  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

// WebGPU backend (Dawn).
//
// The model follows the Vulkan backend (gfx_api_vk.cpp):
// - Each uniform block is its own bind group, with one uniform buffer at binding 0 and a
//   dynamic offset. The textures (and the point light storage buffers) are in the next group.
// - A pipeline is built for each render pass layout (attachment formats and sample count)
//   that it is bound in, the first time that it is bound there.
// - The WGSL comes from the Vulkan GLSL (data/CMakeLists.txt). A texture at GLSL binding b
//   is at binding 2b and its sampler at 2b+1; other resources are at 2b.
//
// Differences from Vulkan:
// - WebGPU tracks image layouts and barriers itself, so the render graph layout data is ignored.
// - Depth bias is pipeline state, so it is part of the pipeline variant key.
// - Uniform data and streamed vertex data are collected in CPU memory and written to the GPU
//   once, before the frame is submitted. All draws of a frame are submitted together, so an
//   offset is never used two times in one frame.
// - Border colors (shadow maps only) are not emulated yet: those samplers clamp to the edge.
//
// This file must build as C++20, because webgpu_cpp.h needs C++20.

#if defined(WZ_WEBGPU_ENABLED)

#include "lib/framework/frame.h"
#include "lib/framework/physfs_ext.h"
#include "lib/framework/wzapp.h"
#include "gfx_api_webgpu.h"
#include "shader_catalog.h"
#include "png_util.h"
#include "render_graph/attachment.h"
#include "render_graph/pass_resolve.h"
#include "render_graph/pipeline_surfaces.h"
#include "render_graph/render_pass.h"
#include "render_graph/scene_post_effects.h"

#include <webgpu/webgpu_cpp.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>

namespace
{

// The binding size of every uniform block. It is the WebGPU maxUniformBufferBindingSize
// default, and every WZ uniform block is smaller (gfx_api.h asserts this).
constexpr uint64_t UniformBindingSize = 65536;
constexpr uint64_t UniformChunkSize = 1024 * 1024;
constexpr uint64_t StreamedVertexChunkSize = 1024 * 1024;
constexpr uint32_t LightDataStorageBinding = 14; // GLSL bindings (vk/pointlights.glsl)
constexpr uint32_t LightIndexStorageBinding = 15;

uint64_t alignUp(uint64_t value, uint64_t alignment)
{
	return (value + alignment - 1) / alignment * alignment;
}

std::string to_std_string(const wgpu::StringView& value)
{
	return std::string(std::string_view(value));
}

const char* to_string(wgpu::BackendType type)
{
	switch (type)
	{
		case wgpu::BackendType::Undefined: return "Undefined";
		case wgpu::BackendType::Null: return "Null";
		case wgpu::BackendType::WebGPU: return "WebGPU";
		case wgpu::BackendType::D3D11: return "D3D11";
		case wgpu::BackendType::D3D12: return "D3D12";
		case wgpu::BackendType::Metal: return "Metal";
		case wgpu::BackendType::Vulkan: return "Vulkan";
		case wgpu::BackendType::OpenGL: return "OpenGL";
		case wgpu::BackendType::OpenGLES: return "OpenGL ES";
	}
	return "Unknown";
}

const char* to_string(wgpu::AdapterType type)
{
	switch (type)
	{
		case wgpu::AdapterType::DiscreteGPU: return "Discrete GPU";
		case wgpu::AdapterType::IntegratedGPU: return "Integrated GPU";
		case wgpu::AdapterType::CPU: return "CPU";
		case wgpu::AdapterType::Unknown: return "Unknown";
	}
	return "Unknown";
}

// MARK: Formats

wgpu::TextureFormat to_wgpu(gfx_api::pixel_format format)
{
	switch (format)
	{
		case gfx_api::pixel_format::invalid: return wgpu::TextureFormat::Undefined;
		case gfx_api::pixel_format::FORMAT_RGBA8_UNORM_PACK8: return wgpu::TextureFormat::RGBA8Unorm;
		case gfx_api::pixel_format::FORMAT_BGRA8_UNORM_PACK8: return wgpu::TextureFormat::BGRA8Unorm;
		case gfx_api::pixel_format::FORMAT_RGB8_UNORM_PACK8: return wgpu::TextureFormat::Undefined; // WebGPU has no RGB8
		case gfx_api::pixel_format::FORMAT_RG8_UNORM: return wgpu::TextureFormat::RG8Unorm;
		case gfx_api::pixel_format::FORMAT_R8_UNORM: return wgpu::TextureFormat::R8Unorm;
#if defined(__EMSCRIPTEN__)
		// The emdawnwebgpu port of Emscripten 4.0.10 has no 16-bit unorm formats (an optional feature)
		case gfx_api::pixel_format::FORMAT_R16_UNORM: return wgpu::TextureFormat::Undefined;
		case gfx_api::pixel_format::FORMAT_RG16_UNORM: return wgpu::TextureFormat::Undefined;
#else
		case gfx_api::pixel_format::FORMAT_R16_UNORM: return wgpu::TextureFormat::R16Unorm;
		case gfx_api::pixel_format::FORMAT_RG16_UNORM: return wgpu::TextureFormat::RG16Unorm;
#endif
		case gfx_api::pixel_format::FORMAT_D24_UNORM_S8: return wgpu::TextureFormat::Depth24PlusStencil8;
		case gfx_api::pixel_format::FORMAT_RGB_BC1_UNORM: return wgpu::TextureFormat::BC1RGBAUnorm;
		case gfx_api::pixel_format::FORMAT_RGBA_BC2_UNORM: return wgpu::TextureFormat::BC2RGBAUnorm;
		case gfx_api::pixel_format::FORMAT_RGBA_BC3_UNORM: return wgpu::TextureFormat::BC3RGBAUnorm;
		case gfx_api::pixel_format::FORMAT_R_BC4_UNORM: return wgpu::TextureFormat::BC4RUnorm;
		case gfx_api::pixel_format::FORMAT_RG_BC5_UNORM: return wgpu::TextureFormat::BC5RGUnorm;
		case gfx_api::pixel_format::FORMAT_RGBA_BPTC_UNORM: return wgpu::TextureFormat::BC7RGBAUnorm;
		case gfx_api::pixel_format::FORMAT_RGB8_ETC1: return wgpu::TextureFormat::ETC2RGB8Unorm; // ETC2 decodes ETC1 data
		case gfx_api::pixel_format::FORMAT_RGB8_ETC2: return wgpu::TextureFormat::ETC2RGB8Unorm;
		case gfx_api::pixel_format::FORMAT_RGBA8_ETC2_EAC: return wgpu::TextureFormat::ETC2RGBA8Unorm;
		case gfx_api::pixel_format::FORMAT_R11_EAC: return wgpu::TextureFormat::EACR11Unorm;
		case gfx_api::pixel_format::FORMAT_RG11_EAC: return wgpu::TextureFormat::EACRG11Unorm;
		case gfx_api::pixel_format::FORMAT_ASTC_4x4_UNORM: return wgpu::TextureFormat::ASTC4x4Unorm;
		case gfx_api::pixel_format::FORMAT_A2B10G10R10_UNORM_PACK32: return wgpu::TextureFormat::RGB10A2Unorm;
		case gfx_api::pixel_format::FORMAT_D32_SFLOAT_S8_UINT: return wgpu::TextureFormat::Depth32FloatStencil8;
		case gfx_api::pixel_format::FORMAT_D32_SFLOAT: return wgpu::TextureFormat::Depth32Float;
	}
	return wgpu::TextureFormat::Undefined;
}

bool isDepthFormat(wgpu::TextureFormat format)
{
	return format == wgpu::TextureFormat::Depth24PlusStencil8
		|| format == wgpu::TextureFormat::Depth32FloatStencil8
		|| format == wgpu::TextureFormat::Depth32Float
		|| format == wgpu::TextureFormat::Depth24Plus
		|| format == wgpu::TextureFormat::Depth16Unorm;
}

bool hasStencil(wgpu::TextureFormat format)
{
	return format == wgpu::TextureFormat::Depth24PlusStencil8
		|| format == wgpu::TextureFormat::Depth32FloatStencil8;
}

// The block size of a compressed format, or 1 for an uncompressed format
uint32_t blockDimension(gfx_api::pixel_format format)
{
	return gfx_api::is_uncompressed_format(format) ? 1 : 4; // BC, ETC2, EAC and ASTC 4x4 all use 4x4 blocks
}

// MARK: wgpu_image

// A texture and its views. Textures, texture arrays and pipeline surfaces all use this.
struct wgpu_image
{
	wgpu::Texture texture;
	wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
	gfx_api::pixel_format wzFormat = gfx_api::pixel_format::invalid;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t layers = 1;
	uint32_t mipLevels = 1;
	uint32_t samples = 1;

	bool create(const wgpu::Device& device, wgpu::TextureFormat _format, uint32_t _width, uint32_t _height, uint32_t _layers, uint32_t _mipLevels, uint32_t _samples, wgpu::TextureUsage usage, const std::string& label)
	{
		format = _format;
		width = std::max<uint32_t>(_width, 1);
		height = std::max<uint32_t>(_height, 1);
		layers = std::max<uint32_t>(_layers, 1);
		mipLevels = std::max<uint32_t>(_mipLevels, 1);
		samples = std::max<uint32_t>(_samples, 1);

		wgpu::TextureDescriptor desc;
		desc.label = label.c_str();
		desc.usage = usage;
		desc.dimension = wgpu::TextureDimension::e2D;
		desc.size = {width, height, layers};
		desc.format = format;
		desc.mipLevelCount = mipLevels;
		desc.sampleCount = samples;
		texture = device.CreateTexture(&desc);
		return static_cast<bool>(texture);
	}

	// The view that a shader samples
	const wgpu::TextureView& sampleView(wgpu::TextureViewDimension dimension)
	{
		wgpu::TextureView& view = (dimension == wgpu::TextureViewDimension::e2DArray) ? arraySampleView : sampleView2D;
		if (!view)
		{
			wgpu::TextureViewDescriptor desc;
			desc.dimension = dimension;
			desc.arrayLayerCount = (dimension == wgpu::TextureViewDimension::e2DArray) ? layers : 1;
			// A shader samples only the depth of a depth/stencil texture
			desc.aspect = isDepthFormat(format) ? wgpu::TextureAspect::DepthOnly : wgpu::TextureAspect::All;
			view = texture.CreateView(&desc);
		}
		return view;
	}

	// The view of one mip level and one layer that a render pass writes
	const wgpu::TextureView& attachmentView(uint32_t mipLevel, uint32_t layer)
	{
		wgpu::TextureView& view = attachmentViews[{mipLevel, layer}];
		if (!view)
		{
			wgpu::TextureViewDescriptor desc;
			desc.dimension = wgpu::TextureViewDimension::e2D;
			desc.baseMipLevel = mipLevel;
			desc.mipLevelCount = 1;
			desc.baseArrayLayer = layer;
			desc.arrayLayerCount = 1;
			view = texture.CreateView(&desc);
		}
		return view;
	}

private:
	wgpu::TextureView sampleView2D;
	wgpu::TextureView arraySampleView;
	std::map<std::pair<uint32_t, uint32_t>, wgpu::TextureView> attachmentViews;
};

// Write one image to a mip level of one layer
bool uploadImage(const wgpu::Queue& queue, wgpu_image& target, size_t mip_level, size_t offset_x, size_t offset_y, size_t layer, const iV_BaseImage& image)
{
	ASSERT_OR_RETURN(false, target.texture.Get() != nullptr, "Attempt to upload to a texture that was not created");
	ASSERT_OR_RETURN(false, image.data() != nullptr, "Attempt to upload image without data");
	ASSERT_OR_RETURN(false, image.pixel_format() == target.wzFormat, "Uploading image to texture with different format");
	ASSERT_OR_RETURN(false, mip_level < target.mipLevels, "Invalid mip level: %zu", mip_level);
	const uint32_t width = image.width();
	const uint32_t height = image.height();
	ASSERT_OR_RETURN(false, width > 0 && height > 0, "Attempt to upload texture with width or height of 0 (width: %u, height: %u)", width, height);

	const uint32_t rowLength = (image.bufferRowLength() != 0) ? image.bufferRowLength() : width;
	const uint32_t block = blockDimension(target.wzFormat);

	wgpu::TexelCopyTextureInfo destination;
	destination.texture = target.texture;
	destination.mipLevel = static_cast<uint32_t>(mip_level);
	destination.origin = {static_cast<uint32_t>(offset_x), static_cast<uint32_t>(offset_y), static_cast<uint32_t>(layer)};

	wgpu::TexelCopyBufferLayout layout;
	layout.offset = 0;
	// For a compressed format, this is the size of one row of blocks
	layout.bytesPerRow = static_cast<uint32_t>(gfx_api::format_memory_size(target.wzFormat, rowLength, 1));

	// A compressed copy covers whole blocks, also for the mip levels that are smaller than a block
	wgpu::Extent3D size = {
		static_cast<uint32_t>(alignUp(width, block)),
		static_cast<uint32_t>(alignUp(height, block)),
		1
	};
	queue.WriteTexture(&destination, image.data(), image.data_size(), &layout, &size);
	return true;
}

// MARK: Texture objects

enum class WgpuTextureKind : size_t
{
	Texture,
	TextureArray,
	Surface,
	SwapchainColor,
};

struct wgpu_context;

struct wgpu_texture final : public gfx_api::texture
{
	wgpu_image image;
	const wgpu::Queue* queue = nullptr;

	void bind() override {}
	bool upload(const size_t& mip_level, const iV_BaseImage& data) override { return uploadImage(*queue, image, mip_level, 0, 0, 0, data); }
	bool upload_sub(const size_t& mip_level, const size_t& offset_x, const size_t& offset_y, const iV_BaseImage& data) override { return uploadImage(*queue, image, mip_level, offset_x, offset_y, 0, data); }
	unsigned id() override { return 0; }
	gfx_api::texture2dDimensions get_dimensions() const override { return {image.width, image.height}; }
	size_t backend_internal_value() const override { return static_cast<size_t>(WgpuTextureKind::Texture); }
};

struct wgpu_texture_array final : public gfx_api::texture_array
{
	wgpu_image image;
	const wgpu::Queue* queue = nullptr;

	void bind() override {}
	bool upload_layer(const size_t& layer, const size_t& mip_level, const iV_BaseImage& data) override
	{
		ASSERT_OR_RETURN(false, layer < image.layers, "Invalid layer: %zu", layer);
		return uploadImage(*queue, image, mip_level, 0, 0, layer, data);
	}
	unsigned id() override { return 0; }
	size_t backend_internal_value() const override { return static_cast<size_t>(WgpuTextureKind::TextureArray); }
};

// A pipeline surface (render target), or the proxy for the swapchain image
struct wgpu_surface_texture final : public gfx_api::abstract_texture
{
	WgpuTextureKind kind = WgpuTextureKind::Surface;
	wgpu_image image;
	bool arrayTexture = false;

	void bind() override {}
	bool isArray() const override { return arrayTexture; }
	size_t backend_internal_value() const override { return static_cast<size_t>(kind); }
};

wgpu_image* imageOf(gfx_api::abstract_texture* texture)
{
	if (texture == nullptr)
	{
		return nullptr;
	}
	switch (static_cast<WgpuTextureKind>(texture->backend_internal_value()))
	{
		case WgpuTextureKind::Texture: return &static_cast<wgpu_texture*>(texture)->image;
		case WgpuTextureKind::TextureArray: return &static_cast<wgpu_texture_array*>(texture)->image;
		case WgpuTextureKind::Surface: return &static_cast<wgpu_surface_texture*>(texture)->image;
		case WgpuTextureKind::SwapchainColor: return nullptr; // the image changes each frame
	}
	return nullptr;
}

// MARK: wgpu_buffer

struct wgpu_buffer final : public gfx_api::buffer
{
	gfx_api::buffer::usage usage;
	std::string label;
	const wgpu::Device* device = nullptr;
	const wgpu::Queue* queue = nullptr;
	wgpu::Buffer object;
	size_t buffer_size = 0;
	// A CPU copy of the contents. WriteBuffer needs offsets and sizes that are multiples
	// of 4, so an unaligned update writes the aligned range around it from this copy.
	std::vector<uint8_t> shadow;

	explicit wgpu_buffer(const gfx_api::buffer::usage& _usage)
	: usage(_usage)
	{ }

	void bind() override {}

	void upload(const size_t& size, const void* data) override
	{
		ASSERT_OR_RETURN(, size > 0, "Attempt to upload buffer of size 0");
		const uint64_t allocatedSize = alignUp(size, 4);
		if (!object || object.GetSize() != allocatedSize)
		{
			// A new data store. Commands recorded earlier in this frame keep the old one.
			wgpu::BufferDescriptor desc;
			desc.label = label.c_str();
			desc.usage = wgpu::BufferUsage::CopyDst | ((usage == gfx_api::buffer::usage::index_buffer) ? wgpu::BufferUsage::Index : wgpu::BufferUsage::Vertex);
			desc.size = allocatedSize;
			object = device->CreateBuffer(&desc);
		}
		buffer_size = size;
		shadow.assign(static_cast<size_t>(allocatedSize), 0);
		if (data != nullptr)
		{
			memcpy(shadow.data(), data, size);
			queue->WriteBuffer(object, 0, shadow.data(), shadow.size());
		}
	}

	void update(const size_t& start, const size_t& size, const void* data, const update_flag flag = update_flag::none) override
	{
		ASSERT_OR_RETURN(, start < buffer_size, "Starting offset (%zu) is past end of buffer (length: %zu)", start, buffer_size);
		ASSERT_OR_RETURN(, start + size <= buffer_size, "Attempt to write past end of buffer");
		if (size == 0)
		{
			return;
		}
		memcpy(shadow.data() + start, data, size);
		const size_t alignedStart = start & ~static_cast<size_t>(3);
		const size_t alignedEnd = static_cast<size_t>(alignUp(start + size, 4));
		queue->WriteBuffer(object, alignedStart, shadow.data() + alignedStart, alignedEnd - alignedStart);
	}

	size_t current_buffer_size() override { return buffer_size; }
};

// MARK: Shader modules

struct wgsl_binding
{
	std::string type; // for example: "texture_2d<f32>", "sampler_comparison", "uniform", "storage"
	wgpu::ShaderStage stages = wgpu::ShaderStage::None;
};

struct wgpu_shader_module
{
	wgpu::ShaderModule module;
	wgpu::ShaderStage stage = wgpu::ShaderStage::None;
	std::set<uint32_t> overrideIds;                                  // @id(N) override declarations
	std::map<std::pair<uint32_t, uint32_t>, std::string> bindings;   // (group, binding) -> type
};

// Read the "@group(G) @binding(B) var NAME : TYPE;" and "@id(N) override" declarations
// that Tint writes. They give the bind group layout the exact types that the shader uses.
void parseWgslDeclarations(const std::string& code, wgpu_shader_module& out)
{
	size_t lineStart = 0;
	while (lineStart < code.size())
	{
		size_t lineEnd = code.find('\n', lineStart);
		if (lineEnd == std::string::npos)
		{
			lineEnd = code.size();
		}
		const std::string_view line(code.data() + lineStart, lineEnd - lineStart);
		lineStart = lineEnd + 1;

		auto readNumberAfter = [&line](std::string_view prefix, uint32_t& value) {
			const size_t pos = line.find(prefix);
			if (pos == std::string_view::npos)
			{
				return false;
			}
			value = static_cast<uint32_t>(strtoul(std::string(line.substr(pos + prefix.size())).c_str(), nullptr, 10));
			return true;
		};

		uint32_t id = 0;
		if (line.find("override") != std::string_view::npos && readNumberAfter("@id(", id))
		{
			out.overrideIds.insert(id);
			continue;
		}

		uint32_t group = 0;
		uint32_t binding = 0;
		if (!readNumberAfter("@group(", group) || !readNumberAfter("@binding(", binding))
		{
			continue;
		}
		std::string type;
		if (line.find("var<uniform>") != std::string_view::npos)
		{
			type = "uniform";
		}
		else if (line.find("var<storage") != std::string_view::npos)
		{
			type = "storage";
		}
		else
		{
			const size_t colon = line.rfind(':');
			const size_t semicolon = line.rfind(';');
			if (colon == std::string_view::npos || semicolon == std::string_view::npos || semicolon < colon)
			{
				continue;
			}
			type = std::string(line.substr(colon + 1, semicolon - colon - 1));
			type.erase(0, type.find_first_not_of(' '));
			type.erase(type.find_last_not_of(' ') + 1);
		}
		out.bindings[{group, binding}] = type;
	}
}

// MARK: wgpu_pipeline_state_object

struct texture_slot
{
	uint32_t textureBinding = 0;
	uint32_t samplerBinding = 0;
	gfx_api::sampler_type sampler = gfx_api::sampler_type::bilinear;
	gfx_api::pixel_format_target target = gfx_api::pixel_format_target::texture_2d;
	wgpu::TextureSampleType sampleType = wgpu::TextureSampleType::Float;
	wgpu::TextureViewDimension viewDimension = wgpu::TextureViewDimension::e2D;
	wgpu::SamplerBindingType samplerType = wgpu::SamplerBindingType::Filtering;
};

struct pipeline_variant_key
{
	std::vector<wgpu::TextureFormat> colorFormats;
	wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Undefined;
	uint32_t samples = 1;
	int32_t depthBias = 0;
	float depthBiasSlopeScale = 0.f;
	wgpu::IndexFormat stripIndexFormat = wgpu::IndexFormat::Undefined;

	bool operator<(const pipeline_variant_key& other) const
	{
		return std::tie(colorFormats, depthFormat, samples, depthBias, depthBiasSlopeScale, stripIndexFormat)
			< std::tie(other.colorFormats, other.depthFormat, other.samples, other.depthBias, other.depthBiasSlopeScale, other.stripIndexFormat);
	}
};

struct wgpu_pipeline_state_object final : public gfx_api::pipeline_state_object
{
	gfx_api::pipeline_create_info createInfo;
	wgpu_context* context = nullptr;
	wgpu_shader_module* vertexModule = nullptr;
	wgpu_shader_module* fragmentModule = nullptr;
	size_t uniformBlockCount = 0;
	bool hasLightData = false;
	std::vector<texture_slot> textureSlots;
	wgpu::BindGroupLayout textureLayout; // null if the pipeline has no textures and no light data
	wgpu::PipelineLayout layout;
	std::map<pipeline_variant_key, wgpu::RenderPipeline> variants; // a null entry means the build failed

	wgpu_pipeline_state_object(const gfx_api::pipeline_create_info& _createInfo)
	: createInfo(_createInfo)
	{ }
	~wgpu_pipeline_state_object() override;

	bool isStrip() const
	{
		return createInfo.primitive == gfx_api::primitive_type::line_strip || createInfo.primitive == gfx_api::primitive_type::triangle_strip;
	}
};

// MARK: Per-frame chunks

// A GPU buffer with a CPU copy that collects data during a frame. The used part is written
// to the GPU before the frame is submitted.
struct staging_chunk
{
	wgpu::Buffer buffer;
	std::vector<uint8_t> staging;
	uint64_t used = 0;
	wgpu::BindGroup uniformBindGroup; // uniform chunks only
};

struct chunk_allocation
{
	staging_chunk* chunk = nullptr;
	uint64_t offset = 0;
};

// MARK: wgpu_context

struct wgpu_context final : public gfx_api::context
{
public:
	explicit wgpu_context(bool _useDebug)
	: useDebug(_useDebug)
	{ }
	~wgpu_context() override;

	gfx_api::texture* create_texture(const size_t& mipmap_count, const size_t& width, const size_t& height, const gfx_api::pixel_format& internal_format, const std::string& filename) override;
	gfx_api::texture_array* create_texture_array(const size_t& mipmap_count, const size_t& layer_count, const size_t& width, const size_t& height, const gfx_api::pixel_format& internal_format, const std::string& filename) override;
	gfx_api::buffer* create_buffer_object(const gfx_api::buffer::usage& usage, const buffer_storage_hint& hint = buffer_storage_hint::static_draw, const std::string& debugName = "") override;

	gfx_api::pipeline_state_object* build_pipeline(gfx_api::pipeline_state_object* existing_pso, const gfx_api::pipeline_create_info& createInfo) override;
	void bind_pipeline(gfx_api::pipeline_state_object* pso, bool notextures) override;
	void bind_index_buffer(gfx_api::buffer& buffer, const gfx_api::index_type& index) override;
	void unbind_index_buffer(gfx_api::buffer&) override {}
	void bind_vertex_buffers(const std::size_t& first, const std::vector<std::tuple<gfx_api::buffer*, std::size_t>>& vertex_buffers_offset) override;
	void unbind_vertex_buffers(const std::size_t& first, const std::vector<std::tuple<gfx_api::buffer*, std::size_t>>& vertex_buffers_offset) override {}
	void disable_all_vertex_buffers() override {}
	void bind_streamed_vertex_buffers(const void* data, const std::size_t size) override;
	void bind_textures(const std::vector<gfx_api::texture_input>& texture_descriptions, const std::vector<gfx_api::abstract_texture*>& textures) override;
	void set_constants(const void* buffer, const size_t& size) override;
	void set_uniforms(const size_t& first, const std::vector<std::tuple<const void*, size_t>>& uniform_blocks) override;
	gfx_api::frame_uniform_allocation upload_frame_uniform_raw(const void* data, size_t size) override;
	void set_frame_uniform_at(size_t slot, const gfx_api::frame_uniform_allocation& allocation, std::type_index type) override;
	gfx_api::data_buffer_transport lightDataTransport() const override { return gfx_api::data_buffer_transport::storage_buffer; }
	void upload_light_data(const void* lights, size_t lightBytes, const void* indices, size_t indexBytes) override;
	void draw(const size_t& offset, const size_t& count, const gfx_api::primitive_type& primitive) override;
	void draw_elements(const size_t& offset, const size_t& count, const gfx_api::primitive_type& primitive, const gfx_api::index_type& index) override;
	void draw_instanced(const std::size_t& offset, const std::size_t& count, const gfx_api::primitive_type& primitive, std::size_t instance_count) override;
	void draw_elements_instanced(const std::size_t& offset, const std::size_t& count, const gfx_api::primitive_type& primitive, const gfx_api::index_type& index, std::size_t instance_count) override;
	void set_polygon_offset(const float& factor, const float& units) override;
	void set_depth_range(const float& min, const float& max) override;
	int32_t get_context_value(const context_value property) override;
	uint64_t get_estimated_vram_mb(bool dedicatedOnly) override { return 0; }

	size_t numDepthPasses() override { return depthPassCount; }
	bool setDepthPassProperties(size_t numDepthPasses, size_t depthBufferResolution) override;
	size_t getDepthPassDimensions(size_t idx) override { return depthMapSize; }
	void beginPass(const gfx_api::RenderPassDesc& pass, const gfx_api::CompiledPass* compiledPass = nullptr) override;
	void endPass(const gfx_api::CompiledPass* compiledPass = nullptr) override;
	void reconcileSwapchainAtFrameOpen() override;
	void acquireSwapchainForFrameDraw() override;
	bool canRecordSwapchainDraws() const override { return swapchainAcquired; }
	void beginScreenFrame() override;
	void finishScreenFrame() override;

	gfx_api::abstract_texture* getPipelineSurface(gfx_api::PipelineSurfaceId id) override { return pipelineSurfaces.get(id); }
	gfx_api::PipelineSurfaceUsage pipelineSurfaceUsage(gfx_api::PipelineSurfaceId id) const override { return pipelineSurfaces.usage(id); }
	const gfx_api::ResolvedSurfaceSpec& resolvedPipelineSurface(gfx_api::PipelineSurfaceId id) const override { return pipelineSurfaces.spec(id); }
	gfx_api::pixel_format getDepthStencilFormat() const override { return gfx_api::pixel_format::FORMAT_D24_UNORM_S8; }
	bool isSceneMSAAEnabled() const override { return sceneMsaaSamples > 1; }
	bool isMultisampledColorAttachment(gfx_api::abstract_texture* texture) const override;
	gfx_api::PipelineSurfaceSyncInputs pipelineSurfaceSyncInputs() const override;
	gfx_api::SurfaceCapabilityHints surfaceCapabilities() const override;
	bool ensurePipelineSurfaces(const gfx_api::ResolvedSurfaceTable& specs) override;
	optional<std::pair<uint32_t, uint32_t>> getRenderTargetDimensions(gfx_api::abstract_texture* texture) override;
	bool setSceneRenderScale(uint32_t scalePercent) override;
	bool setSceneDynamicResolution(bool enabled) override;
	bool setSceneUpscalingMode(scene_upscaling_mode mode) override;
	bool setSmaaEnabled(bool enabled) override;
	bool setSceneEffectSurfaces(gfx_api::SceneEffectSurfaces cfg) override;

	void debugStringMarker(const char* str) override;
	void debugSceneBegin(const char* descr) override;
	void debugSceneEnd(const char* descr) override;
	bool debugPerfAvailable() override { return false; }
	bool debugPerfStart(size_t sample) override { return false; }
	void debugPerfStop() override {}
	void debugPerfBegin(PERF_POINT pp, const char* descr) override {}
	void debugPerfEnd(PERF_POINT pp) override {}
	uint64_t debugGetPerfValue(PERF_POINT pp) override { return 0; }
	std::map<std::string, std::string> getBackendGameInfo() override { return backendGameInfo; }
	const std::string& getFormattedRendererInfoString() const override { return formattedRendererInfoString; }
	bool getScreenshot(std::function<void (std::unique_ptr<iV_Image>)> callback) override;
	void handleWindowSizeChange(unsigned int oldWidth, unsigned int oldHeight, unsigned int newWidth, unsigned int newHeight) override;
	std::pair<uint32_t, uint32_t> getDrawableDimensions() override;
	// The WebGPU shaders negate Y, so that they render the same image as Vulkan (see the vertex shaders)
	bool isYAxisInverted() const override { return true; }
	bool shouldDraw() override { return surfaceConfigured && !deviceLost; }
	void shutdown() override;
	const size_t& current_FrameNum() const override { return frameNum; }
	bool setSwapInterval(gfx_api::context::swap_interval_mode mode, const SetSwapIntervalCompletionHandler& completionHandler) override;
	gfx_api::context::swap_interval_mode getSwapInterval() const override { return swapMode; }
	bool textureFormatIsSupported(gfx_api::pixel_format_target target, gfx_api::pixel_format format, gfx_api::pixel_format_usage::flags usage) override;
	bool supportsMipLodBias() const override { return false; }
	bool supports2DTextureArrays() const override { return true; }
	bool supportsIntVertexAttributes() const override { return true; }
	size_t maxFramesInFlight() const override { return 2; }
	gfx_api::lighting_constants getShadowConstants() override { return shadowConstants; }
	bool setShadowConstants(gfx_api::lighting_constants values) override;
	bool supportsInstancedRendering() override { return true; }
	bool debugRecompileAllPipelines() override;

private:
	friend struct wgpu_pipeline_state_object;

	struct PipelineSurfaceAllocator final : gfx_api::SurfaceAllocator
	{
		explicit PipelineSurfaceAllocator(wgpu_context& r) : root(r) {}
		bool create(gfx_api::PipelineSurfaceId id, const gfx_api::ResolvedSurfaceSpec& spec, gfx_api::abstract_texture*& outTexture) override;
		void destroy(gfx_api::PipelineSurfaceId id, gfx_api::abstract_texture* texture) override;
		void prepareForSurfaceDestroy() override;
		void onChanged() override;
		wgpu_context& root;
	};

	bool _initialize(const gfx_api::backend_Impl_Factory& impl, int32_t antialiasing, swap_interval_mode mode, optional<float> mipLodBias, uint32_t depthMapResolution) override;
#if defined(__EMSCRIPTEN__)
	bool initializeWebDevice();
#else
	bool requestAdapter();
	bool requestDevice();
	bool readSurfaceCapabilities();
#endif
	void createDefaultResources();
	optional<wgpu::PresentMode> presentModeFor(swap_interval_mode mode) const;
	void configureSurface();
	void presentClearedFrame();
	[[noreturn]] void handleDeviceLost();
	void completeScreenshot();
	bool compileAllShaderModules();
	void releaseWebGPUObjects();

	wgpu_shader_module* getShaderModule(const std::string& path, wgpu::ShaderStage stage);
	bool setupPipelineLayout(wgpu_pipeline_state_object& pso);
	wgpu::RenderPipeline buildPipelineVariant(wgpu_pipeline_state_object& pso, const pipeline_variant_key& key);
	pipeline_variant_key currentVariantKey(const wgpu_pipeline_state_object& pso) const;
	void selectPipelineVariant();
	void unregisterPipeline(wgpu_pipeline_state_object* pso);

	chunk_allocation allocateUniform(const void* data, size_t size);
	chunk_allocation allocateStreamedVertexData(const void* data, size_t size);
	void bindUniform(size_t slot, const chunk_allocation& allocation);
	void flushStagingChunks();
	const wgpu::Sampler& getSampler(gfx_api::sampler_type type, bool comparison, bool filtering);
	wgpu::TextureView textureViewFor(gfx_api::abstract_texture* texture, const texture_slot& slot);
	wgpu::BindGroup getTextureBindGroup(wgpu_pipeline_state_object& pso, const std::vector<gfx_api::abstract_texture*>& textures);
	void ensureLightBuffer(wgpu::Buffer& buffer, size_t size, const char* label);
	wgpu::TextureView attachmentViewFor(const gfx_api::AttachmentDesc& attachment, wgpu::TextureFormat& format, uint32_t& samples);
	void applyViewport();
	bool ensureEncoder();

private:
	bool useDebug = false; // --gfxdebug
	std::unique_ptr<gfx_api::backend_WebGPU_Impl> backend_impl;

	// Declaration order matters: members are destroyed in reverse order, and the
	// surface must be released before the device (see releaseWebGPUObjects).
	wgpu::Instance instance;
	wgpu::Adapter adapter;
	wgpu::Device device;
	wgpu::Queue queue;
	wgpu::Surface surface;

	// Device capabilities
	wgpu::Limits deviceLimits;
	std::set<wgpu::FeatureName> enabledFeatures;

	// Surface
	wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
	wgpu::TextureUsage surfaceUsage = wgpu::TextureUsage::RenderAttachment;
	std::vector<wgpu::PresentMode> supportedPresentModes;
	uint32_t surfaceWidth = 0;
	uint32_t surfaceHeight = 0;
	bool surfaceConfigured = false;
	bool surfaceNeedsConfigure = false;
	// Set by the device-lost callback, which Dawn can call on a different thread
	std::atomic<bool> deviceLost{false};
	swap_interval_mode swapMode = swap_interval_mode::vsync;

	// Frame state
	wgpu::CommandEncoder encoder;
	wgpu::RenderPassEncoder renderPass;
	wgpu::Texture swapchainTexture;
	wgpu::TextureView swapchainView;
	bool swapchainAcquired = false;
	bool swapchainWritten = false;
	bool passTargetsSwapchain = false;
	uint32_t passWidth = 0;
	uint32_t passHeight = 0;
	pipeline_variant_key passLayout;
	float depthRangeMin = 0.f;
	float depthRangeMax = 1.f;

	// Draw state
	wgpu_pipeline_state_object* currentPSO = nullptr;
	wgpu::RenderPipeline currentPipeline;
	wgpu::IndexFormat currentIndexFormat = wgpu::IndexFormat::Undefined;
	bool indexedDraw = false; // a strip pipeline needs the index format for indexed draws only
	float polygonOffsetFactor = 0.f;
	float polygonOffsetUnits = 0.f;

	// Per-frame data
	std::vector<std::unique_ptr<staging_chunk>> uniformChunks;
	size_t currentUniformChunk = 0;
	std::vector<std::unique_ptr<staging_chunk>> vertexChunks;
	size_t currentVertexChunk = 0;
	std::vector<chunk_allocation> frameUniforms;
	std::map<std::vector<uintptr_t>, wgpu::BindGroup> textureBindGroups; // cleared each frame

	// Shared objects
	wgpu::BindGroupLayout uniformLayout;
	wgpu::Buffer zeroUniformBuffer;
	wgpu::BindGroup zeroUniformBindGroup;
	std::map<std::tuple<gfx_api::sampler_type, bool, bool>, wgpu::Sampler> samplers;
	std::unique_ptr<wgpu_texture> defaultTexture;
	std::unique_ptr<wgpu_texture_array> defaultArrayTexture;
	std::unique_ptr<wgpu_surface_texture> defaultDepthTexture;
	std::unique_ptr<wgpu_surface_texture> defaultDepthArrayTexture;
	wgpu::Buffer lightDataBuffer;
	wgpu::Buffer lightIndexBuffer;
	std::unordered_map<std::string, std::unique_ptr<wgpu_shader_module>> shaderModules;
	std::set<wgpu_pipeline_state_object*> pipelines;

	// Pipeline surfaces
	gfx_api::PipelineSurfaceStore pipelineSurfaces;
	std::array<std::unique_ptr<wgpu_surface_texture>, gfx_api::PIPELINE_SURFACE_COUNT> surfaceTextures;
	size_t depthPassCount = 0;
	uint32_t depthMapSize = 0;
	uint32_t sceneMsaaSamples = 1; // WebGPU supports 1 and 4 samples

	// Screenshot readback (getScreenshot records the copy; finishScreenFrame reads it after the submit)
	std::function<void (std::unique_ptr<iV_Image>)> screenshotCallback;
	wgpu::Buffer screenshotBuffer;
	uint32_t screenshotBytesPerRow = 0;
	uint32_t screenshotWidth = 0;
	uint32_t screenshotHeight = 0;
	// Debug aid: WZ_WEBGPU_DEBUG_SCREENSHOT=<frame>:<file.png> saves that frame as a PNG
	optional<size_t> debugScreenshotFrame;
	std::string debugScreenshotPath;
	// Debug aid: WZ_WEBGPU_DEBUG_FORCE_DEVICE_LOSS=<frame> loses the device at that frame
	optional<size_t> debugForceDeviceLossFrame;

	gfx_api::lighting_constants shadowConstants;
	std::map<std::string, std::string> backendGameInfo;
	std::string formattedRendererInfoString = "WebGPU";
	size_t frameNum = 0;
};

wgpu_pipeline_state_object::~wgpu_pipeline_state_object()
{
	if (context != nullptr)
	{
		context->unregisterPipeline(this);
	}
}

wgpu_context::~wgpu_context()
{
	releaseWebGPUObjects();
}

// MARK: wgpu_context - initialization

bool wgpu_context::_initialize(const gfx_api::backend_Impl_Factory& impl, int32_t antialiasing, swap_interval_mode mode, optional<float> mipLodBias, uint32_t depthMapResolution)
{
	backend_impl = impl.createWebGPUBackendImpl();
	if (!backend_impl)
	{
		debug(LOG_ERROR, "Failed to get the WebGPU backend implementation");
		return false;
	}

#if defined(__EMSCRIPTEN__)
	instance = wgpu::CreateInstance(nullptr);
#else
	static const wgpu::InstanceFeatureName instanceFeatures[] = { wgpu::InstanceFeatureName::TimedWaitAny };
	wgpu::InstanceDescriptor instanceDesc;
	instanceDesc.requiredFeatureCount = 1;
	instanceDesc.requiredFeatures = instanceFeatures;
	instance = wgpu::CreateInstance(&instanceDesc);
#endif
	if (!instance)
	{
		debug(LOG_ERROR, "Failed to create the WebGPU instance");
		return false;
	}

	std::string surfaceError;
	WGPUSurface rawSurface = backend_impl->createSurface(instance.Get(), surfaceError);
	if (rawSurface == nullptr)
	{
		debug(LOG_ERROR, "Failed to create the WebGPU surface: %s", surfaceError.c_str());
		return false;
	}
	surface = wgpu::Surface::Acquire(rawSurface);

#if defined(__EMSCRIPTEN__)
	// In the browser, the page creates the device before the game starts (webgpu-postjs.js)
	if (!initializeWebDevice())
	{
		return false;
	}
#else
	if (!requestAdapter())
	{
		return false;
	}

	// A bad surface descriptor gives an error object, not a null surface,
	// so check the surface capabilities to find out if the surface works
	if (!readSurfaceCapabilities())
	{
		return false;
	}

	if (!requestDevice())
	{
		return false;
	}
#endif

	createDefaultResources();

	if (!presentModeFor(mode).has_value())
	{
		debug(LOG_3D, "Swap interval %d is not supported; defaulting to vsync on", to_int(mode));
		mode = swap_interval_mode::vsync;
	}
	swapMode = mode;
	depthMapSize = (depthMapResolution > 0) ? depthMapResolution : 2048;
	// As in Vulkan, MSAA applies to the scene only. WebGPU supports only 1 and 4 samples.
	sceneMsaaSamples = (antialiasing >= 2) ? 4 : 1;
	debug(LOG_3D, "Using scene sample count: %u (antialiasing setting: %d)", sceneMsaaSamples, static_cast<int>(antialiasing));
	configureSurface();

	if (useDebug && !compileAllShaderModules())
	{
		debug(LOG_ERROR, "Some WebGPU shaders failed to compile");
	}

	if (const char* debugScreenshot = getenv("WZ_WEBGPU_DEBUG_SCREENSHOT"))
	{
		const char* separator = strchr(debugScreenshot, ':');
		if (separator != nullptr && separator[1] != '\0')
		{
			debugScreenshotFrame = static_cast<size_t>(strtoul(debugScreenshot, nullptr, 10));
			debugScreenshotPath = separator + 1;
			debug(LOG_INFO, "WebGPU debug screenshot: frame %zu -> %s", debugScreenshotFrame.value(), debugScreenshotPath.c_str());
		}
	}
	if (const char* forceLoss = getenv("WZ_WEBGPU_DEBUG_FORCE_DEVICE_LOSS"))
	{
		debugForceDeviceLossFrame = static_cast<size_t>(strtoul(forceLoss, nullptr, 10));
		debug(LOG_INFO, "WebGPU debug: the device will be lost at frame %zu", debugForceDeviceLossFrame.value());
	}

	return true;
}

#if defined(__EMSCRIPTEN__)
bool wgpu_context::initializeWebDevice()
{
	WGPUDevice rawDevice = emscripten_webgpu_get_device();
	if (rawDevice == nullptr)
	{
		debug(LOG_ERROR, "The page did not create a WebGPU device");
		return false;
	}
	device = wgpu::Device::Acquire(rawDevice);
	queue = device.GetQueue();
	device.GetLimits(&deviceLimits);
	for (wgpu::FeatureName feature : {
		wgpu::FeatureName::TextureCompressionBC,
		wgpu::FeatureName::TextureCompressionETC2,
		wgpu::FeatureName::TextureCompressionASTC,
		wgpu::FeatureName::Depth32FloatStencil8 })
	{
		if (device.HasFeature(feature))
		{
			enabledFeatures.insert(feature);
		}
	}

	wgpu::AdapterInfo info;
	device.GetAdapterInfo(&info);
	backendGameInfo["webgpu_backend"] = "Browser";
	backendGameInfo["webgpu_vendor"] = to_std_string(info.vendor);
	backendGameInfo["webgpu_device"] = to_std_string(info.device);
	backendGameInfo["webgpu_driver"] = to_std_string(info.description);
	formattedRendererInfoString = "WebGPU (Browser): " + backendGameInfo["webgpu_vendor"] + " " + to_std_string(info.architecture);
	debug(LOG_INFO, "WebGPU: browser device (%s)", formattedRendererInfoString.c_str());

	// Without the adapter, the surface capabilities are not available, so the page gives the canvas format.
	// The browser presents at the end of the animation frame, so FIFO is the only present mode.
	const bool rgba = MAIN_THREAD_EM_ASM_INT({ return (Module['WZVAL_webgpuPreferredCanvasFormat'] === 'rgba8unorm') ? 1 : 0; }) != 0;
	surfaceFormat = rgba ? wgpu::TextureFormat::RGBA8Unorm : wgpu::TextureFormat::BGRA8Unorm;
	supportedPresentModes = { wgpu::PresentMode::Fifo };
	surfaceUsage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
	return true;
}
#else
bool wgpu_context::requestAdapter()
{
	wgpu::RequestAdapterOptions options;
	options.compatibleSurface = surface;
	instance.WaitAny(instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
		[this](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView message) {
			if (status == wgpu::RequestAdapterStatus::Success)
			{
				adapter = std::move(result);
			}
			else
			{
				debug(LOG_ERROR, "WebGPU RequestAdapter failed: %s", to_std_string(message).c_str());
			}
		}), UINT64_MAX);
	// The options hold a reference to the surface. It must not outlive the device.
	options.compatibleSurface = nullptr;

	if (!adapter)
	{
		return false;
	}

	wgpu::AdapterInfo info;
	adapter.GetInfo(&info);
	backendGameInfo["webgpu_backend"] = to_string(info.backendType);
	backendGameInfo["webgpu_adapter_type"] = to_string(info.adapterType);
	backendGameInfo["webgpu_vendor"] = to_std_string(info.vendor);
	backendGameInfo["webgpu_device"] = to_std_string(info.device);
	backendGameInfo["webgpu_driver"] = to_std_string(info.description);
	formattedRendererInfoString = "WebGPU (" + backendGameInfo["webgpu_backend"] + "): " + backendGameInfo["webgpu_device"];
	debug(LOG_INFO, "WebGPU adapter: %s (%s), %s backend", backendGameInfo["webgpu_device"].c_str(), backendGameInfo["webgpu_driver"].c_str(), backendGameInfo["webgpu_backend"].c_str());
	return true;
}

bool wgpu_context::readSurfaceCapabilities()
{
	wgpu::SurfaceCapabilities capabilities;
	if (surface.GetCapabilities(adapter, &capabilities) != wgpu::Status::Success || capabilities.formatCount == 0)
	{
		debug(LOG_ERROR, "The WebGPU surface is not usable with the adapter");
		return false;
	}

	// Prefer a unorm format: Dawn can report an sRGB format first
	surfaceFormat = capabilities.formats[0];
	for (size_t i = 0; i < capabilities.formatCount; ++i)
	{
		if (capabilities.formats[i] == wgpu::TextureFormat::BGRA8Unorm || capabilities.formats[i] == wgpu::TextureFormat::RGBA8Unorm)
		{
			surfaceFormat = capabilities.formats[i];
			break;
		}
	}

	supportedPresentModes.assign(capabilities.presentModes, capabilities.presentModes + capabilities.presentModeCount);
	std::string presentModes;
	for (wgpu::PresentMode presentMode : supportedPresentModes)
	{
		presentModes += (presentModes.empty() ? "" : ", ") + std::to_string(static_cast<int>(presentMode));
	}
	debug(LOG_3D, "WebGPU surface present modes: %s", presentModes.c_str());

	surfaceUsage = wgpu::TextureUsage::RenderAttachment;
	if (capabilities.usages & wgpu::TextureUsage::CopySrc)
	{
		// For screenshots
		surfaceUsage |= wgpu::TextureUsage::CopySrc;
	}
	return true;
}

bool wgpu_context::requestDevice()
{
	// Ask for the full limits of the adapter: terrain needs at least 256 array layers,
	// and the texture size limits feed MAX_TEXTURE_SIZE
	wgpu::Limits adapterLimits;
	adapter.GetLimits(&adapterLimits);

	// Ask for the optional features that the adapter has (texture formats)
	std::vector<wgpu::FeatureName> requiredFeatures;
	for (wgpu::FeatureName feature : {
		wgpu::FeatureName::TextureCompressionBC,
		wgpu::FeatureName::TextureCompressionETC2,
		wgpu::FeatureName::TextureCompressionASTC,
		wgpu::FeatureName::Depth32FloatStencil8,
		wgpu::FeatureName::TextureFormatsTier1 })
	{
		if (adapter.HasFeature(feature))
		{
			requiredFeatures.push_back(feature);
		}
	}

	wgpu::DeviceDescriptor deviceDesc;
	deviceDesc.label = "Warzone 2100";
	deviceDesc.requiredLimits = &adapterLimits;
	deviceDesc.requiredFeatureCount = requiredFeatures.size();
	deviceDesc.requiredFeatures = requiredFeatures.data();
	deviceDesc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message) {
		debug(LOG_ERROR, "WebGPU error (%d): %s", static_cast<int>(type), to_std_string(message).c_str());
	});
	deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
		[](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message, wgpu_context* self) {
			if (reason == wgpu::DeviceLostReason::Destroyed || reason == wgpu::DeviceLostReason::CallbackCancelled)
			{
				return; // expected at shutdown
			}
			self->deviceLost = true;
			debug(LOG_ERROR, "WebGPU device lost (%d): %s", static_cast<int>(reason), to_std_string(message).c_str());
		}, this);

	instance.WaitAny(adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
		[this](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView message) {
			if (status == wgpu::RequestDeviceStatus::Success)
			{
				device = std::move(result);
			}
			else
			{
				debug(LOG_ERROR, "WebGPU RequestDevice failed: %s", to_std_string(message).c_str());
			}
		}), UINT64_MAX);
	if (!device)
	{
		return false;
	}

	device.SetLoggingCallback([](wgpu::LoggingType type, wgpu::StringView message) {
		const code_part part = (type == wgpu::LoggingType::Error) ? LOG_ERROR : ((type == wgpu::LoggingType::Warning) ? LOG_WARNING : LOG_3D);
		debug(part, "Dawn: %s", to_std_string(message).c_str());
	});

	queue = device.GetQueue();
	device.GetLimits(&deviceLimits);
	enabledFeatures.insert(requiredFeatures.begin(), requiredFeatures.end());
	return true;
}
#endif // defined(__EMSCRIPTEN__)

void wgpu_context::createDefaultResources()
{
	// The layout of every uniform block group: one uniform buffer with a dynamic offset
	wgpu::BindGroupLayoutEntry uniformEntry;
	uniformEntry.binding = 0;
	uniformEntry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
	uniformEntry.buffer.type = wgpu::BufferBindingType::Uniform;
	uniformEntry.buffer.hasDynamicOffset = true;
	wgpu::BindGroupLayoutDescriptor uniformLayoutDesc;
	uniformLayoutDesc.label = "WZ uniform block";
	uniformLayoutDesc.entryCount = 1;
	uniformLayoutDesc.entries = &uniformEntry;
	uniformLayout = device.CreateBindGroupLayout(&uniformLayoutDesc);

	// Zero-filled uniform data, bound to every uniform slot when a pipeline is bound.
	// WebGPU rejects a draw if a group of the pipeline layout is not set.
	wgpu::BufferDescriptor zeroDesc;
	zeroDesc.label = "WZ zero uniform";
	zeroDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	zeroDesc.size = UniformBindingSize;
	zeroUniformBuffer = device.CreateBuffer(&zeroDesc);
	wgpu::BindGroupEntry zeroEntry;
	zeroEntry.binding = 0;
	zeroEntry.buffer = zeroUniformBuffer;
	zeroEntry.size = UniformBindingSize;
	wgpu::BindGroupDescriptor zeroGroupDesc;
	zeroGroupDesc.layout = uniformLayout;
	zeroGroupDesc.entryCount = 1;
	zeroGroupDesc.entries = &zeroEntry;
	zeroUniformBindGroup = device.CreateBindGroup(&zeroGroupDesc);

	// Default textures (2x2, all zero), as in the Vulkan backend
	const std::vector<uint8_t> zeros(2 * 2 * 4, 0);
	auto writeZeros = [this, &zeros](wgpu_image& image, uint32_t layer) {
		wgpu::TexelCopyTextureInfo destination;
		destination.texture = image.texture;
		destination.origin = {0, 0, layer};
		wgpu::TexelCopyBufferLayout layout;
		layout.bytesPerRow = 2 * 4;
		wgpu::Extent3D size = {2, 2, 1};
		queue.WriteTexture(&destination, zeros.data(), zeros.size(), &layout, &size);
	};
	const wgpu::TextureUsage sampledUsage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	defaultTexture = std::make_unique<wgpu_texture>();
	defaultTexture->queue = &queue;
	defaultTexture->image.wzFormat = gfx_api::pixel_format::FORMAT_RGBA8_UNORM_PACK8;
	defaultTexture->image.create(device, wgpu::TextureFormat::RGBA8Unorm, 2, 2, 1, 1, 1, sampledUsage, "<default texture>");
	writeZeros(defaultTexture->image, 0);
	defaultArrayTexture = std::make_unique<wgpu_texture_array>();
	defaultArrayTexture->queue = &queue;
	defaultArrayTexture->image.wzFormat = gfx_api::pixel_format::FORMAT_RGBA8_UNORM_PACK8;
	defaultArrayTexture->image.create(device, wgpu::TextureFormat::RGBA8Unorm, 2, 2, 1, 1, 1, sampledUsage, "<default array texture>");
	writeZeros(defaultArrayTexture->image, 0);
	// Depth textures cannot be written with WriteTexture; their contents are not defined,
	// as in the Vulkan backend
	const wgpu::TextureUsage depthUsage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
	defaultDepthTexture = std::make_unique<wgpu_surface_texture>();
	defaultDepthTexture->image.create(device, wgpu::TextureFormat::Depth32Float, 4, 4, 1, 1, 1, depthUsage, "<default depth>");
	defaultDepthArrayTexture = std::make_unique<wgpu_surface_texture>();
	defaultDepthArrayTexture->arrayTexture = true;
	defaultDepthArrayTexture->image.create(device, wgpu::TextureFormat::Depth32Float, 4, 4, 1, 1, 1, depthUsage, "<default depth array>");

	// Placeholder light data, until the first upload_light_data
	ensureLightBuffer(lightDataBuffer, 16, "WZ light data");
	ensureLightBuffer(lightIndexBuffer, 16, "WZ light indices");
}

// MARK: wgpu_context - surface and frames

optional<wgpu::PresentMode> wgpu_context::presentModeFor(swap_interval_mode mode) const
{
	auto isSupported = [this](wgpu::PresentMode presentMode) {
		return std::find(supportedPresentModes.begin(), supportedPresentModes.end(), presentMode) != supportedPresentModes.end();
	};
	switch (mode)
	{
		case swap_interval_mode::immediate:
			if (isSupported(wgpu::PresentMode::Immediate))
			{
				return wgpu::PresentMode::Immediate;
			}
			if (isSupported(wgpu::PresentMode::Mailbox))
			{
				return wgpu::PresentMode::Mailbox;
			}
			return nullopt;
		case swap_interval_mode::vsync:
			return wgpu::PresentMode::Fifo; // always supported
		case swap_interval_mode::adaptive_vsync:
			if (isSupported(wgpu::PresentMode::FifoRelaxed))
			{
				return wgpu::PresentMode::FifoRelaxed;
			}
			return nullopt;
	}
	return nullopt;
}

void wgpu_context::configureSurface()
{
	surfaceNeedsConfigure = false;

	int width = 0;
	int height = 0;
	backend_impl->getDrawableSize(&width, &height);
	if (width <= 0 || height <= 0)
	{
		// For example, the window is minimized. Try again when the size changes.
		surfaceConfigured = false;
		return;
	}

	wgpu::SurfaceConfiguration config;
	config.device = device;
	config.format = surfaceFormat;
	config.usage = surfaceUsage;
	config.width = static_cast<uint32_t>(width);
	config.height = static_cast<uint32_t>(height);
	config.presentMode = presentModeFor(swapMode).value_or(wgpu::PresentMode::Fifo);
	config.alphaMode = wgpu::CompositeAlphaMode::Opaque;
	surface.Configure(&config);
	surfaceConfigured = true;
	const bool sizeChanged = (surfaceWidth != config.width) || (surfaceHeight != config.height);
	surfaceWidth = config.width;
	surfaceHeight = config.height;
	debug(LOG_3D, "Configured the WebGPU surface: %dx%d, format %d, present mode %d", width, height, static_cast<int>(config.format), static_cast<int>(config.presentMode));

	if (sizeChanged)
	{
		// The UI and backdrop geometry uses the drawable size (as Vulkan does at swapchain creation)
		markScreenGeometryDirty();
		if (!syncPipelineSurfaces())
		{
			debug(LOG_ERROR, "Failed to create the WebGPU pipeline surfaces (%ux%u)", surfaceWidth, surfaceHeight);
		}
	}
}

void wgpu_context::reconcileSwapchainAtFrameOpen()
{
	if (device && surfaceNeedsConfigure)
	{
		configureSurface();
	}
}

void wgpu_context::beginScreenFrame()
{
	if (deviceLost)
	{
		handleDeviceLost();
	}
	for (auto& chunk : uniformChunks)
	{
		chunk->used = 0;
	}
	currentUniformChunk = 0;
	for (auto& chunk : vertexChunks)
	{
		chunk->used = 0;
	}
	currentVertexChunk = 0;
	frameUniforms.clear();
	textureBindGroups.clear();
	swapchainWritten = false;
	purgeFrameResources();
	advanceFrameUniformGeneration();
}

void wgpu_context::acquireSwapchainForFrameDraw()
{
	if (swapchainAcquired || !surfaceConfigured || deviceLost)
	{
		return;
	}
	wgpu::SurfaceTexture surfaceTexture;
	surface.GetCurrentTexture(&surfaceTexture);
	switch (surfaceTexture.status)
	{
		case wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal:
			break;
		case wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal:
			surfaceNeedsConfigure = true; // draw this frame, configure again at the next frame
			break;
		case wgpu::SurfaceGetCurrentTextureStatus::Timeout:
		case wgpu::SurfaceGetCurrentTextureStatus::Outdated:
		case wgpu::SurfaceGetCurrentTextureStatus::Lost:
			surfaceNeedsConfigure = true;
			return;
		case wgpu::SurfaceGetCurrentTextureStatus::Error:
			debug(LOG_ERROR, "Failed to get the current WebGPU surface texture");
			return;
	}
	if (surfaceTexture.texture.GetWidth() != surfaceWidth || surfaceTexture.texture.GetHeight() != surfaceHeight)
	{
		// In the browser, the page can change the canvas size after the configuration.
		// The pipeline surfaces must have the size of the surface texture, so configure again.
		debug(LOG_3D, "The WebGPU surface texture is %ux%u, not %ux%u", surfaceTexture.texture.GetWidth(),
			  surfaceTexture.texture.GetHeight(), surfaceWidth, surfaceHeight);
		surfaceNeedsConfigure = true;
		return;
	}
	swapchainTexture = surfaceTexture.texture;
	swapchainView = swapchainTexture.CreateView();
	swapchainAcquired = true;
}

bool wgpu_context::ensureEncoder()
{
	if (!encoder && device)
	{
		encoder = device.CreateCommandEncoder();
	}
	return static_cast<bool>(encoder);
}

void wgpu_context::presentClearedFrame()
{
	// The frame did not write the swapchain image, so clear it
	if (!ensureEncoder())
	{
		return;
	}
	wgpu::RenderPassColorAttachment colorAttachment;
	colorAttachment.view = swapchainView;
	colorAttachment.loadOp = wgpu::LoadOp::Clear;
	colorAttachment.storeOp = wgpu::StoreOp::Store;
	colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};
	wgpu::RenderPassDescriptor passDesc;
	passDesc.label = "WebGPU clear";
	passDesc.colorAttachmentCount = 1;
	passDesc.colorAttachments = &colorAttachment;
	encoder.BeginRenderPass(&passDesc).End();
}

void wgpu_context::handleDeviceLost()
{
	// The same path as the Vulkan backend (handleUnrecoverableError): offer a different graphics
	// backend for the next start, or show the error, and then stop. A lost device cannot be used again.
	debug(LOG_ERROR, "The WebGPU device was lost");
	const std::string details = "Failed with error: the WebGPU device was lost";
	if (!wzPromptToChangeGfxBackendOnFailure(details))
	{
		wzDisplayFatalGfxBackendFailure(details);
	}
	abort();
}

void wgpu_context::finishScreenFrame()
{
	ASSERT(!renderPass, "finishScreenFrame called while a render pass is active");
	if (device && !deviceLost)
	{
		if (swapchainAcquired && !swapchainWritten)
		{
			presentClearedFrame();
		}
		if (debugScreenshotFrame.has_value() && debugScreenshotFrame.value() == frameNum && !screenshotCallback)
		{
			const std::string path = debugScreenshotPath;
			getScreenshot([path](std::unique_ptr<iV_Image> image) {
				const bool saved = image && iV_saveImage_PNG(path.c_str(), image.get()).noError();
				debug(LOG_INFO, "WebGPU debug screenshot %s: %s", saved ? "saved" : "FAILED", path.c_str());
			});
		}
		flushStagingChunks();
		if (encoder)
		{
			wgpu::CommandBuffer commands = encoder.Finish();
			queue.Submit(1, &commands);
		}
		if (screenshotCallback)
		{
			completeScreenshot();
		}
#if !defined(__EMSCRIPTEN__)
		if (debugForceDeviceLossFrame.has_value() && debugForceDeviceLossFrame.value() == frameNum)
		{
			device.ForceLoss(wgpu::DeviceLostReason::Unknown, "WZ_WEBGPU_DEBUG_FORCE_DEVICE_LOSS");
		}
#endif
#if !defined(__EMSCRIPTEN__)
		// In the browser, the frame is presented at the end of the animation frame
		if (swapchainAcquired)
		{
			surface.Present();
		}
#endif
	}
	encoder = nullptr;
	swapchainView = nullptr;
	swapchainTexture = nullptr;
	swapchainAcquired = false;
	if (instance)
	{
		instance.ProcessEvents();
	}
	frameNum = std::max<size_t>(frameNum + 1, 1);
	purgeFrameResources();
}

void wgpu_context::handleWindowSizeChange(unsigned int oldWidth, unsigned int oldHeight, unsigned int newWidth, unsigned int newHeight)
{
	surfaceNeedsConfigure = true;
	markScreenGeometryDirty();
}

std::pair<uint32_t, uint32_t> wgpu_context::getDrawableDimensions()
{
	int width = 0;
	int height = 0;
	if (backend_impl)
	{
		backend_impl->getDrawableSize(&width, &height);
	}
	return {static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0))};
}

bool wgpu_context::setSwapInterval(gfx_api::context::swap_interval_mode mode, const SetSwapIntervalCompletionHandler& completionHandler)
{
	if (!presentModeFor(mode).has_value())
	{
		return false;
	}
	swapMode = mode;
	surfaceNeedsConfigure = true; // the present mode changes at the next frame
	if (completionHandler)
	{
		completionHandler();
	}
	return true;
}

// MARK: wgpu_context - per-frame data

chunk_allocation wgpu_context::allocateUniform(const void* data, size_t size)
{
	ASSERT_OR_RETURN({}, size <= UniformBindingSize, "Uniform block size (%zu) exceeds 64k", size);
	const uint64_t alignment = std::max<uint64_t>(deviceLimits.minUniformBufferOffsetAlignment, 256);
	for (;; ++currentUniformChunk)
	{
		if (currentUniformChunk >= uniformChunks.size())
		{
			auto chunk = std::make_unique<staging_chunk>();
			wgpu::BufferDescriptor desc;
			desc.label = "WZ uniforms";
			desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
			// The space after the last allocation, so that every binding of UniformBindingSize fits
			desc.size = UniformChunkSize + UniformBindingSize;
			chunk->buffer = device.CreateBuffer(&desc);
			chunk->staging.resize(static_cast<size_t>(desc.size), 0);
			wgpu::BindGroupEntry entry;
			entry.binding = 0;
			entry.buffer = chunk->buffer;
			entry.size = UniformBindingSize;
			wgpu::BindGroupDescriptor groupDesc;
			groupDesc.layout = uniformLayout;
			groupDesc.entryCount = 1;
			groupDesc.entries = &entry;
			chunk->uniformBindGroup = device.CreateBindGroup(&groupDesc);
			uniformChunks.push_back(std::move(chunk));
		}
		staging_chunk& chunk = *uniformChunks[currentUniformChunk];
		const uint64_t offset = alignUp(chunk.used, alignment);
		if (offset + size <= UniformChunkSize)
		{
			memcpy(chunk.staging.data() + offset, data, size);
			chunk.used = offset + size;
			return {&chunk, offset};
		}
	}
}

chunk_allocation wgpu_context::allocateStreamedVertexData(const void* data, size_t size)
{
	for (;; ++currentVertexChunk)
	{
		if (currentVertexChunk >= vertexChunks.size())
		{
			auto chunk = std::make_unique<staging_chunk>();
			wgpu::BufferDescriptor desc;
			desc.label = "WZ streamed vertices";
			desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
			desc.size = alignUp(std::max<uint64_t>(StreamedVertexChunkSize, size), 4);
			chunk->buffer = device.CreateBuffer(&desc);
			chunk->staging.resize(static_cast<size_t>(desc.size), 0);
			vertexChunks.push_back(std::move(chunk));
		}
		staging_chunk& chunk = *vertexChunks[currentVertexChunk];
		const uint64_t offset = alignUp(chunk.used, 4);
		if (offset + size <= chunk.staging.size())
		{
			memcpy(chunk.staging.data() + offset, data, size);
			chunk.used = offset + size;
			return {&chunk, offset};
		}
	}
}

void wgpu_context::flushStagingChunks()
{
	auto flush = [this](std::vector<std::unique_ptr<staging_chunk>>& chunks) {
		for (auto& chunk : chunks)
		{
			if (chunk->used > 0)
			{
				queue.WriteBuffer(chunk->buffer, 0, chunk->staging.data(), static_cast<size_t>(alignUp(chunk->used, 4)));
			}
		}
	};
	flush(uniformChunks);
	flush(vertexChunks);
}

void wgpu_context::bindUniform(size_t slot, const chunk_allocation& allocation)
{
	ASSERT_OR_RETURN(, renderPass && currentPSO != nullptr, "No bound pipeline");
	ASSERT_OR_RETURN(, slot < currentPSO->uniformBlockCount, "Uniform slot %zu is out of range (have %zu)", slot, currentPSO->uniformBlockCount);
	ASSERT_OR_RETURN(, allocation.chunk != nullptr, "Invalid uniform allocation");
	const uint32_t dynamicOffset = static_cast<uint32_t>(allocation.offset);
	renderPass.SetBindGroup(static_cast<uint32_t>(slot), allocation.chunk->uniformBindGroup, 1, &dynamicOffset);
}

void wgpu_context::set_constants(const void* buffer, const size_t& size)
{
	ASSERT_OR_RETURN(, currentPSO != nullptr, "currentPSO == NULL");
	bindUniform(0, allocateUniform(buffer, size));
}

void wgpu_context::set_uniforms(const size_t& first, const std::vector<std::tuple<const void*, size_t>>& uniform_blocks)
{
	ASSERT_OR_RETURN(, currentPSO != nullptr, "currentPSO == NULL");
	for (size_t i = 0; i < uniform_blocks.size() && (first + i) < currentPSO->uniformBlockCount; ++i)
	{
		const void* data = std::get<0>(uniform_blocks[i]);
		if (data == nullptr)
		{
			continue;
		}
		bindUniform(first + i, allocateUniform(data, std::get<1>(uniform_blocks[i])));
	}
}

gfx_api::frame_uniform_allocation wgpu_context::upload_frame_uniform_raw(const void* data, size_t size)
{
	ASSERT_OR_RETURN({}, device != nullptr, "No device");
	const chunk_allocation allocation = allocateUniform(data, size);
	ASSERT_OR_RETURN({}, allocation.chunk != nullptr, "Failed to allocate a frame uniform");
	frameUniforms.push_back(allocation);

	gfx_api::frame_uniform_allocation result;
	result.handle = frameUniforms.size(); // one based, so a zero handle stays invalid
	result.offset = static_cast<uint32_t>(allocation.offset);
	result.size = static_cast<uint32_t>(size);
	result.generation = frameUniformGeneration();
	return result;
}

void wgpu_context::set_frame_uniform_at(size_t slot, const gfx_api::frame_uniform_allocation& allocation, std::type_index type)
{
	ASSERT_OR_RETURN(, currentPSO != nullptr, "currentPSO == NULL");
	ASSERT_OR_RETURN(, slot < currentPSO->uniformBlockCount, "Uniform slot %zu is out of range (have %zu)", slot, currentPSO->uniformBlockCount);
	ASSERT(currentPSO->createInfo.uniform_blocks[slot] == type, "Uniform slot %zu holds %s, not %s", slot, currentPSO->createInfo.uniform_blocks[slot].name(), type.name());

	// Nothing was uploaded for this block this frame (generations start at one)
	if (allocation.generation == 0)
	{
		return;
	}
	if (!allocation.valid() || allocation.generation != frameUniformGeneration()
		|| allocation.handle == 0 || allocation.handle > frameUniforms.size())
	{
		ASSERT(false, "Uniform slot %zu was given a frame uniform from generation %" PRIu32 " (current is %" PRIu32 ")",
			slot, allocation.generation, frameUniformGeneration());
		return;
	}
	bindUniform(slot, frameUniforms[static_cast<size_t>(allocation.handle) - 1]);
}

void wgpu_context::ensureLightBuffer(wgpu::Buffer& buffer, size_t size, const char* label)
{
	const uint64_t neededSize = alignUp(std::max<size_t>(size, 16), 16);
	if (buffer && buffer.GetSize() >= neededSize)
	{
		return;
	}
	wgpu::BufferDescriptor desc;
	desc.label = label;
	desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
	desc.size = neededSize;
	buffer = device.CreateBuffer(&desc);
	textureBindGroups.clear(); // the bind groups of light pipelines hold the old buffer
}

void wgpu_context::upload_light_data(const void* lights, size_t lightBytes, const void* indices, size_t indexBytes)
{
	ASSERT_OR_RETURN(, device != nullptr, "No device");
	ensureLightBuffer(lightDataBuffer, lightBytes, "WZ light data");
	ensureLightBuffer(lightIndexBuffer, indexBytes, "WZ light indices");
	if (lightBytes > 0)
	{
		std::vector<uint8_t> padded(static_cast<size_t>(alignUp(lightBytes, 4)), 0);
		memcpy(padded.data(), lights, lightBytes);
		queue.WriteBuffer(lightDataBuffer, 0, padded.data(), padded.size());
	}
	if (indexBytes > 0)
	{
		std::vector<uint8_t> padded(static_cast<size_t>(alignUp(indexBytes, 4)), 0);
		memcpy(padded.data(), indices, indexBytes);
		queue.WriteBuffer(lightIndexBuffer, 0, padded.data(), padded.size());
	}
}

// MARK: wgpu_context - shaders and pipelines

wgpu_shader_module* wgpu_context::getShaderModule(const std::string& path, wgpu::ShaderStage stage)
{
	auto it = shaderModules.find(path);
	if (it != shaderModules.end())
	{
		return it->second.get();
	}

	PHYSFS_file* file = PHYSFS_openRead(path.c_str());
	if (file == nullptr)
	{
		debug(LOG_ERROR, "Missing WebGPU shader: %s", path.c_str());
		return nullptr;
	}
	const PHYSFS_sint64 fileSize = PHYSFS_fileLength(file);
	std::string code(static_cast<size_t>(std::max<PHYSFS_sint64>(fileSize, 0)), '\0');
	const bool readOk = (fileSize > 0) && (WZ_PHYSFS_readBytes(file, code.data(), static_cast<PHYSFS_uint32>(fileSize)) == fileSize);
	PHYSFS_close(file);
	if (!readOk)
	{
		debug(LOG_ERROR, "Failed to read WebGPU shader: %s", path.c_str());
		return nullptr;
	}

	auto entry = std::make_unique<wgpu_shader_module>();
	entry->stage = stage;
	parseWgslDeclarations(code, *entry);

	wgpu::ShaderSourceWGSL source;
	source.code = code.c_str();
	wgpu::ShaderModuleDescriptor moduleDesc;
	moduleDesc.nextInChain = &source;
	moduleDesc.label = path.c_str();
	entry->module = device.CreateShaderModule(&moduleDesc);

	wgpu_shader_module* result = entry.get();
	shaderModules[path] = std::move(entry);
	return result;
}

bool wgpu_context::setupPipelineLayout(wgpu_pipeline_state_object& pso)
{
	const gfx_api::shader_program_desc& desc = gfx_api::shader_catalog(pso.createInfo.shader_mode);
	ASSERT_OR_RETURN(false, gfx_api::shader_supported_by_webgpu(desc), "Shader program \"%s\" is not supported by WebGPU", desc.friendly_name);
	const gfx_api::shader_stage_file* vertexStage = gfx_api::find_stage(desc, gfx_api::shader_stage_kind::vertex);
	const gfx_api::shader_stage_file* fragmentStage = gfx_api::find_stage(desc, gfx_api::shader_stage_kind::fragment);
	ASSERT_OR_RETURN(false, vertexStage != nullptr && fragmentStage != nullptr, "Shader program \"%s\" has no vertex or fragment stage", desc.friendly_name);
	pso.vertexModule = getShaderModule(gfx_api::wgsl_path(vertexStage->basename), wgpu::ShaderStage::Vertex);
	pso.fragmentModule = getShaderModule(gfx_api::wgsl_path(fragmentStage->basename), wgpu::ShaderStage::Fragment);
	if (pso.vertexModule == nullptr || pso.fragmentModule == nullptr)
	{
		return false;
	}

	pso.uniformBlockCount = pso.createInfo.uniform_blocks.size();
	pso.hasLightData = std::find(pso.createInfo.uniform_blocks.begin(), pso.createInfo.uniform_blocks.end(),
		std::type_index(typeid(gfx_api::PointLightsUniforms))) != pso.createInfo.uniform_blocks.end();
	const uint32_t textureGroup = static_cast<uint32_t>(pso.uniformBlockCount);

	// The type and the stages of a binding, as the shaders declare it
	auto declared = [&pso, textureGroup](uint32_t binding) {
		wgsl_binding result;
		for (const wgpu_shader_module* module : {pso.vertexModule, pso.fragmentModule})
		{
			auto it = module->bindings.find({textureGroup, binding});
			if (it != module->bindings.end())
			{
				result.type = it->second;
				result.stages |= module->stage;
			}
		}
		return result;
	};

	std::vector<wgpu::BindGroupLayoutEntry> entries;
	for (const gfx_api::texture_input& input : pso.createInfo.texture_desc)
	{
		texture_slot slot;
		slot.textureBinding = static_cast<uint32_t>(2 * input.id);
		slot.samplerBinding = static_cast<uint32_t>(2 * input.id + 1);
		slot.sampler = input.sampler;
		slot.target = input.target;

		const wgsl_binding textureDecl = declared(slot.textureBinding);
		const wgsl_binding samplerDecl = declared(slot.samplerBinding);
		std::string textureType = textureDecl.type;
		if (textureType.empty())
		{
			// The shaders do not use this texture; use the type of the target
			switch (input.target)
			{
				case gfx_api::pixel_format_target::texture_2d: textureType = "texture_2d<f32>"; break;
				case gfx_api::pixel_format_target::texture_2d_array: textureType = "texture_2d_array<f32>"; break;
				case gfx_api::pixel_format_target::depth_map: textureType = "texture_depth_2d_array"; break;
			}
		}
		const bool depthTexture = textureType.rfind("texture_depth", 0) == 0;
		slot.viewDimension = (textureType.find("2d_array") != std::string::npos) ? wgpu::TextureViewDimension::e2DArray : wgpu::TextureViewDimension::e2D;
		if (depthTexture)
		{
			slot.sampleType = wgpu::TextureSampleType::Depth;
		}
		else if (input.target == gfx_api::pixel_format_target::depth_map
			|| input.sampler == gfx_api::sampler_type::nearest_clamped || input.sampler == gfx_api::sampler_type::nearest_border)
		{
			// A depth texture read as float values cannot be filtered. The scene effects read the
			// depth surfaces through nearest samplers as texture_2d<f32>, so a texture with a nearest
			// sampler is unfilterable: that binding accepts depth and color textures.
			slot.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
		}
		else
		{
			slot.sampleType = wgpu::TextureSampleType::Float;
		}
		if (samplerDecl.type == "sampler_comparison" || (samplerDecl.type.empty() && depthTexture))
		{
			slot.samplerType = wgpu::SamplerBindingType::Comparison;
		}
		else
		{
			slot.samplerType = (slot.sampleType == wgpu::TextureSampleType::UnfilterableFloat) ? wgpu::SamplerBindingType::NonFiltering : wgpu::SamplerBindingType::Filtering;
		}

		wgpu::BindGroupLayoutEntry textureEntry;
		textureEntry.binding = slot.textureBinding;
		textureEntry.visibility = (textureDecl.stages != wgpu::ShaderStage::None) ? textureDecl.stages : wgpu::ShaderStage::Fragment;
		textureEntry.texture.sampleType = slot.sampleType;
		textureEntry.texture.viewDimension = slot.viewDimension;
		entries.push_back(textureEntry);

		wgpu::BindGroupLayoutEntry samplerEntry;
		samplerEntry.binding = slot.samplerBinding;
		samplerEntry.visibility = (samplerDecl.stages != wgpu::ShaderStage::None) ? samplerDecl.stages : wgpu::ShaderStage::Fragment;
		samplerEntry.sampler.type = slot.samplerType;
		entries.push_back(samplerEntry);

		pso.textureSlots.push_back(slot);
	}
	if (pso.hasLightData)
	{
		for (uint32_t glslBinding : {LightDataStorageBinding, LightIndexStorageBinding})
		{
			const wgsl_binding decl = declared(2 * glslBinding);
			wgpu::BindGroupLayoutEntry storageEntry;
			storageEntry.binding = 2 * glslBinding;
			storageEntry.visibility = (decl.stages != wgpu::ShaderStage::None) ? decl.stages : wgpu::ShaderStage::Fragment;
			storageEntry.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
			entries.push_back(storageEntry);
		}
	}

	std::vector<wgpu::BindGroupLayout> groupLayouts(pso.uniformBlockCount, uniformLayout);
	if (!entries.empty())
	{
		wgpu::BindGroupLayoutDescriptor layoutDesc;
		layoutDesc.label = desc.friendly_name;
		layoutDesc.entryCount = entries.size();
		layoutDesc.entries = entries.data();
		pso.textureLayout = device.CreateBindGroupLayout(&layoutDesc);
		groupLayouts.push_back(pso.textureLayout);
	}
	ASSERT_OR_RETURN(false, groupLayouts.size() <= deviceLimits.maxBindGroups, "Pipeline \"%s\" needs %zu bind groups (limit: %u)", desc.friendly_name, groupLayouts.size(), deviceLimits.maxBindGroups);

	wgpu::PipelineLayoutDescriptor pipelineLayoutDesc;
	pipelineLayoutDesc.label = desc.friendly_name;
	pipelineLayoutDesc.bindGroupLayoutCount = groupLayouts.size();
	pipelineLayoutDesc.bindGroupLayouts = groupLayouts.data();
	pso.layout = device.CreatePipelineLayout(&pipelineLayoutDesc);
	return static_cast<bool>(pso.layout);
}

gfx_api::pipeline_state_object* wgpu_context::build_pipeline(gfx_api::pipeline_state_object* existing_pso, const gfx_api::pipeline_create_info& createInfo)
{
	auto* pso = new wgpu_pipeline_state_object(createInfo);
	pso->context = this;
	if (!device || !setupPipelineLayout(*pso))
	{
		pso->broken = true;
	}
	pipelines.insert(pso);
	return pso;
}

void wgpu_context::unregisterPipeline(wgpu_pipeline_state_object* pso)
{
	pipelines.erase(pso);
	if (currentPSO == pso)
	{
		currentPSO = nullptr;
		currentPipeline = nullptr;
	}
}

wgpu::BlendState to_wgpu(REND_MODE mode, bool& enabled)
{
	auto component = [](wgpu::BlendFactor src, wgpu::BlendFactor dst) {
		wgpu::BlendComponent result;
		result.operation = wgpu::BlendOperation::Add;
		result.srcFactor = src;
		result.dstFactor = dst;
		return result;
	};
	wgpu::BlendState state;
	enabled = true;
	switch (mode)
	{
		case REND_ADDITIVE:
			state.color = component(wgpu::BlendFactor::SrcAlpha, wgpu::BlendFactor::One);
			state.alpha = component(wgpu::BlendFactor::SrcAlpha, wgpu::BlendFactor::One);
			return state;
		case REND_ALPHA:
			state.color = component(wgpu::BlendFactor::SrcAlpha, wgpu::BlendFactor::OneMinusSrcAlpha);
			state.alpha = component(wgpu::BlendFactor::SrcAlpha, wgpu::BlendFactor::OneMinusSrcAlpha);
			return state;
		case REND_PREMULTIPLIED:
		case REND_TEXT:
			state.color = component(wgpu::BlendFactor::One, wgpu::BlendFactor::OneMinusSrcAlpha);
			state.alpha = component(wgpu::BlendFactor::One, wgpu::BlendFactor::OneMinusSrcAlpha);
			return state;
		case REND_MULTIPLICATIVE:
			state.color = component(wgpu::BlendFactor::Dst, wgpu::BlendFactor::OneMinusSrcAlpha);
			state.alpha = component(wgpu::BlendFactor::DstAlpha, wgpu::BlendFactor::OneMinusSrcAlpha);
			return state;
		case REND_OPAQUE:
			enabled = false;
			return state;
		default:
			break;
	}
	debug(LOG_FATAL, "Unsupported blend state");
	enabled = false;
	return state;
}

wgpu::VertexFormat to_wgpu(gfx_api::vertex_attribute_type type)
{
	switch (type)
	{
		case gfx_api::vertex_attribute_type::float2: return wgpu::VertexFormat::Float32x2;
		case gfx_api::vertex_attribute_type::float3: return wgpu::VertexFormat::Float32x3;
		case gfx_api::vertex_attribute_type::float4: return wgpu::VertexFormat::Float32x4;
		case gfx_api::vertex_attribute_type::u8x4_norm: return wgpu::VertexFormat::Unorm8x4;
		case gfx_api::vertex_attribute_type::u8x4_uint: return wgpu::VertexFormat::Uint8x4;
		case gfx_api::vertex_attribute_type::int1: return wgpu::VertexFormat::Sint32;
	}
	debug(LOG_FATAL, "Unsupported vertex_attribute_type");
	return wgpu::VertexFormat::Float32x4;
}

wgpu::PrimitiveTopology to_wgpu(gfx_api::primitive_type primitive)
{
	switch (primitive)
	{
		case gfx_api::primitive_type::lines: return wgpu::PrimitiveTopology::LineList;
		case gfx_api::primitive_type::line_strip: return wgpu::PrimitiveTopology::LineStrip;
		case gfx_api::primitive_type::triangles: return wgpu::PrimitiveTopology::TriangleList;
		case gfx_api::primitive_type::triangle_strip: return wgpu::PrimitiveTopology::TriangleStrip;
		case gfx_api::primitive_type::patch_list_4: break; // WebGPU has no tessellation
	}
	debug(LOG_ERROR, "Unsupported primitive_type");
	return wgpu::PrimitiveTopology::TriangleList;
}

wgpu::RenderPipeline wgpu_context::buildPipelineVariant(wgpu_pipeline_state_object& pso, const pipeline_variant_key& key)
{
	const gfx_api::pipeline_create_info& info = pso.createInfo;
	const gfx_api::state_description& state = info.state_desc;
	const gfx_api::shader_program_desc& shaderInfo = gfx_api::shader_catalog(info.shader_mode);

	// Vertex buffers
	std::vector<std::vector<wgpu::VertexAttribute>> attributes(info.attribute_descriptions.size());
	std::vector<wgpu::VertexBufferLayout> buffers(info.attribute_descriptions.size());
	for (size_t i = 0; i < info.attribute_descriptions.size(); ++i)
	{
		const gfx_api::vertex_buffer& buffer = info.attribute_descriptions[i];
		for (const gfx_api::vertex_buffer_input& input : buffer.attributes)
		{
			wgpu::VertexAttribute attribute;
			attribute.format = to_wgpu(input.type);
			attribute.offset = input.offset;
			attribute.shaderLocation = static_cast<uint32_t>(input.id);
			attributes[i].push_back(attribute);
		}
		buffers[i].stepMode = (buffer.rate == gfx_api::vertex_attribute_input_rate::instance) ? wgpu::VertexStepMode::Instance : wgpu::VertexStepMode::Vertex;
		buffers[i].arrayStride = buffer.stride;
		buffers[i].attributeCount = attributes[i].size();
		buffers[i].attributes = attributes[i].data();
	}

	// Specialization constants become WGSL overrides. Vulkan sets them on the fragment stage only,
	// and WebGPU rejects an id that the module does not declare.
	std::vector<std::string> constantKeys;
	std::vector<wgpu::ConstantEntry> constants;
	auto addConstant = [&](uint32_t id, uint32_t value) {
		if (pso.fragmentModule->overrideIds.count(id) == 0)
		{
			return;
		}
		constantKeys.push_back(std::to_string(id));
		wgpu::ConstantEntry entry;
		entry.value = static_cast<double>(value);
		constants.push_back(entry);
	};
	if (shaderInfo.spec_constants & gfx_api::spec_shadow_mode)
	{
		addConstant(1, shadowConstants.shadowMode);
	}
	if (shaderInfo.spec_constants & gfx_api::spec_shadow_filter_size)
	{
		addConstant(2, shadowConstants.shadowFilterSize);
	}
	if (shaderInfo.spec_constants & gfx_api::spec_shadow_cascades)
	{
		addConstant(3, shadowConstants.shadowCascadesCount);
	}
	if (shaderInfo.spec_constants & gfx_api::spec_point_light_enabled)
	{
		addConstant(4, shadowConstants.isPointLightPerPixelEnabled ? 1 : 0);
	}
	for (size_t i = 0; i < constants.size(); ++i)
	{
		constants[i].key = constantKeys[i].c_str();
	}

	// Color targets
	bool blendEnabled = false;
	const wgpu::BlendState blend = to_wgpu(state.blend_state, blendEnabled);
	wgpu::ColorWriteMask writeMask = wgpu::ColorWriteMask::None;
	if (state.output_mask & 0x01) { writeMask |= wgpu::ColorWriteMask::Red; }
	if (state.output_mask & 0x02) { writeMask |= wgpu::ColorWriteMask::Green; }
	if (state.output_mask & 0x04) { writeMask |= wgpu::ColorWriteMask::Blue; }
	if (state.output_mask & 0x08) { writeMask |= wgpu::ColorWriteMask::Alpha; }
	std::vector<wgpu::ColorTargetState> targets(key.colorFormats.size());
	for (size_t i = 0; i < key.colorFormats.size(); ++i)
	{
		targets[i].format = key.colorFormats[i];
		targets[i].blend = blendEnabled ? &blend : nullptr;
		targets[i].writeMask = writeMask;
	}

	wgpu::FragmentState fragment;
	fragment.module = pso.fragmentModule->module;
	fragment.constantCount = constants.size();
	fragment.constants = constants.data();
	fragment.targetCount = targets.size();
	fragment.targets = targets.data();

	// Depth and stencil
	wgpu::DepthStencilState depthStencil;
	if (key.depthFormat != wgpu::TextureFormat::Undefined)
	{
		depthStencil.format = key.depthFormat;
		switch (state.depth_mode)
		{
			case DEPTH_CMP_LEQ_WRT_ON:
				depthStencil.depthCompare = wgpu::CompareFunction::LessEqual;
				depthStencil.depthWriteEnabled = wgpu::OptionalBool::True;
				break;
			case DEPTH_CMP_LEQ_WRT_OFF:
				depthStencil.depthCompare = wgpu::CompareFunction::LessEqual;
				depthStencil.depthWriteEnabled = wgpu::OptionalBool::False;
				break;
			case DEPTH_CMP_ALWAYS_WRT_ON:
				depthStencil.depthCompare = wgpu::CompareFunction::Always;
				depthStencil.depthWriteEnabled = wgpu::OptionalBool::True;
				break;
			case DEPTH_CMP_ALWAYS_WRT_OFF:
			default:
				depthStencil.depthCompare = wgpu::CompareFunction::Always;
				depthStencil.depthWriteEnabled = wgpu::OptionalBool::False;
				break;
		}
		auto stencilFace = [](wgpu::CompareFunction compare, wgpu::StencilOperation passOp) {
			wgpu::StencilFaceState face;
			face.compare = compare;
			face.failOp = wgpu::StencilOperation::Keep;
			face.depthFailOp = wgpu::StencilOperation::Keep;
			face.passOp = passOp;
			return face;
		};
		if (hasStencil(key.depthFormat) && state.stencil != gfx_api::stencil_mode::stencil_disabled)
		{
			if (state.stencil == gfx_api::stencil_mode::stencil_shadow_quad)
			{
				depthStencil.stencilFront = stencilFace(wgpu::CompareFunction::Less, wgpu::StencilOperation::Keep);
				depthStencil.stencilBack = depthStencil.stencilFront;
			}
			else
			{
				depthStencil.stencilFront = stencilFace(wgpu::CompareFunction::Always, wgpu::StencilOperation::IncrementWrap);
				depthStencil.stencilBack = stencilFace(wgpu::CompareFunction::Always, wgpu::StencilOperation::DecrementWrap);
			}
		}
		else
		{
			depthStencil.stencilFront = stencilFace(wgpu::CompareFunction::Always, wgpu::StencilOperation::Keep);
			depthStencil.stencilBack = depthStencil.stencilFront;
		}
		depthStencil.stencilReadMask = 0xFF;
		depthStencil.stencilWriteMask = 0xFF;
		depthStencil.depthBias = key.depthBias;
		depthStencil.depthBiasSlopeScale = key.depthBiasSlopeScale;
	}

	wgpu::RenderPipelineDescriptor desc;
	desc.label = shaderInfo.friendly_name;
	desc.layout = pso.layout;
	desc.vertex.module = pso.vertexModule->module;
	desc.vertex.bufferCount = buffers.size();
	desc.vertex.buffers = buffers.data();
	desc.primitive.topology = to_wgpu(info.primitive);
	desc.primitive.stripIndexFormat = key.stripIndexFormat;
	// The shaders render the same image as Vulkan, and WebGPU (as Vulkan) finds the front face
	// from the triangle orientation in the framebuffer. Thus, use the same front face as Vulkan.
	desc.primitive.frontFace = wgpu::FrontFace::CW;
	switch (state.cull)
	{
		case gfx_api::cull_mode::back:
		case gfx_api::cull_mode::shadow_mapping:
			desc.primitive.cullMode = wgpu::CullMode::Back;
			break;
		case gfx_api::cull_mode::front:
			desc.primitive.cullMode = wgpu::CullMode::Front;
			break;
		case gfx_api::cull_mode::none:
			desc.primitive.cullMode = wgpu::CullMode::None;
			break;
	}
	desc.depthStencil = (key.depthFormat != wgpu::TextureFormat::Undefined) ? &depthStencil : nullptr;
	desc.multisample.count = key.samples;
	desc.fragment = &fragment;

#if defined(__EMSCRIPTEN__)
	// The browser cannot wait for the error scope; validation errors go to the page (webgpu-postjs.js)
	return device.CreateRenderPipeline(&desc);
#else
	// Find the validation errors here, not at the first draw
	device.PushErrorScope(wgpu::ErrorFilter::Validation);
	wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
	bool failed = false;
	instance.WaitAny(device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
		[&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView message) {
			if (type != wgpu::ErrorType::NoError)
			{
				failed = true;
				debug(LOG_ERROR, "Failed to create the WebGPU pipeline \"%s\": %s", shaderInfo.friendly_name, to_std_string(message).c_str());
			}
		}), UINT64_MAX);
	return failed ? wgpu::RenderPipeline() : pipeline;
#endif
}

pipeline_variant_key wgpu_context::currentVariantKey(const wgpu_pipeline_state_object& pso) const
{
	pipeline_variant_key key = passLayout;
	const bool triangles = pso.createInfo.primitive == gfx_api::primitive_type::triangles || pso.createInfo.primitive == gfx_api::primitive_type::triangle_strip;
	if (pso.createInfo.state_desc.offset && triangles && key.depthFormat != wgpu::TextureFormat::Undefined)
	{
		// glPolygonOffset(factor, units): factor scales the depth slope, units the constant part
		key.depthBias = static_cast<int32_t>(std::lround(polygonOffsetUnits));
		key.depthBiasSlopeScale = polygonOffsetFactor;
	}
	if (pso.isStrip() && indexedDraw)
	{
		key.stripIndexFormat = currentIndexFormat;
	}
	return key;
}

void wgpu_context::selectPipelineVariant()
{
	ASSERT_OR_RETURN(, renderPass && currentPSO != nullptr, "No active pass or pipeline");
	const pipeline_variant_key key = currentVariantKey(*currentPSO);
	auto it = currentPSO->variants.find(key);
	if (it == currentPSO->variants.end())
	{
		it = currentPSO->variants.emplace(key, buildPipelineVariant(*currentPSO, key)).first;
	}
	if (it->second.Get() != currentPipeline.Get())
	{
		currentPipeline = it->second;
		if (currentPipeline)
		{
			renderPass.SetPipeline(currentPipeline);
		}
	}
}

void wgpu_context::bind_pipeline(gfx_api::pipeline_state_object* pso, bool notextures)
{
	ASSERT_OR_RETURN(, renderPass.Get() != nullptr, "bind_pipeline() called outside a render pass");
	auto* newPSO = static_cast<wgpu_pipeline_state_object*>(pso);
	ASSERT_OR_RETURN(, newPSO != nullptr && !newPSO->broken, "Invalid pipeline");
	if (newPSO == currentPSO)
	{
		return;
	}
	currentPSO = newPSO;
	currentPipeline = nullptr;
	selectPipelineVariant();

	// Set every group of the layout, so that a draw is valid also if the caller does not set a group
	const uint32_t zeroOffset = 0;
	for (size_t slot = 0; slot < currentPSO->uniformBlockCount; ++slot)
	{
		renderPass.SetBindGroup(static_cast<uint32_t>(slot), zeroUniformBindGroup, 1, &zeroOffset);
	}
	if (currentPSO->textureLayout)
	{
		renderPass.SetBindGroup(static_cast<uint32_t>(currentPSO->uniformBlockCount), getTextureBindGroup(*currentPSO, {}));
	}
}

const wgpu::Sampler& wgpu_context::getSampler(gfx_api::sampler_type type, bool comparison, bool filtering)
{
	wgpu::Sampler& sampler = samplers[{type, comparison, filtering}];
	if (sampler)
	{
		return sampler;
	}
	wgpu::SamplerDescriptor desc;
	auto setAddressMode = [&desc](wgpu::AddressMode mode) {
		desc.addressModeU = mode;
		desc.addressModeV = mode;
		desc.addressModeW = mode;
	};
	auto setFilter = [&desc](wgpu::FilterMode filter, wgpu::MipmapFilterMode mipFilter) {
		desc.magFilter = filter;
		desc.minFilter = filter;
		desc.mipmapFilter = mipFilter;
	};
	switch (type)
	{
		case gfx_api::sampler_type::bilinear:
			setFilter(wgpu::FilterMode::Linear, wgpu::MipmapFilterMode::Nearest);
			setAddressMode(wgpu::AddressMode::ClampToEdge);
			desc.lodMaxClamp = 0.f;
			break;
		case gfx_api::sampler_type::bilinear_repeat:
			setFilter(wgpu::FilterMode::Linear, wgpu::MipmapFilterMode::Nearest);
			setAddressMode(wgpu::AddressMode::Repeat);
			desc.lodMaxClamp = 0.f;
			break;
		case gfx_api::sampler_type::bilinear_border:
			// WebGPU has no border color. Only the shadow maps use border samplers (TODO: emulate in the shader).
			setFilter(wgpu::FilterMode::Linear, wgpu::MipmapFilterMode::Nearest);
			setAddressMode(wgpu::AddressMode::ClampToEdge);
			desc.lodMaxClamp = 0.f;
			break;
		case gfx_api::sampler_type::anisotropic:
			setFilter(wgpu::FilterMode::Linear, wgpu::MipmapFilterMode::Linear);
			setAddressMode(wgpu::AddressMode::ClampToEdge);
			desc.maxAnisotropy = 16;
			break;
		case gfx_api::sampler_type::anisotropic_repeat:
			setFilter(wgpu::FilterMode::Linear, wgpu::MipmapFilterMode::Linear);
			setAddressMode(wgpu::AddressMode::Repeat);
			desc.maxAnisotropy = 16;
			break;
		case gfx_api::sampler_type::nearest_clamped:
			setFilter(wgpu::FilterMode::Nearest, wgpu::MipmapFilterMode::Nearest);
			setAddressMode(wgpu::AddressMode::ClampToEdge);
			desc.lodMaxClamp = 0.f;
			break;
		case gfx_api::sampler_type::nearest_border:
			setFilter(wgpu::FilterMode::Nearest, wgpu::MipmapFilterMode::Nearest);
			setAddressMode(wgpu::AddressMode::ClampToEdge);
			desc.lodMaxClamp = 0.f;
			break;
	}
	if (!filtering)
	{
		// A non-filtering binding (an unfilterable texture) needs a sampler with nearest filters
		setFilter(wgpu::FilterMode::Nearest, wgpu::MipmapFilterMode::Nearest);
		desc.maxAnisotropy = 1;
	}
	if (comparison)
	{
		desc.compare = wgpu::CompareFunction::LessEqual;
	}
	sampler = device.CreateSampler(&desc);
	return sampler;
}

wgpu::TextureView wgpu_context::textureViewFor(gfx_api::abstract_texture* texture, const texture_slot& slot)
{
	wgpu_image* image = imageOf(texture);
	if (image == nullptr || !image->texture)
	{
		// The default texture for the binding type
		if (slot.sampleType == wgpu::TextureSampleType::Depth || slot.sampleType == wgpu::TextureSampleType::UnfilterableFloat)
		{
			image = (slot.viewDimension == wgpu::TextureViewDimension::e2DArray) ? &defaultDepthArrayTexture->image : &defaultDepthTexture->image;
		}
		else
		{
			image = (slot.viewDimension == wgpu::TextureViewDimension::e2DArray) ? &defaultArrayTexture->image : &defaultTexture->image;
		}
	}
	return image->sampleView(slot.viewDimension);
}

wgpu::BindGroup wgpu_context::getTextureBindGroup(wgpu_pipeline_state_object& pso, const std::vector<gfx_api::abstract_texture*>& textures)
{
	std::vector<wgpu::TextureView> views;
	views.reserve(pso.textureSlots.size());
	std::vector<uintptr_t> key;
	key.reserve(pso.textureSlots.size() + 3);
	key.push_back(reinterpret_cast<uintptr_t>(pso.textureLayout.Get()));
	for (size_t i = 0; i < pso.textureSlots.size(); ++i)
	{
		views.push_back(textureViewFor((i < textures.size()) ? textures[i] : nullptr, pso.textureSlots[i]));
		key.push_back(reinterpret_cast<uintptr_t>(views.back().Get()));
	}
	if (pso.hasLightData)
	{
		key.push_back(reinterpret_cast<uintptr_t>(lightDataBuffer.Get()));
		key.push_back(reinterpret_cast<uintptr_t>(lightIndexBuffer.Get()));
	}

	auto it = textureBindGroups.find(key);
	if (it != textureBindGroups.end())
	{
		return it->second;
	}

	std::vector<wgpu::BindGroupEntry> entries;
	for (size_t i = 0; i < pso.textureSlots.size(); ++i)
	{
		const texture_slot& slot = pso.textureSlots[i];
		wgpu::BindGroupEntry textureEntry;
		textureEntry.binding = slot.textureBinding;
		textureEntry.textureView = views[i];
		entries.push_back(textureEntry);
		wgpu::BindGroupEntry samplerEntry;
		samplerEntry.binding = slot.samplerBinding;
		samplerEntry.sampler = getSampler(slot.sampler, slot.samplerType == wgpu::SamplerBindingType::Comparison, slot.samplerType != wgpu::SamplerBindingType::NonFiltering);
		entries.push_back(samplerEntry);
	}
	if (pso.hasLightData)
	{
		wgpu::BindGroupEntry lightsEntry;
		lightsEntry.binding = 2 * LightDataStorageBinding;
		lightsEntry.buffer = lightDataBuffer;
		entries.push_back(lightsEntry);
		wgpu::BindGroupEntry indicesEntry;
		indicesEntry.binding = 2 * LightIndexStorageBinding;
		indicesEntry.buffer = lightIndexBuffer;
		entries.push_back(indicesEntry);
	}

	wgpu::BindGroupDescriptor desc;
	desc.layout = pso.textureLayout;
	desc.entryCount = entries.size();
	desc.entries = entries.data();
	wgpu::BindGroup bindGroup = device.CreateBindGroup(&desc);
	textureBindGroups[key] = bindGroup;
	return bindGroup;
}

void wgpu_context::bind_textures(const std::vector<gfx_api::texture_input>& texture_descriptions, const std::vector<gfx_api::abstract_texture*>& textures)
{
	ASSERT_OR_RETURN(, renderPass && currentPSO != nullptr, "currentPSO == NULL");
	ASSERT(textures.size() <= texture_descriptions.size(), "Received more textures than expected");
	if (!currentPSO->textureLayout)
	{
		return;
	}
	renderPass.SetBindGroup(static_cast<uint32_t>(currentPSO->uniformBlockCount), getTextureBindGroup(*currentPSO, textures));
}

bool wgpu_context::setShadowConstants(gfx_api::lighting_constants values)
{
	if (shadowConstants == values)
	{
		return true;
	}
	shadowConstants = values;
	// The constants are pipeline overrides, so rebuild the pipelines at their next bind
	for (wgpu_pipeline_state_object* pso : pipelines)
	{
		if (gfx_api::shader_catalog(pso->createInfo.shader_mode).spec_constants != 0)
		{
			pso->variants.clear();
		}
	}
	currentPSO = nullptr;
	currentPipeline = nullptr;
	return true;
}

bool wgpu_context::debugRecompileAllPipelines()
{
	for (wgpu_pipeline_state_object* pso : pipelines)
	{
		pso->variants.clear();
	}
	currentPSO = nullptr;
	currentPipeline = nullptr;
	return true;
}

// MARK: wgpu_context - draws

void wgpu_context::bind_vertex_buffers(const std::size_t& first, const std::vector<std::tuple<gfx_api::buffer*, std::size_t>>& vertex_buffers_offset)
{
	ASSERT_OR_RETURN(, renderPass && currentPSO != nullptr, "currentPSO == NULL");
	for (size_t i = 0; i < vertex_buffers_offset.size(); ++i)
	{
		auto* buffer = static_cast<wgpu_buffer*>(std::get<0>(vertex_buffers_offset[i]));
		ASSERT_OR_RETURN(, buffer != nullptr && buffer->object, "bind_vertex_buffers called with a null or empty vertex buffer");
		ASSERT(buffer->usage == gfx_api::buffer::usage::vertex_buffer, "bind_vertex_buffers called with non-vertex-buffer");
		renderPass.SetVertexBuffer(static_cast<uint32_t>(first + i), buffer->object, std::get<1>(vertex_buffers_offset[i]));
	}
}

void wgpu_context::bind_streamed_vertex_buffers(const void* data, const std::size_t size)
{
	ASSERT_OR_RETURN(, renderPass && currentPSO != nullptr, "currentPSO == NULL");
	ASSERT_OR_RETURN(, size > 0, "bind_streamed_vertex_buffers called with size 0");
	const chunk_allocation allocation = allocateStreamedVertexData(data, size);
	renderPass.SetVertexBuffer(0, allocation.chunk->buffer, allocation.offset, size);
}

void wgpu_context::bind_index_buffer(gfx_api::buffer& _buffer, const gfx_api::index_type& index)
{
	ASSERT_OR_RETURN(, renderPass && currentPSO != nullptr, "currentPSO == NULL");
	auto& buffer = static_cast<wgpu_buffer&>(_buffer);
	ASSERT_OR_RETURN(, buffer.usage == gfx_api::buffer::usage::index_buffer && buffer.object, "Passed gfx_api::buffer is not an index buffer");
	currentIndexFormat = (index == gfx_api::index_type::u16) ? wgpu::IndexFormat::Uint16 : wgpu::IndexFormat::Uint32;
	renderPass.SetIndexBuffer(buffer.object, currentIndexFormat);
	if (currentPSO->isStrip() && indexedDraw)
	{
		selectPipelineVariant(); // the strip index format changed
	}
}

void wgpu_context::draw(const size_t& offset, const size_t& count, const gfx_api::primitive_type& primitive)
{
	draw_instanced(offset, count, primitive, 1);
}

void wgpu_context::draw_instanced(const std::size_t& offset, const std::size_t& count, const gfx_api::primitive_type& primitive, std::size_t instance_count)
{
	ASSERT_OR_RETURN(, renderPass && currentPSO != nullptr, "draw() called without a pipeline");
	if (!currentPipeline)
	{
		return; // the pipeline failed to build (already reported)
	}
	if (currentPSO->isStrip() && indexedDraw)
	{
		// A strip pipeline for non-indexed draws must have no strip index format
		indexedDraw = false;
		selectPipelineVariant();
		if (!currentPipeline)
		{
			return;
		}
	}
	renderPass.Draw(static_cast<uint32_t>(count), static_cast<uint32_t>(instance_count), static_cast<uint32_t>(offset), 0);
}

void wgpu_context::draw_elements(const size_t& offset, const size_t& count, const gfx_api::primitive_type& primitive, const gfx_api::index_type& index)
{
	draw_elements_instanced(offset, count, primitive, index, 1);
}

void wgpu_context::draw_elements_instanced(const std::size_t& offset, const std::size_t& count, const gfx_api::primitive_type& primitive, const gfx_api::index_type& index, std::size_t instance_count)
{
	ASSERT_OR_RETURN(, renderPass && currentPSO != nullptr, "draw_elements() called without a pipeline");
	if (!currentPipeline)
	{
		return;
	}
	if (currentPSO->isStrip() && !indexedDraw)
	{
		indexedDraw = true;
		selectPipelineVariant();
		if (!currentPipeline)
		{
			return;
		}
	}
	// The offset is in bytes
	const size_t indexSize = (index == gfx_api::index_type::u16) ? 2 : 4;
	renderPass.DrawIndexed(static_cast<uint32_t>(count), static_cast<uint32_t>(instance_count), static_cast<uint32_t>(offset / indexSize), 0, 0);
}

void wgpu_context::set_polygon_offset(const float& factor, const float& units)
{
	polygonOffsetFactor = factor;
	polygonOffsetUnits = units;
	if (renderPass && currentPSO != nullptr && currentPSO->createInfo.state_desc.offset)
	{
		selectPipelineVariant(); // depth bias is pipeline state in WebGPU
	}
}

void wgpu_context::applyViewport()
{
	// As in Vulkan: the depth range applies to the passes that target the swapchain
	const float minDepth = passTargetsSwapchain ? depthRangeMin : 0.f;
	const float maxDepth = passTargetsSwapchain ? depthRangeMax : 1.f;
	renderPass.SetViewport(0.f, 0.f, static_cast<float>(passWidth), static_cast<float>(passHeight), minDepth, maxDepth);
	renderPass.SetScissorRect(0, 0, passWidth, passHeight);
}

void wgpu_context::set_depth_range(const float& min, const float& max)
{
	depthRangeMin = min;
	depthRangeMax = max;
	if (renderPass && passTargetsSwapchain)
	{
		applyViewport();
	}
}

// MARK: wgpu_context - render passes

wgpu::TextureView wgpu_context::attachmentViewFor(const gfx_api::AttachmentDesc& attachment, wgpu::TextureFormat& format, uint32_t& samples)
{
	ASSERT_OR_RETURN(nullptr, attachment.texture != nullptr, "Null attachment texture");
	if (static_cast<WgpuTextureKind>(attachment.texture->backend_internal_value()) == WgpuTextureKind::SwapchainColor)
	{
		format = surfaceFormat;
		samples = 1;
		return swapchainView;
	}
	wgpu_image* image = imageOf(attachment.texture);
	ASSERT_OR_RETURN(nullptr, image != nullptr && image->texture, "Attachment has no texture");
	format = image->format;
	samples = image->samples;
	return image->attachmentView(attachment.mipLevel, attachment.arrayLayer);
}

void wgpu_context::beginPass(const gfx_api::RenderPassDesc& pass, const gfx_api::CompiledPass* compiledPass)
{
	ASSERT_OR_RETURN(, !renderPass, "beginPass called while another pass is active");
	ASSERT_OR_RETURN(, pass.viewportSize.has_value(), "Pass requires resolved viewportSize");
	ASSERT_OR_RETURN(, !pass.colorAttachments.empty() || (pass.depthAttachment.has_value() && pass.depthAttachment->texture != nullptr),
		"Pass requires at least one color or depth attachment");
	if (!ensureEncoder())
	{
		return;
	}

	passTargetsSwapchain = gfx_api::passTargetsSwapchainColor(pass);
	if (passTargetsSwapchain && !swapchainAcquired)
	{
		debug(LOG_ERROR, "Pass \"%s\" targets the swapchain, but no swapchain image is acquired", pass.debugName.c_str());
	}
	passWidth = pass.viewportSize->first;
	passHeight = pass.viewportSize->second;
	passLayout = pipeline_variant_key();

	auto toLoadOp = [](gfx_api::AttachmentLoadOp op) {
		// WebGPU has no "don't care" load operation; a clear is the cheap choice
		return (op == gfx_api::AttachmentLoadOp::Load) ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear;
	};
	auto toStoreOp = [](const std::optional<gfx_api::AttachmentStoreOp>& op) {
		if (!op.has_value())
		{
			return wgpu::StoreOp::Store;
		}
		switch (op.value())
		{
			case gfx_api::AttachmentStoreOp::Store: return wgpu::StoreOp::Store;
			case gfx_api::AttachmentStoreOp::DontCare:
			case gfx_api::AttachmentStoreOp::Resolve: // the resolve target keeps the result
			case gfx_api::AttachmentStoreOp::Invalidate:
				return wgpu::StoreOp::Discard;
		}
		return wgpu::StoreOp::Store;
	};

	std::vector<wgpu::RenderPassColorAttachment> colorAttachments(pass.colorAttachments.size());
	for (size_t i = 0; i < pass.colorAttachments.size(); ++i)
	{
		const gfx_api::AttachmentDesc& attachment = pass.colorAttachments[i];
		wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
		uint32_t samples = 1;
		colorAttachments[i].view = attachmentViewFor(attachment, format, samples);
		colorAttachments[i].loadOp = toLoadOp(attachment.loadOp);
		colorAttachments[i].storeOp = toStoreOp(attachment.storeOp);
		const auto& c = attachment.clearValue.color;
		colorAttachments[i].clearValue = {c[0], c[1], c[2], c[3]};
		passLayout.colorFormats.push_back(format);
		passLayout.samples = samples;
	}
	if (pass.resolveAttachment.has_value() && pass.resolveAttachment->texture != nullptr && !colorAttachments.empty())
	{
		wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
		uint32_t samples = 1;
		colorAttachments[0].resolveTarget = attachmentViewFor(pass.resolveAttachment.value(), format, samples);
	}

	wgpu::RenderPassDepthStencilAttachment depthAttachment;
	const bool hasDepth = pass.depthAttachment.has_value() && pass.depthAttachment->texture != nullptr;
	if (hasDepth)
	{
		const gfx_api::AttachmentDesc& attachment = pass.depthAttachment.value();
		wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
		uint32_t samples = 1;
		depthAttachment.view = attachmentViewFor(attachment, format, samples);
		depthAttachment.depthLoadOp = toLoadOp(attachment.loadOp);
		depthAttachment.depthStoreOp = toStoreOp(attachment.storeOp);
		depthAttachment.depthClearValue = attachment.clearValue.depth;
		if (hasStencil(format))
		{
			depthAttachment.stencilLoadOp = depthAttachment.depthLoadOp;
			depthAttachment.stencilStoreOp = depthAttachment.depthStoreOp;
			depthAttachment.stencilClearValue = attachment.clearValue.stencil;
		}
		passLayout.depthFormat = format;
		if (pass.colorAttachments.empty())
		{
			passLayout.samples = samples;
		}
	}

	wgpu::RenderPassDescriptor desc;
	desc.label = pass.debugName.c_str();
	desc.colorAttachmentCount = colorAttachments.size();
	desc.colorAttachments = colorAttachments.data();
	desc.depthStencilAttachment = hasDepth ? &depthAttachment : nullptr;
	renderPass = encoder.BeginRenderPass(&desc);

	applyViewport();
	currentPSO = nullptr;
	currentPipeline = nullptr;
	currentIndexFormat = wgpu::IndexFormat::Undefined;
	indexedDraw = false;
}

void wgpu_context::endPass(const gfx_api::CompiledPass* compiledPass)
{
	ASSERT_OR_RETURN(, renderPass.Get() != nullptr, "endPass called without an active pass");
	renderPass.End();
	renderPass = nullptr;
	if (passTargetsSwapchain)
	{
		swapchainWritten = true;
	}
	passTargetsSwapchain = false;
	currentPSO = nullptr;
	currentPipeline = nullptr;
}

void wgpu_context::debugStringMarker(const char* str)
{
	if (renderPass)
	{
		renderPass.InsertDebugMarker(str);
	}
	else if (encoder)
	{
		encoder.InsertDebugMarker(str);
	}
}

void wgpu_context::debugSceneBegin(const char* descr)
{
	if (renderPass)
	{
		renderPass.PushDebugGroup(descr);
	}
}

void wgpu_context::debugSceneEnd(const char* descr)
{
	if (renderPass)
	{
		renderPass.PopDebugGroup();
	}
}

// MARK: wgpu_context - pipeline surfaces

gfx_api::PipelineSurfaceSyncInputs wgpu_context::pipelineSurfaceSyncInputs() const
{
	gfx_api::PipelineSurfaceSyncInputs inputs;
	if (surfaceConfigured && surfaceWidth > 0 && surfaceHeight > 0)
	{
		inputs.drawableW = surfaceWidth;
		inputs.drawableH = surfaceHeight;
		const uint32_t scalePercent = getSceneRenderScalePercent();
		inputs.sceneW = std::max((surfaceWidth * scalePercent) / 100u, 2u);
		inputs.sceneH = std::max((surfaceHeight * scalePercent) / 100u, 2u);
	}
	inputs.shadowMapSize = depthMapSize;
	inputs.numShadowCascades = static_cast<uint32_t>(std::min<size_t>(depthPassCount, WZ_MAX_SHADOW_CASCADES));
	inputs.sceneMsaaSamples = sceneMsaaSamples;
	inputs.swapchainMsaaSamples = 1;
	inputs.presentColorFormat = (surfaceFormat == wgpu::TextureFormat::BGRA8Unorm) ? gfx_api::pixel_format::FORMAT_BGRA8_UNORM_PACK8 : gfx_api::pixel_format::FORMAT_RGBA8_UNORM_PACK8;
	inputs.fsr1SceneUpscale = (getSceneUpscalingMode() == gfx_api::context::scene_upscaling_mode::fsr1);
	inputs.sceneDynamicResolution = sceneDynamicResolutionEnabled();
	inputs.smaa = smaaEnabled();
	inputs.effects = storedSceneEffectSurfaces();
	inputs.prepassNeeds = gfx_api::prepassNeeds(inputs.effects);
	return inputs;
}

gfx_api::SurfaceCapabilityHints wgpu_context::surfaceCapabilities() const
{
	gfx_api::SurfaceCapabilityHints caps;
	caps.sceneColorFormat = gfx_api::pixel_format::FORMAT_RGBA8_UNORM_PACK8;
	caps.depthStencilFormat = gfx_api::pixel_format::FORMAT_D24_UNORM_S8;
	caps.depthSampledFormat = gfx_api::pixel_format::FORMAT_D32_SFLOAT;
	return caps;
}

bool wgpu_context::ensurePipelineSurfaces(const gfx_api::ResolvedSurfaceTable& specs)
{
	PipelineSurfaceAllocator allocator(*this);
	return pipelineSurfaces.ensure(specs, allocator);
}

bool wgpu_context::PipelineSurfaceAllocator::create(gfx_api::PipelineSurfaceId id, const gfx_api::ResolvedSurfaceSpec& spec, gfx_api::abstract_texture*& outTexture)
{
	outTexture = nullptr;
	ASSERT_OR_RETURN(false, id != gfx_api::PipelineSurfaceId::Count, "Invalid pipeline surface id");
	auto surfaceTexture = std::make_unique<wgpu_surface_texture>();
	const std::string label = "<pipeline surface " + std::to_string(static_cast<unsigned>(id)) + ">";
	const wgpu::TextureFormat format = to_wgpu(spec.format);
	const uint32_t samples = std::max<uint32_t>(spec.samples, 1);
	bool created = false;

	switch (spec.provisionMode)
	{
		case gfx_api::SurfaceProvisionMode::WsiPresentColor:
			// The swapchain image changes each frame (see attachmentViewFor)
			surfaceTexture->kind = WgpuTextureKind::SwapchainColor;
			created = true;
			break;
		case gfx_api::SurfaceProvisionMode::WsiPresentDepth:
			// WebGPU surfaces have no depth buffer, so make one
			created = surfaceTexture->image.create(root.device, (format != wgpu::TextureFormat::Undefined) ? format : wgpu::TextureFormat::Depth24PlusStencil8,
				spec.width, spec.height, 1, 1, samples, wgpu::TextureUsage::RenderAttachment, label);
			break;
		case gfx_api::SurfaceProvisionMode::Allocate:
			switch (spec.storageKind)
			{
				case gfx_api::SurfaceStorageKind::SampledColor2D:
					created = surfaceTexture->image.create(root.device, format, spec.width, spec.height, 1, 1, 1,
						wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc, label);
					break;
				case gfx_api::SurfaceStorageKind::MsaaColorAttachment:
				case gfx_api::SurfaceStorageKind::DepthStencilAttachment:
					created = surfaceTexture->image.create(root.device, format, spec.width, spec.height, 1, 1, samples,
						wgpu::TextureUsage::RenderAttachment, label);
					break;
				case gfx_api::SurfaceStorageKind::SampledDepthArray:
					surfaceTexture->arrayTexture = true;
					created = surfaceTexture->image.create(root.device, format, spec.width, spec.height, std::max<uint32_t>(spec.arrayLayers, 1), 1, 1,
						wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding, label);
					break;
				case gfx_api::SurfaceStorageKind::SampledDepth2D:
					created = surfaceTexture->image.create(root.device, format, spec.width, spec.height, 1, 1, 1,
						wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding, label);
					break;
				case gfx_api::SurfaceStorageKind::None:
					debug(LOG_ERROR, "Allocate surface %u has storageKind None", static_cast<unsigned>(id));
					return false;
			}
			break;
	}
	if (!created)
	{
		debug(LOG_ERROR, "Failed to create the pipeline surface %u (%ux%u, format %d)", static_cast<unsigned>(id), spec.width, spec.height, static_cast<int>(format));
		return false;
	}
	surfaceTexture->image.wzFormat = spec.format;
	root.surfaceTextures[static_cast<size_t>(id)] = std::move(surfaceTexture);
	outTexture = root.surfaceTextures[static_cast<size_t>(id)].get();
	return true;
}

void wgpu_context::PipelineSurfaceAllocator::destroy(gfx_api::PipelineSurfaceId id, gfx_api::abstract_texture* texture)
{
	ASSERT_OR_RETURN(, id != gfx_api::PipelineSurfaceId::Count, "Invalid pipeline surface id");
	// Release only: commands recorded earlier in this frame can still use the texture
	root.surfaceTextures[static_cast<size_t>(id)].reset();
}

void wgpu_context::PipelineSurfaceAllocator::prepareForSurfaceDestroy()
{
	root.textureBindGroups.clear();
}

void wgpu_context::PipelineSurfaceAllocator::onChanged()
{
	root.textureBindGroups.clear();
	root.bumpRenderGraphEpoch();
}

bool wgpu_context::isMultisampledColorAttachment(gfx_api::abstract_texture* texture) const
{
	const wgpu_image* image = imageOf(texture);
	return image != nullptr && image->samples > 1 && !isDepthFormat(image->format);
}

optional<std::pair<uint32_t, uint32_t>> wgpu_context::getRenderTargetDimensions(gfx_api::abstract_texture* texture)
{
	if (texture == nullptr)
	{
		return nullopt;
	}
	if (static_cast<WgpuTextureKind>(texture->backend_internal_value()) == WgpuTextureKind::SwapchainColor)
	{
		return std::make_pair(surfaceWidth, surfaceHeight);
	}
	wgpu_image* image = imageOf(texture);
	if (image == nullptr || !image->texture)
	{
		return nullopt;
	}
	return std::make_pair(image->width, image->height);
}

bool wgpu_context::setDepthPassProperties(size_t numDepthPasses, size_t depthBufferResolution)
{
	const size_t clampedDepthPasses = std::min<size_t>(numDepthPasses, WZ_MAX_SHADOW_CASCADES);
	const uint32_t clampedDepthMapSize = static_cast<uint32_t>(depthBufferResolution);
	if (depthPassCount == clampedDepthPasses && depthMapSize == clampedDepthMapSize)
	{
		return true;
	}
	depthPassCount = clampedDepthPasses;
	depthMapSize = clampedDepthMapSize;
	return !surfaceConfigured || syncPipelineSurfaces();
}

bool wgpu_context::setSceneRenderScale(uint32_t scalePercent)
{
	const uint32_t oldScalePercent = getSceneRenderScalePercent();
	gfx_api::context::setSceneRenderScale(scalePercent);
	if (getSceneRenderScalePercent() == oldScalePercent || !surfaceConfigured)
	{
		return true;
	}
	if (!syncPipelineSurfaces())
	{
		gfx_api::context::setSceneRenderScale(oldScalePercent);
		syncPipelineSurfaces();
		return false;
	}
	return true;
}

bool wgpu_context::setSceneDynamicResolution(bool enabled)
{
	gfx_api::context::setSceneDynamicResolution(enabled);
	return !surfaceConfigured || syncPipelineSurfaces();
}

bool wgpu_context::setSceneUpscalingMode(scene_upscaling_mode mode)
{
	if (mode == getSceneUpscalingMode())
	{
		return true;
	}
	gfx_api::context::setSceneUpscalingMode(mode);
	return !surfaceConfigured || syncPipelineSurfaces();
}

bool wgpu_context::setSmaaEnabled(bool enabled)
{
	if (enabled == smaaEnabled())
	{
		return true;
	}
	gfx_api::context::setSmaaEnabled(enabled);
	return !surfaceConfigured || syncPipelineSurfaces();
}

bool wgpu_context::setSceneEffectSurfaces(gfx_api::SceneEffectSurfaces cfg)
{
	gfx_api::context::setSceneEffectSurfaces(cfg);
	return !surfaceConfigured || syncPipelineSurfaces();
}

// MARK: wgpu_context - resources

gfx_api::texture* wgpu_context::create_texture(const size_t& mipmap_count, const size_t& width, const size_t& height, const gfx_api::pixel_format& internal_format, const std::string& filename)
{
	auto* new_texture = new wgpu_texture();
	new_texture->queue = &queue;
	new_texture->image.wzFormat = internal_format;
	const wgpu::TextureFormat format = to_wgpu(internal_format);
	ASSERT(format != wgpu::TextureFormat::Undefined, "Unsupported texture format: %s", gfx_api::format_to_str(internal_format));
	if (format != wgpu::TextureFormat::Undefined)
	{
		new_texture->image.create(device, format, static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1, static_cast<uint32_t>(mipmap_count), 1,
			wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst, filename);
	}
	return new_texture;
}

gfx_api::texture_array* wgpu_context::create_texture_array(const size_t& mipmap_count, const size_t& layer_count, const size_t& width, const size_t& height, const gfx_api::pixel_format& internal_format, const std::string& filename)
{
	ASSERT(mipmap_count > 0, "mipmap_count must be > 0");
	auto* new_texture = new wgpu_texture_array();
	new_texture->queue = &queue;
	new_texture->image.wzFormat = internal_format;
	const wgpu::TextureFormat format = to_wgpu(internal_format);
	ASSERT(format != wgpu::TextureFormat::Undefined, "Unsupported texture format: %s", gfx_api::format_to_str(internal_format));
	if (format != wgpu::TextureFormat::Undefined)
	{
		new_texture->image.create(device, format, static_cast<uint32_t>(width), static_cast<uint32_t>(height), static_cast<uint32_t>(layer_count), static_cast<uint32_t>(mipmap_count), 1,
			wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst, filename);
	}
	return new_texture;
}

gfx_api::buffer* wgpu_context::create_buffer_object(const gfx_api::buffer::usage& usage, const buffer_storage_hint& hint, const std::string& debugName)
{
	auto* buffer = new wgpu_buffer(usage);
	buffer->label = debugName;
	buffer->device = &device;
	buffer->queue = &queue;
	return buffer;
}

bool wgpu_context::textureFormatIsSupported(gfx_api::pixel_format_target target, gfx_api::pixel_format format, gfx_api::pixel_format_usage::flags usage)
{
	if (usage & gfx_api::pixel_format_usage::storage_image)
	{
		return false;
	}
	const wgpu::TextureFormat wgpuFormat = to_wgpu(format);
	if (wgpuFormat == wgpu::TextureFormat::Undefined)
	{
		return false;
	}
	if (isDepthFormat(wgpuFormat) != ((usage & gfx_api::pixel_format_usage::depth_stencil_attachment) != 0 || target == gfx_api::pixel_format_target::depth_map))
	{
		return false;
	}
	switch (format)
	{
		case gfx_api::pixel_format::FORMAT_RGB_BC1_UNORM:
		case gfx_api::pixel_format::FORMAT_RGBA_BC2_UNORM:
		case gfx_api::pixel_format::FORMAT_RGBA_BC3_UNORM:
		case gfx_api::pixel_format::FORMAT_R_BC4_UNORM:
		case gfx_api::pixel_format::FORMAT_RG_BC5_UNORM:
		case gfx_api::pixel_format::FORMAT_RGBA_BPTC_UNORM:
			return enabledFeatures.count(wgpu::FeatureName::TextureCompressionBC) > 0;
		case gfx_api::pixel_format::FORMAT_RGB8_ETC1:
		case gfx_api::pixel_format::FORMAT_RGB8_ETC2:
		case gfx_api::pixel_format::FORMAT_RGBA8_ETC2_EAC:
		case gfx_api::pixel_format::FORMAT_R11_EAC:
		case gfx_api::pixel_format::FORMAT_RG11_EAC:
			return enabledFeatures.count(wgpu::FeatureName::TextureCompressionETC2) > 0;
		case gfx_api::pixel_format::FORMAT_ASTC_4x4_UNORM:
			return enabledFeatures.count(wgpu::FeatureName::TextureCompressionASTC) > 0;
		case gfx_api::pixel_format::FORMAT_R16_UNORM:
		case gfx_api::pixel_format::FORMAT_RG16_UNORM:
#if defined(__EMSCRIPTEN__)
			return false; // see to_wgpu()
#else
			return enabledFeatures.count(wgpu::FeatureName::TextureFormatsTier1) > 0;
#endif
		case gfx_api::pixel_format::FORMAT_D32_SFLOAT_S8_UINT:
			return enabledFeatures.count(wgpu::FeatureName::Depth32FloatStencil8) > 0;
		default:
			return true;
	}
}

int32_t wgpu_context::get_context_value(const context_value property)
{
	switch (property)
	{
		case gfx_api::context::context_value::MAX_TEXTURE_SIZE:
			return static_cast<int32_t>(deviceLimits.maxTextureDimension2D);
		case gfx_api::context::context_value::MAX_SAMPLES:
			return 4; // WebGPU supports only 1 and 4 samples
		case gfx_api::context::context_value::MAX_ARRAY_TEXTURE_LAYERS:
			return static_cast<int32_t>(deviceLimits.maxTextureArrayLayers);
		case gfx_api::context::context_value::MAX_VERTEX_ATTRIBS:
			return static_cast<int32_t>(deviceLimits.maxVertexAttributes);
		case gfx_api::context::context_value::MAX_VERTEX_OUTPUT_COMPONENTS:
			return static_cast<int32_t>(deviceLimits.maxInterStageShaderVariables * 4);
		case gfx_api::context::context_value::MAX_TESS_GEN_LEVEL:
			return 0; // WebGPU has no tessellation
	}
	debug(LOG_FATAL, "Unsupported property");
	return 0;
}

// MARK: wgpu_context - screenshots

bool wgpu_context::getScreenshot(std::function<void (std::unique_ptr<iV_Image>)> callback)
{
#if defined(__EMSCRIPTEN__)
	return false; // TODO: the readback waits for MapAsync, which the browser cannot do
#else
	if (!swapchainAcquired || renderPass || !(surfaceUsage & wgpu::TextureUsage::CopySrc) || screenshotCallback || !ensureEncoder())
	{
		return false; // like Vulkan, fail if the swapchain cannot be copied
	}
	screenshotWidth = surfaceWidth;
	screenshotHeight = surfaceHeight;
	screenshotBytesPerRow = static_cast<uint32_t>(alignUp(static_cast<uint64_t>(screenshotWidth) * 4, 256));
	wgpu::BufferDescriptor desc;
	desc.label = "WZ screenshot";
	desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
	desc.size = static_cast<uint64_t>(screenshotBytesPerRow) * screenshotHeight;
	screenshotBuffer = device.CreateBuffer(&desc);

	wgpu::TexelCopyTextureInfo source;
	source.texture = swapchainTexture;
	wgpu::TexelCopyBufferInfo destination;
	destination.buffer = screenshotBuffer;
	destination.layout.bytesPerRow = screenshotBytesPerRow;
	destination.layout.rowsPerImage = screenshotHeight;
	wgpu::Extent3D size = {screenshotWidth, screenshotHeight, 1};
	encoder.CopyTextureToBuffer(&source, &destination, &size);
	screenshotCallback = std::move(callback);
	return true;
#endif
}

void wgpu_context::completeScreenshot()
{
#if !defined(__EMSCRIPTEN__)
	auto callback = std::move(screenshotCallback);
	screenshotCallback = nullptr;
	bool mapped = false;
	instance.WaitAny(screenshotBuffer.MapAsync(wgpu::MapMode::Read, 0, screenshotBuffer.GetSize(), wgpu::CallbackMode::WaitAnyOnly,
		[&mapped](wgpu::MapAsyncStatus status, wgpu::StringView message) {
			mapped = (status == wgpu::MapAsyncStatus::Success);
			if (!mapped)
			{
				debug(LOG_ERROR, "Failed to map the screenshot buffer: %s", to_std_string(message).c_str());
			}
		}), UINT64_MAX);
	if (!mapped)
	{
		screenshotBuffer = nullptr;
		return;
	}

	auto image = std::make_unique<iV_Image>();
	image->allocate(screenshotWidth, screenshotHeight, 3, false);
	const auto* source = static_cast<const uint8_t*>(screenshotBuffer.GetConstMappedRange());
	uint8_t* target = image->bmp_w();
	const bool bgra = (surfaceFormat == wgpu::TextureFormat::BGRA8Unorm);
	for (uint32_t y = 0; y < screenshotHeight; ++y)
	{
		// The screenshot image is bottom-up, as in the Vulkan backend (vk/screenshot_readback.cpp)
		const uint8_t* row = source + static_cast<size_t>(screenshotHeight - 1 - y) * screenshotBytesPerRow;
		for (uint32_t x = 0; x < screenshotWidth; ++x)
		{
			uint8_t* pixel = target + (static_cast<size_t>(y) * screenshotWidth + x) * 3;
			pixel[0] = row[x * 4 + (bgra ? 2 : 0)];
			pixel[1] = row[x * 4 + 1];
			pixel[2] = row[x * 4 + (bgra ? 0 : 2)];
		}
	}
	screenshotBuffer.Unmap();
	screenshotBuffer = nullptr;
	callback(std::move(image));
#endif
}

// MARK: wgpu_context - shader check

// With --gfxdebug, compile the WGSL of every shader program that WebGPU supports, and report
// the errors. This checks the translation, the packing into base.wz, and Dawn's validation.
bool wgpu_context::compileAllShaderModules()
{
#if defined(__EMSCRIPTEN__)
	return true; // the check waits for the compilation info, which the browser cannot do
#else
	std::set<std::string> paths;
	for (int mode = static_cast<int>(SHADER_NONE) + 1; mode < static_cast<int>(SHADER_MAX); ++mode)
	{
		const gfx_api::shader_program_desc& desc = gfx_api::shader_catalog(static_cast<SHADER_MODE>(mode));
		if (desc.stage_count == 0 || !gfx_api::shader_supported_by_webgpu(desc))
		{
			continue;
		}
		for (uint8_t i = 0; i < desc.stage_count; ++i)
		{
			paths.insert(gfx_api::wgsl_path(desc.stages[i].basename));
		}
	}

	size_t failures = 0;
	for (const std::string& path : paths)
	{
		const bool isVertex = path.find(".vert.") != std::string::npos;
		wgpu_shader_module* entry = getShaderModule(path, isVertex ? wgpu::ShaderStage::Vertex : wgpu::ShaderStage::Fragment);
		if (entry == nullptr)
		{
			++failures;
			continue;
		}
		bool hasError = false;
		instance.WaitAny(entry->module.GetCompilationInfo(wgpu::CallbackMode::WaitAnyOnly,
			[&](wgpu::CompilationInfoRequestStatus, const wgpu::CompilationInfo* compilationInfo) {
				for (size_t m = 0; compilationInfo != nullptr && m < compilationInfo->messageCount; ++m)
				{
					const wgpu::CompilationMessage& message = compilationInfo->messages[m];
					if (message.type == wgpu::CompilationMessageType::Error)
					{
						hasError = true;
						debug(LOG_ERROR, "%s:%llu: %s", path.c_str(), static_cast<unsigned long long>(message.lineNum), to_std_string(message.message).c_str());
					}
				}
			}), UINT64_MAX);
		failures += hasError ? 1 : 0;
	}

	debug(LOG_INFO, "WebGPU shader check: %zu shader modules, %zu failed", paths.size(), failures);
	return failures == 0;
#endif
}

// MARK: wgpu_context - shutdown

void wgpu_context::releaseWebGPUObjects()
{
	if (renderPass)
	{
		renderPass.End();
		renderPass = nullptr;
	}
	encoder = nullptr;
	swapchainView = nullptr;
	swapchainTexture = nullptr;
	swapchainAcquired = false;

	// Pipelines are owned by the pipeline_state_helper objects, so only stop tracking them
	for (wgpu_pipeline_state_object* pso : pipelines)
	{
		pso->variants.clear();
		pso->layout = nullptr;
		pso->textureLayout = nullptr;
		pso->vertexModule = nullptr;
		pso->fragmentModule = nullptr;
		pso->context = nullptr;
	}
	pipelines.clear();
	currentPSO = nullptr;
	currentPipeline = nullptr;

	{
		PipelineSurfaceAllocator allocator(*this);
		pipelineSurfaces.resetAll(allocator);
	}
	for (auto& surfaceTexture : surfaceTextures)
	{
		surfaceTexture.reset();
	}
	textureBindGroups.clear();
	frameUniforms.clear();
	uniformChunks.clear();
	vertexChunks.clear();
	shaderModules.clear();
	samplers.clear();
	defaultTexture.reset();
	defaultArrayTexture.reset();
	defaultDepthTexture.reset();
	defaultDepthArrayTexture.reset();
	lightDataBuffer = nullptr;
	lightIndexBuffer = nullptr;
	zeroUniformBindGroup = nullptr;
	zeroUniformBuffer = nullptr;
	uniformLayout = nullptr;

	// Release the surface first. If the device goes first, Dawn's Vulkan
	// swapchain teardown crashes in FencedDeleter.
	if (surface && surfaceConfigured)
	{
		surface.Unconfigure();
	}
	surfaceConfigured = false;
	surface = nullptr;
	queue = nullptr;
	device = nullptr;
	adapter = nullptr;
	instance = nullptr;
}

void wgpu_context::shutdown()
{
	debug(LOG_3D, "WebGPU backend shutdown after %zu frames", frameNum);
	releaseWebGPUObjects();
	backend_impl.reset();
}

} // anonymous namespace

gfx_api::context* gfx_api::createWebGPUContext(bool useDebug)
{
	return new wgpu_context(useDebug);
}

#endif // defined(WZ_WEBGPU_ENABLED)
