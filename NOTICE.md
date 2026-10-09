# Credits and third-party notices

defjam-sw is by **Rodrigo Ferreira** ([@R-drg](https://github.com/R-drg)). It stands on the work of the people
below. Thank you all.

## nfsu2-sw — Anton Artemov ([@antoxa2584x](https://github.com/antoxa2584x))

**[nfsu2-sw](https://github.com/antoxa2584x/nfsu2-sw)**, the static recompilation of Need for Speed: Underground 2
for Linux and Nintendo Switch, is the project this port is modelled on and built from. Its vendored copy of
xboxrecomp — with the NV2A pushbuffer executor, the OpenGL and Vulkan renderers, the SDL audio path, the Switch
platform layer (memory without aliasing, the loading screen, the console profiler, the clock control), the input
and USB work and many translator fixes — is the base of `xboxrecomp/` here, and `src/main.c`, `src/switch_nx.c`,
`switch/build.sh` and most of `tools/` started as its files. Its notes (its `CLAUDE.md`) answered most questions
this port ran into before they were asked. Without nfsu2-sw there would be no Def Jam on the Switch.

## xboxrecomp — Ned Heller ([@sp00nznet](https://github.com/sp00nznet)) and contributors

**[xboxrecomp](https://github.com/sp00nznet/xboxrecomp)** is the toolkit that lifts an Xbox executable to C and
runs it: the x86 disassembler and lifter, XBE parsing, the kernel/XAPI runtime, the D3D8 and NV2A translation and
the XISO tools. MIT licence (`xboxrecomp/LICENSE`); its contributors are listed in `xboxrecomp/CONTRIBUTORS.md`.

It includes code extracted from **[xemu](https://github.com/xemu-project/xemu)** (the MCPX APU and NV2A register
definitions; © espes, Jannik Vogel, Matt Borgerson), under the GNU LGPL 2.1 or later. The files and their
copyrights are listed in `xboxrecomp/NOTICE`, the licence text is `xboxrecomp/LICENSES/LGPL-2.1.txt`.

## The installer

The installer page, [defjam-sw-installer](https://github.com/R-drg/defjam-sw-installer), is adapted from
[the-simpsons-game-nx-installer](https://github.com/R-drg/the-simpsons-game-nx-installer), itself from
[nfsmw-nx-installer](https://github.com/StevensND/nfsmw-nx-installer) by **StevensND** (GPL-3.0).

## Libraries in the Switch build

| Library | Licence |
|---|---|
| [libnx](https://github.com/switchbrew/libnx) and the [devkitPro](https://devkitpro.org) toolchain (devkitA64, newlib) | ISC (libnx); see each package |
| [SDL2](https://www.libsdl.org) (devkitPro's switch-sdl2) | zlib |
| [Mesa](https://www.mesa3d.org) with the nouveau driver and libdrm_nouveau (devkitPro's switch-mesa) | MIT |

The full texts that ship with the program are in `switch/LICENSES.txt`.

## Tools used to build it

[Capstone](https://www.capstone-engine.org) (BSD-3-Clause) disassembles the executable during lifting; it is not
part of the program.

## The game

*Def Jam: Fight for NY* is © 2004 Electronic Arts Inc., developed by AKI Corporation. Def Jam and its marks belong
to their respective owners. This is an unofficial fan project, not affiliated with or endorsed by Electronic Arts,
AKI Corporation, Def Jam Recordings, Microsoft or Nintendo. No game data is included: you need your own copy of the
game. The cover art in `assets/` (the program's icon and loading-screen logo) belongs to its owners and is used to
identify the game; it is not covered by this repository's licence.
