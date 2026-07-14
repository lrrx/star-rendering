#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include "gl_context.hpp"

namespace newstar {

class GpuTexture {
public:
    enum class Format: uint8_t {
        R32F,
        RGBA8
    };

    GpuTexture(glm::uvec2 const& size, Format const format);
    ~GpuTexture();

    //non-copyable
    GpuTexture(GpuTexture const&) = delete;
    GpuTexture& operator=(GpuTexture const&) = delete;
 
    //non-movable (for simplicity, for now)
    GpuTexture(GpuTexture&& other) = delete;
    GpuTexture& operator=(GpuTexture&& other) = delete;

    
    GLuint handle() const;

private:
    glm::uvec2 mSize;
    Format mFormat;
    GLuint mHandle;
};

}