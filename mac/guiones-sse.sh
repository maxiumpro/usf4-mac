#!/bin/bash
# guiones-sse.sh: corre en Git Bash de la PC con el candado de USF4 tomado y la Air
# desbloqueada. Pasa los guiones de W0b (w0b/guiones, referencias de Windows en
# ember-oraculo/volcados/w0b) por el oráculo del port en la Air, con SSE_HOST=OFF
# (build) y ON (build-sse): lo que pidió el dictamen de la compuerta USF4-1840 antes
# de dejar SSE_HOST de fábrica. Cada corrida: FASE=1 (ventana oculta), volcado
# contra la referencia con --estricto (columnas comunes: las de W0b traen extras).
#   GUIONES="proy agarre cody fuzz1 fuzz2" bash b2/guiones-sse.sh
set -uo pipefail
export MSYS_NO_PATHCONV=1
cd "${USF4_ROOT:?define USF4_ROOT (la carpeta de trabajo)}"
MAC=(ssh -F /dev/null -i "${MAC_KEY:-$HOME/.ssh/id_ed25519}" -o BatchMode=yes "${MAC_SSH:?define MAC_SSH=usuario@host de la Mac}")
OUT=b2/resultados/guiones; mkdir -p "$OUT"
REF=ember-oraculo/volcados/w0b
GUIONES="${GUIONES:-proy agarre cody fuzz1 fuzz2 fuzz3}"
# guiones a la Mac (texto chico, sin datos del juego)
"${MAC[@]}" 'mkdir -p ~/Games/USF4/guiones'
for g in $GUIONES; do "${MAC[@]}" "cat > ~/Games/USF4/guiones/$g.txt" < "w0b/guiones/$g.txt"; done
ref_de() { case "$1" in fuzz*) echo "$REF/fz${1#fuzz}a.csv" ;; *) echo "$REF/${1}_A.csv" ;; esac; }
for g in $GUIONES; do
  read -r p1 p2 < <(python -I -c "import json,sys; d=json.load(open('w0b/guiones/indice.json'))['$g']; print(d['p1'], d['p2'])")
  for b in build build-sse; do
    echo "== $g ($p1 contra $p2) $b $(date +%T)"
    "${MAC[@]}" "BUILD_DIR=\$HOME/Games/USF4/$b USF4_ORACLE_SCRIPT=file:\$HOME/Games/USF4/guiones/$g.txt USF4_ORACLE_P1=$p1 USF4_ORACLE_P2=$p2 FASE=1 bash -s" < b2/correr-mac.sh 2>&1 | grep -E "^salida|VEREDICTO|FATAL" | cut -c1-160
    "${MAC[@]}" 'cat "$(ls -t ~/Games/USF4/run/oraculo/MAC1-*.csv | head -1)"' > "$OUT/${g}_$b.csv"
    python -I ember-oraculo/comparar_hashes.py "$(ref_de "$g")" "$OUT/${g}_$b.csv" --estricto 2>&1 | grep -E "RESULTADO|filas totales|DIFERENTES|primera" | cut -c1-140
  done
done
echo "== fin $(date +%T)"
