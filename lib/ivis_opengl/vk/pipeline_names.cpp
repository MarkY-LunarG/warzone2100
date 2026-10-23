// SPDX-License-Identifier: GPL-2.0-or-later

/*
	This file is part of Warzone 2100.
	Copyright (C) 2026  Warzone 2100 Project (https://github.com/Warzone2100)

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
/** @file pipeline_names.cpp
 * Stable names for pipeline recipes and render pass compatibility classes.
 */

#if defined(WZ_VULKAN_ENABLED)

#include "vk/pipeline_names.h"

#include "shader_catalog.h"

#include <cinttypes>
#include <cstring>

namespace gfx_api::vk
{

namespace
{

// FNV-1a, 32 bits: short enough to read in a name; a collision is caught by samePipelineRecipe().
class NameHash
{
public:
	void add(uint64_t value)
	{
		for (int i = 0; i < 8; ++i)
		{
			addByte(static_cast<uint8_t>(value >> (i * 8)));
		}
	}

	void add(const char* text)
	{
		for (const char* p = (text) ? text : ""; *p != '\0'; ++p)
		{
			addByte(static_cast<uint8_t>(*p));
		}
		addByte(0);
	}

	std::string hex() const
	{
		char buffer[9];
		snprintf(buffer, sizeof(buffer), "%08" PRIx32, _hash);
		return buffer;
	}

private:
	void addByte(uint8_t byte)
	{
		_hash ^= byte;
		_hash *= 16777619u;
	}

