// The launcher: the settings screen in a window of its own, before the game starts.
//
// It has its own small Direct3D 11 device and swap chain (the game's are not created until the
// guest boots), runs its own message loop, and returns when the player presses Play or quits. It
// takes the keyboard and mouse from its window and the pads from the host input layer, which
// pc_settings.c starts before this runs.

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern "C" {
#include "recomp_settings.h"
#include "input_host.h"
}
#include "pc_input.h"
#include "pc_settings.h"
#include "pc_ui.h"
#include "pc_ui_internal.h"

namespace {

ID3D11Device *g_dev;
ID3D11DeviceContext *g_ctx;
IDXGISwapChain *g_swap;
ID3D11RenderTargetView *g_rtv;
bool g_done;
int g_result;

void make_rtv()
{
    ID3D11Texture2D *buffer = nullptr;
    if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&buffer)))) {
        g_dev->CreateRenderTargetView(buffer, nullptr, &g_rtv);
        buffer->Release();
    }
}

void drop_rtv()
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

bool make_device(HWND window)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = window;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &level, &g_ctx);
    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &level, &g_ctx);
    if (FAILED(hr)) return false;
    IDXGIFactory *factory = nullptr;
    if (SUCCEEDED(g_swap->GetParent(IID_PPV_ARGS(&factory)))) {
        factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
    }
    make_rtv();
    return true;
}

// ---- the launcher's artwork ---------------------------------------------------------------------
//
// The banner and the logo are the game's own artwork, rendered from its textures and kept under
// branding/ (see branding/README.md). They are read from the source tree at run time, never built
// into the executable; a tree without them gets the launcher without pictures.

struct Art {
    ID3D11ShaderResourceView *srv = nullptr;
    int width = 0, height = 0;
};
Art g_banner, g_logo;

// branding/<relative> in the working directory (the source tree: pc_settings_init makes the folder
// that holds game\default.xbe current) or up to four folders above the executable.
bool find_branding(const wchar_t *relative, wchar_t *out, size_t size)
{
    wchar_t dir[MAX_PATH];
    if (_snwprintf(out, size, L"branding\\%s", relative) > 0 && GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES)
        return true;
    if (!GetModuleFileNameW(nullptr, dir, MAX_PATH)) return false;
    for (int up = 0; up <= 4; up++) {
        wchar_t *slash = wcsrchr(dir, L'\\');
        if (!slash) return false;
        *slash = 0;
        if (_snwprintf(out, size, L"%s\\branding\\%s", dir, relative) > 0 && GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES)
            return true;
    }
    return false;
}

// A PNG as an RGBA texture for ImGui, through Windows' own codecs.
bool load_art(const wchar_t *relative, Art &art)
{
    wchar_t path[MAX_PATH * 2];
    IWICImagingFactory *factory = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *converter = nullptr;
    UINT w = 0, h = 0;
    bool ok = false;
    if (!find_branding(relative, path, sizeof(path) / sizeof(path[0]))) return false;
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(converter->GetSize(&w, &h)) && w && h && w <= 8192 && h <= 8192) {
        std::vector<BYTE> pixels((size_t)w * h * 4);
        if (SUCCEEDED(converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data()))) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = w;
            desc.Height = h;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data = { pixels.data(), w * 4, 0 };
            ID3D11Texture2D *texture = nullptr;
            if (SUCCEEDED(g_dev->CreateTexture2D(&desc, &data, &texture))) {
                ok = SUCCEEDED(g_dev->CreateShaderResourceView(texture, nullptr, &art.srv));
                texture->Release();
            }
        }
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (SUCCEEDED(com)) CoUninitialize();
    if (ok) { art.width = (int)w; art.height = (int)h; }
    return ok;
}

void drop_art()
{
    if (g_banner.srv) { g_banner.srv->Release(); g_banner = Art(); }
    if (g_logo.srv) { g_logo.srv->Release(); g_logo = Art(); }
}

