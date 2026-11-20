#!/bin/bash
# sincronizar.sh (B2): lleva a la Mac lo necesario para COMPILAR el port de USF4.
#
# Corre en Git Bash de Windows, NO en WSL: desde WSL la Mac no responde
# ("No route to host" a la Mac y tiempo agotado por Tailscale; medido
# el 19-nov). Por eso el paquete se arma DENTRO de WSL y viaja por un tubo:
#   wsl tar -c  |  ssh mac tar -x
# La llave se queda en Windows (la llave de MAC_KEY); nunca se copia
# ni se imprime. Se verifica el host contra MAC_KNOWN_HOSTS.
#
# Uso:
#   bash sincronizar.sh -n     solo arma y mide el paquete en WSL (/root/b2-paquete); NO toca la Mac
#   bash sincronizar.sh -p     arma el paquete y lo prueba en WSL: configure sin red + compila toolkit y HLE (sin lo liftado)
#   bash sincronizar.sh        arma, manda a ~/Games/USF4/fuente y verifica las huellas en la Mac
#
# Qué va (medido el 19-nov: 307 MB, 2,248 archivos):
#   x86recomp/       toolkit, rama usf4-hle (E:\...\b1\x86recomp-hle, sin .git ni out/)
#   proyecto/        archivos que sigue git del proyecto del juego (E:\...\b1\proyecto)
#   generated/       salida de recomp_lift (WSL /root/b1-proyecto/generated) — DERIVADO DEL JUEGO
#   shaders/d3dx_asm 39 bytecodes de D3DXAssembleShader                     — DERIVADO DEL JUEGO
#   medidas/saltos_trampas.txt  saltos que W1 no vio (USF4_BRANCH_TRAPS)
#   deps/            softfloat, zydis (+zycore) y los 21 archivos de coremath que se compilan,
#                    sin .git, para configurar sin red (FETCHCONTENT_SOURCE_DIR_*)
#   datos-usuario/   el guardado y el registro que creó el port en B1 (/root/b1-run/data,
#                    sin hangs ni crashes): sin él el guion del oráculo choca con el
#                    diálogo "No saved data"
#   MANIFIESTO.sha256 y VERSION.txt
# Qué NO va: el build de WSL, symbols/, medidas/ (salvo las trampas), los datos del
# juego (eso es copiar-datos.sh). Nada de esto va a git ni a la nube.
set -euo pipefail
export MSYS_NO_PATHCONV=1

MODO=enviar
case "${1:-}" in
  -n) MODO=medir ;;
  -p) MODO=probar ;;
  "") ;;
  *) echo "uso: $0 [-n|-p]" >&2; exit 2 ;;
esac

DISTRO=Ubuntu-24.04
PAQ=/root/b2-paquete
MAC="${MAC_SSH:?define MAC_SSH=usuario@host de la Mac}"
SSH=(ssh -F /dev/null -i "${MAC_KEY:-$HOME/.ssh/id_ed25519}" -o BatchMode=yes -o IdentitiesOnly=yes
     -o StrictHostKeyChecking=yes -o UserKnownHostsFile="${MAC_KNOWN_HOSTS:-$HOME/.ssh/known_hosts}"
     -o ConnectTimeout=10 "$MAC")

# --- 1. Armar el paquete dentro de WSL --------------------------------------
# Las versiones se leen aquí (Git Bash): x86recomp-hle es un worktree con ruta
# de Windows y el git de WSL no lo abre.
ver() { echo "$(git -C "$1" rev-parse --short HEAD) $(git -C "$1" status --porcelain --untracked-files=no | wc -l | tr -d ' ') cambios sin commit"; }
V_TK=$(ver ${USF4_ROOT}/b1/x86recomp-hle)
V_PR=$(ver ${USF4_ROOT}/b1/proyecto)
wsl -d "$DISTRO" -u root -- env V_TK="$V_TK" V_PR="$V_PR" bash -s <<'WSL'
set -euo pipefail
P=/root/b2-paquete
E=${USF4_ROOT_WSL:?define USF4_ROOT_WSL}
TK=$E/b1/x86recomp-hle
PR=$E/b1/proyecto
W=/root/b1-proyecto
D=$W/build/_deps
mkdir -p "$P"/{x86recomp,proyecto,generated,shaders/d3dx_asm,medidas,deps,datos-usuario}

