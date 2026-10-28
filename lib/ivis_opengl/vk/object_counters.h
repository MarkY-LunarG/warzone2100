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
/** @file object_counters.h
 * Live and peak counts of the Vulkan objects that the backend creates.
 *
 * The counters wrap the create and destroy entry points of the device dispatch table,
 * so they also count objects that VMA and the Vulkan-Hpp unique handles create.
 * Vulkan SC reserves the most objects of each type that are alive at one time
 * (VkDeviceObjectReservationCreateInfo), and these peaks are the measured values for it.
 */

#pragma once

#if defined(WZ_VULKAN_ENABLED)

#include "vk/wz_vk.h"

#include "lib/framework/frame.h"

namespace gfx_api::vk
{

// Replaces the create and destroy entry points in the dispatch table with counting wrappers,
// and resets the counts. Call it after the dispatch table is initialized with the device,
// and before anything else copies entry points from it (VMA).
void installObjectCounters(WZ_vk::DispatchLoaderDynamic& vkDynLoader);

// Writes the peak, total, and live count of each object type to the log.
// Call it before the device is destroyed: objects that are still live then were not destroyed.
void reportObjectCounters(code_part part);
// The end of startup: the report also counts what is created after it. Vulkan SC has no vkFreeMemory, so
// device memory allocated after startup is never given back.
void markObjectCountersStartupComplete();

} // namespace gfx_api::vk

#endif // defined(WZ_VULKAN_ENABLED)
