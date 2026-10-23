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
/** @file vksc_bridge.cpp
 * The Vulkan SC structures and entry points that the desktop Vulkan headers do not have.
 *
 * This is the only file that includes <vulkan/vulkan_sc.h>. It must not include a header that
 * includes <vulkan/vulkan.h> or Vulkan-Hpp.
 */

#if defined(WZ_VULKANSC)

#include <vulkan/vulkan_sc.h>

#include "vk/vksc_bridge.h"
#include "lib/framework/frame.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace gfx_api::vk::sc
{

namespace
{

struct DeviceChain
{
	VkFaultCallbackInfo faultCallback;
	VkPhysicalDeviceVulkanSC10Features features;
	VkDeviceObjectReservationCreateInfo reservation;
	VkPipelineCacheCreateInfo pipelineCache;
	VkPipelinePoolSize pipelinePoolSize;
};
static_assert(sizeof(DeviceChain) <= sizeof(DeviceChainStorage::bytes), "DeviceChainStorage is too small");
static_assert(alignof(DeviceChain) <= alignof(DeviceChainStorage), "DeviceChainStorage is not aligned enough");
static_assert(sizeof(VkCommandPoolMemoryReservationCreateInfo) <= sizeof(CommandPoolChainStorage::bytes), "CommandPoolChainStorage is too small");
static_assert(sizeof(VkPipelineOfflineCreateInfo) <= sizeof(PipelineChainStorage::bytes), "PipelineChainStorage is too small");

// The create info of the pipeline cache: the device reservation and vkCreatePipelineCache must use the same one.
VkPipelineCacheCreateInfo pipelineCacheCreateInfo(const void* data, size_t size)
{
	VkPipelineCacheCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	createInfo.flags = VK_PIPELINE_CACHE_CREATE_READ_ONLY_BIT | VK_PIPELINE_CACHE_CREATE_USE_APPLICATION_STORAGE_BIT;
	createInfo.initialDataSize = size;
	createInfo.pInitialData = data;
	return createInfo;
}

const char* faultLevelName(VkFaultLevel level)
{
	switch (level)
	{
		case VK_FAULT_LEVEL_UNASSIGNED: return "unassigned";
		case VK_FAULT_LEVEL_CRITICAL: return "critical";
		case VK_FAULT_LEVEL_RECOVERABLE: return "recoverable";
		case VK_FAULT_LEVEL_WARNING: return "warning";
		default: return "unknown";
	}
}

const char* faultTypeName(VkFaultType type)
{
	switch (type)
	{
		case VK_FAULT_TYPE_INVALID: return "invalid";
		case VK_FAULT_TYPE_UNASSIGNED: return "unassigned";
		case VK_FAULT_TYPE_IMPLEMENTATION: return "implementation";
		case VK_FAULT_TYPE_SYSTEM: return "system";
		case VK_FAULT_TYPE_PHYSICAL_DEVICE: return "physical device";
		case VK_FAULT_TYPE_COMMAND_BUFFER_FULL: return "command buffer full";
		case VK_FAULT_TYPE_INVALID_API_USAGE: return "invalid API usage";
		default: return "unknown";
	}
}

void logFaults(uint32_t faultCount, const VkFaultData* pFaults, const char* when)
{
	for (uint32_t i = 0; i < faultCount; ++i)
	{
		debug(LOG_ERROR, "Vulkan SC fault (%s): level %s, type %s", when, faultLevelName(pFaults[i].faultLevel), faultTypeName(pFaults[i].faultType));
	}
}

// Vulkan SC calls this from any thread, so it only logs.
VKAPI_ATTR void VKAPI_CALL faultCallback(VkBool32 unrecordedFaults, uint32_t faultCount, const VkFaultData* pFaults)
{
	logFaults(faultCount, pFaults, "callback");
	if (unrecordedFaults)
	{
		debug(LOG_ERROR, "Vulkan SC: more faults occurred than were recorded");
	}
}

} // anonymous namespace

uint32_t apiVersion()
{
	return VKSC_API_VERSION_1_0;
}

const void* deviceCreateInfoChain(DeviceChainStorage& storage, const DeviceReservation& r, const void* next)
{
	DeviceChain* chain = new (storage.bytes) DeviceChain();
	memset(chain, 0, sizeof(DeviceChain));

	chain->features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_SC_1_0_FEATURES;
	chain->features.pNext = &chain->reservation;
	chain->features.shaderAtomicInstructions = VK_FALSE;

	VkDeviceObjectReservationCreateInfo& o = chain->reservation;
	o.sType = VK_STRUCTURE_TYPE_DEVICE_OBJECT_RESERVATION_CREATE_INFO;
	o.pNext = next;
	if (r.pipelineCacheData != nullptr)
	{
		// Read-only, and the data stays where the application keeps it, as the samples load their caches.
		chain->pipelineCache = pipelineCacheCreateInfo(r.pipelineCacheData, r.pipelineCacheSize);
		o.pipelineCacheCreateInfoCount = 1;
		o.pPipelineCacheCreateInfos = &chain->pipelineCache;

		chain->pipelinePoolSize.sType = VK_STRUCTURE_TYPE_PIPELINE_POOL_SIZE;
		chain->pipelinePoolSize.poolEntrySize = r.pipelinePoolEntrySize;
		chain->pipelinePoolSize.poolEntryCount = r.pipelinePoolEntryCount;
		o.pipelinePoolSizeCount = 1;
		o.pPipelinePoolSizes = &chain->pipelinePoolSize;
	}
	o.semaphoreRequestCount = r.semaphoreRequestCount;
	o.commandBufferRequestCount = r.commandBufferRequestCount;
	o.fenceRequestCount = r.fenceRequestCount;
	o.deviceMemoryRequestCount = r.deviceMemoryRequestCount;
	o.bufferRequestCount = r.bufferRequestCount;
	o.imageRequestCount = r.imageRequestCount;
	o.eventRequestCount = r.eventRequestCount;
	o.queryPoolRequestCount = r.queryPoolRequestCount;
	o.bufferViewRequestCount = r.bufferViewRequestCount;
	o.imageViewRequestCount = r.imageViewRequestCount;
	o.layeredImageViewRequestCount = r.layeredImageViewRequestCount;
	o.pipelineCacheRequestCount = r.pipelineCacheRequestCount;
	o.pipelineLayoutRequestCount = r.pipelineLayoutRequestCount;
	o.renderPassRequestCount = r.renderPassRequestCount;
	o.graphicsPipelineRequestCount = r.graphicsPipelineRequestCount;
	o.computePipelineRequestCount = r.computePipelineRequestCount;
	o.descriptorSetLayoutRequestCount = r.descriptorSetLayoutRequestCount;
	o.samplerRequestCount = r.samplerRequestCount;
	o.descriptorPoolRequestCount = r.descriptorPoolRequestCount;
	o.descriptorSetRequestCount = r.descriptorSetRequestCount;
	o.framebufferRequestCount = r.framebufferRequestCount;
	o.commandPoolRequestCount = r.commandPoolRequestCount;
	o.samplerYcbcrConversionRequestCount = r.samplerYcbcrConversionRequestCount;
	o.surfaceRequestCount = r.surfaceRequestCount;
	o.swapchainRequestCount = r.swapchainRequestCount;
	o.displayModeRequestCount = r.displayModeRequestCount;
	o.subpassDescriptionRequestCount = r.subpassDescriptionRequestCount;
	o.attachmentDescriptionRequestCount = r.attachmentDescriptionRequestCount;
	o.descriptorSetLayoutBindingRequestCount = r.descriptorSetLayoutBindingRequestCount;
	o.descriptorSetLayoutBindingLimit = r.descriptorSetLayoutBindingLimit;
	o.maxImageViewMipLevels = r.maxImageViewMipLevels;
	o.maxImageViewArrayLayers = r.maxImageViewArrayLayers;
	o.maxLayeredImageViewMipLevels = r.maxLayeredImageViewMipLevels;
	o.maxOcclusionQueriesPerPool = r.maxOcclusionQueriesPerPool;
	o.maxPipelineStatisticsQueriesPerPool = r.maxPipelineStatisticsQueriesPerPool;
	o.maxTimestampQueriesPerPool = r.maxTimestampQueriesPerPool;
	o.maxImmutableSamplersPerDescriptorSetLayout = r.maxImmutableSamplersPerDescriptorSetLayout;

	chain->faultCallback.sType = VK_STRUCTURE_TYPE_FAULT_CALLBACK_INFO;
	chain->faultCallback.pNext = &chain->features;
	chain->faultCallback.pfnFaultCallback = faultCallback;
	return &chain->faultCallback;
}

const void* commandPoolCreateInfoChain(CommandPoolChainStorage& storage, uint32_t maxCommandBuffers, uint64_t reservedSizePerCommandBuffer, const void* next)
{
	auto* reservation = new (storage.bytes) VkCommandPoolMemoryReservationCreateInfo();
	memset(reservation, 0, sizeof(*reservation));
	reservation->sType = VK_STRUCTURE_TYPE_COMMAND_POOL_MEMORY_RESERVATION_CREATE_INFO;
	reservation->pNext = next;
	reservation->commandPoolMaxCommandBuffers = maxCommandBuffers;
	reservation->commandPoolReservedSize = static_cast<VkDeviceSize>(maxCommandBuffers) * reservedSizePerCommandBuffer;
	return reservation;
}

uint32_t pipelineCacheEntryCount(const void* data, size_t size)
{
	VkPipelineCacheHeaderVersionSafetyCriticalOne header;
	if (data == nullptr || size < sizeof(header))
	{
		return 0;
	}
	memcpy(&header, data, sizeof(header));
	if (header.headerVersionOne.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_SAFETY_CRITICAL_ONE)
	{
		return 0;
	}
	return header.pipelineIndexCount;
}

uint64_t createPipelineCache(void* device, void* createPipelineCache, const void* data, size_t size)
{
	auto pfnCreatePipelineCache = reinterpret_cast<PFN_vkCreatePipelineCache>(createPipelineCache);
	if (device == nullptr || pfnCreatePipelineCache == nullptr)
	{
		return 0;
	}
	const VkPipelineCacheCreateInfo createInfo = pipelineCacheCreateInfo(data, size);
	VkPipelineCache cache = VK_NULL_HANDLE;
	const VkResult result = pfnCreatePipelineCache(static_cast<VkDevice>(device), &createInfo, nullptr, &cache);
	if (result != VK_SUCCESS)
	{
		debug(LOG_ERROR, "Vulkan SC: vkCreatePipelineCache failed: %d", static_cast<int>(result));
		return 0;
	}
	static_assert(sizeof(cache) <= sizeof(uint64_t), "VkPipelineCache does not fit in 64 bits");
	uint64_t value = 0;
	memcpy(&value, &cache, sizeof(cache));
	return value;
}

const void* pipelineCreateInfoChain(PipelineChainStorage& storage, const uint8_t identifier[16], uint64_t poolEntrySize, const void* next)
{
	static_assert(VK_UUID_SIZE == 16, "A pipeline identifier is 16 bytes");
	auto* offlineInfo = new (storage.bytes) VkPipelineOfflineCreateInfo();
	memset(offlineInfo, 0, sizeof(*offlineInfo));
	offlineInfo->sType = VK_STRUCTURE_TYPE_PIPELINE_OFFLINE_CREATE_INFO;
	offlineInfo->pNext = next;
	memcpy(offlineInfo->pipelineIdentifier, identifier, VK_UUID_SIZE);
	offlineInfo->matchControl = VK_PIPELINE_MATCH_CONTROL_APPLICATION_UUID_EXACT_MATCH;
	offlineInfo->poolEntrySize = poolEntrySize;
	return offlineInfo;
}

bool getProperties(void* physicalDevice, void* getPhysicalDeviceProperties2, Properties& out)
{
	auto pfnGetProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(getPhysicalDeviceProperties2);
	if (pfnGetProperties2 == nullptr)
	{
		return false;
	}
	VkPhysicalDeviceVulkanSC10Properties sc10 = {};
	sc10.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_SC_1_0_PROPERTIES;
	VkPhysicalDeviceProperties2 properties2 = {};
	properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties2.pNext = &sc10;
	pfnGetProperties2(static_cast<VkPhysicalDevice>(physicalDevice), &properties2);

	out.commandPoolResetCommandBuffer = sc10.commandPoolResetCommandBuffer == VK_TRUE;
	out.recycleDescriptorSetMemory = sc10.recycleDescriptorSetMemory == VK_TRUE;
	out.recyclePipelineMemory = sc10.recyclePipelineMemory == VK_TRUE;
	out.maxRenderPassSubpasses = sc10.maxRenderPassSubpasses;
	out.maxDescriptorSetLayoutBindings = sc10.maxDescriptorSetLayoutBindings;
	out.maxCommandPoolCommandBuffers = sc10.maxCommandPoolCommandBuffers;
	out.maxCommandBufferSize = sc10.maxCommandBufferSize;
	return true;
}

void reportFaults(void* device, void* getFaultData, const char* when)
{
	auto pfnGetFaultData = reinterpret_cast<PFN_vkGetFaultData>(getFaultData);
	if (device == nullptr || pfnGetFaultData == nullptr)
	{
		return;
	}
	VkBool32 unrecorded = VK_FALSE;
	uint32_t faultCount = 0;
	if (pfnGetFaultData(static_cast<VkDevice>(device), VK_FAULT_QUERY_BEHAVIOR_GET_AND_CLEAR_ALL_FAULTS, &unrecorded, &faultCount, nullptr) != VK_SUCCESS || faultCount == 0)
	{
		return;
	}
	VkFaultData faults[16] = {};
	for (auto& fault : faults)
	{
		fault.sType = VK_STRUCTURE_TYPE_FAULT_DATA;
	}
	faultCount = std::min<uint32_t>(faultCount, 16);
	if (pfnGetFaultData(static_cast<VkDevice>(device), VK_FAULT_QUERY_BEHAVIOR_GET_AND_CLEAR_ALL_FAULTS, &unrecorded, &faultCount, faults) == VK_SUCCESS)
	{
		logFaults(faultCount, faults, when);
	}
}

} // namespace gfx_api::vk::sc

#endif // defined(WZ_VULKANSC)
