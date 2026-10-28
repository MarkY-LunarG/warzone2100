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
/** @file pipeline_manifest.h
 * The data the pipeline manifest (wz2100_pipelines.json) keeps so that the Vulkan SC build can create
 * every pipeline at startup: each pipeline recipe (gfx_api::pipeline_create_info), and a render pass
 * layout of each render pass compatibility class. The desktop build writes them when it exports the
 * pipeline JSON; the Vulkan SC build reads them back.
 *
 * Enumerations are stored as their values: the manifest and the binary come from the same source, and
 * a recipe read back is checked against its name (pipelineRecipeName()), which hashes the values.
 */

#pragma once

#if defined(WZ_VULKAN_ENABLED)

#include "gfx_api.h"
#include "vk/pass_layout_key.h"

#include <nlohmann/json.hpp>

#include <deque>
#include <optional>
#include <string>

namespace gfx_api::vk
{

nlohmann::ordered_json recipeToJson(const gfx_api::pipeline_create_info& createInfo);

// Uniform blocks come back as their count, with a placeholder type, except the point lights block, which
// a pipeline layout depends on (VkPSO::hasLightDataBindings). The game's recipe gives the real types when
// it registers. The block names are kept in nameStorage, which must outlive the result.
std::optional<gfx_api::pipeline_create_info> recipeFromJson(const nlohmann::json& value, std::deque<std::string>& nameStorage);

nlohmann::ordered_json passLayoutKeyToJson(const PassLayoutKey& key);
std::optional<PassLayoutKey> passLayoutKeyFromJson(const nlohmann::json& value);

// "m1.f5.c3.l0" (as VkPSO names them; "-" for none): sets the values it names in constants.
bool parseSpecialization(const std::string& specialization, gfx_api::lighting_constants& constants);

} // namespace gfx_api::vk

#endif // defined(WZ_VULKAN_ENABLED)
