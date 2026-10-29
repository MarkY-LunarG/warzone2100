#!/usr/bin/env bash
# Make a zip of the Linux build for another Linux user: the game binary, the game
# data, the shared libraries that are not part of glibc, and launcher scripts.
# The zip has play_longbattle.sh, which watches the long battle of
# data/mp/tests/longbattle.json with the automatic camera.
#
# Usage: package_warzone2100.sh [-B <dir>] [-o <dir>] [-n <name>] [-m]
#
#   -B <dir>    Build directory. The default is "build".
#   -o <dir>    Output directory. The default is "build_package".
#   -n <name>   Package name. The default is "warzone2100-longbattle-linux-x86_64".
#   -m          Include the music (approximately 110 MB more).
#   -h          Show this help.
#
# The zip runs only on an x86_64 Linux with a glibc that is at least as new as the
# glibc of this machine (the script prints the version). The binary links glibc,
# libstdc++, and the Kerberos, udev, and SELinux libraries from the system of the
# user. All other libraries are in the zip.
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SRC_DIR}/build"
OUT_DIR="${SRC_DIR}/build_package"
NAME="warzone2100-longbattle-linux-x86_64"
WITH_MUSIC=0

usage()
{
	sed -n '2,19s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":B:o:n:mh" opt; do
	case "${opt}" in
		B) BUILD_DIR="$(realpath -m "${OPTARG}")" ;;
		o) OUT_DIR="$(realpath -m "${OPTARG}")" ;;
		n) NAME="${OPTARG}" ;;
		m) WITH_MUSIC=1 ;;
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
if [[ ! -x "${GAME}" ]]; then
	echo "error: ${GAME} not found. Run build_warzone2100.sh first." >&2
	exit 1
fi

STAGE_DIR="${OUT_DIR}/stage"
PKG_DIR="${OUT_DIR}/${NAME}"
ZIP_FILE="${OUT_DIR}/${NAME}.zip"

echo "==> Staging the CMake install in ${STAGE_DIR}"
rm -rf "${STAGE_DIR}" "${PKG_DIR}" "${ZIP_FILE}"
mkdir -p "${OUT_DIR}"
cmake --install "${BUILD_DIR}" --prefix "${STAGE_DIR}" > "${OUT_DIR}/install.log"

echo "==> Collecting the package in ${PKG_DIR}"
mkdir -p "${PKG_DIR}/bin" "${PKG_DIR}/lib" "${PKG_DIR}/share/warzone2100"
cp -p "${STAGE_DIR}/bin/warzone2100" "${PKG_DIR}/bin/"
# The data: the .wz archives, the fonts, the campaign mods, and the terrain packs.
# The music is optional. The debug symbols (400 MB), the translations, the man page,
# and the desktop files are not necessary to play.
for item in base.wz mp.wz fonts mods terrain_overrides; do
	cp -a "${STAGE_DIR}/share/warzone2100/${item}" "${PKG_DIR}/share/warzone2100/"
done
if [[ ${WITH_MUSIC} -eq 1 ]]; then
	cp -a "${STAGE_DIR}/share/warzone2100/music" "${PKG_DIR}/share/warzone2100/"
fi

# The shared libraries. Leave out the ones that must come from the system of the
# user: glibc and its loader, libstdc++ and libgcc (tied to glibc), Kerberos, udev,
# SELinux, and glib (they read system configuration).
SYSTEM_LIBS='^(ld-linux.*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libgcc_s|libstdc\+\+|libgssapi_krb5|libkrb5|libk5crypto|libkrb5support|libcom_err|libkeyutils|libudev|libselinux|libpcre2-8|libglib-2\.0|libcrypt)\.so.*$'
echo "==> Copying shared libraries"
ldd "${GAME}" | awk '/=> \//{print $1 "\t" $3}' | while IFS=$'\t' read -r soname path; do
	if [[ "${soname}" =~ ${SYSTEM_LIBS} ]]; then
		continue
	fi
	# Copy the file itself (not the symlink chain), under the soname that the binary wants.
	cp -L "${path}" "${PKG_DIR}/lib/${soname}"
done
chmod 755 "${PKG_DIR}/lib/"*.so*
# The Dawn library from the build tree carries 480 MB of debug symbols. Remove them.
strip --strip-unneeded "${PKG_DIR}/lib/"*.so*

# Note: sed reads all of its input. "head -1" would close the pipe early, and with
# pipefail the SIGPIPE of the first command would stop this script.
GLIBC_VERSION="$(ldd --version 2>&1 | sed -n '1s/.*[^0-9.]\([0-9]\+\.[0-9]\+\)$/\1/p')"
NEEDED_GLIBC="$(objdump -T "${GAME}" | grep -o 'GLIBC_[0-9.]*' | sort -V | uniq | tail -1 | sed 's/GLIBC_//')"
WZ_VERSION="$("${GAME}" --version 2>&1 | sed -n '1s/^Warzone 2100 - //p' || true)"

