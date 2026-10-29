#!/usr/bin/env bash
# Start the automatic test battle that the WebGPU backend work uses, native or in
# the browser. The battle is the skirmish settings file data/mp/tests/highground.json
# (two SemperFi AIs on Sk-HighGround), started with --skirmish and --autogame. The
# game plays it without input and quits when it ends, after approximately 8 seconds
# of game time. It is not a replay file, so it plays the same in every build.
#
# The most common use is the long battle at 6 times normal speed with the automatic
# camera, which follows the fighting:
#   autogame_warzone2100.sh -w -x 6 -s longbattle.json       Desktop (native WebGPU)
#   autogame_warzone2100.sh -b -w -x 6 -s longbattle.json    Browser (Firefox, WebGPU)
# The game quits by itself when the battle ends, after approximately 4 minutes.
#
# Usage: autogame_warzone2100.sh [-b] [-w] [-x <speed>] [-g <backend>] [-s <file.json>] [-n] [-P] [-B <dir>] [-W <browser>] [-- <game args>]
#
#   -b           Browser version. The script makes "autogame.html" next to the built
#                index.html. That page passes the game arguments and clicks "Play Now"
#                itself. Then it serves the page with run_warzone2100.sh -b, which also
#                puts the game arguments in the page URL. A build linked with --emrun
#                ignores the arguments from the page and reads only the URL.
#                When the game ends, the page closes its window, and the script stops
#                emrun and ends by itself, like the native run.
#   -w           Watch mode: normal game speed (--autogamespeed=1) and the automatic
#                camera (--autocamera), which moves the view to the fighting. Without -w,
#                an autogame runs at 500 times normal speed and ends in seconds.
#   -x <speed>   Game speed as a multiple of normal speed. 1 = normal. The default is 500,
#                or 1 with -w.
#   -g <backend> Graphics backend: webgpu (default), vulkan, vulkansc, opengl, opengles,
#                null. vulkansc runs the build in "build_vulkansc" (build_warzone2100.sh -s),
#                and needs the Vulkan SC environment: VK_DRIVER_FILES (the ICD) and, if
#                the Vulkan SC loader is not installed, WZ_VULKANSC_LIBRARY.
#   -s <file>    Skirmish settings file in data/mp/tests. The default is highground.json.
#                longbattle.json is a longer battle: three AIs on Sk-Rush, and the local
#                player is a spectator who sees the whole map.
#   -n           Native only: headless, no window (--headless). The game prints a game
#                summary to stdout.
#   -P           With -b: only write autogame.html and print its path. Do not open a browser.
#   -B <dir>     Build directory. The default is "build" ("build_web" with -b).
#   -W <browser> With -b: the browser command. The default is "firefox".
#   -h           Show this help.
#
# Arguments after "--" go to the game without change, for example:
#   autogame_warzone2100.sh -g vulkan -- --gfxdebug
#
# Examples:
#   autogame_warzone2100.sh                 Native, WebGPU, with a window.
#   autogame_warzone2100.sh -g vulkan       Native, Vulkan.
#   autogame_warzone2100.sh -n              Native, headless, prints the summary.
#   autogame_warzone2100.sh -b              Browser (Firefox), WebGPU.
#   autogame_warzone2100.sh -w -s longbattle.json   Watch the long battle at normal speed.
#   autogame_warzone2100.sh -w -x 4         Watch highground at 4 times normal speed.
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BROWSER=0
BACKEND="webgpu"
SKIRMISH="highground.json"
HEADLESS=0
PREPARE_ONLY=0
WATCH=0
SPEED=""
BUILD_DIR=""
RUN_ARGS=()
PAGE_NAME="autogame.html"

usage()
{
	sed -n '2,51s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":bwx:g:s:nPB:W:h" opt; do
	case "${opt}" in
		b) BROWSER=1 ;;
		w) WATCH=1 ;;
		x) SPEED="${OPTARG}" ;;
		g) BACKEND="${OPTARG}" ;;
		s) SKIRMISH="${OPTARG}" ;;
		n) HEADLESS=1 ;;
		P) PREPARE_ONLY=1 ;;
		B) BUILD_DIR="$(realpath -m "${OPTARG}")" ;;
		W) RUN_ARGS+=(-W "${OPTARG}") ;;
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

case "${BACKEND}" in
	webgpu|vulkan|vulkansc|opengl|opengles|null) ;;
	*)
		echo "error: unknown backend '${BACKEND}' (use webgpu, vulkan, vulkansc, opengl, opengles, or null)" >&2
		exit 1
		;;
esac
if [[ ! -f "${SRC_DIR}/data/mp/tests/${SKIRMISH}" ]]; then
	echo "error: data/mp/tests/${SKIRMISH} not found. The game reads the skirmish file from there." >&2
	exit 1
fi

