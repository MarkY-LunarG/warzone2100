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
/** @file object_counters.cpp
 * Live and peak counts of the Vulkan objects that the backend creates.
 */

#if defined(WZ_VULKAN_ENABLED)

#include "vk/object_counters.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdint>
#include <type_traits>
#include <unordered_map>

// All Vulkan calls of the backend are on the main thread, so the counters need no lock.

namespace gfx_api::vk
{

namespace
{

// Each kind names the Vulkan SC reservation field that its peak is the measurement for.
enum class CountedKind : size_t
{
	Buffer,
	Image,
	ImageView,
	LayeredImageView,
	Sampler,
	ShaderModule,
	DescriptorSetLayout,
	DescriptorSetLayoutBinding,
	PipelineLayout,
	GraphicsPipeline,
	ComputePipeline,
	PipelineCache,
	RenderPass,
	SubpassDescription,
	AttachmentDescription,
	Framebuffer,
	DescriptorPool,
	DescriptorSet,
	CommandPool,
	CommandBuffer,
	Fence,
	Semaphore,
	QueryPool,
	DeviceMemory,
	Swapchain,
	Count
};

struct KindInfo
{
	const char* name;
	const char* reservationField;
};

const std::array<KindInfo, static_cast<size_t>(CountedKind::Count)> kKindInfo = {{
	{"Buffer", "bufferRequestCount"},
	{"Image", "imageRequestCount"},
	{"ImageView", "imageViewRequestCount"},
	{"LayeredImageView", "layeredImageViewRequestCount"},
	{"Sampler", "samplerRequestCount"},
	{"ShaderModule", "(none: not in Vulkan SC)"},
	{"DescriptorSetLayout", "descriptorSetLayoutRequestCount"},
	{"DescriptorSetLayoutBinding", "descriptorSetLayoutBindingRequestCount"},
	{"PipelineLayout", "pipelineLayoutRequestCount"},
	{"GraphicsPipeline", "graphicsPipelineRequestCount"},
	{"ComputePipeline", "computePipelineRequestCount"},
	{"PipelineCache", "pipelineCacheRequestCount"},
	{"RenderPass", "renderPassRequestCount"},
	{"SubpassDescription", "subpassDescriptionRequestCount"},
	{"AttachmentDescription", "attachmentDescriptionRequestCount"},
	{"Framebuffer", "framebufferRequestCount"},
	{"DescriptorPool", "descriptorPoolRequestCount"},
	{"DescriptorSet", "descriptorSetRequestCount"},
	{"CommandPool", "commandPoolRequestCount"},
	{"CommandBuffer", "commandBufferRequestCount"},
	{"Fence", "fenceRequestCount"},
	{"Semaphore", "semaphoreRequestCount"},
	{"QueryPool", "queryPoolRequestCount"},
	{"DeviceMemory", "deviceMemoryRequestCount"},
	{"Swapchain", "swapchainRequestCount"},
}};

struct KindCount
{
	uint64_t live = 0;
	uint64_t peak = 0;
	uint64_t created = 0;
};

// The largest values that Vulkan SC reserves as limits rather than as counts.
struct Limits
{
	uint32_t maxImageViewMipLevels = 0;
	uint32_t maxImageViewArrayLayers = 0;
	uint32_t maxLayeredImageViewMipLevels = 0;
	uint32_t descriptorSetLayoutBindingLimit = 0;
	uint32_t maxTimestampQueriesPerPool = 0;
	uint32_t maxDescriptorSetsPerPool = 0;
	uint32_t maxCommandBuffersPerPool = 0;
	VkDeviceSize liveMemoryBytes = 0;
	VkDeviceSize peakMemoryBytes = 0;
	VkDeviceSize largestAllocationBytes = 0;
};

struct ImageInfo
{
	uint32_t mipLevels = 1;
	uint32_t arrayLayers = 1;
};

struct State
{
	std::array<KindCount, static_cast<size_t>(CountedKind::Count)> counts {};
	Limits limits;
	std::unordered_map<uint64_t, ImageInfo> images;
	std::unordered_map<uint64_t, bool> imageViewIsLayered;
	std::unordered_map<uint64_t, uint32_t> setLayoutBindings;
	std::unordered_map<uint64_t, CountedKind> pipelineKinds;
	std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> renderPassSubpassesAndAttachments;
	std::unordered_map<uint64_t, uint32_t> descriptorSetsPerPool;
	std::unordered_map<uint64_t, uint32_t> commandBuffersPerPool;
	std::unordered_map<uint64_t, VkDeviceSize> memorySizes;
};

State g_state;

template <typename Handle>
uint64_t handleKey(Handle handle)
{
	if constexpr (std::is_pointer_v<Handle>)
	{
		return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
	}
	else
	{
		return static_cast<uint64_t>(handle);
	}
}

KindCount& countOf(CountedKind kind)
{
	return g_state.counts[static_cast<size_t>(kind)];
}

void onCreate(CountedKind kind, uint64_t count = 1)
{
	KindCount& c = countOf(kind);
	c.live += count;
	c.created += count;
	c.peak = std::max(c.peak, c.live);
}

void onDestroy(CountedKind kind, uint64_t count = 1)
{
	KindCount& c = countOf(kind);
	ASSERT(c.live >= count, "Vulkan object counter for %s went below zero", kKindInfo[static_cast<size_t>(kind)].name);
	c.live -= std::min(c.live, count);
}

// Objects that need nothing but a count: one create entry point and one destroy entry point,
// named vkCreate<Type> and vkDestroy<Type>, with a Vk<Type>CreateInfo.
#define WZ_COUNTED_SIMPLE_OBJECT(Type, Kind) \
	PFN_vkCreate##Type real_vkCreate##Type = nullptr; \
	PFN_vkDestroy##Type real_vkDestroy##Type = nullptr; \
	VKAPI_ATTR VkResult VKAPI_CALL counted_vkCreate##Type(VkDevice device, const Vk##Type##CreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, Vk##Type* pHandle) \
	{ \
		const VkResult result = real_vkCreate##Type(device, pCreateInfo, pAllocator, pHandle); \
		if (result == VK_SUCCESS) \
		{ \
			onCreate(CountedKind::Kind); \
		} \
		return result; \
	} \
	VKAPI_ATTR void VKAPI_CALL counted_vkDestroy##Type(VkDevice device, Vk##Type handle, const VkAllocationCallbacks* pAllocator) \
	{ \
		if (handle != VK_NULL_HANDLE) \
		{ \
			onDestroy(CountedKind::Kind); \
		} \
		real_vkDestroy##Type(device, handle, pAllocator); \
	}

WZ_COUNTED_SIMPLE_OBJECT(Buffer, Buffer)
WZ_COUNTED_SIMPLE_OBJECT(Sampler, Sampler)
WZ_COUNTED_SIMPLE_OBJECT(ShaderModule, ShaderModule)
WZ_COUNTED_SIMPLE_OBJECT(PipelineLayout, PipelineLayout)
WZ_COUNTED_SIMPLE_OBJECT(PipelineCache, PipelineCache)
WZ_COUNTED_SIMPLE_OBJECT(Framebuffer, Framebuffer)
WZ_COUNTED_SIMPLE_OBJECT(Fence, Fence)
WZ_COUNTED_SIMPLE_OBJECT(Semaphore, Semaphore)

#undef WZ_COUNTED_SIMPLE_OBJECT

// Images: the view counters need each image's mip and layer counts to resolve VK_REMAINING_*.

PFN_vkCreateImage real_vkCreateImage = nullptr;
PFN_vkDestroyImage real_vkDestroyImage = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL counted_vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage)
{
	const VkResult result = real_vkCreateImage(device, pCreateInfo, pAllocator, pImage);
	if (result == VK_SUCCESS)
	{
		onCreate(CountedKind::Image);
		g_state.images[handleKey(*pImage)] = ImageInfo{pCreateInfo->mipLevels, pCreateInfo->arrayLayers};
	}
	return result;
}

VKAPI_ATTR void VKAPI_CALL counted_vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
{
	if (image != VK_NULL_HANDLE)
	{
		onDestroy(CountedKind::Image);
		g_state.images.erase(handleKey(image));
	}
	real_vkDestroyImage(device, image, pAllocator);
}

PFN_vkCreateImageView real_vkCreateImageView = nullptr;
PFN_vkDestroyImageView real_vkDestroyImageView = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL counted_vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView)
{
	const VkResult result = real_vkCreateImageView(device, pCreateInfo, pAllocator, pView);
	if (result == VK_SUCCESS)
	{
		// Swapchain images are not created through vkCreateImage: they have one mip level and one layer.
		ImageInfo image;
		const auto it = g_state.images.find(handleKey(pCreateInfo->image));
		if (it != g_state.images.end())
		{
			image = it->second;
		}
		const VkImageSubresourceRange& range = pCreateInfo->subresourceRange;
		const uint32_t levels = (range.levelCount == VK_REMAINING_MIP_LEVELS) ? (image.mipLevels - range.baseMipLevel) : range.levelCount;
		const uint32_t layers = (range.layerCount == VK_REMAINING_ARRAY_LAYERS) ? (image.arrayLayers - range.baseArrayLayer) : range.layerCount;
		const bool layered = layers > 1;

		onCreate(CountedKind::ImageView);
		Limits& limits = g_state.limits;
		limits.maxImageViewMipLevels = std::max(limits.maxImageViewMipLevels, levels);
		limits.maxImageViewArrayLayers = std::max(limits.maxImageViewArrayLayers, layers);
		if (layered)
		{
			onCreate(CountedKind::LayeredImageView);
			limits.maxLayeredImageViewMipLevels = std::max(limits.maxLayeredImageViewMipLevels, levels);
		}
		g_state.imageViewIsLayered[handleKey(*pView)] = layered;
	}
	return result;
}

