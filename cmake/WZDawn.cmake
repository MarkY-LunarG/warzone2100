# Provides the Dawn WebGPU implementation for the WebGPU backend.
#
# Native builds: use an installed Dawn if find_package(Dawn) finds one. Otherwise, fetch and
# build Dawn at a pinned commit. To use a local Dawn checkout instead of a new clone, set
# FETCHCONTENT_SOURCE_DIR_DAWN to that checkout.
#
# Emscripten builds: use Emscripten's emdawnwebgpu port (Dawn's webgpu.h for the browser).
# The shader build needs the host tools tint and wz_webgpu_spirv_prep, which cannot be built
# with the Emscripten toolchain. Set WZ_WEBGPU_HOST_TOOLS_DIR to a native WebGPU build folder
# that has them (for example, the "build" folder).
#
# On success, the target dawn::webgpu_dawn exists and WZ_DAWN_FOUND is TRUE.

set(WZ_DAWN_FOUND FALSE)

if(CMAKE_VERSION VERSION_LESS 3.28)
	message(FATAL_ERROR "WZ_ENABLE_BACKEND_WEBGPU requires CMake 3.28 or later, because Dawn does")
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Emscripten")
	set(WZ_WEBGPU_HOST_TOOLS_DIR "" CACHE PATH "Native WebGPU build folder that has the host tools tint and wz_webgpu_spirv_prep")
	find_program(WZ_WEBGPU_TINT NAMES tint PATHS "${WZ_WEBGPU_HOST_TOOLS_DIR}/_deps/dawn-build" NO_DEFAULT_PATH)
	find_program(WZ_WEBGPU_SPIRV_PREP NAMES wz_webgpu_spirv_prep PATHS "${WZ_WEBGPU_HOST_TOOLS_DIR}/build_tools" NO_DEFAULT_PATH)
	if(NOT WZ_WEBGPU_TINT OR NOT WZ_WEBGPU_SPIRV_PREP)
		message(FATAL_ERROR "WZ_ENABLE_BACKEND_WEBGPU for Emscripten requires WZ_WEBGPU_HOST_TOOLS_DIR: a native WebGPU build folder that has _deps/dawn-build/tint and build_tools/wz_webgpu_spirv_prep")
	endif()
	# The same target names as a native build, so that data/CMakeLists.txt needs no change
	add_executable(tint IMPORTED GLOBAL)
	set_target_properties(tint PROPERTIES IMPORTED_LOCATION "${WZ_WEBGPU_TINT}")
	add_executable(wz_webgpu_spirv_prep IMPORTED GLOBAL)
	set_target_properties(wz_webgpu_spirv_prep PROPERTIES IMPORTED_LOCATION "${WZ_WEBGPU_SPIRV_PREP}")

	add_library(wz_emdawnwebgpu INTERFACE)
	target_compile_options(wz_emdawnwebgpu INTERFACE "--use-port=emdawnwebgpu")
	target_link_options(wz_emdawnwebgpu INTERFACE "--use-port=emdawnwebgpu"
		# Creates the WebGPU device before the game starts (WebGPU device creation is asynchronous)
		"SHELL:--post-js ${CMAKE_CURRENT_LIST_DIR}/../platforms/emscripten/webgpu-postjs.js")
	add_library(dawn::webgpu_dawn ALIAS wz_emdawnwebgpu)
	set(WZ_DAWN_FOUND TRUE)
	message(STATUS "Dawn WebGPU: Emscripten emdawnwebgpu port (host tools: ${WZ_WEBGPU_HOST_TOOLS_DIR})")
	return()
endif()

set(WZ_DAWN_GIT_TAG "224dae66485fc686892222e55dae5c83821f4d10" CACHE STRING "Dawn commit to fetch and build")

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
	# WebGPU on Linux uses Wayland only, not X11
	find_package(PkgConfig QUIET)
	if(PkgConfig_FOUND)
		pkg_check_modules(_WZ_DAWN_WAYLAND QUIET wayland-client wayland-cursor wayland-egl xkbcommon)
	endif()
	if(NOT _WZ_DAWN_WAYLAND_FOUND)
		message(FATAL_ERROR "WZ_ENABLE_BACKEND_WEBGPU requires the Wayland development packages (wayland-client, wayland-cursor, wayland-egl, xkbcommon)")
	endif()
endif()

# Dawn build settings, used only if Dawn is built from source
set(DAWN_BUILD_MONOLITHIC_LIBRARY SHARED)
set(DAWN_FETCH_DEPENDENCIES ON)
set(DAWN_USE_GLFW OFF)
set(DAWN_BUILD_SAMPLES OFF)
set(DAWN_BUILD_TESTS OFF)
set(TINT_BUILD_TESTS OFF)
set(TINT_BUILD_CMD_TOOLS ON) # the shader build uses the tint executable
# WZ already finds protobuf (for GameNetworkingSockets), and Dawn's own protobuf
# build then fails. Only the Tint IR binary format needs protobuf.
set(DAWN_BUILD_PROTOBUF OFF)
set(TINT_BUILD_IR_BINARY OFF)
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
	set(DAWN_USE_WAYLAND ON)
	set(DAWN_USE_X11 OFF)
	# Dawn runs on Vulkan on Linux; the GL backends are not needed
	set(DAWN_ENABLE_DESKTOP_GL OFF)
	set(DAWN_ENABLE_OPENGLES OFF)
endif()

include(FetchContent)
FetchContent_Declare(
	dawn
	GIT_REPOSITORY https://dawn.googlesource.com/dawn.git
	GIT_TAG        ${WZ_DAWN_GIT_TAG}
	GIT_SHALLOW    TRUE
	GIT_SUBMODULES "" # DAWN_FETCH_DEPENDENCIES gets the dependencies
	EXCLUDE_FROM_ALL
	# System headers: WZ builds with -Werror, and webgpu_cpp.h trips -Wstrict-aliasing on GCC
	SYSTEM
	FIND_PACKAGE_ARGS NAMES Dawn
)
if(WZ_DISABLE_FETCHCONTENT_GIT_CLONE AND NOT FETCHCONTENT_SOURCE_DIR_DAWN)
	find_package(Dawn CONFIG)
else()
	FetchContent_MakeAvailable(dawn)
endif()

if(TARGET dawn::webgpu_dawn)
	set(WZ_DAWN_FOUND TRUE)
	message(STATUS "Dawn WebGPU: found (${WZ_DAWN_GIT_TAG})")
else()
	message(WARNING "Dawn WebGPU was not found; the WebGPU backend is disabled")
endif()
