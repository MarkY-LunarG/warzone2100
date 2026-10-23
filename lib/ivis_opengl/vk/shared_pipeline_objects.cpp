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
/** @file shared_pipeline_objects.cpp
 * Samplers, descriptor set layouts, pipeline layouts, and shader modules shared by all pipelines.
 */

#if defined(WZ_VULKAN_ENABLED)

#include "vk/shared_pipeline_objects.h"

#include "lib/framework/frame.h"

#include <type_traits>

namespace gfx_api::vk
{

namespace
{

template <typename Handle>
uint64_t handleKey(Handle handle)
{
	const auto cHandle = static_cast<typename Handle::CType>(handle);
	if constexpr (std::is_pointer_v<typename Handle::CType>)
	{
		return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cHandle));
	}
	else
	{
		return static_cast<uint64_t>(cHandle);
	}
}

} // anonymous namespace

::vk::Sampler SharedPipelineObjects::sampler(::vk::Device dev, const WZ_vk::DispatchLoaderDynamic& vkDynLoader,
	const std::vector<uint64_t>& key, const ::vk::SamplerCreateInfo& createInfo)
{
	auto it = _samplers.find(key);
	if (it == _samplers.end())
	{
		it = _samplers.emplace(key, dev.createSampler(createInfo, nullptr, vkDynLoader)).first;
	}
	return it->second;
}

::vk::DescriptorSetLayout SharedPipelineObjects::descriptorSetLayout(::vk::Device dev, const WZ_vk::DispatchLoaderDynamic& vkDynLoader,
	const std::vector<::vk::DescriptorSetLayoutBinding>& bindings)
{
	std::vector<uint64_t> key;
	key.reserve(bindings.size() * 5);
	for (const auto& binding : bindings)
	{
		key.push_back(binding.binding);
		key.push_back(static_cast<uint64_t>(binding.descriptorType));
		key.push_back(binding.descriptorCount);
		key.push_back(static_cast<uint64_t>(static_cast<VkShaderStageFlags>(binding.stageFlags)));
		uint64_t immutableSamplers = 0;
		for (uint32_t i = 0; binding.pImmutableSamplers != nullptr && i < binding.descriptorCount; ++i)
		{
			immutableSamplers = immutableSamplers * 31 + handleKey(binding.pImmutableSamplers[i]);
		}
		key.push_back(immutableSamplers);
	}

	auto it = _descriptorSetLayouts.find(key);
	if (it == _descriptorSetLayouts.end())
	{
		const auto layout = dev.createDescriptorSetLayout(
			::vk::DescriptorSetLayoutCreateInfo()
				.setBindingCount(static_cast<uint32_t>(bindings.size()))
				.setPBindings(bindings.data())
			, nullptr, vkDynLoader);
		it = _descriptorSetLayouts.emplace(std::move(key), layout).first;
	}
	return it->second;
}

::vk::PipelineLayout SharedPipelineObjects::pipelineLayout(::vk::Device dev, const WZ_vk::DispatchLoaderDynamic& vkDynLoader,
	const std::vector<::vk::DescriptorSetLayout>& setLayouts)
{
	std::vector<uint64_t> key;
	key.reserve(setLayouts.size());
	for (const auto& setLayout : setLayouts)
	{
		key.push_back(handleKey(setLayout));
	}

	auto it = _pipelineLayouts.find(key);
	if (it == _pipelineLayouts.end())
	{
		const auto layout = dev.createPipelineLayout(
			::vk::PipelineLayoutCreateInfo()
				.setPSetLayouts(setLayouts.data())
				.setSetLayoutCount(static_cast<uint32_t>(setLayouts.size()))
			, nullptr, vkDynLoader);
		it = _pipelineLayouts.emplace(std::move(key), layout).first;
	}
	return it->second;
}

::vk::ShaderModule SharedPipelineObjects::shaderModule(::vk::Device dev, const WZ_vk::DispatchLoaderDynamic& vkDynLoader,
	const std::string& name, const std::function<std::vector<uint32_t>(const std::string&)>& loadCode)
{
	auto it = _shaderModules.find(name);
	if (it == _shaderModules.end())
	{
		const auto code = loadCode(name);
		ASSERT_OR_RETURN(::vk::ShaderModule(), !code.empty(), "Failed to read shader: %s", name.c_str());
		const auto module = dev.createShaderModule(
			::vk::ShaderModuleCreateInfo()
				.setCodeSize(code.size() * sizeof(uint32_t))
				.setPCode(code.data())
			, nullptr, vkDynLoader);
		it = _shaderModules.emplace(name, module).first;
	}
	return it->second;
}

void SharedPipelineObjects::releaseShaderModules(::vk::Device dev, const WZ_vk::DispatchLoaderDynamic& vkDynLoader)
{
	for (auto& entry : _shaderModules)
	{
		dev.destroyShaderModule(entry.second, nullptr, vkDynLoader);
	}
	_shaderModules.clear();
}

void SharedPipelineObjects::destroy(::vk::Device dev, const WZ_vk::DispatchLoaderDynamic& vkDynLoader)
{
	releaseShaderModules(dev, vkDynLoader);
	for (auto& entry : _pipelineLayouts)
	{
		dev.destroyPipelineLayout(entry.second, nullptr, vkDynLoader);
	}
	_pipelineLayouts.clear();
	for (auto& entry : _descriptorSetLayouts)
	{
		dev.destroyDescriptorSetLayout(entry.second, nullptr, vkDynLoader);
	}
	_descriptorSetLayouts.clear();
	for (auto& entry : _samplers)
	{
		dev.destroySampler(entry.second, nullptr, vkDynLoader);
	}
	_samplers.clear();
}

} // namespace gfx_api::vk

#endif // defined(WZ_VULKAN_ENABLED)
