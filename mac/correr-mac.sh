#!/bin/bash
# correr-mac.sh (B2): corre el port de USF4 en la Mac. Se corre en la Mac, con el turno tomado:
#   ssh mac 'FASE=1 bash -s' < correr-mac.sh
#
# FASE=1   SIN VENTANA visible (USF4_HIDDEN=1): la ruta del oráculo (logos -> Title ->
#          MainMenu -> Training, Ryu contra Ryu, 3000 frames con guion). Guarda una
#          captura cada 300 frames y el volcado run/oraculo/MAC1-*.csv, y TERMINA SOLA
#          al escribir la fila 3000 (salida 0). Primera compuerta: el volcado contra A1
#          (en la PC, ver PLAN-B2.md) dice si el arm64 calcula igual antes de mirar pixeles.
#          OCULTA=0: la misma corrida con la ventana visible (si oculta no avanza, ver riesgos).
# FASE=2   CON VENTANA a 2560x1664 (RES=...): la misma ruta, y al terminar el guion se
#          queda en Training (USF4_ORACLE_KEEP_RUNNING=1) para que el jugador mire. Corre en
#          segundo plano y regresa; se cierra con FASE=parar.
# FASE=jugar   PARA JUGAR: pantalla completa EXCLUSIVA a 2560x1664 (RES=...): macOS cambia
#          el modo solo mientras el juego vive y lo regresa al salir (la configuración
#          de Pantallas de el jugador no se toca). Sin guion ni oráculo; los mandos reales
#          de la Mac cuentan (USF4_HOST_PADS=1); teclado y ratón siguen ignorados (Raw
#          Input no existe aún: una tecla sería fatal). Se cierra con FASE=parar.
# FASE=estado   ¿sigue vivo?, últimas líneas del log, capturas, y rehace el registro real.
# FASE=parar    cierra la corrida de la FASE=2.
#
# REGISTRO DE LO REAL (desde la primera corrida): junto a cada log queda
# <log>.real.txt con lo que la corrida USÓ, sacado del log del runtime y de la
# ventana, no de lo que se pidió: backend de SDL_GPU (tiene que ser Metal),
# back buffer, drawable y ventana, modo (ventana o pantalla completa), vsync y
# FPS medidos, y cómo terminó (FATAL, salida, filas del volcado). Termina con
# "VEREDICTO: VALIDA" o "VEREDICTO: INVALIDA (razón)". Un FATAL en el log, una
# salida distinta de 0 o un volcado con 0 filas dan INVALIDA (turno 1, 19-nov:
# murió con FATAL a los 51 s y el veredicto había dicho VALIDA).
# Una corrida INVALIDA no cuenta como medida (le pasó a FM4: pedía pantalla
# completa y corría en 1280x720; y a 120 FPS por una opción repetida).
#
# PANTALLA DE LA AIR: antes de medir se revisa que la tapa esté abierta, la
# sesión de el jugador en consola, la pantalla desbloqueada y encendida. Si no, el
# script se niega a correr y lo dice (CTR midió de noche con la pantalla apagada).
#
# Otras variables:
#   RES=2560x1664     resolución del juego en la FASE=2 (se escribe en su config.ini;
#                     probado en WSL: el juego la toma tal cual). Alternativa 16:9: 2560x1440.
#   PANTALLA=desktop  FASE=2: desktop (pantalla completa sin cambiar el modo), window o
#                     exclusive (cambia el modo solo mientras corre; FASE=jugar lo usa)
#   GFX=gpu           backend: gpu (SDL_GPU sobre Metal, el mismo camino que WSL) o metal
#                     (nativo; guarda una caché en ~/Library/Caches/x86recomp, FUERA de
#                     ~/Games/USF4: solo con CACHE_FUERA=1, ver PLAN-B2.md §5b)
#   LIMITE=1200       FASE=1: segundos máximos; después se corta con SIGALRM (salida 142)
#   NICE=5            prioridad de la corrida (la compilación usa 15)
#   REINICIAR=1       vuelve a sembrar run/data con el guardado de B1 (fuente/datos-usuario)
#
# Solo escribe dentro de ~/Games/USF4 (run/: guardados, registro, logs, capturas,
# volcados). La carpeta del juego (juego/) solo se lee: se compara una huella antes
# y después. No toca el candado: solo se niega a correr si no es de USF4.
# Con USF4_BACKGROUND=1 el runtime ignora el teclado y el ratón de la Mac: así, si
# alguien toca la ventana, no se dispara el fatal de raw input (WM_INPUT no existe aún).
set -uo pipefail
export PATH=/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin

