#!/usr/bin/env bash
# Play a Warzone 2100 replay (.wzrp) file with the build from this repository.
#
# Usage: replay_warzone2100.sh [-b] [-B <dir>] <file.wzrp> [-- <game args>]
#
#   -b          Browser version: not supported. The browser keeps its files in its own
#               storage, so this script cannot give it a replay file.
#   -B <dir>    Build directory. The default is "build" in the repository.
#   -h          Show this help.
#
# Arguments after "--" go to the game without change, for example:
#   replay_warzone2100.sh match.wzrp -- --gfxbackend=vulkan
#
# The game reads replays only from the "replay" folder of its config
# directory, and it does not follow symbolic links there. Thus this script
# copies a replay from a different location to
# build_userdata/replay/imported/ before it starts the game. A replay that is
# already in build_userdata/replay/ is used where it is.
#
# A replay plays correctly only in a build with the same netcode version as
# the build that recorded it. At the end of the replay, press ESC to quit.
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPLAY_DIR="${SRC_DIR}/build_userdata/replay"
RUN_ARGS=()

usage()
{
	sed -n '2,22s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":bB:h" opt; do
	case "${opt}" in
		b)
			echo "error: -b (the browser version) is not supported for replays: the browser keeps its files in its own storage" >&2
			exit 1
			;;
		B) RUN_ARGS+=(-B "${OPTARG}") ;;
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

if [[ $# -lt 1 || "$1" == "--" ]]; then
	echo "error: give the path of a .wzrp replay file" >&2
	usage >&2
	exit 1
fi
REPLAY_FILE="$1"
shift
if [[ "${1:-}" == "--" ]]; then
	shift
fi

if [[ ! -f "${REPLAY_FILE}" ]]; then
	echo "error: replay file '${REPLAY_FILE}' not found" >&2
	exit 1
fi
if [[ "${REPLAY_FILE}" != *.wzrp ]]; then
	echo "error: '${REPLAY_FILE}' is not a .wzrp file" >&2
	exit 1
fi

REPLAY_PATH="$(realpath "${REPLAY_FILE}")"

# The game wants the replay name relative to the replay folder.
if [[ "${REPLAY_PATH}" == "${REPLAY_DIR}/"* ]]; then
	REPLAY_NAME="${REPLAY_PATH#"${REPLAY_DIR}/"}"
else
	mkdir -p "${REPLAY_DIR}/imported"
	REPLAY_NAME="imported/$(basename "${REPLAY_PATH}")"
	cp -f -- "${REPLAY_PATH}" "${REPLAY_DIR}/${REPLAY_NAME}"
	echo "==> Copied replay to ${REPLAY_DIR}/${REPLAY_NAME}"
fi

exec "${SRC_DIR}/run_warzone2100.sh" "${RUN_ARGS[@]}" -- "--loadreplay=${REPLAY_NAME}" "$@"
