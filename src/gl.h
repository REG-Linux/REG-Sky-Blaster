// GL headers for the NanoVG backend chosen at build time.
#pragma once

#if defined(SKYBLASTER_GL2)
#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL_opengl.h>
#define NANOVG_GL2 1
#else
#include <SDL3/SDL_opengles2.h>
#define NANOVG_GLES2 1
#endif
