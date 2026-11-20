#!/bin/bash
# copiar-datos.sh (B2): copia a la Mac el SUBCONJUNTO mínimo de los datos del juego
# que el port abre en la ruta de B2: arranque -> logos -> Title -> MainMenu ->
# Training (Ryu contra Ryu, escenario de entrenamiento, guion del oráculo).
#
# Corre en Git Bash de Windows (WSL no llega a la Mac). Lee la instalación de
# Steam de la PC (solo lectura) y escribe en la Mac SOLO ~/Games/USF4/juego.
# Los datos del juego nunca van a git ni a la nube: ~/Games no está en iCloud
# (no es Escritorio ni Documentos).
#
# Cómo se midió la lista (19-nov, WSL): corrida del binario liftado con
# USF4_FILE_LOG=1 por la ruta del oráculo (IGUALES contra A1): 2,744 rutas
# distintas pedidas a CreateFile, de las que 204 existen en la instalación (el
# resto son sondeos de parches que tampoco existen en Windows). Se suman los
# 10 archivos de la raíz (SSFIV.exe y las DLL: el runtime los mapea sin
# CreateFile). Total: 212 archivos, 393 MB (385 MiB) de 16.4 GB. Más el
# esqueleto completo de carpetas (3,835), porque el juego sondea carpetas (DLC).
# Prueba de que alcanza: la misma corrida contra un árbol con SOLO estos
# archivos dio IGUALES contra A1 (ver PLAN-B2.md §4).
#
# Si se juega otra cosa (otros personajes, escenarios, Arcade, películas) van a
# faltar archivos: el juego fallará al abrirlos. Para agregar carpetas enteras:
#   EXTRA="resource/battle/chara/KEN dlc/04_costume/battle/chara/KEN" bash copiar-datos.sh
#
# OJO, azar: durante los logos el juego precarga una pelea de demostración con
# escenario y personajes que cambian de corrida a corrida (3 corridas: DET,
# MAD + Juri, VNX). Esta lista trae solo lo de la primera (DET). Con la
# instalación completa el Title llega en el frame ~1,127 (2 corridas); con la
# lista, a la corrida que le tocó MAD + Juri (que no están) el juego reintentó
# abrirlos 1,900 veces, arrancó una pelea antes del Title y llegó al Title en el
# frame 848. Aun así la ruta del oráculo dio IGUALES (es lo que se valida). Lo
# que pasa antes del Title con la lista NO es lo mismo que con todo: para mirar
# logos y Title tal cual, o el modo de atracción, usar AMPLIO=1 (abajo).
#
# Uso:
#   bash copiar-datos.sh -n    solo mide la lista en la PC (no toca la Mac)
#   bash copiar-datos.sh       copia y verifica huellas en la Mac
#   AMPLIO=1 bash copiar-datos.sh   todo menos películas y trajes (7.4 GB; decide el jugador)
set -euo pipefail
export MSYS_NO_PATHCONV=1

JUEGO="${USF4_PC_GAME_DIR:?define USF4_PC_GAME_DIR (la carpeta del juego en la PC)}"
MAC="${MAC_SSH:?define MAC_SSH=usuario@host de la Mac}"
SSH=(ssh -F /dev/null -i "${MAC_KEY:-$HOME/.ssh/id_ed25519}" -o BatchMode=yes -o IdentitiesOnly=yes
     -o StrictHostKeyChecking=yes -o UserKnownHostsFile="${MAC_KNOWN_HOSTS:-$HOME/.ssh/known_hosts}"
     -o ConnectTimeout=10 "$MAC")
SOLO_MEDIR=0
[ "${1:-}" = "-n" ] && SOLO_MEDIR=1

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
LISTA="$TMP/archivos.txt"