# rsync -a conserva las fechas de origen: si un archivo no cambió, ninja en la
# Mac no lo recompila después de una nueva sincronización.
rsync -a --delete --exclude out --exclude .git --exclude USF4-HLE.md "$TK/" "$P/x86recomp/"
rm -rf "$P/proyecto.tmp"; mkdir -p "$P/proyecto.tmp"
(cd "$PR" && git -c safe.directory='*' ls-files -z | rsync -a --from0 --files-from=- ./ "$P/proyecto.tmp/")
rsync -a --delete "$P/proyecto.tmp/" "$P/proyecto/"; rm -rf "$P/proyecto.tmp"
# El veredicto de correr-mac.sh necesita la línea "presentacion:" (driver,
# drawable, ventana, escala, modo) que escribe src/estado_juego.cpp desde el
# commit de B2. Sin ella la FASE=2 saldría SIN VERIFICAR: mejor no mandar nada.
grep -q 'presentacion: frame' "$P/proyecto/src/estado_juego.cpp" \
  || { echo "NO: src/estado_juego.cpp no escribe la línea presentacion: (commit de B2)" >&2; exit 1; }
rsync -a --delete --include='*.cpp' --include='lift_report.txt' --exclude='*' "$W/generated/" "$P/generated/"
rsync -a --delete "$W/shaders/d3dx_asm/" "$P/shaders/d3dx_asm/"
cp -p "$W/medidas/saltos_trampas.txt" "$P/medidas/"
rsync -a --delete --exclude .git "$D/softfloat-src/" "$P/deps/softfloat/"
rsync -a --delete --exclude .git --exclude .github "$D/zydis-src/" "$P/deps/zydis/"
# coremath: el clon pesa 831 MB; se compilan 17 funciones. Lista = cierre de
# includes medido con clang -MM (19-nov): 17 .c + dint.h (x2), pow.h, qint.h.
C=$D/coremath-src
rm -rf "$P/deps/coremath.tmp"; mkdir -p "$P/deps/coremath.tmp"
(cd "$C" && rsync -a --relative LICENSE \
  src/binary32/acos/acosf.c src/binary32/asin/asinf.c src/binary32/atan/atanf.c \
  src/binary32/atan2/atan2f.c src/binary32/cos/cosf.c src/binary32/log/logf.c \
  src/binary32/log2/log2f.c src/binary32/log10/log10f.c src/binary32/pow/powf.c \
  src/binary32/sin/sinf.c src/binary32/tan/tanf.c src/binary64/atan/atan.c \
  src/binary64/cos/cos.c src/binary64/exp/exp.c src/binary64/log/log.c \
  src/binary64/log/dint.h src/binary64/pow/pow.c src/binary64/pow/pow.h \
  src/binary64/pow/dint.h src/binary64/pow/qint.h src/binary64/sin/sin.c \
  "$P/deps/coremath.tmp/")
rsync -a --delete "$P/deps/coremath.tmp/" "$P/deps/coremath/"; rm -rf "$P/deps/coremath.tmp"
rsync -a --delete --exclude hangs --exclude crashes /root/b1-run/data/ "$P/datos-usuario/"

{
  echo "armado: $(date '+%Y-%m-%d %H:%M')"
  echo "x86recomp: $V_TK (rama usf4-hle)"
  echo "proyecto:  $V_PR"
  echo "generated: $(stat -c %y "$W/generated/table.cpp" | cut -c1-16) (fecha de table.cpp)"
} > "$P/VERSION.txt"
(cd "$P" && find . -type f ! -name MANIFIESTO.sha256 -print0 | LC_ALL=C sort -z \
   | xargs -0 sha256sum > MANIFIESTO.sha256)
