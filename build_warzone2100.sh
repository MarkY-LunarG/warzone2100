#!/usr/bin/env bash
# Configure and build Warzone 2100 with CMake, as the "How to build" section
# of README.md tells.
#
# Usage: build_warzone2100.sh [-c] [-t <type>] [-w] [-b] [-s] [-B <dir>] [-H <dir>] [-E <dir>] [-j <jobs>] [-- <cmake args>]
#
#   -c          Remove the build directory before the build (clobber).
#   -t <type>   Build type: debug, relwithdebinfo (default), or release.
#   -w          Force WebGPU backend support (WZ_ENABLE_BACKEND_WEBGPU=ON).
#   -b          Build the browser version (Emscripten) with WebGPU, in "build_web".
#   -s          Build the Vulkan SC version (WZ_ENABLE_BACKEND_VULKANSC=ON), in
#               "build_vulkansc". Set VULKANSC_INCLUDE (or VULKANSC_SDK) to the Vulkan SC
#               headers, and VULKANSC_PCC to the pipeline cache compiler (pcconvk).
#   -B <dir>    Build directory. The default is "build" ("build_web" with -b,
#               "build_vulkansc" with -s).
#   -H <dir>    With -b: the native build that gives the host shader tools. The default
#               is "build". The script builds the tools there if they are missing.
#   -E <dir>    With -b: the Emscripten SDK. The default is build_emsdk, $EMSDK, ../emsdk, or ~/emsdk.
#   -j <jobs>   Number of parallel build jobs. The default is the CPU count.
#   -h          Show this help.
#
# Arguments after "--" go to the CMake configure step without change, for
# example: build_warzone2100.sh -- -DWZ_PERF_COUNTERS=ON
#
# With -b, vcpkg and the libraries that it builds for the browser are in
# "build_vcpkg", so that -c does not make the script build them again.
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR=""
BUILD_TYPE="RelWithDebInfo"
CLOBBER=0
WEBGPU=0
BROWSER=0
VULKANSC=0
HOST_TOOLS_DIR="${SRC_DIR}/build"
EMSDK_DIR=""
JOBS=""
WEBGPU_OPTION="WZ_ENABLE_BACKEND_WEBGPU"

usage()
{
	sed -n '2,26s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":ct:wbsB:H:E:j:h" opt; do
	case "${opt}" in
		c) CLOBBER=1 ;;
		t)
			case "${OPTARG,,}" in
				debug) BUILD_TYPE="Debug" ;;
				relwithdebinfo) BUILD_TYPE="RelWithDebInfo" ;;
				release) BUILD_TYPE="Release" ;;
				*)
					echo "error: unknown build type '${OPTARG}'. Use debug, relwithdebinfo, or release." >&2
					exit 1
					;;
			esac
			;;
		w) WEBGPU=1 ;;
		b) BROWSER=1 ;;
		s) VULKANSC=1 ;;
		B) BUILD_DIR="$(realpath -m "${OPTARG}")" ;;
		H) HOST_TOOLS_DIR="$(realpath -m "${OPTARG}")" ;;
		E) EMSDK_DIR="$(realpath -m "${OPTARG}")" ;;
		j) JOBS="${OPTARG}" ;;
		h)
			usage
			exit 0
			;;
		:)
			echo "error: option -${OPTARG} needs a value" >&2
			exit 1
			;;
		*)
			echo "error: unknown option -${OPTARG}" >&2
			usage >&2
			exit 1
			;;
	esac
done
shift $((OPTIND - 1))
if [[ "${1:-}" == "--" ]]; then
	shift
fi
EXTRA_CMAKE_ARGS=("$@")

if [[ -z "${BUILD_DIR}" ]]; then
	if [[ ${BROWSER} -eq 1 ]]; then
		BUILD_DIR="${SRC_DIR}/build_web"
	elif [[ ${VULKANSC} -eq 1 ]]; then
		BUILD_DIR="${SRC_DIR}/build_vulkansc"
	else
		BUILD_DIR="${SRC_DIR}/build"
	fi
fi

BUILD_ARGS_PARALLEL=(--parallel)
if [[ -n "${JOBS}" ]]; then
	BUILD_ARGS_PARALLEL=(--parallel "${JOBS}")
fi

# The build fails without the submodules (README.md, "Getting the Source").
# "git submodule status" puts a "-" before each submodule that is not initialized.
if [[ -e "${SRC_DIR}/.git" ]] && git -C "${SRC_DIR}" submodule status --recursive | grep -q '^-'; then
	echo "==> Initializing git submodules"
	git -C "${SRC_DIR}" submodule update --init --recursive
fi

if [[ ${CLOBBER} -eq 1 && -d "${BUILD_DIR}" ]]; then
	# Do not remove the source tree or a parent of it by mistake.
	if [[ "${BUILD_DIR}" == "/" || "${SRC_DIR}/" == "${BUILD_DIR}/"* ]]; then
		echo "error: refusing to clobber '${BUILD_DIR}'" >&2
		exit 1
	fi
	echo "==> Removing ${BUILD_DIR}"
	rm -rf -- "${BUILD_DIR}"
fi

CMAKE_ARGS=(-S "${SRC_DIR}" -B "${BUILD_DIR}" "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")

# Use Ninja when it is available, as README.md does. CMake cannot change the
# generator of a build directory that exists, so use Ninja only for a new one.
NINJA="$(command -v ninja || true)"
if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" && -n "${NINJA}" ]]; then
	CMAKE_ARGS+=(-G Ninja)
