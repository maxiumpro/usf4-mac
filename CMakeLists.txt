cmake_minimum_required(VERSION 3.25)
project(usf4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# El runtime y los módulos HLE de x86recomp son bibliotecas compartidas:
# recompilar una no vuelve a enlazar el juego.
set(CMAKE_LINK_DEPENDS_NO_SHARED ON)

# x86recomp (solo lectura): por defecto la base de B0, aplaino/x86recomp rama
# darksouls en el commit de X86RECOMP. Cuando b1\x86recomp-hle tenga el HLE
# genérico que falta, se apunta aquí: -DX86RECOMP_DIR=...
set(X86RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../b0/src/x86recomp"
    CACHE PATH "árbol de x86recomp")
# Lo que produce recomp_lift desde el exe del usuario (tools/lift.sh). Nunca
# en el repo.
set(USF4_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated"
    CACHE PATH "salida de recomp_lift")
# B1 compara hash por frame contra el original: nada de aritmética SSE del
# anfitrión ni LTO (darksouls-mac las prende; aquí no).
set(RECOMP_LTO OFF CACHE BOOL "ThinLTO del código liftado")
set(RECOMP_SSE_HOST OFF CACHE BOOL "SSE del anfitrión en el código liftado")
# Solo para medir el enlace rápido: el código liftado a -O0 (lento; nunca
# para correr el oráculo).
option(USF4_LIFTED_O0 "código liftado sin optimizar (solo para medir el enlace)" OFF)

enable_testing()
add_subdirectory("${X86RECOMP_DIR}" x86recomp)

# HLE propio del juego: Steam sin conexión. Objetos dentro del ejecutable
# (una biblioteca estática dejaría fuera los registradores).
add_library(usf4_hle OBJECT src/steam_offline.cpp src/curl_offline.cpp src/estado_juego.cpp src/oraculo.cpp src/espera_render.cpp)
target_link_libraries(usf4_hle PRIVATE recomp_runtime)

recomp_add_lifted(usf4_lifted "${USF4_GENERATED_DIR}")
if(TARGET usf4_lifted)
  if(USF4_LIFTED_O0)
    target_compile_options(usf4_lifted PRIVATE -O0)
  endif()
  add_executable(usf4 src/main.cpp $<TARGET_OBJECTS:usf4_lifted> $<TARGET_OBJECTS:usf4_hle>)
  target_include_directories(usf4 PRIVATE "${USF4_GENERATED_DIR}")
  # Lo que importan SSFIV.exe y las DLL liftadas (ESTADO.md §Imports):
  # kernel32/user32/gdi32/advapi32/ole32/oleaut32/shell32/ws2_32/winmm/
  # msvcr90 en recomp_runtime; d3d9+d3dx9; dinput8; xinput; dsound;
  # dbghelp; DirectShow (COM) en quartz. Steam: usf4_hle. NO recomp_online.
  target_link_libraries(usf4 PRIVATE recomp_runtime recomp_d3d9 recomp_dinput8
                        recomp_xinput recomp_dbghelp recomp_quartz)
  if(TARGET recomp_dsound)
    target_link_libraries(usf4 PRIVATE recomp_dsound)
  endif()
  set_target_properties(usf4 PROPERTIES ENABLE_EXPORTS ON)  # nombres en los backtraces
endif()
