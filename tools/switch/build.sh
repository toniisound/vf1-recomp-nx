#!/usr/bin/env bash
# The Switch .nro from your own MAME "vf" set, in one go (Linux or WSL, with devkitPro installed):
#
#   tools/switch/build.sh path/to/vf.zip [path/to/model1io.zip]
#
# 1. tools/romset.py checks the set and writes its memory regions to build/vf (they stay in build/);
# 2. the PC's compilers recompile the game's V60 code to C++ (build/gen/vf_rec.cpp), with the
#    shipped address map (assets/vf_map.txt) saying where its functions start;
# 3. devkitA64 builds everything for the Switch: build/switch/vf1-recomp-nx.nro.
#
# Nothing made from the ROM leaves build/: the .nro holds the recompiled code, so it's yours alone
# (don't share it). The game's data is read from vf.zip on the SD card when it starts.
set -euo pipefail
cd "$(dirname "$0")/../.."

if [ $# -lt 1 ]; then
	echo "usage: $0 path/to/vf.zip [path/to/model1io.zip]" >&2
	exit 2
fi
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
if [ ! -f "$DEVKITPRO/libnx/switch_rules" ]; then
	echo "devkitPro's libnx isn't in $DEVKITPRO: install it (dkp-pacman -S switch-dev switch-sdl2 switch-freetype switch-zlib) or set DEVKITPRO" >&2
	exit 1
fi
for tool in python3 make; do command -v $tool >/dev/null || { echo "needs $tool" >&2; exit 1; }; done
HOST_CXX="${HOST_CXX:-g++}"
HOST_CC="${HOST_CC:-gcc}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

echo "== 1/3: the ROM set"
# where the desktop Makefile expects it (roms/ is git-ignored)
mkdir -p roms
if [ ! roms/vf.zip -ef "$1" ]; then cp "$1" roms/vf.zip; fi
if [ $# -ge 2 ] && [ ! roms/model1io.zip -ef "$2" ]; then cp "$2" roms/model1io.zip; fi
IO=()
[ -f roms/model1io.zip ] && IO=(roms/model1io.zip)
python3 tools/romset.py roms/vf.zip build/vf "${IO[@]}"

echo "== 2/3: the game's code recompiled to C++ (on the PC)"
# the address map as the recompiler's hints (the desktop Makefile reads every file in build/cov)
mkdir -p build/cov
cp assets/vf_map.txt build/cov/vf_map.txt
make -j"$JOBS" CXX="$HOST_CXX" CC="$HOST_CC" HOSTCC="$HOST_CC" build/gen/vf_rec.cpp build/musashi/m68kops.c

echo "== 3/3: the Switch build"
make -f Makefile.switch -j"$JOBS"

echo
echo "Done: build/switch/vf1-recomp-nx.nro"
echo "On the SD card:  /switch/vf1-recomp-nx/vf1-recomp-nx.nro"
echo "                 /switch/vf1-recomp-nx/roms/vf.zip   (and model1io.zip, if you have it)"
