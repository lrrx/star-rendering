#pragma once
#include <string>
#include <unordered_map>
#include <optional>

#include "gl_context.hpp"

GLuint createProgramFromFiles(std::string const& vsPath, std::string const& fsPath);
GLuint createComputeProgramFromFile(std::string const& csname, std::unordered_map<std::string, std::optional<size_t>> const& defines);