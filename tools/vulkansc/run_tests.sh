#!/usr/bin/env bash
# Tests of the Vulkan SC port: the desktop Vulkan backend, the Vulkan SC build on the VulkanSC-Emulation
# ICD, and a comparison of the frames the two render.
#
# Usage: run_tests.sh [-B <dir>] [-S <dir>] [-d] [-k] [-h]
#
#   -B <dir>  The desktop Vulkan build. The default is "build".
#   -S <dir>  The Vulkan SC build (WZ_ENABLE_BACKEND_VULKANSC, with a pipeline cache). The default is
#             "build_vulkansc".
#   -d        Also run the pipeline JSON drift check (tools/vulkansc/export_pipelines.sh -c), which takes
#             about 3 minutes. Its result depends on what the autogames happen to draw: run it again
#             before treating a difference as a change.
#   -k        Keep the work directory, with the logs and screenshots.
#   -h        Show this help.
#
# Environment:
#   VULKANSC_ICD_JSON  The emulation ICD's manifest (VulkanSC-Emulation/build/<type>/icd/vksconvk.json).
#                      Without it, the Vulkan SC tests are skipped.
#   VULKANSC_LOADER    The Vulkan SC loader (VulkanSC-Loader/build/<type>/loader/libvulkansc.so.1).
#                      The default is the system's.
#   GFXR_VULKANSC_BUILD  A Premium-GFXR build with Vulkan SC (GFXRECON_ENABLE_VULKANSC=ON). With it, the
#                      Vulkan SC build is also captured through the Vulkan SC capture layer, the capture is
#                      replayed, and gfxrecon-premium-info checks it (no faults, no error messages, every
#                      pipeline cache source). Needs VULKANSC_LOADER: the layer forwards to the loader.
#
# Every test is an autogame with the high graphics profile (tools/vulkansc/profile_high.cfg), in a new
# config directory. The Vulkan SC tests run the desktop validation layer under the emulation ICD.
# A test passes when the game plays to the end with no validation message, no error, no Vulkan SC
# fault, no pipeline missing from the pipeline cache, and no Vulkan object alive at shutdown.
# The emulation ICD's known VUID-VkPhysicalDeviceProperties2-pNext-pNext message is allowed: it passes
# VkPhysicalDeviceVulkanSC10Properties to the desktop driver.
set -uo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DESKTOP_BUILD="${SRC_DIR}/build"
SC_BUILD="${SRC_DIR}/build_vulkansc"
DRIFT_CHECK=0
KEEP=0
PROFILE_FILE="${SRC_DIR}/tools/vulkansc/profile_high.cfg"
PROFILE_ENV_FILE="${SRC_DIR}/tools/vulkansc/profile_high.env"
# The frame the image comparison takes, and its limit: the share of pixels where a channel differs by
# more than 16. Two runs of the same build differ in about 1 to 2% (moving units, timing).
# The comparison runs play at normal speed (at the autogame's 500 times, game time depends on how fast
# frames are drawn) without validation, and stop after COMPARE_SECONDS: the battle is long at normal speed.
COMPARE_FRAME=350
COMPARE_LIMIT_PERCENT=4
COMPARE_SECONDS=60

usage()
{
	sed -n '2,31s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":B:S:dkh" opt; do
	case "${opt}" in
		B) DESKTOP_BUILD="$(realpath -m "${OPTARG}")" ;;
		S) SC_BUILD="$(realpath -m "${OPTARG}")" ;;
		d) DRIFT_CHECK=1 ;;
		k) KEEP=1 ;;
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

WORK_DIR="$(mktemp -d)"
if [[ ${KEEP} -eq 0 ]]; then
	trap 'rm -rf "${WORK_DIR}"' EXIT
fi
FAILURES=0
SWAPCHAIN_FORMAT="$(sed -n 's/^WZ_VULKAN_FORCE_SWAPCHAIN_FORMAT=//p' "${PROFILE_ENV_FILE}" 2>/dev/null)"

