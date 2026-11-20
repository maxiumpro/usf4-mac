// Sondas de estado de USF4 (solo lectura de la memoria del invitado).
//
// Direcciones: las mismas que usa el oráculo (analisis/ember_names.tsv):
//   0xA64994 CurrentBattleFlow, 0xA6499C CurrentBattleFlowSubstate,
//   0xA85694 Dimps::App::eventSystemCurrent, 0xA856A0 eventSystemBuilt.
// Un evento (EventBase) lleva su nombre en +0xC; EventBaseWithEC guarda su
// controlador en +0x44; un EventController tiene el índice del primer plano en
// +0x1AC y ranuras de 0x18 bytes desde +0x28 (ranura+0 = evento, +4 = ocupada,
// +0x10 = estado). Todo desensamblado de SSFIV.exe (0x4246B0, 0x6AA3C0/420).

#include "estado_juego.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <SDL3/SDL.h>

#include <vector>

#include "runtime/gfx/device.h"
#include "runtime/hle/hle.h"
#include "runtime/hle/present.h"
#include "runtime/hle/xinput.h"
#include "runtime/log.h"
#include "runtime/runtime.h"

namespace usf4 {
namespace {
using recomp::gaddr;

bool Ok(gaddr a, uint32_t n = 4) {
  return a >= 0x10000 && recomp::g_proc->mem.IsCommitted(a) && recomp::g_proc->mem.IsCommitted(a + n - 1);
}
uint32_t R(gaddr a) { return Ok(a) ? recomp::Rd32(a) : 0xDEADBEEFu; }

constexpr gaddr kFlow = 0xA64994, kSubstate = 0xA6499C, kEvCur = 0xA85694, kEvBuilt = 0xA856A0;

std::string Name(gaddr ev) {
  std::string s;
  for (int i = 0; i < 48 && Ok(ev + 0xC + i, 1); i++) {
    char ch = char(recomp::Rd8(ev + 0xC + i));
    if (!ch) break;
    s += (ch >= 32 && ch < 127) ? ch : '?';
  }
  return s;
}

// Ruta de eventos en primer plano desde el controlador raíz: "A(6)/B(6)/...".
// El controlador se reconoce porque su vtable[0xA8/4] es GetForegroundEvent
// (0x6AA420); un evento sin controlador de hijos no lo cumple.
std::string EventPath(gaddr ctl) {
  std::string path;
  for (int depth = 0; depth < 8 && Ok(ctl, 0x1B0); depth++) {
    const uint32_t vt = R(ctl);
    if (R(vt + 0xA8) != 0x6AA420u) break;
    const int32_t idx = int32_t(R(ctl + 0x1AC));
    if (idx < 0 || idx > 64) break;
    const gaddr slot = ctl + 0x28 + uint32_t(idx) * 0x18;
    const uint32_t ev = R(slot), occupied = R(slot + 4), status = R(slot + 0x10);
    if (!occupied || !ev) break;
    char buf[32];
    std::snprintf(buf, sizeof buf, "(%u)", status);
    path += (path.empty() ? "" : "/") + Name(ev) + buf;
    ctl = R(ev + 0x44);
  }
  return path;
}

// Evento en primer plano (estado 6) del controlador ctl: ev y su estado; 0 si no hay.
uint32_t ForegroundOf(gaddr ctl, uint32_t* status) {
  if (!Ok(ctl, 0x1B0)) return 0;
  if (R(R(ctl) + 0xA8) != 0x6AA420u) return 0;
  const int32_t idx = int32_t(R(ctl + 0x1AC));
  if (idx < 0 || idx > 64) return 0;
  const gaddr slot = ctl + 0x28 + uint32_t(idx) * 0x18;
  if (!R(slot + 4) || !R(slot)) return 0;
  if (status) *status = R(slot + 0x10);
  return R(slot);
}

// Entrada programada (USF4_PAD_SCRIPT), por el pad virtual del HLE de XInput
// (USF4_TEST_PAD=1), igual que lo vería el juego con un mando real:
//   "<cuándo>:<botón>[:<frames>],..."  cuándo = frame (número), +N (N frames
// después de que se disparó la entrada anterior) o @Evento (la hoja de la ruta
// de eventos en primer plano, p. ej. @Title: se pulsa una vez, cuando lleva 40
// frames ahí). botón = nombre de botón SDL (start, a,
// dpdown...). Se mantiene <frames> frames (por defecto 6) y se suelta.
struct PadStep {
  uint64_t frame = 0;      // si when es número
  bool relative = false;   // si when es +N: N frames tras disparar la entrada anterior
  std::string leaf;        // si when es @Evento
  int button = 0;
  uint64_t hold = 6;
  bool fired = false;
  uint64_t release_at = 0;  // frame en que se suelta (0 = no pulsado)
};

std::vector<PadStep>& PadScript() {
  static std::vector<PadStep> steps = [] {
    std::vector<PadStep> v;
    const char* e = recomp::Env("PAD_SCRIPT");
    if (!e || !*e) return v;
    if (!recomp::Env("TEST_PAD"))
      recomp::FatalF("USF4_PAD_SCRIPT necesita USF4_TEST_PAD=1 (el pad virtual del HLE de XInput)");
    std::string all = e;
    while (!all.empty()) {
      const size_t comma = all.find(',');
      std::string item = all.substr(0, comma);
      all = comma == std::string::npos ? "" : all.substr(comma + 1);
      std::vector<std::string> f;
      for (size_t a = 0;;) {
        const size_t b = item.find(':', a);
        f.push_back(item.substr(a, b == std::string::npos ? b : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
      }
      if (f.size() < 2 || f[0].empty()) recomp::FatalF("USF4_PAD_SCRIPT: entrada mal formada: \"%s\"", item.c_str());
      PadStep st;
      if (f[0][0] == '@') st.leaf = f[0].substr(1);
      else if (f[0][0] == '+') {
        st.relative = true;
        st.frame = std::strtoull(f[0].c_str() + 1, nullptr, 10);
      } else st.frame = std::strtoull(f[0].c_str(), nullptr, 10);
      const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(f[1].c_str());
      if (b == SDL_GAMEPAD_BUTTON_INVALID) recomp::FatalF("USF4_PAD_SCRIPT: botón desconocido \"%s\"", f[1].c_str());
      st.button = int(b);
      if (f.size() > 2) st.hold = std::strtoull(f[2].c_str(), nullptr, 10);
      v.push_back(st);
    }
    return v;
  }();
  return steps;
}

// Hoja de la ruta ("Game(6)/Title(6)" -> "Title") si está en primer plano (6).
std::string Leaf(const std::string& path) {
  const size_t slash = path.rfind('/');
  const std::string last = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t par = last.find('(');
  if (par == std::string::npos || last.compare(par, 3, "(6)") != 0) return "";
  return last.substr(0, par);
}

void RunPadScript(uint64_t frame, const std::string& path) {
  auto& steps = PadScript();
  if (steps.empty()) return;
  static std::string cur_leaf;
  static uint64_t leaf_since = 0;
  const std::string leaf = Leaf(path);
  if (leaf != cur_leaf) {
    cur_leaf = leaf;
    leaf_since = frame;
  }
  static uint64_t prev_fired = 0;
  // 19-nov (Air): con carga rápida, @Title llega antes de que la pantalla acepte
  // botones y el Start se pierde (el guion se quedaba en el título). Si el último
  // paso @Evento ya pulsó, la pantalla sigue siendo la misma 120 frames después y
  // el paso siguiente no se ha disparado, se vuelve a pulsar.
  for (size_t i = 0; i < steps.size(); ++i) {
    PadStep& st = steps[i];
    const bool next_pending = i + 1 < steps.size() && !steps[i + 1].fired;
    if (st.fired && !st.leaf.empty() && next_pending && leaf == st.leaf && !st.release_at &&
        frame >= prev_fired + 120 && prev_fired >= leaf_since) {
      prev_fired = frame;
      st.release_at = frame + st.hold;
      recomp::hle::TestPadButton(st.button, true);
      recomp::WarnF("entrada: frame %llu: pad %s otra vez (@%s sigue en pantalla)\n", (unsigned long long)frame,
                    SDL_GetGamepadStringForButton(SDL_GamepadButton(st.button)), st.leaf.c_str());
    }
  }
  bool prior_done = true;  // las entradas van en orden: una espera a la anterior
  for (PadStep& st : steps) {
    if (st.release_at && frame >= st.release_at) {
      recomp::hle::TestPadButton(st.button, false);
      st.release_at = 0;
    }
    const bool was_done = prior_done;
    prior_done = st.fired;
    if (st.fired || !was_done) continue;
    const bool go = !st.leaf.empty() ? (leaf == st.leaf && frame - leaf_since >= 40)
                    : st.relative ? (prev_fired && frame >= prev_fired + st.frame)
                                  : frame >= st.frame;
    if (!go) continue;
    st.fired = true;
    prev_fired = frame;
    st.release_at = frame + st.hold;
    recomp::hle::TestPadButton(st.button, true);
    recomp::WarnF("entrada: frame %llu: pad %s %llu frames (%s)\n", (unsigned long long)frame,
                  SDL_GetGamepadStringForButton(SDL_GamepadButton(st.button)), (unsigned long long)st.hold,
                  st.leaf.empty() ? "por frame" : ("@" + st.leaf).c_str());
  }
}
}  // namespace

uint32_t FindForegroundChild(const char* name) {
  // FindForegroundEvent(GetRootEvent(), {name}, 1) del juego: el evento raiz es
  // el primero en primer plano del sistema de eventos; el hijo, el de su controlador.
  if (!(R(kEvBuilt) & 1)) return 0;
  const uint32_t root = ForegroundOf(R(kEvCur), nullptr);
  if (!root) return 0;
  uint32_t status = 0;
  const uint32_t child = ForegroundOf(R(root + 0x44), &status);
  return child && status == 6 && Name(child) == name ? child : 0;
}

// --- Registro de la presentación real (B2) ------------------------------------
// Una línea "presentacion:" al primer Present y otra cada vez que cambia algo
// de lo que sigue: lo que de verdad se usó, no lo que se pidió (correr-mac.sh
// la lee para su veredicto; PLAN-B2.md §5b). Solo lee el estado de SDL y del
// backend: no toca memoria del invitado ni nada de la simulación.
//
// Driver: el dispositivo de SDL_GPU es privado del toolkit (no se llega a él
// para SDL_GetGPUDeviceDriver sin cambiar el toolkit), así que se lee qué
// driver reclamó la ventana: cada backend de SDL_GPU deja su propiedad en
// ella (SDL_gpu_metal.m / SDL_gpu_vulkan.c / SDL_gpu_d3d12.c). La línea
// "gfx: SDL_GPU (<driver>)" del runtime sí sale de SDL_GetGPUDeviceDriver.
static const char* DriverDeVentana(SDL_Window* w) {
  if (recomp::hle::GfxBackend() == recomp::gfx::Backend::Metal) return "metal nativo (sin SDL_GPU)";
  if (recomp::hle::GfxBackend() == recomp::gfx::Backend::D3D9) return "d3d9 (dxvk-native)";
  const SDL_PropertiesID p = SDL_GetWindowProperties(w);
  if (SDL_HasProperty(p, "SDL_GPUMetalWindowPropertyData")) return "metal";
  if (SDL_HasProperty(p, "SDL_GPUVulkanWindowPropertyData")) return "vulkan";
  if (SDL_HasProperty(p, "SDL_GPUD3D12WindowPropertyData")) return "direct3d12";
  return "SIN DATO (la ventana no tiene la propiedad de ningún driver conocido)";
}

// La ventana de SDL de la presentación: la que reclamó SDL_GPU; si no hay
// ninguna así, la primera.
static SDL_Window* VentanaDelJuego() {
  int n = 0;
  SDL_Window** ws = SDL_GetWindows(&n);
  SDL_Window* found = nullptr;
  for (int i = 0; ws && i < n && !found; i++) {
    const SDL_PropertiesID p = SDL_GetWindowProperties(ws[i]);
    if (SDL_HasProperty(p, "SDL_GPUMetalWindowPropertyData") ||
        SDL_HasProperty(p, "SDL_GPUVulkanWindowPropertyData") ||
        SDL_HasProperty(p, "SDL_GPUD3D12WindowPropertyData"))
      found = ws[i];
  }
  if (!found && ws && n > 0) found = ws[0];
  SDL_free(ws);
  return found;
}

static void RegistrarPresentacion(uint64_t frame) {
  const recomp::gfx::Presentation pr = recomp::gfx::LastPresentation();
  SDL_Window* w = VentanaDelJuego();
  int pt_w = 0, pt_h = 0, px_w = 0, px_h = 0;
  float escala = 0;
  SDL_WindowFlags flags = 0;
  const char* driver = "SIN DATO (sin ventana)";
  std::string modo = "SIN DATO";
  if (w) {
    SDL_GetWindowSize(w, &pt_w, &pt_h);
    SDL_GetWindowSizeInPixels(w, &px_w, &px_h);
    escala = SDL_GetWindowPixelDensity(w);
    flags = SDL_GetWindowFlags(w);
    driver = DriverDeVentana(w);
    if (flags & SDL_WINDOW_FULLSCREEN) {
      const SDL_DisplayMode* m = SDL_GetWindowFullscreenMode(w);
      char buf[64];
      if (m) std::snprintf(buf, sizeof buf, "pantalla completa (exclusiva %dx%d)", m->w, m->h);
      else std::snprintf(buf, sizeof buf, "pantalla completa (escritorio)");
      modo = buf;
    } else {
      modo = "ventana";
    }
    if (flags & SDL_WINDOW_HIDDEN) modo += ", oculta";
  }
  char linea[512];
  std::snprintf(linea, sizeof linea,
                "driver %s | back buffer %dx%d -> drawable %dx%d | rect %d,%d-%d,%d | ventana %dx%d pt = "
                "%dx%d px | escala %.2f | modo %s",
                driver, pr.src_w, pr.src_h, pr.out_w, pr.out_h, pr.rect.left, pr.rect.top, pr.rect.right,
                pr.rect.bottom, pt_w, pt_h, px_w, px_h, double(escala), modo.c_str());
  static std::string ultima;
  if (ultima == linea) return;
  ultima = linea;
  recomp::WarnF("presentacion: frame %llu | %s\n", (unsigned long long)frame, linea);
}

void OnPresent(uint64_t frame) {
  RegistrarPresentacion(frame);
  static const uint64_t every = [] {
    const char* e = recomp::Env("STATE_EVERY");
    return e ? std::strtoull(e, nullptr, 10) : 0ull;
  }();
  const bool script = !PadScript().empty();
  if (!every && !script) return;
  const uint32_t flow = R(kFlow), sub = R(kSubstate);
  const uint32_t built = R(kEvBuilt), cur = R(kEvCur);
  static std::string last_path;
  static uint32_t last_flow = ~0u, last_sub = ~0u;
  const std::string path = built & 1 ? EventPath(cur) : "(sin sistema de eventos)";
  RunPadScript(frame, path);
  const bool changed = flow != last_flow || sub != last_sub || path != last_path;
  last_flow = flow;
  last_sub = sub;
  last_path = path;
  if (!every || (!changed && frame % every)) return;
  recomp::WarnF("estado: frame %llu flow=%u sub=%d eventos=%s%s\n", (unsigned long long)frame, flow,
                int(sub), path.c_str(), changed ? " (cambio)" : "");
}

}  // namespace usf4
