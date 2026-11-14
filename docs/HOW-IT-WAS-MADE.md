# How Virtua Fighter 2 Recomp was made

This is a write-up of how the port was built and checked: what is recompiled, what comes from MAME, and how AI coding
tools were used. The commit history has the detail; this is the overview.

## The approach

Virtua Fighter 2 (1994) runs on Sega's Model 2A board. The game logic is a program for the main CPU, an Intel i960.
Around that CPU are a Fujitsu MB86234 DSP that does the geometry (the TGP), a geometrizer and polygon renderer, the
System 24 tile chip for the 2D layers, an I/O board, and a sound board (a 68000 and a Yamaha SCSP with its own DSP).

**The i960 program is recompiled. The rest of the board is not.** Only the main CPU's program is recompiled. The TGP,
the 3D renderer, the tile chip and the sound board run on code ported from MAME. The i960 only takes about 2 ms of a
frame, so recompiling it isn't about speed. The point is that "recomp" should be literally true of the game's program.

- **The recompiler (`src/rec`).** The port ships an address map, `src/rec/vf2_map.cpp`: where the functions start,
  where the indirect jumps lead, and the program's string thunks. It holds addresses only, no bytes from the ROM. When
  the game starts, it reads the i960 program from the player's `vf2.zip`, finds the code from the map, and turns it
  into ARM64 or x86-64 with SLJIT. The common instructions (loads and stores to plain memory, the ALU, compares and
  branches) become machine instructions. The rest run the interpreter's case for that opcode, with the decoding fixed
  in advance. Time slices end where the interpreter's would, and interrupts and FIFO stalls come where it has them.
  The idle loop skips rounds that only count cycles. If the program is patched at run time, its native code is made
  again.
- **Coverage.** The map was made from coverage runs (`vf2run --cov`, then `make map`): both game versions, 1P,
  versus, the ending, game over and attract. That came to 38,567 instructions run, all of them recompiled.
- **A C++ form for development.** `make rec` also writes the program out as C++ (from your own ROM, into `build/`,
  never committed) for the lockstep checker.
- **The interpreter is kept.** MAME's i960 interpreter is the reference for checking and the fallback.
  `vf2 --interp` runs it.

