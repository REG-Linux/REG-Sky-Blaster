// GL headers for the flavour chosen at build time.
#pragma once

#if defined(SKYBLASTER_GL2)
#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL_opengl.h>
#else
#include <SDL3/SDL_opengles2.h>
#endif
