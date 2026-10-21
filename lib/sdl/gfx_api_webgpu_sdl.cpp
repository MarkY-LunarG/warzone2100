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

#include "gfx_api_webgpu_sdl.h" // Must be prior to frame.h include

#if defined(WZ_WEBGPU_ENABLED)

#include "lib/framework/frame.h"
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_properties.h>
#include <cstdlib>
#include <cstring>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

sdl_WebGPU_Impl::sdl_WebGPU_Impl(SDL_Window* _window)
{
	ASSERT(_window != nullptr, "Invalid SDL_Window*");
	window = _window;
}

WGPUSurface sdl_WebGPU_Impl::createSurface(WGPUInstance instance, std::string& errorMessage)
{
#if defined(__linux__)
	// WebGPU on Linux uses Wayland only (Dawn is built without X11)
	const char* videoDriver = SDL_GetCurrentVideoDriver();
	if (videoDriver == nullptr || strcmp(videoDriver, "wayland") != 0)
	{
		errorMessage = "WebGPU needs the SDL Wayland video driver (current driver: ";
		errorMessage += (videoDriver != nullptr) ? videoDriver : "none";
		errorMessage += ")";
		return nullptr;
	}

	const SDL_PropertiesID props = SDL_GetWindowProperties(window);
	WGPUSurfaceSourceWaylandSurface waylandSource = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
	waylandSource.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
	waylandSource.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
	if (waylandSource.display == nullptr || waylandSource.surface == nullptr)
	{
		errorMessage = "Failed to get the Wayland display or surface from the SDL window";
		return nullptr;
	}

	WGPUSurfaceDescriptor surfaceDesc = WGPU_SURFACE_DESCRIPTOR_INIT;
	surfaceDesc.nextInChain = &waylandSource.chain;
	WGPUSurface surface = wgpuInstanceCreateSurface(instance, &surfaceDesc);
	if (surface == nullptr)
	{
		errorMessage = "wgpuInstanceCreateSurface failed";
	}
	return surface;
#elif defined(__EMSCRIPTEN__)
	// The browser draws to the canvas of the page (platforms/emscripten/shell.html)
	WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSource = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
	canvasSource.selector = WGPUStringView{"#canvas", WGPU_STRLEN};
	WGPUSurfaceDescriptor surfaceDesc = WGPU_SURFACE_DESCRIPTOR_INIT;
	surfaceDesc.nextInChain = &canvasSource.chain;
	WGPUSurface surface = wgpuInstanceCreateSurface(instance, &surfaceDesc);
	if (surface == nullptr)
	{
		errorMessage = "wgpuInstanceCreateSurface failed for the canvas";
	}
	return surface;
#else
	// Windows and macOS are not done yet (plan: work order step 9)
	errorMessage = "The WebGPU backend is not supported on this platform yet";
	return nullptr;
#endif
}

void sdl_WebGPU_Impl::getDrawableSize(int* w, int* h)
{
#if defined(__EMSCRIPTEN__)
	// The surface texture has the size of the canvas backing store. The soft fullscreen mode
	// (main_sdl.cpp) changes that size for the device pixel ratio, and the SDL window size can
	// be different.
	if (emscripten_get_canvas_element_size("#canvas", w, h) == EMSCRIPTEN_RESULT_SUCCESS)
	{
		return;
	}
#endif
	SDL_GetWindowSizeInPixels(window, w, h);
}

bool sdl_WebGPU_VideoDriverIsSupported()
{
#if defined(__linux__)
	if (SDL_WasInit(SDL_INIT_VIDEO) != 0)
	{
		const char* videoDriver = SDL_GetCurrentVideoDriver();
		return videoDriver != nullptr && strcmp(videoDriver, "wayland") == 0;
	}
	// The video subsystem is not running yet, so predict the driver:
	// SDL uses Wayland when a Wayland compositor is available, unless the driver hint says otherwise
	const char* driverHint = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
	if (driverHint != nullptr && driverHint[0] != '\0')
	{
		return strncmp(driverHint, "wayland", 7) == 0;
	}
	const char* waylandDisplay = getenv("WAYLAND_DISPLAY");
	return waylandDisplay != nullptr && waylandDisplay[0] != '\0';
#elif defined(__EMSCRIPTEN__)
	// The page creates the device before the game starts (platforms/emscripten/webgpu-postjs.js)
	return MAIN_THREAD_EM_ASM_INT({ return Module['preinitializedWebGPUDevice'] ? 1 : 0; }) != 0;
#else
	return false;
#endif
}

#endif // defined(WZ_WEBGPU_ENABLED)