// RECOMP_WINDOW_SHOT=<file.bmp>: write the launcher's picture once it has settled (frame 30) and
// quit without starting the game -- a look at the launcher without a person at the keyboard, as
// pc_launcher_sdl.cpp offers off Windows. Local only; the picture holds the game's artwork.
bool write_shot(const char *path)
{
    ID3D11Texture2D *back = nullptr, *staging = nullptr;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE map;
    bool ok = false;
    if (FAILED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    if (SUCCEEDED(g_dev->CreateTexture2D(&desc, nullptr, &staging))) {
        g_ctx->CopyResource(staging, back);
        if (SUCCEEDED(g_ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
            FILE *f = fopen(path, "wb");
            if (f) {
                unsigned w = desc.Width, h = desc.Height, row = w * 4, size = 54 + row * h;
                unsigned char header[54] = { 'B', 'M' };
                memcpy(header + 2, &size, 4);
                header[10] = 54; header[14] = 40;
                memcpy(header + 18, &w, 4);
                int height = -(int)h;                               // top-down rows
                memcpy(header + 22, &height, 4);
                header[26] = 1; header[28] = 32;
                fwrite(header, 1, 54, f);
                std::vector<unsigned char> line(row);
                for (unsigned y = 0; y < h; y++) {
                    const unsigned char *src = (const unsigned char *)map.pData + (size_t)y * map.RowPitch;
                    for (unsigned x = 0; x < w; x++) {                  // RGBA -> BGRA
                        line[x * 4 + 0] = src[x * 4 + 2];
                        line[x * 4 + 1] = src[x * 4 + 1];
                        line[x * 4 + 2] = src[x * 4 + 0];
                        line[x * 4 + 3] = 255;
                    }
                    fwrite(line.data(), 1, row, f);
                }
                ok = fclose(f) == 0;
            }
            g_ctx->Unmap(staging, 0);
        }
        staging->Release();
    }
    back->Release();
    return ok;
}

void drop_device()
{
    drop_art();
    drop_rtv();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

LRESULT CALLBACK launcher_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ui_lock();
    bool taken = ui_capture_message(h, msg, wp, lp);
    if (!taken && ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp)) {
        ui_unlock();
        return 1;
    }
    ui_unlock();
    if (taken) return 0;
    switch (msg) {
    case WM_SIZE:
        if (g_swap && wp != SIZE_MINIMIZED) {
            drop_rtv();
            g_swap->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
            make_rtv();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU) return 0;      // no menu on Alt
        break;
    case WM_DPICHANGED: {
        const RECT *r = (const RECT *)lp;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE:
        g_done = true;
        g_result = 0;
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

extern "C" int pc_launcher_run(void)
{
    typedef BOOL (WINAPI *SetDpiContext)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    SetDpiContext set = user32 ? (SetDpiContext)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext") : nullptr;
    if (set) set((HANDLE)(INT_PTR)-4);          // per-monitor aware: sharp text on a scaled desktop

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = launcher_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));   // IDC_ARROW
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));      // the exe's own icon (src/defjam.rc.in)
    wc.hIconSm = (HICON)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = L"DefJamRecompLauncher";
    RegisterClassExW(&wc);

    int dpi = 96;
    {
        HDC dc = GetDC(nullptr);
        if (dc) { dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc); }
    }
    RECT r = { 0, 0, MulDiv(1000, dpi, 96), MulDiv(720, dpi, 96) };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    RECT work;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        if (w > work.right - work.left) w = work.right - work.left;
        if (h > work.bottom - work.top) h = work.bottom - work.top;
        x = work.left + (work.right - work.left - w) / 2;
        y = work.top + (work.bottom - work.top - h) / 2;
    }
    HWND window = CreateWindowExW(0, L"DefJamRecompLauncher", L"Def Jam: Fight for NY - Launcher",
                                  WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return 1;                       // no window: do not hold the game back
    if (!make_device(window)) {
        fprintf(stderr, "[UI] launcher: no Direct3D 11 device; starting the game\n");
        DestroyWindow(window);
        return 1;
    }

    ui_lock();
    ui_context_begin(window);
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    load_art(L"steam\\hero.png", g_banner);
    load_art(L"fan\\steam\\logo.png", g_logo);
    ui_set_launcher_art((ImTextureID)(intptr_t)g_banner.srv, (float)g_banner.width, (float)g_banner.height,
                        (ImTextureID)(intptr_t)g_logo.srv, (float)g_logo.width, (float)g_logo.height);
    fprintf(stderr, "[UI] launcher artwork: banner %s, logo %s\n", g_banner.srv ? "loaded" : "not found",
            g_logo.srv ? "loaded" : "not found");
    ui_unlock();
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    fprintf(stderr, "[UI] launcher shown\n");
    fflush(stderr);

    g_done = false;
    g_result = 1;
    const char *shot = getenv("RECOMP_WINDOW_SHOT");
    const char *shot_at = getenv("RECOMP_WINDOW_SHOT_FRAME");     /* default 30 */
    int frames = 0, shot_frame = shot_at && atoi(shot_at) > 0 ? atoi(shot_at) : 30;
    while (!g_done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) { g_done = true; g_result = 0; }
        }
        if (g_done) break;
        if (IsIconic(window)) { Sleep(50); continue; }

        ui_lock();
        ui_apply_scale(window);
        ui_feed_gamepad();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGuiIO &io = ImGui::GetIO();
        UiResult res = ui_draw(UI_LAUNCHER, 0, 0, io.DisplaySize.x, io.DisplaySize.y);
        ImGui::Render();
        const float clear[4] = { 0.055f, 0.055f, 0.065f, 1.0f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        ui_unlock();
        if (shot && *shot && ++frames == shot_frame) {
            fprintf(stderr, "[UI] launcher picture %s %s\n", write_shot(shot) ? "written to" : "could not be written to", shot);
            g_done = true;
            g_result = 0;
            res.play = res.leave = false;
        }
        g_swap->Present(1, 0);
        if (res.play) { g_done = true; g_result = 1; }
        if (res.leave) { g_done = true; g_result = 0; }
    }

    ui_lock();
    ui_set_launcher_art(0, 0, 0, 0, 0, 0);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ui_context_end();
    ui_unlock();
    drop_device();
    DestroyWindow(window);
    UnregisterClassW(L"DefJamRecompLauncher", GetModuleHandleW(nullptr));
    fprintf(stderr, "[UI] launcher closed: %s\n", g_result ? "play" : "quit");
    fflush(stderr);
    return g_result;
}
