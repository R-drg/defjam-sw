# Def Jam: Fight for NY (Xbox) static recompilation — notes for Claude

Port of the Xbox NTSC-U Def Jam: Fight for NY to Linux (and later Switch)
with xboxrecomp, following nfsu2-sw (clone at `../ref/nfsu2-sw`; its
CLAUDE.md is the deep reference for runtime/renderer/Switch findings — most
apply here unchanged). Upstream xboxrecomp clone: `../ref/xboxrecomp`.

## Rules

- Never commit game data (`game/`, `default.xbe`, `switch_sd/`, `dist/`) or
  generated C (`gen/`). Ask before pushing; the user publishes the repos to
  GitHub themselves (R-drg/defjam-sw and R-drg/defjam-sw-installer).
- `game/` is the only copy of the disc (the ISO was deleted 2026-10-08):
  never delete or overwrite it. Saves live in `game/UDATA/45410049/`.
- Clean up every test run (tools kill by PID; never `pkill -f` a pattern that
  matches your own shell).
- **zsh**: `$t:start` is a history modifier. Write `${t}:start` when building
  RECOMP_PAD_SCRIPT strings, and call helpers through a function
  (`r() { python3 -I tools/remote.py "$@"; }`), not `$R` (no word splitting).

## Release (v1.0.0, 2026-10-09)

- `DOCKER=1 tools/release.sh` builds the NRO and puts defjamx.nro,
  defjamx_env.txt (switch/), LICENSES.txt (switch/) and SHA256SUMS in dist/,
  and prints the xbe/nro SHA-256 for the installer's manifest.
- Installer: ../defjam-sw-installer (GitHub Pages site, adapted from
  R-drg/the-simpsons-game-nx-installer / StevensND's nfsmw-nx-installer,
  GPL-3.0). release/manifest.json names the build by default.xbe SHA-256
  (31cc0d11...) and nro_sha256; its deploy workflow downloads defjamx.nro
  from the latest R-drg/defjam-sw release. `node test/package_node.mjs
  ../defjam-sw/game/default.xbe` tests it.
- NACP: "Def Jam: Fight for NY", author Rodrigo Ferreira, version in
  CMakeLists.txt (DEFJAM_SWITCH_VERSION). Icon and loader logo from
  assets/cover.webp (tools/make_logo.py). Credits: README + NOTICE.md
  (nfsu2-sw by Anton Artemov first).
- Switch default RECOMP_GIL_EAGER=1 (main.c).

## Layout

| Where | What |
|---|---|
| `game/` | extracted disc (2.9 GB), from `tools.xiso unpack` |
| `gen/` | lifted C (`tools/regen.sh`), ~92 MB, 70 files |
| `build/` | Linux build (`cmake --build build -j8`, ~40 s for gen changes) |
| `.venv/` | Python with capstone + pytest for the pipeline |
| `runs/` | test logs, frame dumps, remote session (`runs/remote/`) — scratch |
| `xboxrecomp/` | nfsu2-sw's vendored toolkit + `tools/{disasm,func_id,recomp}/output.py` restored from upstream (missing from nfsu2-sw's tree) + this port's fixes below |

## Status (2026-10-08)