# The game arguments. --skirmish reads "tests/<file>" from the mp data, so the file
# comes from mp.wz, not from the config directory.
GAME_ARGS=("--skirmish=${SKIRMISH}" "--autogame")
if [[ -n "${SPEED}" && ! "${SPEED}" =~ ^[1-9][0-9]*$ ]]; then
	echo "error: -x needs a whole number of 1 or more, not '${SPEED}'" >&2
	exit 1
fi
if [[ ${WATCH} -eq 1 ]]; then
	SPEED="${SPEED:-1}"
	GAME_ARGS+=("--autocamera")
fi
if [[ -n "${SPEED}" ]]; then
	GAME_ARGS+=("--autogamespeed=${SPEED}")
fi
if [[ ${HEADLESS} -eq 1 && ${BROWSER} -eq 0 ]]; then
	# Headless mode selects the null backend itself.
	GAME_ARGS+=("--headless")
else
	GAME_ARGS+=("--gfxbackend=${BACKEND}")
fi
GAME_ARGS+=("$@")

if [[ ${BROWSER} -eq 0 ]]; then
	if [[ -z "${BUILD_DIR}" && "${BACKEND}" == "vulkansc" ]]; then
		BUILD_DIR="${SRC_DIR}/build_vulkansc"
	fi
	if [[ -n "${BUILD_DIR}" ]]; then
		RUN_ARGS+=(-B "${BUILD_DIR}")
	fi
	echo "==> Native: ${GAME_ARGS[*]}"
	exec "${SRC_DIR}/run_warzone2100.sh" "${RUN_ARGS[@]}" -- "${GAME_ARGS[@]}"
fi

# Browser: the built index.html gives the game a fixed argument list and waits for a
# click on "Play Now". Make a copy that adds the game arguments and clicks the
# button when the page enables it. The copy is in the build directory, so a rebuild
# or build_warzone2100.sh -c removes it. The script makes it again on each run.
BUILD_DIR="${BUILD_DIR:-${SRC_DIR}/build_web}"
INDEX="${BUILD_DIR}/src/index.html"
PAGE="${BUILD_DIR}/src/${PAGE_NAME}"
if [[ ! -f "${INDEX}" ]]; then
	echo "error: ${INDEX} not found. Run build_warzone2100.sh -b first." >&2
	exit 1
fi
if [[ ${HEADLESS} -eq 1 ]]; then
	echo "warning: -n has no effect with -b (the browser always shows the canvas)" >&2
fi
ARGS_JSON="$(python3 -c 'import json, sys; print(json.dumps(sys.argv[1:]))' "${GAME_ARGS[@]}")"
export ARGS_JSON
python3 - "${INDEX}" "${PAGE}" <<'PYEOF'
import os
import sys

index_path, page_path = sys.argv[1], sys.argv[2]
page = open(index_path, encoding="utf-8").read()

# 1. Give the game the extra arguments. constructWZModuleConfig() takes an optional list
#    that it appends to Module.arguments after --configdir.
old_call = "Module = constructWZModuleConfig();"
if page.count(old_call) != 1:
    sys.exit("error: index.html does not have one '%s'; the page layout changed" % old_call)
page = page.replace(old_call, "Module = constructWZModuleConfig(window.WZ_AUTOGAME_ARGS || []);")

# 2. Click "Play Now" as soon as the page shows it enabled. The page shows the
#    launch-game block only after the browser check, and it disables the button
#    while a service worker update is pending.
script = """
  <script>
    window.WZ_AUTOGAME_ARGS = %s;
    (function () {
      var timer = setInterval(function () {
        var launch = document.getElementById('launch-game');
        var button = document.getElementById('start-button');
        if (!launch || !button || launch.style.display !== 'block' || button.disabled || window.wz_starting_game) {
          return;
        }
        clearInterval(timer);
        console.log('autogame: starting the game with ' + JSON.stringify(window.WZ_AUTOGAME_ARGS));
        startGame();
        // The page shows the post-exit block when the game has ended. Close the window
        // then, so that emrun stops (the Firefox profile allows scripts to close windows).
        // The short wait lets the page finish its save to the browser storage.
        var exitTimer = setInterval(function () {
          var postExit = document.getElementById('post-exit');
          if (!postExit || postExit.style.display !== 'block') {
            return;
          }
          clearInterval(exitTimer);
          console.log('autogame: the game has ended; closing the window');
          setTimeout(function () { window.close(); }, 2000);
        }, 250);
      }, 250);
    })();
  </script>
</body>""" % os.environ["ARGS_JSON"]
if page.count("</body>") != 1:
    sys.exit("error: index.html does not have one </body>")
page = page.replace("</body>", script)

open(page_path, "w", encoding="utf-8").write(page)
PYEOF
echo "==> Wrote ${PAGE} (game arguments: ${ARGS_JSON})"
if [[ ${PREPARE_ONLY} -eq 1 ]]; then
	exit 0
fi
exec "${SRC_DIR}/run_warzone2100.sh" -b -B "${BUILD_DIR}" -p "${PAGE_NAME}" "${RUN_ARGS[@]}" -- "${GAME_ARGS[@]}"