	uint32_t _hash = 2166136261u;
};

template <typename Enum>
uint64_t enumValue(Enum value)
{
	return static_cast<uint64_t>(value);
}

} // anonymous namespace

std::string pipelineRecipeName(const gfx_api::pipeline_create_info& createInfo)
{
	const gfx_api::state_description& state = createInfo.state_desc;
	char stateText[96];
	snprintf(stateText, sizeof(stateText), "b%" PRIu64 ".d%" PRIu64 ".m%u.o%d.s%" PRIu64 ".c%" PRIu64 ".p%" PRIu64,
		enumValue(state.blend_state), enumValue(state.depth_mode), static_cast<unsigned>(state.output_mask),
		state.offset ? 1 : 0, enumValue(state.stencil), enumValue(state.cull), enumValue(createInfo.primitive));

	// uniform_blocks holds std::type_index values, whose names depend on the compiler, so hash the block names.
	NameHash layout;
	layout.add(createInfo.uniform_block_names.size());
	for (const char* blockName : createInfo.uniform_block_names)
	{
		layout.add(blockName);
	}
	layout.add(createInfo.texture_desc.size());
	for (const auto& texture : createInfo.texture_desc)
	{
		layout.add(texture.id);
		layout.add(enumValue(texture.sampler));
		layout.add(enumValue(texture.target));
		layout.add(enumValue(texture.border));
		layout.add(enumValue(texture.stage));
	}
	layout.add(createInfo.attribute_descriptions.size());
	for (const auto& buffer : createInfo.attribute_descriptions)
	{
		layout.add(buffer.stride);
		layout.add(enumValue(buffer.rate));
		layout.add(buffer.attributes.size());
		for (const auto& attribute : buffer.attributes)
		{
			layout.add(attribute.id);
			layout.add(enumValue(attribute.type));
			layout.add(attribute.offset);
		}
	}

	return std::string(gfx_api::shader_catalog(createInfo.shader_mode).friendly_name) + "/" + stateText + "/" + layout.hex();
}

bool samePipelineRecipe(const gfx_api::pipeline_create_info& a, const gfx_api::pipeline_create_info& b)
{
	const gfx_api::state_description& sa = a.state_desc;
	const gfx_api::state_description& sb = b.state_desc;
	if (sa.blend_state != sb.blend_state || sa.depth_mode != sb.depth_mode || sa.output_mask != sb.output_mask
		|| sa.offset != sb.offset || sa.stencil != sb.stencil || sa.cull != sb.cull
		|| a.shader_mode != b.shader_mode || a.primitive != b.primitive)
	{
		return false;
	}
	if (a.uniform_block_names.size() != b.uniform_block_names.size()
		|| a.texture_desc.size() != b.texture_desc.size()
		|| a.attribute_descriptions.size() != b.attribute_descriptions.size())
	{
		return false;
	}
	for (size_t i = 0; i < a.uniform_block_names.size(); ++i)
	{
		if (strcmp(a.uniform_block_names[i], b.uniform_block_names[i]) != 0)
		{
			return false;
		}
	}
	for (size_t i = 0; i < a.texture_desc.size(); ++i)
	{
		const auto& ta = a.texture_desc[i];
		const auto& tb = b.texture_desc[i];
		if (ta.id != tb.id || ta.sampler != tb.sampler || ta.target != tb.target || ta.border != tb.border || ta.stage != tb.stage)
		{
			return false;
		}
	}
	for (size_t i = 0; i < a.attribute_descriptions.size(); ++i)
	{
		const auto& ba = a.attribute_descriptions[i];
		const auto& bb = b.attribute_descriptions[i];
		if (ba.stride != bb.stride || ba.rate != bb.rate || ba.attributes.size() != bb.attributes.size())
		{
			return false;
		}
		for (size_t j = 0; j < ba.attributes.size(); ++j)
		{
			const auto& aa = ba.attributes[j];
			const auto& ab = bb.attributes[j];
			if (aa.id != ab.id || aa.type != ab.type || aa.offset != ab.offset)
			{
				return false;
			}
		}
	}
	return true;
}

std::string renderPassClassName(const ::vk::RenderPassCreateInfo& createInfo)
{
	std::string name;
	for (uint32_t i = 0; i < createInfo.attachmentCount; ++i)
	{
		const auto& attachment = createInfo.pAttachments[i];
		if (!name.empty())
		{
			name += ".";
		}
		name += std::to_string(static_cast<uint64_t>(attachment.format)) + "x" + std::to_string(static_cast<uint64_t>(attachment.samples));
	}

	// Which attachments each subpass uses, and how, is part of compatibility too; so are the dependencies.
	NameHash rest;
	rest.add(createInfo.subpassCount);
	for (uint32_t i = 0; i < createInfo.subpassCount; ++i)
	{
		const auto& subpass = createInfo.pSubpasses[i];
		rest.add(subpass.colorAttachmentCount);
		for (uint32_t c = 0; c < subpass.colorAttachmentCount; ++c)
		{
			rest.add(subpass.pColorAttachments[c].attachment);
			rest.add((subpass.pResolveAttachments) ? subpass.pResolveAttachments[c].attachment : VK_ATTACHMENT_UNUSED);
		}
		rest.add((subpass.pDepthStencilAttachment) ? subpass.pDepthStencilAttachment->attachment : VK_ATTACHMENT_UNUSED);
		rest.add(subpass.inputAttachmentCount);
	}
	rest.add(createInfo.dependencyCount);
	for (uint32_t i = 0; i < createInfo.dependencyCount; ++i)
	{
		const auto& dependency = createInfo.pDependencies[i];
		rest.add(dependency.srcSubpass);
		rest.add(dependency.dstSubpass);
		rest.add(static_cast<uint64_t>(static_cast<VkPipelineStageFlags>(dependency.srcStageMask)));
		rest.add(static_cast<uint64_t>(static_cast<VkPipelineStageFlags>(dependency.dstStageMask)));
		rest.add(static_cast<uint64_t>(static_cast<VkAccessFlags>(dependency.srcAccessMask)));
		rest.add(static_cast<uint64_t>(static_cast<VkAccessFlags>(dependency.dstAccessMask)));
		rest.add(static_cast<uint64_t>(static_cast<VkDependencyFlags>(dependency.dependencyFlags)));
	}
	return name + "/" + rest.hex();
}

} // namespace gfx_api::vk

#endif // defined(WZ_VULKAN_ENABLED)
