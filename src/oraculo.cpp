// Oraculo nativo de B1 (USF4): el mismo volcado por frame que `oracle-training`
// de Ember, sobre el binario liftado. Apagado salvo USF4_ORACLE_DUMP=<csv>.
//
// Que hace, y de donde sale cada cosa (SF4 Ember Netplay, MIT, rama `oraculo`):
//   - Volcado: una fila tras cada actualizacion nativa de BattleUpdate
//     (sf4e__Oracle.cxx, AfterNativeUpdate), con las mismas columnas, la misma
//     lectura del invitado (los getters del propio juego, sf4e__Game__Battle__
//     System__StateHash.cxx: CaptureCharaSemantics / ComputeSemanticHashes) y el
//     mismo hash (oraculo_volcado.h).
//   - Guion: el `Full()` de sf4e__SelfTest__OracleTraining.cxx (oraculo_guion.h),
//     metido en la misma capa que Ember: lo que devuelven Pad::System::
//     GetButtons_MappedOn/RawOn (sf4e__Pad.cxx, ReadButtons) mientras corre la
//     actualizacion, con el espejo por frame de Session::Prepare, el arranque tras 240
//     actualizaciones con la pelea en marcha (TrainingRuntime.cxx, OracleScriptStart),
//     el control de P2 en PC_PAD (FollowDummy) y los medidores de Training
//     en 7 = infinito (OPT_SC_GAUGE y OPT_REVENGE_GAUGE).
//   - Paridad con Ember (lo que Ember hace al juego y afecta al estado):
//       * JobManager::Start con 0 hilos de trabajo (sf4e__Game__Battle.cxx, fJobManager::Start).
//       * Hilo de render apagado (sf4e__Platform.cxx, ApplyStartupParams:
//         StartupRenderingThread(params) = 0).
//       * Entrada a Training sin la pregunta de Fight Request y con Ryu contra Ryu
//         (sf4e__GameEvents.cxx: GetItemObserverState, TrainingMode::Choose/Enter/
//         ReplaceCharaSelect; sf4e__Event.cxx: QueueEvent).
//   Todo con `replace <va>` del manifest (manifest/usf4_oraculo.inc): ninguna
//   edicion del codigo generado.
//
// Lo que NO se replica (y por que no cambia el estado): Ember no escribe nada mas
// en el juego en esta ruta: PrepareBattleRequest/RegisterTasks pasan a la original
// (la peticion de Training no se pide), sus ganchos de HUD, sonido, camara, GGPO
// y pausa no tocan el estado de simulacion. Detalle en ESTADO.md.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "estado_juego.h"
#include "oraculo_guion.h"
#include "oraculo_volcado.h"
#include "runtime/ctx.h"
#include "runtime/hle/kernel32_state.h"
#include "runtime/log.h"
#include "runtime/runtime.h"

namespace recomp::gen {
// Los cuerpos liftados de las funciones sustituidas (los emite el lifter con `replace`).
void f_005D6B10_lifted(::recomp::Ctx& c);  // Battle::System::BattleUpdate
void f_005171B0_lifted(::recomp::Ctx& c);  // Pad::System::GetButtons_MappedOn
void f_00517130_lifted(::recomp::Ctx& c);  // Pad::System::GetButtons_RawOn
void f_00538110_lifted(::recomp::Ctx& c);  // Battle::JobManager::Start
void f_006AB3B0_lifted(::recomp::Ctx& c);  // Event::EventController::QueueEvent
void f_00614CB0_lifted(::recomp::Ctx& c);  // GameEvents::MainMenu::OnModeSelected (item observer)
void f_00780380_lifted(::recomp::Ctx& c);  // Platform::Main::ApplyStartupParams
}  // namespace recomp::gen

