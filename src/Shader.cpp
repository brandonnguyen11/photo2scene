#include "Shader.h"

#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>

namespace {

bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

GLuint compileStage(GLenum type, const std::string& source, const std::string& label,
                    std::string* error) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<size_t>(len > 1 ? len : 1), '\0');
        glGetShaderInfoLog(shader, len, nullptr, log.data());
        if (error) {
            *error = label + (type == GL_VERTEX_SHADER ? " (vertex)" : " (fragment)") +
                     " compile error:\n" + log;
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

}  // namespace

Shader::~Shader() { destroy(); }

Shader::Shader(Shader&& other) noexcept { *this = std::move(other); }

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        destroy();
        program_ = other.program_;
        vertexPath_ = std::move(other.vertexPath_);
        fragmentPath_ = std::move(other.fragmentPath_);
        locationCache_ = std::move(other.locationCache_);
        other.program_ = 0;
    }
    return *this;
}

void Shader::destroy() {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    locationCache_.clear();
}

bool Shader::loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath,
                           std::string* error) {
    std::string vs, fs;
    if (!readTextFile(vertexPath, vs)) {
        if (error) *error = "Cannot read shader file: " + vertexPath;
        return false;
    }
    if (!readTextFile(fragmentPath, fs)) {
        if (error) *error = "Cannot read shader file: " + fragmentPath;
        return false;
    }
    if (!loadFromSource(vs, fs, error, vertexPath + " + " + fragmentPath)) return false;
    vertexPath_ = vertexPath;
    fragmentPath_ = fragmentPath;
    return true;
}

bool Shader::loadFromSource(const std::string& vertexSrc, const std::string& fragmentSrc,
                            std::string* error, const std::string& label) {
    GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSrc, label, error);
    if (!vs) return false;
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSrc, label, error);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<size_t>(len > 1 ? len : 1), '\0');
        glGetProgramInfoLog(program, len, nullptr, log.data());
        if (error) *error = label + " link error:\n" + log;
        glDeleteProgram(program);
        return false;
    }

    destroy();  // only replace the old program once the new one is good
    program_ = program;
    return true;
}

bool Shader::reload(std::string* error) {
    if (vertexPath_.empty()) {
        if (error) *error = "Shader was not loaded from files";
        return false;
    }
    const std::string vs = vertexPath_, fs = fragmentPath_;
    return loadFromFiles(vs, fs, error);
}

void Shader::use() const { glUseProgram(program_); }

GLint Shader::location(const char* name) const {
    auto it = locationCache_.find(name);
    if (it != locationCache_.end()) return it->second;
    GLint loc = glGetUniformLocation(program_, name);
    locationCache_.emplace(name, loc);
    return loc;  // -1 is fine: GL silently ignores writes to location -1
}

void Shader::setBool(const char* name, bool v) const { glUniform1i(location(name), v ? 1 : 0); }
void Shader::setInt(const char* name, int v) const { glUniform1i(location(name), v); }
void Shader::setFloat(const char* name, float v) const { glUniform1f(location(name), v); }
void Shader::setVec2(const char* name, const glm::vec2& v) const {
    glUniform2fv(location(name), 1, glm::value_ptr(v));
}
void Shader::setVec3(const char* name, const glm::vec3& v) const {
    glUniform3fv(location(name), 1, glm::value_ptr(v));
}
void Shader::setMat3(const char* name, const glm::mat3& m) const {
    glUniformMatrix3fv(location(name), 1, GL_FALSE, glm::value_ptr(m));
}
void Shader::setMat4(const char* name, const glm::mat4& m) const {
    glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(m));
}
