#!/bin/bash
# Compila y corre la prueba del guion contra el volcado del original (sin el juego).
#   tools/oraculo_guion_test.sh <A1.csv>
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
clang++ -std=c++20 -O1 -o /tmp/oraculo_guion_test "$here/tools/oraculo_guion_test.cpp"
/tmp/oraculo_guion_test "$1"
