#!/bin/bash
# turno-optimizar.sh: corre en Git Bash de la PC con el candado de USF4 ya tomado y la
# Air desbloqueada. Compuerta USF4-1840-esperas-sse (dictamen del hub, 19-nov 19:50):
#   tanda 1 (espera)  A/B/B/A   A = USF4_ESPERA_ORIGINAL=1, B = esperas sin giro (build)
#   tanda 2 (SSE)     B/C/C/B   C = SSE_HOST=ON (build-sse), mismas esperas sin giro
# Cada corrida: CPU acumulada entre las marcas del guion, intervalos > 25 ms, FPS
# (cuadros producidos, con vsync), pmset therm, y el volcado contra A1 (--estricto).
set -uo pipefail
export MSYS_NO_PATHCONV=1
cd "${USF4_ROOT:?define USF4_ROOT (la carpeta de trabajo)}"
MAC=(ssh -F /dev/null -i "${MAC_KEY:-$HOME/.ssh/id_ed25519}" -o BatchMode=yes "${MAC_SSH:?define MAC_SSH=usuario@host de la Mac}")
OUT=b2/resultados/optimizar; mkdir -p "$OUT"
compara() {
  "${MAC[@]}" "cat \"$1\"" > "$OUT/$2.csv"
  wsl -u root -e python3 -I ${USF4_ROOT_WSL:?define USF4_ROOT_WSL}/ember-oraculo/comparar_hashes.py \
    ${USF4_ROOT_WSL:?define USF4_ROOT_WSL}/ember-oraculo/volcados/training/A1.csv "${USF4_ROOT_WSL:?define USF4_ROOT_WSL}/$OUT/$2.csv" --estricto 2>&1 \
    | grep -E "RESULTADO" | cut -c1-30
}
corrida() {  # etiqueta, entorno extra
  echo "== $1 $(date +%T)"
  "${MAC[@]}" "FASE=parar bash -s" < b2/correr-mac.sh > /dev/null 2>&1
  "${MAC[@]}" "$2 USF4_HITCH=25 FASE=2 PANTALLA=exclusive bash -s" < b2/correr-mac.sh 2>&1 | grep -E "VIVO|SE CERR|NO"
  r=$("${MAC[@]}" 'bash -s' < b2/medir-cpu.sh 2>&1); echo "$r"
  v=$(echo "$r" | sed -n 's/^volcado: //p')
  "${MAC[@]}" "FASE=parar bash -s" < b2/correr-mac.sh > /dev/null 2>&1
  [ -n "$v" ] && compara "$v" "$1" || echo "sin volcado"
  sleep 60
}
echo "== sincronizar + compilar build (SSE_HOST=OFF) y build-sse (ON) $(date +%T)"
bash b2/sincronizar.sh 2>&1 | grep -E "huellas"
"${MAC[@]}" 'JOBS=8 TSO=OFF SSE_HOST=OFF LLVM=/opt/homebrew/opt/llvm@22 bash -s' < b2/compilar-mac.sh 2>&1 | grep -E "compilado|FALL|error:"
"${MAC[@]}" 'BUILD_DIR=$HOME/Games/USF4/build-sse JOBS=8 TSO=OFF SSE_HOST=ON LLVM=/opt/homebrew/opt/llvm@22 bash -s' < b2/compilar-mac.sh 2>&1 | grep -E "compilado|FALL|error:"
sleep 60
echo "######## tanda 1: espera A/B/B/A"
corrida A1 "USF4_ESPERA_ORIGINAL=1"
corrida B1 ""
corrida B2 ""
corrida A2 "USF4_ESPERA_ORIGINAL=1"
echo "######## tanda 2: SSE B/C/C/B"
corrida B3 ""
corrida C1 "BUILD_DIR=\$HOME/Games/USF4/build-sse"
corrida C2 "BUILD_DIR=\$HOME/Games/USF4/build-sse"
corrida B4 ""
echo "== fin $(date +%T)"
