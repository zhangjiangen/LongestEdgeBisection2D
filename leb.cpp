#include <cstdio>
#include <cstdlib>
#include <utility>

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl.h"

#define LOG(fmt, ...) fprintf(stdout, fmt "\n", ##__VA_ARGS__); fflush(stdout);

#define CBT_IMPLEMENTATION
#include "cbt.h"

#define DJ_OPENGL_IMPLEMENTATION
#include "dj_opengl.h"

//#define LEB_IMPLEMENTATION
//#include "leb.h"

#define CBT_INIT_MAX_DEPTH 1

#ifndef PATH_TO_SRC_DIRECTORY
#   define PATH_TO_SRC_DIRECTORY "./"
#endif

struct Window {
    const char *name;
    int32_t width, height;
    struct {
        int32_t major, minor;
    } glversion;
    GLFWwindow* handle;
} g_window = {
    "Longest Edge Bisection",
    512, 512,
    {4, 5},
    NULL
};

#define CBT_MAX_DEPTH 10
enum {MODE_TRIANGLE, MODE_SQUARE};
enum {BACKEND_CPU, BACKEND_GPU};
struct LongestEdgeBisection {
    cbt_Tree *cbt;
    struct {
        int mode;
        int backend;
        struct {
            float x, y;
        } target;
    } params;
} g_leb = {
    cbt_CreateAtDepth(CBT_MAX_DEPTH, CBT_INIT_MAX_DEPTH),
    {
        MODE_TRIANGLE,
        BACKEND_CPU,
        {0.49951f, 0.41204f}
    }
};
#undef CBT_MAX_DEPTH

enum {
    PROGRAM_TRIANGLES,
    PROGRAM_TARGET,
    PROGRAM_CBT_SUM_REDUCTION_PREPASS,
    PROGRAM_CBT_SUM_REDUCTION,
    PROGRAM_CBT_DISPATCH,
    PROGRAM_LEB_DISPATCH,
    PROGRAM_LEB_SPLIT,
    PROGRAM_LEB_MERGE,

    PROGRAM_COUNT
};
enum {VERTEXARRAY_EMPTY, VERTEXARRAY_COUNT};
enum {BUFFER_CBT, BUFFER_CBT_DISPATCHER, BUFFER_LEB_DISPATCHER, BUFFER_COUNT};
struct OpenGL {
    GLuint programs[PROGRAM_COUNT];
    GLuint vertexarrays[VERTEXARRAY_COUNT];
    GLuint buffers[BUFFER_COUNT];
} g_gl = {
    {0},
    {0},
    {0}
};

#define PATH_TO_SHADER_DIRECTORY PATH_TO_SRC_DIRECTORY "shaders/"
#define PATH_TO_CBT_DIRECTORY PATH_TO_SRC_DIRECTORY "submodules/libcbt/"

bool LoadTargetProgram()
{
    LOG("Loading {Target Program}")
    djg_program *djgp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_TARGET];

    djgp_push_file(djgp, PATH_TO_SHADER_DIRECTORY "target.glsl");
    if (!djgp_to_gl(djgp, 450, false, true, glp)) {
        djgp_release(djgp);

        return false;
    }

    djgp_release(djgp);

    return glGetError() == GL_NO_ERROR;
}

bool LoadCbtSumReductionPrepassProgram()
{
    LOG("Loading {CBT-Sum-Reduction-Prepass Program}");
    djg_program *djgp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_CBT_SUM_REDUCTION_PREPASS];

    djgp_push_string(djgp, "#define CBT_HEAP_BUFFER_BINDING %i\n", BUFFER_CBT);
    djgp_push_file(djgp, PATH_TO_CBT_DIRECTORY "glsl/cbt.glsl");
    djgp_push_file(djgp, PATH_TO_CBT_DIRECTORY "glsl/cbt_SumReductionPrepass.glsl");
    djgp_push_string(djgp, "#ifdef COMPUTE_SHADER\n#endif");
    if (!djgp_to_gl(djgp, 450, false, true, glp)) {
        djgp_release(djgp);

        return false;
    }

    djgp_release(djgp);

    return glGetError() == GL_NO_ERROR;
}

