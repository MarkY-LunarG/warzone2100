/*
* Vulkan header selection for the vulkansc-samples pipeline exporter (local to Warzone 2100)
*
* The exporter was written against <vulkan/vulkan_sc.h>. Warzone 2100 exports pipeline JSON from
* its desktop Vulkan build, which uses <vulkan/vulkan.h>, and the two headers cannot share a
* translation unit. The structures the exporter writes have the same layout in both headers, so
* in the desktop build this header declares only the Vulkan SC items the exporter names, exactly as
* vulkan_sc_core.h (VulkanSC-Headers 1.0.22) declares them.
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#if defined(WZ_VULKANSC)

#include <vulkan/vulkan_sc.h>

#else

#include <vulkan/vulkan.h>

#if !defined(VKSC_API_VERSION_1_0)

constexpr VkStructureType VK_STRUCTURE_TYPE_PIPELINE_OFFLINE_CREATE_INFO = static_cast<VkStructureType>(1000298010);

typedef enum VkPipelineMatchControl {
    VK_PIPELINE_MATCH_CONTROL_APPLICATION_UUID_EXACT_MATCH = 0,
    VK_PIPELINE_MATCH_CONTROL_MAX_ENUM = 0x7FFFFFFF
} VkPipelineMatchControl;

typedef struct VkPipelineOfflineCreateInfo {
    VkStructureType           sType;
    const void*               pNext;
    uint8_t                   pipelineIdentifier[VK_UUID_SIZE];
    VkPipelineMatchControl    matchControl;
    VkDeviceSize              poolEntrySize;
} VkPipelineOfflineCreateInfo;

#endif // !defined(VKSC_API_VERSION_1_0)

#endif // defined(WZ_VULKANSC)
