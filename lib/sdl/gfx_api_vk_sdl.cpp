/*
	This file is part of Warzone 2100.
	Copyright (C) 2019  Warzone 2100 Project

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

#if defined(WZ_VULKAN_ENABLED) && defined(HAVE_SDL_VULKAN_H)

#include "gfx_api_vk_sdl.h" // Must be prior to frame.h include
#include "lib/framework/frame.h"
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_version.h>
#include <SDL3/SDL_loadso.h>

#include <array>

sdl_Vulkan_Impl::sdl_Vulkan_Impl(SDL_Window* _window, bool _allowImplicitLayers)
{
	ASSERT(_window != nullptr, "Invalid SDL_Window*");
	window = _window;
	m_allowImplicitLayers = _allowImplicitLayers;
}

#if defined(WZ_VULKANSC)

sdl_Vulkan_Impl::~sdl_Vulkan_Impl()
{
	if (m_vulkanSCLibrary)
	{
		SDL_UnloadObject(m_vulkanSCLibrary);
		m_vulkanSCLibrary = nullptr;
	}
}

PFN_vkGetInstanceProcAddr sdl_Vulkan_Impl::getVkGetInstanceProcAddr()
{
	if (m_getInstanceProcAddr)
	{
		return m_getInstanceProcAddr;
	}
#if defined(WZ_OS_WIN)
	const char* libraryName = "vulkansc-1.dll";
#else
	const char* libraryName = "libvulkansc.so.1";
#endif
	if (const char* overrideName = getenv("WZ_VULKANSC_LIBRARY"))
	{
		libraryName = overrideName;
	}
	m_vulkanSCLibrary = SDL_LoadObject(libraryName);
	if (!m_vulkanSCLibrary)
	{
		debug(LOG_ERROR, "Failed to load the Vulkan SC loader (%s): %s", libraryName, SDL_GetError());
		return nullptr;
	}
#if defined( _MSC_VER )
#pragma warning( push )
#pragma warning( disable : 4191 ) // warning C4191: 'reinterpret_cast': unsafe conversion from 'SDL_FunctionPointer' to 'PFN_vkGetInstanceProcAddr'
#endif
	m_getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_LoadFunction(m_vulkanSCLibrary, "vkGetInstanceProcAddr"));
#if defined( _MSC_VER )
#pragma warning( pop )
#endif
	if (!m_getInstanceProcAddr)
	{
		debug(LOG_ERROR, "%s has no vkGetInstanceProcAddr: %s", libraryName, SDL_GetError());
		return nullptr;
	}
	debug(LOG_INFO, "Loaded the Vulkan SC loader: %s", libraryName);
	return m_getInstanceProcAddr;
}

bool sdl_Vulkan_Impl::getRequiredInstanceExtensions(std::vector<const char*> &output)
{
	output = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME };
	return true;
}

// The first display of the first physical device that has one, its largest mode (then the highest
// refresh rate), and the first plane that can show that display, as the vulkansc-samples swapchain does.
// The enumeration arrays have fixed sizes.
bool sdl_Vulkan_Impl::createWindowSurface(VkInstance instance, VkSurfaceKHR* surface)
{
	const PFN_vkGetInstanceProcAddr gipa = getVkGetInstanceProcAddr();
	if (!gipa)
	{
		return false;
	}
#if defined( _MSC_VER )
#pragma warning( push )
#pragma warning( disable : 4191 ) // warning C4191: 'reinterpret_cast': unsafe conversion from 'PFN_vkVoidFunction'
#endif
	const auto enumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
	const auto getDisplayProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceDisplayPropertiesKHR>(gipa(instance, "vkGetPhysicalDeviceDisplayPropertiesKHR"));
	const auto getDisplayModeProperties = reinterpret_cast<PFN_vkGetDisplayModePropertiesKHR>(gipa(instance, "vkGetDisplayModePropertiesKHR"));
	const auto getPlaneProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR>(gipa(instance, "vkGetPhysicalDeviceDisplayPlanePropertiesKHR"));
	const auto getPlaneSupportedDisplays = reinterpret_cast<PFN_vkGetDisplayPlaneSupportedDisplaysKHR>(gipa(instance, "vkGetDisplayPlaneSupportedDisplaysKHR"));
	const auto createDisplayPlaneSurface = reinterpret_cast<PFN_vkCreateDisplayPlaneSurfaceKHR>(gipa(instance, "vkCreateDisplayPlaneSurfaceKHR"));
#if defined( _MSC_VER )
#pragma warning( pop )
#endif
	if (!enumeratePhysicalDevices || !getDisplayProperties || !getDisplayModeProperties || !getPlaneProperties || !getPlaneSupportedDisplays || !createDisplayPlaneSurface)
	{
		debug(LOG_ERROR, "Vulkan SC: the instance has no VK_KHR_display entry points");
		return false;
	}

	std::array<VkPhysicalDevice, 16> physicalDevices{};
	uint32_t physicalDeviceCount = static_cast<uint32_t>(physicalDevices.size());
	const VkResult enumerateResult = enumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data());
	if (enumerateResult != VK_SUCCESS && enumerateResult != VK_INCOMPLETE)
	{
		debug(LOG_ERROR, "Vulkan SC: vkEnumeratePhysicalDevices failed: %d", static_cast<int>(enumerateResult));
		return false;
	}
	for (uint32_t deviceIndex = 0; deviceIndex < physicalDeviceCount; ++deviceIndex)
	{
		const VkPhysicalDevice physicalDevice = physicalDevices[deviceIndex];
		std::array<VkDisplayPropertiesKHR, 32> displays{};
		uint32_t displayCount = static_cast<uint32_t>(displays.size());
		const VkResult displayResult = getDisplayProperties(physicalDevice, &displayCount, displays.data());
		if ((displayResult != VK_SUCCESS && displayResult != VK_INCOMPLETE) || displayCount == 0)
		{
			continue;
		}
		const VkDisplayKHR display = displays[0].display;

		std::array<VkDisplayModePropertiesKHR, 64> modes{};
		uint32_t modeCount = static_cast<uint32_t>(modes.size());
		const VkResult modeResult = getDisplayModeProperties(physicalDevice, display, &modeCount, modes.data());
		if ((modeResult != VK_SUCCESS && modeResult != VK_INCOMPLETE) || modeCount == 0)
		{
			continue;
		}
		const VkDisplayModePropertiesKHR* bestMode = &modes[0];
		for (uint32_t i = 1; i < modeCount; ++i)
		{
			const auto& p = modes[i].parameters;
			const auto& best = bestMode->parameters;
			const uint64_t area = static_cast<uint64_t>(p.visibleRegion.width) * p.visibleRegion.height;
			const uint64_t bestArea = static_cast<uint64_t>(best.visibleRegion.width) * best.visibleRegion.height;
			if (area > bestArea || (area == bestArea && p.refreshRate > best.refreshRate))
			{
				bestMode = &modes[i];
			}
		}

		std::array<VkDisplayPlanePropertiesKHR, 32> planes{};
		uint32_t planeCount = static_cast<uint32_t>(planes.size());
		const VkResult planeResult = getPlaneProperties(physicalDevice, &planeCount, planes.data());
		if (planeResult != VK_SUCCESS && planeResult != VK_INCOMPLETE)
		{
			continue;
		}
		for (uint32_t planeIndex = 0; planeIndex < planeCount; ++planeIndex)
		{
			if (planes[planeIndex].currentDisplay != VK_NULL_HANDLE && planes[planeIndex].currentDisplay != display)
			{
				continue;
			}
			std::array<VkDisplayKHR, 32> supported{};
			uint32_t supportedCount = static_cast<uint32_t>(supported.size());
			const VkResult supportedResult = getPlaneSupportedDisplays(physicalDevice, planeIndex, &supportedCount, supported.data());
			if (supportedResult != VK_SUCCESS && supportedResult != VK_INCOMPLETE)
			{
				continue;
			}
			bool planeShowsDisplay = false;
			for (uint32_t i = 0; i < supportedCount; ++i)
			{
				planeShowsDisplay = planeShowsDisplay || (supported[i] == display);
			}
			if (!planeShowsDisplay)
			{
				continue;
			}

			VkDisplaySurfaceCreateInfoKHR createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
			createInfo.displayMode = bestMode->displayMode;
			createInfo.planeIndex = planeIndex;
			createInfo.planeStackIndex = planes[planeIndex].currentStackIndex;
			createInfo.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
			createInfo.globalAlpha = 1.0f;
			createInfo.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
			createInfo.imageExtent = bestMode->parameters.visibleRegion;
			const VkResult surfaceResult = createDisplayPlaneSurface(instance, &createInfo, nullptr, surface);
			if (surfaceResult != VK_SUCCESS)
			{
				debug(LOG_ERROR, "Vulkan SC: vkCreateDisplayPlaneSurfaceKHR failed: %d", static_cast<int>(surfaceResult));
				return false;
			}
			m_displayWidth = static_cast<int>(createInfo.imageExtent.width);
			m_displayHeight = static_cast<int>(createInfo.imageExtent.height);
			debug(LOG_INFO, "Vulkan SC display: %s, mode %dx%d at %.3f Hz, plane %u (physical device %u)",
				(displays[0].displayName) ? displays[0].displayName : "(no name)", m_displayWidth, m_displayHeight,
				static_cast<double>(bestMode->parameters.refreshRate) / 1000.0, planeIndex, deviceIndex);
			return true;
		}
	}
	debug(LOG_ERROR, "Vulkan SC: no display with a usable plane was found");
	return false;
}

void sdl_Vulkan_Impl::getDrawableSize(int* w, int* h)
{
	if (m_displayWidth > 0 && m_displayHeight > 0)
	{
		*w = m_displayWidth;
		*h = m_displayHeight;
		return;
	}
	SDL_GetWindowSizeInPixels(window, w, h);
}

#else // !defined(WZ_VULKANSC)

PFN_vkGetInstanceProcAddr sdl_Vulkan_Impl::getVkGetInstanceProcAddr()
{
#if defined( _MSC_VER )
#pragma warning( push )
#pragma warning( disable : 4191 ) // warning C4191: 'reinterpret_cast': unsafe conversion from 'SDL_FunctionPointer' to 'PFN_vkGetInstanceProcAddr'
#endif
	PFN_vkGetInstanceProcAddr _vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
#if defined( _MSC_VER )
#pragma warning( pop )
#endif
	if(!_vkGetInstanceProcAddr)
	{
		debug(LOG_ERROR, "SDL_Vulkan_GetVkGetInstanceProcAddr() failed: %s\n", SDL_GetError());
		return nullptr;
	}
	return _vkGetInstanceProcAddr;
}

bool sdl_Vulkan_Impl::getRequiredInstanceExtensions(std::vector<const char*> &output)
{
	// Get the required extension count
	uint32_t requiredExtensionCount;
	const char * const *instance_extensions = SDL_Vulkan_GetInstanceExtensions(&requiredExtensionCount);
	if (!instance_extensions)
	{
		debug(LOG_ERROR, "SDL_Vulkan_GetInstanceExtensions failed: %s\n", SDL_GetError());
		return false;
	}

	std::vector<const char*> extensions;
	extensions.reserve(requiredExtensionCount);
	for (uint32_t i = 0; i < requiredExtensionCount; i++)
	{
		if (!instance_extensions[i]) { continue; }
		extensions.push_back(instance_extensions[i]);
	}

	output = extensions;
	return true;
}

bool sdl_Vulkan_Impl::createWindowSurface(VkInstance instance, VkSurfaceKHR* surface)
{
	if (!SDL_Vulkan_CreateSurface(window, instance, NULL, surface))
	{
		debug(LOG_ERROR, "SDL_Vulkan_CreateSurface() failed: %s\n", SDL_GetError());
		return false;
	}
	return true;
}

void sdl_Vulkan_Impl::getDrawableSize(int* w, int* h)
{
	SDL_GetWindowSizeInPixels(window, w, h);
}

#endif // defined(WZ_VULKANSC)

bool sdl_Vulkan_Impl::allowImplicitLayers() const
{
	return m_allowImplicitLayers;
}

#endif // defined(WZ_VULKAN_ENABLED) && defined(HAVE_SDL_VULKAN_H)
