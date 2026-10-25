#!/bin/sh
# Builds the Mac download: build/release/VirtuaFighter2Recomp-mac.zip with the app (universal,
# macOS 11+, ad-hoc signed: players right-click > Open the first time), README.txt and LICENSES.txt.
# Nothing from the ROMs goes in it: the player chooses their own vf2.zip at the first launch. The
# one picture shipped is assets/backgrounds/select.png, the widescreen sides painted for the player
# select (its middle 4:3 blank: the game draws it), as vf1-recomp ships its painted sides.
set -e
cd "$(dirname "$0")/../.."
tools/mac/build_release.sh
APP="build/release/Virtua Fighter 2 Recomp.app"
rm -rf build/release && mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/assets"
cp build/rel/vf2 "$APP/Contents/MacOS/Virtua Fighter 2 Recomp"
strip -S -x "$APP/Contents/MacOS/Virtua Fighter 2 Recomp"   # (no debug info: it names the build machine's folders)
cp -R assets/ui assets/fonts assets/backgrounds "$APP/Contents/Resources/assets/"
cp assets/icon/VF2.icns "$APP/Contents/Resources/VF2.icns"
VERSION=${VF2_VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}   # (VF2_VERSION=v0.1.0 for a release)
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key><string>Virtua Fighter 2 Recomp</string>
	<key>CFBundleDisplayName</key><string>Virtua Fighter 2 Recomp</string>
	<key>CFBundleExecutable</key><string>Virtua Fighter 2 Recomp</string>
	<key>CFBundleIdentifier</key><string>recomp.vf2.unofficial</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>NSLocalNetworkUsageDescription</key><string>Online play: to host a match or join one on your network.</string>
	<key>CFBundleIconFile</key><string>VF2</string>
	<key>CFBundleShortVersionString</key><string>$VERSION</string>
	<key>CFBundleVersion</key><string>$VERSION</string>
	<key>LSMinimumSystemVersion</key><string>11.0</string>
	<key>NSHighResolutionCapable</key><true/>
	<key>LSApplicationCategoryType</key><string>public.app-category.arcade-games</string>
	<key>GCSupportsControllerUserInteraction</key><true/>
</dict>
</plist>
PLIST
codesign --force --deep -s - "$APP"
cp tools/release/README.txt build/release/README.txt
tools/release/licenses.sh build/release/LICENSES.txt
# one zip holding the app folder and the two text files
(cd build/release && rm -f VirtuaFighter2Recomp-mac.zip && mkdir -p pkg/"Virtua Fighter 2 Recomp" && \
 cp -R "Virtua Fighter 2 Recomp.app" README.txt LICENSES.txt pkg/"Virtua Fighter 2 Recomp"/ && \
 ditto -c -k --norsrc --keepParent "pkg/Virtua Fighter 2 Recomp" VirtuaFighter2Recomp-mac.zip && rm -rf pkg)
ls -la build/release
