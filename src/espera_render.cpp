// espera_render.cpp: la espera del hilo principal al de render, sin girar.
//
// 006AA5B0 (SSFIV.exe 834219) es `while (*(int*)0xAAC9E8) Sleep(0);`: espera a
// que otro hilo baje con InterlockedExchange (0x9311E8) la bandera que subió al
// mandar trabajo (6AA722 la pone en 1, 6AA8E1 en 0). Sleep(0) es un yield: en la
// pelea gira 57,000-103,000 veces por segundo (USF4_PERF, 19-nov) y ocupa un
// núcleo entero solo en esperar. En Windows hace lo mismo; aquí se espera igual
// (misma condición, misma bandera) pero cediendo unas vueltas y luego durmiendo
// de 50 en 50 microsegundos, una fracción mínima de un frame de 16.6 ms.
//
// No cambia qué se calcula, solo cómo se espera: la pelea del oráculo no pasa
// por aquí (hilo de render apagado), y aun así se verifica IGUALES contra A1.
// USF4_ESPERA_ORIGINAL=1 vuelve al cuerpo liftado (para medir antes/después).
// Se sustituye con `replace 6aa5b0` (manifest/usf4_optim.inc).

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "runtime/ctx.h"
#include "runtime/platform.h"
#include "runtime/runtime.h"

namespace recomp::gen {
void f_006AA5B0_lifted(::recomp::Ctx& c);
}  // namespace recomp::gen

namespace recomp::gen {

void f_006AA5B0(::recomp::Ctx& c) {
  static const bool original = [] {
    const char* e = std::getenv("USF4_ESPERA_ORIGINAL");
    return e && *e == '1';
  }();
  if (original) return f_006AA5B0_lifted(c);
  constexpr uint32_t kBandera = 0xAAC9E8;
  auto* bandera = reinterpret_cast<std::atomic<uint32_t>*>(::recomp::GPtr(kBandera));
  for (int vueltas = 0; bandera->load(std::memory_order_acquire) != 0; vueltas++) {
    if (vueltas < 64)
      std::this_thread::yield();
    else
      ::recomp::platform::SleepUntilNs(::recomp::platform::MonotonicNs() + 50'000);
  }
  c.esp += 4;  // ret
}

}  // namespace recomp::gen

// Los otros dos giros con Sleep(0) que quedan en la pelea (USF4_PERF en WSL,
// 19-nov), por la dirección de regreso de la llamada:
//   0x770EAC  D3D::LimitFrame (0x770DE0): `while (objetivo > transcurrido)
//             { Sleep(0); QPC; }`, el resto del frame (~900,000/s);
//   0x6AAFB7  el otro bucle sobre la bandera 0xAAC9E8 (~72,000/s una vez que
//             006AA5B0 ya no gira).
// En los dos el juego vuelve a revisar su condición tras cada Sleep: aquí ese
// Sleep(0) duerme de verdad un rato corto (50 us en el limitador y en
// la bandera; 200 us se pasaba del frame) en vez de ceder y volver al instante.
// Leer el tiempo que falta del estado del limitador se probó y casi nunca
// aplicaba (unidades del juego no confirmadas): esto no depende de ellas.
namespace usf4 {
namespace {

bool EsperaLimitador(::recomp::Ctx&, uint32_t caller, uint32_t ms) {
  static const bool original = [] {
    const char* e = std::getenv("USF4_ESPERA_ORIGINAL");
    return e && *e == '1';
  }();
  if (original || ms != 0) return false;
  uint64_t ns;
  if (caller == 0x770EAC) ns = 50'000;  // 200 us pasaba el frame: 16.80 ms en vez de 16.67 (Air, 19-nov)
  else if (caller == 0x6AAFB7) ns = 50'000;
  else return false;
  ::recomp::platform::SleepUntilNs(::recomp::platform::MonotonicNs() + ns);
  return true;
}

const bool g_registrado = [] {
  ::recomp::hle::SetSleepHook(&EsperaLimitador);
  return true;
}();

}  // namespace
}  // namespace usf4