# --- la lista medida (rutas relativas a la carpeta del juego) -------------------
cat > "$LISTA" <<'LISTA'
SSFIV.exe
archive/battle/sound/BTL_CMN.cpk
archive/system/dat
cryptopp.dll
curllib.dll
dlc/03_character/ui/user_area/user_area.eaf
dlc/03_character_free/battle/hud/CMN/result.ems.emz
dlc/03_character_free/battle/hud/CMN/result.tex.emz
dlc/03_character_free/battle/stage/TRN.vfx.bvs
dlc/03_character_free/battle/stage/TRN.vfx.emz
dlc/03_character_free/battle/stage/TRN.vfx.ptex.emz
dlc/03_character_free/ui/update_info/user_update.eaf
dlc/03_character_free/ui/user_icon/user_icon_dlc2.eaf
dlc/03_character_free/ui/user_title/ENG/user_title_dlc2.eaf
dlc/03_character_free/ui/versus/localize/ENG/versus_tu03.m4s
dlc/03_character_free/ui/versus/versus/img16.emb.emz
dlc/04_ae2/battle/chara/PSN/PSN.cam.ema
dlc/04_ae2/battle/chara/PSN/PSN.fce.ema
dlc/04_ae2/battle/chara/PSN/PSN.light.emz
dlc/04_ae2/battle/chara/PSN/PSN.obj.ema
dlc/04_ae2/battle/chara/PSN/PSN.skl.emm
dlc/04_ae2/battle/chara/PSN/PSN.skl.emo
dlc/04_ae2/battle/chara/PSN/PSN.uc1.ema
dlc/04_ae2/battle/chara/PSN/PSN.vfx.emz
dlc/04_ae2/battle/chara/PSN/PSN.vfx.ptex.emz
dlc/04_ae2/battle/chara/PSN/PSN.vfx.ttex.emz
dlc/04_ae2/battle/chara/PSN/PSN_01.bsr
dlc/04_ae2/battle/chara/PSN/PSN_01.nml.emb
dlc/04_ae2/battle/chara/PSN/PSN_01.obj.emo
dlc/04_ae2/battle/chara/PSN/PSN_01.shd.emo
dlc/04_ae2/battle/chara/PSN/PSN_01_01.col.emb
dlc/04_ae2/battle/chara/PSN/PSN_01_01.obj.emm
dlc/04_ae2/battle/chara/PSN/PSN_E.csb
dlc/04_ae2/battle/hud/CMN/announce_tu3.ems.emz
dlc/04_ae2/battle/hud/CMN/announce_tu3.tex.emz
dlc/04_ae2/battle/hud/CMN/comment.ems.emz
dlc/04_ae2/battle/hud/CMN/training.ems.emz
dlc/04_ae2/battle/hud/ENG/IBK/comment.eaf
dlc/04_ae2/battle/hud/ENG/IBK/subtitle.tex.emz
dlc/04_ae2/battle/hud/ENG/PSN/chara.tex.emz
dlc/04_ae2/battle/hud/ENG/PSN/comment.eaf
dlc/04_ae2/battle/hud/ENG/PSN/subtitle.tex.emz
dlc/04_ae2/battle/hud/ENG/PSN/uc.tex.emz
dlc/04_ae2/battle/hud/ENG/RYU/comment.eaf
dlc/04_ae2/battle/hud/ENG/RYU/subtitle.tex.emz
dlc/04_ae2/battle/hud/ENG/training.tex.emz
dlc/04_ae2/battle/stage/DET.vfx.bvs
dlc/04_ae2/battle/stage/DET.vfx.emz
dlc/04_ae2/battle/stage/DET.vfx.ptex.emz
dlc/04_ae2/battle/stage/STG_DET.emz
dlc/04_ae2/battle/stage/STG_DET.tex.emz
dlc/04_ae2/movie/opening/opening.wmv
dlc/04_ae2/ui/advertise/demo/demo_title_logo_r.emz
dlc/04_ae2/ui/advertise/logo/capcom_1280x720.emz
dlc/04_ae2/ui/advertise/logo/criware_1280x720.emz
dlc/04_ae2/ui/advertise/logo/dolby_1280x720.emz
dlc/04_ae2/ui/advertise/logo/scaleform_1280x720.emz
dlc/04_ae2/ui/common/costume_info_wnd/localize/ENG/costume_info_wnd_patch.m4s
dlc/04_ae2/ui/common/openavi/localize/ENG/openavi_patch.m4s
dlc/04_ae2/ui/edition_mark/edition_mark.emz
dlc/04_ae2/ui/main_menu/bg.emz
dlc/04_ae2/ui/main_menu/localize/ENG/menu_item_tu3.m4s
dlc/04_ae2/ui/main_menu/player_status_dlc4.emz
dlc/04_ae2/ui/main_menu/vfx/main_menu.emz
dlc/04_ae2/ui/msg_box/localize/ENG/msg_box.m4s
dlc/04_ae2/ui/msg_box/localize/ENG/msg_box_patch.m4s
dlc/04_ae2/ui/msg_box/msg_box.emz
dlc/04_ae2/ui/network_mode/localize/ENG/youtube_replay.m4s
dlc/04_ae2/ui/pause_menu/localize/ENG/pause_menu.m4s
dlc/04_ae2/ui/pause_menu/training.emz
dlc/04_ae2/ui/user_icon/user_icon_dlc4.eaf
dlc/04_ae2/ui/user_title/ENG/user_title_dlc4.eaf
dlc/04_ae2/ui/versus/localize/ENG/versus.m4s
libeay32.dll
libsasl.dll
movie/nvidia/TheWay_1280x800.wmv
openldap.dll
patch/battle/hud/CMN/cockpit_text.ems.emz
patch/battle/hud/ENG/cockpit_text.tex.emz
patch/ui/common/costume_info_wnd/costume_info_wnd.emz
patch/ui/common/openavi/localize/ENG/openavi_patch.m4s
patch_ae2/battle/vfx/CMN.vfx.bvs
patch_ae2/shader/PsBgShader.emz
patch_ae2/shader/PsUserShader.emz
patch_ae2/shader/VsBgShader.emz
patch_ae2/shader/VsUserShader.emz
patch_ae2/shader/setting_technique.emz
patch_ae2/ui/common/openavi/localize/ENG/openavi.m4s
patch_ae2/ui/common/openavi/openavi.emz
patch_ae2/ui/common/openavi/openavi_icon.emz
patch_ae2/ui/fonts/ENG/fonts.f4s
patch_ae2/ui/fonts/ENG/fonts.gfx
patch_ae2/ui/main_menu/page_title/title_eng.emz
patch_ae2/ui/main_menu/title.emz
patch_ae2/ui/msg_box/localize/ENG/msg_box_patch_ae2.m4s
patch_ae2/ui/msg_box/localize/ENG/msg_box_patch_steam.m4s
patch_ae2/ui/user_text/user_text.emz
patch_ae2/ui/versus/versus/img17.emb.emz
patch_ae2_tu1/ui/user_flag/user_flag.eaf
patch_ae2_tu2/battle/hud/CMN/cockpit.ems.emz
patch_ae2_tu2/battle/hud/CMN/cockpit.tex.emz
patch_ae2_tu2/battle/regulation/ae2_110/IBK/IBK.bac
patch_ae2_tu2/battle/regulation/ae2_110/IBK/IBK.bcm
patch_ae2_tu2/ui/advertise/title/title_ultra.emz
patch_ae2_tu2/ui/main_menu/item_ut.emz
patch_ae2_tu2/ui/user_flag/user_flag_dlc4.eaf
patch_ae2_tu2/ui/user_title/ENG/user_title_dlc4_patch.eaf
patch_ae2_tu2/ui/versus/versus/gfx_swan.emb
patch_ae2_tu2/ui/versus/versus/img18.emb.emz
patch_ae2_tu3/battle/chara/PSN/PSN.vfx.bvs
patch_ae2_tu3/battle/regulation/ae2_111/PSN/PSN.bac
patch_ae2_tu3/battle/regulation/ae2_111/PSN/PSN.bcm
patch_ae2_tu3/battle/regulation/ae2_111/RYU/RYU.bac
patch_ae2_tu3/battle/regulation/ae2_111/RYU/RYU.bcm
patch_ae2_tu3/ui/main_menu/localize/ENG/menu_item.m4s
resource/battle/chara/IBK/IBK.cam.ema
resource/battle/chara/IBK/IBK.fce.ema
resource/battle/chara/IBK/IBK.light.emz
resource/battle/chara/IBK/IBK.obj.ema
resource/battle/chara/IBK/IBK.skl.emm
resource/battle/chara/IBK/IBK.skl.emo
resource/battle/chara/IBK/IBK.uc1.ema
resource/battle/chara/IBK/IBK.vfx.bvs
resource/battle/chara/IBK/IBK.vfx.emz
resource/battle/chara/IBK/IBK.vfx.ptex.emz
resource/battle/chara/IBK/IBK.vfx.ttex.emz
resource/battle/chara/IBK/IBK_01.bsr
resource/battle/chara/IBK/IBK_01.nml.emb
resource/battle/chara/IBK/IBK_01.obj.emo
resource/battle/chara/IBK/IBK_01.shd.emo
resource/battle/chara/IBK/IBK_01_01.col.emb
resource/battle/chara/IBK/IBK_01_01.obj.emm
resource/battle/chara/IBK/IBK_E.csb
resource/battle/chara/RYU/RYU.cam.ema
resource/battle/chara/RYU/RYU.fce.ema
resource/battle/chara/RYU/RYU.light.emz
resource/battle/chara/RYU/RYU.obj.ema
resource/battle/chara/RYU/RYU.skl.emm
resource/battle/chara/RYU/RYU.skl.emo
resource/battle/chara/RYU/RYU.uc1.ema
resource/battle/chara/RYU/RYU.vfx.bvs
resource/battle/chara/RYU/RYU.vfx.emz
resource/battle/chara/RYU/RYU.vfx.ptex.emz
resource/battle/chara/RYU/RYU.vfx.ttex.emz
resource/battle/chara/RYU/RYU_01.bsr
resource/battle/chara/RYU/RYU_01.nml.emb
resource/battle/chara/RYU/RYU_01.obj.emo
resource/battle/chara/RYU/RYU_01.shd.emo
resource/battle/chara/RYU/RYU_01_01.col.emb
resource/battle/chara/RYU/RYU_01_01.obj.emm
resource/battle/chara/RYU/RYU_01_02.col.emb
resource/battle/chara/RYU/RYU_01_02.obj.emm
resource/battle/chara/RYU/RYU_E.csb
resource/battle/chara_ex/hatching/tam.emz
resource/battle/hud/CMN/announce.ems.emz
resource/battle/hud/CMN/announce.tex.emz
resource/battle/hud/CMN/chara.ems.emz
resource/battle/hud/CMN/comment.ems.emz
resource/battle/hud/CMN/continue.ems.emz
resource/battle/hud/CMN/continue.tex.emz
resource/battle/hud/CMN/cursor.ems.emz
resource/battle/hud/CMN/cursor.tex.emz
resource/battle/hud/CMN/cursor_text.ems.emz
resource/battle/hud/CMN/notice.ems.emz
resource/battle/hud/CMN/notice.tex.emz
resource/battle/hud/CMN/result_text.ems.emz
resource/battle/hud/CMN/uc.ems.emz
resource/battle/hud/ENG/IBK/chara.tex.emz
resource/battle/hud/ENG/IBK/uc.tex.emz
resource/battle/hud/ENG/RYU/chara.tex.emz
resource/battle/hud/ENG/RYU/uc.tex.emz
resource/battle/hud/ENG/cursor_text.tex.emz
resource/battle/hud/ENG/result_text.tex.emz
resource/battle/stage/STG_TRN.emz
resource/battle/stage/STG_TRN.tex.emz
resource/battle/vfx/CMN.light.emz
resource/battle/vfx/CMN.vfx.emz
resource/battle/vfx/CMN.vfx.ptex.emz
resource/ui/chara_select/chara/sel_RYU.tex.emz
resource/ui/common/registration_symbol/registration_symbol_m.emz
resource/ui/intrusion/ENG/intrusion.emz
resource/ui/main_menu/localize/ENG/player_status.m4s
resource/ui/main_menu/player_status.emz
resource/ui/msg_box/msg_box.emz
resource/ui/net_icon/net_icon.emz
resource/ui/now_loading/now_loading.emz
resource/ui/option/option_bg.emz
resource/ui/pre_battle/pre_battle.emz
resource/ui/user_icon/user_icon.eaf
resource/ui/user_title/ENG/user_title.eaf
resource/ui/versus/versus/img0.emb.emz
resource/ui/versus/versus/img1.emb.emz
resource/ui/versus/versus/img10.emb.emz
resource/ui/versus/versus/img11.emb.emz
resource/ui/versus/versus/img12.emb.emz
resource/ui/versus/versus/img13.emb.emz
resource/ui/versus/versus/img14.emb.emz
resource/ui/versus/versus/img15.emb.emz
resource/ui/versus/versus/img2.emb.emz
resource/ui/versus/versus/img3.emb.emz
resource/ui/versus/versus/img4.emb.emz
resource/ui/versus/versus/img5.emb.emz
resource/ui/versus/versus/img6.emb.emz
resource/ui/versus/versus/img7.emb.emz
resource/ui/versus/versus/img8.emb.emz
resource/ui/versus/versus/img9.emb.emz
resource/ui/versus/vs.ems.emz
resource/ui/versus/vs.tex.emz
sdkencryptedappticket.dll
signature
ssleay32.dll
steam_api.dll
LISTA

