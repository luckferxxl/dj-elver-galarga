// Unidad de compilación de miniaudio con el decodificador Vorbis (stb_vorbis).
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"

#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"

/* Retardo del codificador que el decodificador de MP3 quita al principio (cabecera LAME/Xing),
   en muestras; 0 si el archivo no la trae o no se puede abrir. */
unsigned int rdj_mp3_delay(const wchar_t* path) {
    ma_dr_mp3 mp3;
    unsigned int d = 0;
    if (ma_dr_mp3_init_file_w(&mp3, path, NULL)) { d = mp3.delayInPCMFrames; ma_dr_mp3_uninit(&mp3); }
    return d;
}