VKAPI_ATTR void VKAPI_CALL counted_vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator)
{
	if (imageView != VK_NULL_HANDLE)
	{
		onDestroy(CountedKind::ImageView);
		const auto it = g_state.imageViewIsLayered.find(handleKey(imageView));
		if (it != g_state.imageViewIsLayered.end())
		{
			if (it->second)
			{
				onDestroy(CountedKind::LayeredImageView);
			}
			g_state.imageViewIsLayered.erase(it);
		}
	}
	real_vkDestroyImageView(device, imageView, pAllocator);
}

PFN_vkCreateDescriptorSetLayout real_vkCreateDescriptorSetLayout = nullptr;
PFN_vkDestroyDescriptorSetLayout real_vkDestroyDescriptorSetLayout = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL counted_vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout)
{
	const VkResult result = real_vkCreateDescriptorSetLayout(device, pCreateInfo, pAllocator, pSetLayout);
	if (result == VK_SUCCESS)
	{
		onCreate(CountedKind::DescriptorSetLayout);
		onCreate(CountedKind::DescriptorSetLayoutBinding, pCreateInfo->bindingCount);
		for (uint32_t i = 0; i < pCreateInfo->bindingCount; ++i)
		{
			g_state.limits.descriptorSetLayoutBindingLimit = std::max(g_state.limits.descriptorSetLayoutBindingLimit, pCreateInfo->pBindings[i].binding + 1);
		}
		g_state.setLayoutBindings[handleKey(*pSetLayout)] = pCreateInfo->bindingCount;
	}
	return result;
}