bool LoadCbtSumReductionProgram()
{
    LOG("Loading {CBT-Sum-Reduction Program}");
    djg_program *djgp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_CBT_SUM_REDUCTION];

    djgp_push_string(djgp, "#define CBT_HEAP_BUFFER_BINDING %i\n", BUFFER_CBT);
    djgp_push_file(djgp, PATH_TO_CBT_DIRECTORY "glsl/cbt.glsl");
    djgp_push_file(djgp, PATH_TO_CBT_DIRECTORY "glsl/cbt_SumReduction.glsl");
    djgp_push_string(djgp, "#ifdef COMPUTE_SHADER\n#endif");
    if (!djgp_to_gl(djgp, 450, false, true, glp)) {
        djgp_release(djgp);

        return false;
    }

    djgp_release(djgp);

    return glGetError() == GL_NO_ERROR;
}

bool LoadCbtDispatcherProgram()
{
    LOG("Loading {CBT-Dispatcher Program}");
    djg_program *djgp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_CBT_DISPATCH];

    djgp_push_string(djgp, "#define CBT_HEAP_BUFFER_BINDING %i\n", BUFFER_CBT);
    djgp_push_string(djgp, "#define CBT_DISPATCHER_BUFFER_BINDING %i\n", BUFFER_CBT_DISPATCHER);
    djgp_push_file(djgp, PATH_TO_CBT_DIRECTORY "glsl/cbt.glsl");
    djgp_push_file(djgp, PATH_TO_CBT_DIRECTORY "glsl/cbt_Dispatcher.glsl");
    djgp_push_string(djgp, "#ifdef COMPUTE_SHADER\n#endif");
    if (!djgp_to_gl(djgp, 450, false, true, glp)) {
        djgp_release(djgp);

        return false;
    }

    djgp_release(djgp);

    return glGetError() == GL_NO_ERROR;
}

bool LoadLebDispatcherProgram()
{
    LOG("Loading {CBT-Dispatcher Program}");
    djg_program *djgp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_LEB_DISPATCH];

    djgp_push_string(djgp, "#define CBT_HEAP_BUFFER_BINDING %i\n", BUFFER_CBT);
    djgp_push_string(djgp, "#define LEB_DISPATCHER_BUFFER_BINDING %i\n", BUFFER_LEB_DISPATCHER);
    djgp_push_file(djgp, PATH_TO_CBT_DIRECTORY "glsl/cbt.glsl");
    djgp_push_file(djgp, PATH_TO_SHADER_DIRECTORY "leb_dispatcher.glsl");
    djgp_push_string(djgp, "#ifdef COMPUTE_SHADER\n#endif");
    if (!djgp_to_gl(djgp, 450, false, true, glp)) {
        djgp_release(djgp);

        return false;
    }

    djgp_release(djgp);

    return glGetError() == GL_NO_ERROR;
}

bool LoadSubdivisionProgram(int programID, const char *flags)
{
    LOG("Loading {Subdivision Program}")
    djg_program *djgp = djgp_create();
    GLuint *glp = &g_gl.programs[programID];

    if (g_leb.params.mode == MODE_SQUARE)
        djgp_push_string(djgp, "#define MODE_SQUARE\n");
    else
        djgp_push_string(djgp, "#define MODE_TRIANGLE\n");

    djgp_push_string(djgp, flags);
    djgp_push_string(djgp, "#define CBT_HEAP_BUFFER_BINDING %i\n", BUFFER_CBT);
    djgp_push_file(djgp, PATH_TO_CBT_DIRECTORY "glsl/cbt.glsl");
    djgp_push_file(djgp, PATH_TO_SHADER_DIRECTORY "leb.glsl");
    djgp_push_file(djgp, PATH_TO_SHADER_DIRECTORY "subdivision.glsl");
    djgp_push_string(djgp, "#ifdef COMPUTE_SHADER\n#endif");
    if (!djgp_to_gl(djgp, 450, false, true, glp)) {
        djgp_release(djgp);

        return false;
    }

    djgp_release(djgp);

    return glGetError() == GL_NO_ERROR;
}

bool LoadSubdivisionSplitProgram()
{
    return LoadSubdivisionProgram(PROGRAM_LEB_SPLIT, "#define FLAG_SPLIT 1\n");
}

bool LoadSubdivisionMergeProgram()
{
    return LoadSubdivisionProgram(PROGRAM_LEB_MERGE, "#define FLAG_MERGE 1\n");
}

bool LoadSubdivisionPrograms()
{
    return LoadSubdivisionMergeProgram() && LoadSubdivisionSplitProgram();
}


