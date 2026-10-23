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
/** @file vksc_bridge.h
 * The Vulkan SC structures and entry points that the desktop Vulkan headers do not have.
 *
 * The Vulkan SC build compiles the Vulkan backend against the desktop headers (Vulkan-Hpp), and
 * loads the entry points from the Vulkan SC loader. Every structure the two APIs share has the same
 * layout. The Vulkan SC-only structures are filled in vksc_bridge.cpp, which includes only
 * <vulkan/vulkan_sc.h>; <vulkan/vulkan.h> and <vulkan/vulkan_sc.h> cannot share a translation unit.
 * So this header uses no Vulkan type: handles and structure chains cross it as void pointers.
 */

#pragma once

#if defined(WZ_VULKANSC)

#include <cstddef>
#include <cstdint>

namespace gfx_api::vk::sc
{

// VKSC_API_VERSION_1_0: the application and instance API version.
uint32_t apiVersion();

// The counts of VkDeviceObjectReservationCreateInfo, with the same names.
struct DeviceReservation
{
	uint32_t semaphoreRequestCount = 0;
	uint32_t commandBufferRequestCount = 0;
	uint32_t fenceRequestCount = 0;
	uint32_t deviceMemoryRequestCount = 0;
	uint32_t bufferRequestCount = 0;
	uint32_t imageRequestCount = 0;
	uint32_t eventRequestCount = 0;
	uint32_t queryPoolRequestCount = 0;
	uint32_t bufferViewRequestCount = 0;
	uint32_t imageViewRequestCount = 0;
	uint32_t layeredImageViewRequestCount = 0;
	uint32_t pipelineCacheRequestCount = 0;
	uint32_t pipelineLayoutRequestCount = 0;
	uint32_t renderPassRequestCount = 0;
	uint32_t graphicsPipelineRequestCount = 0;
	uint32_t computePipelineRequestCount = 0;
	uint32_t descriptorSetLayoutRequestCount = 0;
	uint32_t samplerRequestCount = 0;
	uint32_t descriptorPoolRequestCount = 0;
	uint32_t descriptorSetRequestCount = 0;
	uint32_t framebufferRequestCount = 0;
	uint32_t commandPoolRequestCount = 0;
	uint32_t samplerYcbcrConversionRequestCount = 0;
	uint32_t surfaceRequestCount = 0;
	uint32_t swapchainRequestCount = 0;
	uint32_t displayModeRequestCount = 0;
	uint32_t subpassDescriptionRequestCount = 0;
	uint32_t attachmentDescriptionRequestCount = 0;
	uint32_t descriptorSetLayoutBindingRequestCount = 0;
	uint32_t descriptorSetLayoutBindingLimit = 0;
	uint32_t maxImageViewMipLevels = 0;
	uint32_t maxImageViewArrayLayers = 0;
	uint32_t maxLayeredImageViewMipLevels = 0;
	uint32_t maxOcclusionQueriesPerPool = 0;
	uint32_t maxPipelineStatisticsQueriesPerPool = 0;
	uint32_t maxTimestampQueriesPerPool = 0;
	uint32_t maxImmutableSamplersPerDescriptorSetLayout = 0;

	// The pipeline cache: none when pipelineCacheData is null (no pipeline can then be created).
	const void* pipelineCacheData = nullptr;
	size_t pipelineCacheSize = 0;
	uint32_t pipelinePoolEntryCount = 0;
	uint64_t pipelinePoolEntrySize = 0;
};

// The Vulkan SC properties that the backend checks.
struct Properties
{
	bool commandPoolResetCommandBuffer = false;
	bool recycleDescriptorSetMemory = false;
	bool recyclePipelineMemory = false;
	uint32_t maxRenderPassSubpasses = 0;
	uint32_t maxDescriptorSetLayoutBindings = 0;
	uint32_t maxCommandPoolCommandBuffers = 0;
	uint64_t maxCommandBufferSize = 0;
};

// Memory for the structures the bridge chains. It must live as long as the create info that uses it.
struct DeviceChainStorage
{
	alignas(16) unsigned char bytes[768];
};
struct CommandPoolChainStorage
{
	alignas(16) unsigned char bytes[64];
};
struct PipelineChainStorage
{
	alignas(16) unsigned char bytes[64];
};

// Returns the head of: VkFaultCallbackInfo -> VkPhysicalDeviceVulkanSC10Features ->
// VkDeviceObjectReservationCreateInfo (with the pipeline cache and pool size, if any) -> next.
// The fault callback writes each fault to the log.
const void* deviceCreateInfoChain(DeviceChainStorage& storage, const DeviceReservation& reservation, const void* next);

// Returns a VkCommandPoolMemoryReservationCreateInfo -> next. Vulkan SC requires it on every command pool.
const void* commandPoolCreateInfoChain(CommandPoolChainStorage& storage, uint32_t maxCommandBuffers, uint64_t reservedSizePerCommandBuffer, const void* next);

// The number of pipelines in a Vulkan SC pipeline cache (VkPipelineCacheHeaderVersionSafetyCriticalOne),
// or 0 if data is not one.
uint32_t pipelineCacheEntryCount(const void* data, size_t size);

// Creates the VkPipelineCache with the same create info as the device reservation (read-only, the data
// in application storage). createPipelineCache is a PFN_vkCreatePipelineCache. Returns the handle's
// value, or 0 on failure.
uint64_t createPipelineCache(void* device, void* createPipelineCache, const void* data, size_t size);

// Returns a VkPipelineOfflineCreateInfo -> next, which names the pipeline in the pipeline cache.
const void* pipelineCreateInfoChain(PipelineChainStorage& storage, const uint8_t identifier[16], uint64_t poolEntrySize, const void* next);

// Reads VkPhysicalDeviceVulkanSC10Properties. getPhysicalDeviceProperties2 is a PFN_vkGetPhysicalDeviceProperties2.
bool getProperties(void* physicalDevice, void* getPhysicalDeviceProperties2, Properties& out);

// Writes the faults the device recorded to the log, and clears them. getFaultData is a PFN_vkGetFaultData.
void reportFaults(void* device, void* getFaultData, const char* when);

} // namespace gfx_api::vk::sc

#endif // defined(WZ_VULKANSC)