VKAPI_ATTR void VKAPI_CALL counted_vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator)
{
	if (descriptorSetLayout != VK_NULL_HANDLE)
	{
		onDestroy(CountedKind::DescriptorSetLayout);
		const auto it = g_state.setLayoutBindings.find(handleKey(descriptorSetLayout));
		if (it != g_state.setLayoutBindings.end())
		{
			onDestroy(CountedKind::DescriptorSetLayoutBinding, it->second);
			g_state.setLayoutBindings.erase(it);
		}
	}
	real_vkDestroyDescriptorSetLayout(device, descriptorSetLayout, pAllocator);
}

// Pipelines: one destroy entry point for both kinds, so remember the kind of each.

void onPipelinesCreated(CountedKind kind, uint32_t count, const VkPipeline* pPipelines)
{
	for (uint32_t i = 0; i < count; ++i)
	{
		if (pPipelines[i] != VK_NULL_HANDLE)
		{
			onCreate(kind);
			g_state.pipelineKinds[handleKey(pPipelines[i])] = kind;
		}
	}
}

PFN_vkCreateGraphicsPipelines real_vkCreateGraphicsPipelines = nullptr;
PFN_vkCreateComputePipelines real_vkCreateComputePipelines = nullptr;
PFN_vkDestroyPipeline real_vkDestroyPipeline = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL counted_vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
{
	const VkResult result = real_vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
	// On a failure, the pipelines that were created are still returned, and the others are null.
	onPipelinesCreated(CountedKind::GraphicsPipeline, createInfoCount, pPipelines);
	return result;
}