namespace usf4 {
namespace {
using recomp::gaddr;
namespace orc = ::usf4::oraculo;

// Direcciones del juego (SSFIV.exe 834219), de analisis/ember_names.tsv.
constexpr uint32_t kGetCharaUnit = 0x563510, kGetActorByIndex = 0x565D10;
constexpr uint32_t kGetGameMode = 0x5D97C0, kIsLeavingBattle = 0x5D6A70, kIsFight = 0x5D9F60;
constexpr uint32_t kGetUnitTimeScale = 0x5DA060, kTrainingManagerSingleton = 0x5E28B0;
// Chara::Actor
constexpr uint32_t kStatus = 0x542520, kSide = 0x541D20, kRootPos = 0x52D660;
constexpr uint32_t kVitAmt = 0x541EE0, kVitMax = 0x541F00, kRevAmt = 0x542180, kRevMax = 0x5421A0;
constexpr uint32_t kRecAmt = 0x541F80, kRecMax = 0x541FA0, kSuperAmt = 0x542020, kSuperMax = 0x542040;
constexpr uint32_t kScAmt = 0x5420C0, kScMax = 0x5420E0, kUcAmt = 0x542220, kUcMax = 0x542240;
constexpr uint32_t kComboDmg = 0x542410, kDamage = 0x5423D0, kActionId = 0x52D700, kActionFrame = 0x52D720;
constexpr uint32_t kPosture = 0x541EA0, kActorId = 0x56C6A0;
constexpr uint32_t kIdCody = 28;
// Variables estaticas de Battle::System (el hash de flujo)
constexpr gaddr kCurFlow = 0xA64994, kPrevFlow = 0xA64998, kCurSub = 0xA6499C, kPrevSub = 0xA649A0;
constexpr gaddr kPrevSubFrame = 0xAA925C, kPrevFlowFrame = 0xAA9260, kCurFlowFrame = 0xAA9264, kCurSubFrame = 0xAA9268;
constexpr uint32_t kFlowIdle = 16;
// Battle::System: campos
constexpr uint32_t kOffFrames = 0xDD4, kOffReady = 0xDF8, kOffRandom = 0x50, kOffPlayers = 0x9C;
constexpr uint32_t kGameModeTraining = 3, kReadyFight = 5, kControlPad = 0;
// Training::Manager: opciones desde +8; medidores 6 y 7 (7 = infinito)
constexpr uint32_t kOptScGauge = 6, kOptRevengeGauge = 7, kGaugeInfinite = 7;
// TrainingMode: eleccion de cada lado, 0x14 bytes desde +0x60
constexpr uint32_t kSelBase = 0x60, kSelSize = 0x14, kEditionUsf4 = 14;
constexpr int kFields = 26;

struct State {
  bool enabled = false, inited = false;
  std::string dump_path, mutate;
  orc::Script script;
  orc::Dump dump;
  bool dump_failed = false, done = false;
  // La sesion del laboratorio (Session de Ember), lo justo para el guion.
  bool available = false;
  bool ready = false;
  orc::Facing face;
  int ready_updates = 0;
  bool started = false, finished = false, playing = false;
  int cursor = 0, frame = -1;
  unsigned out[2] = {0, 0}, in[2] = {0, 0};
  bool overriding = false, sampling = false, commit = false, following = false;
  int before_frame = 0;
  gaddr scratch = 0, str_versus = 0;
  bool chara_sent = false;
  std::string fp_line;
} g;

void Init() {
  if (g.inited) return;
  g.inited = true;
  const char* d = recomp::Env("ORACLE_DUMP");
  if (!d || !*d) return;
  g.enabled = true;
  g.dump_path = d;
  if (const char* m = recomp::Env("ORACLE_MUTATE")) g.mutate = m;
  g.script = orc::Full();
  if (!g.mutate.empty()) {
    const int frame = std::atoi(g.mutate.c_str());
    if (!orc::Mutate(g.script, frame))
      recomp::FatalF("oraculo: USF4_ORACLE_MUTATE=%s no es una pulsacion de un frame de P1", g.mutate.c_str());
    recomp::WarnF("oraculo: control negativo: botones de P1 fuera del frame %d del guion\n", frame);
  }
  g.scratch = recomp::HeapAllocProcess(64, true);
  g.str_versus = recomp::HeapAllocProcess(32, true);
  recomp::PutStr(g.str_versus, "VersusFromChr");
  recomp::WarnF("oraculo: volcado en %s, guion de %d frames\n", g.dump_path.c_str(), g.script.frames);
}

// Llamadas al invitado (thiscall: ecx = this; los argumentos por la pila).
uint32_t Call(recomp::Ctx& c, uint32_t va, uint32_t self, std::initializer_list<uint64_t> args = {}) {
  c.ecx = self;
  return uint32_t(recomp::CallGuest(c, va, args));
}
// Getter que escribe un FixedPoint (4 bytes) en el argumento: devuelve la palabra cruda.
uint32_t Fixed(recomp::Ctx& c, uint32_t va, uint32_t self) {
  recomp::Wr32(g.scratch, 0);
  Call(c, va, self, {g.scratch});
  return recomp::Rd32(g.scratch);
}
int16_t Integral(uint32_t word) { return int16_t(word >> 16); }
uint32_t FixedAt(gaddr va) { return recomp::Rd32(va); }

uint32_t EngineWord(uint32_t system) { return recomp::Rd32(system + kOffFrames); }

struct Words {
  uint32_t v[kFields] = {};
};

// CaptureCharaSemantics (sf4e__Game__Battle__System__StateHash.cxx), en el mismo orden.
Words Capture(recomp::Ctx& c, uint32_t system, int side) {
  const uint32_t unit = Call(c, kGetCharaUnit, system);
  const uint32_t a = Call(c, kGetActorByIndex, unit, {uint64_t(side)});
  if (!a) recomp::FatalF("oraculo: sin actor para el lado %d", side);
  if (Call(c, kActorId, a) == kIdCody)
    recomp::FatalF("oraculo: Cody: el cuchillo (CodyKnife) no esta portado al volcado");
  Words w;
  uint32_t* v = w.v;
  *v++ = Call(c, kStatus, a);
  *v++ = Call(c, kSide, a);
  const uint32_t root = Call(c, kRootPos, a);
  for (int i = 0; i < 4; i++) *v++ = recomp::Rd32(root + 4u * i);
  *v++ = Fixed(c, kVitAmt, a);
  *v++ = Fixed(c, kVitMax, a);
  *v++ = Fixed(c, kRevAmt, a);
  *v++ = Fixed(c, kRevMax, a);
  *v++ = Fixed(c, kRecAmt, a);
  *v++ = Fixed(c, kRecMax, a);
  *v++ = Fixed(c, kSuperAmt, a);
  *v++ = Fixed(c, kSuperMax, a);
  *v++ = Fixed(c, kScAmt, a);
  *v++ = Fixed(c, kScMax, a);
  *v++ = Fixed(c, kUcAmt, a);
  *v++ = Fixed(c, kUcMax, a);
  *v++ = Fixed(c, kComboDmg, a);
  *v++ = Fixed(c, kDamage, a);
  *v++ = Call(c, kActionId, a);
  *v++ = Fixed(c, kActionFrame, a);
  *v++ = Call(c, kPosture, a);
  recomp::Wr32(g.scratch, 0);
  Call(c, kGetUnitTimeScale, system, {g.scratch, uint64_t(side)});
  *v++ = recomp::Rd32(g.scratch);
  *v++ = 0;  // knifeState: cero para quien no es Cody (el caso Cody es fatal arriba)
  *v++ = 0;  // knifeX
  return w;
}

struct Hashes {
  uint64_t overall, flow, chara[2];
};

// ComputeSemanticHashes (misma archivo de Ember).
Hashes Compute(uint32_t system, const Words chara[2]) {
  Hashes h{};
  const uint32_t frames = EngineWord(system);
  orc::Hasher flow;
  flow.Fixed(uint16_t(frames & 0xFFFF), Integral(frames));
  flow.U32(recomp::Rd32(kCurFlow));
  flow.U32(recomp::Rd32(kPrevFlow));
  flow.U32(recomp::Rd32(kCurSub));
  flow.U32(recomp::Rd32(kPrevSub));
  for (gaddr a : {kCurFlowFrame, kCurSubFrame, kPrevFlowFrame, kPrevSubFrame}) {
    const uint32_t w = FixedAt(a);
    flow.Fixed(uint16_t(w & 0xFFFF), Integral(w));
  }
  h.flow = flow.Value();
  for (int i = 0; i < 2; i++) {
    orc::Hasher ch;
    for (uint32_t word : chara[i].v) ch.U32(word);
    h.chara[i] = ch.Value();
  }
  orc::Hasher overall;
  overall.U64(h.flow);
  overall.U64(h.chara[0]);
  overall.U64(h.chara[1]);
  h.overall = overall.Value();
  return h;
}

std::string FpLine(const recomp::Ctx& c) {
  const unsigned cw = c.fpu.cw;
  const unsigned pc = (cw >> 8) & 3, rc = (cw >> 10) & 3;
  char t[96];
  std::snprintf(t, sizeof t, "x87 pc=%s rc=%s mxcsr=0x%04x", pc == 0 ? "24" : pc == 2 ? "53" : pc == 3 ? "64" : "?",
                rc == 0 ? "near" : rc == 1 ? "down" : rc == 2 ? "up" : "chop", unsigned(c.mxcsr));
  return t;
}

gaddr TrainingOptions(recomp::Ctx& c) {
  const uint32_t mgr = Call(c, kTrainingManagerSingleton, 0);
  if (!mgr) recomp::FatalF("oraculo: el Training::Manager no existe (los medidores infinitos no se pueden poner)");
  return mgr + 8;
}

// --- Antes de la actualizacion: TrainingRuntime.cxx, BeforeUpdate -------------------
void Before(recomp::Ctx& c, uint32_t system) {
  g.overriding = g.sampling = g.commit = g.following = false;
  const bool lab = Call(c, kGetGameMode, system) == kGameModeTraining && !(Call(c, kIsLeavingBattle, system) & 0xFF);
  if (!lab) {
    g.available = false;  // CloseBattle: la sesion se reinicia
    return;
  }
  if (!g.available) {  // Session::Enter + Reset
    g.available = true;
    g.face = orc::Facing{};
    g.playing = false;
    g.cursor = 0;
  }
  g.ready = (Call(c, kIsFight, system) & 0xFF) && int(recomp::Rd32(system + kOffReady)) == int(kReadyFight);
  // OracleScriptStart: tras kStartAfter actualizaciones seguidas con la pelea en marcha.
  if (!g.started) {
    if (!g.ready || g.playing) {
      g.ready_updates = 0;
    } else if (++g.ready_updates >= orc::kStartAfter) {
      const gaddr opt = TrainingOptions(c);
      recomp::Wr32(opt + 4 * kOptScGauge, kGaugeInfinite);
      recomp::Wr32(opt + 4 * kOptRevengeGauge, kGaugeInfinite);
      g.started = true;
      g.playing = true;
      g.cursor = 0;
      recomp::WarnF("oraculo: el guion de %d frames arranca en el engine frame %d (%d actualizaciones listas), medidores infinitos\n",
                    g.script.frames, int(Integral(EngineWord(system))), g.ready_updates);
    }
  }
  g.following = true;
  g.before_frame = Integral(EngineWord(system));
  bool script_p2 = false;
  if (g.ready) {
    g.sampling = true;
    g.commit = true;
    g.overriding = g.playing;  // PlanInput: override = el modo no es Idle
    // Session::Prepare: P1 por la reproduccion, con el espejo del frame.
    if (g.playing && g.cursor < g.script.frames)
      g.out[0] = orc::ForFacing(g.script.p1.f[size_t(g.cursor)], g.face.Right(0));
    // OracleScriptApply: P2 en el mismo cursor.
    g.frame = -1;
    if (g.started && !g.finished) {
      if (g.playing && g.cursor >= 0 && g.cursor < g.script.frames) {
        g.frame = g.cursor;
        g.out[1] = orc::ForFacing(g.script.p2.f[size_t(g.frame)], g.face.Right(1));
        g.in[0] = g.out[0];
        g.in[1] = g.out[1];
        script_p2 = true;
      } else {
        g.finished = true;
        recomp::WarnF("oraculo: el guion llego a su fin\n");
      }
    }
  }
  // FollowDummy: el control de P2 pasa a PC_PAD mientras el guion le da entrada.
  if (script_p2) {
    const uint32_t players = recomp::Rd32(system + kOffPlayers);
    if (players) recomp::Wr32(players + 1 * 0x2B8 + 0x1C, kControlPad);
  }
}

// --- Despues de la actualizacion: TrainingRuntime.cxx AfterUpdate + sf4e__Oracle.cxx --
void After(recomp::Ctx& c, uint32_t system) {
  g.overriding = false;
  if (g.following) {
    const int after = Integral(EngineWord(system));
    const uint16_t delta = uint16_t(after - g.before_frame);
    if (g.sampling && delta == 1) {
      // Session::Commit: un frame mas del guion si la actualizacion simulo exactamente uno.
      if (g.commit && g.playing) {
        if (++g.cursor == g.script.frames) g.playing = false;  // Stop
      }
      // FollowPositions: hacia donde miran, por las x de raiz de los dos.
      float x[2] = {0, 0};
      const uint32_t unit = Call(c, kGetCharaUnit, system);
      for (int side = 0; side < 2 && unit; side++) {
        const uint32_t a = Call(c, kGetActorByIndex, unit, {uint64_t(side)});
        const uint32_t root = a ? Call(c, kRootPos, a) : 0;
        if (root) {
          const uint32_t w = recomp::Rd32(root);
          std::memcpy(&x[side], &w, 4);
        }
      }
      g.face.SetPositions(x[0], x[1]);
    }
  }
  g.sampling = g.following = false;
  // sf4e::oracle::AfterNativeUpdate
  if (!g.enabled || g.dump_failed || g.done) return;
  if (recomp::Rd32(kCurFlow) == kFlowIdle) return;
  if (!g.started || g.frame < 0) return;
  const uint32_t unit = Call(c, kGetCharaUnit, system);
  if (!unit || !recomp::Rd32(unit + 8) || !recomp::Rd32(unit + 12)) return;
  if (!g.dump.IsOpen()) {
    std::vector<std::string> names = {"status", "side", "rootX", "rootY", "rootZ", "rootW", "vitality", "vitalityMax",
                                      "revenge", "revengeMax", "recoverable", "recoverableMax", "super", "superMax",
                                      "scTime", "scTimeMax", "ucTime", "ucTimeMax", "comboDamage", "damage",
                                      "action", "actionFrame", "posture", "timeScale", "knifeState", "knifeX"};
    const std::vector<std::string> comments = {
        "usf4-recomp oracle dump v1 (SF4 Ember Netplay patch; Ember is MIT, see LICENSE and ATTRIBUTION.md)",
        "hash: FNV-1a/64 of Ember's semantic state (common/sf4e__StateHash.hxx); overall = hash(flow, p1, p2)",
        "rng is the EmRandom seed at System+0x54: extra column, NOT inside overall/p1_hash/p2_hash",
        "fp: " + FpLine(c),
        "replay: ",
        "extra: off",
        "script: training frames=" + std::to_string(g.script.frames) + " mutate=" + g.mutate,
        "writer: usf4-recomp B1 native port (src/oraculo.cpp), not the original game",
    };
    if (!g.dump.Open(g.dump_path, comments, names)) {
      g.dump_failed = true;
      recomp::FatalF("oraculo: no se puede escribir '%s'", g.dump_path.c_str());
    }
    recomp::WarnF("oraculo: volcando frames a '%s' (%s)\n", g.dump_path.c_str(), FpLine(c).c_str());
  }
  const Words chara[2] = {Capture(c, system, 0), Capture(c, system, 1)};
  const Hashes h = Compute(system, chara);
  orc::Row row;
  row.round = 0;
  row.cursor = uint32_t(g.frame + 1);
  row.scriptFrame = g.frame;
  row.input[0] = g.in[0];
  row.input[1] = g.in[1];
  row.engineFrame = Integral(EngineWord(system));
  row.flow = recomp::Rd32(kCurFlow);
  row.substate = recomp::Rd32(kCurSub);
  row.overall = h.overall;
  row.flowHash = h.flow;
  row.chara[0] = h.chara[0];
  row.chara[1] = h.chara[1];
  row.rng = recomp::Rd32(system + kOffRandom + 4);
  for (int side = 0; side < 2; side++) row.words[side].assign(chara[side].v, chara[side].v + kFields);
  if (!g.dump.Append(row)) return;
  if (g.frame + 1 >= g.script.frames) {
    g.done = true;
    recomp::WarnF("oraculo: %llu filas escritas, el guion tiene %d frames; fin\n", (unsigned long long)g.dump.Rows(),
                  g.script.frames);
    g.dump.Close();
    if (!recomp::Env("ORACLE_KEEP_RUNNING")) std::_Exit(g.dump.Rows() == uint64_t(g.script.frames) ? 0 : 3);
  }
}
}  // namespace
}  // namespace usf4

