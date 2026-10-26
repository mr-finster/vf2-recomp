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

## Where it stands

| Part | State |
| --- | --- |
| Save states | `vf2soak`: every frame rolled back 7, 20,000 frames, identical; `--delay` 1, 3, 12 and `--only-misses` identical |
| The adapter, two boards | `vf2net`: 4,000 frames in step at 40 ms each way, jitter 8; 12,000 with the score counted the same on all three boards |
| The game online | Two copies through a stand-in lobby: a room code's match, 2,800 frames in step (30 ms each way); a random match, 1,800; the sound board alone playing (music and effects) |
| `src/net` and its tests | VF3's: `nettest` 22, `delaytest`, `netfuzz` 2,000 runs, `roomtest` |
| The Online screens | VF3's (`assets/ui`, `menutest`'s pictures of every one) |
| To do | A match against another machine (the Mac and the Deck), a far one, the relay; the Windows build checked (it links Winsock and WinHTTP) |

```sh
build/bin/vf2soak vf2.zip --random 1 --frames 20000     # the save state, every frame rolled back
build/bin/vf2net vf2.zip --random 1 --frames 4000       # two boards, 40 ms each way
build/bin/vf2soundonly vf2.zip --frames 2400             # the match's sound board against the game's own
SDL_VIDEO_DRIVER=offscreen build/bin/menutest /tmp/shots  # the Online screens as pictures, no ROM
```

Tests with the real game: `VF2_HOST_ROOM=1`, `VF2_JOIN_CODE=XXXXXX`, `VF2_RANDOM=1` (at pass 60, as the menu's),
`VF_LOBBY=http://127.0.0.1:8787` (a lobby on this machine), `VF3_NET_LAG` (the shared library's: a worse network),
`VF2_DUMP_BEGIN=file` (the state a match starts from), `VF2_MATCH_SOUND=file` (what the match's sound board played).

The online version (`vf2net::NET_VERSION`): changed only with what a match depends on (the board, the recompiler,
the adapter, the match's settings, the protocol); a test build plays only the same build.