VKAPI_ATTR VkResult VKAPI_CALL counted_vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
{
	const VkResult result = real_vkCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
	onPipelinesCreated(CountedKind::ComputePipeline, createInfoCount, pPipelines);
	return result;
}

VKAPI_ATTR void VKAPI_CALL counted_vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator)
{
	if (pipeline != VK_NULL_HANDLE)
	{
		const auto it = g_state.pipelineKinds.find(handleKey(pipeline));
		if (it != g_state.pipelineKinds.end())
		{
			onDestroy(it->second);
			g_state.pipelineKinds.erase(it);
		}
	}
	real_vkDestroyPipeline(device, pipeline, pAllocator);
}

PFN_vkCreateRenderPass real_vkCreateRenderPass = nullptr;
PFN_vkDestroyRenderPass real_vkDestroyRenderPass = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL counted_vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass)
{
	const VkResult result = real_vkCreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
	if (result == VK_SUCCESS)
	{
		onCreate(CountedKind::RenderPass);
		onCreate(CountedKind::SubpassDescription, pCreateInfo->subpassCount);
		onCreate(CountedKind::AttachmentDescription, pCreateInfo->attachmentCount);
		g_state.renderPassSubpassesAndAttachments[handleKey(*pRenderPass)] = {pCreateInfo->subpassCount, pCreateInfo->attachmentCount};
	}
	return result;
}

VKAPI_ATTR void VKAPI_CALL counted_vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator)
{
	if (renderPass != VK_NULL_HANDLE)
	{
		onDestroy(CountedKind::RenderPass);
		const auto it = g_state.renderPassSubpassesAndAttachments.find(handleKey(renderPass));
		if (it != g_state.renderPassSubpassesAndAttachments.end())
		{
			onDestroy(CountedKind::SubpassDescription, it->second.first);
			onDestroy(CountedKind::AttachmentDescription, it->second.second);
			g_state.renderPassSubpassesAndAttachments.erase(it);
		}
	}
	real_vkDestroyRenderPass(device, renderPass, pAllocator);
}

// Descriptor sets are freed by a pool reset or destroy as well as by vkFreeDescriptorSets,
// so count them per pool.

PFN_vkCreateDescriptorPool real_vkCreateDescriptorPool = nullptr;
PFN_vkDestroyDescriptorPool real_vkDestroyDescriptorPool = nullptr;
PFN_vkResetDescriptorPool real_vkResetDescriptorPool = nullptr;
PFN_vkAllocateDescriptorSets real_vkAllocateDescriptorSets = nullptr;
PFN_vkFreeDescriptorSets real_vkFreeDescriptorSets = nullptr;

void releaseDescriptorSetsOfPool(VkDescriptorPool pool, bool erasePool)
{
	const auto it = g_state.descriptorSetsPerPool.find(handleKey(pool));
	if (it == g_state.descriptorSetsPerPool.end())
	{
		return;
	}
	onDestroy(CountedKind::DescriptorSet, it->second);
	if (erasePool)
	{
		g_state.descriptorSetsPerPool.erase(it);
	}
	else
	{
		it->second = 0;
	}
}

VKAPI_ATTR VkResult VKAPI_CALL counted_vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool)
{
	const VkResult result = real_vkCreateDescriptorPool(device, pCreateInfo, pAllocator, pDescriptorPool);
	if (result == VK_SUCCESS)
	{
		onCreate(CountedKind::DescriptorPool);
		g_state.descriptorSetsPerPool[handleKey(*pDescriptorPool)] = 0;
	}
	return result;
}

