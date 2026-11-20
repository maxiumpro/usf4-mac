// curllib.dll de USF4 (libcurl con OpenSSL y LDAP) SIN RED.
//
// Solo la usa el cliente HTTP que sube repeticiones a un servicio que ya no
// existe, y W1 midió 0 llamadas a curl_* en arranque, menús y una pelea de
// entrenamiento (ni curl_global_init). Por eso no se lifta la DLL (arrastraría
// OpenSSL, LDAP y 34 funciones de msvcr90 sin HLE): el exe importa 9 funciones
// y las 9 están aquí.
//
// Fail-closed: ninguna contesta. Un "éxito" falso (curl_easy_init devolviendo
// un manejador que no hace nada, curl_easy_perform devolviendo CURLE_OK sin
// haber mandado nada) escondería un camino que el juego sí usó; un "fallo"
// inventado (CURLE_COULDNT_CONNECT) sería una respuesta de red que Windows
// no dio. Si alguna se llama, la corrida se detiene con su nombre.
//
// Convención: todas cdecl (curl_easy_setopt es de argumentos variables).

#include <cstdint>

#include "runtime/hle/hle.h"
#include "runtime/log.h"

namespace recomp::hle {
namespace {

constexpr const char* kDll = "curllib.dll";

HLE(kDll, curl_global_init, kCdecl) {
  FatalF("curllib.dll!curl_global_init(%X): sin red; W1 midió 0 llamadas a curl", uint32_t(Arg(c, 0)));
}
HLE(kDll, curl_global_cleanup, kCdecl) { FatalF("curllib.dll!curl_global_cleanup: sin red"); }
HLE(kDll, curl_easy_init, kCdecl) { FatalF("curllib.dll!curl_easy_init: sin red"); }
HLE(kDll, curl_easy_reset, kCdecl) {
  FatalF("curllib.dll!curl_easy_reset(%X): sin red", uint32_t(Arg(c, 0)));
}
HLE(kDll, curl_easy_cleanup, kCdecl) {
  FatalF("curllib.dll!curl_easy_cleanup(%X): sin red", uint32_t(Arg(c, 0)));
}
HLE(kDll, curl_easy_setopt, kCdecl) {
  FatalF("curllib.dll!curl_easy_setopt(%X, %u): sin red", uint32_t(Arg(c, 0)),
         uint32_t(Arg(c, 1)));
}
HLE(kDll, curl_easy_perform, kCdecl) {
  FatalF("curllib.dll!curl_easy_perform(%X): sin red", uint32_t(Arg(c, 0)));
}
HLE(kDll, curl_easy_strerror, kCdecl) {
  FatalF("curllib.dll!curl_easy_strerror(%u): sin red", uint32_t(Arg(c, 0)));
}
HLE(kDll, curl_slist_append, kCdecl) {
  FatalF("curllib.dll!curl_slist_append(%X, %X): sin red", uint32_t(Arg(c, 0)),
         uint32_t(Arg(c, 1)));
}

}  // namespace
}  // namespace recomp::hle
