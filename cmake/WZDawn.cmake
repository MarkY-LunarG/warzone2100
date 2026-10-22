# Provides the Dawn WebGPU implementation for the WebGPU backend.
#
# Native builds: use an installed Dawn if find_package(Dawn) finds one. Otherwise, fetch and
# build Dawn at a pinned commit. To use a local Dawn checkout instead of a new clone, set
# FETCHCONTENT_SOURCE_DIR_DAWN to that checkout.
#
# Emscripten builds: use Dawn's emdawnwebgpu port (Dawn's webgpu.h for the browser), from a
# Dawn release near the pinned commit, so that the browser and native webgpu.h match. The
# build downloads the package and patches it for newer Emscripten (tested: 6.0.8), which
# removed the USE_WEBGPU setting that the package reads. To use an extracted package instead of a
# download, set WZ_EMDAWNWEBGPU_PKG_DIR to its emdawnwebgpu_pkg folder.
# The shader build needs the host tools tint and wz_webgpu_spirv_prep, which cannot be built
# with the Emscripten toolchain. Set WZ_WEBGPU_HOST_TOOLS_DIR to a native WebGPU build folder
# that has them (for example, the "build" folder).
#
# On success, the target dawn::webgpu_dawn exists and WZ_DAWN_FOUND is TRUE.

set(WZ_DAWN_FOUND FALSE)

# Gets the emdawnwebgpu package, and copies it into the build folder with the patches that
# Emscripten 6 needs. The patched code also works with Emscripten 4, which has USE_WEBGPU.
# Sets OUT_PORT_FILE to the patched port file, for --use-port.
function(wz_prepare_emdawnwebgpu_port OUT_PORT_FILE)
	set(WZ_EMDAWNWEBGPU_VERSION "v20260623.191042" CACHE STRING "Dawn release of the emdawnwebgpu package")
	set(WZ_EMDAWNWEBGPU_SHA256 "cfa71bc53b5a73c2948a39795f31a0cdda1e8e6109d13682f0c08caaf45482f3" CACHE STRING "SHA-256 of the emdawnwebgpu package zip")
	set(WZ_EMDAWNWEBGPU_PKG_DIR "" CACHE PATH "Extracted emdawnwebgpu_pkg folder to use instead of a download")

	set(_deps_dir "${CMAKE_BINARY_DIR}/_deps")
	set(_pkg_src "${WZ_EMDAWNWEBGPU_PKG_DIR}")
	if(NOT _pkg_src)
		set(_download_dir "${_deps_dir}/emdawnwebgpu-${WZ_EMDAWNWEBGPU_VERSION}")
		set(_pkg_src "${_download_dir}/emdawnwebgpu_pkg")
		if(NOT EXISTS "${_pkg_src}/emdawnwebgpu.port.py")
			set(_zip "${_download_dir}/emdawnwebgpu_pkg-${WZ_EMDAWNWEBGPU_VERSION}.zip")
			file(DOWNLOAD "https://github.com/google/dawn/releases/download/${WZ_EMDAWNWEBGPU_VERSION}/emdawnwebgpu_pkg-${WZ_EMDAWNWEBGPU_VERSION}.zip"
				"${_zip}" EXPECTED_HASH SHA256=${WZ_EMDAWNWEBGPU_SHA256} STATUS _status)
			list(GET _status 0 _status_code)
			if(NOT _status_code EQUAL 0)
				list(GET _status 1 _status_message)
				message(FATAL_ERROR "Failed to download emdawnwebgpu ${WZ_EMDAWNWEBGPU_VERSION}: ${_status_message}")
			endif()
			file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${_download_dir}")
		endif()
	endif()
	if(NOT EXISTS "${_pkg_src}/emdawnwebgpu.port.py")
		message(FATAL_ERROR "No emdawnwebgpu.port.py in the emdawnwebgpu package folder: ${_pkg_src}")
	endif()

	# The port file finds the rest of the package next to itself, so copy all of it.
	# The patched files are written only if they change, so that a reconfigure does not relink.
	set(_pkg_out "${_deps_dir}/emdawnwebgpu-${WZ_EMDAWNWEBGPU_VERSION}-patched")
	set(_patched_files "emdawnwebgpu.port.py" "webgpu/src/library_webgpu.js")
	file(COPY "${_pkg_src}/" DESTINATION "${_pkg_out}"
		PATTERN "emdawnwebgpu.port.py" EXCLUDE
		PATTERN "library_webgpu.js" EXCLUDE)
	set(_old_text_emdawnwebgpu.port.py "if settings.USE_WEBGPU:")
	set(_new_text_emdawnwebgpu.port.py "if getattr(settings, 'USE_WEBGPU', False):")
	set(_old_text_webgpu/src/library_webgpu.js "if (USE_WEBGPU ||")
	set(_new_text_webgpu/src/library_webgpu.js "if ((typeof USE_WEBGPU != 'undefined' && USE_WEBGPU) ||")
	foreach(_file IN LISTS _patched_files)
		file(READ "${_pkg_src}/${_file}" _content)
		string(FIND "${_content}" "${_old_text_${_file}}" _found)
		if(_found EQUAL -1)
			# A newer package may have fixed this itself
			message(WARNING "emdawnwebgpu ${_file} does not have \"${_old_text_${_file}}\"; not patched")
		else()
			string(REPLACE "${_old_text_${_file}}" "${_new_text_${_file}}" _content "${_content}")
		endif()
		set(_existing "")
		if(EXISTS "${_pkg_out}/${_file}")
			file(READ "${_pkg_out}/${_file}" _existing)
		endif()
		if(NOT _existing STREQUAL _content)
			file(WRITE "${_pkg_out}/${_file}" "${_content}")
		endif()
	endforeach()

	set(${OUT_PORT_FILE} "${_pkg_out}/emdawnwebgpu.port.py" PARENT_SCOPE)
endfunction()

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

	wz_prepare_emdawnwebgpu_port(_wz_emdawnwebgpu_port)
	add_library(wz_emdawnwebgpu INTERFACE)
	target_compile_options(wz_emdawnwebgpu INTERFACE "--use-port=${_wz_emdawnwebgpu_port}")
	target_link_options(wz_emdawnwebgpu INTERFACE "--use-port=${_wz_emdawnwebgpu_port}"
		# Creates the WebGPU device before the game starts (WebGPU device creation is asynchronous).
		# --pre-js, because it adds a preRun step: Emscripten 6 runs --post-js code after the start.
		"SHELL:--pre-js ${CMAKE_CURRENT_LIST_DIR}/../platforms/emscripten/webgpu-postjs.js")
	add_library(dawn::webgpu_dawn ALIAS wz_emdawnwebgpu)
	set(WZ_DAWN_FOUND TRUE)
	message(STATUS "Dawn WebGPU: Emscripten emdawnwebgpu port ${WZ_EMDAWNWEBGPU_VERSION} (host tools: ${WZ_WEBGPU_HOST_TOOLS_DIR})")
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