Linux GL: boot → EA/THX/intro movies (MAD, lifted decoder) with audio →
APT (Flash) front end → title → autosave dialogs → user ID creation (save
written) → main menu → Battle → One on One → fighter/venue select → **3D
fight at 60 fps**, combos, HUD, full match to KO and back to menu. Story
mode: saved user loads, Sketch Artist creator, in-engine cutscenes, Choose
Style (video preview), first training fight with tutorial prompts. Not
tested: later Story, multiplayer, Switch on hardware.
Switch GL NRO builds cleanly (2026-10-08) in Docker image devkitpro/devkita64
(already on this machine; devkitPro's download site 403s wget):
`docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -e STAGE_GAME=0 -v $PWD:$PWD
-w $PWD devkitpro/devkita64 bash switch/build.sh` -> switch_sd/switch/defjamx/
defjamx.nro (23.7 MB, ~20 min). No emulator installed; hardware logs go to
sdmc:/switch/defjamx/defjamx_log.txt.
Vulkan (`build-vk/`, -DDEFJAM_VULKAN=ON): boot, menus, fight render
correctly on the RTX 5070 (headless RECOMP_VK_HEADLESS=1, ~48 fps there vs
60 GL; windowed not measured). glslang-tools is not installed, so it is
configured with -Dglslang_DIR=$PWD/build-vk-cmake/glslang, a copy of the
system config without the /usr/bin/glslang check (`BIN=build-vk/defjam_recomp`
for run_test.sh / remote.py).

## Title facts

- XBE: DJFFNY, title 0x45410049, XDK 5849, entry 0x001F6357, kernel thunks
  0x00285240. No XNET (nfsu2's nic.c dropped). 2x multisample back buffer
  1280x480 for a 640x480 mode; renders 4:3 (main.c sets RECOMP_WIDESCREEN=0).
- Data: EA `.viv` (BIG) archives, RefPack-compressed (`10 FB`); movies
  `movies/*.mad` (EA Madcow, YUY2 textures fmt 0x24). Front end = EA APT
  (Flash 5 player): `screens/screens.viv`, flow in `screens/feflow.xml`.
- Pads: XAPI maps USB root port 2 -> player 1, 3 -> 2, 0 -> 3, 1 -> 4
  (measured). main.c sets RECOMP_USB_NDP=4, PORT=2, PORT2=3; the title
  screen only listens to player 1.
- Game structure: main loop sub_00011CD0 consumes 60 Hz input frames
  (input object = registry[0x27] at 0x2FB148+0x27*4; ring of 10 x 0x260)
  and runs the update (loading manager registry[0x17], sub_00077040, states
  0 idle / 1 kick / 2 loading / 3 finish) once per frame; render
  sub_00077740. Input frames are produced by thread sub_000D7700 woken by a
  D3D SetTimerCallback (PTIMER) callback 0xD7EA0 -> 0xD7DE0 re-arm.
- D3D (5849, NFSU2's layout at other addresses): device global 0x00234768;
  interrupt dispatcher 0x002239A0 (PMC bit 12 PGRAPH 0x223760, 20 PTIMER
  0x2232E0, 24 vblank 0x2231D0, 8 PFIFO); flip retire index blk+0x1BC.
- APT: host callback table 0x3B1DF0.. (file request 0x69CC0, fscommand
  0x68D20, alloc 0x683C0 / free 0x68D70 via [0x3452A8]); request list on
  [0x3B1D84], loadMovie 0x1423F0 -> 0x148600; async file slots [0x3B37A0];
  RefPack decode 0x1E9040.

## Fixes (what broke and why)

src/recomp_manual.c (title code):
- memmove 0x002016B0 (CRT tail-table jumps the lifter can't follow).
- DirectSound DSP post 0x0025F394 (ack word), AC'97 reset 0x00267B3D
  (hoisted-load spin) — same bugs as NFSU2's 0x32EB65 / 0x33518D.
- D3D BlockOnTime 0x0021EC50: hand the GPU semaphore to the executor.
  Without it the boot hung before the first frame.
- D3D vblank 0x002231D0 / PGRAPH 0x00223760 handlers at DISPATCH (as NFSU2).
- D3D SetTimerCallback 0x00216410: a deadline already past is moved to now +
  1 ms (a late DPC otherwise killed the 60 Hz input chain for good).
- D3D PTIMER handler 0x002232E0: report the alarm taken
  (`xbox_Nv2aPtimerTaken`) or D3D's busy-waits re-run the callbacks.

xboxrecomp runtime:
- **PTIMER emulation** (xbox_memory_layout.c `xbox_Nv2aPtimer*`,
  kernel_bridge.c `kernel_ptimer_tick`): TIME_0/1 count host ns (guest writes
  rebase), ALARM_0 raises PTIMER_INTR + PMC bit 20 + the GPU IRQ; delivered
  only when the handler takes it (retried after 50 ms). Before this the game
  rendered its first loading screen forever (no input frames, no update).
  `RECOMP_PTIMER_TRACE=1|2`.
- **Viewport constants** (nv2a_pb_exec.c `vp_viewport_constant`):
  SET_VIEWPORT_SCALE/OFFSET are transform constants c[58]/c[59] on NV2A. This
  D3D never loads 58/59 directly -> every program vertex at (0,0), black
  screen. `RECOMP_VP_CONST_TRACE=1`.
- **MmQueryStatistics** (kernel_memory.c): AvailablePages = 64 MB - 1 MB -
  image - guest heap/contiguous in use (`xbox_GuestMemoryInUse`). It was a
  flat 32 MB; the game sized an 18 MB heap from it, a 574 KB allocation
  failed, and RefPack decoded a movie into NULL over .text. `RECOMP_AVAIL_MB`.
- USB pad script: left-stick names (`lup ldown lleft lright`) and
  **RECOMP_PAD_CMD=<file>** live control (usb_gamepad.c).
- Linux input (input/xinput_device.c): keyboard fallback for player 1 when no
  controller (RECOMP_KEYBOARD=0 off) and controller hot-plug.
- GIL owner tracking for debuggers (`s_gil_owner_tid`, cached tid).
- Texture formats SZ_A8Y8 0x1A -> LIN_A8L8 0x20 and SZ_AY8 0x01 -> LIN_AL8
  0x1B decoded (nv2a_pb_exec.c); 0x1A is the character eye texture (drawn
  magenta before). nv2a_gl now logs each undecodable format once
  (`[GL] texture ... not decodable (drawn magenta)`) without RECOMP_GL_TRACE.

xboxrecomp translator (tools/recomp/translator.py):
- **Flag materialization at joins** (`_join_flag_needs`,
  `_inject_flag_stores`, test_flag_join_materialize.py, `RECOMP_FLAG_JOIN=0`
  off): a jcc/setcc/cmov at a join whose predecessors set flags differently
  (`dec eax` vs `cmp eax, ebx`) lifted to the never-assigned `_flags`. Now
  each predecessor stores the consumer's condition into _flags on its edge.
  APT's request constructor 0x00147430 freed every file request it created
  because of this — the front end never loaded a movie. 52 stores in this
  title. The 16 failing toolkit tests fail identically without the change.

Seeds (config/seed_functions.json, 23): thread entries 0x001ED080,
0x001F62E3, 0x001F98A2, 0x001242D0; indirect targets incl. APT ActionScript
handlers (table 0x2E95B8: 0x00154810, 0x00159560), 0x00163360, 0x000297E0,
0x001141A0; fight code 0x00038440, 0x001DC400 and the stack-built move
handler table in sub_001B54D0 (0x001D7250, 0x001BD990 -- the disassembler had
swallowed them into a neighbouring jump table); callbacks registered with
`push imm32` (0x000B4E90, 0x001D83B0, 0x00127740, 0x00170B70).
`tools/seed_from_data.py` scans data tables; most hits (SEH funclets at
0x20B000-0x20E000, int3 padding) must NOT be seeded -- check each by
disassembly (prev byte ret/int3, sane prologue).

Verified 2026-10-08: full one-on-one match (~2.5 min, combos, Blazin',
KO) returns to Choose Match Type without a crash; windowed run on the
user's RTX 5070 at 60 fps.

## Debug tools

- `tools/run_test.sh SECS LOG K=V...` headless run, killed by PID (SIGUSR2
  first so DEFJAM_PROF writes).
- `python3 -I tools/remote.py start|press|shot|wait|stop` — live headless
  session; `press "0:a:200,600:ldown:250"`; `shot` -> runs/remote/shot.png
  (Read it). `tools/to_fight.sh` = timed route into a fight (timing varies;
  fall back to remote.py step by step).
- `tools/snap.sh SECS OUT` all-thread stacks via gdb (ASLR on: gdb's ASLR-off
  layout crashes the CRT TLS init); `python3 -I tools/hang_snap.py PATTERN`
  stacks when a log pattern stops (+ GIL/dispatch lock state).
- `tools/bp_count.sh SECS sub_X ... -- K=V` hit counts per lifted function
  (names that don't exist are silently missing from the output).
- `python3 -I tools/peek_run.py SECS VA[:LEN] @PTR+OFF &host_sym --every S`
  guest/host memory of a running game (low VAs < 0x400000 read through a
  different view than the guest's — use gdb `x` on g_xbox_mem_offset+VA for
  .text/.rdata).
- `DEFJAM_PROF=path,hz,delay` + `tools/prof_linux_report.py` sampling profile
  by lifted function (perf is disabled on this host).
- `.venv/bin/python -I tools/disasm_at.py game/default.xbe VA [N]`,
  `python3 -I tools/fn_strings.py sub_X` (strings a function references).
- Renderer: `RECOMP_GL_DRAWLOG=x,y[,lo,hi]` + `touch drawlog.go` in the
  game's cwd: logs the next frame's draws and clears with the pixel each draw
  leaves at title (x,y), and dumps drawlog_<frame>_<n>.bmp after draws lo..hi
  -- which draw paints a region. `RECOMP_GL_TEXLOG=1|2` textures (and
  render surfaces) first sampled/created, with content/palette summary (2:
  all binds every 120th frame). `RECOMP_TILED_ALIAS=0` reproduces the
  Switch's unaliased tiled aperture on Linux. `DEFJAM_LOCK_TRACE=1`.
- Renderer: `RECOMP_GL_DUMP=prefix,N` (BMP every N frames),
  `RECOMP_GL_WATCH=va` (draws sampling/into va: state, consts, vertices,
  readback), `RECOMP_GL_VSH_DUMP=1` (GLSL per new program),
  `RECOMP_GL_YUV_TRACE=1` (video frames' luma), `RECOMP_FPS_LOG=1`.
- `DEFJAM_TIMER_TRACE=1` (SetTimerCallback / PTIMER handler slots).
- Audio capture: `SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=x.raw` (48 kHz s16
  stereo).
- `xboxrecomp: python -m tools.seed_from_log LOG ../game/default.xbe
  --functions tools/disasm/output/functions.json --seeds ../config/seed_functions.json`
  then full `tools/regen.sh`; `tools/seed_cycle.sh SECS ROUNDS K=V` loops it.
- ICALL failures log `called from` (guest [esp]); for the real call site
  break on `recomp_icall_fail_log if $edi == VA` in gdb.

Switch hardware run 1 (2026-10-08): boot, menus, 60 fps; crashed entering a
fight -- exception 257 writing guest 0xF3304004 (tiled aperture; the console
refuses memory aliasing, "second view of a mapping refused (0xD401)"). Caller
sub_0010D690 -> memcpy 0x1ED010 copies the frame buffer. Fix: when the tiled
view cannot be mapped, xbox_memory_layout.c commits separate storage there
(RECOMP_TILED_FALLBACK=0 restores the fault). Logs: runs/switch_log1*.txt.
Switch run 2: fight start OK with the fallback, but the copy was black, and
Sketch Artist's "Positive ID?" mugshot was black on Linux too. Fixes:
- nv2a_gl surf_get reuses a surface when the new clip fits inside it (width
  too, same AA). The mugshot renders with the clip narrowed to 1186x405 and
  back every frame; each change made a fresh black surface. Found with
  RECOMP_GL_DRAWLOG (below).
- Front-buffer copy sub_0010D690 (GetBackBuffer(-1), LockRect 0x21DCB0,
  memcpy, used at fight start): overrides in recomp_manual.c make LockRect
  ask the renderer (xbox_nv2a_read_front hook, serviced on the NV2A ack
  thread via xbox_nv2a_service_hook) to write the last presented picture
  into the locked bits. Not available with RECOMP_GL_THREAD=1 or Vulkan.
- The title never calls D3D's lock functions anywhere else, uses no 2D blits
  (RECOMP_PB_SUBCH_TRACE=1 shows only fence writes), and has one render
  surface (the 1280x480 back buffer); front buffers are never rendered by us.
Copying to a console over DBI's MTP: gio refuses writes into some existing
folders ("Could not send object info") -- copy into a fresh folder, and
remove a file before replacing it.

Switch run 3 (2026-10-09, runs/switch_log4.txt): mugshot + fight-start fixes
on the console. Open: clothing shop slows to 5-30 fps (t=1741-1814 s): frames
of 0.3-4 s with ~200 draws, 0-3 textures, no shader compiles; game thread
~30%, executor ~20%, several guest worker threads 14-21% -- nothing saturated,
so something waits. Not the front-buffer read-back (2 in the whole run).
Next: profile the shop on Linux (DEFJAM_PROF) and check whether it is slow
there too; look at worker threads/file I/O during clothing loads.
Shop on Linux (a console save copied to runs/game_rod/UDATA, game files
symlinked): 60 fps, loader thread sub_001242D0
~3% of a core, reads ~100-1100 per 10 s, ~8 KB each (RECOMP_IO_STATS=1, new
[io] line; on by default on the Switch). So the console's stall is CPU under
the guest lock, not I/O. Test sent to the console: defjamx_env.txt with
RECOMP_GIL_EAGER=1 (main thread pre-empts lock holders) + RECOMP_NX_PROFILE=1
(prof.bin; tools/prof_report.py copied from nfsu2-sw, needs devkitA64 nm --
run it in the docker image).
4-player match on the console (runs/switch_log6.txt, prof6.bin; slice by the
"[prof] N samples written" lines): 12-21 fps, nv2a_ack_thread (executor +
GL) at 91-94% of a core, game thread ~18%. Profile: swizzle_index 35%,
tex_get 11% (inlined palette decode), Mesa convert_ubyte 4%, nvmap ioctls --
textures re-decoded ~150/s. Cause: tex_bytes() returned w*h*4 for every
swizzled format, so an 8-bit palette texture's sampled hash covered 4x its
size and writes next to it "changed" it every frame. Fixed (exact texel
size), plus: palette VA in the cache key (RECOMP_GL_PAL_KEY=0 old),
swizzle via per-column/row tables, palette decoded to RGBA (no Mesa
conversion), glTexSubImage2D into existing storage. Linux 4-way FFA: 1100-
1500 decodes/10 s -> 7-36. Debug: RECOMP_GL_TEXLOG=3 logs every decode;
RECOMP_FPS_LOG line now counts decodes.
