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
  <a href="https://youtu.be/cUXXvIGlZ2c">Trailer</a> ·
  <a href="#status">Status</a> ·
  <a href="#controls">Controls</a> ·
  <a href="https://github.com/mr-finster/vf2-recomp/issues">Report a bug</a>
</p>

<p align="center">
  <a href="https://vf2-recomp.pages.dev/img/montage.jpg"><img src="https://vf2-recomp.pages.dev/img/montage.jpg" alt="Virtua Fighter 2 Recomp at 3x: fights on five stages and the widescreen player select" width="800"></a>
</p>

<p align="center">
  <a href="https://youtu.be/cUXXvIGlZ2c"><img src="https://img.youtube.com/vi/cUXXvIGlZ2c/maxresdefault.jpg" alt="Watch the trailer" width="560"></a>
  <br><sub>The online trailer (click to watch on YouTube)</sub>
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
| Random fighters (Menu > Cheats): three of the cast made one, for a player or every computer opponent | Working (new), online too (Online > Settings > Your fighter) |
| Secret characters (Menu > Cheats): Dural, MUE and KOJAC | Working |
| Online play | Experimental: rollback netplay, a random opponent or a friend by room code, no setup (see [Online play](#online-play-experimental)) |

## Random fighters

<img width="1920" height="1084" alt="vf2random" src="https://github.com/user-attachments/assets/66745bae-52f1-4376-9a05-2786a5b60c2e" />

Menu > Cheats > **Player 1 fighter** (and **Player 2 fighter**):

- **Random:** a new fighter every game, made of three of the cast: its head, body and limbs from the three, and its
  moves dealt from them a move group at a time (its Punch moves one's, its Kick moves another's, its throws a third's,
  and so on), so it plays like a mix of them, at the game's own timings. Its stance is the first's and its voice one
  of theirs, and it gets a name made of theirs on the energy bar (JACPAIWOLF, LIJACPAI).
- **Textures:** every part is drawn in its own fighter's textures and their colours, as on that fighter: Jacky's
  vest, Kage's wraps, Pai's and Lau's patterns, belts, boots and bare feet beside its head's face and hair.
- **Colours:** new every game (not part of a seed). A random fighter wears its head's colours, so its face, hair and
  skin are right and its skin is one tone all over; its clothes' main colour is turned round the colour wheel, or
  given a theme: cool, warm, two opposite colours, neon, pastel, black and white, black and gold. Its hair (and
  headband, hood) is given a hue of its own, as in VF1 Recomp, in the fight and on its select portrait; Shun's white
  hair and Dural's chrome stay as they are. Belts and shoes keep their colours (VF2 shares those between its fighters).
- **Seed:** type a fighter's name, as seen on an energy bar, to get that fighter again: the same build and the same
  moves on any machine. A name is the same fighter in every game that has it: VF1 Recomp's 337 names are here with
  their recipes (the same three, head, voice and moves by group), then VF2's own with Lion or Shun, 678 in all
  ([docs/SEEDS.md](docs/SEEDS.md); Random draws from the whole book). VF3 Recomp carries this book on in turn
  ([docs/random_book.txt](docs/random_book.txt), `vf2random --recipes`).

Anything but Normal is picked for you at the select, as in VF1 Recomp: playing alone, or with both players random
(offline or online), the select is skipped (who you got is a surprise as the fight comes in); beside a player on
Normal, it shows, the Normal player picks as ever and the random one is picked at once: its portrait is its head's,
and no name is shown under it. A challenger's select doesn't pick again for a player already in, whose portrait is
their random fighter's own head and hair in its colours. The attract shows random fighters too: each side whose
player has one (Random: new ones each time the demo fight comes round; a Seed: that fighter), or every side with
Random opponents, in the intro's scenes and the demo fights (with Random opponents, a new one as each scene brings
in another fighter). A random fighter's time in the ranking shows its name (kept in ranking_names.txt beside the
backup). As a match's first round comes in, each random fighter is built on screen during
the camera's sweep: its parts from the feet up, polygon by polygon, each a white flash as it lands, grey until all
are on, then its colours sweep up the same way before READY (drawing only: online, both screens show the same).
**Random opponents** makes every computer opponent a random fighter, made afresh for each match (not your own
fighter's mirror match). Changing them restarts the game.

**Online**, your fighter is Online > Settings > **Your fighter** (Normal, Random or Seed, as here), whichever side you
play: your opponent sees it as the match starts (RANDOM, or the seed's name), and both machines make the same fighters
from the match's own seed (Random's draws and the colours), in step through every rollback. Random opponents stay
offline (online, no one plays the computer). With Random or Seed, you only meet players who also have a random
fighter (Random or Seed), on a version that has them: Random, room codes and addresses alike (the game says so if your
opponent's is Normal). On Normal you play everyone else, players of v0.1.16 included, exactly as before.

**How it works.** VF2 loads a fighter by its number from tables: its record (its body's 16 parts, skeleton, sizes,
textures), its faces, its commands, its voices. A random fighter is a record of its own, its parts taken from the three
fighters' records, and the tables pointed at the three's entries: player 1's is made in MUE's place (fighter 11) and
player 2's in KOJAC's (12), which the select never gives, and the player's pick is changed to it as the VS screen comes up.
The commands are a dispatch on the buttons pressed, each to a program of checks ending in a move; moves are numbered
for the whole game, so a random fighter's dispatch can send each button to a different fighter's program. A side's
colours are loaded by the fighter's number: one instruction of the game's now reads them from the fighter's record,
each of the cast's records naming its own, and the colour tables are moved to tables of their own, an entry a number,
so a random fighter has colours of its own: its head's, recoloured. A side's textures are one fighter's set, every
fighter's in the same places on the side's texture sheet, so a random fighter's sheet is put together a 32 x 32 tile
at a time: each tile from the fighter whose texture is there (its head's where no other's is), a texture that would
land on another's moved to a free place with its models' texture headers, and its rows of detail colours put in rows
its head doesn't use. The sets' blocks are decoded by the game's own decompressor, run on an i960 of its own, and
written to texture RAM as the side's set finishes loading, so nothing more is loaded than for any fighter. The game's
code is otherwise untouched, and every byte changed is put back when they're off.

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
arcade mode included, are unchanged. MUE and KOJAC are off while a player's fighter is Random or Seed (those are made
in their places).

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

Arcade sticks the game doesn't know (DirectInput sticks such as HORI's Virtua Stick High Grade or the Astro City
Mini) get a fighting stick's layout; Controls > Player 1 (or 2) > Set up buttons sets theirs by pressing each in turn. A
`gamecontrollerdb.txt` (SDL's mappings) in the data folder or beside the program is read too.

## Online play (experimental)

Rollback netcode, as modern fighting games and Virtua Fighter 3 Recomp have it: find a random opponent in one click, or send a friend a room code. There's nothing to set up and no ports to forward. The game runs at the Model 2 board's own 57.5 fps online too: input delay and rollback absorb the connection, so it never slows down to wait for it.

> **Both players need the same `vf2.zip`**, and the same online version: every release from v0.1.16 on plays the others, until one changes the game (its notes will say so). A build of your own between releases plays only the same build. The game checks both, and tells you in plain words if they don't match.

### How to play online

Open the menu (Esc, or View / Select on a controller) and choose **Online**. The first time, it asks for your name (up to 10 letters). Your name shows under your energy bar, with your country's flag.

**RANDOM: play anyone**
- Pick **RANDOM**. The card shows how many people are online right now.
- It finds whoever else is searching, the nearest first (the best connection), and widens the search the longer you wait. **Stop** gives up.
- You can play solo while you wait. The match takes over as soon as someone's found.
- After a match, **New opponent** searches again.
- **Random: max ping** (Online > Settings): Any, or 80 to 250 ms. A random opponent's ping is measured as you connect, before the match starts; over your limit, the game leaves and looks for someone else (and the two of you aren't matched again for 10 minutes). Room codes and addresses play whatever the ping.
- **Match found chime** (Online > Settings): On (the default) plays a chime when an online match is found, and the game's window flashes in the taskbar or the Dock if it isn't the one in front, so you can wait for an opponent in another window.
- **Your fighter** (Online > Settings): Normal (you pick at the select), Random (a new [random fighter](#random-fighters) at every select) or Seed (one random fighter by its name), in any match, whichever side you're on. Your opponent sees it as the match starts. You only meet players who also have a random fighter (Random or Seed); Normal plays everyone else, v0.1.16 included. It can't change while hosting, joining or playing.

**HOST / JOIN: play a friend**
1. One player picks **HOST**. It shows a 6-letter room code like `FR7-W25`. **Copy code** puts it on the clipboard: send it to your friend.
2. The other picks **JOIN** and types or pastes the code. It joins by itself at the 6th letter, and the steps on screen show how far it's got.
3. Once you're both in, the game starts at its title screen: both press Start.

On a controller, the code and name screens show letters to pick with A (X deletes, Y pastes). On a keyboard, just type.

**In a match**
- Each player uses their own **Player 1** controls.
- Matches play the PC way (free play), with the host's arcade settings (rounds, difficulty, energy).
- **The rounds are the host's.** How many rounds win a match is the host's Menu > Arcade settings > **Rounds to win (vs player)**: 3 out of the box since v0.1.16's update of 28 Oct, as in Virtua Fighter 3, SEGA's tournaments and Japan's arcades (the board's own is 2: an install from before it goes from 2 to 3 once, and a 2 you set after that stays). Host, and it's yours; join a friend, and it's theirs; with RANDOM, it's whoever the match makes host.
- The stage is random, like a cabinet that's been on all day: a random one for the first fight, then the game's own order.
- Whoever loses is asked to press Start for a rematch. As in the arcade, the winner plays on against the computer until they do.
- After leaving, **Play again** reopens the same room code, and your friend rejoins by itself.

### How online play works

- **Rollback netcode**, like modern fighting games and Fightcade. Your own inputs show at once, after a small input delay. When your opponent's inputs arrive, the game quietly re-runs the last few frames with them, so it never waits on the network. At higher ping you may see a move "jump" slightly as it's corrected: that's rollback doing its job.
- **Auto input delay**, on by default:
  - It uses as little delay as it can: none on a LAN and 1 frame almost anywhere else, with rollback quietly covering the rest.
  - **It accounts for slower machines.** Each game measures how many frames it can re-run without a hitch and tells the other; the other side's delay covers the rest, so a Steam Deck in a heavy fight never stutters.
  - Against a far opponent it adds delay rather than slowing the game down, so the game always runs at its full 57.5 fps (about 3 frames at 170 ms, 5 at 250 ms, up to 15).
  - It ignores short bursts (a phone network's second of high ping): rollback covers those.
  - You can set it yourself instead (0–10) in Online > Settings.
- **Keeping in time.** When one side runs a frame or two ahead of the other, it slows down a touch (2% a frame) until the other catches up, rather than stopping for a whole frame. So a match runs at the pace both machines can hold: 57.5 fps when both can.
- **The sound runs on its own clock.** In a match, the music, voices and effects play in real time on their own, so a slow frame, a wait for the other player, or the game keeping in time never makes them drop out or change pitch. The Sound page's volumes apply mid-match.
- **Direct first, relay if needed.** The two games connect to each other directly whenever they can. When a network won't allow that (phones' 5G, some office, school and hotel networks), the match goes through Cloudflare's relay instead, automatically. The connection display says **DIRECT** or **RELAY**.
- **The lobby only introduces players.** Room codes and random matching run on a small Cloudflare service. No game data goes through it, and it doesn't log anything.
- **Desync protection.** Both games check every 2 seconds that they're in exactly the same state, on a Mac against a PC too. If they ever weren't, the match ends with a message rather than playing on wrong.

### The connection display

At the bottom of the screen during a match, out of the way of the game's own bars and timer:

| | |
|---|---|
| **● NAME** | Your opponent. The dot is green (great), yellow (OK) or red (rough), by how the match plays. |
| **PING** | Round trip in milliseconds. Under ~100 ms plays great; up to ~250 ms plays well. |
| **DELAY** | Your input delay in frames (Auto changes it as needed). |
| **RB** | The most frames rolled back recently. |
| **DIRECT / RELAY** | How you're connected. |

Online > Settings > **Connection display** sets it to On, Detailed or Off. **Detailed** adds the jitter, the average rollback, how far ahead your side is running, and what each frame costs your machine.

### Privacy

- **IP addresses:** in a direct match your opponent's game knows your IP address, as in any peer-to-peer game.
  - A random opponent never gets your home network's local address.
  - **Hide my IP address** (Online > Settings) sends every match through the relay, so your opponent only sees Cloudflare. It adds a little ping.
- **Your flag** is the country Cloudflare places your connection in. A VPN shows the VPN's country.
- **log.txt** masks every IP address, so it's safe to post.

### Known issues

- **Few players online at first.** RANDOM only finds players searching at the same time, so tell a friend, or post a time to meet.
- **Each game plays only itself.** A Virtua Fighter 2 player can't match a Virtua Fighter 3 one, and the same for each pair.
- **Far opponents:** past about a 250 ms round trip, Auto adds input delay to keep full speed. Beyond about 670 ms the game slows down instead.
- **Slow machines:** a machine that can't hold full speed offline will struggle online, and slows the match for both. Use the GPU 3D renderer (see Troubleshooting).
- **Through the relay** the ping is a little higher than direct, and a session through it may need to reconnect after 6 hours.
- **A dropped connection** holds the match for a few seconds, then ends it if it doesn't come back.
- **The menu doesn't pause** an online match. It goes on behind the menu, with your inputs off.
- **The character select plays on guesses**, like the fight: for a moment you may see the other player's cursor or face jump as their inputs arrive.
- **Mac:** the app isn't signed, so macOS may ask to allow incoming connections again for each new version.
- **Not yet:** spectating, replays, lobbies or chat.

### Troubleshooting

**"Couldn't reach the online service"**
- Check your internet connection.
- A firewall or antivirus may be blocking the game: allow it (see Firewalls below).
- If it's the service itself, try again in a few minutes.

**"You both need the same version"**: one of you has a test build or one built from source between releases (those only play the same build), or a later release changed the game. Both download the latest.

**"Your game files (the ROM) aren't the same"**: you have different `vf2.zip` files. Both need the same dump.

**It won't connect, or gets stuck on a step**
- The Online screen shows which step it reached and why, in plain words.
- Check the code, and that the host is still hosting: a room closes when the host stops.
- **Try again** goes again. If a direct connection can't be made, it uses the relay by itself.

**Firewalls**
- **Windows:** the first time you host, allow the game when asked. If you dismissed it: Windows Security > Firewall > Allow an app, and tick Virtua Fighter 2 Recomp for **Private and Public**.
- **Mac:** System Settings > Network > Firewall > Options: allow it. Also System Settings > Privacy & Security > Local Network: on.

**Stutter, slowdown, or lots of rollback**
- **Use the GPU renderer:** Menu > Display > 3D renderer: **GPU**. On Windows and Linux, Vulkan or Direct3D 12 work best.
- **Check your speed offline:** Display > Show FPS should read 57 or 58 steadily. If it doesn't offline, it won't online. Lower the render scale (F5).
- **Use a cable** if you can: Wi-Fi adds jitter.
- **Steam Deck:** turn off Wi-Fi power management (Settings > System > Developer Mode on, then Settings > Developer > Enable Wi-Fi power management: off).
- Set the Connection display to **Detailed**: a high JITTER means the network; a high FRAME means the machine.

**"The match went out of step"** means the two games disagreed: a desync. This shouldn't happen. Please report it with **both** players' `log.txt`.

### Reporting a problem

[Open an issue on GitHub](https://github.com/mr-finster/vf2-recomp/issues) with:
1. **Both players' `log.txt`** if you can (one helps too). The last 3 runs are kept as `log.1.txt` to `log.3.txt`. They're in the game's data folder:
   - **Windows:** `%APPDATA%\Virtua Fighter 2 Recomp`
   - **Mac:** `~/Library/Application Support/Virtua Fighter 2 Recomp`
   - **Steam Deck / Linux:** the game's own folder, or `~/.local/share/Virtua Fighter 2 Recomp`
2. What each player is on (PC/Mac/Deck), Wi-Fi or cable, and roughly where (country is enough).
3. The ping and connection shown, and what happened.

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
`make build/bin/vf2random` builds the random fighters' headless runner: a script's shots with each side's fighter and
its name (`tools/scripts/random_versus.txt`, `random_1p.txt`), the seed book (`--names`) and a check that an online
match starts with them off (`--online`). `vf2soak` and `vf2net` take each player's fighter online (`--p1`, `--p2`: `R`,
`S<place>`, `*` or a name) for the rollback and network checks with random fighters. The address map is made from coverage runs (`vf2run --cov`, then `make map`). `make rec` also recompiles the
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