U="$HOME/Games/USF4"
F="$U/fuente"
J="$U/juego"
R="$U/run"
BIN="${BUILD_DIR:-$U/build}/usf4"   # BUILD_DIR: correr otro build (build-sse)
FASE="${FASE:-1}"
PIDF="$R/fase2.pid"
mkdir -p "$R/logs" "$R/oraculo" "$R/capturas"

# --- ventana real, vista desde fuera (CoreGraphics por JXA; solo lee) -----------
# Desde SSH puede no ver ventanas (probado el 19-nov: la lista salió vacía); por
# eso el registro también usa la línea "presentacion:" del log si el proyecto la
# escribe (PLAN-B2.md §5b). Si ninguna fuente da el dato, queda "SIN DATO".
ventana_real() {
  osascript -l JavaScript - "$1" 2>&1 <<'JXA'
ObjC.import('CoreGraphics');
ObjC.import('AppKit');
function run(argv) {
  var pid = parseInt(argv[0], 10);
  var s = $.NSScreen.mainScreen, f = s.frame, k = s.backingScaleFactor;
  // Coincide con /Library/Preferences/com.apple.windowserver.displays.plist (19-nov:
  // 2048x1332 a escala 1): el escritorio sí se ve bien desde SSH; las ventanas no.
  var out = ['escritorio: ' + Math.round(f.size.width * k) + 'x' + Math.round(f.size.height * k) +
             ' px (' + f.size.width + 'x' + f.size.height + ' pt, escala ' + k + ')'];
  var list = ObjC.deepUnwrap(ObjC.castRefToObject(
      $.CGWindowListCopyWindowInfo($.kCGWindowListOptionOnScreenOnly, 0))) || [];
  var n = 0;
  for (var i = 0; i < list.length; i++) {
    var w = list[i];
    if (w.kCGWindowOwnerPID !== pid || w.kCGWindowLayer !== 0) continue;
    var b = w.kCGWindowBounds;
    var full = (b.Width === f.size.width && b.Height === f.size.height);
    out.push('ventana: ' + b.Width + 'x' + b.Height + ' pt = ' + Math.round(b.Width * k) + 'x' +
             Math.round(b.Height * k) + ' px' + (full ? ' (cubre la pantalla)' : ' (en ventana)'));
    n++;
  }
  if (!n) out.push('ventana: SIN DATO (ninguna ventana visible del pid ' + pid + ' desde esta sesión)');
  return out.join('\n');
}
JXA
}

