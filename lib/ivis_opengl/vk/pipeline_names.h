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
/** @file pipeline_names.h
 * Stable names for pipeline recipes and render pass compatibility classes.
 *
 * A name depends only on what the pipeline or render pass is, not on the order in which the game
 * builds them, the compiler, or the device. Vulkan SC names each precompiled pipeline with an
 * identifier, and these names are what the identifier is made from.
 */

#pragma once

#if defined(WZ_VULKAN_ENABLED)

#include "vk/vulkan_hpp_include.h"

#include "gfx_api.h"

#include <string>

namespace gfx_api::vk
{

// "<shader program>/<fixed-function state>/<hash of the uniform, texture, and vertex layout>",
// for example "Component/b2.d1.m255.o0.s0.c1.p2/7c01a2f3".
std::string pipelineRecipeName(const gfx_api::pipeline_create_info& createInfo);

// True when two create infos describe the same pipeline (every field that pipelineRecipeName() reads).
bool samePipelineRecipe(const gfx_api::pipeline_create_info& a, const gfx_api::pipeline_create_info& b);

// The attachments (format and sample count of each, in order), the resolve attachments, and a hash of
// the subpass dependencies: what Vulkan render pass compatibility compares. For example "44x1.130x1/d5e0b1c2".
std::string renderPassClassName(const ::vk::RenderPassCreateInfo& createInfo);

} // namespace gfx_api::vk

#endif // defined(WZ_VULKAN_ENABLED)