namespace recomp::gen {
using usf4::g;

// Battle::System::BattleUpdate (thiscall, this en ecx): Ember = fSystem::BattleUpdate, rama nativa.
void f_005D6B10(::recomp::Ctx& c) {
  usf4::Init();
  if (!g.enabled) return f_005D6B10_lifted(c);
  const uint32_t system = c.ecx;
  {
    const ::recomp::Ctx saved = c;
    usf4::Before(c, system);
    c = saved;
  }
  f_005D6B10_lifted(c);
  {
    const ::recomp::Ctx saved = c;
    usf4::After(c, system);
    c = saved;
  }
}

// Pad::System::GetButtons_MappedOn / RawOn(int side), thiscall ret 4. Ember: fSystem::ReadButtons.
static bool Override(::recomp::Ctx& c) {
  if (!g.overriding) return false;
  const uint32_t side = ::recomp::Rd32(c.esp + 4);
  if (side > 1) return false;
  c.eax = g.out[side];
  c.esp += 8;  // ret 4
  return true;
}
void f_005171B0(::recomp::Ctx& c) {
  if (!Override(c)) f_005171B0_lifted(c);
}
void f_00517130(::recomp::Ctx& c) {
  if (!Override(c)) f_00517130_lifted(c);
}

// Battle::JobManager::Start(workers, jobs, jobSize), thiscall ret 0xC. Ember: fJobManager::Start.
void f_00538110(::recomp::Ctx& c) {
  usf4::Init();
  if (g.enabled) {
    static bool said = false;
    if (!said) {
      said = true;
      ::recomp::WarnF("oraculo: trabajos de la pelea en el hilo del juego (el motor pidio %u hilos)\n", ::recomp::Rd32(c.esp + 4));
    }
    ::recomp::Wr32(c.esp + 4, 0);
  }
  f_00538110_lifted(c);
}

// Platform::Main::ApplyStartupParams(params), thiscall ret 4. Ember: fMain::ApplyStartupParams.
void f_00780380(::recomp::Ctx& c) {
  usf4::Init();
  if (g.enabled) {
    const uint32_t params = ::recomp::Rd32(c.esp + 4);
    ::recomp::WarnF("oraculo: hilo de render apagado (el juego pedia %u)\n", ::recomp::Rd32(params + 0x70));
    ::recomp::Wr32(params + 0x70, 0);
  }
  f_00780380_lifted(c);
}

// GameEvents::MainMenu item observer OnModeSelected(mode), thiscall ret 4: Training sin la
// pregunta de Fight Request. Ember: fMainMenu::GetItemObserverState.
void f_00614CB0(::recomp::Ctx& c) {
  usf4::Init();
  constexpr uint32_t kModeTraining = 4, kObserverOffset = 0x40, kFightRequestOffered = 0x860;
  if (g.enabled && ::recomp::Rd32(c.esp + 4) == kModeTraining) {
    const uint32_t offered = c.ecx - kObserverOffset + kFightRequestOffered;
    const uint32_t was = ::recomp::Rd32(offered);
    ::recomp::Wr32(offered, 0);
    f_00614CB0_lifted(c);
    ::recomp::Wr32(offered, was);
    ::recomp::WarnF("oraculo: Training sin la pregunta de Fight Request (estaba %s)\n", was ? "ofrecida" : "no ofrecida");
    return;
  }
  f_00614CB0_lifted(c);
}

// EventController::QueueEvent(name, a1, a2, a3), thiscall ret 0x10. Ember: fEventController::QueueEvent
// + fTrainingMode::ReplaceCharaSelect: el CharaSelect de Training pasa a VersusFromChr con Ryu y Ryu.
void f_006AB3B0(::recomp::Ctx& c) {
  usf4::Init();
  if (g.enabled && !g.chara_sent) {
    const uint32_t name = ::recomp::Rd32(c.esp + 4);
    if (name && std::strncmp(reinterpret_cast<const char*>(::recomp::GPtr(name)), "CharaSelect", 12) == 0) {
      if (const uint32_t mode = usf4::FindForegroundChild("TrainingMode")) {
        const char* e1 = ::recomp::Env("ORACLE_P1");
        const char* e2 = ::recomp::Env("ORACLE_P2");
        const uint32_t ids[2] = {e1 ? uint32_t(std::atoi(e1)) : 0u, e2 ? uint32_t(std::atoi(e2)) : 0u};
        for (int side = 0; side < 2; side++) {
          const uint32_t sel = mode + usf4::kSelBase + uint32_t(side) * usf4::kSelSize;
          for (uint32_t i = 0; i < 9; i++) ::recomp::Wr8(sel + i, 0);  // ConfirmedCharaConditions: todo 0
          ::recomp::Wr8(sel + 0, uint8_t(ids[side]));                   // charaID
          ::recomp::Wr8(sel + 8, uint8_t(usf4::kEditionUsf4));          // unc_edition = ED_USF4
          ::recomp::Wr32(sel + 12, ids[side]);                          // cursorCharaID
          ::recomp::Wr32(sel + 16, 1);                                  // chosen = TRUE
        }
        ::recomp::Wr32(c.esp + 4, uint32_t(g.str_versus));
        g.chara_sent = true;
        ::recomp::WarnF("oraculo: CharaSelect de Training -> VersusFromChr, luchadores %u y %u (edicion %u)\n", ids[0], ids[1],
                        usf4::kEditionUsf4);
      }
    }
  }
  f_006AB3B0_lifted(c);
}
}  // namespace recomp::gen
