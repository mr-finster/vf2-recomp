# Netplay: VF2 online, as VF3's

VF2 plays online the way VF3 Recomp does: the same rollback library (`src/net`, kept the same in all three games),
the same lobby (vf3-recomp's `tools/lobby`, a Cloudflare Worker: `game` "vf2"), the same Online screens and the same
words. What's VF2's own is below. VF3's `docs/netplay.md` has the shared parts in full: rollback, Auto input delay,
keeping in time, the handshake, the lobby, privacy and safety.

## What's VF2's own

- **The save state** (`model2a::save_state` / `load_state`, `src/machine/state_io.h`): the i960, the TGP, the
  scheduler and its timers, every RAM (the device RAMs by the 4 KB pages written since the last save), the I/O, the
  EEPROM and the UART; 7.1 MB. Save 0.16 ms, restore 0.13. Not in it: the sound board and the 3D's own state
  (picture only).
- **The match's board** (`src/host/vf2_netplay.*`, `vf2net::Adapter`): a cold start (`model2a::power_on`: a reset
  keeps work RAM and more, so a side that had played would start out of step) from the host's backup RAM, EEPROM and
  secret character; on both sides the scenery culling widescreen uses (a 16:9 and a 4:3 player run the same
  program), the WARNING shortened, the FREE PLAY notices hidden, the sound board held and the byte link; free play and
  "winner plays on" (the loser presses Start for a rematch); the boot run unseen.
- **The match's sound**: the match's board runs with its sound board held (the same on both sides: the i960 never
  hears back from it), and a second board, the sound board alone (`model2a::set_sound_only`: only the 68000 and the
  SCSP run), plays the bytes it sends, a MIDI byte's time apart, on a thread of its own to the real clock (main.cpp's
  `match_sound_start`), booted first as the match's board booted, a frame for each of its with that frame's bytes
  (`Adapter::boot_sound`): reset at the match's start with them all at once, its program was still starting for a
  second or two and a Start pressed then (its sound, and the select's music after it) was lost. Under rollback a frame's bytes go once (`Adapter::take_sound`): a
  wrong guess can leave a stray sound, never a late one. `vf2soundonly` checks the sound board alone against the
  game's own: each second's loudness alike (1.000 over 40 s of the attract).
- **The score** (`vf2net::Score`, found with `tools/vf2detect.cpp`): work RAM 0x4f and 0x51, each player's rounds
  won in the match on now (both back to 0 when it's over); 0x4c, the players in a fight (2 both, 1 player 2 alone, 0
  player 1 alone). Checked against the HUD's round markers over four matches and a challenger who joined and lost.
- **The names on the energy bars** (`src/machine/hud_names.*`): the HUD's name pictures (map 0, rows 6 and 7,
  columns 2-7 and 54-59; recognised by the round markers at columns 22-25 and 36-39) made again from the game's own
  box (its frame and checker), as wide as the name, and the name drawn in the game's orange, the flag's place left
  beside it (the host draws the 48 x 36 flag there, on the window's pixels: `model2a::hud_flag_at`); in the 2D
  picture only, never the board. `VF2_HUD_NAMES=P1,P2` for vf2run's pictures.
- **The character select in lockstep**, as VF3's (`vf2net::in_select`: the game's mode, work RAM 0x2b, 7 from Start
  to the VS screen, the challenger's select too): no frame on a guess, so a face never flashes as one is put right.

## Rooms and watching

As VF3's (its `docs/netplay.md` and `docs/spectate-design.md` in the private repo have the whole of it): the Online
screen is a room browser (the lobby's listed rooms, this game's: `game` "vf2"), Quick match, Create room and Enter code
its buttons, By address and Settings under More; Create room is a name and Listed in Rooms (off by default: the code
only); every match may be watched by anyone with its room's code (`src/net/watch.*`, the same files as VF3's: the host
sends the lobby the confirmed frames and the desync check's hashes 5 times a second on one WebSocket kept open (one
every 30 s while nobody watches, the lobby's `every`, woken by the first watcher), HTTPS if a network won't carry
one, on threads, never waited for; a watcher gets them on a WebSocket of its own, else the
HTTPS stream, else pieces, and runs its own board from the match's start a few hundred ms behind, checked against the
players' hashes; past the lobby's monthly ceiling the host is told `off` and stops sending, its match going on); a
room stays for the next in its line. Quick match games are listed too, to watch only (both players, a Quick match
label, no code), when the host's game sends the match (a new build's) and both players have More > Settings > Show
my Quick Match games in Rooms On (the default); either way one can be watched by its code. 400 watch a match at most
(one more: "Too many watching right now"); with the lobby down, each screen says so in one plain line and a match
already on carries on. A room code's host waits for its player, and holds a room that stays, on the room's own
WebSocket (`/v1/room/ws`; refused: `/v1/wait` and `/v1/hold` as before); the guest asks the watcher count once a
minute, only while the figures at the top are shown; the browser asks for the rooms every 15 s while it's open. The
lobby's off switches (`SWITCH_ROOMS`, `SWITCH_LISTING`, `SWITCH_WATCH`, `SWITCH_WEBSOCKETS`) and an older lobby (no
`features` in `/v1/online`) hide what's off: "Rooms aren't available right now. Quick match and Enter code still
work.", "Watching isn't available right now."; room codes and Quick match play as ever. Every line the new screens show is in `src/host/online_text.h` (whole sentences, `{name}`
places: `tr::fill`), for the translations. Links from the site: `vf2recomp://vf2/join/CODE` and `vf2recomp://vf2/watch/CODE`
(`src/host/links.cpp`: only exactly that form; the Mac's Info.plist, this user's registry or `.desktop` file on
Windows and Linux; none on Android). NET_VERSION is unchanged: the session's packets are the same, old and new builds
play each other as before; an old build's rooms are never listed and its matches send nothing to be watched.

