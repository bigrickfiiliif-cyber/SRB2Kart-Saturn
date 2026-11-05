# SRB2Kart Saturn – Nintendo 3DS port

A port of the [SRB2Kart Saturn](https://github.com/Indev450/SRB2Kart-Saturn)
client to the **New Nintendo 3DS / New 3DS XL / New 2DS XL**. It also starts
on the original 3DS/2DS, but runs much too slowly there to be playable.

The engine is unchanged Saturn code (the Lua extensions, Saturn menus and
Saturn netcode/resync included) plus a native libctru platform layer in
`src/n3ds/`. It does not use SDL.

| Part | Implementation |
|---|---|
| Video | **GPU renderer (default):** Saturn's hardware renderer with a citro3d/PICA200 driver (`src/n3ds/r_citro3d.c`). **Software renderer** (`-software` in args.txt): CPU rendering to the top screen, 400×240 or 320×200 |
| Audio | Own software mixer → NDSP, 32 voices, 32728 Hz; mixer thread runs on core 2 (New 3DS) |
| Music | Ogg Vorbis via Tremor (LOOPPOINT/LOOPMS/LENGTHMS loop tags), MOD/XM/S3M/IT via libxmp |
| Input | Buttons, Circle Pad, C-stick, ZL/ZR, touch screen, system keyboard (swkbd) |
| Network | UDP netplay via soc:u, master server + HTTP addon downloads via libcurl/mbedTLS |
| Threads | libctru threads (HTTP/master server workers) |

## Requirements

* A 3DS with custom firmware (Luma3DS), plus either the Homebrew Launcher
  (.3dsx) or FBI to install the CIA.

**Which file?** The CI artifact contains both. The **.cia** is recommended
on the New 3DS: it gets 178 MB of RAM (the .3dsx gets about 96 MB) and is
guaranteed the 804 MHz clock + L2 cache. Copy it to the SD card, install it
with FBI, and start it from the HOME Menu. The game data still goes into
`sdmc:/3ds/srb2kart/`.
* **DSP firmware dump** for sound: run the *DSP1* homebrew once, which writes
  `sdmc:/3ds/dspfirm.cdc`. Without it the game runs, but with no sound.
* The **SRB2Kart v1.6 data files** from the official release
  (<https://github.com/STJr/Kart-Public/releases>).

## Setting up the SD card

```
sdmc:/3ds/srb2kart-saturn.3dsx      <- the program (or install the .cia)
sdmc:/3ds/srb2kart/
    srb2.srb
    gfx.kart
    textures.kart
    chars.kart
    maps.kart
    sounds.kart
    music.kart
    (extra.kart, extra2.kart, extra3.kart, if you have them)
    args.txt                        <- optional, see below
```

The config file (`kartconfig.cfg`), game data, replays, `logs/log.txt` and
the `addons/` and `DOWNLOAD/` folders are also written to `sdmc:/3ds/srb2kart/`.

### args.txt (optional)

Extra command-line options, separated by spaces or newlines. For example:

```
-bottomlog          keep the log on the bottom screen while playing
-nosound            no audio
-width 320 -height 200   faster lower-resolution mode
-connect 1.2.3.4    connect to a server straight away
```

## Controls

| 3DS | Game |
|---|---|
| Circle Pad / D-pad | steer, menus |
| A | accelerate / confirm |
| B | brake / back |
| R | drift |
| L | use item |
| X | look back |
| Y | change view |
| START | menu |
| SELECT | pause |
| SELECT + START | console |
| C-stick | analog axis 2 (free to bind) |
| ZL / ZR | JOY8 / JOY9 (free to bind) |

**Touch screen**
* left half: type a console command (system keyboard)
* right half: send a chat message
* bottom row (hold): scoreboard
* while a text field, the console or chat is open, any touch opens the keyboard and types into it

All buttons can be rebound in the game's control menu (they show up as JOY1–JOY11 / HAT1).

## Performance

**Emulators (Azahar/Citra):** emulators count emulated CPU cycles and run the
old-3DS clock (268 MHz) by default, so FPS shown there is much lower than on
a real New 3DS at 804 MHz. The log line `CPU clock: ~266 MHz` in an emulator
is expected. To get closer to New 3DS speed in the emulator, enable New 3DS
mode and raise the emulated CPU clock (about 300%) in its settings.


What the port does out of the box:

* **Multi-core rendering (New 3DS):** Saturn's parallel software renderer
  (floors, ceilings, sprites) runs with a worker thread pinned to CPU core 2.
  The main thread on core 0 helps while it waits. The log says
  "Render worker running on CPU core 2" when that works.
* **No needless vsync waits:** frames are only held back when they would
  otherwise tear. Frame output is converted on CPU core 2.
* **Frame rate:** `fpscap` stays at 60 (refresh rate). If your 3DS can't hold
  60 on a map, `fpscap 30` gives a steady 30 instead of jumping between the two.
* **Cheaper defaults:** skybox off, `drawdist 2048`, less weather, no water
  ripples, no splats, no texture precaching (saves RAM). These only change
  how the game looks on your screen, so online games stay in sync with PC
  players.

If you ran an older build before, **delete `sdmc:/3ds/srb2kart/kartconfig.cfg`
once**. Otherwise the old values stored in it override the new defaults
(`parallelsoftware` in particular).

More tweaks if it is still too slow:

* `vid_mode 1` (320×200) renders about a third fewer pixels.
* Lower `drawdist` further (`1024`).
* `fpscap 30` for steady pacing on heavy maps.

## Building

You only need Docker:

```sh
./3ds/build.sh            # -> 3ds/srb2kart-saturn.3dsx
./3ds/build.sh clean
```

Without Docker, with devkitPro installed (`3ds-dev`, `3ds-curl`,
`3ds-mbedtls`, `3ds-libpng`, `3ds-zlib`, `3ds-libvorbisidec`, `3ds-libogg`,
`3ds-libxmp`):

```sh
cd 3ds && make -j$(nproc)
```

### CIA

With [makerom](https://github.com/3DSGuy/Project_CTR) and
[bannertool](https://github.com/diasurgical/bannertool) in your `PATH`:

```sh
cd 3ds && make cia
```

## Technical notes / differences from the PC version

* The 3DS has no OpenGL. The GPU renderer reuses Saturn's hardware renderer
  (`src/hardware`) and implements its driver interface (`hw_gl.h`) with
  citro3d. Not supported on the GPU path yet: GLSL shaders (lighting uses the
  fixed-function fallback), stencil portals, the sky dome, 3D models, and
  screen effects (wipes, VHS). If something looks wrong, add `-software` to
  `args.txt` to use the CPU renderer.
* `int32_t` is retargeted to `int` (newlib defines it as `long` on ARM), and
  the build uses `-fsigned-char` and no `-ffast-math`, so game logic gives
  bit-for-bit the same results as the PC builds. That is a requirement for
  netgames against PC players.
* MIDI and GME (VGM/SPC) music are not supported. The standard music.kart is
  Ogg only.
* On old 3DS models, or when core 2 isn't available, the thread pool runs inline (single-threaded).
* The bottom screen shows the boot log, then a control overview.