VKAPI_ATTR void VKAPI_CALL counted_vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator)
{
	if (descriptorPool != VK_NULL_HANDLE)
	{
		onDestroy(CountedKind::DescriptorPool);
		releaseDescriptorSetsOfPool(descriptorPool, true);
	}
	real_vkDestroyDescriptorPool(device, descriptorPool, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL counted_vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags)
{
	releaseDescriptorSetsOfPool(descriptorPool, false);
	return real_vkResetDescriptorPool(device, descriptorPool, flags);
}

VKAPI_ATTR VkResult VKAPI_CALL counted_vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets)
{
	const VkResult result = real_vkAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
	if (result == VK_SUCCESS)
	{
		onCreate(CountedKind::DescriptorSet, pAllocateInfo->descriptorSetCount);
		uint32_t& inPool = g_state.descriptorSetsPerPool[handleKey(pAllocateInfo->descriptorPool)];
		inPool += pAllocateInfo->descriptorSetCount;
		g_state.limits.maxDescriptorSetsPerPool = std::max(g_state.limits.maxDescriptorSetsPerPool, inPool);
	}
	return result;
}

VKAPI_ATTR VkResult VKAPI_CALL counted_vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets)
{
	uint32_t freed = 0;
	for (uint32_t i = 0; i < descriptorSetCount; ++i)
	{
		freed += (pDescriptorSets[i] != VK_NULL_HANDLE) ? 1 : 0;
	}
	onDestroy(CountedKind::DescriptorSet, freed);
	uint32_t& inPool = g_state.descriptorSetsPerPool[handleKey(descriptorPool)];
	inPool -= std::min(inPool, freed);
	return real_vkFreeDescriptorSets(device, descriptorPool, descriptorSetCount, pDescriptorSets);
}

// Command buffers are freed with their pool, so count them per pool.

PFN_vkCreateCommandPool real_vkCreateCommandPool = nullptr;
PFN_vkDestroyCommandPool real_vkDestroyCommandPool = nullptr;
PFN_vkAllocateCommandBuffers real_vkAllocateCommandBuffers = nullptr;
PFN_vkFreeCommandBuffers real_vkFreeCommandBuffers = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL counted_vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool)
{
	const VkResult result = real_vkCreateCommandPool(device, pCreateInfo, pAllocator, pCommandPool);
	if (result == VK_SUCCESS)
	{
		onCreate(CountedKind::CommandPool);
		g_state.commandBuffersPerPool[handleKey(*pCommandPool)] = 0;
	}
	return result;
}

VKAPI_ATTR void VKAPI_CALL counted_vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator)
{
	if (commandPool != VK_NULL_HANDLE)
	{
		onDestroy(CountedKind::CommandPool);
		const auto it = g_state.commandBuffersPerPool.find(handleKey(commandPool));
		if (it != g_state.commandBuffersPerPool.end())
		{
			onDestroy(CountedKind::CommandBuffer, it->second);
			g_state.commandBuffersPerPool.erase(it);
		}
	}
	real_vkDestroyCommandPool(device, commandPool, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL counted_vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers)
{
	const VkResult result = real_vkAllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
	if (result == VK_SUCCESS)
	{
		onCreate(CountedKind::CommandBuffer, pAllocateInfo->commandBufferCount);
		uint32_t& inPool = g_state.commandBuffersPerPool[handleKey(pAllocateInfo->commandPool)];
		inPool += pAllocateInfo->commandBufferCount;
		g_state.limits.maxCommandBuffersPerPool = std::max(g_state.limits.maxCommandBuffersPerPool, inPool);
	}
	return result;
}

VKAPI_ATTR void VKAPI_CALL counted_vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers)
{
	uint32_t freed = 0;
	for (uint32_t i = 0; i < commandBufferCount; ++i)
	{
		freed += (pCommandBuffers[i] != VK_NULL_HANDLE) ? 1 : 0;
	}
	onDestroy(CountedKind::CommandBuffer, freed);
	uint32_t& inPool = g_state.commandBuffersPerPool[handleKey(commandPool)];
	inPool -= std::min(inPool, freed);
	real_vkFreeCommandBuffers(device, commandPool, commandBufferCount, pCommandBuffers);
}

