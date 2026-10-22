# Updating from upstream

This fork preserves the history of [mr-finster/vf2-recomp](https://github.com/mr-finster/vf2-recomp).
Apple changes are ordinary commits on top of that history, so upstream updates can be merged normally.
The most recently integrated upstream commit is recorded in [upstream.json](upstream.json).

## Directory boundaries

- `apple/` owns the Apple build configuration, platform adapters, ROM importer, native controls,
  touch input state, iOS interpreter adapter, app icon and regression checks.
- `VF2Recomp.xcodeproj` is generated from `apple/generate-xcode-project.py` and `apple/sources.json`.
  Change the generator or manifest before regenerating. There is no second copy of the game engine.
- Upstream's `src/`, `assets/`, `third_party/`, `Makefile` and release tools remain in place.
  Only the integration points below need special attention when merging.

| Upstream files | Port integration |
| --- | --- |
| `src/host/main.cpp` | Apple startup/lifecycle hooks, interpreter defaults, touch input, menu actions and Xcode launch arguments |
| `src/host/menu.cpp`, `src/host/menu.h` | Touch navigation and scrolling; omit Quit on iOS |
| `src/host/romset.cpp`, `src/host/romset.h` | Validated, atomic ROM import shared by the desktop and iOS importers |
| `src/host/sysinfo.cpp` | iOS-compatible system information |
| `src/machine/model2a.cpp` | Interpreter-only build guard |
| `src/gpu/gpu3d_metal.mm` | iOS availability checks and synchronization supported by the simulator |
| `assets/ui/menu.rcss` | Menu layout while touch controls are visible |
| `README.md`, `.gitignore` | Apple entry points and local Xcode file exclusions |

Keep new Apple behavior in `apple/` where possible. Keep host changes small and use `VF2_IOS`,
`VF2_NO_JIT` or `VF2_NATIVE_CONTROLS` for behavior specific to those builds.
The iOS icon generator imports upstream's palette and letter geometry without modifying its release tool.

## Merge an update

Start from a clean working tree. Add the upstream remote once:

```sh
git remote add upstream https://github.com/mr-finster/vf2-recomp.git
```

Then fetch and inspect an update on a separate branch:

```sh
git fetch upstream
python3 apple/upstream.py
git switch -c update-upstream
git merge upstream/main
```

`apple/upstream.py` is read-only unless `--record-base` is explicitly supplied. It lists files modified by the
port, checks the source manifest and reports overlap with incoming upstream changes. It does not download,
merge or push anything. Review the integration points even when Git reports no textual conflicts.

After resolving conflicts, complete the merge commit. If upstream added or removed game source files,
update `apple/sources.json` to match its build before regenerating the Xcode project:

```sh
python3 apple/generate-xcode-project.py
python3 apple/upstream.py --record-base upstream/main
git diff --check
xcodebuild -project VF2Recomp.xcodeproj -scheme 'VF2 macOS' \
  -configuration Debug -destination 'platform=macOS' build
xcodebuild -project VF2Recomp.xcodeproj -scheme 'VF2 iOS' \
  -configuration Debug -destination 'generic/platform=iOS Simulator' build
cmake -S apple -B build/apple-macos -DCMAKE_BUILD_TYPE=Release
cmake --build build/apple-macos --target rom_import_test touchcheck --parallel 4
ctest --test-dir build/apple-macos --output-on-failure
```

Check launch from Xcode, the Files app importer, touch controls in game and in menus, and Metal rendering.
Record the refreshed manifest/project/base in an English commit. Review the update branch before merging
it into your fork's `main`. Preserve upstream history; do not replace the repository with a source archive.
