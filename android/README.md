# Virtua Fighter 2 Recomp on Android

The same game as the desktop builds, for an arm64 Android phone: the i960 program recompiled to
native code as the game starts, MAME's Model 2A board around it, SDL3 (Vulkan through its GPU
renderer, the CPU's 3D as the fallback) in front. No game files are included: no ROMs, and nothing
made from them. You need your own dump of the game as MAME's `vf2` set (`vf2.zip`, Version 2.1).

This file is the part a player needs: build it, install it, get a ROM in.

## What it needs

- An arm64 phone, Android 8.0 (API 26) or newer.
- A Vulkan driver (essentially every phone since 2017 has one): the 3D can also run on the CPU,
  and the game falls back to it by itself if the GPU's picture differs or its driver fails.
- Your own `vf2.zip`.
- One permission, `INTERNET`, for Online's **Play by address** (UDP sockets). It is Android's
  normal permission (no prompt). The app asks for nothing else and keeps everything in its own
  folders. The lobby and its room codes need libcurl, which Android doesn't ship, so this build
  has no lobby address in it: Online plays by address (the same network, or a VPN).

## Build

```
cd android
ANDROID_HOME=~/Android/Sdk ./gradlew :app:assembleDebug      # app/build/outputs/apk/debug/app-debug.apk
```

It needs a JDK 17, the Android SDK (`ANDROID_HOME`/`ANDROID_SDK_ROOT`, or a `local.properties` with
`sdk.dir=`), the SDK's CMake 3.22.1 and NDK 30.0.16248370 (both named in `app/build.gradle`).
SDL3 and FreeType are not in the repository: the build itself runs `tools/android/deps.sh`, which
fetches their pinned release tarballs, checks them against the SHA-256 that `tools/mac/deps.sh`
pins for the Mac and Deck builds, and unpacks them into `build/deps/android/`. Nothing else is to be
installed first, and the tarballs are cached: a first build takes a couple of minutes, later ones
are seconds.

The APK is signed with the debug key: it installs by sideloading, not from the Play Store.

## Install

