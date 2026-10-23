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
/** @file pipeline_json_export.h
 * Writes the Vulkan SC pipeline JSON for every pipeline the Vulkan backend builds.
 *
 * Debug aid: WZ_VULKAN_EXPORT_PIPELINES=<directory> records the create info of each render pass,
 * descriptor set layout, pipeline layout, and pipeline, and at shutdown writes one Khronos
 * pipeline JSON document per pipeline into that directory, plus a manifest, wz2100_pipelines.json.
 * A pipeline cache compiler builds the Vulkan SC pipeline cache from the documents.
 * The directory must exist. Runs that reach different pipelines can write into one directory:
 * each document is named by its pipeline identifier, and the manifest keeps earlier entries.
 */

#pragma once

#if defined(WZ_VULKAN_ENABLED)

#include "vk/vulkan_hpp_include.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vks
{
class PipelineExporter;
}

namespace gfx_api::vk
{

class PipelineJsonExport
{
public:
	// The size of a pipeline in the Vulkan SC pipeline pool, as the vulkansc-samples framework uses.
	// Verify against each implementation before the SC build relies on it.
	static constexpr VkDeviceSize POOL_ENTRY_SIZE = 1024 * 1024;

	// Returns null unless WZ_VULKAN_EXPORT_PIPELINES names a directory.
	static std::unique_ptr<PipelineJsonExport> createFromEnvironment();

	explicit PipelineJsonExport(const std::string& directory);
	~PipelineJsonExport();

	void recordRenderPass(const VkRenderPassCreateInfo& createInfo, VkRenderPass renderPass);
	void recordDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& createInfo, VkDescriptorSetLayout setLayout);
	void recordPipelineLayout(const VkPipelineLayoutCreateInfo& createInfo, VkPipelineLayout pipelineLayout);

	// name is "<recipe>/<render pass class>/<specialization values>". shaderFiles are the SPIR-V file
	// names in stage order. The document gets a VkPipelineOfflineCreateInfo with the identifier of the
	// name, which createInfo itself must not carry on a desktop device.
	void recordGraphicsPipeline(const std::string& name, const std::string& recipe, const std::string& renderPassClass,
		const std::string& specialization, const VkGraphicsPipelineCreateInfo& createInfo, const std::vector<std::string>& shaderFiles);

	// Writes the documents and the manifest. Returns false on failure.
	bool write();

	// The identifier Vulkan SC knows the named pipeline by: the same as the documents' PipelineUUID.
	static void pipelineIdentifier(const std::string& name, uint8_t out[VK_UUID_SIZE]);

private:
	struct ManifestEntry
	{
		std::string identifier;
		std::string recipe;
		std::string renderPassClass;
		std::string specialization;
		std::vector<std::string> shaderFiles;
	};

	std::string _directory;
	std::unique_ptr<vks::PipelineExporter> _exporter;
	std::map<std::string, ManifestEntry> _manifest;
};

} // namespace gfx_api::vk

#endif // defined(WZ_VULKAN_ENABLED)