# --- registro de lo real: <log>.real.txt -------------------------------------------
# $1 log, $2 pid (o vacío si ya terminó). Lee lo pedido de <log>.pedido.
registrar_real() {
  local L="$1" P="${2:-}" out="$1.real.txt" razones="" fat vivo=0 filas F_RC="" P_DUMP=""
  . "$L.pedido"
  # Cómo terminó. <log>.fin (F_RC) lo deja el guion al acabar el proceso; P_DUMP viene
  # de <log>.pedido. Así `estado` y la llamada de los 60 s rehacen lo mismo.
  [ -f "$L.fin" ] && . "$L.fin"
  if [ -n "$P" ] && kill -0 "$P" 2>/dev/null; then vivo=1; fi
  fat=$(grep -m1 '^FATAL' "$L" | cut -c1-200)
  # Filas de datos: sin las líneas "#" ni el encabezado (empiezan con el número de fila).
  if [ -n "$P_DUMP" ] && [ -f "$P_DUMP" ]; then filas=$(grep -c '^[0-9]' "$P_DUMP"); else filas=0; fi
  {
    echo "corrida: $(basename "$L")  (registro hecho $(date '+%Y-%m-%d %H:%M:%S'))"
    echo "PEDIDO: fase $P_FASE | backend $P_GFX | resolución $P_RES | modo $P_MODO | vsync $P_VSYNC | tope FPS ${P_FPS_LIMIT:-ninguno (FrameRate=FIXED del juego)}"
    echo "--- REAL (del log del runtime) ---"
    # SDL_GPU escribe "gfx: SDL_GPU (<driver>)"; el backend nativo, "gfx: Metal (<GPU>)".
    b=$(grep -m1 -E '^gfx: (SDL_GPU|Metal) \(' "$L" | sed 's/^gfx: //')
    echo "backend: ${b:-SIN DATO}"
    bb=$(grep -m1 '^gfx: back buffer' "$L" | sed 's/^gfx: back buffer //')
    echo "back buffer: ${bb:-SIN DATO}"
    cd=$(grep -m1 '^d3d9: CreateDevice [0-9]' "$L" | sed 's/^d3d9: //')
    echo "dispositivo: ${cd:-SIN DATO}"
    fs=$(grep -m1 'fullscreen for a\|fullscreen not entered' "$L")
    if [ -n "$fs" ]; then echo "modo: pantalla completa ($fs)"; else echo "modo: ventana (no hay línea de pantalla completa en el log)"; fi
    # Línea del proyecto (src/estado_juego.cpp, commit de B2): al primer Present y
    # en cada cambio. Formato:
    #   presentacion: frame N | driver D | back buffer AxB -> drawable CxD | rect … |
    #   ventana WxH pt = WxH px | escala E | modo M
    np=$(grep -c 'presentacion: frame' "$L")
    pr=$(grep 'presentacion: frame' "$L" | tail -1 | sed 's/^.*presentacion: //')
    echo "presentaciones distintas: $np (la última manda)"
    if [ -n "$pr" ]; then
      echo "presentacion: $pr"
      echo "driver (ventana): $(echo "$pr" | sed -n 's/.*| driver \([^|]*\) |.*/\1/p' | sed 's/ *$//')"
      echo "drawable: $(echo "$pr" | sed -n 's/.*-> drawable \([0-9]*x[0-9]*\).*/\1/p')"
      echo "ventana (proyecto): $(echo "$pr" | sed -n 's/.*| ventana \([^|]*\) |.*/\1/p' | sed 's/ *$//')"
      echo "escala: $(echo "$pr" | sed -n 's/.*| escala \([0-9.]*\).*/\1/p')"
      echo "modo (proyecto): $(echo "$pr" | sed -n 's/.*| modo \(.*\)$/\1/p')"
    else
      echo "drawable: SIN DATO (no hay línea presentacion: en el log; ¿binario anterior al commit de B2?)"
    fi
    if [ -n "$P" ] && kill -0 "$P" 2>/dev/null; then ventana_real "$P"; else ventana_real -1 | grep '^escritorio'; echo "ventana: SIN DATO (el proceso ya terminó)"; fi
    nf=$(grep -c '^warning: fps: \|^fps: ' "$L")
    echo "reportes de FPS: $nf (cada 10 s)"
    grep '^warning: fps: \|^fps: ' "$L" | tail -3 | cut -c1-200
    fstat=$(grep '^warning: fps: \|^fps: ' "$L" | sed 's/.*fps: \([0-9.]*\) at.*/\1/' | sort -n \
      | awk '{a[NR]=$1} END {if (NR) print a[int((NR+1)/2)], a[NR]}')
    echo "FPS (mediana y máximo de los reportes): ${fstat:-SIN DATO}"
    echo "tope efectivo: vsync del back buffer + FrameRate=FIXED del juego; USF4_FPS_LIMIT y USF4_VSYNC sin definir"
    # Cómo terminó (turno 1, 19-nov: murió con FATAL, 0 filas, y el veredicto dijo VALIDA).
    echo "fatal: ${fat:-ninguno}"
    if [ "$vivo" = 1 ]; then echo "salida: (sigue corriendo)"; else echo "salida: ${F_RC:-SIN DATO}"; fi
    echo "filas del volcado: $filas${P_DUMP:+ en $P_DUMP}"
  } > "$out"
  # Veredicto: todo lo que invalide la medida, con su razón.
  b=$(grep -m1 '^backend:' "$out")
  echo "$b" | grep -qi 'metal' || razones="$razones; backend no es Metal ($b)"
  dv=$(sed -n 's/^driver (ventana): //p' "$out")
  if [ -n "$dv" ] && ! echo "$dv" | grep -qi 'metal\|SIN DATO'; then razones="$razones; la ventana la reclamó $dv, no Metal"; fi
  # Drawable: lo que de verdad sale a la pantalla. Con ventana o pantalla completa
  # tiene que medir lo pedido; si no, la imagen se reescala (caso FM4).
  dr=$(sed -n 's/^drawable: \([0-9]*x[0-9]*\)$/\1/p' "$out")
  if [ "$P_MODO" != oculta ] && [ -n "$dr" ] && [ "$dr" != "$P_RES" ]; then
    razones="$razones; drawable $dr distinto de $P_RES (se reescala)"
  fi
  mp=$(sed -n 's/^modo (proyecto): //p' "$out")
  if [ -n "$mp" ]; then
    case "$P_MODO" in
      "pantalla completa") echo "$mp" | grep -q '^pantalla completa' || razones="$razones; se pidió pantalla completa y SDL dice: $mp" ;;
      ventana) echo "$mp" | grep -q '^ventana' || razones="$razones; se pidió ventana y SDL dice: $mp" ;;
    esac
  fi
  want=$P_RES
  echo "$(grep -m1 '^back buffer:' "$out")" | grep -q "$want" || razones="$razones; back buffer distinto de $want"
  if [ "$P_VSYNC" = ON ]; then grep -m1 '^back buffer:' "$out" | grep -q 'vsync' || razones="$razones; sin vsync"; fi
  if [ "$P_MODO" = "pantalla completa exclusiva" ]; then
    grep -q '^modo: pantalla completa.*host mode' "$out" || razones="$razones; se pidió pantalla completa exclusiva y no hay modo del anfitrión en el log"
    grep -q 'not entered' "$out" && razones="$razones; la pantalla completa no entró"
  fi
  if [ "$P_MODO" = "pantalla completa" ]; then
    grep -q '^modo: pantalla completa' "$out" || razones="$razones; se pidió pantalla completa y corrió en ventana"
    grep -q 'not entered' "$out" && razones="$razones; la pantalla completa no entró"
    # Pantalla completa de escritorio: el back buffer se escala al escritorio. Si el
    # escritorio no mide lo pedido, la imagen no es de esa resolución (caso FM4).
    esc=$(sed -n 's/^escritorio: \([0-9]*x[0-9]*\) px.*/\1/p' "$out" | head -1)
    if [ -n "$esc" ] && [ "$esc" != "$P_RES" ]; then
      razones="$razones; el escritorio de la Air es $esc: los $P_RES se reescalan (Pantalla en resolución nativa, o RES=$esc)"
    fi
  fi
  # Con vsync a 60 Hz ningún tramo de 10 s puede pasar de ~60: si uno pasa, el tope no es el pedido.
  fmax=$(sed -n 's/^FPS (mediana y máximo de los reportes): [0-9.]* \([0-9.]*\)$/\1/p' "$out")
  if [ -n "$fmax" ] && awk -v f="$fmax" 'BEGIN {exit !(f > 62)}'; then razones="$razones; FPS $fmax por encima de 60 (¿tope o vsync roto?)"; fi
  # Un FATAL siempre invalida. La salida y las filas cuentan solo con el proceso
  # terminado (a los 60 s, en `estado` y en la FASE=2 viva todavía no hay volcado).
  [ -n "$fat" ] && razones="$razones; el juego murió con $fat"
  if [ "$vivo" = 0 ]; then
    if [ -n "$F_RC" ] && [ "$F_RC" != 0 ]; then
      razones="$razones; salida $F_RC (0 = 3000 filas; 3 = guion incompleto; 142 = LIMITE)"
    elif [ -z "$F_RC" ] && [ "$P_FASE" = 1 ]; then
      razones="$razones; salida SIN DATO (falta $L.fin)"
    fi
    [ -z "$P_DUMP" ] || [ "$filas" -gt 0 ] || razones="$razones; 0 filas en el volcado${P_DUMP:+ ($P_DUMP)}"
  fi
  sin_drawable=0; grep -q '^drawable: SIN DATO' "$out" && sin_drawable=1
  if [ -n "$razones" ]; then
    echo "VEREDICTO: INVALIDA (${razones#; })" >> "$out"
  elif [ "$sin_drawable" = 1 ] && [ "$P_FASE" = 2 ]; then
    # En la FASE=2 lo que se mide es la imagen: sin drawable real no se puede dar por buena.
    echo "VEREDICTO: SIN VERIFICAR (falta el drawable: línea presentacion:, PLAN-B2.md §5b)" >> "$out"
  else
    [ "$sin_drawable" = 1 ] && echo "AVISO: drawable sin dato (FASE=1 oculta: la medida es el volcado, no la imagen)" >> "$out"
    echo "VEREDICTO: VALIDA" >> "$out"
  fi
  echo "registro de lo real: $out"; tail -1 "$out"
}