bool LoadTrianglesProgram()
{
    LOG("Loading {Triangles Program}")
    djg_program *djgp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_TRIANGLES];

    if (g_leb.params.mode == MODE_SQUARE)
        djgp_push_string(djgp, "#define MODE_SQUARE\n");
    else
        djgp_push_string(djgp, "#define MODE_TRIANGLE\n");

    djgp_push_string(djgp, "#define CBT_HEAP_BUFFER_BINDING %i\n", BUFFER_CBT);
    djgp_push_file(djgp, PATH_TO_CBT_DIRECTORY "glsl/cbt.glsl");
    djgp_push_file(djgp, PATH_TO_SHADER_DIRECTORY "leb.glsl");
    djgp_push_file(djgp, PATH_TO_SHADER_DIRECTORY "triangles.glsl");
    if (!djgp_to_gl(djgp, 450, false, true, glp)) {
        djgp_release(djgp);

        return false;
    }

    djgp_release(djgp);

    return glGetError() == GL_NO_ERROR;
}


bool LoadPrograms()
{
    bool success = true;

    if (success) success = LoadTrianglesProgram();
    if (success) success = LoadTargetProgram();
    if (success) success = LoadCbtSumReductionPrepassProgram();
    if (success) success = LoadCbtSumReductionProgram();
    if (success) success = LoadCbtDispatcherProgram();
    if (success) success = LoadLebDispatcherProgram();
    if (success) success = LoadSubdivisionPrograms();

    return success;
}

#undef PATH_TO_CBT_DIRECTORY
#undef PATH_TO_SHADER_DIRECTORY

bool LoadEmptyVertexArray()
{
    GLuint *glv = &g_gl.vertexarrays[VERTEXARRAY_EMPTY];

    glGenVertexArrays(1, glv);
    glBindVertexArray(*glv);
    glBindVertexArray(0);

    return glGetError() == GL_NO_ERROR;
}

bool LoadVertexArrays()
{
    bool success = true;

    if (success) success = LoadEmptyVertexArray();

    return success;
}

bool LoadCbtBuffer()
{
    GLuint *buffer = &g_gl.buffers[BUFFER_CBT];

    if (glIsBuffer(*buffer))
        glDeleteBuffers(1, buffer);

    glGenBuffers(1, buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, *buffer);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER,
                    cbt_HeapByteSize(g_leb.cbt),
                    cbt_GetHeap(g_leb.cbt),
                    0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BUFFER_CBT, *buffer);

    return glGetError() == GL_NO_ERROR;
}

bool LoadCbtDispatcherBuffer()
{
    GLuint *buffer = &g_gl.buffers[BUFFER_CBT_DISPATCHER];
    uint32_t dispatchCmd[8] = {CBT_INIT_MAX_DEPTH, 1, 1, 0, 0, 0, 0, 0};

    glGenBuffers(1, buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, *buffer);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER,
                    sizeof(dispatchCmd),
                    dispatchCmd,
                    0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BUFFER_CBT_DISPATCHER, *buffer);

    return glGetError() == GL_NO_ERROR;
}

bool LoadLebDispatcherBuffer()
{
    GLuint *buffer = &g_gl.buffers[BUFFER_LEB_DISPATCHER];
    uint32_t drawArraysCmd[8] = {3, CBT_INIT_MAX_DEPTH, 0, 0, 0, 0, 0, 0};

    glGenBuffers(1, buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, *buffer);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER,
                    sizeof(drawArraysCmd),
                    drawArraysCmd,
                    0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BUFFER_LEB_DISPATCHER, *buffer);

    return glGetError() == GL_NO_ERROR;
}

bool LoadBuffers()
{
    bool success = true;

    if (success) success = LoadCbtBuffer();
    if (success) success = LoadCbtDispatcherBuffer();
    if (success) success = LoadLebDispatcherBuffer();

    return success;
}

