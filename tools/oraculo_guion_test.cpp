// Prueba del guion del oraculo contra el volcado del original (sin el juego):
//   oraculo_guion_test A1.csv
// Para cada fila (script_frame i) el guion de src/oraculo_guion.h, con el
// espejo que da la posicion de la fila anterior, debe dar el in_p1 / in_p2 que
// el original dejo en el volcado. Tambien comprueba que engine_frame -
// script_frame sea constante. Compilar con tools/oraculo_guion_test.sh.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../src/oraculo_guion.h"

using namespace usf4::oraculo;

static std::vector<std::string> Split(const std::string& s) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string t;
  while (std::getline(ss, t, ',')) out.push_back(t);
  return out;
}
static float F(const std::string& hex) {
  const uint32_t v = uint32_t(std::strtoul(hex.c_str(), nullptr, 16));
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  std::map<std::string, int> col;
  Script s = Full();
  Facing face;
  int bad = 0, rows = 0, engine_delta = -1;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const auto f = Split(line);
    if (col.empty()) {
      for (size_t i = 0; i < f.size(); i++) col[f[i]] = int(i);
      continue;
    }
    const int sf = std::atoi(f[col["script_frame"]].c_str());
    if (sf < 0) continue;
    const unsigned want1 = unsigned(std::strtoul(f[col["in_p1"]].c_str(), nullptr, 16));
    const unsigned want2 = unsigned(std::strtoul(f[col["in_p2"]].c_str(), nullptr, 16));
    const unsigned got1 = ForFacing(s.p1.f[size_t(sf)], face.Right(0));
    const unsigned got2 = ForFacing(s.p2.f[size_t(sf)], face.Right(1));
    if (got1 != want1 || got2 != want2) {
      if (bad++ < 10)
        std::printf("frame %d: in_p1 %08x (original %08x)  in_p2 %08x (original %08x)\n", sf, got1, want1, got2, want2);
    }
    const int ef = std::atoi(f[col["engine_frame"]].c_str());
    if (engine_delta < 0) engine_delta = ef - sf;
    else if (ef - sf != engine_delta) {
      if (bad++ < 10) std::printf("frame %d: engine_frame - script_frame = %d, antes %d\n", sf, ef - sf, engine_delta);
    }
    // El espejo del frame siguiente sale de las x de raiz de esta fila.
    face.SetPositions(F(f[col["p1_rootX"]]), F(f[col["p2_rootX"]]));
    rows++;
  }
  std::printf("filas %d, diferencias %d, engine_frame - script_frame = %d\n", rows, bad, engine_delta);
  // Control negativo: el frame 374 es una pulsacion de P1 y al mutarlo sin botones.
  Script m = Full();
  const bool ok = Mutate(m, 374) && m.p1.f[374] == 0 && !Mutate(m, 375);
  std::printf("mutar 374 ok: %s\n", ok ? "si" : "NO");
  return bad == 0 && rows == 3000 && ok ? 0 : 1;
}
