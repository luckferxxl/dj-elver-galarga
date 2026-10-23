// Compilación de comprobación: -DRDJ_IMASSERT_DIAG -DIMGUI_USER_CONFIG="\"<ruta>/tools/imgui_comprobaciones.h\"" (ver docs/COMPILAR.md).
#pragma once
#ifdef __cplusplus
extern "C"
#endif
void rdj_imassert(const char* expr, const char* file, int line);
#define IM_ASSERT(_EXPR) ((_EXPR) ? (void)0 : rdj_imassert(#_EXPR, __FILE__, __LINE__))
