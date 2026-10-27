<p align="center"><img src="assets/icon/icon.png" alt="Virtua Fighter 2 Recomp" width="160"></p>

<h1 align="center">Virtua Fighter 2 Recomp</h1>

<p align="center"><b>Virtua Fighter 2 (SEGA, arcade, Model 2, 1994) as a native PC game: the game's own i960 program,
recompiled to native code, with render scales up to 6x, widescreen and smooth motion at any refresh rate.</b></p>

<p align="center">
  <a href="https://github.com/mr-finster/vf2-recomp/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/mr-finster/vf2-recomp?label=release"></a>
  <a href="LICENSE"><img alt="Licence: MIT" src="https://img.shields.io/badge/licence-MIT-blue"></a>
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-macOS%20%7C%20Windows%20%7C%20Linux%20%2F%20Steam%20Deck-informational">
</p>

<p align="center">
  <a href="https://vf2-recomp.pages.dev/">Play</a> ·
  <a href="https://github.com/mr-finster/vf2-recomp/releases/latest">Download</a> ·
  <a href="https://youtu.be/mpaUiJwlYu8">Trailer</a> ·
  <a href="#status">Status</a> ·
  <a href="#controls">Controls</a> ·
  <a href="https://github.com/mr-finster/vf2-recomp/issues">Report a bug</a>
</p>

<p align="center">
  <a href="https://vf2-recomp.pages.dev/img/montage.jpg"><img src="https://vf2-recomp.pages.dev/img/montage.jpg" alt="Virtua Fighter 2 Recomp at 3x: fights on five stages and the widescreen player select" width="800"></a>
</p>

<p align="center">
  <a href="https://youtu.be/mpaUiJwlYu8"><img src="https://img.youtube.com/vi/mpaUiJwlYu8/maxresdefault.jpg" alt="Watch the trailer" width="560"></a>
  <br><sub>Trailer (click to watch on YouTube)</sub>
</p>

> [!IMPORTANT]
> No game files are included: no ROMs and nothing made from them. You need your own dump of the game as MAME's
> `vf2` set (`vf2.zip`, Version 2.1). It is read on your computer, and the game's program is recompiled there each
> time it starts. An unofficial fan project, not made, approved or supported by SEGA, and not affiliated with it.

## About

Virtua Fighter 2 Recomp runs the game's own program, for the board's Intel i960 CPU, recompiled to native code
(ARM64 or x86-64) from your ROM each time it starts, and checked instruction for instruction, cycle for cycle
against MAME's i960 interpreter. Only the i960 program is recompiled: the rest of the Model 2A board (the TGP
geometry processor, the geometrizer and polygon renderer, the System 24 tile layers, and the sound board's 68000
and SCSP) runs on code from MAME, inside an SDL 3 host. At 1x you get the arcade's frames and sound as MAME makes
them (pixel for pixel on Apple silicon; on x86-64 a few texels a frame round differently, the renderer's
multiply-adds not fused); above it, render scales up to 6x, widescreen and ultrawide, and smooth motion at your
screen's refresh rate.

## Status

