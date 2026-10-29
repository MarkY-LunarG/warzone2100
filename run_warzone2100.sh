#!/usr/bin/env bash
# Run the Warzone 2100 build from this repository. The game keeps its config,
# saves, logs, and videos in "build_userdata" in the repository, not in
# ~/.local/share.
#
# Usage: run_warzone2100.sh [-b] [-B <dir>] [-p <page>] [-v] [-W <browser>] [-E <dir>] [-- <game args>]
#
#   -b          Run the browser version (build_warzone2100.sh -b). emrun serves the page
#               with the headers that threads need, and the script opens the browser
#               and waits for it. When the browser exits, the script stops emrun.
#               Firefox starts with its own profile in build_userdata/firefox_profile,
#               which enables WebGPU (dom.webgpu.enabled is off by default on Linux).
#               The browser keeps the config and saves in that profile.
#               The game log goes to the terminal through the page console.
#               Game arguments after "--" go into the page URL as query parameters.
#               A build linked with --emrun reads its arguments only from there.
#   -B <dir>    Build directory. The default is "build" ("build_web" with -b).
#   -p <page>   With -b: the page to open, relative to "<build dir>/src". The default is
#               "index.html". autogame_warzone2100.sh uses this for its generated page.
#   -v          Download the campaign videos (approximately 570 MB) to the
#               user data directory if they are not there, then run the game.
#   -W <browser> With -b: the browser command. The default is "firefox". The browser
#               must stay running until its window closes, or the script stops early.
#   -E <dir>    With -b: the Emscripten SDK. The default is build_emsdk, $EMSDK, ../emsdk, or ~/emsdk.
#   -h          Show this help.
#
# Arguments after "--" go to the game without change, for example:
#   run_warzone2100.sh -- --gfxbackend=vulkan
#
# The "build_userdata" directory is outside the build directory, so
# "build_warzone2100.sh -c" does not remove it. The /build_*/ rule in
# .gitignore keeps it out of git.
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR=""
USER_DATA_DIR="${SRC_DIR}/build_userdata"
GET_VIDEOS=0
BROWSER=0
BROWSER_NAME="firefox"
PAGE_NAME="index.html"
EMSDK_DIR=""
VIDEOS_URL="https://github.com/Warzone2100/wz-sequences/releases/download/v3/standard-quality-en-sequences.wz"

usage()
{
	sed -n '2,32s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":bB:p:vW:E:h" opt; do
	case "${opt}" in
		b) BROWSER=1 ;;
		B) BUILD_DIR="$(realpath -m "${OPTARG}")" ;;
		p) PAGE_NAME="${OPTARG}" ;;
		v) GET_VIDEOS=1 ;;
		W) BROWSER_NAME="${OPTARG}" ;;
		E) EMSDK_DIR="$(realpath -m "${OPTARG}")" ;;
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

if [[ ${BROWSER} -eq 1 ]]; then
	BUILD_DIR="${BUILD_DIR:-${SRC_DIR}/build_web}"
	PAGE="${BUILD_DIR}/src/${PAGE_NAME}"
	if [[ ! -f "${PAGE}" ]]; then
		echo "error: ${PAGE} not found. Run build_warzone2100.sh -b first." >&2
		exit 1
	fi
	if [[ ${GET_VIDEOS} -eq 1 ]]; then
		echo "warning: -v has no effect with -b (it downloads to build_userdata, which the browser does not use)" >&2
	fi
	if [[ -z "${EMSDK_DIR}" ]]; then
		# build_emsdk: the Emscripten version that the WZ CI uses (4.0.10), installed in the repository
		for candidate in "${SRC_DIR}/build_emsdk" "${EMSDK:-}" "${SRC_DIR}/../emsdk" "${HOME}/emsdk"; do
			if [[ -n "${candidate}" && -f "${candidate}/emsdk_env.sh" ]]; then
				EMSDK_DIR="$(realpath "${candidate}")"
				break
			fi
		done
	fi
	EMRUN="${EMSDK_DIR}/upstream/emscripten/emrun"
	if [[ ! -x "${EMRUN}" ]]; then
		echo "error: emrun not found. Set EMSDK or use -E <dir>." >&2
		exit 1
	fi
	BROWSER_ARGS=()
	if [[ "${BROWSER_NAME}" == *firefox* ]]; then
		# Firefox on Linux ships with WebGPU off. Without it, the page falls back to
		# WebGL and shows a black canvas. A separate profile turns WebGPU on and keeps
		# the game's browser storage apart from the normal browsing profile.
		# -new-instance starts a second Firefox even when one is already open.
		FIREFOX_PROFILE="${USER_DATA_DIR}/firefox_profile"
		mkdir -p "${FIREFOX_PROFILE}"
		cat > "${FIREFOX_PROFILE}/user.js" <<'EOF'