static void APIENTRY
DebugOutputLogger(
    GLenum source,
    GLenum type,
    GLuint,
    GLenum severity,
    GLsizei,
    const GLchar* message,
    const GLvoid*
) {
    char srcstr[32], typestr[32];

    switch(source) {
        case GL_DEBUG_SOURCE_API: strcpy(srcstr, "OpenGL"); break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM: strcpy(srcstr, "Windows"); break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: strcpy(srcstr, "Shader Compiler"); break;
        case GL_DEBUG_SOURCE_THIRD_PARTY: strcpy(srcstr, "Third Party"); break;
        case GL_DEBUG_SOURCE_APPLICATION: strcpy(srcstr, "Application"); break;
        case GL_DEBUG_SOURCE_OTHER: strcpy(srcstr, "Other"); break;
        default: strcpy(srcstr, "???"); break;
    };

    switch(type) {
        case GL_DEBUG_TYPE_ERROR: strcpy(typestr, "Error"); break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: strcpy(typestr, "Deprecated Behavior"); break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: strcpy(typestr, "Undefined Behavior"); break;
        case GL_DEBUG_TYPE_PORTABILITY: strcpy(typestr, "Portability"); break;
        case GL_DEBUG_TYPE_PERFORMANCE: strcpy(typestr, "Performance"); break;
        case GL_DEBUG_TYPE_OTHER: strcpy(typestr, "Message"); break;
        default: strcpy(typestr, "???"); break;
    }

    if(severity == GL_DEBUG_SEVERITY_HIGH) {
        LOG("djg_error: %s %s\n"                \
                "-- Begin -- GL_debug_output\n" \
                "%s\n"                              \
                "-- End -- GL_debug_output\n",
                srcstr, typestr, message);
    } else if(severity == GL_DEBUG_SEVERITY_MEDIUM) {
        LOG("djg_warn: %s %s\n"                 \
                "-- Begin -- GL_debug_output\n" \
                "%s\n"                              \
                "-- End -- GL_debug_output\n",
                srcstr, typestr, message);
    }
}

bool Load()
{
    bool success = true;

    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(&DebugOutputLogger, NULL);

    if (success) success = LoadPrograms();
    if (success) success = LoadVertexArrays();
    if (success) success = LoadBuffers();

    return success;
}


void ReductionKernel()
{
    int it = cbt_MaxDepth(g_leb.cbt);

    glUseProgram(g_gl.programs[PROGRAM_CBT_SUM_REDUCTION_PREPASS]);
    if (true) {
        int cnt = ((1 << it) >> 5);
        int numGroup = (cnt >= 256) ? (cnt >> 8) : 1;
        int loc = glGetUniformLocation(g_gl.programs[PROGRAM_CBT_SUM_REDUCTION_PREPASS],
                                       "u_PassID");

        glUniform1i(loc, it);
        glDispatchCompute(numGroup, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        it-= 5;
    }

    glUseProgram(g_gl.programs[PROGRAM_CBT_SUM_REDUCTION]);
    while (--it >= 0) {
        int loc = glGetUniformLocation(g_gl.programs[PROGRAM_CBT_SUM_REDUCTION], "u_PassID");
        int cnt = 1 << it;
        int numGroup = (cnt >= 256) ? (cnt >> 8) : 1;

        glUniform1i(loc, it);
        glDispatchCompute(numGroup, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void DispatcherKernel()
{
    glUseProgram(g_gl.programs[PROGRAM_CBT_DISPATCH]);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glUseProgram(0);
}

void SubdivisionKernel()
{
    static int pingPong = 0;
    const GLuint *program = &g_gl.programs[PROGRAM_LEB_SPLIT + pingPong];

    glUseProgram(*program);
    int loc = glGetUniformLocation(*program, "u_TargetPosition");
    glUniform2f(loc, g_leb.params.target.x, g_leb.params.target.y);
    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,
                 g_gl.buffers[BUFFER_CBT_DISPATCHER]);
    glDispatchComputeIndirect(0);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glUseProgram(0);

    pingPong = 1 - pingPong;
}

void LebDispatcherKernel()
{
    glUseProgram(g_gl.programs[PROGRAM_LEB_DISPATCH]);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glUseProgram(0);
}

void InitGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(g_window.handle, false);
    ImGui_ImplOpenGL3_Init("#version 450");

    ImGuiStyle& style = ImGui::GetStyle();

    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.04f, 0.04f, 0.4f);

}

void ReleaseGui()
{
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void UpdateSubdivision()
{
    DispatcherKernel();
    SubdivisionKernel();
    ReductionKernel();
}

void DrawTarget()
{
    // target helper
    glUseProgram(g_gl.programs[PROGRAM_TARGET]);
    glUniform2f(
        glGetUniformLocation(g_gl.programs[PROGRAM_TARGET], "u_Target"),
        g_leb.params.target.x, g_leb.params.target.y
    );
    glPointSize(11.f);
    glBindVertexArray(g_gl.vertexarrays[VERTEXARRAY_EMPTY]);
        glDrawArrays(GL_POINTS, 0, 1);
    glBindVertexArray(0);
    glUseProgram(0);
}

void DrawLeb()
{
    // prepare indirect draw command
    glUseProgram(g_gl.programs[PROGRAM_LEB_DISPATCH]);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);

    // draw
    glEnable(GL_CULL_FACE);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, g_gl.buffers[BUFFER_LEB_DISPATCHER]);
    glUseProgram(g_gl.programs[PROGRAM_TRIANGLES]);
    glBindVertexArray(g_gl.vertexarrays[VERTEXARRAY_EMPTY]);
        glDrawArraysIndirect(GL_TRIANGLES, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_CULL_FACE);
}

