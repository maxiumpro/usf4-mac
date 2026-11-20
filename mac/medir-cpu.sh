#!/bin/bash
# medir-cpu.sh (B2/optimizar): corre en la Mac, con una FASE=2 de correr-mac.sh ya viva
# y lanzada con USF4_HITCH=25.   ssh mac 'bash -s' < medir-cpu.sh
# Compuerta USF4-1840-esperas-sse (dictamen del hub, 19-nov):
# - CPU ACUMULADA del proceso entre las dos marcas del guion del oráculo ("el guion
#   de 3000 frames arranca" y "3000 filas escritas"): segundos de CPU / segundos de
#   pared, en % de UN núcleo (100 = un núcleo entero). No muestras de top.
# - Tiempo por cuadro: los reportes de FPS dentro de la ventana (mediana, p99) y el
#   conteo de intervalos > 25 ms (líneas "hitch:" de USF4_HITCH=25). Son CUADROS
#   PRODUCIDOS, CON VSYNC (SDL_GPU no da presentedTime).
# - Térmica: pmset -g therm al inicio y al final.
set -uo pipefail
export PATH=/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin
R="$HOME/Games/USF4/run"
P=$(cat "$R/fase2.pid" 2>/dev/null) || { echo "no hay FASE=2 viva" >&2; exit 2; }
kill -0 "$P" 2>/dev/null || { echo "la FASE=2 ($P) no está viva" >&2; exit 2; }
L=$(ls -t "$R"/logs/fase2-*.log | head -1)
cpu_s() { ps -o cputime= -p "$P" | awk -F: '{s=0; for (i=1;i<=NF;i++) s=s*60+$i; print s}'; }
termico() { pmset -g therm 2>/dev/null | grep -iE "CPU_Speed_Limit|Scheduler_Limit|thermal warning|No thermal" | tr '\n' ' '; }
echo "termico al lanzar: $(termico)"
for i in $(seq 1 1200); do grep -q "el guion de 3000 frames arranca" "$L" && break; kill -0 "$P" 2>/dev/null || { echo "murió antes de la pelea"; tail -5 "$L"; exit 3; }; sleep 0.2; done
grep -q "el guion de 3000 frames arranca" "$L" || { echo "la pelea no arrancó en 240 s" >&2; exit 3; }
c0=$(cpu_s); t0=$(perl -MTime::HiRes=time -e 'printf "%.3f", time'); n0=$(wc -l < "$L")
echo "termico al inicio: $(termico)"
for i in $(seq 1 600); do grep -q "oraculo: 3000 filas escritas" "$L" && break; kill -0 "$P" 2>/dev/null || { echo "murió en la pelea"; tail -5 "$L"; exit 3; }; sleep 0.2; done
grep -q "oraculo: 3000 filas escritas" "$L" || { echo "sin la fila 3000 en 120 s" >&2; exit 3; }
c1=$(cpu_s); t1=$(perl -MTime::HiRes=time -e 'printf "%.3f", time')
echo "termico al final: $(termico)"
awk -v c0="$c0" -v c1="$c1" -v t0="$t0" -v t1="$t1" 'BEGIN {printf "CPU acumulada: %.1f s de CPU en %.1f s de pared = %.1f%% (100%% = 1 nucleo)\n", c1-c0, t1-t0, 100*(c1-c0)/(t1-t0)}'
tail -n +"$((n0 + 1))" "$L" > /tmp/u4_ventana.txt
echo "intervalos > 25 ms (cuadros producidos, con vsync): $(grep -c 'hitch: frame' /tmp/u4_ventana.txt)"
grep "fps:" /tmp/u4_ventana.txt | sed -E 's/.*fps: ([0-9.]+) at t=([0-9.]+) s \(interval ms: min ([0-9.]+) p1 [0-9.]+ median ([0-9.]+) p99 ([0-9.]+) max ([0-9.]+).*/FPS \1 (t=\2 s) mediana \4 ms p99 \5 ms max \6 ms/'
echo "volcado: $(ls -t "$R"/oraculo/MAC2-*.csv | head -1)"