What's VF2's own:

- **The guest's fighter** (Your fighter: Cheats' random fighters): the host's is in the session's blob, the guest's
  only in its hello. The start the host sends for watchers is the blob with the guest's `Identity::extra` after it
  (`vf2net::watch_blob`: the letters, their count, "VF2W"; every new host's, the plain match's with none), split off
  again by the watcher (`watch_blob_split`), which then plays the match as its guest does (`hosting` false, its
  `own_extra` the guest's). The lobby keeps the start as it is.
- **Sound**: a watcher catching up runs those frames unseen with the adapter's `mute` (their bytes for the sound board
  dropped), from the start and after a long spell cut off alike (`Spectator::on_catch_up`), so none come out late;
  the match's sound board (`match_sound_start`) starts at its first picture, booted as the players' was, and plays
  from there.
- **A late watcher runs the match from its start**, unseen: no watcher's state (a checkpoint) is ever handed to another,
  as one could be forged to the players' hash.
- **The WebSocket** (`src/net/http`'s `Socket`, VF3's): WinHTTP's own on Windows; on macOS and Linux the system's
  libcurl connected only, the upgrade and the frames written in `http.cpp`. Android has no system libcurl: the
  WebSockets and the HTTPS requests alike say so and nothing is sent or watched there (it compiles: only `dlfcn.h`
  and `poll.h`, both bionic's). The lobby's frames are read strictly (`FrameReader`: a masked frame, a reserved bit
  or opcode, a control frame fragmented or over 125, a stray continuation, anything over 32 MB end the connection);
  `wsfuzz` (ASan/UBSan) reads valid streams cut every way back whole, refuses each forbidden case and takes random
  and spoilt streams without a fault.
- **Tests**: `watchsim` (no ROM, the shared one); `vf2net --watchers 2 [--watch-late F] [--watch-ms MS]` (the ROM):
  two boards watching through a stand-in lobby, one from the start and one joining late, each the straight run's at
  every checkpoint and matching the players' hashes (the late one every one of them, from the match's start), a random
  fighters' match too (`--p1 R --p2 S12`: the guest's
  fighter from the start's tail); the players' rollbacks and waits the same as with none. `menutest` draws every new
  screen and checks the link parser. The game: `VF2_WATCH_CODE=XXXXXX` (watch at pass 60), `VF2_HOST_PUBLIC=1` (a
  listed room with `VF2_HOST_ROOM`).

## Where it stands

