// Code Klicker - Stufe 1
//
// Fenster (Win32) + Grafik (DirectX 11) + Oberflaeche (Dear ImGui).
// Beides gehoert zu Windows, es wird also nichts weiter installiert.

#include "codecheck.h"
#include "console.h"
#include "native.h"
#include "ore.h"
#include "save.h"
#include "skillfile.h"
#include "skills.h"
#include "skilltree.h"
#include "world.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>

#include <algorithm>
#include <memory>
#include <vector>

static ID3D11Device*           g_device      = nullptr;
static ID3D11DeviceContext*    g_context     = nullptr;
static IDXGISwapChain*         g_swapChain   = nullptr;
static ID3D11RenderTargetView* g_renderTarget = nullptr;
static UINT                    g_resizeW     = 0;
static UINT                    g_resizeH     = 0;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static void CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer)
    {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
        backBuffer->Release();
    }
}

static void CleanupRenderTarget()
{
    if (g_renderTarget)
    {
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }
}

static bool CreateDeviceD3D(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 0;
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL       got      = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                               levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain,
                                               &g_device, &got, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)  // Rechner ohne Grafikkarte
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got,
                                           &g_context);
    if (FAILED(hr))
        return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_swapChain)
    {
        g_swapChain->Release();
        g_swapChain = nullptr;
    }
    if (g_context)
    {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device)
    {
        g_device->Release();
        g_device = nullptr;
    }
}

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return 1;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            g_resizeW = (UINT)LOWORD(lParam);
            g_resizeH = (UINT)HIWORD(lParam);
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)  // Alt-Menue nicht oeffnen
            return 0;
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ApplyStyle()
{
    ImGui::StyleColorsDark();

    ImGuiStyle& s      = ImGui::GetStyle();
    s.WindowRounding   = 6.0f;
    s.ChildRounding    = 4.0f;
    s.FrameRounding    = 4.0f;
    s.GrabRounding     = 4.0f;
    s.WindowBorderSize = 1.0f;
    s.WindowPadding    = ImVec2(8.0f, 8.0f);
    s.ItemSpacing      = ImVec2(8.0f, 6.0f);
    s.WindowTitleAlign = ImVec2(0.02f, 0.5f);

    ImVec4* c                      = s.Colors;
    c[ImGuiCol_WindowBg]           = ImVec4(0.11f, 0.12f, 0.15f, 0.97f);
    c[ImGuiCol_TitleBg]            = ImVec4(0.14f, 0.15f, 0.19f, 1.00f);
    c[ImGuiCol_TitleBgActive]      = ImVec4(0.20f, 0.23f, 0.30f, 1.00f);
    c[ImGuiCol_Button]             = ImVec4(0.22f, 0.42f, 0.30f, 1.00f);
    c[ImGuiCol_ButtonHovered]      = ImVec4(0.28f, 0.54f, 0.38f, 1.00f);
    c[ImGuiCol_ButtonActive]       = ImVec4(0.18f, 0.34f, 0.25f, 1.00f);
    c[ImGuiCol_MenuBarBg]          = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
}

// Welche Seite ist gerade offen? Die Seiten ersetzen einander, sie liegen
// nicht nebeneinander.
enum class Page
{
    Welt,
    Tasche,
    Skills
};

// Reiter in der Menueleiste. Die offene Seite wird hervorgehoben.
static bool PageTab(const char* label, bool active)
{
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.30f, 0.40f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.35f, 0.46f, 1.00f));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.22f, 0.28f, 1.00f));
    }
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.26f, 0.34f, 1.00f));

    const bool clicked = ImGui::Button(label);

    ImGui::PopStyleColor(3);
    return clicked;
}

// Geldanzeige oben rechts: kleine Muenze plus Zahl.
static void DrawMoney(const World& world)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%d", world.money);

    const float coin    = ImGui::GetFontSize() * 0.62f;
    const float textW   = ImGui::CalcTextSize(text).x;
    const float total   = coin * 2.0f + 8.0f + textW;
    const float padding = 14.0f;

    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - total - padding);

    const ImVec2 p  = ImGui::GetCursorScreenPos();
    const float  cy = p.y + ImGui::GetTextLineHeight() * 0.5f;
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(p.x + coin, cy), coin, IM_COL32(224, 176, 62, 255));
    dl->AddCircle(ImVec2(p.x + coin, cy), coin, IM_COL32(150, 112, 30, 255), 0, 1.5f);

    ImGui::Dummy(ImVec2(coin * 2.0f + 8.0f, 0.0f));
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(ImVec4(1.00f, 0.83f, 0.44f, 1.00f), "%s", text);
}