# --- pantalla de la Air encendida y desbloqueada -------------------------------------
pantalla_lista() {
  # 19-nov: sin tuberías hacia grep -q. Con `set -o pipefail`, grep -q sale al primer
  # acierto, el productor recibe SIGPIPE y la tubería "falla": daba "no encontrado"
  # justo cuando sí estaba (la Air desbloqueada salía como APAGADA).
  local root clam disp asrt problemas=""
  root=$(ioreg -n Root -d1)
  clam=$(ioreg -r -k AppleClamshellState -d1)
  disp=$(system_profiler SPDisplaysDataType 2>/dev/null)
  asrt=$(pmset -g assertions)
  grep -q '"CGSSessionScreenIsLocked"=Yes' <<<"$root" && problemas="$problemas; pantalla BLOQUEADA"
  grep -q '"kCGSSessionOnConsoleKey"=Yes' <<<"$root" || problemas="$problemas; la sesión de el jugador no está en la consola"
  grep -q '"AppleClamshellState" = Yes' <<<"$clam" && problemas="$problemas; tapa CERRADA"
  grep -qi 'Display Asleep: Yes' <<<"$disp" && problemas="$problemas; pantalla DORMIDA"
  # powerd sostiene esta aserción mientras la pantalla está prendida. Señal indirecta.
  grep -q 'Prevent sleep while display is on' <<<"$asrt" || problemas="$problemas; la pantalla parece APAGADA (sin la aserción de powerd)"
  if [ -n "$problemas" ]; then
    echo "NO SE MIDE: ${problemas#; }. Pide a el jugador que abra la tapa y desbloquee la Air." >&2
    return 1
  fi
  echo "pantalla: encendida, desbloqueada y con la sesión en consola"
}

