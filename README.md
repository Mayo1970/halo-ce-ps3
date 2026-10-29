Halo: Combat Evolved ported to the PlayStation 3. built with the open-source [PSL1GHT](https://github.com/ps3dev/PSL1GHT) SDK.

The game's own code, from the decompilation of the Xbox build 2342, is compiled for the PS3.
The port is based on [OpenCE](https://github.com/OpenCommunityEdition/OpenCE), and an crossplay with the
OpenCE builds for Windows, Linux, Android and the web.

OpenCE-PS3 is work in progress. See [Known limits](#known-limits).

## Install and set up

1. Install `halo_ps3.pkg` with the Package Manager on the XMB.
2. Extract the `maps` folder from your Xbox disc image of Halo: Combat Evolved. An OpenCE build for Windows or Linux
   extracts it next to the executable at its first start.
3. Copy the `maps` folder to `/dev_hdd0/data/HaloCE/`, for example with FTP. Do not change the files.
4. Start Halo: Combat Evolved from the Game column.

**OpenCE-PS3 does not include the game data.** You must provide your own copy of the game.

If the HDD has no `maps` folder, the game looks for `HaloCE/maps` on the USB drives.

| Path | Content |
|---|---|
| `/dev_hdd0/data/HaloCE/maps/` | The Xbox maps. |
| `/dev_hdd0/data/HaloCE/config.toml` | Settings. A new package install keeps them. |
| `/dev_hdd0/data/HaloCE/u/` | Profiles and user data. |
| `/dev_hdd0/data/HaloCE/z/` | The map cache (about 730 MB) and the checkpoint save. |
| `/dev_hdd0/data/HaloCE/init.txt` | Optional. Console commands that the game runs at start-up, one on each line. |

## Features

- **60 frames per second at 720p.** The game draws a frame at each refresh, blended between its 30 ticks a second.
- **Widescreen.** 852x480 of the game drawn at 1280x720, or the Xbox's 4:3.
- **PS3 button icons** in the menus and the HUD, in two styles, or the Xbox's.
- **LAN play** with the OpenCE builds for the PC on the same network.
- **Internet play.** Join public games from the game list, or host a multiplayer or co-op campaign game.
- **Invite codes.** The PS3 cannot paste an invite link, so it uses 8-digit codes. The converter page
  `port/ps3/invite/index.html` changes a link to a code.
- **Split screen** for up to 4 players.

SETTINGS > EXTRA OPTIONS in the main menu has these settings:

| Option | What it does | Default |
|---|---|---|
| WIDESCREEN | 16:9 over the whole display, or the Xbox's 4:3 in the middle | ON |
| BUTTON ICONS | XBOX, STYLE A (colored circles, white symbols) or STYLE B (black circles, colored symbols) | STYLE A |
| PRESET | QUALITY (30 fps), PERFORMANCE (60 fps, dynamic resolution) or CUSTOM | PERFORMANCE |
| FRAME RATE | 60 or 30 frames per second | 60 |
| DYNAMIC RESOLUTION | In heavy scenes, draws the 3D view smaller and scales it up | ON |

### Known limits

**Custom Edition maps** are not supported.

**Split screen** always plays at 30 frames per second.

**Invite codes** work only on the PS3. The OpenCE builds for the PC, Android and the web take only invite links,
and they refuse a code as a link that is not valid. They cannot join a game that a PS3 hosts by its code.

Some heavy fights drop below 60 frames per second. DYNAMIC RESOLUTION or the QUALITY preset helps.

## Compiling

The build makes `build/ps3/halo_ps3.pkg`. The maintainer builds in Docker with `ppu-gcc` 7.2.0.

### With Docker (recommended)

`build.sh` runs `make` in a Docker image named `ps3dev-latest:latest`. The image has the toolchain, PSL1GHT and
the libraries (FreeType, libpng, zlib).

1. Get the prebuilt image from Docker Hub
   ([mayo2853/ps3dev-latest](https://hub.docker.com/r/mayo2853/ps3dev-latest)) and give it the local name:

   ```sh
   docker pull mayo2853/ps3dev-latest:latest
   docker tag mayo2853/ps3dev-latest:latest ps3dev-latest:latest
   ```

2. In the repository folder, build the package:

   ```sh
   ./build.sh -j12 pkg
   ```

On Windows, run `build.sh` from Git Bash with Docker Desktop.

#### Make the image yourself

Do this only if you cannot use the prebuilt image.

1. Clone [ps3dev](https://github.com/ps3dev/ps3dev):

   ```sh
   git clone https://github.com/ps3dev/ps3dev.git
   cd ps3dev
   ```

2. Save this file as `Dockerfile` in that folder:

   ```dockerfile
   FROM ubuntu:24.04

   ENV DEBIAN_FRONTEND=noninteractive
   ENV PS3DEV=/usr/local/ps3dev
   ENV PSL1GHT=$PS3DEV
   ENV PATH=$PATH:$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin

   RUN apt-get update && apt-get install -y autoconf automake bison \
       bzip2 ca-certificates clang curl flex g++ gcc git libelf-dev \
       libgmp3-dev libncurses5-dev libssl-dev libtool libtool-bin \
       make patch pkg-config python-is-python3 python3 python3-dev \
       subversion tar texinfo wget xz-utils zlib1g-dev \
       && rm -rf /var/lib/apt/lists/*

   WORKDIR /opt/ps3dev-src
   COPY build-all.sh ./
   COPY scripts ./scripts
   COPY utils ./utils

   # Windows checkouts use CRLF, which breaks bash scripts.
   RUN sed -i 's/\r$//' build-all.sh scripts/*.sh utils/*.sh \
       && chmod +x build-all.sh scripts/*.sh

   RUN ./build-all.sh && rm -rf build

   WORKDIR /src
   ```

3. Build the image. This builds the toolchain, PSL1GHT and the libraries (FreeType, libpng, zlib), so it takes a
   long time:

   ```sh
   docker build -t ps3dev-latest:latest .
   ```

4. In the repository folder, run `./build.sh -j12 pkg`.

#### Build commands

| Command | Result |
|---|---|
| `./build.sh -j12` | `build/ps3/halo_ps3.elf` |
| `./build.sh -j12 self` | `build/ps3/EBOOT.BIN` |
| `./build.sh -j12 pkg` | `build/ps3/halo_ps3.pkg` |
| `./build.sh check` | The link checks. Run it before each test on a PS3. |
| `./build.sh clean` | Deletes `build/ps3/` |

| Variable | Effect |
|---|---|
| `LOG_IP=<PC IP>` | The PC that gets the UDP log, port 18194. Example: `./build.sh pkg LOG_IP=192.168.1.20` |
| `DEBUG=0` | The retail build, without the UDP log. The default is `DEBUG=1`. |

To see the log, enter `nc -ul 18194` on the PC.

Run `./build.sh clean` before you build again with another image or another variable. Object files from two
toolchains link without an error, but the program then fails on the PS3.

### With a local toolchain

1. Install the PS3 toolchain, PSL1GHT and the libraries as the [ps3dev](https://github.com/ps3dev/ps3dev) README
   says (Linux, macOS, or WSL on Windows).
2. Set `PS3DEV`, `PSL1GHT` and `PATH` as that README says.
3. In the repository folder, run `make pkg`.

### Shaders

The pixel shader keys are in `port/ps3/shader_keys.txt`. The build translates them to RSX microcode on the PC and
links them in. A normal build does not need the shader sweep.

### For developers

[port/ps3/README.md](port/ps3/README.md) describes the port's code, the log and the PC checks.

## AI disclosure

OpenCE-PS3 is an AI-assisted project.

## Credits

- [OpenCE](https://github.com/OpenCommunityEdition/OpenCE): the port that OpenCE-PS3 starts from.
- [bnunu/halo-1](https://github.com/bnunu/halo-1) and [punpckhdq/halo](https://github.com/punpckhdq/halo): the
  decompilation.
- [Invader](https://github.com/SnowyMouse/invader): the tag definitions for the map data's byte swap.

## License

OpenCE-PS3 is free software, licensed under the
[GNU Affero General Public License, version 3 or later](https://www.gnu.org/licenses/agpl-3.0.html)
(AGPL-3.0-or-later). You can use, share and change it under the terms of that license. It comes with no warranty.
See [LICENSE](LICENSE). If you share a package built from OpenCE-PS3, you must also give its full source code under
the AGPL-3.0.

- **What OpenCE-PS3 builds on.** [OpenCE](https://github.com/OpenCommunityEdition/OpenCE) is under the AGPL-3.0-or-later.
  Its commits before its change to the AGPL, and the releases built from them, stay under
  [CC0 1.0](LICENSES/CC0-1.0.txt). The decompilation of [punpckhdq/halo](https://github.com/punpckhdq/halo) is under
  the AGPL 3.0 from 17 October 2026, and under CC0 1.0 before that date.
  [bnunu/halo-1](https://github.com/bnunu/halo-1) is under CC0 1.0.
- **Contributions** are under the AGPL-3.0-or-later. Code taken from a project under the GPL 3.0 keeps that license
  and its notices. Section 13 of each license lets the two be combined.
- **Network play.** If you change the game and let other persons play with your version over a network, the AGPL
  asks you to offer them the source of your version (section 13).
- **Third-party code and art** keep their own licenses, next to them in `port/third_party/` and `port/assets/`.
- **Halo is not covered.** The license covers the work of the authors of OpenCE and OpenCE-PS3. It gives no rights
  in Halo, its maps or anything else that Microsoft owns.

## Disclaimer

OpenCE-PS3 is an unofficial fan project. It is not endorsed by or affiliated with Microsoft, Xbox Game Studios,
Halo Studios, Activision, Bungie or Sony. Halo and Halo: Combat Evolved are trademarks of Microsoft Corporation.
No game data is included. You must provide your own copy of the game.
