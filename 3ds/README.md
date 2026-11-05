# SRB2Kart Saturn – Nintendo 3DS port

A port of the [SRB2Kart Saturn](https://github.com/Indev450/SRB2Kart-Saturn)
client to the **New Nintendo 3DS / New 3DS XL / New 2DS XL**. It also starts
on the original 3DS/2DS, but runs much too slowly there to be playable.

The engine is unchanged Saturn code (the Lua extensions, Saturn menus and
Saturn netcode/resync included) plus a native libctru platform layer in
`src/n3ds/`. It does not use SDL.

| Part | Implementation |
|---|---|
| Video | Software renderer → top-screen framebuffer (RGB565), 400×240 1:1 or 320×200 stretched |
| Audio | Own software mixer → NDSP, 32 voices, 32728 Hz; mixer thread runs on core 2 (New 3DS) |
| Music | Ogg Vorbis via Tremor (LOOPPOINT/LOOPMS/LENGTHMS loop tags), MOD/XM/S3M/IT via libxmp |
| Input | Buttons, Circle Pad, C-stick, ZL/ZR, touch screen, system keyboard (swkbd) |
| Network | UDP netplay via soc:u, master server + HTTP addon downloads via libcurl/mbedTLS |
| Threads | libctru threads (HTTP/master server workers) |

## Requirements

* A 3DS with custom firmware (Luma3DS) and the Homebrew Launcher, or FBI to install the CIA.
* **DSP firmware dump** for sound: run the *DSP1* homebrew once, which writes
  `sdmc:/3ds/dspfirm.cdc`. Without it the game runs, but with no sound.
* The **SRB2Kart v1.6 data files** from the official release
  (<https://github.com/STJr/Kart-Public/releases>).

## Setting up the SD card

```
sdmc:/3ds/srb2kart-saturn.3dsx      <- the program (or install the .cia)
sdmc:/3ds/srb2kart/
    srb2.srb
    patch.dta
    gfx.kart
    textures.kart
    chars.kart
    maps.kart
    patch.kart
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

## Performance tips

* `vid_mode 1` (320×200) renders about a third fewer pixels than 400×240.
* Lower `drawdist`, turn off `skybox`, and cap the frame rate at the game tic rate (`fpscap 35`)
  to get a steady picture.
* Prefer the CIA: on the New 3DS it gets 178 MB of RAM instead of the
  ~124 MB of a 3dsx started from the Homebrew Launcher. Big maps or addons
  can run out of memory otherwise.

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

* No OpenGL renderer (the 3DS GPU, a PICA200, has no OpenGL); software renderer only.
* `int32_t` is retargeted to `int` (newlib defines it as `long` on ARM), and
  the build uses `-fsigned-char` and no `-ffast-math`, so game logic gives
  bit-for-bit the same results as the PC builds. That is a requirement for
  netgames against PC players.
* MIDI and GME (VGM/SPC) music are not supported. The standard music.kart is
  Ogg only.
* `parallelsoftware` is off by default, and the thread pool runs inline.
* The bottom screen shows the boot log, then a control overview.
