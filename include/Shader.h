#pragma once
// Shader.h — GLSL program wrapper (Member 1: Graphics Engine)
//
// Compiles a vertex + fragment shader pair from files, caches uniform
// locations, and supports hot-reloading (a failed reload keeps the old
// program running so a typo in a shader never crashes the app).

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    // Loads, compiles and links. On failure returns false, fills *error and
    // leaves any previously loaded program untouched.
    bool loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath,
                       std::string* error);
    bool loadFromSource(const std::string& vertexSrc, const std::string& fragmentSrc,
                        std::string* error, const std::string& label = "shader");
    // Re-reads the files passed to the last successful loadFromFiles().
    bool reload(std::string* error);

    void use() const;
    bool valid() const { return program_ != 0; }
    GLuint id() const { return program_; }

    void setBool(const char* name, bool v) const;
    void setInt(const char* name, int v) const;
    void setFloat(const char* name, float v) const;
    void setVec2(const char* name, const glm::vec2& v) const;
    void setVec3(const char* name, const glm::vec3& v) const;
    void setMat3(const char* name, const glm::mat3& m) const;
    void setMat4(const char* name, const glm::mat4& m) const;

    // String overloads (used for array-of-struct uniforms like "uPoints[2].color").
    void setBool(const std::string& n, bool v) const { setBool(n.c_str(), v); }
    void setFloat(const std::string& n, float v) const { setFloat(n.c_str(), v); }
    void setVec3(const std::string& n, const glm::vec3& v) const { setVec3(n.c_str(), v); }

private:
    GLint location(const char* name) const;
    void destroy();

    GLuint program_ = 0;
    std::string vertexPath_;
    std::string fragmentPath_;
    mutable std::unordered_map<std::string, GLint> locationCache_;
};
