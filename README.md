# usf4-mac

Ultra Street Fighter IV (the Steam PC release, `SSFIV.exe` build 834219) running natively on Apple Silicon Macs (arm64 + Metal), through **static recompilation**: the game's x86 code is translated ahead of time to C++ and built for arm64. It is not an emulator and not a translation layer at run time.

**This repository contains no game files and no game code.** You need your own copy of the game. The recompiled code (`generated/`), the game data, saves and shaders are produced locally from your install and are never part of this repository.

## What is here

| Folder | What |
|---|---|
| `src/` | The game-specific runtime pieces: Steam offline HLE, curl stub, game-state probes, the determinism oracle, wait loops that sleep instead of spinning |
| `manifest/` | Lifter manifest: function entries, jump-table seeds and replacements (addresses and names only) |
| `tools/` | Lift, build and run scripts (WSL/Linux), jump-table and manifest generators |
| `mac/` | Scripts to sync, build and run on a Mac over SSH (`MAC_SSH`, `MAC_KEY`, `USF4_ROOT`, `USF4_PC_GAME_DIR` set the machine and paths) |
| `oraculo/` | `comparar_hashes.py`: compares two per-frame state dumps |

It builds on a fork of [x86recomp](https://github.com/aplaino/x86recomp) (MIT), branch `usf4-hle` in [maxiumpro/x86recomp](https://github.com/maxiumpro/x86recomp/tree/usf4-hle) (HLE for this game: Metal through SDL_GPU, XAudio2 on FAudio, XInput pads, exclusive fullscreen at the panel's native size, and more).

## Status

- Native arm64 build on an M4 MacBook Air, Metal, 2560x1664 exclusive fullscreen, wired Xbox-protocol pad, audio.
- **Determinism oracle:** a scripted 3000-frame Training fight (Ryu vs Ryu) dumps 62 state fields per frame; the Mac build matches the Windows original frame for frame (`IGUALES`).
- Wait loops (render-thread flag and frame limiter) sleep instead of spinning `Sleep(0)`: about 44 % less CPU in that fight on the M4, same frame pacing (measured with the oracle scene, frames produced with vsync).
- Paths the measured runs never reached may still stop the game; each one found is fixed in the manifest or the toolkit.

## Credits

- [x86recomp](https://github.com/aplaino/x86recomp) (MIT): the static recompiler and runtime this builds on.
- [SF4 Ember Netplay](https://github.com/Confetti3/SF4-Ember-Netplay) (MIT): game addresses, the state hash and the scripted Training fight used by the oracle (`src/oraculo*`). Its license is in `LICENSE-Ember`.
- [typex2-recomp](https://github.com/PabloVSouza/typex2-recomp) by PabloVSouza (GPL-3.0-or-later): its notes on porting SSF4 AE and its shader test were consulted while planning. No code from it is used here.
- FAudio, SDL3, SoftFloat and the other libraries the toolkit uses, under their own licenses.

Street Fighter is a trademark of Capcom. This project is not affiliated with or endorsed by Capcom.

## License

MIT (see `LICENSE`). Code derived from SF4 Ember Netplay keeps its MIT notice (`LICENSE-Ember`).