case "$FASE" in
  estado)
    if [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; then VIVO=$(cat "$PIDF"); echo "VIVO (pid $VIVO)"; else VIVO=""; echo "NO corre"; fi
    L=$(ls -t "$R"/logs/fase*.log 2>/dev/null | head -1)
    if [ -n "$L" ]; then
      echo "log $L"; grep -E "FATAL|gfx:|back buffer|oraculo: 3000" "$L" | tail -6
      [ -f "$L.pedido" ] && registrar_real "$L" "$VIVO"
    fi
    C=$(ls -td "$R"/capturas/*/ 2>/dev/null | head -1)
    [ -n "$C" ] && echo "capturas: $(ls "$C" | wc -l | tr -d ' ') en $C (última: $(ls -t "$C" | head -1))"
    exit 0 ;;
  parar)
    if [ -f "$PIDF" ]; then
      P=$(cat "$PIDF")
      L=$(ls -t "$R"/logs/fase2-*.log "$R"/logs/fasejugar-*.log 2>/dev/null | head -1)
      [ -n "$L" ] && [ -f "$L.pedido" ] && registrar_real "$L" "$P"   # último registro con la ventana viva
      kill -INT "$P" 2>/dev/null; sleep 3; kill -9 "$P" 2>/dev/null
      rm -f "$PIDF"; echo "parado (pid $P)"
    else
      echo "no hay corrida de FASE=2"
    fi
    exit 0 ;;
  1|2|jugar) ;;
  *) echo "FASE=1, 2, jugar, estado o parar" >&2; exit 2 ;;
esac

# --- candado (solo se lee) -----------------------------------------------------
CAND="$HOME/Games/.mac-ocupada"
if [ ! -f "$CAND" ]; then
  echo "NO: el candado ~/Games/.mac-ocupada no existe; tómalo antes (línea 'USF4 | ...')" >&2; exit 3
fi
dueno=$(head -1 "$CAND" | cut -d'|' -f1 | tr -d ' ')
[ "$dueno" = "USF4" ] || { echo "NO: la Mac es de otra sesión: $(head -1 "$CAND")" >&2; exit 3; }

# --- requisitos ---------------------------------------------------------------
[ -x "$BIN" ] || { echo "falta $BIN (corre compilar-mac.sh)" >&2; exit 2; }
[ -f "$J/SSFIV.exe" ] || { echo "falta $J (corre copiar-datos.sh en la PC)" >&2; exit 2; }
[ "$FASE" = jugar ] && PANTALLA="${PANTALLA:-exclusive}"
if [ "$FASE" != 1 ] && [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; then
  echo "ya hay una FASE=2 o jugar corriendo (pid $(cat "$PIDF")); FASE=parar primero" >&2; exit 2
fi
GFXSEL="${GFX:-gpu}"
if [ "$GFXSEL" = metal ] && [ "${CACHE_FUERA:-0}" != 1 ]; then
  echo "NO: GFX=metal escribe ~/Library/Caches/x86recomp/metal-pipelines-v1.bin (fuera de ~/Games/USF4)." >&2
  echo "    Con el OK, repite con CACHE_FUERA=1." >&2; exit 2
fi
pantalla_lista || exit 4
# FASE=2 en pantalla completa: el escritorio tiene que medir lo pedido, o la imagen
# se reescala y la medida no vale. Leído el 19-nov: el escritorio está en 2048x1332
# a escala 1 (no en los 2560x1664 nativos del panel). Cambiarlo es de el jugador.
if [ "$FASE" = 2 ] && [ "${PANTALLA:-desktop}" = desktop ]; then
  esc=$(ventana_real -1 | sed -n 's/^escritorio: \([0-9]*x[0-9]*\) px.*/\1/p')
  if [ -n "$esc" ] && [ "$esc" != "${RES:-2560x1664}" ]; then
    echo "NO: el escritorio de la Air mide $esc y se pidió ${RES:-2560x1664}: se reescalaría." >&2
    echo "    Que el jugador ponga la pantalla en 2560x1664 (Ajustes > Pantallas), o corre con RES=$esc." >&2
    exit 4
  fi
fi

# --- datos del usuario (guardado, registro, config.ini) -----------------------------
D="$R/data"
if [ ! -d "$D/usf4" ] || [ "${REINICIAR:-0}" = 1 ]; then
  rm -rf "$D"; mkdir -p "$D"
  cp -R "$F/datos-usuario/" "$D/"
  echo "run/data sembrado con el guardado de B1"
fi
CFG="$D/usf4/drive_c/Documents and Settings/Player/My Documents/CAPCOM/SUPERSTREETFIGHTERIV/config.ini"
mkdir -p "$(dirname "$CFG")"
if [ "$FASE" = 1 ]; then
  RESOL=1280x720; MODO=oculta; FS_JUEGO=OFF
else
  RESOL="${RES:-2560x1664}"
  case "${PANTALLA:-desktop}" in
    window) MODO=ventana; FS_JUEGO=OFF ;;
    exclusive) MODO="pantalla completa exclusiva"; FS_JUEGO=ON ;;
    *) MODO="pantalla completa"; FS_JUEGO=ON ;;
  esac
