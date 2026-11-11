#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
# The NTFS bind mount may be owned differently from the container user.
# PTube also sets Git safe.directory before configuring fetched dependencies.
# Limit trust to this disposable build's dependency checkouts.
git config --global --add safe.directory "$ROOT/upstream/komi-tube/vendor/tilefinch/build-preset-psp/_deps/*"
python3 "$ROOT/native/patch-upstream.py"
cd "$ROOT/upstream/komi-tube/vendor/tilefinch"
sh scripts/fetch-psp-transport-deps.sh
cmake --preset psp \
    -DPython3_EXECUTABLE=/usr/bin/python3 \
    -DCMAKE_AR="$PSPDEV/bin/psp-ar" -DCMAKE_RANLIB="$PSPDEV/bin/psp-ranlib" \
    -DCMAKE_NM="$PSPDEV/bin/psp-nm" -DCMAKE_STRIP="$PSPDEV/bin/psp-strip" \
    -DCMAKE_LINKER="$PSPDEV/bin/psp-ld" -DCMAKE_OBJDUMP="$PSPDEV/bin/psp-objdump" \
    -DCMAKE_C_FLAGS_MINSIZEREL='-Os -DNDEBUG' \
    -DPSP_BROWSER_ENABLE_PSP_VOICE=OFF \
    -DPSP_BROWSER_ENABLE_HOST_WEBASSEMBLY=OFF \
    -DKOMI_EXTRA_CMAKE="$ROOT/native/ptwitch.cmake"
cmake --build build-preset-psp --target ptwitch-app-elf ptwitch-app-prx -j4
cmake -DPSP_OBJDUMP="$PSPDEV/bin/psp-objdump" \
    -DPSP_ELF="$PWD/build-preset-psp/ptwitch-app/ptwitch-app-elf.elf" \
    -DPSP_TEXT_LIMIT=4480000 -P cmake/CheckPspTextSize.cmake
python3 "$ROOT/native/package.py"