# --- carpetas extra pedidas (EXTRA="a/b c/d"): todos sus archivos -------------
cd "$JUEGO"
# AMPLIO=1: todo menos películas (carpetas movie/, 3.69 GB) y trajes (*_costume/,
# 5.33 GB): 7.38 GB + lo medido. Para el modo de atracción (DemoBattle elige
# personajes y escenario al azar) y para elegir otros personajes; ver PLAN-B2.md §4.
if [ "${AMPLIO:-0}" = 1 ]; then
  find . -type f | sed 's#^\./##' | grep -v -E '(^|/)movie/|_costume/' >> "$LISTA"
fi
for d in ${EXTRA:-}; do
  [ -d "$d" ] || { echo "no existe la carpeta $d en el juego" >&2; exit 2; }
  find "$d" -type f >> "$LISTA"
done
sort -u -o "$LISTA" "$LISTA"
# EXCLUIR="regex": quita de la lista las rutas que casan (p. ej. los trajes 2+ de
# battle/chara, para que quepa en la Air).
if [ -n "${EXCLUIR:-}" ]; then grep -vE "$EXCLUIR" "$LISTA" > "$TMP/l2" || true; mv "$TMP/l2" "$LISTA"; fi
falta=0
while IFS= read -r f; do [ -f "$f" ] || { echo "falta en la PC: $f" >&2; falta=1; }; done < "$LISTA"
[ "$falta" = 0 ] || exit 2
# Esqueleto: todas las carpetas, vacías salvo lo de la lista.
find . -type d | sed 's#^\./##' | grep -v '^\.$' > "$TMP/carpetas.txt"
bytes=$(tr '\n' '\0' < "$LISTA" | xargs -0 stat -c %s | awk '{s+=$1} END {print s}')
echo "lista: $(wc -l < "$LISTA") archivos, $((bytes / 1048576)) MiB, $(wc -l < "$TMP/carpetas.txt") carpetas"
# Huellas para verificar en la Mac (formato de sha256sum; shasum -c lo lee).
tr '\n' '\0' < "$LISTA" | xargs -0 sha256sum > "$TMP/juego.sha256"
if [ "$SOLO_MEDIR" = 1 ]; then echo "solo medido (-n): la Mac no se tocó"; exit 0; fi