PFN_vkCreateQueryPool real_vkCreateQueryPool = nullptr;
PFN_vkDestroyQueryPool real_vkDestroyQueryPool = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL counted_vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool)
{
	const VkResult result = real_vkCreateQueryPool(device, pCreateInfo, pAllocator, pQueryPool);
	if (result == VK_SUCCESS)
	{
		onCreate(CountedKind::QueryPool);
		if (pCreateInfo->queryType == VK_QUERY_TYPE_TIMESTAMP)
		{
			g_state.limits.maxTimestampQueriesPerPool = std::max(g_state.limits.maxTimestampQueriesPerPool, pCreateInfo->queryCount);
		}
	}
	return result;
}

VKAPI_ATTR void VKAPI_CALL counted_vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator)
{
	if (queryPool != VK_NULL_HANDLE)
	{
		onDestroy(CountedKind::QueryPool);
	}
	real_vkDestroyQueryPool(device, queryPool, pAllocator);
}

PFN_vkAllocateMemory real_vkAllocateMemory = nullptr;
PFN_vkFreeMemory real_vkFreeMemory = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL counted_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
{
	const VkResult result = real_vkAllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
	if (result == VK_SUCCESS)
	{
		onCreate(CountedKind::DeviceMemory);
		Limits& limits = g_state.limits;
		limits.liveMemoryBytes += pAllocateInfo->allocationSize;
		limits.peakMemoryBytes = std::max(limits.peakMemoryBytes, limits.liveMemoryBytes);
		limits.largestAllocationBytes = std::max(limits.largestAllocationBytes, pAllocateInfo->allocationSize);
		g_state.memorySizes[handleKey(*pMemory)] = pAllocateInfo->allocationSize;
	}
	return result;
}

VKAPI_ATTR void VKAPI_CALL counted_vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator)
{
	if (memory != VK_NULL_HANDLE)
	{
		onDestroy(CountedKind::DeviceMemory);
		const auto it = g_state.memorySizes.find(handleKey(memory));
		if (it != g_state.memorySizes.end())
		{
			g_state.limits.liveMemoryBytes -= std::min(g_state.limits.liveMemoryBytes, it->second);
			g_state.memorySizes.erase(it);
		}
	}
	real_vkFreeMemory(device, memory, pAllocator);
}

PFN_vkCreateSwapchainKHR real_vkCreateSwapchainKHR = nullptr;
PFN_vkDestroySwapchainKHR real_vkDestroySwapchainKHR = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL counted_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
	const VkResult result = real_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
	if (result == VK_SUCCESS)
	{
		onCreate(CountedKind::Swapchain);
	}
	return result;
}

VKAPI_ATTR void VKAPI_CALL counted_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
{
	if (swapchain != VK_NULL_HANDLE)
	{
		onDestroy(CountedKind::Swapchain);
	}
	real_vkDestroySwapchainKHR(device, swapchain, pAllocator);
}

// Swap one entry point for its counting wrapper. An entry point the device does not have stays null.
// A second install (for example after the dispatch table was initialized again) keeps the real one.
template <typename PFN>
void installWrapper(PFN& entryPoint, PFN& real, PFN counted)
{
	if (entryPoint == nullptr || entryPoint == counted)
	{
		return;
	}
	real = entryPoint;
	entryPoint = counted;
}

} // anonymous namespace

