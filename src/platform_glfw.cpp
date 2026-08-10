// Fenster (GLFW) + Grafik (OpenGL 3). Das Gegenstueck zu platform_win32.cpp.
//
// Unter Linux gibt es kein Fenster "vom System" wie unter Windows - deshalb
// GLFW, das kleinste Stueck, das ein Fenster und eine Tastatur besorgt. Gemalt
// wird mit OpenGL 3, das auf jedem Linux mit Grafiktreiber da ist.

#include "platform.h"

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <cstdio>

namespace
{

GLFWwindow* g_window = nullptr;

void OnError(int code, const char* text)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", code, text);
}

}  // namespace

namespace plat
{

bool Init(const char* title, int width, int height)
{
    glfwSetErrorCallback(OnError);
    if (!glfwInit())
    {
        std::fprintf(stderr,
                     "The window could not be opened. Is a graphical session running "
                     "(DISPLAY/WAYLAND_DISPLAY)?\n");
        return false;
    }

    // OpenGL 3.2 Core: das Wenigste, was der ImGui-Renderer braucht, und alt
    // genug, dass es ueberall laeuft.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    g_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (g_window == nullptr)
    {
        std::fprintf(stderr, "The window could not be created.\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);  // mit VSync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init("#version 150");
    return true;
}

void LoadFont()
{
    ImGuiIO& io = ImGui::GetIO();

    // Eine Schreibmaschinenschrift, so wie Consolas unter Windows. Die Liste
    // deckt ab, was auf den ueblichen Distributionen dabei ist - die erste,
    // die da ist, gewinnt.
    static const char* kCandidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/liberation-mono/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
    };

    for (const char* path : kCandidates)
    {
        FILE* f = std::fopen(path, "rb");
        if (f == nullptr)
            continue;
        std::fclose(f);

        if (io.Fonts->AddFontFromFileTTF(path, 17.0f) != nullptr)
        {
            // Zweite, groessere Schrift fuer die Karten im Skilltree.
            io.Fonts->AddFontFromFileTTF(path, 32.0f);
            return;
        }
    }

    io.Fonts->AddFontDefault();
}

bool BeginFrame()
{
    if (g_window == nullptr)
        return false;

    glfwPollEvents();

    // Eingeklapptes Fenster: nichts zeichnen, sonst dreht die Schleife leer
    // mit voller Geschwindigkeit - glfwSwapBuffers bremst dann naemlich nicht.
    // Gewartet wird in einer Schleife und nicht ueber einen erneuten Aufruf:
    // wer das Fenster eine Stunde eingeklappt laesst, haette sonst eine Stunde
    // Rekursion auf dem Stapel.
    while (glfwGetWindowAttrib(g_window, GLFW_ICONIFIED) != 0)
    {
        if (glfwWindowShouldClose(g_window))
            return false;
        glfwWaitEventsTimeout(0.1);
    }

    if (glfwWindowShouldClose(g_window))
        return false;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    return true;
}

void EndFrame(ImVec4 grund)
{
    ImGui::Render();

    int w = 0, h = 0;
    glfwGetFramebufferSize(g_window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(grund.x, grund.y, grund.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(g_window);
}

void Shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (g_window != nullptr)
        glfwDestroyWindow(g_window);
    glfwTerminate();
}

}  // namespace plat
