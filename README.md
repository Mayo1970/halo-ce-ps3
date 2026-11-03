<h1 align="center">Warthog PS3</h1>

<p align="center"><b>Chupathingy on the PlayStation 3.</b></p>

<p align="center">
<a href="https://discord.gg/chupathingy">Discord</a> ·
<a href="https://stats.chupathingy.org">Games online now</a> ·
<a href="https://github.com/ChupathingyCE/chupathingyce">The game</a> ·
<a href="https://chupathingy.org">chupathingy.org</a>
</p>

Halo: Combat Evolved on the PlayStation 3, built with the open-source
[PSL1GHT](https://github.com/ps3dev/PSL1GHT) SDK. It plays online with the
Chupathingy builds on the other platforms.

The port is made and maintained by **Matt (Mayo1970)**. His own repository,
[Mayo1970/halo-ce-ps3](https://github.com/Mayo1970/halo-ce-ps3), stays his and
follows this one.

**Status: work in progress.** See [Known limits](#known-limits).

You need your own copy of Halo. No game files or SDK files are included.

This repository holds the PS3 port. The game itself is the `chupathingyce`
submodule. For now it points at Matt's `ps3` branch of the game; those
changes are moving into the game's main branch one pull request at a time,
and then the submodule follows ChupathingyCE/chupathingyce. The other
consoles have their own repositories:
[warthog-xbox](https://github.com/ChupathingyCE/warthog-xbox),
[warthog-360](https://github.com/ChupathingyCE/warthog-360),
[warthog-wiiu](https://github.com/ChupathingyCE/warthog-wiiu) and
[warthog-switch](https://github.com/ChupathingyCE/warthog-switch).

## Install and set up

1. Download `halo_ps3.pkg` from the [Releases page](https://github.com/ChupathingyCE/warthog-ps3/releases/latest).
2. Install it with the Package Manager on the XMB (CFW or PS3HEN).
3. Extract the `maps` folder from your Xbox disc image of Halo: Combat Evolved. A Chupathingy build for the PC
   copies the maps out of the disc image at its first start.
4. Copy the `maps` folder to `/dev_hdd0/data/HaloCE/`, for example with FTP. Do not change the files.
5. Start Halo: Combat Evolved from the Game column.

**Chupathingy-PS3 does not include the game data.** You must provide your own copy of the game.

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
- **LAN play** with the Chupathingy builds for the PC on the same network.
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
| BRIGHTNESS | 0 to 10. Higher lifts the dark and middle tones, lower darkens them | 5 |
| TOGGLE CROUCH | A press crouches, the next press stands up. OFF: crouch while the button is held | OFF |

### Known limits

**Custom Edition maps** are not supported.

**Split screen** always plays at 30 frames per second.

**Invite codes** work only on the PS3. The Chupathingy builds for the other platforms take only invite links,
and they refuse a code as a link that is not valid. They cannot join a game that a PS3 hosts by its code.

Some heavy fights drop below 60 frames per second. DYNAMIC RESOLUTION or the QUALITY preset helps.

## Compiling

The build makes `build/ps3/halo_ps3.pkg`. The maintainer builds in Docker with `ppu-gcc` 7.2.0.

The game's code is the `chupathingyce` submodule. Clone with it:

```sh
git clone --recursive https://github.com/ChupathingyCE/warthog-ps3.git
```

If you already cloned without `--recursive`, run `git submodule update --init` in the repository folder.

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
| `DEBUG=0` | The retail build, without the UDP log. The release packages use it. The default is `DEBUG=1`. |

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

Chupathingy-PS3 is an AI-assisted project.

## Credits

- [ChupathingyCE](https://github.com/ChupathingyCE/chupathingyce): the game that Chupathingy-PS3 starts from.
- [bnunu/halo-1](https://github.com/bnunu/halo-1) and [punpckhdq/halo](https://github.com/punpckhdq/halo): the
  decompilation.
- [Invader](https://github.com/SnowyMouse/invader): the tag definitions for the map data's byte swap.

## License

Chupathingy-PS3 is free software, licensed under the
[GNU General Public License, version 3 or later](https://www.gnu.org/licenses/gpl-3.0.html)
(GPL-3.0-or-later). You can use, share and change it under the terms of that license. It comes with no warranty.
See [LICENSE](LICENSE). If you share a package built from Chupathingy-PS3, you must also give its full source code
under the GPL-3.0.

- **What Chupathingy-PS3 builds on.** [ChupathingyCE](https://github.com/ChupathingyCE/chupathingyce) is under the
  GPL-3.0-or-later, with a permission for console SDKs. Its releases up to 0.7.5d stay under CC0. The details are in
  its [docs/license.md](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/license.md).
- **Earlier versions.** The versions of this repository before the move to ChupathingyCE stay under the licenses
  they were published with.
- **Contributions** are under the GPL-3.0-or-later.
- **Third-party code and art** keep their own licenses, next to them in `port/third_party/` and `port/assets/`.
- **Halo is not covered.** The license covers the work of the authors of ChupathingyCE and Chupathingy-PS3. It gives
  no rights in Halo, its maps or anything else that Microsoft owns.

## Disclaimer

Chupathingy-PS3 is an unofficial fan project. It is not endorsed by or affiliated with Microsoft, Xbox Game Studios,
Halo Studios, Activision, Bungie or Sony. Halo and Halo: Combat Evolved are trademarks of Microsoft Corporation.
No game data is included. You must provide your own copy of the game.

## License

GPL-3.0-or-later (LICENSE). The history of this repository is Matt's PS3
work from Mayo1970/halo-ce-ps3, with his name and dates.