fi

if [[ ${BROWSER} -eq 1 ]]; then
	# 1. The host shader tools come from a native WebGPU build (see cmake/WZDawn.cmake)
	if [[ ! -x "${HOST_TOOLS_DIR}/_deps/dawn-build/tint" || ! -x "${HOST_TOOLS_DIR}/build_tools/wz_webgpu_spirv_prep" ]]; then
		echo "==> Building the host shader tools in ${HOST_TOOLS_DIR}"
		HOST_ARGS=(-S "${SRC_DIR}" -B "${HOST_TOOLS_DIR}" "-D${WEBGPU_OPTION}=ON")
		if [[ ! -f "${HOST_TOOLS_DIR}/CMakeCache.txt" && -n "${NINJA}" ]]; then
			HOST_ARGS+=(-G Ninja "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
		fi
		cmake "${HOST_ARGS[@]}"
		cmake --build "${HOST_TOOLS_DIR}" "${BUILD_ARGS_PARALLEL[@]}" --target tint wz_webgpu_spirv_prep
	fi

	# 2. The Emscripten SDK
	if [[ -z "${EMSDK_DIR}" ]]; then
		# build_emsdk: the Emscripten version that the WZ CI uses (4.0.10), installed in the repository
		for candidate in "${SRC_DIR}/build_emsdk" "${EMSDK:-}" "${SRC_DIR}/../emsdk" "${HOME}/emsdk"; do
			if [[ -n "${candidate}" && -f "${candidate}/emsdk_env.sh" ]]; then
				EMSDK_DIR="$(realpath "${candidate}")"
				break
			fi
		done
	fi
	if [[ -z "${EMSDK_DIR}" || ! -f "${EMSDK_DIR}/emsdk_env.sh" ]]; then
		echo "error: Emscripten SDK not found. Set EMSDK or use -E <dir>." >&2
		exit 1
	fi
	# shellcheck disable=SC1091
	source "${EMSDK_DIR}/emsdk_env.sh" > /dev/null 2>&1
	echo "==> Using $(emcc --version | head -1)"

	# 3. vcpkg, which builds the libraries for the browser (README-build.md in platforms/emscripten)
	VCPKG_ROOT_DIR="${SRC_DIR}/build_vcpkg"
	if [[ ! -x "${VCPKG_ROOT_DIR}/vcpkg/vcpkg" ]]; then
		echo "==> Getting vcpkg in ${VCPKG_ROOT_DIR}"
		mkdir -p "${VCPKG_ROOT_DIR}"
		if [[ ! -d "${VCPKG_ROOT_DIR}/vcpkg/.git" ]]; then
			git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT_DIR}/vcpkg"
		fi
		"${VCPKG_ROOT_DIR}/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
	fi

	CMAKE_ARGS+=(
		"-DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
		"-DVCPKG_TARGET_TRIPLET=wasm32-emscripten"
		"-DVCPKG_INSTALLED_DIR=${VCPKG_ROOT_DIR}/installed"
		"-D${WEBGPU_OPTION}=ON"
		"-DWZ_WEBGPU_HOST_TOOLS_DIR=${HOST_TOOLS_DIR}"
		"-DCMAKE_INSTALL_PREFIX:PATH=${BUILD_DIR}/installed"
	)
	# The emsdk environment can hide the system Ninja from CMake
	if [[ -n "${NINJA}" ]]; then
		CMAKE_ARGS+=("-DCMAKE_MAKE_PROGRAM=${NINJA}")
	fi
elif [[ ${WEBGPU} -eq 1 ]]; then
	CMAKE_ARGS+=("-D${WEBGPU_OPTION}=ON")
else
	# Remove a value that an earlier -w run put in the cache, so that the
	# project default applies.
	CMAKE_ARGS+=("-U${WEBGPU_OPTION}")
fi

if [[ ${VULKANSC} -eq 1 ]]; then
	if [[ -z "${VULKANSC_INCLUDE:-}" && -n "${VULKANSC_SDK:-}" ]]; then
		VULKANSC_INCLUDE="${VULKANSC_SDK}/include"
	fi
	if [[ ! -f "${VULKANSC_INCLUDE:-}/vulkan/vulkan_sc.h" ]]; then
		echo "error: -s needs VULKANSC_INCLUDE, the include directory of VulkanSC-Headers" >&2
		exit 1
	fi
	if [[ ! -x "${VULKANSC_PCC:-}" ]]; then
		echo "error: -s needs VULKANSC_PCC, the pipeline cache compiler (VulkanSC-Emulation/build/pcc/pcconvk)" >&2
		exit 1
	fi
	CMAKE_ARGS+=(-DWZ_ENABLE_BACKEND_VULKANSC=ON "-DWZ_VULKANSC_INCLUDE_DIR=${VULKANSC_INCLUDE}"
		"-DWZ_VULKANSC_PCC=${VULKANSC_PCC}")
fi

echo "==> Configuring (${BUILD_TYPE}) in ${BUILD_DIR}"
cmake "${CMAKE_ARGS[@]}" "${EXTRA_CMAKE_ARGS[@]}"

echo "==> Building"
cmake --build "${BUILD_DIR}" "${BUILD_ARGS_PARALLEL[@]}"

if [[ ${BROWSER} -eq 1 ]]; then
	echo "==> Done: ${BUILD_DIR} (run it with: run_warzone2100.sh -b)"
else
	echo "==> Done: ${BUILD_DIR}/src/warzone2100"
fi