**From MAME:** the i960 and 68000 cores (unchanged, on a small shim in place of MAME's device framework), the MB86234
core, the geometrizer and renderer, the tile chip, and the SCSP with its DSP. Every file ported from MAME keeps MAME's
licence and copyright-holder lines. `src/machine` holds the board (memory map, interrupts, timers, the TGP glue) as a
standalone runtime: one machine, no other drivers.

**New for the port:** the SDL 3 host in `src/host`. It has the window and controls, an in-game menu (RmlUi), saves,
and these additions to the arcade:

- **Render scale.** The 3D is drawn at 1x to 6x. MAME's polygon setup is kept, and its scan conversion is split into
  row bands across all cores. Both give the same pixels, and 1x is still MAME's picture.
- **Smooth motion.** Up to 120 pictures a second between the game's 57.5 frames. Each object's matrix is blended with
  the same object in the frame before. Objects are paired by model and list order, with the camera's move fitted to
  check the pairing.
- **Widescreen.** In fights the 3D sees further to the sides and the backdrop panorama goes with it. The player select
  gets painted side columns.
- **Antialiasing** (an edge filter, or 2x/3x supersampling), scanlines, and GPU compositing of the 2D and 3D from 2x.
- **Pacing, pipelining and a load governor.** The game runs on its own clock, nudged to keep the sound queued. When
  running a frame and then drawing it no longer fit in a frame's time, each picture is drawn on another thread while
  the next frame runs. If that still isn't enough, the governor turns off antialiasing and then lowers the scale.
- **Scene sync and sound.** Scene changes are held until the new scene is whole. Music and effects have separate
  volumes, and the overall level has a peak limiter.

**The GPU renderer.** Since v0.1.2 the 3D can also be drawn on the GPU (Display > 3D renderer): Metal on a Mac,
SDL_GPU compute on Windows (Direct3D 12 or Vulkan) and Linux / Steam Deck (Vulkan). The goal was the CPU renderer's
picture exactly, not an approximation of it:

- **Draw order as depth.** Model 2 sorts its polygons and draws them in order; there is no depth buffer to hand to a
  GPU. So the GPU isn't used as a normal triangle rasteriser: a compute shader rasterises the polygons itself, in the
  game's order, and the last polygon to cover a pixel wins, as on the CPU.
- **The same arithmetic.** Each step follows the CPU renderer's: the edge set-up, the texture coordinates, the
  shading. Matching it on every platform took care: GPU division isn't correctly rounded, so divisions are done in
  integer arithmetic; shader compilers fuse multiply-adds, so every float operation is marked `precise` and none are
  fused; and float-to-integer conversion copies x86's (out of range gives the most negative integer).
- **Checked, then trusted.** `tools/gpu3dcmp.cpp` draws the same frames both ways and compares every pixel's polygon
  and colour. Across the attract and fights, 1x to 4x, 4:3 to 21:9, on Metal, Vulkan (several drivers) and Direct3D
  12, the GPU's pictures are the CPU's: 100%, no pixel different. The game repeats a small version of this at every
  start: a test scene drawn both ways, then its first pictures of the game itself. A difference, an error or a
  missing driver, and that run uses the CPU. Auto (the default) then uses the GPU unless it's clearly slower than the
  CPU on that machine.

## How it was checked

Everything is checked against two references: MAME's interpreter, and MAME itself.

- **Lockstep against the interpreter.** `tools/vf2lock.cpp` runs two boards side by side, one on MAME's i960
  interpreter and one on the recompiled code. Per frame, or per time slice for short runs, it compares the CPU's whole
  state, the clocks, the RAMs and the pictures. The C++ form ran 176,000 frames identical. The native form is checked
  over the same scripted runs, on Apple silicon and on x86-64.
- **Frames, RAM and sound against MAME.** From the first boot, the headless runner (`tools/vf2run.cpp`) was compared
  with MAME frame by frame: `tools/compare.py` for the pictures, and NVRAM, backup RAM and EEPROM against what MAME
  saves. Other checks compare the i960's traffic to the TGP word for word, the SCSP's output sample for sample
  (`tools/sound_compare.py`), and 68000 traces (`tools/trace68k_compare.py`). The scripts that take the references from
  MAME need the ROMs, so they aren't in this repo.
- **Scripted, headless runs.** `vf2run --script` plays input scripts (`tools/scripts/`). The same events can be played
  in MAME, so both runs get identical inputs. Options like `--ko` get a script past rounds, and logs cover the sound
  voices, the coprocessor and the pacing.
- **Twin-board picture checks.** These compare pictures drawn by different paths of the same build: pipelined against
  drawn at once, smooth motion at a frame's own moment against the plain frame, redraws, and GPU-composited parts
  against the CPU's. All must match pixel for pixel.
- **Where MAME isn't the arcade.** The game's mix puts its music well over the hits. Every gain stage of MAME's SCSP
  was checked against other hardware-tested implementations (Mednafen's and Yabause's) and the chip's manual. One
  difference turned up, in the reverb (below), and is the one deliberate departure from MAME's sound. The rest of the
  balance is the game's own, so it's set by the host's volume defaults, as a stated choice.

## Working with AI

The port was built with Claude, through Claude Code, after the same process on Virtua Fighter 1. In practice:

- **A person directs, tests and decides.** The human side was play-testing on a Mac, Windows and a Steam Deck,
  reporting what looked or sounded wrong (a bush jumping aside, music too loud over the hits), deciding the trade-offs, and
  reviewing what went out. One example: the first builds ran the i960 on MAME's core, and the release waited until the
  recompiler was done so that the name would be accurate.
- **Agents in parallel, all headless.** Several agents worked at once on separate branches and problems. Each had its
  own data folder and ran the game hidden (`VF2_HIDDEN=1`, no window, no Dock icon), never in a visible window on the
  machine the person was using.
- **Measure, don't assume.** Every claim had to come with a number: a lockstep run, a pixel count, a timing, a dB
  figure. Ideas that measured out to nothing were dropped. A "cabinet speaker EQ" for the sound balance moved it by
  under 1 dB, and an option to soften the edges of see-through textures made no difference to the shimmer. Both were
  measured and left out.
- **Memory in the repo.** Sessions don't share memory, so commit messages record what changed, why, and how it was
  checked, and private project notes hold findings and dead ends. A new session reads those instead of rediscovering
  them.

The AI wrote nearly all the code. A person decided what was right.

## Problems worth knowing about

- **A bush jumped aside for one picture every few seconds** → smooth motion paired a bush with the next one of its
  model when the camera turned → runs of one model pair by position, the camera's move is fitted to check pairs, and an
  object still in the world is found where it was.
- **Music drowning the hits** → that's the game's own mix, not an emulation bug → separate music and effects volumes,
  stage ambience moved with the music and crowd roars with the effects, and a leveller on the effects.
- **28 pictures a second for minutes on a slow PC, governor untouched** → two frames a pass made every pass look like
  a scene change, so nothing was judged → a whole busy window short of pictures now counts.
- **Steam Deck dipped as the first fight began** → pipelining only switched on after the governor's window → pipelined
  from the first frame on a Deck, and at the scale where a previous run needed it.
- **Hedges drawn over towers at the far edges of 32:9** → Model 2 sorts a polygon by one depth, taken from an
  off-screen end → polygons seen only in the side columns sort by their on-screen part. The 4:3 part is unchanged.
- **The music's reverb was a near-mono smear, and too loud** → MAME treats the SCSP's registers at 0x7c0-0x7ff as a
  mirror of the DSP's delay-line addresses (MADRS); the game's sound program sets those addresses, then clears
  0x7c0-0x7ff, which with the mirror wiped them all, so every reverb tap read the same place (left/right correlation
  0.99, the music 8-11 dB louder). Found by comparing with other SCSP implementations, which keep that range as its
  own storage → the same here; the reverb is wide stereo again (correlation 0.65-0.85). The default music volume went
  from 20 to 50 to keep the same balance with the hits, and a saved 20 moves to 50 once. `VF2_SCSP_MADRS_MIRROR=1`
  brings MAME's behaviour back for comparisons.
- **The music 2% fast** → MAME's SCSP timers fall due (255 - value) << prescale samples after the write, a sample
  short of the chip's: a Sega Rally cabinet's music, on the same sound board, runs 2.18% slower than MAME's → a
  timer counts from the sample after its write, ((255 - value) << prescale) + 1 samples, as VF3's. VF2's sound
  driver's tick (timer A at 0xCE, re-armed in its handler) went from 49.87 samples (884 a second) to 50.87 (867):
  every note 2.0% later, the pitches the same. Online play is untouched: a match holds the sound board and its state
  never includes it (offline the main board's timeslices move with the sound board's timers, as before).
  `VF2_SCSP_TIMER_MAME=1` brings MAME's period back for comparisons.
- **Slow above 1x on Windows, even on fast PCs** → SDL's Direct3D 11 renderer makes a new staging texture each time a
  texture is locked, and the game drew its pictures straight into the locked texture → on Direct3D 11 the pictures
  are drawn in ordinary memory and copied into a texture mapped for writing only (`src/host/d3d11_map.cpp`). The
  same pixels as before.
- **A GPU picture that wasn't the CPU's on another platform** → a GPU's division, fused multiply-adds and
  float-to-integer conversion each differ from the CPU's → integer division, no fusing, x86's conversion, and a
  self-test at every start (see above).
- **Players' setups** → portable mode (a `portable.txt` beside the program keeps everything in its folder) and a ROM
  folder of the player's own (`--roms`, `VF2_ROMS` or `roms_folder` in `settings.ini`), only read, so an update can't
  touch it.
- **x86-64 pictures a few texels off Apple silicon's** → fused multiply-adds round differently → an exact build flag
  for checking. RAM and registers are identical either way.
- **A crash when the renderer lost its textures** → a device reset (Direct3D) dropped textures still in use → every
  texture is rebuilt after a reset.
- **Waiting through the boot** → the board's warning countdown → the game runs unseen at full speed to the attract,
  and the countdown is cut after a CRC check.

## Where to look

- `src/rec/`: the i960 recompiler (code discovery, SLJIT native code, the C++ form, the runtime).
  `src/rec/vf2_map.cpp` is the address map.
- `tools/vf2lock.cpp`: the lockstep checker. Start here to see what "identical" means.
- `tools/recomp/`, and the Makefile's `map` and `rec` targets: making the map and the C++ form.
- `src/machine/`: the Model 2A board (memory map, timers, TGP glue, renderer, sound, smooth motion).
- `src/host/`: window, pacing, governor, pipelining, scene sync, GPU compositing, the menu.
- `src/gpu/` and `tools/gpu3dcmp.cpp`: the GPU renderer and its comparison with the CPU's.
- `tools/vf2run.cpp`, `tools/scripts/`, `tools/compare.py`, `tools/sound_compare.py`: the headless runner and the
  comparisons with MAME.
- `git log`: each commit message explains what changed, why, and how it was checked.
