#include "shader_utils.hpp"

#include <cassert>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

std::string loadFile(std::string const& name)
{
    constexpr auto SHADER_BASE_PATH = "/home/u/git/stars/newrenderer/src/shaders/";
    std::string const path = SHADER_BASE_PATH + name;

    std::ifstream file(path);
    std::cout << "loading: " << path << std::endl;
    if (!file) {
        std::cerr << "Failed to open file: " << path << "\n";
        return {};
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

GLuint compileShader(GLenum type, const std::string& src)
{
    GLuint shader = GL.CreateShader(type);
    const char* csrc = src.c_str();
    GL.ShaderSource(shader, 1, &csrc, nullptr);
    GL.CompileShader(shader);

    GLint ok;
    GL.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GL.GetShaderInfoLog(shader, 2048, nullptr, log);
        std::cout << src << std::endl;
        std::cerr << "Shader compile error:\n" << log << "\n";
        std::quick_exit(-1);
    }
    return shader;
}

GLuint createProgramFromFiles(std::string const& vsname, std::string const& fsname)
{
    std::string vsrc = loadFile(vsname);
    std::string fsrc = loadFile(fsname);

    GLuint vs = compileShader(GL_VERTEX_SHADER, vsrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsrc);

    GLuint prog = GL.CreateProgram();
    GL.AttachShader(prog, vs);
    GL.AttachShader(prog, fs);
    GL.LinkProgram(prog);

    GLint ok;
    GL.GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GL.GetProgramInfoLog(prog, 2048, nullptr, log);
        std::cerr << "Program link error:\n" << log << "\n";
        std::quick_exit(-1);
    }

    GL.DeleteShader(vs);
    GL.DeleteShader(fs);
    return prog;
}

void injectDefine(std::string& shaderSource, std::string const& name, std::optional<size_t> value) {
    if(name == "") return;

    std::string const valueString = value.has_value() ? std::to_string(*value) : "";
    // Inject WARP_SIZE define after the #version directive
    std::string define = "#define " + name + " " + valueString + "\n";
    size_t versionPos = shaderSource.find("#version");
    if (versionPos != std::string::npos) {
        size_t firstNewline = shaderSource.find('\n', versionPos);
        if (firstNewline != std::string::npos) {
            shaderSource.insert(firstNewline + 1, define);
        } else {
            shaderSource += "\n" + define;
        }
    } else {
        shaderSource = define + shaderSource;
    }
}

//#define DUMP_COMPUTE_SHADER_BINARY

std::string resolveIncludes(std::string const& csrc) {
    static std::string const prefix = "#include \"";

    std::string result = "";

    std::istringstream iss(csrc);
    
    for(std::string line; std::getline(iss, line);) {
        if(line.rfind(prefix, 0) == 0) {
            //we hit an include directive!
            std::string_view sv = line;
            sv.remove_prefix(prefix.size());
            assert(sv.back() == '"');
            sv.remove_suffix(1);
            assert(sv.size() > 0);
            std::string_view filepath = sv;
            std::cout << "resolving include directive: " << line << std::endl;
            std::string includeContent = loadFile(std::string(filepath));
            result += "//" + line + '\n';
            result += includeContent + '\n';
        }
        else {
            result += line + '\n';
        }
    }

    return result;
}

GLuint createComputeProgramFromFile(std::string const& csname, std::unordered_map<std::string, std::optional<size_t>> const& defines)
{
    std::string csrc = loadFile(csname);
    csrc = resolveIncludes(csrc);

    for(auto const& [name, value] : defines){
        injectDefine(csrc, name, value);
    }

    GLuint cs = compileShader(GL_COMPUTE_SHADER, csrc);

    GLuint prog = GL.CreateProgram();
    GL.AttachShader(prog, cs);
    GL.LinkProgram(prog);

    GLint ok;
    GL.GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GL.GetProgramInfoLog(prog, 2048, nullptr, log);
        std::cerr << "Compute program link error:\n" << log << "\n";
        std::quick_exit(-1);
    }
#ifdef DUMP_COMPUTE_SHADER_BINARY
    // add this once after link succeeds in createComputeProgramFromFile
    GLint binLen = 0;
    GL.GetProgramiv(prog, GL_PROGRAM_BINARY_LENGTH, &binLen);
    std::vector<char> bin(binLen);
    GLenum format = 0;
    GL.GetProgramBinary(prog, binLen, nullptr, &format, bin.data());
    std::ofstream(std::to_string(prog) + "shader_dump.bin", std::ios::binary).write(bin.data(), binLen);
#endif

    GL.DeleteShader(cs);
    return prog;
}
