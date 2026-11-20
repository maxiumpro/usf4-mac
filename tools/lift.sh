#!/bin/bash
# lift.sh: traduce a C++ el código del juego desde TU instalación: SSFIV.exe
# (build 834219) más cryptopp.dll como módulo (curllib.dll ya no se lifta:
# src/curl_offline.cpp, W1 midió 0 llamadas). Escribe solo
# fuera del repo ($USF4_WORK, por defecto /root/b1-proyecto):
#   symbols/   símbolos de Ghidra (de B0) y de pe_symbols.py
#   manifest/  manifests generados (copia de referencia en manifest/ del repo)
#   generated/ C++ liftado
#   medidas/   desensamblado, tablas EH y reportes
#
# Variables (entorno o tools/local.env):
#   USF4_GAME_DIR  carpeta del juego (la de SSFIV.exe)
#   USF4_LIFTER    recomp_lift ya compilado
#   USF4_B0        carpeta b0 (symbols de Ghidra de B0 y de pe_symbols.py)
#   USF4_ANALISIS  carpeta analisis (Ember, S0)
#   USF4_W1        carpeta w1 (opcional): su saltos_destinos.tsv entra como
#                  semillas del lift (tools/semillas_saltos.py)
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$here/tools/local.env" ] && . "$here/tools/local.env"
: "${USF4_GAME_DIR:?define USF4_GAME_DIR (carpeta de SSFIV.exe)}"
: "${USF4_LIFTER:?define USF4_LIFTER (recomp_lift)}"
: "${USF4_B0:?define USF4_B0 (carpeta b0 con proyecto/symbols)}"
: "${USF4_ANALISIS:?define USF4_ANALISIS (carpeta analisis)}"
W="${USF4_WORK:-/root/b1-proyecto}"
exe="$USF4_GAME_DIR/SSFIV.exe"
have=$(sha256sum "$exe" | cut -d' ' -f1)
case "$have" in 5d724595*b0b9eb) ;; *) echo "SSFIV.exe no es el build 834219 (SHA-256 $have)"; exit 1 ;; esac

mkdir -p "$W"/{symbols,manifest,generated,medidas,lift}
# Símbolos: los de Ghidra que dejó B0 (ya en LF) y los de las DLL.
rsync -a "$USF4_B0/proyecto/symbols/" "$W/symbols/usf4/"
for m in cryptopp; do
  # pe_symbols.py del toolkit: exports como entradas, functions.tsv vacío.
  mkdir -p "$W/symbols/$m"
  [ -s "$W/symbols/$m/exports.tsv" ] || rsync -a "$USF4_B0/proyecto/symbols_$m/" "$W/symbols/$m/"
  [ -f "$W/symbols/$m/functions.tsv" ] || printf 'entry\tmin\tmax\tchunks\tthunk\tnoreturn\tcallconv\tname\n' > "$W/symbols/$m/functions.tsv"
  [ -f "$W/symbols/$m/jumptables.tsv" ] || printf 'jmp\ttargets\n' > "$W/symbols/$m/jumptables.tsv"
done

# Tablas de EH y manifests (por patrón de bytes; ver tools/armar_manifest.py).
python3 -I "$here/tools/eh_entradas.py" "$exe" --handler4 8c8fa0 --seh-prolog 8d0170,8eb720 \
  > "$W/medidas/eh_entradas.tsv"
python3 -I "$here/tools/armar_manifest.py" --pe "$exe" --titulo "SSFIV.exe (USF4, build 834219)" \
  --ember "$USF4_ANALISIS/ember_names.tsv" --s0 "$USF4_ANALISIS/s0/candidatos_funcion.tsv" \
  --eh "$W/medidas/eh_entradas.tsv" --funcs "$W/symbols/usf4/functions.tsv" > "$W/manifest/usf4.manifest"
