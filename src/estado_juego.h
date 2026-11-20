#pragma once
// Sondas del estado del juego (USF4): dónde está el flujo, qué evento tiene
// en primer plano. Solo lee memoria del invitado; nada se escribe aquí.
#include <cstdint>

namespace usf4 {
// GameConfig::on_present: se llama tras cada Present, en el hilo principal.
void OnPresent(uint64_t frame);
// Evento (direccion del invitado) en primer plano bajo el evento raiz si se llama ; 0 si no.
uint32_t FindForegroundChild(const char* name);
}  // namespace usf4
