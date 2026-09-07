#include <SDL.h>
#include <glad/glad.h>
#include <switch.h>

#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr const char* kReceipt =
    "sdmc:/switch/oot3dre/switch-gl-probe.txt";

void WriteReceipt(std::string_view status, std::string_view detail = {}) {
    std::ofstream stream(kReceipt, std::ios::trunc);
    if (!stream) {
        return;
    }
    stream << "status=" << status << '\n';
    if (!detail.empty()) {
        stream << "detail=" << detail << '\n';
    }
}

GLuint Compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return shader;
    }
    std::array<char, 2048> log{};
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr,
                       log.data());
    glDeleteShader(shader);
    throw std::runtime_error(log.data());
}

GLuint BuildProgram() {
    static constexpr const char* kVertex = R"glsl(#version 430
out vec3 color;
void main() {
    const vec2 positions[3] = vec2[3](
        vec2(-0.8, -0.8), vec2(0.8, -0.8), vec2(0.0, 0.8));
    const vec3 colors[3] = vec3[3](
        vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0),
        vec3(0.0, 0.0, 1.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    color = colors[gl_VertexID];
}
)glsl";
    static constexpr const char* kFragment = R"glsl(#version 430
in vec3 color;
layout(location=0) out vec4 fragment_color;
void main() {
    fragment_color = vec4(color, 1.0);
}
)glsl";

    const GLuint vertex = Compile(GL_VERTEX_SHADER, kVertex);
    const GLuint fragment = Compile(GL_FRAGMENT_SHADER, kFragment);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(fragment);
    glDeleteShader(vertex);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        std::array<char, 2048> log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr,
                            log.data());
        glDeleteProgram(program);
        throw std::runtime_error(log.data());
    }
    return program;
}

} // namespace

int main() {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    GLuint program = 0U;
    GLuint vertexArray = 0U;
    try {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        window = SDL_CreateWindow(
            "OoT3D Switch OpenGL probe", SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_SHOWN);
        if (window == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }
        context = SDL_GL_CreateContext(window);
        if (context == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_GL_MakeCurrent(window, context);
        if (!gladLoadGLLoader(
                reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
            throw std::runtime_error("gladLoadGLLoader failed");
        }

        program = BuildProgram();
        glGenVertexArrays(1, &vertexArray);
        glBindVertexArray(vertexArray);
        glUseProgram(program);
        WriteReceipt("running");

        while (appletMainLoop()) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    return 0;
                }
            }
            glViewport(0, 0, 1280, 720);
            glClearColor(0.05F, 0.07F, 0.11F, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            SDL_GL_SwapWindow(window);
        }
    } catch (const std::exception& ex) {
        WriteReceipt("failed", ex.what());
        return 1;
    }

    glDeleteVertexArrays(1, &vertexArray);
    glDeleteProgram(program);
    if (context != nullptr) {
        SDL_GL_DeleteContext(context);
    }
    if (window != nullptr) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return 0;
}
