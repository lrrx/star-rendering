#pragma once
#include <glad/gl.h>   // resolves to the private MX glad

namespace newstar::detail {

// Single library-wide GL context. Set once by initialize(), read by everyone.
inline GladGLContext g_gl_context{};

inline GladGLContext& gl() { return g_gl_context; }

} // namespace newstar::detail

// Shorthand used throughout library .cpp files: `GL.GenBuffers(...)`
#define GL ::newstar::detail::gl()