cat > "${PKG_DIR}/warzone2100.sh" <<'LAUNCHER'
#!/usr/bin/env bash
# Start Warzone 2100 from this folder. The game uses the libraries in lib/, the
# data in share/warzone2100/, and keeps its configuration, saves, logs, and
# replays in userdata/. All arguments go to the game.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${DIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
mkdir -p "${DIR}/userdata"
exec "${DIR}/bin/warzone2100" "--datadir=${DIR}/share/warzone2100" "--configdir=${DIR}/userdata" "$@"
LAUNCHER

cat > "${PKG_DIR}/play_longbattle.sh" <<'LAUNCHER'
#!/usr/bin/env bash
# Watch the long battle: three AIs (Nexus, Cobra, SemperFi) fight on the map
# Sk-Rush. You are a spectator with full map vision, and the camera moves to the
# fighting by itself. The battle takes approximately 25 minutes at normal speed.
# The game quits when the battle ends. Press ESC to quit before the end.
#
# Usage: play_longbattle.sh [-x <speed>] [-g <backend>] [-- <game args>]
#
#   -x <speed>    Game speed as a multiple of normal speed. The default is 1.
#   -g <backend>  Graphics backend: vulkan, opengl, opengles, or webgpu.
#                 Without -g, the game uses the backend from its configuration.
#                 webgpu needs a Wayland session on Linux.
#
# You can move the camera yourself at any time. The automatic camera takes over
# again after a few seconds. If you track a unit (select it and press Space), the
# automatic camera waits until you stop the tracking.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SPEED=1
ARGS=()
while getopts ":x:g:h" opt; do
	case "${opt}" in
		x) SPEED="${OPTARG}" ;;
		g) ARGS+=("--gfxbackend=${OPTARG}") ;;
		h)
			sed -n '2,15s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
			exit 0
			;;
		*)
			echo "error: unknown option -${OPTARG}" >&2
			exit 1
			;;
	esac
done
shift $((OPTIND - 1))
if [[ "${1:-}" == "--" ]]; then
	shift
fi
exec "${DIR}/warzone2100.sh" --skirmish=longbattle.json --autogame --autocamera "--autogamespeed=${SPEED}" "${ARGS[@]}" "$@"
LAUNCHER
chmod 755 "${PKG_DIR}/warzone2100.sh" "${PKG_DIR}/play_longbattle.sh"

cat > "${PKG_DIR}/README.txt" <<README
Warzone 2100: the long battle
=============================

This folder has a Linux build of Warzone 2100 with two additions:

- An automatic camera (--autocamera) that moves the view to the fighting.
- A game speed option for automatic games (--autogamespeed).

${WZ_VERSION}

How to watch the battle
-----------------------

1. Unzip the file.
2. Open a terminal in the unzipped folder.
3. Run: ./play_longbattle.sh

Three AIs fight on the map Sk-Rush. You are a spectator with full map vision.
The battle takes approximately 25 minutes. The game quits when it ends.
Press ESC to quit before the end.

./play_longbattle.sh -x 4 runs the battle at four times normal speed.
./play_longbattle.sh -g vulkan selects the Vulkan backend (also opengl, webgpu).
./play_longbattle.sh -h shows all options.

./warzone2100.sh starts the normal game with the menus.

Requirements
------------

- Linux on x86_64.
- glibc ${NEEDED_GLIBC} or newer. This build was made on a system with glibc ${GLIBC_VERSION}.
  An older distribution stops with a message like "GLIBC_${NEEDED_GLIBC} not found".
- A GPU driver with Vulkan or OpenGL. The webgpu backend needs a Wayland session.
- The libraries in lib/ come with this folder. The game takes glibc, libstdc++,
  Kerberos, udev, SELinux, and glib from your system.

Files
-----

- bin/warzone2100: the game.
- lib/: the shared libraries.
- share/warzone2100/: the game data. The battle definition is tests/longbattle.json
  inside mp.wz.
- userdata/: configuration, logs, saves, and replays. The game makes this folder.
README

echo "==> Writing ${ZIP_FILE}"
(cd "${OUT_DIR}" && zip -qr "${ZIP_FILE}" "${NAME}")
rm -rf "${STAGE_DIR}"
echo "==> Done: ${ZIP_FILE} ($(du -h "${ZIP_FILE}" | cut -f1)); needs glibc ${NEEDED_GLIBC} or newer (this machine has ${GLIBC_VERSION})"
