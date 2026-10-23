#!/bin/sh
# Builds the Linux / Steam Deck package (x86-64 Linux, SteamOS) in Valve's Steam Runtime 3 "sniper"
# SDK container, so the binary runs against the libraries every Steam install has (as
# vf1-recomp's).
#
#   tools/deck/build.sh        -> build/deck/VirtuaFighter2/ and build/deck/VirtuaFighter2Recomp-deck.tar.gz
#
# Needs Docker able to run linux/amd64 images. The package has nothing from the ROMs in it: the
# player puts their own vf2.zip in VirtuaFighter2/roms/. DECK_FAST=1 also builds a -O3 package for
# the Deck's CPU, to compare speeds.
set -e
cd "$(dirname "$0")/../.."
IMG=registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest
[ -f build/deps/SDL3.tar.gz ] && [ -f build/deps/zlib.tar.gz ] || tools/mac/deps.sh   # the SDL3, FreeType and zlib sources
# the notices (from the tarballs, here), and the menu's version and date, from git here (the
# container may have no git)
tools/release/licenses.sh build/deck/LICENSES.txt
VF2_VERSION=${VF2_VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}   # (VF2_VERSION=v0.1.0 for a release)
VF2_DATE=$(git log -1 --format=%cd --date=format:'%b %-d %Y' 2>/dev/null)
VF2_COMMIT=$(git rev-parse --short HEAD 2>/dev/null)
docker run --rm --platform linux/amd64 -e VF2_VERSION="$VF2_VERSION" -e VF2_DATE="$VF2_DATE" -e VF2_COMMIT="$VF2_COMMIT" -e DECK_FAST="$DECK_FAST" \
	-v "$PWD":/src -w /src "$IMG" bash tools/deck/inside.sh
