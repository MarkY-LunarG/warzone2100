#!/usr/bin/env bash
# Export the Vulkan SC pipeline JSON of a graphics profile from the desktop Vulkan build.
#
# Usage: export_pipelines.sh [-B <build dir>] [-p <profile>] [-o <output dir>] [-c]
#
#   -B <dir>     Build directory with src/warzone2100. The default is "build".
#   -p <profile> Graphics profile: tools/vulkansc/profile_<profile>.cfg (the game settings) and
#                profile_<profile>.env (environment variables, if it exists). The default is "high".
#   -o <dir>     Output directory. The default is "data/vulkansc/<profile>".
#                The old documents and manifest there are removed first, so that a pipeline the
#                game no longer builds does not stay behind.
#   -c           Check only: export into a temporary directory and compare it with the output
#                directory. Exits with 1 if they differ. Run it after a change to the pipelines,
#                the shaders, the render graph, or the profile.
#   -h           Show this help.
#
# Each export session is an autogame (it needs no input and quits by itself), run with a new
# config directory that holds only the profile, and WZ_VULKAN_EXPORT_PIPELINES set to the output
# directory. Each session adds the pipelines it reaches to the directory and to its manifest,
# wz2100_pipelines.json. The Vulkan backend must be able to start: on Linux, SDL must find a
# display.
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${SRC_DIR}/build"
PROFILE="high"
OUTPUT_DIR=""
CHECK=0

# The sessions: a skirmish settings file in data/mp/tests, then any more game arguments after "|".
# Add a session when the Vulkan SC build logs "could not create pipeline ... from the pipeline cache".
# The last one watches the long battle with the automatic camera, so that the fighting is on screen:
# health, reload, and build progress bars draw only then (the batched rectangles, "Rect instanced").
SESSIONS=(
	"highground.json"
	"longbattle.json"
	"longbattle.json|--autocamera --autogamespeed=10"
)

usage()
{
	sed -n '2,24s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":B:p:o:ch" opt; do
	case "${opt}" in
		B) BUILD_DIR="$(realpath -m "${OPTARG}")" ;;
		p) PROFILE="${OPTARG}" ;;
		o) OUTPUT_DIR="$(realpath -m "${OPTARG}")" ;;
		c) CHECK=1 ;;
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

GAME="${BUILD_DIR}/src/warzone2100"
PROFILE_FILE="${SRC_DIR}/tools/vulkansc/profile_${PROFILE}.cfg"
PROFILE_ENV_FILE="${SRC_DIR}/tools/vulkansc/profile_${PROFILE}.env"
OUTPUT_DIR="${OUTPUT_DIR:-${SRC_DIR}/data/vulkansc/${PROFILE}}"
if [[ ! -x "${GAME}" ]]; then
	echo "error: ${GAME} not found. Build the desktop Vulkan backend first." >&2
	exit 1
fi
if [[ ! -f "${PROFILE_FILE}" ]]; then
	echo "error: ${PROFILE_FILE} not found" >&2
	exit 1
fi

if [[ -f "${PROFILE_ENV_FILE}" ]]; then
	set -a
	# shellcheck disable=SC1090
	source "${PROFILE_ENV_FILE}"
	set +a
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

EXPORT_DIR="${OUTPUT_DIR}"
if [[ ${CHECK} -eq 1 ]]; then
	EXPORT_DIR="${WORK_DIR}/export"
fi
mkdir -p "${EXPORT_DIR}"
rm -f "${EXPORT_DIR}"/wz2100.pc.*.json "${EXPORT_DIR}/wz2100_pipelines.json"

session_index=0
for session in "${SESSIONS[@]}"; do
	session_index=$((session_index + 1))
	skirmish="${session%%|*}"
	read -r -a extra_args <<< "$( [[ "${session}" == *"|"* ]] && echo "${session#*|}" )"
	config_dir="${WORK_DIR}/config_${session_index}"
	log="${WORK_DIR}/session_${session_index}.log"
	mkdir -p "${config_dir}"
	cp "${PROFILE_FILE}" "${config_dir}/config"
	echo "==> ${skirmish} ${extra_args[*]} (profile ${PROFILE})"
	if ! WZ_VULKAN_EXPORT_PIPELINES="${EXPORT_DIR}" "${GAME}" "--configdir=${config_dir}" \
		"--skirmish=${skirmish}" --autogame --gfxbackend=vulkan "${extra_args[@]}" > "${log}" 2>&1; then
		echo "error: the session failed; its log:" >&2
		tail -20 "${log}" >&2
		exit 1
	fi
	if ! grep -q "Pipeline JSON export: .* pipelines in this run" "${log}"; then
		echo "error: the session wrote no pipeline JSON. Did it use the Vulkan backend? Its log:" >&2
		tail -20 "${log}" >&2
		exit 1
	fi
	grep -o "Pipeline JSON export: .*" "${log}"
done

if [[ ${CHECK} -eq 0 ]]; then
	echo "==> Wrote ${OUTPUT_DIR}"
	exit 0
fi

# Compare as parsed JSON, so that formatting does not matter.
python3 -I - "${OUTPUT_DIR}" "${EXPORT_DIR}" <<'PYEOF'
import glob
import json
import os
import sys

committed, exported = sys.argv[1], sys.argv[2]

def documents(directory):
    files = glob.glob(os.path.join(directory, "wz2100.pc.*.json")) + glob.glob(os.path.join(directory, "wz2100_pipelines.json"))
    return {os.path.basename(path): json.load(open(path)) for path in files}

old, new = documents(committed), documents(exported)
differences = 0
for name in sorted(set(old) - set(new)):
    print(f"only in {committed}: {name}")
    differences += 1
for name in sorted(set(new) - set(old)):
    print(f"only in the new export: {name}")
    differences += 1
for name in sorted(set(old) & set(new)):
    if old[name] != new[name]:
        print(f"differs: {name}")
        differences += 1
if differences:
    print(f"{differences} difference(s): run tools/vulkansc/export_pipelines.sh without -c, and commit the result")
    sys.exit(1)
print(f"No differences ({len(new)} files)")
PYEOF
