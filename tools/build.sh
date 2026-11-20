#!/bin/bash
# build.sh: configura y compila en WSL/Linux el toolkit (solo lectura, copiado
# a $USF4_WORK/x86recomp), el código liftado ($USF4_WORK/generated) y el
# proyecto, hasta el enlace del ejecutable `usf4`. No lo ejecuta.
#   USF4_X86RECOMP  árbol de x86recomp (b0/src/x86recomp o b1/x86recomp-hle)
#   USF4_JOBS       hilos (por defecto 4; WSL tiene 7 GB)
#   extra: argumentos para cmake (p. ej. -DUSF4_LIFTED_O0=ON)
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$here/tools/local.env" ] && . "$here/tools/local.env"
: "${USF4_X86RECOMP:?define USF4_X86RECOMP (árbol de x86recomp)}"
W="${USF4_WORK:-/root/b1-proyecto}"
J="${USF4_JOBS:-4}"
mkdir -p "$W/x86recomp"
rsync -a --no-t --checksum --delete --exclude out --exclude .git --exclude "USF4-HLE.md" "$USF4_X86RECOMP/" "$W/x86recomp/"
nice -n 15 cmake -S "$here" -B "$W/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DX86RECOMP_DIR="$W/x86recomp" -DUSF4_GENERATED_DIR="$W/generated" "$@"
nice -n 15 ninja -C "$W/build" -j "$J" -k 0 usf4