# Semillas de W1: destinos medidos de los saltos indirectos (tablas de salto
# unidas con las de Ghidra, y `entry` para los destinos sin función). Se parte
# siempre de las tablas de Ghidra (el rsync de arriba las restaura).
if [ -n "${USF4_W1:-}" ] && [ -f "$USF4_W1/saltos_destinos.tsv" ]; then
  # tools/saltos_extra.tsv: destinos que el port vio tomar y W1 no (mismo formato).
  extra=""
  [ -f "$here/tools/saltos_extra.tsv" ] && extra="--w1 $here/tools/saltos_extra.tsv"
  python3 -I "$here/tools/semillas_saltos.py" --w1 "$USF4_W1/saltos_destinos.tsv" $extra \
    --ghidra-jumptables "$W/symbols/usf4/jumptables.tsv" --funcs "$W/symbols/usf4/functions.tsv" \
    --out-jumptables "$W/symbols/usf4/jumptables_con_w1.tsv" --out-manifest "$W/manifest/usf4_saltos.inc" \
    | tee "$W/medidas/semillas_saltos.txt"
  mv "$W/symbols/usf4/jumptables_con_w1.tsv" "$W/symbols/usf4/jumptables.tsv"
  cat "$W/manifest/usf4_saltos.inc" >> "$W/manifest/usf4.manifest"
fi
# Sustituciones del oraculo nativo (src/oraculo.cpp).
cat "$here/manifest/usf4_oraculo.inc" >> "$W/manifest/usf4.manifest"
cat "$here/manifest/usf4_optim.inc" >> "$W/manifest/usf4.manifest"
for m in cryptopp; do
  python3 -I "$here/tools/armar_manifest.py" --pe "$USF4_GAME_DIR/$m.dll" --titulo "$m.dll" \
    > "$W/manifest/$m.manifest"
done

# cryptopp no pasó por Ghidra: sus tablas de salto (la CRT estática trae las de
# memcpy) salen de `jmp [reg*4+tabla]` (tools/tablas_dll.py).
python3 -I "$here/tools/tablas_dll.py" --pe "$USF4_GAME_DIR/cryptopp.dll" > "$W/symbols/cryptopp/jumptables.tsv"

# El lifter lee los módulos por nombre desde su directorio de trabajo.
for f in SSFIV.exe cryptopp.dll; do ln -sf "$USF4_GAME_DIR/$f" "$W/lift/$f"; done
echo "lift.sh: liftando SSFIV.exe + cryptopp.dll"
# --optnone-x87-over: las pocas funciones con miles de instrucciones x87
# tardan media hora cada una en optimizarse (como en darksouls-mac).
(cd "$W/lift" && nice -n 15 "$USF4_LIFTER" --exe SSFIV.exe --symbols "$W/symbols/usf4" \
  --manifest "$W/manifest/usf4.manifest" --out "$W/generated_nuevo" --optnone-x87-over 4000 \
  --module cryptopp.dll --symbols "$W/symbols/cryptopp" --manifest "$W/manifest/cryptopp.manifest" \
  "$@") > "$W/medidas/lift_stdout.txt" 2> "$W/medidas/lift_stderr.txt"
# Solo cambian de fecha los archivos cuyo contenido cambió: así ninja no
# recompila lo que no cambió.
mkdir -p "$W/generated"
rsync -a --no-t --checksum --delete "$W/generated_nuevo/" "$W/generated/"
rm -rf "$W/generated_nuevo"
cp "$W/generated/lift_report.txt" "$W/medidas/lift_report.txt"
# Saltos de la lista de W1 que siguen por despacho en tiempo de ejecución: el
# proyecto los vuelve fatales (USF4_BRANCH_TRAPS, src/main.cpp).
if [ -n "${USF4_W1:-}" ] && [ -f "$USF4_W1/sitios_saltos.tsv" ]; then
  python3 -I "$here/tools/saltos_sin_resolver.py" --generated "$W/generated" \
    --sitios "$USF4_W1/sitios_saltos.tsv" --salida "$W/medidas/saltos_sin_medir.txt" \
    | tee "$W/medidas/saltos_resumen.txt"
  # Los saltos de vtable (método virtual como cola) no tienen tabla que medir:
  # se separan de las trampas (ver saltos_virtuales.py).
  if [ -f "$W/medidas/ssfiv.dis" ]; then
    mv "$W/medidas/saltos_sin_medir.txt" "$W/medidas/saltos_sin_medir_w1.txt"
    python3 -I "$here/tools/saltos_virtuales.py" --dis "$W/medidas/ssfiv.dis"       --sin-medir "$W/medidas/saltos_sin_medir_w1.txt" --trampas "$W/medidas/saltos_trampas.txt"       --virtuales "$W/medidas/saltos_virtuales.txt" | tee -a "$W/medidas/saltos_resumen.txt"
  fi
fi
echo "lift.sh: listo ($(ls "$W/generated" | wc -l) archivos en $W/generated)"