fi
# Mismas claves que el config.ini real de la PC; el resto, por defecto del juego.
# Pantalla completa: el JUEGO la pide (FullScreen=ON) y el runtime la hace de
# escritorio (USF4_FULLSCREEN=desktop); pedirla solo al runtime no basta.
printf '[SYSTEM]\r\nVersion=2\r\n[DISPLAY]\r\nResolution=%s\r\nRefreshRate=60Hz\r\nFullScreen=%s\r\nVSync=ON\r\nFrameRate=FIXED\r\nAspect=AUTO\r\n' \
  "$RESOL" "$FS_JUEGO" > "$CFG"

huella() { (cd "$J" && find . -type f -exec stat -f '%N %z %m' {} + | LC_ALL=C sort | shasum -a 256 | cut -d' ' -f1); }
antes=$(huella)

STAMP=$(date +%Y%m%d-%H%M%S)
SHOTS="$R/capturas/fase$FASE-$STAMP"
LOG="$R/logs/fase$FASE-$STAMP.log"
mkdir -p "$SHOTS"

export XDG_DATA_HOME="$D"
export USF4_GAME_DIR="$J"
export USF4_SHADER_ASM_DIR="$F/shaders"
export USF4_BRANCH_TRAPS="$F/medidas/saltos_trampas.txt"
export USF4_STEAM_DLC=301660          # lo que dice el appmanifest de la PC (no se copia: trae el SteamID)
export USF4_GFX="$GFXSEL"
export USF4_BACKGROUND=1
# Audio: XAudio2 sobre FAudio (merge 4d35277). Mudo en las fases del oráculo; suena en FASE=jugar.
if [ "$FASE" = jugar ]; then unset SDL_AUDIODRIVER; else export SDL_AUDIODRIVER=dummy; fi
unset USF4_VSYNC USF4_FPS_LIMIT        # nada repetido ni heredado: manda lo del config.ini
if [ "$FASE" = jugar ]; then
  export USF4_HOST_PADS=1              # el mando de el jugador; teclado y ratón siguen ignorados
  unset USF4_TEST_PAD USF4_PAD_SCRIPT USF4_ORACLE_DUMP
  # Diagnóstico: JUGAR_TEST_PAD="@Title:start" usa el pad virtual en lugar del mando.
  [ -n "${JUGAR_TEST_PAD:-}" ] && export USF4_TEST_PAD=1 USF4_PAD_SCRIPT="$JUGAR_TEST_PAD"
