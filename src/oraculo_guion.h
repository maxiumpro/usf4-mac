#pragma once
// Guion del oraculo `oracle-training` (fase W0/B1): las entradas de los dos
// lados, frame a frame, tal como las arma el oraculo. Solo biblioteca estandar
// (se prueba en el anfitrion con tools/oraculo_guion_test.cpp contra A1.csv).
//
// Origen: SF4 Ember Netplay (MIT, https://github.com/Confetti3/SF4-Ember-Netplay),
// rama local `oraculo` del arbol de W0, copiado y adaptado:
//   src/sf4e/sf4e__SelfTest__OracleTraining.cxx  (Pad, Full: el guion de 3000 frames)
//   src/training/MoveInputs.hxx                  (DirectionBits)
//   src/training/MoveNotation.hxx                (bits de los botones)
//   src/training/RecordingTypes.hxx              (Mirror, ForFacing)
//   src/training/TrainingSession.hxx             (SetPositions, FacingRight)
// Aviso de copyright de Ember: ver su LICENSE y ATTRIBUTION.md (no se tocaron).
//
// Convenciones (las de Ember): las direcciones son digitos de teclado numerico
// para un luchador que mira a la derecha; el espejo (Left<->Right) se aplica
// por frame segun hacia donde mira cada lado. En el guion `mapped == raw`.

#include <cstdint>
#include <vector>