| Area | State |
| --- | --- |
| The i960 program, recompiled to native code | Working |
| TGP, geometrizer and renderer, tile layers, sound board | Working: they run on MAME's code |
| Render scale up to 6x, widescreen and ultrawide, smooth motion | Working |
| Menu, keyboard and gamepads, two players, the arcade's settings | Working |
| macOS, Windows, Linux / Steam Deck | Working |
| The 3D drawn on the GPU (Display > 3D renderer: Auto, GPU or CPU) | Working on macOS (Metal), Windows (Direct3D 12 or Vulkan) and Linux / Steam Deck (Vulkan): the same picture as the CPU's, checked when the game starts; Auto uses the GPU unless it's clearly slower, and any difference or error falls back to the CPU |
| Stereo reverb (the sound board's DSP) | Working: wide stereo, as on the cabinet (fixed in v0.1.2) |
| Portable mode, your own ROM folder | Working |
| Secret characters (Menu > Cheats): Dural, MUE and KOJAC | Working |
| Online play | Experimental: rollback netplay, a random opponent or a friend by room code, no setup (see [Online play](#online-play-experimental)) |

## Secret characters

Menu > Cheats > **Secret character** puts a fighter the arcade never let you pick in Akira's spot on the player
select, for either player: **Dural**, the final boss, or **MUE** and **KOJAC**, two unused fighters still in the
game's code. Changing it restarts the game.

**How it works.** VF2's fighters are numbered in its code: Akira 0 to Shun 8, Dural 9, Lion 10, and, after him,
MUE 11 and KOJAC 12, which the arcade's select never offers. The player select turns a slot into a fighter from a
table (Akira's slot is fighter 0); the reads that set the fighter now take Akira's slot from a copy of that table
with the secret character in it, while the reads that draw the name and profile keep the original. So the game
loads the secret character exactly as it loads anyone else: their own model, moves, voice and health bar name,
for either player, in any mode.

<p align="center">
  <img src="https://github.com/user-attachments/assets/8ce2e641-575f-49da-bbfd-f3351824860f" alt="The secret characters in action at 2x: Dural, MUE and KOJAC on five stages, and Dural's portrait on the player select" width="800">
</p>
<p align="center">
  <img src="https://github.com/user-attachments/assets/a78b7e81-5c26-47ff-8c63-8485b6c6b77b" alt="The cast's select portraits, and Dural, MUE and KOJAC below" width="800">
</p>

| Character | Select screen | Health bar | Notes |
| --- | --- | --- | --- |
| Dural | Her own portrait (the game's own); Akira's name and profile | DURAL | Complete: the final boss's model and moves, the chrome body, a darker one on player 2's side |
| MUE | Lion's portrait; Akira's name and profile | AKIRA | Lion's body in a black vest, with Akira's moves and voice but Lion's stance and walk |
| KOJAC | Lion's portrait; Akira's name and profile | JACKY | Lion's body in a white vest, with Akira's moves and voice but Lion's stance and walk |

They take Akira's place, so Akira can't be picked while one is on; the computer's own fighters, Dural at the end of
arcade mode included, are unchanged.

The arcade's own Dural code works too, the arcade way (Menu > Game > Play style): on Version 2.1 (the default), hold
Start while you pick any fighter. The PC way, Start is a tap, so a pick confirmed or a menu closed with Start never
turns into Dural by accident; Cheats > Secret character gives him there. Holding Up while you pick gives that
fighter's other costume, the one player 2 wears in a mirror match.

### Who they are

- **Dural**: the final boss of Virtua Fighter and Virtua Fighter 2, a silver cyborg who fights with a mix of the
  cast's moves. The arcade never let you choose her outright; the select's table even has an eleventh, hidden slot
  for her, and the game carries a select portrait for her too.
- **MUE and KOJAC**: two fighters numbered after Lion, named MUE and KOJAC in the game's fighter lists (KOJACKY in
  its test menu). [The Cutting Room Floor](https://tcrf.net/Virtua_Fighter_2_(Arcade)) found them in the game's
  debug mode: Lion's first costume with a different vest (black for MUE, white for KOJAC), Akira's moves and voice,
  and Lion's idle and walking animations. They may have been debug test characters, or placeholders for fighters
  that were planned and never made.

## Play

From the [latest release](https://github.com/mr-finster/vf2-recomp/releases/latest), or the
[site](https://vf2-recomp.pages.dev/):

| System | Download |
| --- | --- |
| macOS 11+ (Apple silicon and Intel) | `VirtuaFighter2Recomp-mac.zip` |
| Windows 10 or 11 (64-bit) | `VirtuaFighter2Recomp-windows.zip` |
| Linux / Steam Deck (x86-64) | `VirtuaFighter2Recomp-deck.tar.gz` |
| Linux on ARM (ARM64: a Raspberry Pi 5; Ubuntu 22.04, Debian 12, Raspberry Pi OS bookworm or newer), unpacked and run as the x86-64 Linux one | `VirtuaFighter2Recomp-linux-arm64.tar.gz` |

**Mac and Windows**

1. Unzip it (Mac: move **Virtua Fighter 2 Recomp** to Applications; Windows: the VirtuaFighter2 folder anywhere, not inside Program Files).
2. Open it (Windows: `VirtuaFighter2.exe`).
3. Choose your `vf2.zip` when it asks. A copy is kept, so you only do this once.

**Steam Deck**

1. Copy the download to the Deck (e.g. Downloads) and unpack it there: in Desktop Mode, right-click it in the file
   manager > Extract > Extract archive here. (Unpacking on Windows, or copying the folder through a USB stick, can
   lose the files' permissions, and then nothing launches.)
2. Put your `vf2.zip` in the `VirtuaFighter2/roms` folder.
3. Desktop Mode: Steam > Games > Add a Non-Steam Game to My Library > Browse, set the file type filter to "All
   files", pick the `vf2` file in the VirtuaFighter2 folder, Add Selected Programs. In its Properties, set Start In
   to the VirtuaFighter2 folder (e.g. `/home/deck/Downloads/VirtuaFighter2/`), and under Compatibility leave "Force
   the use of a specific Steam Play compatibility tool" off: it's a native Linux game, and Proton stops it launching.
4. Back in Game Mode, launch it from your Library with the default Gamepad layout.

The Windows download also runs on the Deck under Proton (add `VirtuaFighter2.exe` instead, with Proton forced on),
but the native Linux build above is the one to use.

On another x86-64 Linux PC: unpack it anywhere, put `vf2.zip` in its `roms` folder and run `VirtuaFighter2.sh`.

**Portable mode:** put a file named `portable.txt` next to the program (Windows: beside `VirtuaFighter2.exe`; Mac:
beside the app, in the same folder) and the game keeps everything in that folder: settings, saves, screenshots,
`log.txt` and the copy of your `vf2.zip` (in its `roms` folder), with nothing in your user data folder (`%APPDATA%`,
`~/Library/Application Support`). The Linux / Steam Deck build keeps everything in its folder already.

**Your own ROM folder** (e.g. Batocera's `/userdata/roms/ports/vf2`, or any folder an update won't touch): start the
game with `--roms <folder>` (or `--roms=<folder>`), set the environment variable `VF2_ROMS`, or add
`roms_folder = <folder>` to `settings.ini`; the command line wins, then `VF2_ROMS`, then `settings.ini`. `~` is your home
folder, and a relative path in `settings.ini` is from its folder. The game looks there first for `vf2.zip` and
only reads it (no copy is kept anywhere else); `log.txt` names the folder it used. If the zip isn't there, the log
says where it looked and the game carries on as without the option; a zip you then choose with the file picker is
copied into your ROM folder, so the next start finds it there. Examples: Windows `VirtuaFighter2.exe --roms "D:\ROMs\vf2"`;
Mac `open "Virtua Fighter 2 Recomp.app" --args --roms ~/ROMs/vf2`; Linux / Batocera
`./VirtuaFighter2.sh --roms /userdata/roms/ports/vf2`.

> [!NOTE]
> The Mac and Windows builds aren't signed. On macOS, open it once, close the message, then System Settings >
> Privacy & Security > Open Anyway. On Windows, if SmartScreen warns, choose More info > Run anyway.

## Controls

Change them in the menu (<kbd>Esc</kbd>, or View / Select on a pad).

| Key | Does |
| --- | --- |
| <kbd>↑</kbd> <kbd>↓</kbd> <kbd>←</kbd> <kbd>→</kbd> | Player 1: move |
| <kbd>Z</kbd> <kbd>X</kbd> <kbd>C</kbd> | Player 1: guard, punch, kick |
| <kbd>1</kbd> | Player 1: start |
| <kbd>I</kbd> <kbd>J</kbd> <kbd>K</kbd> <kbd>L</kbd> | Player 2: move |
| <kbd>B</kbd> <kbd>N</kbd> <kbd>M</kbd> | Player 2: guard, punch, kick |
| <kbd>2</kbd> | Player 2: start |
| <kbd>Esc</kbd> | Menu: display (render scale, picture shape, antialiasing, 3D renderer...), sound, game, arcade settings, controls, cheats, quit |
| <kbd>P</kbd> | Pause |
| <kbd>F3</kbd> | Reset |
| <kbd>F5</kbd> / <kbd>F6</kbd> | Render scale / picture shape |
| <kbd>F9</kbd> | Frame timings |
| <kbd>F11</kbd> | Fullscreen |
| <kbd>F12</kbd> | Screenshot |

Gamepad (Xbox layout): d-pad or stick to move, X guard, A punch, B kick, Y guard + punch (throws), RB punch + kick,
LB guard + kick, RT all three, Start to start. View (tap), L3 + R3 or the touchpad click opens the menu (Menu can be
moved in Controls); View + LB / RB change the render scale / picture shape. In Arcade mode, set Coin in Controls (View
works) for a coin button.

## Online play (experimental)

Rollback netplay, as modern fighting games have it: your own inputs show at once (after the input delay), and the
game quietly corrects the moment your opponent's arrive. Both players need **the same online version** (every release
from v0.1.16 on plays the others, until one changes the game: its notes will say; a build of your own
between releases plays only the same build) and **the same
`vf2.zip`**; it tells you if not. Matches always play the PC way, free play, with the
host's arcade settings (rounds, difficulty, energy). The stage is a random one for the first fight, the game's own order after it, as on an arcade cabinet that's been
on all day. Each player uses their own
Player 1 controls.

**A random opponent:** Menu > **Online** > **RANDOM**. It finds whoever else is looking, the nearest first (the
best connection), and widens the search the longer you wait; **Stop** gives up. Play solo while you wait: the match
takes over once it's found. After the match, **New opponent** looks again.

**With a friend, by room code (anywhere: no setup, nothing to forward):**

1. Both: Menu > **Online**. The first time, it asks your name (up to 10 letters, shown under your energy bar,
   with your country's flag).
2. One player: **HOST**. It shows a room code like `FR7-W25` (**Copy code** puts it on the clipboard): send it to
   the other player.
3. The other: **JOIN**, and type the code (or paste it). It joins by itself at the 6th letter; the steps on the screen
   show how far it's got. Once you're both in, the game starts at its title screen: both press Start.

On a controller the code and name screens show letters to pick with A (X deletes, Y pastes); on a keyboard just type.
In a match, whoever loses is told to press Start for a rematch (as in the arcade, the winner plays on against the
computer until they do). After leaving a match, **Play again** keeps the same code: the host opens it again and the other player joins it by itself.

The games connect to each other directly whenever they can; when a network won't let them (phones' 5G, some
office, school and hotel networks), the match goes through a relay (Cloudflare's) instead, automatically. The
connection display says RELAY then (DIRECT otherwise). The room code only finds the other player: no game data goes through the
lobby. The flags are the countries Cloudflare places each player's connection in (a VPN shows its own).

**If it won't connect**

- The Online screen says what went wrong in plain words, under **Couldn't connect**, and which step it got to:
  check the code, and that the host is still hosting (a room closes when the host stops). **Try again** goes again.
- **Firewalls:** the first time you host, allow the game when asked. Windows: if you dismissed it, Windows Security >
  Firewall > Allow an app, tick Virtua Fighter 2 Recomp for **Private and Public**. Mac: System Settings > Network >
  Firewall > Options: allow it; each new version of the (unsigned) Mac app may need allowing again. Also System
  Settings > Privacy & Security > Local Network: on.
- Hotel, school and public Wi-Fi often keep devices on the same network from seeing each other: the match goes
  through the relay instead, by itself.

**For the smoothest match**

- A cable beats Wi-Fi. **Steam Deck:** turn off Wi-Fi power management (Settings > System > Developer Mode on, then
  Settings > Developer > Enable Wi-Fi power management off): it holds packets back in bursts.
- **Input delay** (Online > Settings): Auto uses as little as it can (none on a LAN, 1 frame almost anywhere else)
  and accounts for slower machines: each side tells the other how much rollback it can run without a hitch (a Steam
  Deck in a fight: none), and the other side's delay covers the rest, so neither side stutters. It keeps the game at
  full speed against a far opponent with more delay instead (up to 15 frames). A short burst of ping is left to rollback; a delay raised for a
  longer spell comes back down a frame a second once it's over.
  Or set 0 to 10 yourself: higher means fewer corrections, lower a quicker response.
- **Connection display:** On shows the other player, the ping, the input delay, the rollback and DIRECT or RELAY at
  the bottom of the screen (the game's own bars and time are at the top), each in a cell of its own that doesn't move
  as the numbers change (the dot green, yellow or red); Detailed adds a row: the jitter, the average rollback, how far
  ahead your side runs and each frame's cost. Under about 100 ms ping plays well; past ~250 ms the delay rises to keep
  the game at its full speed.

**Good to know**

- The menu doesn't pause a match (it goes on behind it, your inputs off). Leaving a match tells the other player.
- Your opponent can see your IP address, as in any direct peer-to-peer game (and a friend by code your local network's, in case you share one; a random opponent never gets that). **Hide my IP address** (Menu >
  Online > Settings) sends matches through the relay instead, so they see only Cloudflare's: it adds a
  little ping (more on the same network), so it's off unless you want it.
- Not yet: spectating, replays, rematch without reconnecting.

**Reporting a problem:** please include **both players'** `log.txt` (and `log.1.txt` if the game's been opened again
since: the last 3 runs' logs are kept as `log.1.txt` to `log.3.txt`; the game's data folder: Windows `%APPDATA%\Virtua
Fighter 2 Recomp`, Mac `~/Library/Application Support/Virtua Fighter 2 Recomp`, Steam Deck / Linux the game's own
folder or `~/.local/share/Virtua Fighter 2 Recomp`), what each is playing on, Wi-Fi or cable, and the ping shown.

## How it works

The recompiler (`src/rec`) finds the i960 program's code with an address map (`src/rec/vf2_map.cpp`: where the
functions start and where the indirect jumps lead, no bytes from the ROM), made from coverage runs, and turns it
into native code with SLJIT as the game starts. Around it, code from MAME runs the rest of the Model 2A board: the
TGP, the geometrizer and polygon renderer, the tile chip and the sound board's 68000 and SCSP. The SDL 3 host
composites the picture, scales and shapes it, and fills in the frames between the arcade's for smooth motion.

The 3D can also be drawn on the GPU (`src/gpu`): Metal on a Mac, and SDL_GPU compute (Direct3D 12 or Vulkan) on
Windows and Linux, where SDL's GPU renderer then draws the whole window. The GPU doesn't use the usual depth buffer:
the polygons are drawn in the game's own order, the order standing in for depth, by a compute rasteriser that
follows the CPU renderer's arithmetic step for step (integer division, no fused multiply-adds, float-to-integer
conversion as on x86), so its picture is the CPU's pixel for pixel (`gpu3dcmp` compares the two). When the game
starts it draws a test scene both ways and compares them, then checks its first pictures of the game itself; any
difference, error or missing driver and it uses the CPU for that run. Auto (the default) then picks the GPU unless
it's clearly slower than the CPU on that machine.

## Repository layout

| Path | Contents |
| --- | --- |
| `src/core` | the Fujitsu MB86233 (TGP) core, from MAME, and the shim MAME's CPU cores compile against |
| `src/rec` | the i960 recompiler: code discovery, native code (SLJIT) at start-up, the C++ build's, the runtime |
| `src/machine` | Model 2A: memory map, interrupts and timers, the TGP glue, geometrizer and renderer, the tile chip, the sound board, smooth motion |
| `src/host` | the SDL 3 host: window, input, pacing, scene sync, the GPU compositing (`d3d11_map.cpp`: Windows' texture uploads), the RmlUi menu |
| `src/gpu` | the GPU renderer for the 3D: Metal (`gpu3d_metal.mm`), SDL_GPU (`gpu3d_sdlgpu.cpp`), its HLSL compute shaders (`gpu3d.hlsl`) and their compiled SPIR-V / DXIL (`gpu3d_shaders.h`) |
| `tools/gpu/shaders.sh`, `tools/gpu3dcmp.cpp` | compiles `gpu3d.hlsl` into `gpu3d_shaders.h` with DXC; compares the GPU's 3D with the CPU's |
| `assets/` | the app icon, the menu (`ui/`) and its font, the player select's painted sides |
| `third_party/` | MAME's i960 and 68000 cores and more, SLJIT, RmlUi, stb_image |
| `tools/recomp` | the ahead-of-time recompiler (i960 to C++), for development |
| `tools/*.cpp` | the headless runner (`vf2run`), disassembler, ROM set check, lockstep checker (`vf2lock`) |
| `tools/*.py`, `tools/scripts` | comparisons with MAME's pictures, sound and traces; input scripts |
| `tools/mac`, `tools/win`, `tools/deck`, `tools/release` | the release packages and their licence notices |

## Run it yourself

Needs a C++20 compiler, zlib, SDL 3 and FreeType (Mac: `brew install sdl3 freetype`).

```
make                    # build/bin/vf2, and the headless tools
build/bin/vf2 path/to/vf2.zip
```

The release packages: `tools/mac/package.sh` (Mac), `tools/win/build.sh` (Windows, cross-compiled with MinGW-w64),
`tools/deck/build.sh` (Linux / Steam Deck: x86-64, in Valve's Steam Runtime "sniper" SDK container).
`tools/release/licenses.sh` gathers the third-party notices they carry.

`build/bin/vf2run` runs the board headless (pictures, RAM dumps, raw sound, an input script such as
`tools/scripts/start_game.txt`); `tools/compare.py` and `tools/sound_compare.py` compare its output with MAME's.
The address map is made from coverage runs (`vf2run --cov`, then `make map`). `make rec` also recompiles the
program to C++ (into `build/`, from your `roms/vf2.zip`: never committed) for `build/bin/vf2lock`, which runs the
interpreter and the recompiled code side by side and compares the CPU's whole state, the clocks, the RAMs and the
pictures as they go. `vf2 --interp` and `vf2run --interp` run the interpreter.

`tools/dump_textures.sh path/to/vf2.zip` saves the game's textures as PNGs (into `build/textures`): coloured as in
the game, the grey levels they're made of, and a list of where each one sits. It runs attract mode headless; for the
rest, play with `VF2_DUMP_TEXTURES=folder` set and each texture is saved the first time it's drawn.

## How it's made

Made with the help of AI coding tools. Every build is checked against the arcade: the recompiled i960 program runs
side by side with MAME's interpreter, and the pictures, RAM and sound are compared frame by frame with MAME's.

The whole story, with how the AI-assisted work was organised and checked: [docs/HOW-IT-WAS-MADE.md](docs/HOW-IT-WAS-MADE.md).

## Credits

Built on MAME's work on the Model 2: the i960 core and disassembler (Farfetch'd, R. Belmont), the 68000 core and
the TGP (Olivier Galibert), the Model 2 driver and video (R. Belmont, Olivier Galibert, ElSemi, Angelo Salese,
Matthew Daniels), the System 24 tile chip (Olivier Galibert), the SCSP and its DSP (ElSemi, R. Belmont), the
polygon scan converter (Ville Linde, Aaron Giles) and more (`third_party/mame/README.md` lists every file and its
authors), all BSD-3-Clause. Also RmlUi (MIT), stb_image (Sean Barrett, public domain / MIT), SDL 3 (zlib),
FreeType (FreeType License), zlib, and the Barlow Condensed font (SIL Open Font License).

Thanks to everyone who tested and reported: [duartepedro03](https://github.com/duartepedro03) and
[MagitekArmor](https://github.com/MagitekArmor) (performance and the GPU renderer on Windows),
[alex2005jp](https://github.com/alex2005jp) (older graphics cards), [darethejackal](https://github.com/darethejackal)
(Steam Deck), [vmicka72](https://github.com/vmicka72), [svera](https://github.com/svera) and
[terryarcade](https://github.com/terryarcade).

Virtua Fighter 2 is SEGA's. This project is not affiliated with or endorsed by SEGA.

## Licence

MIT for this project's own code: see [LICENSE](LICENSE). Code from MAME keeps its BSD-3-Clause licence and notices;
the rest of `third_party/` keeps its own, and the Barlow Condensed font the SIL Open Font License. The art, the
player select's painted sides (`assets/backgrounds/select.png`) and the app icon (`assets/icon/`, drawn by
`tools/release/make_icon.py`), is by Mr Finster under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

## Support

If you enjoy it, you can [buy me a coffee](https://buymeacoffee.com/mrfinster).