# run_game <name> <game binary> [env assignments...] -- [game arguments...]
run_game()
{
	local name="$1" game="$2"
	shift 2
	local env_vars=()
	while [[ $# -gt 0 && "$1" != "--" ]]; do
		env_vars+=("$1")
		shift
	done
	shift
	mkdir -p "${WORK_DIR}/${name}"
	cp "${PROFILE_FILE}" "${WORK_DIR}/${name}/config"
	env "${env_vars[@]}" timeout "${RUN_SECONDS:-900}" "${game}" "--configdir=${WORK_DIR}/${name}" --autogame "$@" > "${WORK_DIR}/${name}.log" 2>&1
	echo $? > "${WORK_DIR}/${name}.exit"
}

# check_log <name> <description>: prints the result line, and counts a failure
check_log()
{
	local name="$1" description="$2"
	if python3 -I - "${WORK_DIR}/${name}.log" "$(cat "${WORK_DIR}/${name}.exit")" "${description}" <<'PYEOF'
import re
import sys

log_path, exit_code, description = sys.argv[1], int(sys.argv[2]), sys.argv[3]
lines = open(log_path, errors="replace").read().splitlines()
allowed = re.compile(r"VUID-VkPhysicalDeviceProperties2-pNext-pNext")

problems = []
if exit_code != 0:
    problems.append(f"exit code {exit_code}")
if not any("Game ended" in line for line in lines):
    problems.append("the game did not end")
validation = [l for l in lines if re.search(r"VUID-|Validation Error", l) and not allowed.search(l)]
errors = [l for l in lines if re.match(r"^(error|fatal) ", l) and not allowed.search(l)]
faults = [l for l in lines if "Vulkan SC fault" in l]
missing = [l for l in lines if "could not create pipeline" in l]
emulation = [l for l in lines if "VKSC-EMU-" in l]
for label, found in (("validation messages", validation), ("errors", errors), ("Vulkan SC faults", faults),
                     ("pipelines missing from the cache", missing), ("emulation ICD messages", emulation)):
    if found:
        problems.append(f"{len(found)} {label} (first: {found[0][:160]})")

# The object report: the last column before the reservation field is the count still alive at shutdown
in_report = False
alive = []
for line in lines:
    if "Vulkan objects: peak live" in line:
        in_report = True
        continue
    if in_report:
        match = re.search(r"\]\s+(\w+)\s+(\d+)\s+(\d+)\s+(\d+)\s+\(", line)
        if not match:
            break
        if int(match.group(4)) != 0:
            alive.append(f"{match.group(1)} {match.group(4)}")
if alive:
    problems.append("objects alive at shutdown: " + ", ".join(alive))
if not in_report:
    problems.append("no object report (the Vulkan backend did not run?)")

status = "PASS" if not problems else "FAIL"
print(f"{status}  {description}")
for problem in problems:
    print(f"        {problem}")
sys.exit(0 if not problems else 1)
PYEOF
	then
		return 0
	fi
	FAILURES=$((FAILURES + 1))
	return 1
}

# compare_images <description> <image a> <image b>
compare_images()
{
	local description="$1" image_a="$2" image_b="$3"
	if python3 -I - "${image_a}" "${image_b}" "${COMPARE_LIMIT_PERCENT}" "${description}" <<'PYEOF'
import sys

image_a, image_b, limit, description = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
try:
    from PIL import Image, ImageChops
except ImportError:
    print(f"SKIP  {description}: Python Imaging Library (PIL) is not installed")
    sys.exit(0)
try:
    a = Image.open(image_a).convert("RGB")
    b = Image.open(image_b).convert("RGB")
except OSError as e:
    print(f"FAIL  {description}\n        {e}")
    sys.exit(1)
if a.size != b.size:
    print(f"FAIL  {description}\n        sizes differ: {a.size} and {b.size}")
    sys.exit(1)
difference = ImageChops.difference(a, b)
changed = sum(1 for pixel in difference.getdata() if max(pixel) > 16)
percent = 100.0 * changed / (a.size[0] * a.size[1])
status = "PASS" if percent <= limit else "FAIL"
print(f"{status}  {description}: {percent:.2f}% of pixels differ (limit {limit:g}%)")
sys.exit(0 if status == "PASS" else 1)
PYEOF
	then
		return 0
	fi
	FAILURES=$((FAILURES + 1))
	return 1
}

echo "Work directory: ${WORK_DIR}"

# Desktop Vulkan backend
DESKTOP_GAME="${DESKTOP_BUILD}/src/warzone2100"
if [[ ! -x "${DESKTOP_GAME}" ]]; then
	echo "error: ${DESKTOP_GAME} not found" >&2
	exit 1
fi
echo "==> Desktop Vulkan (${DESKTOP_BUILD})"
run_game desktop "${DESKTOP_GAME}" -- --skirmish=highground.json --gfxbackend=vulkan --gfxdebug --debug=3d
check_log desktop "desktop: highground, validation"
run_game desktop_recreate "${DESKTOP_GAME}" WZ_VULKAN_DEBUG_FORCE_SWAPCHAIN_RECREATE=30 -- --skirmish=highground.json --gfxbackend=vulkan --gfxdebug --debug=3d
check_log desktop_recreate "desktop: highground, swapchain recreated every 30 frames"
run_game desktop_surface_lost "${DESKTOP_GAME}" WZ_VULKAN_DEBUG_FORCE_SURFACE_LOST=45 -- --skirmish=highground.json --gfxbackend=vulkan --gfxdebug --debug=3d
check_log desktop_surface_lost "desktop: highground, surface lost every 45 frames"

# Vulkan SC build on the emulation ICD
SC_GAME="${SC_BUILD}/src/warzone2100"
if [[ -z "${VULKANSC_ICD_JSON:-}" ]]; then
	echo "SKIP  Vulkan SC tests: set VULKANSC_ICD_JSON to the emulation ICD's vksconvk.json"
elif [[ ! -x "${SC_GAME}" ]]; then
	echo "SKIP  Vulkan SC tests: ${SC_GAME} not found"
else
	echo "==> Vulkan SC (${SC_BUILD}) on ${VULKANSC_ICD_JSON}"
	SC_ENV=(VK_DRIVER_FILES="${VULKANSC_ICD_JSON}" WZ_VULKAN_DEBUG_VALIDATION_NONFATAL=1 VKSC_EMU_VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation)
	if [[ -n "${VULKANSC_LOADER:-}" ]]; then
		SC_ENV+=(WZ_VULKANSC_LIBRARY="${VULKANSC_LOADER}")
	fi
	run_game sc_highground "${SC_GAME}" "${SC_ENV[@]}" -- --skirmish=highground.json --gfxbackend=vulkansc --debug=3d
	check_log sc_highground "Vulkan SC: highground, validation underneath"
	run_game sc_longbattle "${SC_GAME}" "${SC_ENV[@]}" -- --skirmish=longbattle.json --gfxbackend=vulkansc --debug=3d
	check_log sc_longbattle "Vulkan SC: long battle, validation underneath"

	# The same frame from both builds: the desktop one at the display mode's size and with its swapchain format
	SC_SIZE="$(sed -n 's/.*Vulkan SC display: .*, mode \([0-9]*x[0-9]*\) at .*/\1/p' "${WORK_DIR}/sc_highground.log" | head -1)"
	if [[ -n "${SC_SIZE}" ]]; then
		SC_FRAME_ENV=(VK_DRIVER_FILES="${VULKANSC_ICD_JSON}")
		if [[ -n "${VULKANSC_LOADER:-}" ]]; then
			SC_FRAME_ENV+=(WZ_VULKANSC_LIBRARY="${VULKANSC_LOADER}")
		fi
		RUN_SECONDS="${COMPARE_SECONDS}" run_game sc_frame "${SC_GAME}" "${SC_FRAME_ENV[@]}" WZ_VULKAN_DEBUG_SCREENSHOT="${COMPARE_FRAME}:frame.png" -- \
			--skirmish=highground.json --gfxbackend=vulkansc --autogamespeed=1
		RUN_SECONDS="${COMPARE_SECONDS}" run_game desktop_frame "${DESKTOP_GAME}" WZ_VULKAN_DEBUG_SCREENSHOT="${COMPARE_FRAME}:frame.png" ${SWAPCHAIN_FORMAT:+WZ_VULKAN_FORCE_SWAPCHAIN_FORMAT=${SWAPCHAIN_FORMAT}} -- \
			--skirmish=highground.json --gfxbackend=vulkan --window "--resolution=${SC_SIZE}" --autogamespeed=1
		compare_images "frame ${COMPARE_FRAME}: Vulkan SC against desktop Vulkan (${SC_SIZE}, normal speed)" "${WORK_DIR}/sc_frame/frame.png" "${WORK_DIR}/desktop_frame/frame.png"
	else
		echo "FAIL  frame comparison: no display mode in the Vulkan SC log"
		FAILURES=$((FAILURES + 1))
	fi

	# Capture and replay with Premium-GFXR. WZ loads the capture layer in place of the loader
	# (WZ_VULKANSC_LIBRARY), and the layer forwards to the loader.
	if [[ -n "${GFXR_VULKANSC_BUILD:-}" && -n "${VULKANSC_LOADER:-}" ]]; then
		CAPTURE_LAYER="${GFXR_VULKANSC_BUILD}/layer/vulkansc/libvulkansc_gfxrecon.so"
		REPLAY="${GFXR_VULKANSC_BUILD}/tools/premium_replay/gfxrecon-premium-replay"
		INFO="${GFXR_VULKANSC_BUILD}/tools/premium_info/gfxrecon-premium-info"
		CAPTURE="${WORK_DIR}/capture.gfxr"
		run_game sc_capture "${SC_GAME}" VK_DRIVER_FILES="${VULKANSC_ICD_JSON}" WZ_VULKANSC_LIBRARY="${CAPTURE_LAYER}" \
			GFXRECON_VULKANSC_DRIVER_PATH="${VULKANSC_LOADER}" GFXRECON_CAPTURE_FILE="${CAPTURE}" GFXRECON_CAPTURE_FILE_TIMESTAMP=false -- \
			--skirmish=highground.json --gfxbackend=vulkansc --debug=3d
		check_log sc_capture "Vulkan SC: highground, captured with the Premium-GFXR Vulkan SC layer"
		VK_DRIVER_FILES="${VULKANSC_ICD_JSON}" GFXRECON_VULKANSC_DRIVER_PATH="${VULKANSC_LOADER}" timeout 900 "${REPLAY}" "${CAPTURE}" > "${WORK_DIR}/replay.log" 2>&1
		replay_exit=$?
		"${INFO}" "${CAPTURE}" > "${WORK_DIR}/capture_info.log" 2>&1
		frames="$(sed -n 's/.*Measured FPS: .* \([0-9]*\) frames,.*/\1/p' "${WORK_DIR}/replay.log" | tail -1)"
		faults="$(sed -n 's/^[[:space:]]*Faults:[[:space:]]*\([0-9]*\).*/\1/p' "${WORK_DIR}/capture_info.log" | head -1)"
		errors="$(sed -n '/Debug Messages:/,/Faults:/s/^[[:space:]]*Error:[[:space:]]*\([0-9]*\).*/\1/p' "${WORK_DIR}/capture_info.log" | head -1)"
		sources="$(sed -n 's/^[[:space:]]*Pipeline Cache Sources:[[:space:]]*\([0-9]*\).*/\1/p' "${WORK_DIR}/capture_info.log" | head -1)"
		pipelines="$(ls "${SRC_DIR}"/data/vulkansc/high/wz2100.pc.*.json 2>/dev/null | wc -l)"
		if [[ ${replay_exit} -eq 0 && -n "${frames}" && "${faults}" == "0" && "${errors}" == "0" && "${sources}" == "${pipelines}" ]]; then
			echo "PASS  Vulkan SC: capture replayed (${frames} frames); no faults or error messages; ${sources} pipeline cache sources"
		else
			echo "FAIL  Vulkan SC: capture and replay"
			echo "        replay exit ${replay_exit}, frames '${frames}', faults '${faults}', error messages '${errors}', pipeline cache sources '${sources}' of ${pipelines}"
			FAILURES=$((FAILURES + 1))
		fi
	elif [[ -n "${GFXR_VULKANSC_BUILD:-}" ]]; then
		echo "SKIP  capture and replay: set VULKANSC_LOADER (the capture layer forwards to it)"
	fi
fi

if [[ ${DRIFT_CHECK} -eq 1 ]]; then
	echo "==> Pipeline JSON drift check"
	if "${SRC_DIR}/tools/vulkansc/export_pipelines.sh" -B "${DESKTOP_BUILD}" -c > "${WORK_DIR}/drift.log" 2>&1; then
		echo "PASS  pipeline JSON: $(tail -1 "${WORK_DIR}/drift.log")"
	else
		echo "FAIL  pipeline JSON drift check (run it again: coverage depends on the autogames):"
		tail -5 "${WORK_DIR}/drift.log" | sed 's/^/        /'
		FAILURES=$((FAILURES + 1))
	fi
fi

if [[ ${KEEP} -eq 1 ]]; then
	echo "Logs and screenshots: ${WORK_DIR}"
fi
if [[ ${FAILURES} -gt 0 ]]; then
	echo "${FAILURES} test(s) failed"
	exit 1
fi
echo "All tests passed"