// Consolas liegt auf jedem Windows - damit braucht das Programm keine
// mitgelieferte Schriftdatei.
static void LoadFont()
{
    ImGuiIO& io = ImGui::GetIO();

    const char* path = "C:\\Windows\\Fonts\\consola.ttf";
    FILE*       f    = nullptr;
    if (fopen_s(&f, path, "rb") == 0 && f != nullptr)
    {
        fclose(f);
        if (io.Fonts->AddFontFromFileTTF(path, 17.0f) != nullptr)
        {
            // Zweite, groessere Schrift. Der Skilltree zeichnet damit die
            // Karten - herunterskaliert sieht das sauber aus, hochskaliert
            // wuerde es verlaufen.
            io.Fonts->AddFontFromFileTTF(path, 32.0f);
            return;
        }
    }
    io.Fonts->AddFontDefault();
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"CodeKlickerWindow";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Code Klicker", WS_OVERLAPPEDWINDOW, 100, 100,
                              1280, 800, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd)
        return 1;

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, hInstance);
        MessageBoxW(nullptr, L"DirectX 11 konnte nicht gestartet werden.", L"Code Klicker",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ApplyStyle();
    LoadFont();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    // ---- Testschalter ----------------------------------------------------
    //
    // Solange das hier an ist, wird das Geld in jedem Bild wieder aufgefuellt:
    // kaufen kostet damit nichts. Fuer das fertige Spiel auf false.
    //
    // Achtung: Geld ist ein int, mehr als rund 2 Milliarden passen da nicht
    // rein. Eine groessere Zahl laeuft ueber und wird negativ - deshalb die
    // Milliarde und nicht mehr.
    const bool kUnendlichGeld = true;
    const int  kVielGeld      = 2000000000;  // knapp unter der int-Grenze

    // Startgeld. Beim Zuruecksetzen kommt genau das wieder.
    const int kStartGeld = kUnendlichGeld ? kVielGeld : 100000;

    // Welche Erze es gibt, steht in data/erze.json.
    const OrePlan ores = LoadOrePlan();

    World world;
    world.money = kStartGeld;

    // EIN Motor fuer alle Konsolen: sie sind zusammen ein Programm.
    Native engine;

    // Was wann freigeschaltet werden KANN, steht in data/skills.txt - nicht
    // hier. Der Baum waechst dann beim Spielen: erst beim Kauf wird gewuerfelt,
    // was dahinter kommt.
    const SkillPlan plan = LoadSkillPlan();

    SkillTree tree;
    tree.start(plan, 20260808u);

    Page page = Page::Welt;

    std::vector<std::unique_ptr<Console>> consoles;
    int                                   nextId = 1;

    auto addConsole = [&]()
    {
        const float offset = (float)((nextId - 1) % 6) * 26.0f;
        consoles.push_back(std::make_unique<Console>(nextId, ImVec2(40.0f + offset, 60.0f + offset),
                                                     nextId == 1));
        ++nextId;
    };
    addConsole();

    // Alles auf Anfang - und der alte Spielstand weg.
    auto resetAll = [&]()
    {
        engine.stop();
        DeleteSave();

        world = World();
        world.money = kStartGeld;

        tree.start(plan, 20260808u);

        consoles.clear();
        nextId = 1;
        addConsole();

        page = Page::Welt;
    };

    // Gab es schon einen Spielstand? Dann ersetzt er den frischen Anfang.
    LoadGame(world, tree, consoles, nextId);

    // Gespeichert wird nicht bei jeder Kleinigkeit, sondern alle paar Sekunden
    // und beim Beenden. Geld aendert sich staendig - jedes Mal auf die Platte
    // zu schreiben waere Unsinn.
    float saveTimer = 0.0f;

    // Sammelt den Inhalt aller Konsolen ein - daraus wird ein Programm.
    auto collectSources = [&consoles]()
    {
        std::vector<SourceFile> files;
        for (const auto& c : consoles)
            files.push_back({c->id, c->editor.GetText()});
        return files;
    };

    bool running = true;
    while (running)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0u, 0u, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                running = false;
        }
        if (!running)
            break;

        if (g_resizeW != 0 && g_resizeH != 0)
        {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = 0;
            g_resizeH = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        const float dt = ImGui::GetIO().DeltaTime;

        // Das Level entscheidet, welche Erze ueberhaupt vorkommen. Woraus es
        // sich ergibt, steht in data/erze.json.
        if (ores.levelFrom == "skills")
        {
            int gekauft = 0;
            for (const SkillNode& n : tree.nodes)
                if (n.owned)
                    ++gekauft;
            world.level = gekauft;  // die Wurzel gehoert einem schon: Level 1
        }
        else
        {
            world.level = 1 + world.minedCount / ores.perLevel;
        }

        // Abgebaut wird nur, solange das Programm laeuft.
        //
        // Pause haelt den Abbau an der Stelle an, Stopp bricht ihn ab. Sonst
        // wuerde der Block auch dann noch fertig abgebaut, wenn man laengst
        // gestoppt hat - es sieht dann so aus, als holte das Spiel etwas nach.
        if (world.byHand)
        {
            // Von Hand angeklickt: das laeuft immer, auch ohne Programm.
            world.tickMining(dt, ores);
        }
        else
        {
            switch (engine.state())
            {
            case RunState::Paused: break;  // eingefroren
            case RunState::Idle: world.cancelMining(); break;
            default: world.tickMining(dt, ores); break;  // laeuft oder gerade fertig
            }
        }

        // Testschalter: Geld laeuft nie aus.
        if (kUnendlichGeld)
            world.money = kVielGeld;

        // Zeit vergeht auf jeder Seite: der Block waechst auch nach, waehrend
        // man im Skilltree ist.
        world.update(dt, ores);

        saveTimer += dt;
        if (saveTimer >= 10.0f)
        {
            SaveGame(world, tree, consoles);
            saveTimer = 0.0f;
        }

        // Die Werte kommen aus dem Baum - jedes Bild neu ausgerechnet. Dadurch
        // stimmen sie immer, egal in welcher Reihenfolge gekauft wurde.
        const Limits limits   = tree.limits();
        world.moneyPerBlock   = limits.moneyPerBlock;
        world.respawnSeconds  = limits.respawnSeconds;
        engine.setSpeed(limits.linesPerSecond);

        if (ImGui::BeginMainMenuBar())
        {
            if (PageTab("Welt", page == Page::Welt))
                page = Page::Welt;

            // Die Zahl im Reiter: dann sieht man auch von der Welt-Seite aus,
            // dass sich in der Tasche etwas angesammelt hat.
            char tasche[32];
            const int dabei = world.inventoryCount();
            if (dabei > 0)
                std::snprintf(tasche, sizeof(tasche), "Tasche (%d)", dabei);
            else
                std::snprintf(tasche, sizeof(tasche), "Tasche");

            if (PageTab(tasche, page == Page::Tasche))
                page = Page::Tasche;

            if (PageTab("Skilltree", page == Page::Skills))
                page = Page::Skills;

            // Diese Knoepfe gehoeren zur Welt-Seite.
            if (page == Page::Welt)
            {
                ImGui::Spacing();
                ImGui::TextDisabled("|");
                ImGui::Spacing();

                // Wie viele Konsolen erlaubt sind, steht im Skilltree.
                const bool moreAllowed = (int)consoles.size() < limits.maxConsoles;
                ImGui::BeginDisabled(!moreAllowed);
                if (ImGui::Button("+ Neue Konsole"))
                    addConsole();
                ImGui::EndDisabled();

                if (!moreAllowed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Mehr Konsolen gibt es im Skilltree.");
            }

            DrawMoney(world);
            ImGui::EndMainMenuBar();
        }

        // Das Programm laeuft weiter, egal welche Seite offen ist.
        engine.update(dt, world, ores);

        if (page == Page::Welt)
        {
            DrawWorld(world, ores);
            DrawStatus(limits, world.level);

            bool trigger = false;
            for (auto& c : consoles)
                if (DrawConsole(*c, engine))
                    trigger = true;

            if (trigger)
            {
                const bool anyEdited =
                    std::any_of(consoles.begin(), consoles.end(),
                                [](const std::unique_ptr<Console>& c) { return c->edited; });

                if (engine.state() == RunState::Paused && !anyEdited)
                {
                    engine.togglePause();
                }
                else if (engine.state() == RunState::Running)
                {
                    engine.togglePause();
                }
                else
                {
                    // Von vorne: alle Konsolen zusammen sind das Programm.
                    for (auto& c : consoles)
                        c->edited = false;

                    const std::vector<SourceFile> sources = collectSources();

                    // Erst der Skilltree, dann der Compiler. Wer etwas benutzt,
                    // das noch nicht gekauft ist, soll das sofort erfahren -
                    // und nicht erst nach einer halben Sekunde Kompilieren.
                    int         errConsole = 0;
                    int         errLine    = 0;
                    std::string problem    = CheckLimits(sources, limits, errConsole, errLine);

                    if (!problem.empty())
                        engine.fail(problem, errConsole, errLine);
                    else
                        engine.start(sources);
                }
            }
        }
        else if (page == Page::Tasche)
        {
            DrawInventory(world, ores);
        }
        else
        {
            if (DrawSkillPage(world, tree))
                resetAll();

            // Dieselbe Ecke wie auf der Welt-Seite: was man schon hat. Beim
            // Kaufen will man ja sehen, was man damit ueberhaupt schon kann.
            DrawStatus(limits, world.level);
        }

        // Geschlossene Konsolen entfernen
        for (std::size_t i = consoles.size(); i-- > 0;)
            if (!consoles[i]->open)
                consoles.erase(consoles.begin() + (long long)i);

        ImGui::Render();

        const float clear[4] = {0.075f, 0.082f, 0.102f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
        g_context->ClearRenderTargetView(g_renderTarget, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_swapChain->Present(1, 0);  // mit VSync
    }

    SaveGame(world, tree, consoles);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);
    return 0;
}

