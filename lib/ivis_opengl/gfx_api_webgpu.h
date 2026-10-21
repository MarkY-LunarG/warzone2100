/*
	This file is part of Warzone 2100.
	Copyright (C) 2026  Warzone 2100 Project

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

#pragma once

#include "gfx_api.h"

#if defined(WZ_WEBGPU_ENABLED)

// Only the Dawn C header is used here. The C++ header (webgpu_cpp.h) needs C++20,
// and some files that include this header (gfx_api.cpp, lib/sdl) build as C++17.
#include <webgpu/webgpu.h>

#include <string>

namespace gfx_api
{
	// Implemented by the windowing backend (ex. SDL)
	class backend_WebGPU_Impl
	{
	public:
		backend_WebGPU_Impl() {};
		virtual ~backend_WebGPU_Impl() {};

		// Create the surface for the window. On failure, return nullptr and set errorMessage.
		// The caller owns the returned reference.
		virtual WGPUSurface createSurface(WGPUInstance instance, std::string& errorMessage) = 0;

		// The size of the window's drawable area, in pixels
		virtual void getDrawableSize(int* w, int* h) = 0;
	};

	// Create the WebGPU backend context (gfx_api_webgpu.cpp)
	context* createWebGPUContext(bool useDebug);
}

#endif // defined(WZ_WEBGPU_ENABLED)
