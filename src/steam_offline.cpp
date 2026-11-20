// steam_api.dll de USF4 (Steamworks 1.26-1.28: SteamUser017, SteamFriends014,
// SteamUtils006, SteamApps006, STEAMREMOTESTORAGE_INTERFACE_VERSION012,
// SteamClient012, ...) como un cliente de Steam abierto pero SIN CONEXIÓN.
//
// Vive en el proyecto, no en x86recomp: las respuestas dependen de este juego
// (su AppID, sus DLC, dónde guarda) y el recomp_online del toolkit no sirve
// aquí (su GetAppID es fatal porque el juego llama
// RestartAppIfNecessary(0), y su BIsSubscribedApp contesta que no, con lo que
// el juego dice "Steam user not subscribed" y sale). El proyecto NO enlaza
// recomp_online: sus registros tendrían las mismas claves.
//
// Semántica (ESTADO.md §Steam):
// - Arranque: SteamAPI_Init = true (el juego, en 0050E690, muestra "Steam
//   must be running" y sale si es false), RestartAppIfNecessary = false.
// - Sin conexión: BLoggedOn = false; nunca se entrega un SteamAPICall_t, así
//   que nunca hay resultados ni callbacks que publicar.
// - Propiedad: el juego base (45760) y los DLC que el appmanifest de Steam
//   lista como instalados (o USF4_STEAM_DLC). Cada AppID consultado se anota
//   una vez en stderr.
// - Guardados: ISteamRemoteStorage escribe en la carpeta local
//   C:\Program Files (x86)\Steam\userdata\1\45760\remote del árbol por usuario
//   del runtime ($XDG_DATA_HOME/usf4/drive_c/...); esa carpeta es "la nube".
// - Todo lo demás (red, lobbies, P2P, voz, marcadores, UGC/Workshop, compartir
//   repeticiones) no tiene manejador: el primer uso detiene la corrida con su
//   nombre ("steam_api.dll!ISteamMatchmaking009::#13").
//
// Los métodos son __thiscall: `this` en ecx, argumentos en la pila, los saca
// el llamado (nargs = palabras de pila; un CSteamID o uint64 por valor cuenta
// 2). Un CSteamID devuelto va por puntero oculto (primer argumento).

#include <dirent.h>
#include <algorithm>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "runtime/hle/hle.h"
#include "runtime/hle/kernel32_state.h"
#include "runtime/log.h"
#include "runtime/runtime.h"

