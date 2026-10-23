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
/** @file pipeline_json_export.cpp
 * Writes the Vulkan SC pipeline JSON for every pipeline the Vulkan backend builds.
 */

#if defined(WZ_VULKAN_ENABLED)

#include "vk/pipeline_json_export.h"

#include "lib/framework/frame.h"
#include "3rdparty/vulkansc_samples/VulkanSCPipelineExport.h"
#include "3rdparty/vulkansc_samples/VulkanSCPipelineIdentifier.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>

namespace gfx_api::vk
{

namespace
{

// The first part of every pipeline name: the "sample" name of the vulkansc-samples exporter.
const char* const kApplicationName = "wz2100";
const char* const kManifestFileName = "wz2100_pipelines.json";

std::string hexString(const uint8_t* bytes, size_t count)
{
	static const char hexDigits[] = "0123456789abcdef";
	std::string text;
	for (size_t i = 0; i < count; ++i)
	{
		text += hexDigits[bytes[i] >> 4];
		text += hexDigits[bytes[i] & 0xF];
	}
	return text;
}

} // anonymous namespace

std::unique_ptr<PipelineJsonExport> PipelineJsonExport::createFromEnvironment()
{
	const char* directory = getenv("WZ_VULKAN_EXPORT_PIPELINES");
	if (directory == nullptr || directory[0] == '\0')
	{
		return nullptr;
	}
	debug(LOG_INFO, "Exporting Vulkan SC pipeline JSON to: %s", directory);
	return std::make_unique<PipelineJsonExport>(directory);
}

PipelineJsonExport::PipelineJsonExport(const std::string& directory)
	: _directory(directory)
	, _exporter(std::make_unique<vks::PipelineExporter>())
{
	_exporter->setSampleName(kApplicationName);
	_exporter->setOutputDirectory(directory);
}

PipelineJsonExport::~PipelineJsonExport() = default;

void PipelineJsonExport::recordRenderPass(const VkRenderPassCreateInfo& createInfo, VkRenderPass renderPass)
{
	_exporter->recordRenderPass(createInfo, renderPass);
}

void PipelineJsonExport::recordDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& createInfo, VkDescriptorSetLayout setLayout)
{
	_exporter->recordDescriptorSetLayout(createInfo, setLayout);
}

void PipelineJsonExport::recordPipelineLayout(const VkPipelineLayoutCreateInfo& createInfo, VkPipelineLayout pipelineLayout)
{
	_exporter->recordPipelineLayout(createInfo, pipelineLayout);
}

void PipelineJsonExport::pipelineIdentifier(const std::string& name, uint8_t out[VK_UUID_SIZE])
{
	static_assert(VK_UUID_SIZE == 16, "The pipeline identifier is 16 bytes");
	vks::computePipelineIdentifier(std::string(kApplicationName) + "/" + name, out);
}

void PipelineJsonExport::recordGraphicsPipeline(const std::string& name, const std::string& recipe, const std::string& renderPassClass,
	const std::string& specialization, const VkGraphicsPipelineCreateInfo& createInfo, const std::vector<std::string>& shaderFiles)
{
	VkPipelineOfflineCreateInfo offlineInfo = {};
	offlineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_OFFLINE_CREATE_INFO;
	offlineInfo.pNext = createInfo.pNext;
	pipelineIdentifier(name, offlineInfo.pipelineIdentifier);
	offlineInfo.matchControl = VK_PIPELINE_MATCH_CONTROL_APPLICATION_UUID_EXACT_MATCH;
	offlineInfo.poolEntrySize = POOL_ENTRY_SIZE;

	VkGraphicsPipelineCreateInfo exportedInfo = createInfo;
	exportedInfo.pNext = &offlineInfo;
	_exporter->recordGraphicsPipeline(name, exportedInfo, shaderFiles);

	ManifestEntry& entry = _manifest[name];
	entry.identifier = hexString(offlineInfo.pipelineIdentifier, VK_UUID_SIZE);
	entry.recipe = recipe;
	entry.renderPassClass = renderPassClass;
	entry.specialization = specialization;
	entry.shaderFiles = shaderFiles;
}

bool PipelineJsonExport::write()
{
	if (_manifest.empty())
	{
		debug(LOG_ERROR, "Pipeline JSON export: no pipelines were built");
		return false;
	}
	if (!_exporter->write())
	{
		debug(LOG_ERROR, "Pipeline JSON export: writing the documents to %s failed (see the output above)", _directory.c_str());
		return false;
	}

	// Keep the entries of earlier runs: each run reaches only the pipelines its scenes draw.
	const std::string manifestPath = _directory + "/" + kManifestFileName;
	nlohmann::ordered_json pipelines = nlohmann::ordered_json::object();
	{
		std::ifstream existing(manifestPath);
		if (existing.is_open())
		{
			const auto parsed = nlohmann::ordered_json::parse(existing, nullptr, false);
			if (parsed.is_object() && parsed.contains("pipelines") && parsed["pipelines"].is_object())
			{
				pipelines = parsed["pipelines"];
			}
			else
			{
				debug(LOG_ERROR, "Pipeline JSON export: %s is not a manifest; it is replaced", manifestPath.c_str());
			}
		}
	}
	size_t added = 0;
	for (const auto& [name, entry] : _manifest)
	{
		added += pipelines.contains(name) ? 0 : 1;
		pipelines[name] = {
			{"identifier", entry.identifier},
			{"document", std::string(kApplicationName) + ".pc." + entry.identifier + ".json"},
			{"recipe", entry.recipe},
			{"renderPassClass", entry.renderPassClass},
			{"specialization", entry.specialization},
			{"shaderFiles", entry.shaderFiles},
		};
	}

	// Sorted by name, so that the file does not depend on the order the runs built the pipelines in.
	nlohmann::json sortedPipelines = pipelines;
	nlohmann::ordered_json manifest;
	manifest["application"] = kApplicationName;
	manifest["poolEntrySize"] = POOL_ENTRY_SIZE;
	manifest["pipelines"] = sortedPipelines;

	std::ofstream file(manifestPath);
	if (!file.is_open())
	{
		debug(LOG_ERROR, "Pipeline JSON export: could not write %s", manifestPath.c_str());
		return false;
	}
	file << manifest.dump(4) << "\n";
	debug(LOG_INFO, "Pipeline JSON export: %zu pipelines in this run (%zu new), %zu in %s",
		_manifest.size(), added, sortedPipelines.size(), manifestPath.c_str());
	return true;
}

} // namespace gfx_api::vk

#endif // defined(WZ_VULKAN_ENABLED)
