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
/** @file shared_pipeline_objects.h
 * Samplers, descriptor set layouts, pipeline layouts, and shader modules shared by all pipelines.
 *
 * Each is created the first time a pipeline asks for it and kept until the device is destroyed,
 * so the number of these objects depends on the distinct descriptions, not on the number of
 * pipelines. Vulkan SC reserves each of these object types at device creation.
 */

#pragma once

#if defined(WZ_VULKAN_ENABLED)

#include "vk/wz_vk.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gfx_api::vk
{

class SharedPipelineObjects
{
public:
	// The key names what the sampler create info is made from; the create info is used only on the first request.
	::vk::Sampler sampler(::vk::Device dev, const WZ_vk::DispatchLoaderDynamic& vkDynLoader,
		const std::vector<uint64_t>& key, const ::vk::SamplerCreateInfo& createInfo);

	// Bindings with immutable samplers must use samplers from sampler(), so that equal bindings have equal handles.
	::vk::DescriptorSetLayout descriptorSetLayout(::vk::Device dev, const WZ_vk::DispatchLoaderDynamic& vkDynLoader,
		const std::vector<::vk::DescriptorSetLayoutBinding>& bindings);

	::vk::PipelineLayout pipelineLayout(::vk::Device dev, const WZ_vk::DispatchLoaderDynamic& vkDynLoader,
		const std::vector<::vk::DescriptorSetLayout>& setLayouts);

	// loadCode returns the SPIR-V of the named shader, or an empty vector on failure.
	::vk::ShaderModule shaderModule(::vk::Device dev, const WZ_vk::DispatchLoaderDynamic& vkDynLoader,
		const std::string& name, const std::function<std::vector<uint32_t>(const std::string&)>& loadCode);

	// Destroys the shader modules, so that the next request reads the shader again.
	// Pipelines that were created from them are not affected.
	void releaseShaderModules(::vk::Device dev, const WZ_vk::DispatchLoaderDynamic& vkDynLoader);

	// Destroys everything. Call after every pipeline that uses these objects is destroyed.
	void destroy(::vk::Device dev, const WZ_vk::DispatchLoaderDynamic& vkDynLoader);

private:
	std::map<std::vector<uint64_t>, ::vk::Sampler> _samplers;
	std::map<std::vector<uint64_t>, ::vk::DescriptorSetLayout> _descriptorSetLayouts;
	std::map<std::vector<uint64_t>, ::vk::PipelineLayout> _pipelineLayouts;
	std::map<std::string, ::vk::ShaderModule> _shaderModules;
};

} // namespace gfx_api::vk

#endif // defined(WZ_VULKAN_ENABLED)
