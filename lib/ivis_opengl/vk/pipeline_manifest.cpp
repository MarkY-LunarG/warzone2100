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
/** @file pipeline_manifest.cpp
 * Pipeline recipes and render pass layouts to and from the pipeline manifest.
 */

#if defined(WZ_VULKAN_ENABLED)

#include "vk/pipeline_manifest.h"

#include "lib/framework/frame.h"
#include "shader_catalog.h"

#include <cstdlib>
#include <sstream>
#include <typeindex>

namespace gfx_api::vk
{

namespace
{

// A uniform block type the manifest cannot name: the game's recipe replaces it when it registers.
struct PlaceholderUniformBlock {};

template <typename Enum>
int64_t enumValue(Enum value)
{
	return static_cast<int64_t>(value);
}

template <typename Enum>
Enum enumFrom(const nlohmann::json& value)
{
	return static_cast<Enum>(value.get<int64_t>());
}

nlohmann::ordered_json optionalToJson(const std::optional<::vk::Format>& value)
{
	return value.has_value() ? nlohmann::ordered_json(enumValue(value.value())) : nlohmann::ordered_json();
}

nlohmann::ordered_json optionalToJson(const std::optional<::vk::ImageLayout>& value)
{
	return value.has_value() ? nlohmann::ordered_json(enumValue(value.value())) : nlohmann::ordered_json();
}

template <typename Enum>
std::optional<Enum> optionalFrom(const nlohmann::json& value)
{
	if (value.is_null())
	{
		return std::nullopt;
	}
	return enumFrom<Enum>(value);
}

template <typename Enum>
nlohmann::ordered_json enumArray(const std::vector<Enum>& values)
{
	nlohmann::ordered_json array = nlohmann::ordered_json::array();
	for (const Enum& value : values)
	{
		array.push_back(enumValue(value));
	}
	return array;
}

template <typename Enum>
std::vector<Enum> enumVector(const nlohmann::json& array)
{
	std::vector<Enum> values;
	for (const auto& value : array)
	{
		values.push_back(enumFrom<Enum>(value));
	}
	return values;
}

} // anonymous namespace

nlohmann::ordered_json recipeToJson(const gfx_api::pipeline_create_info& createInfo)
{
	const gfx_api::state_description& state = createInfo.state_desc;
	nlohmann::ordered_json recipe;
	recipe["shader"] = enumValue(createInfo.shader_mode);
	recipe["shaderName"] = gfx_api::shader_catalog(createInfo.shader_mode).friendly_name;
	recipe["blend"] = enumValue(state.blend_state);
	recipe["depth"] = enumValue(state.depth_mode);
	recipe["outputMask"] = state.output_mask;
	recipe["polygonOffset"] = state.offset;
	recipe["stencil"] = enumValue(state.stencil);
	recipe["cull"] = enumValue(state.cull);
	recipe["primitive"] = enumValue(createInfo.primitive);

	nlohmann::ordered_json uniformBlocks = nlohmann::ordered_json::array();
	for (size_t i = 0; i < createInfo.uniform_blocks.size(); ++i)
	{
		uniformBlocks.push_back({
			{"name", (i < createInfo.uniform_block_names.size()) ? createInfo.uniform_block_names[i] : ""},
			{"pointLights", createInfo.uniform_blocks[i] == std::type_index(typeid(gfx_api::PointLightsUniforms))},
		});
	}
	recipe["uniformBlocks"] = uniformBlocks;

	nlohmann::ordered_json textures = nlohmann::ordered_json::array();
	for (const auto& texture : createInfo.texture_desc)
	{
		textures.push_back({
			{"id", texture.id},
			{"sampler", enumValue(texture.sampler)},
			{"target", enumValue(texture.target)},
			{"border", enumValue(texture.border)},
			{"stage", enumValue(texture.stage)},
		});
	}
	recipe["textures"] = textures;

	nlohmann::ordered_json vertexBuffers = nlohmann::ordered_json::array();
	for (const auto& buffer : createInfo.attribute_descriptions)
	{
		nlohmann::ordered_json attributes = nlohmann::ordered_json::array();
		for (const auto& attribute : buffer.attributes)
		{
			attributes.push_back({
				{"id", attribute.id},
				{"type", enumValue(attribute.type)},
				{"offset", attribute.offset},
			});
		}
		vertexBuffers.push_back({
			{"stride", buffer.stride},
			{"rate", enumValue(buffer.rate)},
			{"attributes", attributes},
		});
	}
	recipe["vertexBuffers"] = vertexBuffers;
	return recipe;
}

std::optional<gfx_api::pipeline_create_info> recipeFromJson(const nlohmann::json& value, std::deque<std::string>& nameStorage)
{
	try
	{
		const gfx_api::state_description state(
			enumFrom<REND_MODE>(value.at("blend")),
			enumFrom<DEPTH_MODE>(value.at("depth")),
			value.at("outputMask").get<uint8_t>(),
			value.at("polygonOffset").get<bool>() ? gfx_api::polygon_offset::enabled : gfx_api::polygon_offset::disabled,
			enumFrom<gfx_api::stencil_mode>(value.at("stencil")),
			enumFrom<gfx_api::cull_mode>(value.at("cull")));

		std::vector<std::type_index> uniformBlocks;
		std::vector<const char*> uniformBlockNames;
		for (const auto& block : value.at("uniformBlocks"))
		{
			uniformBlocks.emplace_back(block.at("pointLights").get<bool>()
				? typeid(gfx_api::PointLightsUniforms) : typeid(PlaceholderUniformBlock));
			nameStorage.push_back(block.at("name").get<std::string>());
			uniformBlockNames.push_back(nameStorage.back().c_str());
		}

		std::vector<gfx_api::texture_input> textures;
		for (const auto& texture : value.at("textures"))
		{
			textures.emplace_back(texture.at("id").get<size_t>(),
				enumFrom<gfx_api::sampler_type>(texture.at("sampler")),
				enumFrom<gfx_api::pixel_format_target>(texture.at("target")),
				enumFrom<gfx_api::border_color>(texture.at("border")),
				enumFrom<gfx_api::shader_stage>(texture.at("stage")));
		}

		std::vector<gfx_api::vertex_buffer> vertexBuffers;
		for (const auto& buffer : value.at("vertexBuffers"))
		{
			std::vector<gfx_api::vertex_buffer_input> attributes;
			for (const auto& attribute : buffer.at("attributes"))
			{
				attributes.emplace_back(attribute.at("id").get<size_t>(),
					enumFrom<gfx_api::vertex_attribute_type>(attribute.at("type")),
					attribute.at("offset").get<size_t>());
			}
			vertexBuffers.emplace_back(buffer.at("stride").get<size_t>(),
				enumFrom<gfx_api::vertex_attribute_input_rate>(buffer.at("rate")), std::move(attributes));
		}

		return gfx_api::pipeline_create_info(state, enumFrom<SHADER_MODE>(value.at("shader")),
			enumFrom<gfx_api::primitive_type>(value.at("primitive")), uniformBlocks, textures, vertexBuffers, uniformBlockNames);
	}
	catch (const nlohmann::json::exception& e)
	{
		debug(LOG_ERROR, "Pipeline manifest: a recipe could not be read: %s", e.what());
		return std::nullopt;
	}
}

nlohmann::ordered_json passLayoutKeyToJson(const PassLayoutKey& key)
{
	nlohmann::ordered_json layout;
	layout["colorFormats"] = enumArray(key.colorFormats);
	layout["colorLoadOps"] = enumArray(key.colorLoadOps);
	layout["colorStoreOps"] = enumArray(key.colorStoreOps);
	layout["colorSamples"] = enumArray(key.colorSamples);
	layout["colorInitialLayouts"] = enumArray(key.colorInitialLayouts);
	layout["colorFinalLayouts"] = enumArray(key.colorFinalLayouts);
	layout["resolveFormat"] = optionalToJson(key.resolveFormat);
	layout["resolveLoadOp"] = enumValue(key.resolveLoadOp);
	layout["resolveStoreOp"] = enumValue(key.resolveStoreOp);
	layout["resolveInitialLayout"] = optionalToJson(key.resolveInitialLayout);
	layout["resolveFinalLayout"] = optionalToJson(key.resolveFinalLayout);
	layout["depthFormat"] = optionalToJson(key.depthFormat);
	layout["depthLoadOp"] = enumValue(key.depthLoadOp);
	layout["depthStoreOp"] = enumValue(key.depthStoreOp);
	layout["depthInitialLayout"] = optionalToJson(key.depthInitialLayout);
	layout["depthFinalLayout"] = enumValue(key.depthFinalLayout);
	return layout;
}

std::optional<PassLayoutKey> passLayoutKeyFromJson(const nlohmann::json& value)
{
	try
	{
		PassLayoutKey key;
		key.colorFormats = enumVector<::vk::Format>(value.at("colorFormats"));
		key.colorLoadOps = enumVector<gfx_api::AttachmentLoadOp>(value.at("colorLoadOps"));
		key.colorStoreOps = enumVector<gfx_api::AttachmentStoreOp>(value.at("colorStoreOps"));
		key.colorSamples = enumVector<::vk::SampleCountFlagBits>(value.at("colorSamples"));
		key.colorInitialLayouts = enumVector<::vk::ImageLayout>(value.at("colorInitialLayouts"));
		key.colorFinalLayouts = enumVector<::vk::ImageLayout>(value.at("colorFinalLayouts"));
		key.resolveFormat = optionalFrom<::vk::Format>(value.at("resolveFormat"));
		key.resolveLoadOp = enumFrom<gfx_api::AttachmentLoadOp>(value.at("resolveLoadOp"));
		key.resolveStoreOp = enumFrom<gfx_api::AttachmentStoreOp>(value.at("resolveStoreOp"));
		key.resolveInitialLayout = optionalFrom<::vk::ImageLayout>(value.at("resolveInitialLayout"));
		key.resolveFinalLayout = optionalFrom<::vk::ImageLayout>(value.at("resolveFinalLayout"));
		key.depthFormat = optionalFrom<::vk::Format>(value.at("depthFormat"));
		key.depthLoadOp = enumFrom<gfx_api::AttachmentLoadOp>(value.at("depthLoadOp"));
		key.depthStoreOp = enumFrom<gfx_api::AttachmentStoreOp>(value.at("depthStoreOp"));
		key.depthInitialLayout = optionalFrom<::vk::ImageLayout>(value.at("depthInitialLayout"));
		key.depthFinalLayout = enumFrom<::vk::ImageLayout>(value.at("depthFinalLayout"));
		return key;
	}
	catch (const nlohmann::json::exception& e)
	{
		debug(LOG_ERROR, "Pipeline manifest: a render pass layout could not be read: %s", e.what());
		return std::nullopt;
	}
}

bool parseSpecialization(const std::string& specialization, gfx_api::lighting_constants& constants)
{
	if (specialization == "-")
	{
		return true;
	}
	std::istringstream values(specialization);
	std::string value;
	while (std::getline(values, value, '.'))
	{
		if (value.size() < 2)
		{
			return false;
		}
		char* end = nullptr;
		const unsigned long number = strtoul(value.c_str() + 1, &end, 10);
		if (end == value.c_str() + 1 || *end != '\0')
		{
			return false;
		}
		switch (value[0])
		{
			case 'm': constants.shadowMode = static_cast<uint32_t>(number); break;
			case 'f': constants.shadowFilterSize = static_cast<uint32_t>(number); break;
			case 'c': constants.shadowCascadesCount = static_cast<uint32_t>(number); break;
			case 'l': constants.isPointLightPerPixelEnabled = (number != 0); break;
			default: return false;
		}
	}
	return true;
}

} // namespace gfx_api::vk

#endif // defined(WZ_VULKAN_ENABLED)
