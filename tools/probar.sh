#!/bin/bash
# probar.sh SEGUNDOS [VAR=valor ...]: recompila (incremental) y corre sin pantalla
# con las variables dadas; imprime solo la ruta del log. Para iterar rápido.
set -uo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
secs=$1; shift
bash "$here/tools/build.sh" > /root/b1-run/build_ultimo.log 2>&1 || { grep -E "error|FAILED" /root/b1-run/build_ultimo.log | head; exit 3; }
env "$@" bash "$here/tools/correr_sin_pantalla.sh" "$secs"