void installObjectCounters(WZ_vk::DispatchLoaderDynamic& vkDynLoader)
{
	g_state = State();

#define WZ_INSTALL_COUNTED(fn) installWrapper(vkDynLoader.fn, real_##fn, counted_##fn)
	WZ_INSTALL_COUNTED(vkCreateBuffer);
	WZ_INSTALL_COUNTED(vkDestroyBuffer);
	WZ_INSTALL_COUNTED(vkCreateImage);
	WZ_INSTALL_COUNTED(vkDestroyImage);
	WZ_INSTALL_COUNTED(vkCreateImageView);
	WZ_INSTALL_COUNTED(vkDestroyImageView);
	WZ_INSTALL_COUNTED(vkCreateSampler);
	WZ_INSTALL_COUNTED(vkDestroySampler);
	WZ_INSTALL_COUNTED(vkCreateShaderModule);
	WZ_INSTALL_COUNTED(vkDestroyShaderModule);
	WZ_INSTALL_COUNTED(vkCreateDescriptorSetLayout);
	WZ_INSTALL_COUNTED(vkDestroyDescriptorSetLayout);
	WZ_INSTALL_COUNTED(vkCreatePipelineLayout);
	WZ_INSTALL_COUNTED(vkDestroyPipelineLayout);
	WZ_INSTALL_COUNTED(vkCreateGraphicsPipelines);
	WZ_INSTALL_COUNTED(vkCreateComputePipelines);
	WZ_INSTALL_COUNTED(vkDestroyPipeline);
	WZ_INSTALL_COUNTED(vkCreatePipelineCache);
	WZ_INSTALL_COUNTED(vkDestroyPipelineCache);
	WZ_INSTALL_COUNTED(vkCreateRenderPass);
	WZ_INSTALL_COUNTED(vkDestroyRenderPass);
	WZ_INSTALL_COUNTED(vkCreateFramebuffer);
	WZ_INSTALL_COUNTED(vkDestroyFramebuffer);
	WZ_INSTALL_COUNTED(vkCreateDescriptorPool);
	WZ_INSTALL_COUNTED(vkDestroyDescriptorPool);
	WZ_INSTALL_COUNTED(vkResetDescriptorPool);
	WZ_INSTALL_COUNTED(vkAllocateDescriptorSets);
	WZ_INSTALL_COUNTED(vkFreeDescriptorSets);
	WZ_INSTALL_COUNTED(vkCreateCommandPool);
	WZ_INSTALL_COUNTED(vkDestroyCommandPool);
	WZ_INSTALL_COUNTED(vkAllocateCommandBuffers);
	WZ_INSTALL_COUNTED(vkFreeCommandBuffers);
	WZ_INSTALL_COUNTED(vkCreateFence);
	WZ_INSTALL_COUNTED(vkDestroyFence);
	WZ_INSTALL_COUNTED(vkCreateSemaphore);
	WZ_INSTALL_COUNTED(vkDestroySemaphore);
	WZ_INSTALL_COUNTED(vkCreateQueryPool);
	WZ_INSTALL_COUNTED(vkDestroyQueryPool);
	WZ_INSTALL_COUNTED(vkAllocateMemory);
	WZ_INSTALL_COUNTED(vkFreeMemory);
	WZ_INSTALL_COUNTED(vkCreateSwapchainKHR);
	WZ_INSTALL_COUNTED(vkDestroySwapchainKHR);
#undef WZ_INSTALL_COUNTED
}

void reportObjectCounters(code_part part)
{
	debug(part, "Vulkan objects: peak live, total created, live now (Vulkan SC reservation field)");
	for (size_t i = 0; i < kKindInfo.size(); ++i)
	{
		const KindCount& c = g_state.counts[i];
		debug(part, "  %-26s %8" PRIu64 " %8" PRIu64 " %8" PRIu64 "  (%s)", kKindInfo[i].name, c.peak, c.created, c.live, kKindInfo[i].reservationField);
	}
	const Limits& limits = g_state.limits;
	debug(part, "  maxImageViewMipLevels %u, maxImageViewArrayLayers %u, maxLayeredImageViewMipLevels %u",
		limits.maxImageViewMipLevels, limits.maxImageViewArrayLayers, limits.maxLayeredImageViewMipLevels);
	debug(part, "  descriptorSetLayoutBindingLimit %u, maxTimestampQueriesPerPool %u",
		limits.descriptorSetLayoutBindingLimit, limits.maxTimestampQueriesPerPool);
	debug(part, "  most descriptor sets in one pool %u, most command buffers in one pool %u",
		limits.maxDescriptorSetsPerPool, limits.maxCommandBuffersPerPool);
	debug(part, "  device memory: peak %" PRIu64 " bytes, largest allocation %" PRIu64 " bytes, live now %" PRIu64 " bytes",
		static_cast<uint64_t>(limits.peakMemoryBytes), static_cast<uint64_t>(limits.largestAllocationBytes), static_cast<uint64_t>(limits.liveMemoryBytes));
}

} // namespace gfx_api::vk

#endif // defined(WZ_VULKAN_ENABLED)