namespace recomp::hle {
namespace {

constexpr const char* kDll = "steam_api.dll";
constexpr int kSlots = 128;  // más que cualquier interfaz de Steamworks

// El AppID de Ultra Street Fighter IV (el exe pregunta BIsSubscribedApp(45760)
// al arrancar; su steam_appid.txt dice lo mismo).
constexpr uint32_t kAppId = 45760;
// Una cuenta individual fija del universo público (cuenta 1): lo que el juego
// guarde por SteamID no se mueve entre corridas. No es la cuenta del usuario.
constexpr uint64_t kSteamId = 0x0110000100000001ull;
constexpr uint32_t kAccountId = uint32_t(kSteamId);
// Cuota local de la "nube" (la de Steam no se puede consultar sin conexión).
constexpr int32_t kQuotaBytes = 100 * 1000 * 1000;

const std::string& UserDataGuest() {
  static const std::string p = "C:\\Program Files (x86)\\Steam\\userdata\\" +
                               std::to_string(kAccountId) + "\\" + std::to_string(kAppId);
  return p;
}

// Objeto de interfaz {vtable} con kSlots thunks "<interfaz>::#<slot>".
gaddr Interface(const std::string& name) {
  static std::mutex mu;
  static std::map<std::string, gaddr> objs;
  std::lock_guard<std::mutex> l(mu);
  gaddr& o = objs[name];
  if (o) return o;
  gaddr vt = HeapAllocProcess(4 * kSlots, true);
  for (int i = 0; i < kSlots; i++)
    Wr32(vt + 4 * i, ThunkFor(kDll, name + "::#" + std::to_string(i)));
  o = HeapAllocProcess(16, true);
  Wr32(o, vt);
  return o;
}

// Cadena fija en memoria del invitado (propiedad del "cliente").
gaddr GuestString(const std::string& s) {
  static std::mutex mu;
  static std::map<std::string, gaddr> strs;
  std::lock_guard<std::mutex> l(mu);
  gaddr& p = strs[s];
  if (!p) {
    p = HeapAllocProcess(s.size() + 1, true);
    PutStr(p, s);
  }
  return p;
}

std::string EnvOr(const char* name, const char* def) {
  const char* e = Env(name);
  return e && *e ? e : def;
}

// --- propiedad del juego y de sus DLC -------------------------------------

std::set<uint32_t> ParseIds(const std::string& list) {
  std::set<uint32_t> ids;
  std::stringstream ss(list);
  for (std::string item; std::getline(ss, item, ',');)
    if (!item.empty()) ids.insert(uint32_t(std::strtoul(item.c_str(), nullptr, 10)));
  return ids;
}

// Los "dlcappid" del appmanifest_45760.acf (InstalledDepots): lo que el
// cliente de Steam de este usuario tiene instalado. Solo se leen esos campos.
std::set<uint32_t> DlcFromManifest(const std::string& path) {
  std::ifstream in(path);
  if (!in) return {};
  std::set<uint32_t> ids;
  for (std::string line; std::getline(in, line);) {
    const size_t k = line.find("\"dlcappid\"");
    if (k == std::string::npos) continue;
    const size_t a = line.find('"', k + 10), b = a == std::string::npos ? a : line.find('"', a + 1);
    if (b != std::string::npos) ids.insert(uint32_t(std::strtoul(line.c_str() + a + 1, nullptr, 10)));
  }
  ids.insert(0);  // marca "leído", se quita abajo
  return ids;
}

const std::set<uint32_t>& InstalledDlc() {
  static const std::set<uint32_t> ids = [] {
    if (const char* e = Env("STEAM_DLC")) {
      std::set<uint32_t> s = ParseIds(e);
      WarnF("steam: DLC instalados por USF4_STEAM_DLC: %s\n", e);
      return s;
    }
    std::string path = EnvOr("STEAM_MANIFEST", "");
    if (path.empty()) path = g_proc->game_dir + "/../../appmanifest_" + std::to_string(kAppId) + ".acf";
    std::set<uint32_t> s = DlcFromManifest(path);
    if (s.empty())
      FatalF("steam: no se puede saber qué DLC tiene el usuario: no existe %s. "
             "Indica USF4_STEAM_MANIFEST=<appmanifest_45760.acf> o "
             "USF4_STEAM_DLC=<appids separados por coma> (vacío: ninguno)",
             path.c_str());
    s.erase(0);
    std::string list;
    for (uint32_t id : s) list += (list.empty() ? "" : ",") + std::to_string(id);
    WarnF("steam: DLC instalados según %s: %s\n", path.c_str(), list.empty() ? "(ninguno)" : list.c_str());
    return s;
  }();
  return ids;
}

// Cada consulta distinta se anota una vez: W1 la compara con lo que contesta
// el Steam real.
bool Owned(const char* what, uint32_t app, bool base_counts) {
  const bool yes = (base_counts && app == kAppId) || InstalledDlc().count(app);
  static std::mutex mu;
  static std::set<std::pair<std::string, uint32_t>> seen;
  std::lock_guard<std::mutex> l(mu);
  if (seen.insert({what, app}).second)
    WarnF("steam: %s(%u) = %s\n", what, unsigned(app), yes ? "sí" : "no");
  return yes;
}

// --- "nube" local ------------------------------------------------------------

std::mutex g_remote_mu;

// Ruta del anfitrión para un archivo remoto; fatal para nombres que Steam no
// acepta o que saldrían de la carpeta. HIPÓTESIS: Steam guarda los nombres con
// '/' como separador y toma '\' como '/' (el juego usa
// "CAPCOM\SUPERSTREETFIGHTERIV\SSF4_SaveData\0.0", nombres con '\'); aquí
// se normaliza igual en todas las funciones, así un nombre con una barra y el
// mismo con la otra son el mismo archivo. No se distingue mayúsculas (Steam sí
// las ignora; no se ha visto al juego mezclarlas).
std::string RemoteHost(const std::string& raw) {
  std::string name = raw;
  for (char& ch : name)
    if (ch == '\\') ch = '/';
  if (name.empty() || name.size() > 260 || name[0] == '/' || name.find("..") != std::string::npos ||
      name.find(':') != std::string::npos)
    FatalF("steam: nombre de archivo remoto no soportado: \"%s\"", raw.c_str());
  std::string guest = UserDataGuest() + "\\remote\\";
  for (char ch : name) guest += ch == '/' ? '\\' : ch;
  return HostPath(guest, PathAccess::kWrite);
}

std::string RemoteDirHost() { return HostPath(UserDataGuest() + "\\remote", PathAccess::kWrite); }

int64_t HostFileSize(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? int64_t(st.st_size) : -1;
}

int64_t RemoteUsed(const std::string& dir) {
  // Archivos en la carpeta y sus subcarpetas (los nombres con '/').
  int64_t used = 0;
  std::vector<std::string> todo{dir};
  while (!todo.empty()) {
    std::string d = todo.back();
    todo.pop_back();
    if (DIR* h = opendir(d.c_str())) {
      while (dirent* e = readdir(h)) {
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
        const std::string p = d + "/" + e->d_name;
        struct stat st;
        if (stat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) todo.push_back(p);
        else if (S_ISREG(st.st_mode)) used += st.st_size;
      }
      closedir(h);
    }
  }
  return used;
}

// --- callbacks -----------------------------------------------------------------

// CCallbackBase: {vtable, uint8 m_nCallbackFlags, int m_iCallback}. El
// destructor de CCallback del SDK solo da de baja si la bandera de registrado
// está puesta, así que registrar la pone, como el steam_api.dll de Steam.
constexpr gaddr kCallbackFlags = 4, kCallbackId = 8;
constexpr uint8_t kCallbackRegistered = 0x01;

// Callbacks que el juego registró (objeto CCallbackBase) y resultados que el
// "cliente" tiene por entregar. Como en el SDK, nada se entrega fuera de
// SteamAPI_RunCallbacks.
std::mutex g_cb_mu;
std::vector<gaddr> g_registered;  // objetos CCallbackBase
struct Pending {
  int id;
  std::vector<uint8_t> data;
};
std::vector<Pending> g_pending;

void Post(int id, const void* data, size_t n) {
  std::lock_guard<std::mutex> l(g_cb_mu);
  g_pending.push_back({id, std::vector<uint8_t>((const uint8_t*)data, (const uint8_t*)data + n)});
}

// k_iSteamUserStatsCallbacks = 1100: UserStatsReceived_t = 1101.
constexpr int kUserStatsReceived = 1101;
constexpr int32_t kEResultOK = 1, kEResultFail = 2;

// Qué contesta el cliente a RequestCurrentStats (HIPÓTESIS, ver ESTADO.md
// 0.6): USF4_STEAM_STATS=ok|fail|never.
enum class StatsMode { kOk, kFail, kNever };
StatsMode GetStatsMode() {
  static const StatsMode m = [] {
    const std::string v = EnvOr("STEAM_STATS", "ok");
    if (v == "ok") return StatsMode::kOk;
    if (v == "fail") return StatsMode::kFail;
    if (v == "never") return StatsMode::kNever;
    FatalF("steam: USF4_STEAM_STATS=%s no es ok|fail|never", v.c_str());
    return StatsMode::kOk;
  }();
  return m;
}

}  // namespace

// ---- exports planos (cdecl) ---------------------------------------------------

HLE(kDll, SteamAPI_RestartAppIfNecessary, kCdecl) {
  RECOMP_TRACE("  steam: RestartAppIfNecessary(%u): no\n", unsigned(Arg(c, 0)));
  return 0;
}
HLE(kDll, SteamAPI_Init, kCdecl) {
  RECOMP_TRACE("  steam: Init: cliente presente, sin conexión\n");
  return 1;
}
HLE(kDll, SteamAPI_Shutdown, kCdecl) { return 0; }
// Entrega los resultados pendientes a los CCallbackBase registrados con ese id
// (vtable[0] = Run(void*), thiscall). Se saca la cola antes de llamar: un
// callback puede registrar o pedir más.
HLE(kDll, SteamAPI_RunCallbacks, kCdecl) {
  std::vector<Pending> todo;
  std::vector<gaddr> regs;
  {
    std::lock_guard<std::mutex> l(g_cb_mu);
    if (g_pending.empty()) return 0;
    todo.swap(g_pending);
    regs = g_registered;
  }
  for (const Pending& p : todo) {
    bool delivered = false;
    for (gaddr cb : regs) {
      if (!(Rd8(cb + kCallbackFlags) & kCallbackRegistered) || int(Rd32(cb + kCallbackId)) != p.id) continue;
      const gaddr vt = Rd32(cb);
      // GetCallbackSizeBytes (vtable[2]) debe ser el tamaño de lo que se
      // entrega, o el layout del modelo está mal.
      c.ecx = uint32_t(cb);
      const uint32_t size = uint32_t(CallGuest(c, Rd32(vt + 8), {}));
      if (size != p.data.size())
        FatalF("steam: callback %d: el juego espera %u bytes y el modelo entrega %zu", p.id, size, p.data.size());
      const gaddr buf = HeapAllocProcess(p.data.size(), true);
      std::memcpy(GPtr(buf), p.data.data(), p.data.size());
      c.ecx = uint32_t(cb);
      CallGuest(c, Rd32(vt), {buf});
      delivered = true;
    }
    RECOMP_TRACE("  steam: callback %d (%zu bytes) %s\n", p.id, p.data.size(),
                 delivered ? "entregado" : "sin quien lo registre");
  }
  return 0;
}

HLE(kDll, SteamAPI_RegisterCallback, kCdecl) {
  const gaddr cb = Arg(c, 0);
  Wr8(cb + kCallbackFlags, Rd8(cb + kCallbackFlags) | kCallbackRegistered);
  Wr32(cb + kCallbackId, uint32_t(Arg(c, 1)));
  {
    std::lock_guard<std::mutex> l(g_cb_mu);
    g_registered.push_back(cb);
  }
  RECOMP_TRACE("  steam: RegisterCallback(%08X, %u)\n", unsigned(cb), unsigned(Arg(c, 1)));
  return 0;
}
HLE(kDll, SteamAPI_UnregisterCallback, kCdecl) {
  const gaddr cb = Arg(c, 0);
  Wr8(cb + kCallbackFlags, Rd8(cb + kCallbackFlags) & ~kCallbackRegistered);
  {
    std::lock_guard<std::mutex> l(g_cb_mu);
    g_registered.erase(std::remove(g_registered.begin(), g_registered.end(), cb), g_registered.end());
  }
  return 0;
}
// (CCallbackBase*, SteamAPICall_t): este cliente nunca entrega un handle, así
// que solo puede llegar k_uAPICallInvalid (0), que Steam ignora igual.
HLE(kDll, SteamAPI_RegisterCallResult, kCdecl) {
  const uint64_t h = Arg(c, 1) | uint64_t(Arg(c, 2)) << 32;
  if (h) FatalF("steam: RegisterCallResult(%llX): este cliente sin conexión nunca dio ese handle",
                (unsigned long long)h);
  return 0;
}
HLE(kDll, SteamAPI_UnregisterCallResult, kCdecl) {
  const uint64_t h = Arg(c, 1) | uint64_t(Arg(c, 2)) << 32;
  if (h) FatalF("steam: UnregisterCallResult(%llX): este cliente sin conexión nunca dio ese handle",
                (unsigned long long)h);
  return 0;
}

#define USF4_STEAM_ACCESSOR(fn, iface) \
  HLE(kDll, fn, kCdecl) { return Interface(#iface); }
USF4_STEAM_ACCESSOR(SteamClient, ISteamClient012)
USF4_STEAM_ACCESSOR(SteamUser, ISteamUser017)
USF4_STEAM_ACCESSOR(SteamFriends, ISteamFriends014)
USF4_STEAM_ACCESSOR(SteamUtils, ISteamUtils006)
USF4_STEAM_ACCESSOR(SteamUserStats, ISteamUserStats011)
USF4_STEAM_ACCESSOR(SteamApps, ISteamApps006)
USF4_STEAM_ACCESSOR(SteamNetworking, ISteamNetworking005)
USF4_STEAM_ACCESSOR(SteamMatchmaking, ISteamMatchmaking009)
USF4_STEAM_ACCESSOR(SteamRemoteStorage, ISteamRemoteStorage012)
#undef USF4_STEAM_ACCESSOR

// ---- ISteamClient012 ------------------------------------------------------------

// [21] SetWarningMessageHook(fn): nunca hay avisos.
HLE_NAMED(kDll, "ISteamClient012::#21", ClientSetWarningMessageHook, 1) { return 0; }

// ---- ISteamNetworking005 ----------------------------------------------------------

// [1] IsP2PPacketAvailable(uint32* pcubMsgSize, int nChannel) -> bool: sin
// conexión no hay sesiones P2P, así que nunca hay un paquete. W1 midió 8,364
// llamadas y todas dieron false (AL = 0). *pcubMsgSize no se toca.
HLE_NAMED(kDll, "ISteamNetworking005::#1", NetworkingIsP2PPacketAvailable, 2) { return 0; }

// ---- ISteamUser017 --------------------------------------------------------------

// [1] BLoggedOn(): sin conexión a los servidores, como Steam en modo offline.
HLE_NAMED(kDll, "ISteamUser017::#1", UserBLoggedOn, 0) { return 0; }
// [2] GetSteamID() -> CSteamID por puntero oculto.
HLE_NAMED(kDll, "ISteamUser017::#2", UserGetSteamID, 1) {
  const gaddr out = Arg(c, 0);
  Wr32(out, uint32_t(kSteamId));
  Wr32(out + 4, uint32_t(kSteamId >> 32));
  return out;
}
// [6] GetUserDataFolder(char* buf, int cub) -> bool: userdata\<cuenta>\<app>\local.
HLE_NAMED(kDll, "ISteamUser017::#6", UserGetUserDataFolder, 2) {
  const gaddr buf = Arg(c, 0);
  const int32_t cub = int32_t(Arg(c, 1));
  const std::string path = UserDataGuest() + "\\local";
  if (cub <= 0 || size_t(cub) < path.size() + 1) return 0;
  PutStr(buf, path);
  MakeDirs(HostPath(path, PathAccess::kWrite));
  return 1;
}

// ---- ISteamFriends014 -------------------------------------------------------------

static const std::string& PersonaName() {
  static const std::string n = EnvOr("STEAM_NAME", "Player");
  return n;
}
// [0] GetPersonaName() -> const char*: el nombre guardado del perfil.
HLE_NAMED(kDll, "ISteamFriends014::#0", FriendsGetPersonaName, 0) { return GuestString(PersonaName()); }
// [7] GetFriendPersonaName(CSteamID): el propio, o "[unknown]" como contesta
// Steam para quien no conoce (sin conexión no conoce a nadie más).
HLE_NAMED(kDll, "ISteamFriends014::#7", FriendsGetFriendPersonaName, 2) {
  const uint64_t id = Arg(c, 0) | uint64_t(Arg(c, 1)) << 32;
  return GuestString(id == kSteamId ? PersonaName() : "[unknown]");
}
// [22] ActivateGameOverlay(dialog), [23] ...ToUser(dialog, CSteamID),
// [25] ...ToStore(AppId_t, flag): no hay overlay; Steam sin overlay no hace nada.
HLE_NAMED(kDll, "ISteamFriends014::#22", FriendsActivateGameOverlay, 1) {
  RECOMP_TRACE("  steam: ActivateGameOverlay(%s): sin overlay\n", GStr(gaddr(Arg(c, 0))).c_str());
  return 0;
}
HLE_NAMED(kDll, "ISteamFriends014::#23", FriendsActivateGameOverlayToUser, 3) { return 0; }
HLE_NAMED(kDll, "ISteamFriends014::#25", FriendsActivateGameOverlayToStore, 2) { return 0; }
// [37] SetRichPresence(key, value) -> bool: el cliente lo guarda local.
HLE_NAMED(kDll, "ISteamFriends014::#37", FriendsSetRichPresence, 2) {
  static std::mutex mu;
  static std::map<std::string, std::string> presence;
  const gaddr k = Arg(c, 0), v = Arg(c, 1);
  if (!k) return 0;
  std::lock_guard<std::mutex> l(mu);
  if (v) presence[GStr(k)] = GStr(v);
  else presence.erase(GStr(k));
  return 1;
}

// ---- ISteamUserStats011 -----------------------------------------------------------

// [0] RequestCurrentStats() -> bool. HIPÓTESIS: Steam sin sesión (modo
// offline) acepta la petición y contesta con UserStatsReceived_t desde su
// caché local; como no hay caché de este usuario el resultado es OK y vacío.
// Layout (Steamworks, pack 8): {uint64 m_nGameID; int32 m_eResult; CSteamID
// m_steamIDUser} = 24 bytes (RunCallbacks lo compara con el
// GetCallbackSizeBytes del juego). USF4_STEAM_STATS=fail entrega
// k_EResultFail; never contesta false y no entrega nada (lo de la ronda B1).
// En W1 (en línea) contestó true una sola vez.
HLE_NAMED(kDll, "ISteamUserStats011::#0", UserStatsRequestCurrentStats, 0) {
  const StatsMode m = GetStatsMode();
  if (m == StatsMode::kNever) return 0;
  uint8_t d[24] = {};
  const uint64_t gid = kAppId;  // CGameID de una app: el AppID en los 24 bits bajos
  const int32_t res = m == StatsMode::kOk ? kEResultOK : kEResultFail;
  std::memcpy(d, &gid, 8);
  std::memcpy(d + 8, &res, 4);
  std::memcpy(d + 16, &kSteamId, 8);
  Post(kUserStatsReceived, d, sizeof d);
  RECOMP_TRACE("  steam: RequestCurrentStats: true; UserStatsReceived(%s) en el próximo RunCallbacks\n",
               m == StatsMode::kOk ? "OK" : "Fail");
  return 1;
}

// [6] GetAchievement(name, bool* pbAchieved) -> bool. HIPÓTESIS: el esquema de
// logros de la app lo tiene el cliente de Steam (W1: los 67 nombres que el juego
// pregunta dieron true), así que todo nombre que el juego pregunta existe, y
// un usuario sin guardados en este equipo no ha desbloqueado ninguno.
// Cada nombre distinto se anota una vez; más de 256 distintos = fatal (el juego
// tiene 67: sería otro uso).
HLE_NAMED(kDll, "ISteamUserStats011::#6", UserStatsGetAchievement, 2) {
  const gaddr name = Arg(c, 0), out = Arg(c, 1);
  if (!name || !out) return 0;
  static std::mutex mu;
  static std::set<std::string> seen;
  const std::string n = GStr(name);
  {
    std::lock_guard<std::mutex> l(mu);
    if (seen.insert(n).second) {
      RECOMP_TRACE("  steam: GetAchievement(%s) = true, no desbloqueado (%zu nombres)\n", n.c_str(), seen.size());
      if (seen.size() > 256) FatalF("steam: más de 256 logros distintos preguntados");
    }
  }
  Wr8(out, 0);
  return 1;
}
// [12] GetAchievementDisplayAttribute(name, key) -> const char*. Las cadenas
// "name" y "desc" localizadas viven en el esquema de Steam, que aquí no
// existe: HIPÓTESIS, cadena vacía (como Steam cuando no tiene el valor);
// "hidden" = "0". Solo cambia el texto de los avisos de logros.
HLE_NAMED(kDll, "ISteamUserStats011::#12", UserStatsGetAchievementDisplayAttribute, 2) {
  const std::string key = Arg(c, 1) ? GStr(gaddr(Arg(c, 1))) : "";
  if (key == "name" || key == "desc") return GuestString("");
  if (key == "hidden") return GuestString("0");
  FatalF("steam: GetAchievementDisplayAttribute(%s): clave no modelada", key.c_str());
  return 0;
}

// ---- ISteamUtils006 ---------------------------------------------------------------

// [4] GetIPCountry() -> const char*: HIPÓTESIS, sin conexión Steam no tiene
// cómo saber el país por IP y contesta cadena vacía (el exe se lo pasa a su
// tabla de regiones, 0x508660, al llegar al menú). USF4_STEAM_COUNTRY="MX"
// lo cambia por el código de dos letras que se quiera probar. W1 (en línea)
// no guardó el valor.
HLE_NAMED(kDll, "ISteamUtils006::#4", UtilsGetIPCountry, 0) {
  const std::string cc = EnvOr("STEAM_COUNTRY", "");
  if (!cc.empty() && cc.size() != 2) FatalF("steam: USF4_STEAM_COUNTRY debe ser de dos letras o vacío");
  return GuestString(cc);
}
// [9] GetAppID().
HLE_NAMED(kDll, "ISteamUtils006::#9", UtilsGetAppID, 0) { return kAppId; }
// [10] SetOverlayNotificationPosition(pos): no hay overlay.
HLE_NAMED(kDll, "ISteamUtils006::#10", UtilsSetOverlayNotificationPosition, 1) { return 0; }
// [20] ShowGamepadTextInput(mode, line, desc, max) -> bool: el teclado del
// overlay (Big Picture) no existe; el juego usa el suyo.
HLE_NAMED(kDll, "ISteamUtils006::#20", UtilsShowGamepadTextInput, 4) { return 0; }

// ---- ISteamApps006 ----------------------------------------------------------------

// [4] GetCurrentGameLanguage() -> const char*: USF4_STEAM_LANGUAGE o "english".
HLE_NAMED(kDll, "ISteamApps006::#4", AppsGetCurrentGameLanguage, 0) {
  return GuestString(EnvOr("STEAM_LANGUAGE", "english"));
}
// [6] BIsSubscribedApp(AppId_t), [7] BIsDlcInstalled(AppId_t).
HLE_NAMED(kDll, "ISteamApps006::#6", AppsBIsSubscribedApp, 1) {
  return Owned("BIsSubscribedApp", uint32_t(Arg(c, 0)), true) ? 1 : 0;
}
HLE_NAMED(kDll, "ISteamApps006::#7", AppsBIsDlcInstalled, 1) {
  return Owned("BIsDlcInstalled", uint32_t(Arg(c, 0)), false) ? 1 : 0;
}

// ---- ISteamRemoteStorage012 (la "nube" es la carpeta local) ------------------------

// [0] FileWrite(name, data, cub) -> bool.
HLE_NAMED(kDll, "ISteamRemoteStorage012::#0", RemoteFileWrite, 3) {
  const std::string name = GStr(gaddr(Arg(c, 0)));
  const gaddr data = Arg(c, 1);
  const int32_t cub = int32_t(Arg(c, 2));
  if (cub < 0 || (cub && !EnsureReadable(c, data, uint32_t(cub)))) return 0;
  std::lock_guard<std::mutex> l(g_remote_mu);
  const std::string host = RemoteHost(name);
  const std::string dir = host.substr(0, host.find_last_of('/'));
  MakeDirs(dir);
  const int64_t old = HostFileSize(host);
  if (RemoteUsed(RemoteDirHost()) - (old > 0 ? old : 0) + cub > kQuotaBytes) return 0;
  const std::string tmp = host + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return 0;
  const bool ok = (cub == 0 || std::fwrite(GPtr(data), 1, size_t(cub), f) == size_t(cub));
  if (std::fclose(f) != 0 || !ok || std::rename(tmp.c_str(), host.c_str()) != 0) {
    std::remove(tmp.c_str());
    return 0;
  }
  RECOMP_TRACE("  steam: FileWrite(%s, %d)\n", name.c_str(), cub);
  return 1;
}
// [1] FileRead(name, data, cubToRead) -> int32 leídos (0 si no existe).
HLE_NAMED(kDll, "ISteamRemoteStorage012::#1", RemoteFileRead, 3) {
  const std::string name = GStr(gaddr(Arg(c, 0)));
  const gaddr data = Arg(c, 1);
  const int32_t cub = int32_t(Arg(c, 2));
  std::lock_guard<std::mutex> l(g_remote_mu);
  FILE* f = std::fopen(RemoteHost(name).c_str(), "rb");
  if (!f || cub <= 0) {
    if (f) std::fclose(f);
    return 0;
  }
  std::vector<uint8_t> buf(static_cast<size_t>(cub));
  const size_t n = std::fread(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  if (n) std::memcpy(GPtr(data), buf.data(), n);
  return uint32_t(n);
}
// [3] FileDelete(name) -> bool.
HLE_NAMED(kDll, "ISteamRemoteStorage012::#3", RemoteFileDelete, 1) {
  std::lock_guard<std::mutex> l(g_remote_mu);
  return std::remove(RemoteHost(GStr(gaddr(Arg(c, 0)))).c_str()) == 0 ? 1 : 0;
}
// [10] FileExists(name) -> bool.
HLE_NAMED(kDll, "ISteamRemoteStorage012::#10", RemoteFileExists, 1) {
  std::lock_guard<std::mutex> l(g_remote_mu);
  return HostFileSize(RemoteHost(GStr(gaddr(Arg(c, 0))))) >= 0 ? 1 : 0;
}
// [12] GetFileSize(name) -> int32 (0 si no existe).
HLE_NAMED(kDll, "ISteamRemoteStorage012::#12", RemoteGetFileSize, 1) {
  std::lock_guard<std::mutex> l(g_remote_mu);
  const int64_t n = HostFileSize(RemoteHost(GStr(gaddr(Arg(c, 0)))));
  return n > 0 ? uint32_t(n) : 0;
}
// [15] GetFileCount() -> int32: archivos de la "nube" local (los de las
// subcarpetas cuentan con su nombre con '/'; los .tmp de una escritura en
// curso no). W1 midió 2 llamadas.
HLE_NAMED(kDll, "ISteamRemoteStorage012::#15", RemoteGetFileCount, 0) {
  std::lock_guard<std::mutex> l(g_remote_mu);
  uint32_t n = 0;
  std::vector<std::string> todo{RemoteDirHost()};
  while (!todo.empty()) {
    const std::string d = todo.back();
    todo.pop_back();
    DIR* h = opendir(d.c_str());
    if (!h) continue;
    while (dirent* e = readdir(h)) {
      if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
      const std::string p = d + "/" + e->d_name;
      struct stat st;
      if (stat(p.c_str(), &st) != 0) continue;
      if (S_ISDIR(st.st_mode)) todo.push_back(p);
      else if (S_ISREG(st.st_mode) && p.compare(p.size() > 4 ? p.size() - 4 : 0, 4, ".tmp") != 0) n++;
    }
    closedir(h);
  }
  return n;
}
// [17] GetQuota(int32* total, int32* available) -> bool.
HLE_NAMED(kDll, "ISteamRemoteStorage012::#17", RemoteGetQuota, 2) {
  std::lock_guard<std::mutex> l(g_remote_mu);
  const int64_t used = RemoteUsed(RemoteDirHost());
  Wr32(Arg(c, 0), uint32_t(kQuotaBytes));
  Wr32(Arg(c, 1), uint32_t(used >= kQuotaBytes ? 0 : kQuotaBytes - used));
  return 1;
}
// [18] IsCloudEnabledForAccount(), [19] IsCloudEnabledForApp(): sí; la nube
// de este cliente es la carpeta local y el juego guarda por aquí.
HLE_NAMED(kDll, "ISteamRemoteStorage012::#18", RemoteIsCloudEnabledForAccount, 0) { return 1; }
HLE_NAMED(kDll, "ISteamRemoteStorage012::#19", RemoteIsCloudEnabledForApp, 0) { return 1; }

}  // namespace recomp::hle