namespace usf4 {
namespace oraculo {

constexpr unsigned kUp = 1, kDown = 2, kLeft = 4, kRight = 8;
constexpr unsigned kLP = 0x10, kMP = 0x20, kHP = 0x400, kLK = 0x40, kMK = 0x80, kHK = 0x800;
constexpr unsigned kPunches = kLP | kMP | kHP, kKicks = kLK | kMK | kHK;
constexpr int kScriptFrames = 3000;
// Actualizaciones consecutivas con la pelea en marcha antes de arrancar el guion.
constexpr int kStartAfter = 240;

inline unsigned DirectionBits(char digit, bool facing_right) {
  const unsigned f = facing_right ? kRight : kLeft, b = facing_right ? kLeft : kRight;
  switch (digit) {
    case '1': return kDown | b;
    case '2': return kDown;
    case '3': return kDown | f;
    case '4': return b;
    case '6': return f;
    case '7': return kUp | b;
    case '8': return kUp;
    case '9': return kUp | f;
    default: return 0;
  }
}

inline unsigned MirrorDirections(unsigned bits) {
  return (bits & ~(kLeft | kRight)) | (bits & kLeft ? kRight : 0) | (bits & kRight ? kLeft : 0);
}
// Entre el lado en que se guarda el guion (mirando a la derecha) y el del luchador.
inline unsigned ForFacing(unsigned bits, bool facing_right) {
  return facing_right ? bits : MirrorDirections(bits);
}

// El lado que esta a la izquierda mira a la derecha. Empata: conserva el lado.
// Se actualiza tras cada actualizacion simulada, con las x de raiz de los dos
// (Session::SetPositions); empieza con el jugador 1 a la izquierda.
struct Facing {
  bool first_on_left = true;
  void SetPositions(float x0, float x1) {
    if (x0 != x1) first_on_left = x0 < x1;
  }
  bool Right(int side) const { return (side == 0) == first_on_left; }
};

struct Pad {
  std::vector<unsigned> f;
  explicit Pad(int n) : f(static_cast<std::size_t>(n)) {}
  static unsigned D(char digit) { return DirectionBits(digit, true); }
  void Put(int at, unsigned bits) {
    if (at >= 0 && at < static_cast<int>(f.size())) f[static_cast<std::size_t>(at)] = bits;
  }
  // Una direccion sostenida `frames`; devuelve el primer frame tras ella.
  int Hold(int at, int frames, char digit, unsigned buttons = 0) {
    for (int i = 0; i < frames; ++i) Put(at + i, D(digit) | buttons);
    return at + frames;
  }
  // Digitos uno tras otro, 3 frames cada uno (un digito repetido lleva un frame
  // neutro entre medias), luego los botones UN frame con el ultimo digito, y
  // ese digito una vez mas. `pressed`: el frame de los botones.
  int Motion(int at, const char* digits, unsigned buttons, int* pressed = nullptr) {
    char last = 0;
    for (const char* d = digits; *d; ++d) {
      if (*d == last) at = Hold(at, 1, '5');
      at = Hold(at, 3, *d);
      last = *d;
    }
    if (pressed) *pressed = at;
    Put(at, D(last ? last : '5') | buttons);
    Put(at + 1, D(last ? last : '5'));
    return at + 2;
  }
  int Press(int at, char digit, unsigned buttons) {
    Put(at, D(digit) | buttons);
    return at + 1;
  }
};

struct Event {
  int frame;
  const char* what;
};

struct Script {
  Pad p1{kScriptFrames}, p2{kScriptFrames};
  std::vector<int> presses;  // pulsaciones de un frame de P1 (las del control negativo)
  std::vector<Event> events;
  int frames = kScriptFrames;
};

// El guion `full` de Ember (Full() en sf4e__SelfTest__OracleTraining.cxx).
// Cada frame es un frame del guion, a 60 por segundo. Todo lo no puesto es neutro.
inline Script Full() {
  Script s;
  Pad& p1 = s.p1;
  Pad& p2 = s.p2;
  int pressed = 0;
  auto note = [&](const char* what, int at) { s.events.push_back({at, what}); };
  auto close = [&](int at) {
    p1.Hold(at, 60, '6');
    return at + 84;
  };
  auto press1 = [&](const char* what, int at, char digit, unsigned buttons) {
    p1.Press(at, digit, buttons);
    s.presses.push_back(at);
    note(what, at);
  };
  auto motion1 = [&](const char* what, int at, const char* digits, unsigned buttons) {
    p1.Motion(at, digits, buttons, &pressed);
    s.presses.push_back(pressed);
    note(what, pressed);
  };

  // CAMINAR: P1 camina hacia P2 (lo empuja al muro); P2 camina adelante y atras.
  p1.Hold(30, 140, '6');
  p2.Hold(60, 30, '6');
  p2.Hold(100, 30, '4');
  // P1 retroceso rapido (dos toques atras) en 172; salto neutro en 200; P2 salta adelante en 215.
  p1.Hold(172, 2, '4');
  p1.Hold(176, 2, '4');
  p1.Hold(200, 3, '8');
  p2.Hold(215, 3, '9');
  // NORMALES, cada uno tras caminar al contacto, P2 de pie.
  int t = close(290);
  press1("P1 standing LP", t, '5', kLP);
  t += 40;
  t = close(t);
  press1("P1 crouching LK", t, '2', kLK);
  t += 50;
  t = close(t);
  press1("P1 standing MP", t, '5', kMP);
  t += 50;
  t = close(t);
  press1("P1 standing HP", t, '5', kHP);
  t += 60;
  // AGARRE: LP + LK juntos.
  t = close(t);
  press1("P1 throw (LP+LK)", t, '5', kLP | kLK);
  t += 200;
  // Hadoken de P2 (236 + LP) a quemarropa sobre P1.
  t = close(t);
  p2.Motion(t, "236", kLP, &pressed);
  note("P2 Hadoken (236+LP)", pressed);
  t += 120;
  // BLOQUEO: P1 agachado atras; P2 da dos jabs.
  t = close(t);
  p1.Hold(t, 100, '1');
  p2.Press(t + 10, '5', kLP);
  p2.Press(t + 40, '5', kLP);
  note("P1 crouch-blocks, P2 jabs", t + 10);
  t += 110;
  // PROYECTIL: Hadoken (236 + HP) a quemarropa.
  t = close(t);
  motion1("P1 Hadoken (236+HP)", t, "236", kHP);
  t += 200;
  // ESPECIAL INVENCIBLE: P2 pulsa HP; el Shoryuken (623 + HP) de P1 es invencible.
  t = close(t);
  p2.Press(t + 5, '5', kHP);
  note("P2 standing HP", t + 5);
  motion1("P1 Shoryuken (623+HP)", t, "623", kHP);
  t += 200;
  // ULTRA: Metsu Hadoken de P1 (236236 + tres punos), luego el de P2.
  t = close(t);
  motion1("P1 Ultra (236236+3P)", t, "236236", kLP | kMP | kHP);
  t += 392;
  p2.Motion(t, "236236", kLP | kMP | kHP, &pressed);
  note("P2 Ultra (236236+3P)", pressed);
  return s;
}

// Control negativo (SF4E_ORACLE_MUTATE): quita los botones de P1 en ese frame del
// guion, que debe ser una de sus pulsaciones de un frame. False si no lo es.
inline bool Mutate(Script& s, int frame) {
  bool known = false;
  for (int press : s.presses) known = known || press == frame;
  if (!known) return false;
  s.p1.f[static_cast<std::size_t>(frame)] &= ~(kPunches | kKicks);
  return true;
}

}  // namespace oraculo
}  // namespace usf4
