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
/** @file screenshot_readback.h
 * Swapchain screenshot readback integrated with the screen-frame submit path.
 */

#pragma once

#if defined(WZ_VULKAN_ENABLED)

#include "vk/vulkan_hpp_include.h"
#include "vk/wz_vk.h"
#include "3rdparty/vk_mem_alloc.h"

#include "pietypes.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

struct VkRoot;

namespace gfx_api
{

struct abstract_texture;

namespace vk
{

/// Host-visible staging buffer for one capture. ScreenshotReadback reuses it for later captures,
/// so it is made again only for a larger capture. Its memory comes from VMA (in the Vulkan SC build,
/// from the fixed staging pool), so a capture allocates no new device memory.
struct ScreenshotStagingBuffer
{
	VmaAllocator allocator = VK_NULL_HANDLE;
	::vk::Buffer buffer{};
	VmaAllocation allocation = VK_NULL_HANDLE;
	::vk::DeviceSize size = 0;
	::vk::DeviceSize capacity = 0;
	::vk::DeviceSize rowPitch = 0;

	bool create(VmaAllocator vmaAllocator, const VmaAllocationCreateInfo& allocInfo,
		uint32_t width, uint32_t height, uint32_t bytesPerPixel,
		::vk::DeviceSize bufferImageGranularity, const WZ_vk::DispatchLoaderDynamic& vkDynLoader);
	void destroy(const WZ_vk::DispatchLoaderDynamic& vkDynLoader);
	void* map(const WZ_vk::DispatchLoaderDynamic& vkDynLoader) const;
	void unmap(const WZ_vk::DispatchLoaderDynamic& vkDynLoader) const;
};

struct PendingCapture
{
	ScreenshotStagingBuffer staging;
	::vk::Extent2D extent{};
	::vk::Format srcFormat = ::vk::Format::eUndefined;
	uint32_t swapchainImageIndex = 0;
	uint32_t ringSlot = 0;
	bool gpuRecorded = false;
	bool gpuSubmitted = false;
	std::function<void(std::unique_ptr<iV_Image>)> callback;
};

/// Queues swapchain captures, records GPU copy on the draw command buffer, completes on ring-slot fence.
class ScreenshotReadback
{
public:
	static constexpr size_t MAX_PENDING = 2;

	// stagingAllocInfo: host visible and coherent memory (VMA_MEMORY_USAGE_CPU_ONLY), with its pool
	bool requestCapture(std::function<void(std::unique_ptr<iV_Image>)> callback,
		VmaAllocator allocator, const VmaAllocationCreateInfo& stagingAllocInfo,
		::vk::DeviceSize bufferImageGranularity, ::vk::Extent2D extent, ::vk::Format srcFormat,
		uint32_t swapchainImageIndex, uint32_t ringSlot, const WZ_vk::DispatchLoaderDynamic& vkDynLoader);

	bool hasAwaitingRecord() const { return _awaitingRecord.has_value(); }

	void recordCopy(VkRoot& root, ::vk::CommandBuffer drawCmd, ::gfx_api::abstract_texture* swapchainColor,
		::vk::Image swapchainImage);

	void markCurrentCaptureSubmitted(uint32_t ringSlot, const WZ_vk::DispatchLoaderDynamic& vkDynLoader);

	void completeReadyForRingSlot(::vk::Device dev, uint32_t ringSlot,
		const WZ_vk::DispatchLoaderDynamic& vkDynLoader);

	void cancelAwaitingRecord(const WZ_vk::DispatchLoaderDynamic& vkDynLoader);
	void cancelPendingSubmit(const WZ_vk::DispatchLoaderDynamic& vkDynLoader);
	void cancelAll(const WZ_vk::DispatchLoaderDynamic& vkDynLoader);
	/// Cancels every capture (the swapchain is going away). The staging buffers are kept.
	void shutdown(const WZ_vk::DispatchLoaderDynamic& vkDynLoader);
	/// Frees the staging buffers. Call before the device is destroyed.
	void releaseStagingBuffers(const WZ_vk::DispatchLoaderDynamic& vkDynLoader);

private:
	std::optional<PendingCapture> _awaitingRecord;
	std::optional<PendingCapture> _pendingThisSubmit;
	std::deque<PendingCapture> _submitted;
	std::vector<ScreenshotStagingBuffer> _freeStaging;

	size_t totalPendingCount() const;
	static bool isSupportedSwapchainFormat(::vk::Format format);
	static bool buildCpuImage(const PendingCapture& capture, const uint8_t* mappedStaging,
		std::unique_ptr<iV_Image>& outImage);
	void destroyCaptureResources(PendingCapture& capture, const WZ_vk::DispatchLoaderDynamic& vkDynLoader);
};

} // namespace gfx_api::vk

} // namespace gfx_api

#endif // defined(WZ_VULKAN_ENABLED)
