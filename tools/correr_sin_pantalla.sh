#!/bin/bash
# correr_sin_pantalla.sh: corre el binario liftado (`usf4`) una vez, sin
# mostrar nada en pantalla, con los datos del juego en SOLO LECTURA y todo lo
# que escribe a una carpeta propia. Termina por tiempo (por defecto 120 s) o
# porque el juego se detiene con un fatal con nombre.
#
#   tools/correr_sin_pantalla.sh [SEGUNDOS] [-- argumentos del juego]
#
# Variables (entorno o tools/local.env):
#   USF4_GAME_DIR         carpeta del juego (solo se lee)
#   USF4_WORK             /root/b1-proyecto (binario en build/usf4)
#   USF4_RUN_DIR          carpeta propia de la corrida (por defecto /root/b1-run);
#                         ahí van los guardados, el registro y el log
#   USF4_SHADER_ASM_DIR   tabla de D3DXAssembleShader (tools/tabla_d3dx.py)
#   USF4_BRANCH_TRAPS     saltos no medidos por W1 (los deja tools/lift.sh)
#   USF4_VIDEO            driver de video de SDL: offscreen (por defecto), dummy
#
# Nada va a la carpeta del juego, ni a Documents\CAPCOM, ni a Steam: el runtime
# redirige todo lo que el invitado escribe al árbol por usuario de
# $XDG_DATA_HOME/usf4 (aquí dentro de USF4_RUN_DIR). Antes y después se
# compara el contenido de la carpeta del juego (tamaño y fecha de cada
# archivo) y se avisa si algo cambió.
set -uo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$here/tools/local.env" ] && . "$here/tools/local.env"
secs=120
if [ "${1:-}" ] && [ "${1:-}" != "--" ]; then secs=$1; shift; fi
[ "${1:-}" = "--" ] && shift
: "${USF4_GAME_DIR:?define USF4_GAME_DIR}"
W="${USF4_WORK:-/root/b1-proyecto}"
R="${USF4_RUN_DIR:-/root/b1-run}"
mkdir -p "$R/data" "$R/logs"
bin="$W/build/usf4"
[ -x "$bin" ] || { echo "no existe $bin (corre tools/build.sh)"; exit 2; }

snap() { (cd "$USF4_GAME_DIR" && find . -type f -printf '%p %s %T@\n' | LC_ALL=C sort | sha256sum | cut -d' ' -f1); }
antes=$(snap)

export XDG_DATA_HOME="$R/data"
export SDL_VIDEO_DRIVER="${USF4_VIDEO:-offscreen}"
export USF4_BACKGROUND=1
# Vulkan por software (lavapipe): no hay GPU en WSL para SDL_GPU.
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}"
export USF4_GAME_DIR USF4_SHADER_ASM_DIR USF4_BRANCH_TRAPS
export SDL_AUDIODRIVER=dummy
stamp=$(date +%Y%m%d-%H%M%S)
log="$R/logs/corrida-$stamp.log"
echo "correr_sin_pantalla: $secs s, log $log" >&2
nice -n 15 timeout --signal=INT "$secs" "$bin" --game-dir "$USF4_GAME_DIR" "$@" > "$log" 2>&1
rc=$?
echo "correr_sin_pantalla: salida $rc (124 = tiempo)" >&2
despues=$(snap)
if [ "$antes" != "$despues" ]; then
  echo "AVISO: la carpeta del juego CAMBIO durante la corrida" >&2
else
  echo "carpeta del juego: sin cambios (huella $antes)" >&2
fi
echo "$log"
exit $rc
