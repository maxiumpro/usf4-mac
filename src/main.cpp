// usf4: Ultra Street Fighter IV (PC, Steam build 834219), nativo.
//
//   usf4 [--game-dir DIR] [-- ARGUMENTOS DEL JUEGO...]
//
// DIR es la carpeta de instalación (la que tiene SSFIV.exe, cryptopp.dll y
// curllib.dll). Por defecto $USF4_GAME_DIR. USF4_TRACE=1 anota cada llamada
// HLE. Ver ESTADO.md para las variables de Steam (USF4_STEAM_*).
//
// B1: ver ESTADO.md para hasta dónde llega la corrida sin pantalla.

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "estado_juego.h"
#include "runtime/log.h"
#include "runtime/runtime.h"

int main(int argc, char** argv) {
  std::string game_dir, game_args;
  if (const char* e = std::getenv("USF4_GAME_DIR")) game_dir = e;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--game-dir") && i + 1 < argc) {
      game_dir = argv[++i];
    } else if (!std::strcmp(argv[i], "--")) {
      for (i++; i < argc; i++) {
        if (!game_args.empty()) game_args += ' ';
        const bool quote = std::strpbrk(argv[i], " \t") != nullptr;
        if (quote) game_args += '"';
        game_args += argv[i];
        if (quote) game_args += '"';
      }
    } else {
      std::fprintf(stderr, "uso: %s [--game-dir DIR] [-- ARGUMENTOS...]\n", argv[0]);
      return 2;
    }
  }
  if (game_dir.empty()) {
    std::fprintf(stderr, "usf4: pasa --game-dir o define USF4_GAME_DIR\n");
    return 2;
  }
  while (game_dir.size() > 1 && game_dir.back() == '/') game_dir.pop_back();
  if (const char* t = std::getenv("USF4_TRACE")) recomp::g_trace = *t && *t != '0';

  const std::string exe = game_dir + "/SSFIV.exe";
  struct stat st;
  if (stat(exe.c_str(), &st) != 0) {
    std::fprintf(stderr, "usf4: no existe %s\n", exe.c_str());
    return 2;
  }

  recomp::GameConfig game;
  game.id = "usf4";
  // La ruta de una instalación de Steam en Windows; el juego no la necesita
  // fija, pero así sus rutas absolutas se ven como en la PC.
  game.guest_dir = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\"
                   "Super Street Fighter IV - Arcade Edition";
  game.exe_name = "SSFIV.exe";
  // DLL que el exe importa y que se liftea con él (tools/lift.sh) en vez de
  // reimplementarse: cryptopp (verificación RSA/SHA1 de los datos), en su
  // base preferida (0x42900000). curllib.dll ya no se lifta: W1 midió 0
  // llamadas y su HLE "sin red" (src/curl_offline.cpp) detiene la corrida si
  // alguna ocurre.
  game.lifted_dlls = {{"cryptopp.dll", 0, ""}};
  // Lo que el juego carga en tiempo de ejecución es lo que W1 vio (imm32,
  // kernel32, Wmvcore, nvapi.dll). Cualquier otro nombre, de módulo o de
  // función, detiene la corrida; nvapi.dll no existe en la PC donde se midió
  // (LoadLibrary falla con ERROR_MOD_NOT_FOUND y el juego sigue).
  game.fatal_unknown_dynamic_loads = true;
  // Los 8 ps_1_4 que el juego crea al cargar y que W1 no vio usar en un Draw (la
  // traducción de SM 1.4 es de B2): crear funciona, dibujar con uno es fatal.
  game.defer_unsupported_shaders = true;
  game.missing_dlls = {"nvapi.dll"};
  game.on_present = usf4::OnPresent;
  // cryptopp (módulo liftado, no medido por W1) pregunta por un export de su propio
  // build en kernel32: Windows contesta NULL.
  game.missing_procs = {"GetNewAndDeleteForCryptoPP", "SetNewAndDeleteFromCryptoPP"};
  // D3DXAssembleShader: tabla SHA-256 -> bytecode hecha con el D3DX real a
  // partir de lo que el juego ensambla (tools/tabla_d3dx.sh; derivada del
  // juego, nunca en git). Sin ella la primera llamada es fatal con el hash.
  if (const char* t = std::getenv("USF4_SHADER_ASM_DIR")) game.shader_asm_dir = t;
  // Saltos indirectos del exe que W1 no vio ejecutar (tools/lift.sh deja la
  // lista, un VA en hex por línea): si uno se toma, la corrida se detiene con
  // el sitio y el destino en vez de despachar a ciegas.
  if (const char* t = std::getenv("USF4_BRANCH_TRAPS")) {
    std::ifstream in(t);
    if (!in) {
      std::fprintf(stderr, "usf4: no se puede leer USF4_BRANCH_TRAPS=%s\n", t);
      return 2;
    }
    std::string line;
    while (std::getline(in, line))
      if (!line.empty()) game.fatal_branch_sites.push_back(uint32_t(std::stoul(line, nullptr, 16)));
  }
  return recomp::RunProcess(game, exe, game_dir, game_args);
}
