cmake_minimum_required(VERSION 3.20)
project(RamiroDJ VERSION 0.14.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(TP ${CMAKE_SOURCE_DIR}/third_party)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _USE_MATH_DEFINES)

if(MINGW)
  # Ejecutable autónomo: sin DLL de MinGW junto al .exe.
  set(RDJ_STATIC -static -static-libgcc -static-libstdc++)
endif()

# ------------------------------------------------------------ motor
add_library(rdj_core STATIC
  src/audio/ma_impl.c
  src/audio/analysis.cpp
  src/audio/engine.cpp
  src/audio/rack.cpp
  src/audio/instruments.cpp
  src/audio/loader.cpp
  src/audio/recorder.cpp
  src/audio/audio_io.cpp
  src/audio/stretch.cpp
  third_party/sqlite/sqlite3.c
)
target_include_directories(rdj_core PUBLIC src ${TP}/miniaudio ${TP}/json ${TP}/sqlite)
target_include_directories(rdj_core PRIVATE ${TP}/signalsmith-stretch ${TP}/signalsmith-linear/include)
target_compile_options(rdj_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wno-unused-function> -O2)
set_source_files_properties(src/audio/ma_impl.c PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(third_party/sqlite/sqlite3.c PROPERTIES COMPILE_OPTIONS "-w;-O2" COMPILE_DEFINITIONS "SQLITE_THREADSAFE=1;SQLITE_OMIT_LOAD_EXTENSION;SQLITE_DQS=0")
target_link_libraries(rdj_core PUBLIC ole32 winmm avrt)

# ------------------------------------------------------------ intérprete de JavaScript (mapeos de mandos)
add_library(quickjs STATIC ${TP}/quickjs/quickjs.c ${TP}/quickjs/libregexp.c ${TP}/quickjs/libunicode.c ${TP}/quickjs/dtoa.c)
target_include_directories(quickjs PUBLIC ${TP}/quickjs)
target_compile_definitions(quickjs PRIVATE _GNU_SOURCE _WIN32_WINNT=0x0601)
target_compile_options(quickjs PRIVATE -O2 -w)

# ------------------------------------------------------------ mapeos de mandos (Mixxx, GPL) dentro del .exe
file(GLOB MANDOS_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR}/res/mandos ${CMAKE_SOURCE_DIR}/res/mandos/*.xml ${CMAKE_SOURCE_DIR}/res/mandos/*.js)
list(SORT MANDOS_FILES)
set(MANDOS_RC ${CMAKE_BINARY_DIR}/mandos.rc)
set(MANDOS_IDX ${CMAKE_BINARY_DIR}/mandos_indice.txt)
set(_rc "// Generado por CMake: mapeos de mandos de Mixxx (res/mandos)\n2999 RCDATA \"${MANDOS_IDX}\"\n")
set(_idx "")
set(_id 3000)
foreach(f IN LISTS MANDOS_FILES)
  string(APPEND _rc "${_id} RCDATA \"${CMAKE_SOURCE_DIR}/res/mandos/${f}\"\n")
  string(APPEND _idx "${_id}\t${f}\n")
  math(EXPR _id "${_id} + 1")
endforeach()
file(WRITE ${MANDOS_IDX} "${_idx}")
file(WRITE ${MANDOS_RC} "${_rc}")

# ------------------------------------------------------------ pruebas
add_executable(rdj_tests tests/tests.cpp src/core/util.cpp src/core/tags.cpp src/core/library.cpp src/core/midi.cpp src/core/vdjimport.cpp src/core/mandos.cpp)
target_link_libraries(rdj_tests PRIVATE rdj_core shell32 normaliz ${RDJ_STATIC})

# ------------------------------------------------------------ aplicación
if(EXISTS ${CMAKE_SOURCE_DIR}/src/ui/main.cpp)
  set(IMGUI ${TP}/imgui)
  add_library(imgui STATIC
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_win32.cpp ${IMGUI}/backends/imgui_impl_dx11.cpp)
  target_include_directories(imgui PUBLIC ${IMGUI} ${IMGUI}/backends)
  target_compile_options(imgui PRIVATE -O2 -w)

  file(GLOB UI_SOURCES src/ui/*.cpp src/core/*.cpp)
  add_executable(RamiroDJ WIN32 ${UI_SOURCES} res/djelvergalarga.rc ${MANDOS_RC})
  target_link_libraries(RamiroDJ PRIVATE rdj_core imgui quickjs d3d11 d3dcompiler dxgi dwmapi shell32 comdlg32 uuid windowscodecs winhttp normaliz ${RDJ_STATIC})
  target_compile_options(RamiroDJ PRIVATE -Wall -O2)
  set_target_properties(RamiroDJ PROPERTIES OUTPUT_NAME DJElverGalarga)   # 0.10.6: nuevo nombre del programa
  target_link_options(RamiroDJ PRIVATE -municode -s)
endif()

# Herramienta de prueba del separador (no se instala)
add_executable(rdj_stemtool tools/stemtool.cpp src/core/stems.cpp src/core/util.cpp src/core/tags.cpp)
target_link_options(rdj_stemtool PRIVATE -municode)
target_link_libraries(rdj_stemtool PRIVATE rdj_core shell32 ${RDJ_STATIC})

# 0.6 regressions: audio gestures / FX and embedded covers.
# Banco de pruebas del SYNC (no se instala)
add_executable(rdj_sync_bench tests/sync_bench.cpp)
target_link_libraries(rdj_sync_bench PRIVATE rdj_core ${RDJ_STATIC})
add_executable(rdj_performance_tests tests/performance.cpp)
target_link_libraries(rdj_performance_tests PRIVATE rdj_core ${RDJ_STATIC})
add_executable(rdj_cover_tests tests/cover_art.cpp src/core/tags.cpp src/core/util.cpp)
target_link_libraries(rdj_cover_tests PRIVATE rdj_core shell32 normaliz ${RDJ_STATIC})
enable_testing()
add_test(NAME core COMMAND rdj_tests)
add_test(NAME performance COMMAND rdj_performance_tests)
add_test(NAME covers COMMAND rdj_cover_tests)
