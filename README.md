<h1 align="center">
  <img src="icons/warzone2100.large.png" alt="Warzone 2100">
  <br />
  Warzone 2100
</h1>

<p align="center">Warzone 2100 is a <b>free, open source, 3D real-time strategy game</b> with a story-driven single-player campaign, online multi-player, offline skirmish, and more.</p>

# Highlights

- Remastered single-player campaign with expanding persistent bases, away missions, and more
- Online multiplayer with up to 10 players, free-for-all or team-play, for massive battles
- Local skirmish with AI bots, for endless replayability
- Extensive tech tree with over 400 different technologies
- Customizable units with a flexible design system that enables a wide variety of possible tactics
- Cross-platform multiplayer
- Support for multiple graphics backends (OpenGL, OpenGL ES, Vulkan, WebGPU, and Vulkan SC)
- 100% free and open source

For more info, see: [Homepage of the Warzone 2100 Project](https://wz2100.net/)

# Installation

**Visit https://wz2100.net for the latest stable release for Windows, macOS and Linux.**

> Note for videos in Ubuntu: For important information during the game, download the videos manually. Assuming the game is installed in the standard `~/.local/share/` folder, use these commands:
> ```shell
> mkdir ~/.local/share/warzone2100
> wget https://github.com/Warzone2100/wz-sequences/releases/download/v3/standard-quality-en-sequences.wz -O ~/.local/share/warzone2100/sequences.wz
> ```

# Origins

Warzone 2100, was originally developed by Pumpkin Studios, and released in 1999 as a
ground-breaking and innovative 3D real-time strategy game.

In 2004, Eidos (in collaboration with Pumpkin Studios) decided to release the source for the game
under the terms of the GNU GPL - followed later by the remaining music & video files.

It has been developed, maintained, and improved by the community ever since, under the banner of
the “Warzone 2100 Project”.

# State of the game

After the liberation of the Warzone 2100 source-code on December 6th, 2004, all
proprietary technologies have been replaced with open-source counterparts, and
extensive improvements and additions have been made throughout while preserving
what made the original release so great.

Development continues on GitHub, and bug reports & contributions are welcome!

# Latest development builds

[![Windows Build Status](https://img.shields.io/github/actions/workflow/status/Warzone2100/warzone2100/CI_windows.yml?branch=master&label=Windows&logo=windows)](https://github.com/Warzone2100/warzone2100/actions?query=workflow%3AWindows+branch%3Amaster+event%3Apush)
 [![macOS Build Status](https://img.shields.io/github/actions/workflow/status/Warzone2100/warzone2100/CI_macos.yml?branch=master&label=macOS&logo=apple)](https://github.com/Warzone2100/warzone2100/actions?query=workflow%3AmacOS+branch%3Amaster+event%3Apush)
 [![Ubuntu Build Status](https://img.shields.io/github/actions/workflow/status/Warzone2100/warzone2100/CI_ubuntu.yml?branch=master&label=Ubuntu&logo=ubuntu&logoColor=FFFFFF)](https://github.com/Warzone2100/warzone2100/actions?query=workflow%3AUbuntu+branch%3Amaster+event%3Apush)
 [![Fedora Build Status](https://img.shields.io/github/actions/workflow/status/Warzone2100/warzone2100/CI_fedora.yml?branch=master&label=Fedora&logo=fedora&logoColor=FFFFFF)](https://github.com/Warzone2100/warzone2100/actions?query=workflow%3AFedora+branch%3Amaster+event%3Apush)
 [![WebAssembly Build Status](https://img.shields.io/github/actions/workflow/status/Warzone2100/warzone2100/CI_emscripten.yml?branch=master&label=WebASM&logo=webassembly&logoColor=FFFFFF)](https://github.com/Warzone2100/warzone2100/actions?query=workflow%3AEmscripten+branch%3Amaster+event%3Apush)
 [![FreeBSD Build Status](https://img.shields.io/cirrus/github/Warzone2100/warzone2100/master?label=FreeBSD&logo=FreeBSD)](https://cirrus-ci.com/github/Warzone2100/warzone2100/master)
 [![Packaging status](https://repology.org/badge/tiny-repos/warzone2100.svg)](https://repology.org/project/warzone2100/versions)

### Windows

How to get the latest Windows development builds:
1. View the **[latest successful Windows builds](https://github.com/Warzone2100/warzone2100/actions?query=workflow%3AWindows+branch%3Amaster+event%3Apush+is%3Asuccess)**.
2. Select the latest workflow run in the table / list.
   This should display a list of **Artifacts** from the run.
3. Download the `warzone2100_win_installer` artifact.
> Note: A free GitHub account is currently required to download the artifacts.

### macOS

How to get the latest macOS development builds:
1. View the **[latest successful macOS builds](https://github.com/Warzone2100/warzone2100/actions?query=workflow%3AmacOS+branch%3Amaster+event%3Apush+is%3Asuccess)**.
2. Select the latest workflow run in the table / list.
   This should display a list of **Artifacts** from the run.
3. Download the `warzone2100_macOS_universal` or `warzone2100_macOS_universal_novideos` artifact (depending on whether you want the full app bundle or not).
> Note: A free GitHub account is currently required to download the artifacts.

### Ubuntu

How to get the latest Ubuntu development builds:
1. View the **[latest successful Ubuntu builds](https://github.com/Warzone2100/warzone2100/actions?query=workflow%3AUbuntu+branch%3Amaster+event%3Apush+is%3Asuccess)**.
2. Select the latest workflow run in the table / list.
   This should display a list of **Artifacts** from the run.
3. Download the appropriate `warzone2100_ubuntu<version>_amd64_deb` artifact.
   - If you are running Ubuntu 22.04: `warzone2100_ubuntu22.04_amd64_deb`
   - If you are running Ubuntu 24.04: `warzone2100_ubuntu24.04_amd64_deb`
> Note: A free GitHub account is currently required to download the artifacts.
4. Extract the contents of the downloaded .zip (`warzone2100_ubuntu<version>_amd64.deb`) to your Desktop.
5. Execute the following commands in Terminal:
```shell
cd ~/Desktop
sudo apt install ./warzone2100_ubuntu<version>_amd64.deb
```
6. Download the video for crucial information during the game, for more see "Videos" section. Assuming the game is installed in the standard `~/.local/share/` folder, use this command (update `warzone2100-<version>`):
```shell
wget https://github.com/Warzone2100/wz-sequences/releases/download/v3/standard-quality-en-sequences.wz -O ~/.local/share/warzone2100-<version>/sequences.wz
```

### Linux (from source)

Clone this Git repo and build, following the instructions under:
[How to Build](#how-to-build)

> Development builds are a snapshot of the current state of development, from the
> latest (successfully-built) commit. Help testing these builds is always welcomed,
> but they should be considered a work-in-progress.

### Videos
You can download videos from [here](https://github.com/Warzone2100/wz-sequences/releases/tag/v3), or [here](https://sourceforge.net/projects/warzone2100/files/warzone2100/Videos/). You will need to rename the downloaded file to `sequences.wz`, and place it into your Warzone 2100 directory, as described above.
Note that `.wz` files are just `.zip` in disguise, you can rename it and extract the content if wish to inspect them.

# Reporting bugs

This game still has bugs and if you run into one, please use the GitHub bugtracker
(https://github.com/Warzone2100/warzone2100/issues) to report the bug. In order to fix
those bugs more quickly, we require that you follow these rules:

   1. If the game crashes you may save a memory dump. Please do so and upload it
      when reporting the bug. (Linux locates that file at /tmp/warzone2100.gdmp,
      Windows at /Program Files/Warzone 2100/warzone2100.RPT, macOS by
      clicking "Details" in the crash error message)
      A self created backtrace is just as useful.
   2. Give as much information about what you were doing before the crash/bug
      occurred.
   3. Try to reproduce the bug and add a description of the process to your bug
      report.
   4. You may even upload save files. These consist of one file and
      one folder. Both are named after your savegame (e.g. MySaveGame.gam and
      the folder MySaveGame).
   5. Bug reports are not submit-and-forget. It may be that you forgot some
      information or forgot to upload a file. So it is also in your interest to
      watch the bug-report after it has been submitted. Additionally, you can enable
      e-mails of comments to your bug report.

# Configuration

Warzone 2100 uses its own subdirectory in a user's home directory to save
configuration data, save files and certain other things. Additionally you can
use this directory to place custom maps and mods so the game can find them. The
location of this directory depends on the operating system.

> [!TIP]
> The easy way to find the configuration directory is to:
> 1. Launch Warzone 2100
> 2. Click "Options"
> 3. Click the small "Open Configuration Directory" link in the bottom-left

### Configuration file

The configuration file is just called 'config' and contains several configuration
options, some of them can be changed by using command-line options or using
the in-game menus, others can only be changed by editing the file manually.

If at any point you did something wrong, you can delete the old configuration
file and just restart Warzone 2100. Then the game will regenerate a new
configuration file with default values.

# Command-line options

Warzone 2100 can be started with different options and arguments. For a list
of these options, run the game with the --help option.

Notes: These options all have two dashes (--), not one dash only (-). Also,
if the option has an argument, you need to separate the option and its argument
with a '=' sign - spaces do not work.

Note: Some options have corresponding entries in the configuration file and will
persist from one start of Warzone 2100 to the next.

# Multiplaying via internet

There are two methods to start a multiplayer game via the internet: using the host's
IP or using the lobby server. Make sure you are able to communicate on TCP ports
2100 and 9999. Note that for port forwarding, you only need to configure your
router to forward port 2100.

You can choose whether to connect via Lobby or IP:

* If you choose IP, Warzone 2100 asks you for the IP address of the host and
  will try to connect to that IP.
* If you choose Lobby, Warzone 2100 will connect to the lobby server, as long as
  the lobby-server-address in your config file has not been changed.

You will see a list of games from which you can select.

You can kick unwanted players out of a game before it begins by clicking left on
them while holding the right mouse button.

When you are hosting a game it will automatically be listed on the lobby server.
If you do not want your games to be listed on the lobby-server, you should
change the entry "masterserver_name=lobby.wz2100.net" in your config to some-
thing invalid, for example: "nomasterserverplease".

If you then want to see the games that are listed on the lobby server you may
enter "lobby.wz2100.net" when prompted to enter the host's IP or change the
entry in the config file back. You will have to restart Warzone 2100 in order
for config changes to take effect.

# Cheats

Warzone 2100 has many built-in cheat codes:
* [Warzone 2100 Cheats](doc/Cheats.md)

# Modding information

Warzone 2100 AI, maps and campaign can be scripted using JavaScript.

Links to further information
* [Scripting](doc/Scripting.md)
* [Model format](doc/PIE.md)
* [Animation](doc/Animation.md)

# How to build

### Getting the Source

To properly build the game, either:
- Download a release `tar.xz`, which contains all the source code and revision information.

  _OR_

- Clone the Git repo:
  ```shell
  git clone https://github.com/Warzone2100/warzone2100.git
  cd warzone2100
  git fetch --tags
  git submodule update --init --recursive
  ```
  > Note: Initializing submodules is required.

Do **not** use GitHub's "Download Zip" option, as it **does not contain submodules** or the Git-based autorevision information.

### Linux

* Prerequisites
   * Compiling tools (ex. CMake, GCC/G++/Clang, ninja-build)
   * Archiving tools (ex. zip, p7zip)
   * Various libraries:
      * [SDL](https://www.libsdl.org) ≥ 3.2.12 _(strongly recommended: ≥ 3.2.26)_
      * [PhysicsFS](https://icculus.org/physfs/) ≥ 2.1 _(strongly recommended: ≥ 3.0.2)_
      * [libpng](https://www.libpng.org/pub/png/libpng.html) ≥ 1.2
      * [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) ≥ 2.1
      * [libtheora](https://theora.org)
      * [libvorbis](https://xiph.org/vorbis)
      * [libogg](https://xiph.org/ogg/)
      * [libvpx](https://www.webmproject.org/code/)
      * [opus](https://github.com/xiph/opus)
      * [Freetype](https://www.freetype.org/) _(strongly recommended: ≥ 2.10.4)_
      * [Harfbuzz](https://github.com/harfbuzz/harfbuzz) ≥ 1.0 _(strongly recommended: ≥ 3.3.0)_
      * [fribidi](https://github.com/fribidi/fribidi)
      * [OpenAL-Soft](https://openal-soft.org)
      * [libcurl](https://curl.haxx.se/libcurl/) _(strongly recommended: ≥ 7.58.0)_
      * [libsodium](https://github.com/jedisct1/libsodium) ≥ 1.0.16
      * [SQLite](https://www.sqlite.org/index.html) ≥ 3.14
      * [libzip](https://github.com/nih-at/libzip) _(strongly recommended: ≥ 1.10.1)_
      * [libprotobuf](https://github.com/protocolbuffers/protobuf) (if compiling with GNS support)
   * For language support: [Gettext](https://www.gnu.org/software/gettext/)
   * To generate documentation: [Asciidoctor](https://asciidoctor.org) ≥ 1.5.3
   * To build with Vulkan support: the full [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) _(strongly recommended: ≥ 1.2.148.1)_
* **Installing prerequisites:**
   * Using `get-dependencies_linux.sh`:
      1. Specify one of the linux distros supported by the script: (`ubuntu`, `fedora`, `alpine`, `archlinux`, `opensuse-tumbleweed`, `gentoo`, `debian`, `raspberrypios`) _REQUIRED_
      2. Specify a mode: (`build-all` (default), `build-dependencies`) _OPTIONAL_

      Example:
      ```shell
      sudo ./get-dependencies_linux.sh ubuntu build-dependencies
      ```
* **Building from the command-line:**
   1. Starting from the _parent_ directory of the warzone2100 repository, create an [out-of-source](https://cmake.org/cmake/help/book/mastering-cmake/chapter/Getting%20Started.html#directory-structure) build directory:
      ```shell
      mkdir build
      ```
   2. Change directory into the `build` directory:
      ```shell
      cd build
      ```
   3. Run CMake configure to generate the build files:
      ```shell
      cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX:PATH=~/wz/install -GNinja ../warzone2100
      ```
      > - [Modify the `CMAKE_INSTALL_PREFIX` parameter value as desired](https://cmake.org/cmake/help/latest/variable/CMAKE_INSTALL_PREFIX.html) to configure the base installation path.
      > - The `../warzone2100` path at the end should point to the warzone2100 repo directory. This example assumes that the repo directory and the build directory are siblings, and that the repo was cloned into a directory named `warzone2100`.
   4. Run CMake build:
      ```shell
      cmake --build . --target install
      ```

### Windows using MSVC

See [platforms/windows/README-build.md](platforms/windows/README-build.md)

### macOS
See [platforms/macos/README.md](platforms/macos/README.md)

### Graphics backends

The game picks its graphics backend at run time with `--gfxbackend=<backend>`, from the backends the build has.
If a backend cannot start, the game tries the next one.

| Backend | Build | Run |
|---|---|---|
| OpenGL / OpenGL ES | Always built | `./build/src/warzone2100 --gfxbackend=opengl` (or `opengles`) |
| Vulkan | On by default (`WZ_ENABLE_BACKEND_VULKAN`) | `./build/src/warzone2100 --gfxbackend=vulkan` |
| WebGPU, native (Linux) | `-DWZ_ENABLE_BACKEND_WEBGPU=ON` ([below](#native-linux)) | `SDL_VIDEO_DRIVER=wayland ./build/src/warzone2100 --gfxbackend=webgpu` |
| WebGPU, browser | An Emscripten build with `-DWZ_ENABLE_BACKEND_WEBGPU=ON` ([below](#browser)) | `python3 platforms/emscripten/serve.py build_web/src`, then open http://localhost:8000/ |
| Vulkan SC | A separate build with `-DWZ_ENABLE_BACKEND_VULKANSC=ON` ([below](#vulkan-sc-backend)) | `./build_vulkansc/src/warzone2100 --gfxbackend=vulkansc` |

`--gfxdebug` turns on each backend's debug checks (the validation layer for Vulkan and Vulkan SC).

### WebGPU backend

The WebGPU backend uses [Dawn](https://dawn.googlesource.com/dawn).
It is available on Linux (Wayland only) and in the browser.
It is off by default.
Enable it with the CMake option `WZ_ENABLE_BACKEND_WEBGPU=ON`.

The build translates the Vulkan shaders to WGSL.
Thus, it needs `glslc` (from shaderc or the Vulkan SDK).

#### Native (Linux)

* Prerequisites, in addition to the [Linux](#linux) prerequisites:
   * CMake 3.28 or later
   * The Wayland development packages: wayland-client, wayland-cursor, wayland-egl, and xkbcommon
* Configure and build:
   ```shell
   cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWZ_ENABLE_BACKEND_WEBGPU=ON
   cmake --build build
   ```
   The first build gets Dawn at a pinned commit and builds it, which takes several minutes.
   To use a local Dawn checkout at that commit, add `-DFETCHCONTENT_SOURCE_DIR_DAWN=/path/to/dawn`.
* Run:
   ```shell
   ./build/src/warzone2100 --gfxbackend=webgpu
   ```
   WebGPU needs the SDL Wayland video driver.
   If SDL picks X11 (for example, through XWayland), start the game with `SDL_VIDEO_DRIVER=wayland`.
   On X11, the game uses the next available backend.

Debug aids:

* `--gfxdebug` compiles all the WGSL shaders at startup and reports the errors.
* `WZ_WEBGPU_DEBUG_SCREENSHOT=<frame>:<file.png>` saves that frame as a PNG.
  The file name is relative to the configuration directory.
* `WZ_WEBGPU_DEBUG_FORCE_DEVICE_LOSS=<frame>` loses the WebGPU device at that frame.

#### Browser

The browser build uses Dawn's `emdawnwebgpu` port, from a Dawn release near the pinned Dawn commit.
The build downloads the port and patches it for newer Emscripten (tested: 6.0.8), which removed a setting that the port reads.
To use an extracted `emdawnwebgpu_pkg` folder instead of a download, add `-DWZ_EMDAWNWEBGPU_PKG_DIR=/path/to/emdawnwebgpu_pkg`.
The page creates the WebGPU device before the game starts.
See [platforms/emscripten/webgpu-postjs.js](platforms/emscripten/webgpu-postjs.js).
If the browser has no WebGPU, the game uses WebGL 2.

* Prerequisites, in addition to [the Emscripten ones](platforms/emscripten/README-build.md):
   * Emscripten 4.0.10 or later.
     The CI uses 4.0.10. Emscripten 6.0.8 also works.
   * vcpkg, as in the Emscripten build instructions
   * autoconf, autoconf-archive, automake, and libtool (vcpkg needs them to build libsodium)
   * A native WebGPU build (see above).
     Its host tools `tint` and `wz_webgpu_spirv_prep` translate the shaders for the browser build.
     To build only the tools:
     ```shell
     cmake --build build --target tint wz_webgpu_spirv_prep
     ```
* Configure and build (with the Emscripten environment active):
   ```shell
   cmake -S . -B build_web -GNinja -DCMAKE_BUILD_TYPE=Release \
       "-DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake" \
       -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
       -DWZ_ENABLE_BACKEND_WEBGPU=ON "-DWZ_WEBGPU_HOST_TOOLS_DIR=$(pwd)/build"
   cmake --build build_web
   ```
* Run with `emrun`, which serves the page with the headers that threads need:
   ```shell
   emrun --browser firefox build_web/src/index.html
   ```
   Or, without Emscripten, run [platforms/emscripten/serve.py](platforms/emscripten/serve.py), which sends the same headers, and open http://localhost:8000/:
   ```shell
   python3 platforms/emscripten/serve.py build_web/src
   ```
   A plain `python -m http.server` does not send these headers, so the page reports "Unsupported Browser" (Wasm:Threads).
   Use a browser with WebGPU.
   Firefox on Linux ships with WebGPU off.
   Set `dom.webgpu.enabled` to `true` in `about:config`, or start Firefox with a profile that sets it.
   Without WebGPU, the page falls back to WebGL 2.
   The browser version has no screenshots and no `--gfxdebug` shader check yet.

### Vulkan SC backend

[Vulkan SC](https://www.khronos.org/vulkansc/) is the safety-critical variant of Vulkan 1.2.
The Vulkan SC backend is the Vulkan backend, built for Vulkan SC.
Vulkan SC has no shader modules: the pipelines are compiled offline into a pipeline cache, and the application reserves all its objects when it creates the device.
It is off by default, and needs a separate build directory: a Vulkan SC build has no desktop Vulkan.
It is tested on Linux with the [VulkanSC-Emulation](https://github.com/KhronosGroup/VulkanSC-Emulation) ICD, which runs Vulkan SC on a desktop Vulkan driver.

In an SC build, Vulkan SC is the default backend, and there is no desktop Vulkan backend.
The SC build presents through `VK_KHR_display` (the first display, at its largest mode), not a window.
It hides the SDL window (`WZ_VULKANSC_SHOW_WINDOW=1` shows it), so the game takes no keyboard or mouse input: play it with a gamepad.
It shows no message boxes either; it logs them.
Its graphics settings are a fixed graphics profile, `tools/vulkansc/profile_high.cfg`, built into the game; the options it fixes cannot be changed in the menus.

* Prerequisites, in addition to the [Linux](#linux) ones:
   * The Vulkan SC headers ([VulkanSC-Headers](https://github.com/KhronosGroup/VulkanSC-Headers)): set `VULKANSC_SDK` or `-DWZ_VULKANSC_INCLUDE_DIR=/path/to/VulkanSC-Headers/include`.
   * The pipeline cache compiler (PCC) of the Vulkan SC implementation: `pcconvk` from VulkanSC-Emulation, given with `-DWZ_VULKANSC_PCC=/path/to/pcconvk`.
     For another PCC, set `WZ_VULKANSC_PCC_PATH_ARG`, `WZ_VULKANSC_PCC_OUT_ARG`, `WZ_VULKANSC_PCC_PREFIX_ARG`, and `WZ_VULKANSC_PCC_EXTRA_FLAGS` to its options.
   * At run time, the Vulkan SC loader ([VulkanSC-Loader](https://github.com/KhronosGroup/VulkanSC-Loader)), `libvulkansc.so.1`, and a Vulkan SC ICD.
* Configure and build:
   ```shell
   cmake -S . -B build_vulkansc -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
       -DWZ_ENABLE_BACKEND_VULKANSC=ON \
       -DWZ_VULKANSC_INCLUDE_DIR=/path/to/VulkanSC-Headers/include \
       -DWZ_VULKANSC_PCC=/path/to/VulkanSC-Emulation/build/pcc/pcconvk
   cmake --build build_vulkansc
   ```
   The build compiles the pipeline JSON of the profile (`data/vulkansc/high`) and the SPIR-V shaders into the pipeline cache.
   The cache and the pipeline manifest go into `base.wz` (`shaders/vksc/`).
   A cache works only with the implementation whose PCC built it.
* Run, on the emulation ICD:
   ```shell
   VK_DRIVER_FILES=/path/to/VulkanSC-Emulation/build/icd/vksconvk.json \
   WZ_VULKANSC_LIBRARY=/path/to/VulkanSC-Loader/build/loader/libvulkansc.so.1 \
   ./build_vulkansc/src/warzone2100 --gfxbackend=vulkansc
   ```
   `WZ_VULKANSC_LIBRARY` is only needed when the Vulkan SC loader is not installed.
   It can also name a layer that forwards to the loader, such as the Premium-GFXR Vulkan SC capture layer.
   With `--gfxdebug`, the game enables `VK_LAYER_KHRONOS_validation` through the Vulkan SC loader: build [VulkanSC-ValidationLayers](https://github.com/KhronosGroup/VulkanSC-ValidationLayers) and point `VK_LAYER_PATH` at its `layers` directory.
   The emulation ICD can also run the desktop validation layer underneath: `VKSC_EMU_VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`.

#### The pipeline JSON

The desktop Vulkan build writes the pipeline JSON of the pipelines it creates, when `WZ_VULKAN_EXPORT_PIPELINES` names a directory.
[tools/vulkansc/export_pipelines.sh](tools/vulkansc/export_pipelines.sh) runs the export sessions (three autogames and a gamepad script) with the profile, and writes `data/vulkansc/<profile>`:
one JSON document per pipeline, and the manifest `wz2100_pipelines.json`.
Run it with a desktop Vulkan build after a change to the pipelines, the shaders, the render graph, or the profile, and commit the result:
```shell
tools/vulkansc/export_pipelines.sh        # write data/vulkansc/high
tools/vulkansc/export_pipelines.sh -c     # check only: compare with data/vulkansc/high
```
If the SC build logs "not in the pipeline manifest" or "could not create pipeline", the export sessions do not reach that pipeline: add a session that draws it.

#### Everything is created at startup

A safety-critical application creates its objects when it starts, and allocates no memory after.
The SC build does this for the objects that are costly or limited in Vulkan SC:

* **Pipelines.** The manifest holds, besides the pipeline names, each pipeline recipe (the state, the shaders, the vertex, texture, and uniform layout) and a render pass layout of each render pass compatibility class.
  At the end of startup, before the first frame, the game creates a render pass of each class and every pipeline of the manifest, from the pipeline cache (`VkPipelineOfflineCreateInfo`).
  The specialization constants (shadow and lighting settings) come from the manifest too, so that they match the profile.
  When the game code later asks for a pipeline, it gets the one created at startup.
  No pipeline is created after startup: one that is not in the manifest is logged as an error, and its draws are skipped.
  The reservation has exactly one pipeline pool entry for each pipeline in the cache.
* **Device memory.** All device memory is allocated at startup, as fixed [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) pools (`minBlockCount` equal to `maxBlockCount`): device-local buffers, device-local images, streamed buffers, and staging.
  Every buffer and image, including the render targets and the screenshot staging buffers, is sub-allocated from them, and freeing one returns its space to the pool.
  Vulkan SC has no `vkFreeMemory`, so after startup the game refuses any `vkAllocateMemory` and logs an error.
* **Object reservation.** `VkDeviceObjectReservationCreateInfo` reserves every object type, with headroom over the measured peaks.
  Command pools reserve their command buffer memory (`VkCommandPoolMemoryReservationCreateInfo`), and the descriptor pools, command pools, and command buffers are all created at startup.
* **Swapchain.** It is created once. A display change that needs a new swapchain is a fatal error, as Vulkan SC cannot destroy one.

What the game still creates after startup, within the reservation and from the memory pools: images, buffers, and image views (textures, text, and vertex data as maps and menus load), framebuffers, descriptor sets, and the few render passes of layouts that the render graph first uses later.
At shutdown the game logs, for each object type, the peak, the total created, and how many were created after startup (`--debug=3d`).

#### Tests

[tools/vulkansc/run_tests.sh](tools/vulkansc/run_tests.sh) runs the desktop and Vulkan SC builds through autogames, a frame comparison between them, and a gamepad-only game (a virtual gamepad plays [tools/vulkansc/gamepad_skirmish.gpad](tools/vulkansc/gamepad_skirmish.gpad), with `WZ_DEBUG_GAMEPAD_SCRIPT`).
A test fails on any validation message, error, Vulkan SC fault, pipeline missing from the cache, object alive at shutdown, or device memory allocated after startup.
Run it with `-h` for its options and the environment it needs (the emulation ICD, the loader, the Vulkan SC validation layer, and a Premium-GFXR build for capture and replay).

Debug aids:

* `WZ_VULKANSC_PIPELINE_CACHE=<file>` and `WZ_VULKANSC_PIPELINE_MANIFEST=<file>` use that pipeline cache and manifest instead of the ones in `base.wz`.
* `WZ_VULKAN_DEBUG_SCREENSHOT=<frame>:<file.png>` saves that frame as a PNG (also in the desktop Vulkan build).
* `WZ_VULKAN_DEBUG_VALIDATION_NONFATAL=1` logs validation errors instead of showing a fatal message box.

# Licensing

Warzone 2100 is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

[![SPDX-License-Identifier: GPL-2.0-or-later](https://img.shields.io/static/v1?label=SPDX-License-Identifier&message=GPL-2.0-or-later&color=blue&logo=open-source-initiative&logoColor=white&logoWidth=10&style=flat-square)](COPYING)

More information: [COPYING.README](COPYING.README), [COPYING.NONGPL](COPYING.NONGPL)

# Special thanks

Free code signing for Windows provided by [SignPath.io](https://signpath.io), certificate by [SignPath Foundation](https://signpath.org).
