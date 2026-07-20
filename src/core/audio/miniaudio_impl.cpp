//============================================================================
//  core/audio/miniaudio_impl.cpp — compiles miniaudio's single-header implementation.
//
//  miniaudio.h is a ~90k-line combined declaration+implementation header (see
//  external/miniaudio/README.md); exactly one translation unit must define
//  MINIAUDIO_IMPLEMENTATION before including it, or every symbol is undefined at link
//  time. This TU exists ONLY for that -- audio.cpp (edited far more often) includes the
//  header without the macro, so it never re-pays this file's compile cost.
//============================================================================
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