void Draw()
{
    glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, g_window.width, g_window.height);
    DrawLeb();
    DrawTarget();
}

void DrawGui()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::Begin("Window");
    {
        const char* eModes[] = {"Triangle", "Square"};
        const char* eBackend[] = {"CPU", "GPU"};
        int32_t cbtByteSize = cbt_HeapByteSize(g_leb.cbt);
        int32_t maxDepth = cbt_MaxDepth(g_leb.cbt);

        if (ImGui::Combo("Mode", &g_leb.params.mode, &eModes[0], 2)) {
            cbt_ResetToDepth(g_leb.cbt, CBT_INIT_MAX_DEPTH);
            LoadCbtBuffer();
            LoadPrograms();
        }
        if (ImGui::Combo("Backend", &g_leb.params.backend, &eBackend[0], 2)) {

        }
        ImGui::SliderFloat("TargetX", &g_leb.params.target.x, 0, 1);
        ImGui::SliderFloat("TargetY", &g_leb.params.target.y, 0, 1);
        if (ImGui::SliderInt("MaxDepth", &maxDepth, 6, 30)) {
            cbt_Release(g_leb.cbt);
            g_leb.cbt = cbt_CreateAtDepth(maxDepth, CBT_INIT_MAX_DEPTH);
            LoadCbtBuffer();
            LoadPrograms();
        }
        if (ImGui::Button("Reset")) {
            cbt_ResetToDepth(g_leb.cbt, CBT_INIT_MAX_DEPTH);
            LoadCbtBuffer();
        }
        ImGui::Separator();
        ImGui::Text("Mem Usage: %u %s",
                    cbtByteSize >= (1 << 20) ? (cbtByteSize >> 20) : (cbtByteSize >= (1 << 10) ? cbtByteSize >> 10 : cbtByteSize),
                    cbtByteSize >= (1 << 20) ? "MiB" : (cbtByteSize > (1 << 10) ? "KiB" : "B"));
        ImGui::Text("Nodes: %lu", cbt_NodeCount(g_leb.cbt));
    }
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

int main(int argc, char **argv)
{
    LOG("Loading {OpenGL Window}");
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, g_window.glversion.major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, g_window.glversion.minor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif
    g_window.handle = glfwCreateWindow(g_window.width,
                                       g_window.height,
                                       g_window.name,
                                       NULL, NULL);
    if (g_window.handle == NULL) {
        LOG("=> Failure <=");
        glfwTerminate();

        return -1;
    }
    glfwMakeContextCurrent(g_window.handle);

    // setup callbacks
    //glfwSetCursorPosCallback(window.handle, CursorPositionCallback);
    //glfwSetMouseButtonCallback(window.handle, MouseButtonCallback);

    // load OpenGL functions
    LOG("Loading {OpenGL Functions}");
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        LOG("=> Failure <=");
        glfwTerminate();

        return -1;
    }

    // initialize
    LOG("Loading {Demo}");
    if (!Load()) {
        LOG("=> Failure <=");
        glfwTerminate();

        return -1;
    }
    InitGui();

    while (!glfwWindowShouldClose(g_window.handle)) {
        glfwPollEvents();
        UpdateSubdivision();
        Draw();
        DrawGui();
        glfwSwapBuffers(g_window.handle);
    }

    cbt_Release(g_leb.cbt);
    ReleaseGui();
    glfwTerminate();

    return 0;
}
