#!/bin/bash
# compilar-mac.sh (B2): compila el port de USF4 EN LA MAC (macOS arm64, Metal).
# Se corre en la Mac, con el turno tomado:
#   ssh mac 'bash -s' < compilar-mac.sh            (o con variables: 'JOBS=6 TSO=ON bash -s')
#
# Lee ~/Games/USF4/fuente (lo deja sincronizar.sh) y compila en ~/Games/USF4/build.
# Solo escribe dentro de ~/Games/USF4. No toca el candado: el hilo lo toma y lo
# suelta; este script solo se niega a correr si el candado no es de USF4.
#
# Variables:
#   JOBS=8        trabajos de compilación (la M4 tiene 4 núcleos P + 6 E; 8 deja 2 libres)
#   TSO=OFF       RECOMP_TSO: orden de memoria x86 en el código liftado (ON = más lento,
#                 más seguro con hilos; cambiarlo recompila TODO el código liftado)
#   LLVM=/opt/homebrew/opt/llvm@22   donde está libLLVMDemangle.a (brew install llvm@22)
#   SSE_HOST=OFF  RECOMP_SSE_HOST: SSE con la aritmética del procesador (más rápido; validar
#                 con el oráculo; cambiarlo recompila TODO el código liftado)
#   LIMPIO=1      borra ~/Games/USF4/build antes de configurar
#
# Estimado (medido en WSL, i9-13900K: 33.7 min de CPU para los 183 archivos
# liftados + 2.7 min del toolkit; enlace 1.7 s): en la M4 con 8 trabajos,
# ~10-15 min la primera vez; ~1 min si solo cambia el proyecto. Espacio: ~300 MB
# de build (WSL: 241 MB sin _deps).
set -euo pipefail
export PATH=/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin

U="$HOME/Games/USF4"
F="$U/fuente"
B="${BUILD_DIR:-$U/build}"   # BUILD_DIR: otro build lado a lado (p. ej. build-sse con SSE_HOST=ON)
JOBS="${JOBS:-8}"
TSO="${TSO:-OFF}"
SSE_HOST="${SSE_HOST:-OFF}"
LLVM="${LLVM:-/opt/homebrew/opt/llvm@22}"
mkdir -p "$U/logs"
LOG="$U/logs/compilar-$(date +%Y%m%d-%H%M%S).log"

# --- candado (solo se lee) -----------------------------------------------------
CAND="$HOME/Games/.mac-ocupada"
if [ ! -f "$CAND" ]; then
  echo "NO: el candado ~/Games/.mac-ocupada no existe; tómalo antes (línea 'USF4 | ...')" >&2; exit 3
fi
dueno=$(head -1 "$CAND" | cut -d'|' -f1 | tr -d ' ')
if [ "$dueno" != "USF4" ]; then
  echo "NO: la Mac es de otra sesión: $(head -1 "$CAND")" >&2; exit 3
fi

# --- requisitos ---------------------------------------------------------------
falta=0
for p in cmake ninja pkg-config glslangValidator spirv-cross; do
  command -v "$p" > /dev/null || { echo "falta $p (brew)" >&2; falta=1; }
done
[ -f "$LLVM/lib/libLLVMDemangle.a" ] || {
  echo "falta $LLVM/lib/libLLVMDemangle.a: brew install llvm@22 (o LLVM=<otra ruta>)" >&2; falta=1; }
[ -f "$F/MANIFIESTO.sha256" ] || { echo "falta $F (corre sincronizar.sh en la PC)" >&2; falta=1; }
[ "$falta" = 0 ] || exit 2
libre_mb=$(df -m "$HOME" | awk 'NR==2 {print $4}')
if [ "$libre_mb" -lt 3000 ]; then
  echo "NO: quedan $libre_mb MB libres; hacen falta ~3 GB (build ~300 MB + margen)" >&2; exit 2
fi
echo "libre antes: $libre_mb MB | JOBS=$JOBS TSO=$TSO SSE_HOST=$SSE_HOST | log $LOG"
cat "$F/VERSION.txt"

[ "${LIMPIO:-0}" = 1 ] && rm -rf "$B"
t0=$(date +%s)
{
  # Compilador: el clang de las Command Line Tools (Apple clang 21). Dependencias
  # de Homebrew (SDL3, ffmpeg, glslang, spirv-cross). Sin red: softfloat, zydis y
  # coremath vienen en fuente/deps.
  nice -n 15 cmake -S "$F/proyecto" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_PREFIX_PATH=/opt/homebrew -DRECOMP_LLVM_ROOT="$LLVM" \
    -DX86RECOMP_DIR="$F/x86recomp" -DUSF4_GENERATED_DIR="$F/generated" \
    -DRECOMP_TSO="$TSO" -DRECOMP_SSE_HOST="$SSE_HOST" \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_SOFTFLOAT="$F/deps/softfloat" \
    -DFETCHCONTENT_SOURCE_DIR_ZYDIS="$F/deps/zydis" \
    -DFETCHCONTENT_SOURCE_DIR_COREMATH="$F/deps/coremath"
  t1=$(date +%s); echo "configure: $((t1 - t0)) s"
  # -k 0: sigue con los demás archivos si uno falla, para ver todos los errores de una vez.
  nice -n 15 ninja -C "$B" -j "$JOBS" -k 0 usf4
} < /dev/null > "$LOG" 2>&1 || {
  echo "FALLÓ tras $(( $(date +%s) - t0 )) s; primeros errores:" >&2
  grep -E "error:|FAILED|CMake Error" "$LOG" | head -20 >&2
  exit 1
}
t2=$(date +%s)
echo "compilado en $((t2 - t0)) s ($(( (t2 - t0) / 60 )) min)"
ls -la "$B/usf4"
file "$B/usf4"
du -sh "$B" | sed 's/^/build: /'
echo "libre después: $(df -m "$HOME" | awk 'NR==2 {print $4}') MB"