# --- copia: tar | ssh, a juego.nuevo y cambio de nombre al final -----------------
libre=$("${SSH[@]}" 'df -m ~ | awk "NR==2 {print \$4}"')
need=$((bytes / 1048576 + 1024))
if [ "$libre" -lt "$need" ]; then
  echo "NO: la Mac tiene $libre MB libres; hacen falta $need (datos + 1 GB de margen)" >&2; exit 2
fi
"${SSH[@]}" 'set -e; mkdir -p ~/Games/USF4; cd ~/Games/USF4; rm -rf juego.nuevo; mkdir juego.nuevo'
t0=$(date +%s)
{ cat "$TMP/carpetas.txt"; cat "$LISTA"; } > "$TMP/tar.txt"
tar -cf - --no-recursion -T "$TMP/tar.txt" \
  | "${SSH[@]}" 'tar -C ~/Games/USF4/juego.nuevo -xf -'
echo "enviado en $(( $(date +%s) - t0 )) s"
"${SSH[@]}" 'cat > ~/Games/USF4/juego.nuevo.sha256' < "$TMP/juego.sha256"
"${SSH[@]}" 'set -e; cd ~/Games/USF4/juego.nuevo
  shasum -a 256 -c --quiet ../juego.nuevo.sha256 && echo "huellas: OK"
  cd ~/Games/USF4; rm -rf juego.viejo
  if [ -d juego ]; then mv juego juego.viejo; fi
  mv juego.nuevo juego; mv juego.nuevo.sha256 juego.sha256; rm -rf juego.viejo
  echo "juego: $(du -sh juego | cut -f1), $(find juego -type f | wc -l) archivos"; df -h ~ | tail -1'
