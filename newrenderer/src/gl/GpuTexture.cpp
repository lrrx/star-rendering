#include "GpuTexture.hpp"
#include "gl_context.hpp"
#include <unordered_map>

namespace newstar {

namespace {

struct SetupProperties {
    GLint internalFormat;
    GLenum format;
    GLenum type;
};

//define gl gpu side setup for each desired texture format
std::unordered_map<GpuTexture::Format, SetupProperties> const setupDefinitions {
    {GpuTexture::Format::R32F, {GL_R32F, GL_RED, GL_FLOAT}},
    {GpuTexture::Format::RGBA8, {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE}},
};

GLuint createTextureInternal(glm::uvec2 const& size, GpuTexture::Format const format)  {
    GLuint resultHandle;
    SetupProperties const& sp = setupDefinitions.at(format);
    GL.GenTextures(1, &resultHandle);
    GL.BindTexture(GL_TEXTURE_2D, resultHandle);
    GL.TexImage2D(GL_TEXTURE_2D, 0, sp.internalFormat, size.x, size.y, 0, sp.format, sp.type, nullptr);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return resultHandle;
}

}

GpuTexture::GpuTexture(glm::uvec2 const& size, Format const format):
    mSize{size},
    mFormat{format},
    mHandle{createTextureInternal(size,format)}
{}

GpuTexture::~GpuTexture() {
    GL.DeleteTextures(1, &mHandle);
}

GLuint GpuTexture::handle() const {
    return mHandle;
}

}