cat "$P/VERSION.txt"
echo "paquete: $(du -sh "$P" | cut -f1) en $(wc -l < "$P/MANIFIESTO.sha256") archivos"
du -sh "$P"/* | sed 's#/root/b2-paquete/##'
WSL

if [ "$MODO" = medir ]; then
  echo "solo medido (-n): la Mac no se tocó"
  exit 0
fi

# --- 1b. Prueba en WSL: configure SIN RED y compilar lo que no es liftado -----
# Atrapa en la PC lo que falte en el paquete (un archivo, una dependencia). Es
# Linux, así que no prueba las ramas APPLE (Metal, spirv-cross); esas se ven en
# el configure de la Mac. (ninja -n no sirve aquí: con CONFIGURE_DEPENDS se
# detiene en "Re-running CMake".) Compila toolkit + HLE del proyecto con -j4 y
# nice; el código liftado es el mismo que ya compila /root/b1-proyecto.
if [ "$MODO" = probar ]; then
  wsl -d "$DISTRO" -u root -- bash -s <<'WSL'
set -euo pipefail
P=/root/b2-paquete; B=/root/b2-prueba
rm -rf "$B"
cmake -S "$P/proyecto" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DX86RECOMP_DIR="$P/x86recomp" -DUSF4_GENERATED_DIR="$P/generated" \
  -DCMAKE_PREFIX_PATH=/root/sdl3 -DRECOMP_LLVM_ROOT=/usr/lib/llvm-18 \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DFETCHCONTENT_SOURCE_DIR_SOFTFLOAT="$P/deps/softfloat" \
  -DFETCHCONTENT_SOURCE_DIR_ZYDIS="$P/deps/zydis" \
  -DFETCHCONTENT_SOURCE_DIR_COREMATH="$P/deps/coremath" > "$B.configure.log" 2>&1 \
  || { tail -20 "$B.configure.log"; exit 1; }
echo "configure sin red: OK"
echo "objetos liftados en el grafo: $(grep -c 'usf4_lifted.dir/.*gen_.*\.cpp\.o:' "$B/build.ninja") (esperado: los gen_*.cpp de generated/)"
t0=$(date +%s)
nice -n 15 ninja -C "$B" -j 4 recomp_runtime recomp_d3d9 recomp_dinput8 recomp_xinput \
  recomp_dbghelp recomp_quartz recomp_dsound usf4_hle > "$B.ninja.log" 2>&1 \
  || { grep -E "error|FAILED" "$B.ninja.log" | head -20; exit 1; }
echo "toolkit + HLE del proyecto compilan: OK en $(( $(date +%s) - t0 )) s"
rm -rf "$B" "$B.configure.log" "$B.ninja.log"
WSL
  echo "prueba (-p) hecha: la Mac no se tocó"
  exit 0
fi

# --- 2. Mandar a la Mac --------------------------------------------------------
# Copiar archivos es "ligero" (no pide candado, ~/Games/COORDINACION.md), pero
# solo se escribe dentro de ~/Games/USF4. Se arma en fuente.nuevo y se cambia
# de nombre al final: si el tubo se corta, la copia anterior queda intacta.
"${SSH[@]}" 'set -e; df -h ~ | tail -1; mkdir -p ~/Games/USF4; cd ~/Games/USF4; rm -rf fuente.nuevo; mkdir fuente.nuevo'
t0=$(date +%s)
wsl -d "$DISTRO" -u root -- tar -C "$PAQ" -cf - . \
  | "${SSH[@]}" 'tar -C ~/Games/USF4/fuente.nuevo -xf -'
echo "enviado en $(( $(date +%s) - t0 )) s"
"${SSH[@]}" 'set -e; cd ~/Games/USF4/fuente.nuevo
  shasum -a 256 -c --quiet MANIFIESTO.sha256 && echo "huellas: OK"
  cd ~/Games/USF4; rm -rf fuente.viejo
  if [ -d fuente ]; then mv fuente fuente.viejo; fi
  mv fuente.nuevo fuente; rm -rf fuente.viejo
  cat fuente/VERSION.txt; du -sh fuente; df -h ~ | tail -1'
