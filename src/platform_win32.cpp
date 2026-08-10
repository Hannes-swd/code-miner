// Fenster (Win32) + Grafik (DirectX 11). Beides gehoert zu Windows, es wird
// also nichts weiter installiert. Fuer Linux siehe platform_glfw.cpp.

#include "platform.h"

#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>

#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{

ID3D11Device*           g_device       = nullptr;
ID3D11DeviceContext*    g_context      = nullptr;
IDXGISwapChain*         g_swapChain    = nullptr;
ID3D11RenderTargetView* g_renderTarget = nullptr;
UINT                    g_resizeW      = 0;
UINT                    g_resizeH      = 0;

HWND      g_hwnd = nullptr;
HINSTANCE g_inst = nullptr;
WNDCLASSEXW g_class{};

void CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer)
    {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
        backBuffer->Release();
    }
}

void CleanupRenderTarget()
{
    if (g_renderTarget)
    {
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }
}

bool CreateDeviceD3D(HWND hwnd)
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

void CleanupDeviceD3D()
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

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
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

bool g_running = true;

}  // namespace

namespace plat
{

bool Init(const char* title, int width, int height)
{
    ImGui_ImplWin32_EnableDpiAwareness();

    g_inst = GetModuleHandleW(nullptr);

    g_class.cbSize        = sizeof(g_class);
    g_class.style         = CS_CLASSDC;
    g_class.lpfnWndProc   = WndProc;
    g_class.hInstance     = g_inst;
    g_class.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    g_class.lpszClassName = L"CodeKlickerWindow";
    RegisterClassExW(&g_class);

    // Der Titel kommt als UTF-8 an und muss fuer Windows nach UTF-16.
    wchar_t wide[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wide, 255);

    g_hwnd = CreateWindowW(g_class.lpszClassName, wide, WS_OVERLAPPEDWINDOW, 100, 100, width,
                           height, nullptr, nullptr, g_inst, nullptr);
    if (!g_hwnd)
        return false;

    if (!CreateDeviceD3D(g_hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClassW(g_class.lpszClassName, g_inst);
        MessageBoxW(nullptr, L"DirectX 11 could not be started.", L"Code Miner",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    g_running = true;
    return true;
}

void LoadFont()
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

bool BeginFrame()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0u, 0u, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT)
            g_running = false;
    }
    if (!g_running)
        return false;

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
    return true;
}

void EndFrame(ImVec4 grund)
{
    ImGui::Render();

    const float clear[4] = {grund.x, grund.y, grund.z, 1.0f};
    g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
    g_context->ClearRenderTargetView(g_renderTarget, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    g_swapChain->Present(1, 0);  // mit VSync
}

void Shutdown()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    if (g_hwnd != nullptr)
        DestroyWindow(g_hwnd);
    UnregisterClassW(g_class.lpszClassName, g_inst);
}

}  // namespace plat