```
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Or copy the APK to the phone and open it there (Allowing installs from that app first).

## The ROM

**The easy way.** Start the game: it asks for your `vf2.zip` with Android's own file picker
(Downloads, a USB stick, Drive...). It is checked and a copy is kept in the app's own folder, so
you do this once. Only `vf2.zip` is ever read, so a folder shared with other games' sets can't get
in the way.

**Dropping it in instead.** The app also reads `vf2.zip` from its own folder on the shared storage,
`Android/data/com.vf2recomp.vf2/files/roms/`, and plays it where it lies (nothing is copied). That
folder is reachable from a computer without the picker:

```
adb shell mkdir -p /sdcard/Android/data/com.vf2recomp.vf2/files/roms
adb push vf2.zip /sdcard/Android/data/com.vf2recomp.vf2/files/roms/
```

(on older Androids, or with a file manager that still sees `Android/data`, the same folder can be
filled by hand). This shared copy is looked in before the app's own copy the picker made — and an
explicit ROM folder still wins over both (see below), so the two never fight.

**A ROM folder of your own** (a folder an update won't touch, e.g. a sync folder): the game looks
there first and only reads it. `--roms <folder>` on the command line, the environment variable
`VF2_ROMS`, or `roms_folder = <folder>` in `settings.ini` — in that order, the command line winning.
On Android the command line and the environment are awkward to reach, so `settings.ini` is the way;
it lives in the folder below and its relative paths are from there. If the zip isn't there the game
says so in `log.txt` and carries on as without the option.

## Where the game keeps things

Everything is in the app's private folder, `/data/user/0/com.vf2recomp.vf2/files/` (that is
`SDL_GetPrefPath()` on Android; nothing needs a storage permission):

| Path | Holds |
| --- | --- |
| `roms/vf2.zip` | the copy the picker made, if you used it |
| `settings.ini` | the menu's choices (volumes, render scale, controls, on-screen controls...) |
| `backup1`, `eeprom` | the board's NVRAM: the game's own saves and arcade settings |
| `Screenshots/` | F12 on a keyboard |
| `assets/` | the menu's UI, font and painted backgrounds, copied out of the APK on the first start |
| `log.txt` | what the game did, and the ROM folder it used |

`log.txt` is the place to look when something doesn't start:

```
adb shell run-as com.vf2recomp.vf2 cat files/log.txt        # debug builds: run-as works
adb logcat | grep -i vf2                                    # the same lines as they happen
```

Android's backup (and phone-to-phone transfer) is off, so the copy of your ROM stays on this phone.

The log also carries the numbers a slow phone needs: a `pace:` line every ten seconds (how many
pictures a second, where a picture's time went, and the cores' and the GPU's clocks where the phone
lets them be read), and the settings that go with them:

```
adb shell 'for d in /sys/class/devfreq/*; do echo "$d $(cat $d/cur_freq 2>/dev/null)"; done'   # the GPU's clock
```

`Display > Performance hints` (Android 13 and up) asks Android's own scheduler for the frame's
threads and the GPU to be kept at their clocks, instead of taking the game for a program in the
background. It is on by default; turn it off to compare a run, or start one with `VF2_NO_PERF_HINTS=1`.
Where the phone hasn't got it, the log says so in one line and nothing else changes.

## Controls

| Touch | Does |
| --- | --- |
| d-pad (bottom left) | player 1: move |
| G / P / K (bottom right) | player 1: guard, punch, kick |
| Start | player 1: start |
| Menu | the game's menu |
| Back | the game's menu, or a page back inside it |

The pad is shown when there's no gamepad connected: `Display > On-screen controls` is `Off`,
`Auto` (the default) or `On`. Everything else is as on the desktop — a gamepad in Xbox layout
(d-pad or stick, X guard, A punch, B kick, Y guard + punch, RB punch + kick, LB guard + kick,
RT all three, Start to start, View / L3+R3 / touchpad for the menu) and a keyboard if you plug one
in: arrows and Z/X/C for player 1, I/J/K/L and B/N/M for player 2, Esc for the menu. The menu's
Controls page rebinds all of it, and the touch pad keeps the same buttons.

The game starts **fullscreen** — the status and navigation bars hidden, the picture over the whole
screen — and in **landscape**, whichever way you hold the phone. `Display > Fullscreen` turns the
full screen off if you'd rather see the bars; the choice is kept.

The picture follows the phone's refresh rate (60, 120, 144 Hz panels), the screen's rotation
doesn't restart the game, and the sound stops while it is in the background.

The desktop's function keys — F5/F6 render scale and picture shape, F9 frame timings, F11
fullscreen, F12 a screenshot — need a keyboard; the menu covers all but the screenshot (the phone's
own screenshot button works as ever).

## Online

**Play by address** works: Online > Host by address (or Join by address) plays a friend on the same
network, or over a VPN, with no service in between; one side hosts, the other joins with the host's
address and port. Both sides run the same game (the ROM's hash is checked) and the host's Rounds to
win (vs player), 3 by default.

The lobby — Online > Random, room codes, and the count on its card — needs libcurl, which Android
doesn't ship, so this build carries no lobby address and those pages say so. Play by address is the
way to play. A lobby can be built in with `.lobby-url` (one line, kept out of git) or `VF_LOBBY_URL`
at build time, but it still needs libcurl at run time to reach it.

Online's settings (your name, delay, max ping, the match-found chime, Your fighter's random or seed
fighter) are in the game's menu and kept in `settings.ini` like the rest.

## The icons

The launcher's icon is the desktop icon's art: `tools/release/make_icon.py --android steel_red`
writes `app/src/main/res/mipmap-*/ic_launcher.png` and the adaptive icon's two layers (the tile
filling the 108dp canvas, the letters inside the 66dp safe zone), which
`mipmap-anydpi-v26/ic_launcher.xml` names. Any variant from that script's `PALETTES` works.

## What is still owed

The phone has run it (v0.1.17, on a Xiaomi 14T Pro, Dimensity 9300+, Mali-G720 MC12): `i960 native`
(SLJIT's executable memory works), the GPU's 3D exact against the CPU's, the menu (Online included), the
touches, Back, the immersive landscape window, the sound and the on-screen keyboard all work, and the
board holds the game's 57.5 frames a second.

Online's **Play by address** was played against a desktop build over the same network: the handshake, a
match running on both sides (input delay 1, ping ~24 ms, 0 rollbacks, no desync over 8600 frames) and the
HUD's names all worked. Random fighters (Cheats) build and fight on the phone too.

What is left:

- The GPU's own 3D runs under the game's frame rate on a Dimensity 9300+, so **2x at full speed is the
  CPU 3D's**; at 5x/6x a 15-18 ms texture upload appears. During an online match the phone still holds
  57.5 frames/s at 4x.
- The lobby and room codes need libcurl, which Android doesn't ship: this build carries no lobby address
  (see **Online** above).
- The governor still walks the render scale down from 2x to 1x while the frames and pictures are whole:
  it costs sharpness and buys nothing.
- Player 2 has no on-screen pad (a second gamepad or a keyboard plays them).
- Only arm64 is built, and the 2D renderer's `Surface.setFrameRate` idea (forcing the surface's refresh
  rate) is parked.