// Written by run_warzone2100.sh on each run. Firefox reads this file at startup.
user_pref("dom.webgpu.enabled", true);
user_pref("gfx.webgpu.ignore-blocklist", true);
// Print the page console, which has the game log, to the terminal that runs emrun.
user_pref("devtools.console.stdout.content", true);
// Let the autogame page close its window when the game ends, so that emrun stops.
user_pref("dom.allow_scripts_to_close_windows", true);
// Do not ask to be the default browser, and do not restore the last session.
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.sessionstore.resume_from_crash", false);
EOF
		BROWSER_ARGS+=(-profile "${FIREFOX_PROFILE}" -no-remote -new-instance)
	fi
	# The game arguments go into the URL query string as "?arg1&arg2", the form that
	# emrun itself uses for arguments after the page.
	PORT=6931
	URL="http://localhost:${PORT}/${PAGE_NAME}"
	if [[ $# -gt 0 ]]; then
		URL="${URL}?$(IFS='&'; echo "$*")"
	fi
	# emrun serves in the background and the script opens the browser itself. emrun
	# cannot see that the browser has exited without the Python module psutil, so with
	# "emrun --browser" it would serve forever. The EXIT trap stops it on Ctrl+C too.
	echo "==> Serving ${BUILD_DIR}/src with emrun on port ${PORT}. Press Ctrl+C to stop."
	"${EMRUN}" --no-browser --port "${PORT}" --serve-root "${BUILD_DIR}/src" "${PAGE}" &
	EMRUN_PID=$!
	trap 'kill "${EMRUN_PID}" 2>/dev/null' EXIT
	sleep 1
	if ! kill -0 "${EMRUN_PID}" 2>/dev/null; then
		echo "error: emrun did not start. Is port ${PORT} in use by another run?" >&2
		exit 1
	fi
	echo "==> Opening ${URL} in ${BROWSER_NAME}"
	"${BROWSER_NAME}" "${BROWSER_ARGS[@]}" "${URL}"
	exit 0
fi

BUILD_DIR="${BUILD_DIR:-${SRC_DIR}/build}"
GAME="${BUILD_DIR}/src/warzone2100"
if [[ ! -x "${GAME}" ]]; then
	echo "error: ${GAME} not found. Run build_warzone2100.sh first." >&2
	exit 1
fi

mkdir -p "${USER_DATA_DIR}"

if [[ ${GET_VIDEOS} -eq 1 && ! -f "${USER_DATA_DIR}/sequences.wz" ]]; then
	echo "==> Downloading campaign videos to ${USER_DATA_DIR}/sequences.wz"
	# Download to a temporary name, so that an interrupted download does not
	# leave a damaged sequences.wz. If a partial file is there, continue it.
	curl -fL -C - --progress-bar -o "${USER_DATA_DIR}/sequences.wz.part" "${VIDEOS_URL}"
	mv "${USER_DATA_DIR}/sequences.wz.part" "${USER_DATA_DIR}/sequences.wz"
fi

exec "${GAME}" "--configdir=${USER_DATA_DIR}" "$@"