| Part | State |
| --- | --- |
| Save states | `vf2soak`: every frame rolled back 7, 20,000 frames, identical; `--delay` 1, 3, 12 and `--only-misses` identical |
| The adapter, two boards | `vf2net`: 4,000 frames in step at 40 ms each way, jitter 8; 12,000 with the score counted the same on all three boards |
| The game online | Two copies through a stand-in lobby: a room code's match, 2,800 frames in step (30 ms each way); a random match, 1,800; the sound board alone playing (music and effects) |
| `src/net` and its tests | VF3's: `nettest` 22, `delaytest`, `netfuzz` 2,000 runs, `roomtest` |
| The Online screens | VF3's (`assets/ui`, `menutest`'s pictures of every one): the room browser, Create room, a room's page, More, watching |
| Watching | `watchsim` (0, 1 and 50 watchers: the players' every count the same); `vf2net --watchers 2`: both watchers' boards the straight run's at every checkpoint |
| To do | A match against another machine (the Mac and the Deck), a far one, the relay; the Windows build checked (it links Winsock and WinHTTP) |

```sh
build/bin/vf2soak vf2.zip --random 1 --frames 20000     # the save state, every frame rolled back
build/bin/vf2net vf2.zip --random 1 --frames 4000       # two boards, 40 ms each way
build/bin/vf2net vf2.zip --random 1 --frames 6000 --watchers 2   # and two watching, one joining late
build/bin/watchsim                                       # watching, no ROM: the players the same with 0, 1 and 50
build/bin/wsfuzz                                         # the WebSocket's frame reader, ASan/UBSan
build/bin/vf2soundonly vf2.zip --frames 2400             # the match's sound board against the game's own
SDL_VIDEO_DRIVER=offscreen build/bin/menutest /tmp/shots  # the Online screens as pictures, no ROM
```

Tests with the real game: `VF2_HOST_ROOM=1`, `VF2_JOIN_CODE=XXXXXX`, `VF2_RANDOM=1` (at pass 60, as the menu's),
`VF_LOBBY=http://127.0.0.1:8787` (a lobby on this machine), `VF3_NET_LAG` (the shared library's: a worse network),
`VF2_DUMP_BEGIN=file` (the state a match starts from), `VF2_MATCH_SOUND=file` (what the match's sound board played).

The online version (`vf2net::NET_VERSION`): changed only with what a match depends on (the board, the recompiler,
the adapter, the match's settings, the protocol); a test build plays only the same build.

Every release plays the last one online (the private repo's `tools/compat/compat.sh vf2 check` says COMPATIBLE
first): what a match gains goes behind an option that adds to the online version, VF3's "+dural" way, the plain version
and its match as the release's before while the option's off.

Cheats' random fighters online (VF1's way), Online > Settings > **Your fighter**: Normal is the plain online version
(v0.1.16) and v0.1.16's match exactly (its "VF2B" blob, nothing in the hello's `Identity::extra`, the same save
states), so it plays v0.1.16. Random or Seed adds `vf2net::RANDOM_TAG` ("+random") to the online version, so the lobby,
Random and the handshake meet only others with a random fighter (Random v Normal online isn't possible: the two are
told so, `options_apart` in online.cpp), and such a match plays the random fighters' protocol (the adapter's
`random_fighters`, from the version): each player's fighter in the handshake's `Identity::extra` ("" Normal, "R"
Random, "S<place>" a named fighter by its place in the book: the book and its order are part of the tag, a change to
them a new one), the host's in its blob too; the blob ("VF2C", `start_blob_random`: backup RAM, EEPROM, the secret
character, the stage's place, then the match's seed and the host's fighter) gives both sides one seed for every random
choice (Random's draws, the colours). `begin` turns everything off and forgets it, cold-starts, then (VF2C only) turns
them on from the cold start's RAM. While they're on, a save state carries the ROM bytes they change as a match goes (the
empty ROM they're made in and the tables by fighter number) and what they know; a load makes a player's fighter again
if the state's is another. Your fighter changes the identity at once (main.cpp's `online_identity`), and not while
hosting, joining or playing. `vf2soak --p1 R --p2 R` (every frame rolled back 7, 20,000 frames) and `--p1 AKILAUPAI
--p2 AKILAUPAI` (5 back), identical; `vf2net --p1 R --p2 R` (60 ms each way, jitter 15, 2% lost) and `--p1 AKILAUPAI`
(100 ms, jitter 30, 5% lost) in step with a straight run (a fighter on either side makes vf2net and vf2soak play the
random fighters' match; none, v0.1.16's). `random_fighter.cpp` is built with the game's float flags (its colours are in
the game's tables).