else
export USF4_TEST_PAD=1
# @MainMenu:misc1 = ancla sin efecto (XInput no tiene misc1): en la Air el menú llega ~600 frames antes que en WSL y todavía anima su entrada; las flechas esperan 100 frames (menos que los 120 del reintento de bfd36d1, que si no re-presiona el ancla y reinicia la espera).
export USF4_PAD_SCRIPT="${PAD_SCRIPT:-@Title:start,@MainMenu:misc1,+100:dpdown,+20:dpdown,+20:dpdown,+20:dpdown,+30:a}"
export USF4_ORACLE_DUMP="$R/oraculo/MAC$FASE-$STAMP.csv"
export USF4_SHOT_DIR="$SHOTS"
fi
# Capturas BMP: 2.7 MB a 1280x720, 17 MB a 2560x1664. FASE=1: cada 300 frames
# (~16 capturas, ~45 MB). FASE=2 se queda corriendo: cada 3600 (~1 por minuto).
if [ "$FASE" = 1 ]; then export USF4_SHOT_EVERY="${SHOT_EVERY:-300}"; elif [ "$FASE" = 2 ]; then export USF4_SHOT_EVERY="${SHOT_EVERY:-3600}"; fi
export USF4_FPS_REPORT=10
NICE="${NICE:-5}"
# Lo pedido, para comparar con lo real (registrar_real).
printf 'P_FASE=%s\nP_GFX=%s\nP_RES=%s\nP_MODO="%s"\nP_VSYNC=ON\nP_FPS_LIMIT=\nP_DUMP="%s"\n' \
  "$FASE" "$GFXSEL" "$RESOL" "$MODO" "${USF4_ORACLE_DUMP:-}" > "$LOG.pedido"
echo "FASE=$FASE GFX=$GFXSEL res=$RESOL modo=$MODO | log $LOG | capturas $SHOTS"

