# VF2 Recomp — iPhone / Mac

Based on [mr-finster/vf2-recomp](https://github.com/mr-finster/vf2-recomp) at
`1cd58e742f9b2ef19ee2dcdf84e65c8cae69360a`.
The initial touch controls and ROM-free input checker came from
[soulafein83's Android fork](https://github.com/soulafein83/vf2-recomp)
at `cd8b389be28fd486ee78adeb45d0ce5e1eeb6653`. The Android project and its game engine changes are not included.
The adapted implementations live in `apple/touch_controls.h` and `apple/tests/touchcheck.cpp`.
See [UPSTREAM.md](UPSTREAM.md) for the directory boundaries and upstream update workflow.

## Add vf2.zip using the Files app

1. Launch VF2 Recomp once. If no ROM is installed, the app waits on the import screen and creates its folders.
2. Open **Files → Browse → On My iPhone → VF2 Recomp → roms**.
   On iPad, use **On My iPad** instead.
3. Copy your **`vf2.zip`** into the **`roms`** folder. Keep the file compressed and named exactly `vf2.zip`.
4. Return to VF2 Recomp. The app detects and validates the ROM, then starts automatically.

The expected location in the Files app is:

```text
On My iPhone (or On My iPad)
└── VF2 Recomp
    └── roms
        └── vf2.zip
```

You need your own complete MAME **vf2 / Version 2.1** ROM set. No ROMs are included in the source or app.
If you replace the ZIP after the game has started, restart the app to load the new file.

Alternatively, use the ZIP import button on the app's import screen to choose `vf2.zip` from iCloud Drive,
Downloads or another file provider. The iOS document picker handles downloads and access permissions.
The app validates the ZIP and saves a copy as `vf2.zip` in its Documents folder, so you do not need to select it again.
Placing the ZIP directly in **VF2 Recomp/vf2.zip** is also supported. Automatic detection checks only these two
locations inside the app's folder; it does not search the rest of the Files app.

The ROM loader checks the contents and CRCs, not just the file name. Imports are copied to a temporary file on
the same storage volume, flushed to disk and validated before replacing the saved ROM. Leftover import folders
are cleaned up on the next iOS launch. Cancellation, corrupt ZIPs, incomplete sets and save failures leave the
existing file intact. The maximum archive size is 256 MiB.

Settings, logs and NVRAM are stored in the app's Library area returned by `SDL_GetPrefPath()`.
Documents contains only user-managed ROM files.

## Open in Xcode

Open **[VF2Recomp.xcodeproj](../VF2Recomp.xcodeproj)** at the repository root.
No project generation command is required first.

| Run on | Xcode scheme | Destination |
| --- | --- | --- |
| iPhone / iPad | `VF2 iOS` | Your connected device |
| iOS Simulator | `VF2 iOS` | An installed iPhone / iPad simulator |
| Mac | `VF2 macOS` | My Mac |

Select the scheme and destination, then press **⌘R** to build and run.
For a physical iPhone or iPad, select your **Team** and a unique **Bundle Identifier** under the
`VF2 iOS` target's **Signing & Capabilities** tab. Simulator and Mac builds do not require a developer Team.
The generated Mac app and dependency targets require **macOS 15.0 or later**.
CMake also defaults to macOS 15.0.

Xcode and **CMake 3.24 or later** are required. Install CMake with `brew install cmake` if needed.
The first build downloads and compiles SDL and FreeType, so it needs network access and may take a few minutes.
Later builds reuse caches in Xcode DerivedData, separated by platform, configuration and architecture.
Dependency preparation uses file locking to prevent concurrent builds from overwriting the same cache.
The default Apple Silicon and Intel Homebrew paths and `/Applications/CMake.app` are detected automatically.
For another installation, set the custom Xcode build setting `VF2_CMAKE_EXECUTABLE`.

Game sources compile in native Xcode app targets, with source navigation and breakpoints available.
The `Dependencies` targets run automatically and use the existing CMake configuration to prepare SDL and FreeType.
Debug builds also use `-O2` for emulation speed, so some variables and source lines may be optimized during debugging.
Device archives use Release configuration; App Store distribution requires separate setup.

To build the same project from the command line:

```sh
xcodebuild -project VF2Recomp.xcodeproj -scheme 'VF2 macOS' \
  -configuration Debug -destination 'platform=macOS' build
xcodebuild -project VF2Recomp.xcodeproj -scheme 'VF2 iOS' \
  -configuration Debug -destination 'generic/platform=iOS Simulator' build
```

The project uses relative paths and includes shared schemes. Regenerate it only when changing the source list:
edit `apple/sources.json`, then run `python3 apple/generate-xcode-project.py`.
RmlUi Core sources are collected automatically. The generator uses Python and macOS's built-in `plutil`;
XcodeGen is not required. Regeneration preserves the existing developer Team, signing settings,
Bundle Identifier and shared scheme launch settings.

### App icon

The iPhone and iPad builds include an opaque 1024 × 1024 AppIcon based on the upstream VF2 artwork.
The red faceted background fills the square; iOS applies the rounded corners. Both the root Xcode project
and CMake's Xcode project compile `apple/Assets.xcassets` into the app.
To regenerate the artwork, install Pillow in a Python environment and run
`python3 apple/make-ios-icon.py`. The checked-in icon requires no Python packages to build.

## Build with CMake

Xcode and CMake 3.24 or later are required. SDL 3.4.18 and FreeType 2.14.3 are downloaded using the same URLs
and SHA-256 hashes as the upstream release scripts, then built as static libraries. Homebrew libraries are
not required. The first configuration needs network access. Downloads and build outputs stay under `build/`.

### macOS

```sh
cmake -S apple -B build/apple-macos \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build/apple-macos --parallel 8
open "build/apple-macos/VF2 Recomp.app"
```

Select your ZIP on first launch. A copy is saved under Application Support at
`Virtua Fighter 2 Recomp/roms/vf2.zip`. The existing `--roms` option, `VF2_ROMS` environment variable and
`roms_folder` setting in `settings.ini` remain supported. For example:

```sh
"build/apple-macos/VF2 Recomp.app/Contents/MacOS/VF2 Recomp" --roms "$HOME/Downloads"
```

The Mac build retains upstream SLJIT native recompilation and the Metal renderer.

### iPhone Simulator (Apple Silicon)

```sh
cmake -S apple -B build/apple-simulator -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0
cmake --build build/apple-simulator --config Release --target vf2 -- \
  CODE_SIGNING_ALLOWED=NO
xcrun simctl install booted "build/apple-simulator/Release-iphonesimulator/VF2 Recomp.app"
xcrun simctl launch booted org.vf2recomp.apple
```

### Physical iPhone / iPad

```sh
cmake -S apple -B build/apple-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0
open build/apple-ios/VF2Apple.xcodeproj
```

In Xcode, select your Team and a unique Bundle Identifier under the `vf2` target's
**Signing & Capabilities**, then run on your connected device. You can also pass
`-DVF2_BUNDLE_IDENTIFIER=com.example.vf2` and `-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=TEAMID`
to CMake. To check compilation without signing:

```sh
cmake --build build/apple-ios --config Release --target vf2 -- CODE_SIGNING_ALLOWED=NO
```

## iOS runtime and controls

The iOS target uses the existing MAME i960 interpreter and does not link SLJIT.
It requires no JIT entitlement or external JIT activation tool. Performance may differ from the Mac build.
Defaults are 1x resolution, antialiasing off and frame interpolation off. The Metal renderer is retained.
Game speed and compatibility should be checked with your ROM on a physical device.

Landscape controls include a circular joystick, G/P/K, Start and Menu.
The overlay uses a native `UIView` on iOS and `NSView` on Mac, separate from the game renderer.
It covers the window's safe area independently of the game's aspect ratio, including space outside the game image.
Menu is at the upper left; Start is at the upper right.
Buttons use faint dark translucent fills and white native text. Pressing a button slightly darkens its fill
and strengthens its outline. The joystick and Start/Menu buttons use the same style.

The joystick has a small central dead zone and maps touch positions and drag angles to eight digital directions.
Initial touches are accepted up to 1.3 times the visible joystick radius without enlarging the controls.
The left/right sectors extend 30 degrees above and below horizontal to reduce accidental jumping or crouching.
Diagonals hold two direction inputs at once, allowing sequences such as down → down-left → left.
Dragging outside the circle keeps the direction held until you release. Other fingers can press G/P/K at the same time.
Smaller **G+P** and **G+K** buttons sit above G and K; **G+P+K** sits below P.
An equally small **P+K** button sits to the lower left of G and presses punch and kick together.
Each combination holds its component buttons together. Releasing a combination does not release an individual
button still held by another finger.

Touch controls stay visible in menus. Up/down select items; left/right change values.
Button labels become **Back**, **OK**, **Clear** (remove the selected key/button mapping) and **Resume**.
Long menus scroll to the selected item. Back cancels a pending key-binding prompt.
In Auto mode, touch controls hide when a Bluetooth controller connects.
Use **Display → On-screen controls** to keep them visible or turn them off.
The default PC mode uses free play; Arcade mode uses the existing keyboard/controller Coin mapping.
When the app enters the background, it pauses audio and the game loop and saves settings and NVRAM.

## Checks without a ROM

```sh
ctest --test-dir build/apple-macos --output-on-failure
```

Checks cover rejection of corrupt, incomplete and oversized ZIPs; preservation of existing files on failure;
temporary-file cleanup; Unicode paths; touch press/move/release and simultaneous inputs;
eight-direction rotation, diagonals, neutral and cancellation; combination-button release and overlap with
individually held buttons. Native coordinate mapping and independence from the game's presentation are also checked.
Actual ROM execution, game speed, physical-device audio and controller behavior require separate validation.

Verified on **2026-10-21**: macOS, iOS Simulator and unsigned iPhone device builds succeeded.
The root Xcode project was also checked with Mac Debug, iOS Simulator Debug (arm64 and x86_64),
and iPhone Release (arm64, signing disabled).
ROM import and touch-input checks passed. On the iOS 26.5 simulator, the import screen and automatic detection
and error reporting for an incomplete ZIP added to Documents while the app was running were checked.
A user-supplied ROM booted and rendered demo scenes through Metal.
Touch overlays, graphics-initialization error reporting and cleanup of interrupted imports were also checked.
After adding native controls, all three platform builds were checked again. The simulator showed controls
outside a 4:3 game image and the menu layout; the Mac NSView checks passed for combination inputs,
release and menu hit testing. After the translucent dark style update, Mac and iOS Simulator builds passed,
and the overlay was checked over a running demo in the simulator.
The fork integration was also checked with a macOS 15.0 deployment target on Xcode 27, iOS Simulator and
unsigned iPhone builds on Xcode 26.6, and both ROM-import and touch-input regression checks. The Mac ran
900 frames with a real ROM and Metal after passing Xcode's document-versioning launch argument.
The English importer, automatic loading from `Documents/roms/vf2.zip`, and the iOS home-screen icon were
checked in a fresh simulator. The built app includes AppIcon metadata for both iPhone and iPad.
Physical-device performance and hands-on fighting gameplay still need separate validation.

Files app access uses Apple's
[UIFileSharingEnabled / LSSupportsOpeningDocumentsInPlace settings](https://developer.apple.com/library/archive/documentation/General/Reference/InfoPlistKeyReference/Articles/LaunchServicesKeys.html).
This is a development build; App Store distribution signing and review setup are not included.

## Review fixes

A static review with Claude Code led to fixes for iOS termination handling, concurrent dependency builds,
ROM save synchronization and temporary-file cleanup, Xcode resource selection, and waits during boot.
Dates use ISO format.

Xcode Run and Profile schemes disable document-versioning diagnostics. The app also recognizes
`-NSDocumentRevisionsDebugMode` and its boolean value, so launching from an older scheme does not mistake
Xcode's diagnostic argument for a ROM file and exit.

The `memoryBarrierWithScope` call that aborted in the iOS simulator was removed by splitting the spans and
shading work into separate compute passes. Synchronization relies on the default tracked resources between
passes. Demo scenes from an actual ROM continued rendering in the simulator with Metal API Validation enabled.