if [ "$FASE" = 1 ]; then
  [ "${OCULTA:-1}" = 1 ] && export USF4_HIDDEN=1
  [ "${OCULTA:-1}" = 1 ] || sed -i '' 's/^P_MODO=.*/P_MODO="ventana"/' "$LOG.pedido"
  t0=$(date +%s)
  # macOS no trae `timeout`: perl deja una alarma que sobrevive al exec y corta la
  # corrida. caffeinate -d -i: ni la pantalla ni la Mac se duermen mientras viva.
  perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "${LIMITE:-1200}" \
    nice -n "$NICE" "$BIN" --game-dir "$J" < /dev/null > "$LOG" 2>&1 &
  P=$!
  caffeinate -d -i -w "$P" < /dev/null > /dev/null 2>&1 &
  # A los 60 s (shaders creados, logos), el registro con el proceso vivo; al final, otra vez.
  ( sleep 60; kill -0 "$P" 2>/dev/null && registrar_real "$LOG" "$P" > /dev/null ) < /dev/null > /dev/null 2>&1 &
  wait "$P"
  rc=$?
  echo "F_RC=$rc" > "$LOG.fin"          # para registrar_real (y para `estado` después)
  echo "salida $rc en $(( $(date +%s) - t0 )) s (0 = 3000 filas; 3 = guion incompleto; 142 = LIMITE)"
  grep -E "^FATAL|gfx:|back buffer|CreateDevice|watchdog|oraculo: 3000" "$LOG" | head -20
  [ -f "$LOG.real.txt" ] && grep '^ventana' "$LOG.real.txt" > "$LOG.ventana_viva.txt"
  registrar_real "$LOG" ""
  [ -s "$LOG.ventana_viva.txt" ] && { echo "--- ventana vista a los 60 s ---"; cat "$LOG.ventana_viva.txt"; } >> "$LOG.real.txt"
  grep '^filas del volcado:' "$LOG.real.txt"
  echo "capturas: $(ls "$SHOTS" | wc -l | tr -d ' ') en $SHOTS"
  despues=$(huella)
  if [ "$antes" = "$despues" ]; then echo "carpeta del juego: sin cambios"; else echo "AVISO: la carpeta del juego CAMBIÓ"; fi
  exit $rc
fi

# FASE=2: en segundo plano, se queda en Training. FASE=jugar: en segundo plano, sin guion.
[ "$FASE" = 2 ] && export USF4_ORACLE_KEEP_RUNNING=1
case "$MODO" in
  ventana) export USF4_FULLSCREEN=window ;;
  "pantalla completa exclusiva") export USF4_FULLSCREEN=exclusive ;;
  *) export USF4_FULLSCREEN=desktop ;;
esac
# FASE=jugar: el mando Hyperkin "Xenon Controller" (2e24:072c, GIP) solo lo ve un SDL3 con
# libusb (el de Homebrew no lo trae): lib/libSDL3.0.dylib es el que compiló TTT2 (3.4.18,
# copiado; SDL es zlib) y libusb sale de Homebrew. Por /usr/bin/env porque SIP borra las
# DYLD_* al pasar por nohup y nice (binarios del sistema).
CON_ENV=()
if [ "$FASE" = jugar ]; then
  [ -f "$U/lib/libSDL3.0.dylib" ] || { echo "falta $U/lib/libSDL3.0.dylib (SDL3 con libusb)" >&2; exit 2; }
  CON_ENV=(/usr/bin/env DYLD_LIBRARY_PATH="$U/lib" DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib SDL_HIDAPI_LIBUSB_WHITELIST=0)
fi
nohup nice -n "$NICE" ${CON_ENV[@]+"${CON_ENV[@]}"} "$BIN" --game-dir "$J" < /dev/null > "$LOG" 2>&1 &
echo $! > "$PIDF"                     # nohup y nice hacen exec: es el pid del juego
nohup caffeinate -d -i -w "$(cat "$PIDF")" < /dev/null > /dev/null 2>&1 &
sleep 20
if kill -0 "$(cat "$PIDF")" 2>/dev/null; then
  echo "VIVO (pid $(cat "$PIDF")); la ventana debe estar en pantalla. Ver: FASE=estado; cerrar: FASE=parar"
else
  wait "$(cat "$PIDF")"; echo "F_RC=$?" > "$LOG.fin"
  echo "SE CERRÓ en los primeros 20 s:"; tail -15 "$LOG"; rm -f "$PIDF"; registrar_real "$LOG" ""; exit 1
fi
grep -E "gfx:|back buffer|CreateDevice|user32: window" "$LOG" | head -6
registrar_real "$LOG" "$(cat "$PIDF")"
echo "(los FPS se llenan con el tiempo: FASE=estado rehace el registro)"
