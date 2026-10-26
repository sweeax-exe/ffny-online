// The online screens inside the game. See online_screen.h.
//
// The game's main menu has an ONLINE entry in its seventh slot (scripts/apt-patch.py): the menu
// script reports the cursor (Online.Focus(n)) and the choice (Online.Open()) through FuncCall.
// The menu draws nothing in that slot, so this draws the ONLINE poster EA left in the menu's
// assets and, while the slot has the cursor, the door's title and text. Choosing it opens the
// online screens over the menu: the hub (quick match, room list, a room of your own), the room
// (players with their portraits, ready, the host's settings), the fighter choice (the game's
// portrait grid) and the venue vote (the venues' photos and name banners). They are laid out in
// the menu's own 640x480 space, wherever the game draws its menus in the window, and are driven
// by the pad (A choose, B back, X and Y as the hints say), the keyboard or the mouse.
//
// Art and fonts come from <data>/online-art (scripts/online-art.py, from the player's own dump);
// without them the screens still work, with plain shapes and the overlay's font.
#define _CRT_SECURE_NO_WARNINGS
#if defined(_WIN32)
#include <windows.h>
#include <wincodec.h>
#include <d3d11.h>
#endif
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "online_screen.h"
#include "netplay.h"
#include "netplay_servers.h"
#include "voice.h"
#include "account.h"
#include "online_data.h"
#include "pc_settings.h"
#include "recomp_settings.h"
#include "updater.h"

namespace {

// ---- what the menu reports (game thread) ----------------------------------------------------

std::atomic<int> g_menu_up{0};      // the main menu is the current screen
std::atomic<int> g_focus{-1};       // the slot under the cursor, -1 unknown
std::atomic<int> g_open_req{0};
std::atomic<int> g_menu_ticks{0};   // Online.Tick() since the menu came up: it is on screen
std::atomic<long long> g_last_tick_ms{0};
bool g_closed_for_match = false;    // overlay thread: the last close was MATCH FOUND running out

long long now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

extern "C" void online_menu_on_funccall(const char *text)
{
    if (!strncmp(text, "Control.GetScreenInfo(", 22)) {
        int up = strstr(text, "intMain/mainMenu") != nullptr;
        // A hub match's summary leads to the battle match-type screen: the room comes back
        // instead of that menu (the match is still "connected" at this point; automatch then
        // returns the session to the room).
        if (strstr(text, "battle/cmtype") && netplay_in_lobby() && netplay_state() == NETPLAY_CONNECTED)
            g_open_req = 1;
        {   // tests: RECOMP_TEST_OPEN_ONLINE=1 opens the online screens at the first main menu
            static int once;
            if (up && !once && getenv("RECOMP_TEST_OPEN_ONLINE")) { once = 1; g_open_req = 1; }
        }
        g_menu_up = up;
        g_focus = -1;
        g_menu_ticks = 0;
    } else if (!strncmp(text, "Online.Focus(", 13)) {
        g_focus = atoi(text + 13);
    } else if (!strcmp(text, "Online.Tick()")) {
        g_menu_ticks++;
        g_last_tick_ms = now_ms();
    } else if (!strcmp(text, "Online.Open()")) {
        g_open_req = 1;
    }
}

#if defined(_WIN32)

namespace {

const int ONLINE_SLOT = 6;

// ---- art ------------------------------------------------------------------------------------

ID3D11Device *g_dev = nullptr;

struct Tex {
    ID3D11ShaderResourceView *srv = nullptr;
    float w = 0, h = 0;
    bool tried = false;
};
std::map<std::string, Tex> g_tex;

std::wstring art_root()
{
    const char *data = getenv("DEFJAM_DATA");
    std::wstring root;
    if (data && *data) {
        wchar_t wide[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, data, -1, wide, MAX_PATH);
        root = wide;
        if (!root.empty() && root.back() != L'\\' && root.back() != L'/') root += L'\\';
    }
    return root + L"online-art\\";
}

bool load_png(const std::wstring &path, Tex &t)
{
    IWICImagingFactory *factory = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *converter = nullptr;
    UINT w = 0, h = 0;
    bool ok = false;
    if (!g_dev || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(converter->GetSize(&w, &h)) && w && h && w <= 4096 && h <= 4096) {
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
            D3D11_SUBRESOURCE_DATA init = { pixels.data(), w * 4, 0 };
            ID3D11Texture2D *texture = nullptr;
            if (SUCCEEDED(g_dev->CreateTexture2D(&desc, &init, &texture))) {
                ok = SUCCEEDED(g_dev->CreateShaderResourceView(texture, nullptr, &t.srv));
                texture->Release();
            }
        }
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (SUCCEEDED(com)) CoUninitialize();
    if (ok) { t.w = (float)w; t.h = (float)h; }
    return ok;
}

const Tex &tex(const std::string &rel)
{
    Tex &t = g_tex[rel];
    if (!t.tried) {
        t.tried = true;
        std::wstring path = art_root();
        for (char c : rel) path += (wchar_t)(c == '/' ? '\\' : c);
        load_png(path, t);
    }
    return t;
}

const Tex &fighter_tex(int id) { return tex("fighters/" + std::to_string(id) + ".png"); }
const Tex &venue_tex(int fe) { return tex("venues/" + std::to_string(fe) + ".png"); }
const Tex &venue_name_tex(int fe) { return tex("venues/" + std::to_string(fe) + "_name.png"); }

const char *mode_logo(int type)
{
    switch (type) {
    case 0: return "modes/oneOnOne.png";
    case 1: return "modes/team.png";
    case 2: case 3: return "modes/ffa.png";
    case 6: return "modes/demolition.png";
    case 7: return "modes/inferno.png";
    case 8: return "modes/subway.png";
    case 9: return "modes/window.png";
    case 10: return "modes/cage.png";
    case 11: return "modes/ringOut.png";
    }
    return "";
}

// ---- fonts ------------------------------------------------------------------------------------

ImFont *g_title = nullptr, *g_hand = nullptr, *g_body = nullptr, *g_cond = nullptr, *g_stencil = nullptr;
bool g_fonts_tried = false;

ImFont *add_font(const char *file)
{
    std::wstring wpath = art_root() + L"fonts\\";
    for (const char *c = file; *c; c++) wpath += (wchar_t)*c;
    if (GetFileAttributesW(wpath.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
    char path[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, path, sizeof(path), nullptr, nullptr);
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, 24.0f, &cfg);
}

ImFont *font_or_default(ImFont *f) { return f ? f : ImGui::GetFont(); }

// ---- the menu's 640x480 space in the window --------------------------------------------------

struct Frame {
    ImVec2 o;
    float sx = 1, sy = 1;
    ImVec2 p(float x, float y) const { return ImVec2(o.x + x * sx, o.y + y * sy); }
    float s() const { return sy; }
};

// The picture is 4:3, or 16:9 with the menus drawn three quarters as wide (pc_settings.c), so a
// menu is 4:3 inside it; with "fill the window" the picture is stretched to the window.
Frame menu_frame(ImVec2 display)
{
    int keep = pc_display("aspect", 0) == 0, wide = pc_widescreen_latch();
    float num = wide ? 16.0f : 4.0f, den = wide ? 9.0f : 3.0f;
    float px = 0, py = 0, pw = display.x, ph = display.y;
    if (keep && pw > 0 && ph > 0) {
        if (pw * den > ph * num) { float w = ph * num / den; px = (pw - w) / 2; pw = w; }
        else { float h = pw * den / num; py = (ph - h) / 2; ph = h; }
    }
    float mw = pw * (4.0f / 3.0f) / (num / den);
    Frame f;
    f.o = ImVec2(px + (pw - mw) / 2, py);
    f.sx = mw / 640.0f;
    f.sy = ph / 480.0f;
    return f;
}

// ---- drawing helpers --------------------------------------------------------------------------

const ImU32 CREAM = IM_COL32(242, 230, 200, 255), ORANGE = IM_COL32(255, 168, 26, 255),
            WHITE = IM_COL32(245, 245, 245, 255), GREY = IM_COL32(150, 150, 150, 255),
            RED = IM_COL32(200, 16, 46, 255), GREEN = IM_COL32(110, 210, 110, 255),
            BLACK = IM_COL32(0, 0, 0, 255);

enum Align { LEFT, CENTER, RIGHT };

ImU32 with_alpha(ImU32 c, float a)
{
    unsigned al = (unsigned)(((c >> IM_COL32_A_SHIFT) & 0xFF) * (a < 0 ? 0 : a > 1 ? 1 : a));
    return (c & ~IM_COL32_A_MASK) | (al << IM_COL32_A_SHIFT);
}

// Text in the menu's space: `size` and `pos` in 640x480 units, with the black outline the game's
// own labels have.
void text(ImDrawList *dl, const Frame &f, ImFont *font, float size, float x, float y, ImU32 col, const char *s,
          Align align = LEFT, float outline = 1.2f, float wrap = 0)
{
    font = font_or_default(font);
    float px = size * f.s();
    ImVec2 sz = font->CalcTextSizeA(px, FLT_MAX, wrap * f.sx, s);
    ImVec2 at = f.p(x, y);
    if (align == CENTER) at.x -= sz.x / 2;
    else if (align == RIGHT) at.x -= sz.x;
    if (outline > 0) {
        float o = outline * f.s();
        ImU32 shadow = with_alpha(BLACK, ((col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
        static const float d[8][2] = { {-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1} };
        for (auto &k : d)
            dl->AddText(font, px, ImVec2(at.x + k[0] * o, at.y + k[1] * o), shadow, s, nullptr, wrap * f.sx);
    }
    dl->AddText(font, px, at, col, s, nullptr, wrap * f.sx);
}

float text_width(const Frame &f, ImFont *font, float size, const char *s)
{
    return font_or_default(font)->CalcTextSizeA(size * f.s(), FLT_MAX, 0, s).x / f.sx;
}

bool image(ImDrawList *dl, const Frame &f, const Tex &t, float x, float y, float w, float h,
           ImU32 tint = IM_COL32_WHITE, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1))
{
    if (!t.srv) return false;
    dl->AddImage((ImTextureID)(intptr_t)t.srv, f.p(x, y), f.p(x + w, y + h), uv0, uv1, tint);
    return true;
}

// An image fitted inside a box, keeping its shape.
void image_fit(ImDrawList *dl, const Frame &f, const Tex &t, float x, float y, float w, float h, ImU32 tint = IM_COL32_WHITE)
{
    if (!t.srv || t.w <= 0 || t.h <= 0) return;
    float s = std::min(w / t.w, h / t.h), iw = t.w * s, ih = t.h * s;
    image(dl, f, t, x + (w - iw) / 2, y + (h - ih) / 2, iw, ih, tint);
}

void rect(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, ImU32 col, float round = 0)
{
    dl->AddRectFilled(f.p(x, y), f.p(x + w, y + h), col, round * f.s());
}

void frame_rect(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, ImU32 col, float thick = 1.5f)
{
    dl->AddRect(f.p(x, y), f.p(x + w, y + h), col, 0, 0, thick * f.s());
}

// The red dashed bracket the game puts around the chosen item; a red frame without the art.
void selector(ImDrawList *dl, const Frame &f, float x, float y, float w, float h)
{
    const Tex &t = tex("ui/selector.png");
    if (!t.srv || t.w <= 0) {
        frame_rect(dl, f, x - 3, y - 3, w + 6, h + 6, RED, 2.5f);
        return;
    }
    // The bracket's solid lines are pixels 23..80 x 21..103 of the 128x128 texture, the glow a
    // few pixels around them. Nine slices, so the corners keep their shape on a wide item: the
    // source box with its glow (17..86 x 15..109) maps onto the item grown by the same margin.
    const float S0x = 17, S1x = 86, S0y = 15, S1y = 109, LX = 23 - 17, LY = 21 - 15, C = 16;
    float k = std::min(w / (80 - 23), h / (103 - 21));
    k = k < 0.5f ? 0.5f : k > 1.1f ? 1.1f : k;
    float X0 = x - LX * k, Y0 = y - LY * k, X1 = x + w + LX * k, Y1 = y + h + LY * k, c = C * k;
    float xs[4] = { X0, X0 + c, X1 - c, X1 }, ys[4] = { Y0, Y0 + c, Y1 - c, Y1 };
    float us[4] = { S0x, S0x + C, S1x - C, S1x }, vs[4] = { S0y, S0y + C, S1y - C, S1y };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            dl->AddImage((ImTextureID)(intptr_t)t.srv, f.p(xs[i], ys[j]), f.p(xs[i + 1], ys[j + 1]),
                         ImVec2(us[i] / t.w, vs[j] / t.h), ImVec2(us[i + 1] / t.w, vs[j + 1] / t.h));
}

// A button glyph and its word, as in the game's header ("A SELECT").
float hint(ImDrawList *dl, const Frame &f, float x, float y, const char *button, const char *word)
{
    std::string name = std::string("ui/button_") + button + ".png";
    const Tex &t = tex(name);
    if (button[0] && button[1] && !t.srv) {     // "rb", "lb": a small plate with both letters
        char up[3] = { (char)(button[0] - 32), (char)(button[1] - 32), 0 };
        rect(dl, f, x - 2, y + 1, 22, 14, IM_COL32(70, 70, 70, 255), 4);
        text(dl, f, g_body, 10, x + 9, y + 2, WHITE, up, CENTER, 0);
        text(dl, f, g_title, 13, x + 24, y + 1, WHITE, word, LEFT, 1.0f);
        return 24 + text_width(f, g_title, 13, word) + 12;
    }
    if (!image(dl, f, t, x, y, 16, 16)) {
        dl->AddCircleFilled(f.p(x + 8, y + 8), 8 * f.s(), button[0] == 'a' ? GREEN : button[0] == 'b' ? RED :
                            button[0] == 'x' ? IM_COL32(60, 120, 230, 255) : IM_COL32(230, 200, 40, 255));
        char up[2] = { (char)(button[0] - 32), 0 };
        text(dl, f, g_body, 11, x + 8, y + 2, WHITE, up, CENTER, 0);
    }
    text(dl, f, g_title, 13, x + 20, y + 1, WHITE, word, LEFT, 1.0f);
    return 20 + text_width(f, g_title, 13, word) + 12;
}

// The page title in the corner, as the game writes MAIN MENU.
void page_title(ImDrawList *dl, const Frame &f, const char *title)
{
    text(dl, f, g_stencil, 30, 50, 24, CREAM, title, LEFT, 1.6f);
    // A newer build stays announced on every online page, top right, until the player updates.
    int build = 0;
    if (updater_state() == UPDATER_AVAILABLE) updater_info(&build, nullptr, 0, nullptr, 0);
    if (build > updater_local_build()) {
        char line[96];
        snprintf(line, sizeof(line), "UPDATE: BUILD %d IS OUT", build);
        float w = text_width(f, g_title, 12, line);
        rect(dl, f, 606 - w - 10, 8, w + 20, 20, IM_COL32(150, 14, 30, 230), 4);
        text(dl, f, g_title, 12, 606, 11, WHITE, line, RIGHT, 1.0f);
        text(dl, f, g_hand, 11, 606, 30, ORANGE, "close the game: the launcher installs it", RIGHT, 0.8f);
    }
}

// The lit rolling shutter in the menu's door (the door art itself is see-through there).
void door_shutter(ImDrawList *dl, const Frame &f, ImU32 tint = IM_COL32_WHITE)
{
    const Tex &shutter = tex("ui/door_shutter.png");
    if (!image(dl, f, shutter, 350, 106, 226, 366, tint, ImVec2(0, 0), ImVec2(0.94f, 1)))
        rect(dl, f, 350, 106, 226, 366, IM_COL32(40, 38, 26, 255));
}

// Without the launcher's pictures, behind the lists: the game's own main menu, which keeps running under the overlay (wall, lamp,
// the door), with its poster wall and title covered and its door's text hidden by the shutter.
// The room, the fighter choice and the vote use the whole picture instead (`full`).
void plain_backdrop(ImDrawList *dl, const Frame &f, ImVec2 display, bool full)
{
    if (full) {
        dl->AddRectFilled(ImVec2(0, 0), display, IM_COL32(4, 4, 4, 255));   // opaque: no menu showing through
        return;
    }
    // outside the menu's 4:3 (widescreen bars, a stretched picture's edges) stays as it is
    // opaque: the menu's title, its posters and the song captions must not show through
    dl->AddRectFilled(f.p(0, 0), f.p(330, 480), IM_COL32(4, 4, 4, 255));
    dl->AddRectFilledMultiColor(f.p(330, 0), f.p(352, 480), IM_COL32(4, 4, 4, 255), IM_COL32(4, 4, 4, 0),
                                IM_COL32(4, 4, 4, 0), IM_COL32(4, 4, 4, 255));
    door_shutter(dl, f);
}

// The door's text, laid out as the game lays out STORY or OPTIONS: an icon, the title, an orange
// subtitle and a few lines in capitals.
void door_text(ImDrawList *dl, const Frame &f, const Tex *icon, const char *title, const char *sub, const char *desc)
{
    if (icon) image_fit(dl, f, *icon, 400, 140, 110, 100, IM_COL32(255, 255, 255, 235));
    if (title) text(dl, f, g_title, 32, 463, 236, CREAM, title, CENTER, 1.8f);
    if (sub) text(dl, f, g_hand, 17, 463, 274, ORANGE, sub, CENTER, 1.2f);
    if (desc) {
        std::string up(desc);
        for (char &c : up) c = (char)toupper((unsigned char)c);
        text(dl, f, g_body, 17, 372, 304, WHITE, up.c_str(), LEFT, 1.2f, 182);
    }
}

// A fighter's portrait filling a box: the top of the picture, at its own proportions.
bool portrait(ImDrawList *dl, const Frame &f, int id, float x, float y, float w, float h, ImU32 tint = IM_COL32_WHITE)
{
    const Tex &t = fighter_tex(id);
    if (!t.srv || t.w <= 0 || t.h <= 0) return false;
    float v = std::min(1.0f, (t.w / t.h) / (w * f.sx / (h * f.sy)));
    return image(dl, f, t, x, y, w, h, tint, ImVec2(0, 0), ImVec2(1, v));
}

// ---- the launcher's look: its picture behind every page, glass panels ---------------------------
//
// The launcher's background, blurred (ffny-branding/launcher-bg-blur.jpg, shipped with the setup),
// covers the window behind the online pages: darkened, with a coloured light and a framing per page so
// the pages do not all look the same, a vignette, a little film grain and a few of the launcher's
// embers. It moves very slowly (a drift and a breath over a minute or so) and eases to the next
// page's framing. The content sits on glass panels as in the launcher: a dark translucent fill over
// the picture, a light top edge and a faint border, with corners that scale with the menu. Without
// the picture the pages keep the plain near-black background (and the game's door over the main
// menu), and the panels are the same dark glass over it. Nothing is blurred per frame: the light,
// the vignette and the grain are small textures made once.

Tex g_bg_blur;                      // the launcher's blurred picture (a texture of this device's own)
Tex g_light, g_vignette, g_grain;   // made here: a soft round light, the vignette, film grain
bool g_look_tried = false;

// ffny-branding/<file> in the working directory (the game's folder: the source tree, or the installed
// copy, where ffny-branding sits beside branding/) or up to four folders above the executable, as
// the launcher finds it.
bool find_ffny_branding(const wchar_t *file, std::wstring &out)
{
    out = std::wstring(L"ffny-branding\\") + file;
    if (GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    wchar_t dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    for (int up = 0; up <= 4; up++) {
        wchar_t *slash = wcsrchr(dir, L'\\');
        if (!slash) return false;
        *slash = 0;
        out = std::wstring(dir) + L"\\ffny-branding\\" + file;
        if (GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    }
    return false;
}

bool make_texture(const std::vector<unsigned char> &rgba, int w, int h, Tex &t)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = { rgba.data(), (UINT)w * 4, 0 };
    ID3D11Texture2D *texture = nullptr;
    if (!g_dev || FAILED(g_dev->CreateTexture2D(&desc, &init, &texture))) return false;
    bool ok = SUCCEEDED(g_dev->CreateShaderResourceView(texture, nullptr, &t.srv));
    texture->Release();
    if (ok) { t.w = (float)w; t.h = (float)h; }
    return ok;
}

void load_look()
{
    if (g_look_tried || !g_dev) return;
    g_look_tried = true;
    std::wstring path;
    if (find_ffny_branding(L"launcher-bg-blur.jpg", path)) load_png(path, g_bg_blur);
    // a soft round light: white, its alpha falling off like a Gaussian
    const int L = 128;
    std::vector<unsigned char> px((size_t)L * L * 4);
    for (int y = 0; y < L; y++)
        for (int x = 0; x < L; x++) {
            float dx = (x + 0.5f) / L * 2 - 1, dy = (y + 0.5f) / L * 2 - 1, d2 = dx * dx + dy * dy;
            float a = d2 >= 1 ? 0 : expf(-d2 * 3.2f) * (1 - d2);
            unsigned char *p = &px[((size_t)y * L + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = (unsigned char)(a * 255 + 0.5f);
        }
    make_texture(px, L, L, g_light);
    // the vignette: black, clear in the middle, darker toward the corners
    const int V = 256;
    px.assign((size_t)V * V * 4, 0);
    for (int y = 0; y < V; y++)
        for (int x = 0; x < V; x++) {
            float dx = (x + 0.5f) / V * 2 - 1, dy = (y + 0.5f) / V * 2 - 1;
            float d = sqrtf(dx * dx * 0.85f + dy * dy * 1.0f);
            float a = d < 0.45f ? 0 : std::min(1.0f, (d - 0.45f) / 0.95f);
            a = a * a * (3 - 2 * a);
            px[((size_t)y * V + x) * 4 + 3] = (unsigned char)(a * 255 + 0.5f);
        }
    make_texture(px, V, V, g_vignette);
    // film grain: light and dark specks in equal measure, so it adds texture and no haze
    const int N = 256;
    px.assign((size_t)N * N * 4, 0);
    unsigned seed = 0x13579BDu;
    for (int i = 0; i < N * N; i++) {
        seed = seed * 1664525u + 1013904223u;
        float r = (float)(seed >> 8) * (1.0f / 16777216.0f);
        bool light = r > 0.5f;
        float a = fabsf(r - 0.5f) * 2;
        unsigned char *p = &px[(size_t)i * 4];
        p[0] = p[1] = p[2] = light ? 255 : 0;
        p[3] = (unsigned char)(a * a * 255);
    }
    make_texture(px, N, N, g_grain);
    fprintf(stderr, "[ONLINE] launcher picture %s\n", g_bg_blur.srv ? "found" : "missing: the plain background");
}

// How a page frames and lights the picture.
struct Look {
    float fx, fy;       // the point of the picture (0..1) brought to the middle of the window
    float zoom;         // over the size that just covers the window
    float dim;          // black over the picture, 0..1
    float r, g, b;      // the light's colour (0..1)
    float lx, ly;       // its centre, as fractions of the window
    float size;         // its radius, in window heights
    float strength;     // its opacity
};

enum LookId { LOOK_HUB, LOOK_LIST, LOOK_ROOM, LOOK_HILL, LOOK_UP, LOOK_WATCH, LOOK_FIGHTERS, LOOK_VOTE,
              LOOK_STATS, LOOK_PLAYERS, LOOK_VOICE, LOOK_SEARCH, LOOK_OFFLINE, LOOK_MATCH };

// The picture is a fighter's chest in the dark: the jaw and an earring at the top, the chain and a
// gloved fist in the middle, a red watch below, a tattooed arm on the right.
const Look k_looks[] = {
    //  fx     fy    zoom  dim    r     g     b     lx    ly     size  strength
    { 0.55f, 0.45f, 1.10f, 0.30f, 1.00f, 0.40f, 0.06f, 0.92f, 1.00f, 1.15f, 0.48f },  // hub: amber from below right
    { 0.30f, 0.55f, 1.25f, 0.32f, 1.00f, 0.48f, 0.10f, 0.88f, 0.95f, 1.05f, 0.42f },  // room list, code, create
    { 0.52f, 0.58f, 1.30f, 0.30f, 0.90f, 0.08f, 0.16f, 0.95f, 0.95f, 1.20f, 0.55f },  // a room: crimson
    { 0.60f, 0.40f, 1.20f, 0.25f, 0.90f, 0.10f, 0.05f, 0.92f, 1.05f, 1.30f, 0.62f },  // the hill: red rising (the mock-up)
    { 0.45f, 0.62f, 1.45f, 0.28f, 1.00f, 0.12f, 0.05f, 0.70f, 1.05f, 1.40f, 0.70f },  // you're up: the fist, hot red
    { 0.78f, 0.30f, 1.35f, 0.32f, 0.25f, 0.45f, 1.00f, 0.05f, 0.05f, 1.10f, 0.42f },  // watching: a cold broadcast light
    { 0.64f, 0.18f, 1.55f, 0.34f, 1.00f, 0.32f, 0.08f, 0.15f, 1.05f, 1.15f, 0.46f },  // fighter choice: the earring
    { 0.32f, 0.45f, 1.20f, 0.32f, 1.00f, 0.60f, 0.18f, 0.50f, -0.12f, 1.10f, 0.40f }, // venue vote: light from above
    { 0.68f, 0.78f, 1.50f, 0.32f, 1.00f, 0.74f, 0.22f, 0.08f, 0.00f, 1.10f, 0.42f },  // my stats: the watch, gold
    { 0.90f, 0.55f, 1.40f, 0.32f, 0.55f, 0.30f, 1.00f, 1.00f, 0.30f, 1.05f, 0.42f },  // players: the tattoo, violet
    { 0.95f, 0.38f, 1.65f, 0.32f, 0.60f, 0.25f, 1.00f, 0.95f, 0.25f, 0.95f, 0.45f },  // voice chat: the purple light
    { 0.50f, 0.30f, 1.25f, 0.30f, 1.00f, 0.42f, 0.08f, 0.85f, 1.00f, 1.20f, 0.50f },  // ranked search
    { 0.40f, 0.12f, 1.15f, 0.40f, 0.70f, 0.30f, 0.20f, 0.80f, 1.00f, 1.00f, 0.30f },  // connecting, offline
    { 0.50f, 0.40f, 1.08f, 0.22f, 1.00f, 0.25f, 0.05f, 0.50f, 1.10f, 1.45f, 0.65f },  // match found
};

Look g_look;            // the look shown, easing toward the page's
bool g_look_set = false;

float ease_to(float cur, float target, float k) { return cur + (target - cur) * k; }

// The picture behind a page, covering `a`..`b` of the window (the whole window, or the menu's left
// part over the main menu); `display` is the window, which the framing refers to.
void picture(ImDrawList *dl, ImVec2 display, ImVec2 a, ImVec2 b, LookId id)
{
    const Look &want = k_looks[id];
    float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f), k = 1.0f - expf(-dt * 4.0f);
    if (!g_look_set) { g_look = want; g_look_set = true; }
    float *cur = &g_look.fx;
    const float *to = &want.fx;
    for (int i = 0; i < (int)(sizeof(Look) / sizeof(float)); i++) cur[i] = ease_to(cur[i], to[i], k);
    const Look &L = g_look;
    float t = (float)ImGui::GetTime(), W = display.x, H = display.y;
    dl->AddRectFilled(a, b, IM_COL32(4, 4, 4, 255));
    if (!g_bg_blur.srv || g_bg_blur.w <= 0 || W <= 0 || H <= 0) return;
    // cover the window, the focus in the middle as far as the picture allows; a slow drift and breath
    float zoom = L.zoom * (1.0f + 0.03f * (0.5f - 0.5f * cosf(t * 6.2832f / 64.0f)));
    float cover = std::max(W / g_bg_blur.w, H / g_bg_blur.h) * zoom;
    float pw = g_bg_blur.w * cover, ph = g_bg_blur.h * cover;
    float fx = L.fx + 0.015f * sinf(t * 6.2832f / 83.0f), fy = L.fy + 0.012f * cosf(t * 6.2832f / 97.0f);
    float x0 = std::min(0.0f, std::max(W - pw, W * 0.5f - fx * pw));
    float y0 = std::min(0.0f, std::max(H - ph, H * 0.5f - fy * ph));
    ImVec2 uv0((a.x - x0) / pw, (a.y - y0) / ph), uv1((b.x - x0) / pw, (b.y - y0) / ph);
    dl->AddImage((ImTextureID)(intptr_t)g_bg_blur.srv, a, b, uv0, uv1);
    dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, (int)(L.dim * 255)));
    // the coloured light, then the vignette and the grain over everything
    if (g_light.srv) {
        ImVec2 c(L.lx * W, L.ly * H);
        float r = L.size * H;
        dl->PushClipRect(a, b, true);
        dl->AddImage((ImTextureID)(intptr_t)g_light.srv, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(0, 0),
                     ImVec2(1, 1), ImGui::GetColorU32(ImVec4(L.r, L.g, L.b, L.strength)));
        dl->PopClipRect();
    }
}

// Over the page's background (and under its panels): the vignette and the grain.
void finish_picture(ImDrawList *dl, ImVec2 display, ImVec2 a, ImVec2 b)
{
    if (!g_bg_blur.srv) return;
    dl->PushClipRect(a, b, true);
    if (g_vignette.srv)
        dl->AddImage((ImTextureID)(intptr_t)g_vignette.srv, ImVec2(0, 0), display, ImVec2(0, 0), ImVec2(1, 1),
                     IM_COL32(0, 0, 0, 205));
    if (g_grain.srv) {
        // one texel a pixel, tiled (the overlay's sampler clamps); it shifts every few frames like film
        int frame = (int)(ImGui::GetTime() * 12.0);
        float ox = (float)((frame * 97) % 256), oy = (float)((frame * 61) % 256);
        for (float y = a.y - oy; y < b.y; y += g_grain.h)
            for (float x = a.x - ox; x < b.x; x += g_grain.w)
                dl->AddImage((ImTextureID)(intptr_t)g_grain.srv, ImVec2(x, y), ImVec2(x + g_grain.w, y + g_grain.h),
                             ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 34));
    }
    dl->PopClipRect();
}

// A glass panel in the menu's space, as the launcher's cards: a dark translucent tint over the blurred
// picture (the page's light and grain show through it, as frosted glass), a light line along the top
// that fades to the sides, and a faint light border. `tint` and `alpha` colour it (a selected row, the player's own card).
void glass(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, float round = 10,
           ImU32 tint = IM_COL32(10, 10, 13, 255), float alpha = 0.62f)
{
    ImVec2 a = f.p(x, y), b = f.p(x + w, y + h);
    float r = std::min(round * f.s(), std::min(b.x - a.x, b.y - a.y) * 0.5f);
    dl->AddRectFilled(a, b, with_alpha(tint, alpha), r);
    float mid = (a.x + b.x) / 2, t0 = a.y + 1.0f, t1 = t0 + std::max(1.0f, 0.7f * f.s());
    float inset = std::max(r, 4.0f);
    ImU32 hi = IM_COL32(255, 255, 255, 70), clear = IM_COL32(255, 255, 255, 0);
    if (mid - inset > a.x + inset) {
        dl->AddRectFilledMultiColor(ImVec2(a.x + inset, t0), ImVec2(mid, t1), clear, hi, hi, clear);
        dl->AddRectFilledMultiColor(ImVec2(mid, t0), ImVec2(b.x - inset, t1), hi, clear, clear, hi);
    }
    dl->AddRect(a, b, IM_COL32(255, 255, 255, 34), r, 0, std::max(1.0f, 0.55f * f.s()));
}

// A few of the launcher's embers rising on the right of the picture, slower and fainter than there.
struct Spark { float x, y, vx, vy, age, life, size, heat, phase; };

float spark_random()
{
    static unsigned seed = 0x5EED1234u;
    seed = seed * 1664525u + 1013904223u;
    return (float)(seed >> 8) * (1.0f / 16777216.0f);
}

void spark_spawn(Spark &p, float w, float h, float s, bool anywhere)
{
    p.x = w * (0.40f + 0.62f * spark_random());
    p.y = anywhere ? h * (0.25f + 0.75f * spark_random()) : h + 8 * s;
    p.vx = (2 + 7 * spark_random()) * s;
    p.vy = -(12 + 30 * spark_random()) * s;
    p.life = 5.0f + 7.0f * spark_random();
    p.age = anywhere ? p.life * spark_random() : 0.0f;
    p.size = (0.7f + 1.5f * spark_random() * spark_random()) * s;
    p.heat = spark_random();
    p.phase = 6.2832f * spark_random();
}

void sparks(ImDrawList *dl, ImVec2 display, ImVec2 a, ImVec2 b)
{
    static Spark e[48];
    static bool started;
    static ImVec2 seen;
    float w = display.x, h = display.y, s = h / 960.0f;
    if (!started || seen.x != w || seen.y != h) {
        for (Spark &p : e) spark_spawn(p, w, h, s, true);
        started = true;
        seen = display;
    }
    float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f), t = (float)ImGui::GetTime();
    dl->PushClipRect(a, b, true);
    for (Spark &p : e) {
        p.age += dt;
        p.x += (p.vx + sinf(t * 1.1f + p.phase) * 7 * s) * dt;
        p.y += p.vy * dt;
        if (p.age >= p.life || p.y < -10 * s || p.x > w + 10 * s) { spark_spawn(p, w, h, s, false); continue; }
        float k = p.age / p.life;
        float al = std::min(1.0f, p.age / 0.8f) * (k > 0.55f ? (1.0f - k) / 0.45f : 1.0f);
        al *= (0.7f + 0.3f * sinf(t * 9.0f + p.phase * 3.0f)) * 0.75f;
        float cool = k * 0.6f + (1.0f - p.heat) * 0.4f;
        ImVec4 c(1.0f, 0.78f - 0.45f * cool, 0.38f - 0.32f * cool, 1.0f);
        ImVec2 q(p.x, p.y);
        dl->AddCircleFilled(q, p.size * 4.0f, ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, 0.06f * al)), 10);
        dl->AddCircleFilled(q, p.size * 1.8f, ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, 0.20f * al)), 8);
        dl->AddCircleFilled(q, p.size, ImGui::GetColorU32(ImVec4(1.0f, 0.92f - 0.4f * cool, 0.62f - 0.4f * cool, al)), 6);
    }
    dl->PopClipRect();
}

// Behind the pages. The full pages (the room, the fighter choice, the vote, the stats, and every page
// over a screen other than the main menu) get the whole window. Over the game's own main menu (the
// hub, the room list, the code, a new room's settings, voice chat while not in a room) the picture
// covers the window too, and the door's text, which the game's door used to frame, sits on a glass
// panel where the door was: the game's wall and door beside the launcher's picture looked like two
// screens at once (docs/launcher-and-overlay.md, "The online screens' look"). Without the picture it
// is the plain background of before, the game's door included.
bool g_game_door = false;   // this frame shows the game's own door (no picture, a page over the main menu)

void backdrop(ImDrawList *dl, const Frame &f, ImVec2 display, bool full, LookId id)
{
    load_look();
    g_game_door = !full && !g_bg_blur.srv;
    if (!g_bg_blur.srv) { plain_backdrop(dl, f, display, full); return; }
    ImVec2 a(0, 0), b = display;
    picture(dl, display, a, b, id);
    sparks(dl, display, a, b);
    finish_picture(dl, display, a, b);
}

// Where the game's door was: a glass panel for the door's text (unless the game's door shows).
void door_panel(ImDrawList *dl, const Frame &f)
{
    if (!g_game_door) glass(dl, f, 346, 84, 234, 360, 14, IM_COL32(10, 10, 13, 255), 0.58f);
}

// ---- input --------------------------------------------------------------------------------------

struct Pad { bool up, down, left, right, a, b, x, y, lb, rb; };

// Mouse: hovering a box picks it, a click chooses it.
bool mouse_in(const Frame &f, float x, float y, float w, float h)
{
    return ImGui::IsMouseHoveringRect(f.p(x, y), f.p(x + w, y + h), false);
}
bool mouse_click(const Frame &f, float x, float y, float w, float h)
{
    return mouse_in(f, x, y, w, h) && ImGui::IsMouseClicked(0);
}

std::string upper(const char *s)
{
    std::string out(s);
    for (char &c : out) c = (char)toupper((unsigned char)c);
    return out;
}

// ---- the screens --------------------------------------------------------------------------------

enum Page { HUB, ROOMS, SETTINGS, FIGHTERS, CODE, STATS, PLAYERS, RANKED, VOICE, SERVERS };

struct State {
    Page page = HUB;
    bool settings_edit = false;     // SETTINGS changes the room's (host) instead of a new one
    bool ranked_pick = false;       // FIGHTERS picks the fighter for a ranked search
    int hub_sel = 0, rooms_sel = 0, settings_sel = 0, fighter_sel = 0, fighter_top = 0, vote_sel = 0, voice_sel = 0;
    int quick_mode = 0;             // index into online_modes
    int stats_page = 0;             // MY STATS: the leaderboard page
    NetRoomSettings settings;
    bool settings_init = false;
    unsigned long long last_list = 0, start_seen = 0, opened = 0;
    std::string note;
    unsigned long long note_until = 0;
    unsigned room_version = 0;
    char code[7] = "AAAAAA";
    int code_pos = 0;
    uint32_t watch_hidden = 0;      // King of the Hill: the fight whose watching screen B closed
    // the server (netplay_servers.h)
    int servers_sel = 0;            // SERVERS: 0 Automatic, 1 Europe, 2 North America, 3 Both servers
    bool connect_pending = false;   // Automatic waits for the relays' first answers before connecting
    bool fallback_tried = false;    // Automatic: the other relay was tried once after a failed connection
    int auto_moves = 0;             // Both servers: quick matches moved to the other relay since QUICK MATCH
    bool quick_wanted = false;      // QUICK MATCH pressed: the next room is a quick room
    int last_quick = 0, last_quick_max = 2;     // that quick match's mode and players
    char quick_room[7] = "";        // the quick room this PC waits in (for the other relay's hint)
    unsigned long long alone_since = 0;
} S;

// ---- the test gallery ----------------------------------------------------------------------------
//
// RECOMP_TEST_ONLINE_GALLERY=<ms>: the online screens show every page in turn, <ms> each, with
// sample players, rooms, a hill and statistics instead of the server's, and take no input. It
// connects to nothing. With RECOMP_OVERLAY_SHOT_DIR the captures carry the page's name, so a
// design change can be looked at page by page (before and after) without three copies and a relay.
// The pages read the lobby through the wrappers below, which answer from the sample in the gallery.

struct Gallery {
    enum NetplayState state = NETPLAY_LOBBY;
    bool in_lobby = true;
    NetRoom room = {};
    std::vector<NetRoomInfo> rooms;
    std::vector<NetPlayer> players;
    int queue = NETQUEUE_SEARCHING;
    enum AccountState account = ACCOUNT_IN;
    std::string stats;
    const char *page = nullptr;     // the page shown, for the captures' names
    NetServerInfo servers[NETSERVER_COUNT] = {};
    int server_cur = NETSERVER_EU;  // the relay the sample hub is on
    enum NetServerChoice choice = NETCHOICE_AUTO;
    bool ranked = false;            // RANKED MATCH shown in the sample hub
    bool pending = false;           // the sample waits for the relays' first answers
    int room_pings[NETPLAY_ROOM_SLOTS] = {};
    std::vector<NetServerRoom> other_rooms;     // Both servers: the sample's rooms on North America
} G;

bool gallery_on()
{
    static int ms = -1;
    if (ms < 0) {
        const char *e = getenv("RECOMP_TEST_ONLINE_GALLERY");
        ms = e && atoi(e) > 0 ? atoi(e) : 0;
    }
    return ms > 0;
}

enum NetplayState net_state() { return gallery_on() ? G.state : netplay_state(); }
bool net_in_lobby() { return gallery_on() ? G.in_lobby : netplay_in_lobby() != 0; }
void lobby_room(NetRoom *r)
{
    if (gallery_on()) *r = G.room;
    else netplay_lobby_room(r);
}
int lobby_players(NetPlayer *out, int max, int *online, int *rooms)
{
    if (!gallery_on()) return netplay_lobby_players(out, max, online, rooms);
    int n = std::min(max, (int)G.players.size());
    for (int i = 0; out && i < n; i++) out[i] = G.players[i];
    if (online) *online = G.servers[G.server_cur].online;
    if (rooms) *rooms = G.servers[G.server_cur].rooms;
    return n;
}
int lobby_queue(int *waited, int *searching, char *why, int size)
{
    if (!gallery_on()) return netplay_lobby_queue(waited, searching, why, size);
    *waited = 37;
    *searching = 4;
    if (size > 0) why[0] = 0;
    return G.queue;
}
int lobby_caps() { return gallery_on() ? NETPLAY_CAP_KOTH : netplay_lobby_caps(); }
enum AccountState acc_state() { return gallery_on() ? G.account : account_state(); }
bool stats_busy() { return gallery_on() ? false : account_stats_busy() != 0; }
bool stats_text(char *out, int size)
{
    if (!gallery_on()) return account_stats(out, size) != 0;
    snprintf(out, (size_t)size, "%s", G.stats.c_str());
    return true;
}
void status_text(char *out, int size)
{
    if (gallery_on()) snprintf(out, (size_t)size, "Online as Ann. Ping 38 ms.");
    else netplay_status(out, size);
}

// The relays (Europe, North America): their state, the one the hub is on, the choice.
int servers(NetServerInfo *out)
{
    if (gallery_on()) { for (int i = 0; i < NETSERVER_COUNT; i++) out[i] = G.servers[i]; return NETSERVER_COUNT; }
    netservers_probe();     // a new round when the last is older than 10 s
    return netservers_get(out, NETSERVER_COUNT);
}
// The relay the hub session is on, -1 none (offline), -2 a custom address.
int connected_server()
{
    if (gallery_on()) return G.state == NETPLAY_LOBBY || G.state == NETPLAY_CONNECTED ? G.server_cur : -1;
    char address[128];
    if (!netplay_lobby_address(address, sizeof(address))) return -1;
    int i = netservers_index_of(address);
    return i >= 0 ? i : -2;
}
enum NetServerChoice server_choice() { return gallery_on() ? G.choice : netservers_choice(); }
bool both_servers() { return server_choice() == NETCHOICE_BOTH; }
// The room list FIND A ROOM shows: the relay the hub is on, and with Both servers the other one's too
// (netservers_rooms: the lower ping first).
int list_rooms(NetServerRoom *out, int max)
{
    if (!gallery_on()) {
        unsigned version;
        netservers_fetch_rooms();       // Both servers: the other relay's list, every 3 s
        return netservers_rooms(out, max, &version);
    }
    int n = 0;
    for (const NetRoomInfo &r : G.rooms)
        if (n < max) { out[n].room = r; out[n].server = G.server_cur; out[n].ping_ms = G.servers[G.server_cur].ping_ms; out[n].age_s = -1; n++; }
    if (G.choice == NETCHOICE_BOTH)
        for (const NetServerRoom &r : G.other_rooms) if (n < max) out[n++] = r;
    return n;
}
int room_ping(int slot)
{
    if (gallery_on()) return slot >= 0 && slot < NETPLAY_ROOM_SLOTS ? G.room_pings[slot] : -1;
    return netplay_room_ping(slot);
}

// Ping colours: good, fair, high.
ImU32 ping_colour(int ms) { return ms < 0 ? GREY : ms < 80 ? GREEN : ms < 150 ? ORANGE : IM_COL32(235, 70, 60, 255); }

// The relay a player meets the most others on, besides `than` (for the quick-match hint), -1 none.
int other_server(const NetServerInfo *s, int than)
{
    int o = than == NETSERVER_EU ? NETSERVER_NA : than == NETSERVER_NA ? NETSERVER_EU : -1;
    return o >= 0 && s[o].state == NETSERVER_UP ? o : -1;
}

// Signal bars for a ping: four for a good one, one for a high one, none (grey) offline.
void ping_bars(ImDrawList *dl, const Frame &f, float x, float y, float h, int ms)
{
    int lit = ms < 0 ? 0 : ms < 60 ? 4 : ms < 100 ? 3 : ms < 150 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        float bh = h * (i + 1) / 4.0f;
        rect(dl, f, x + i * 5, y + h - bh, 3.5f, bh, i < lit ? ping_colour(ms) : IM_COL32(255, 255, 255, 40), 1);
    }
}

// Room codes use these characters (no 0/O, 1/I), as the server makes them.
const char CODE_CHARS[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

Pad read_pad()
{
    Pad p = {};
    auto any = [](std::initializer_list<ImGuiKey> keys, bool repeat) {
        for (ImGuiKey k : keys) if (k != ImGuiKey_None && ImGui::IsKeyPressed(k, repeat)) return true;
        return false;
    };
    // Letter keys follow the game's keyboard layout (WASD, K/L/J/I); on the code page they type.
    bool letters = S.page != CODE;
    auto key = [&](ImGuiKey k) { return letters ? k : ImGuiKey_None; };
    p.up = any({ ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp, ImGuiKey_UpArrow, key(ImGuiKey_W) }, true);
    p.down = any({ ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown, ImGuiKey_DownArrow, key(ImGuiKey_S) }, true);
    p.left = any({ ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft, ImGuiKey_LeftArrow, key(ImGuiKey_A) }, true);
    p.right = any({ ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight, ImGuiKey_RightArrow, key(ImGuiKey_D) }, true);
    p.a = any({ ImGuiKey_GamepadFaceDown, ImGuiKey_Enter, ImGuiKey_KeypadEnter, key(ImGuiKey_Space), key(ImGuiKey_K) }, false);
    p.b = any({ ImGuiKey_GamepadFaceRight, ImGuiKey_Escape, letters ? ImGuiKey_Backspace : ImGuiKey_None, key(ImGuiKey_L) }, false);
    p.x = any({ ImGuiKey_GamepadFaceLeft, key(ImGuiKey_J), key(ImGuiKey_F) }, false);
    p.y = any({ ImGuiKey_GamepadFaceUp, key(ImGuiKey_I), key(ImGuiKey_R) }, false);
    p.lb = any({ ImGuiKey_GamepadL1, ImGuiKey_PageUp, ImGuiKey_Q }, true);
    p.rb = any({ ImGuiKey_GamepadR1, ImGuiKey_PageDown, ImGuiKey_E }, true);
    return p;
}


std::string setting_text(const char *key, const char *fallback)
{
    char buf[128];
    return recomp_settings_get_text("online", key, buf, sizeof(buf), fallback);
}

void say(const std::string &s, unsigned ms = 3500) { S.note = s; S.note_until = GetTickCount64() + ms; }

const char *mode_name(int type)
{
    if (NETPLAY_IS_KOTH(type)) return "King of the Hill";
    const OnlineMode *m = online_mode(type);
    return m ? m->name : "Match";
}

// The relay answered CAPS with King of the Hill (an older relay never offers it).
bool koth_offered()
{
    int caps = lobby_caps();
    return caps > 0 && (caps & NETPLAY_CAP_KOTH);
}

// The modes CREATE A ROOM and QUICK MATCH cycle through: the game's online modes, then King of the
// Hill when the relay has it. `only_koth` / `no_koth`: a room's host cannot switch between the two
// kinds (the relay refuses it).
std::vector<int> mode_cycle(bool only_koth = false, bool no_koth = false)
{
    std::vector<int> out;
    const OnlineMode *modes;
    int n = online_modes(&modes);
    if (!only_koth)
        for (int i = 0; i < n; i++) out.push_back(modes[i].type);
    if (!no_koth && (koth_offered() || only_koth)) out.push_back(NETPLAY_MODE_KOTH_RANDOM);
    return out;
}

int cycle_index(const std::vector<int> &cycle, int type)
{
    for (size_t i = 0; i < cycle.size(); i++)
        if (cycle[i] == type || (NETPLAY_IS_KOTH(cycle[i]) && NETPLAY_IS_KOTH(type))) return (int)i;
    return 0;
}


// The fighter the player last chose (a fighter.xml ID saved by build 13 or 14 as its Battle-mode ID).
static int saved_fighter() { return online_fighter_id(recomp_settings_get("online", "fighter", 55)); }

// The fighters that have a portrait, by name.
const std::vector<OnlineFighter> &roster()
{
    static std::vector<OnlineFighter> list;
    static bool built = false;
    if (!built) {
        built = true;
        // The fighters with a select portrait in the online art (fighters/<Battle-mode ID>.png,
        // from the game's choose-character portraits; Teck, Santos, O.E and Eric Sermon are
        // 85, 93, 92 and 91 there, as in the game's database).
        const OnlineFighter *all;
        int n = online_fighters(&all);
        for (int i = 0; i < n; i++) {
            std::wstring path = art_root() + L"fighters\\" + std::to_wstring(all[i].id) + L".png";
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) list.push_back(all[i]);
        }
        if (list.empty()) for (int i = 0; i < n; i++) list.push_back(all[i]);
        std::sort(list.begin(), list.end(), [](const OnlineFighter &a, const OnlineFighter &b) {
            return _stricmp(a.name, b.name) < 0; });
    }
    return list;
}

// Venue presets for the room settings: the whole list, then a few themed sets.
struct Preset { const char *name; std::vector<int> ids; };
const std::vector<Preset> &presets()
{
    static std::vector<Preset> list;
    if (list.empty()) {
        const OnlineVenue *v;
        int n = online_venues(&v);
        Preset all{ "Every venue", {} };
        for (int i = 0; i < n; i++) all.ids.push_back(v[i].fe_id);
        list.push_back(all);
        list.push_back({ "Classic three", { 1, 6, 11 } });
        list.push_back({ "Clubs", { 4, 6, 9, 12, 16, 10 } });
        list.push_back({ "Street", { 14, 15, 7, 11, 17, 13 } });
        list.push_back({ "Fight clubs", { 1, 2, 21, 22, 18 } });
    }
    return list;
}

int preset_of(const NetRoomSettings &s)
{
    const auto &p = presets();
    for (size_t i = 0; i < p.size(); i++) {
        if ((int)p[i].ids.size() != s.pool_count) continue;
        bool same = true;
        for (int k = 0; k < s.pool_count; k++)
            if (std::find(p[i].ids.begin(), p[i].ids.end(), (int)s.pool[k]) == p[i].ids.end()) same = false;
        if (same) return (int)i;
    }
    return -1;
}

void apply_preset(NetRoomSettings &s, int index)
{
    const auto &p = presets();
    if (p.empty()) return;
    index = (index % (int)p.size() + (int)p.size()) % (int)p.size();
    s.pool_count = 0;
    for (int id : p[index].ids) if (s.pool_count < 32) s.pool[s.pool_count++] = (unsigned char)id;
}

std::string hub_name();

void default_settings(NetRoomSettings &s)
{
    memset(&s, 0, sizeof(s));
    std::string player = hub_name();          /* the Discord name when signed in, as everywhere else */
    snprintf(s.name, sizeof(s.name), "%s's room", player.empty() ? "Player" : player.c_str());
    s.is_public = 1;
    s.mode = 0;
    s.max = 2;
    apply_preset(s, 0);
}

// The name the hub shows: the Discord name when signed in, else the setting.
std::string hub_name()
{
    std::string name = setting_text("name", "Player");
    char discord[32];
    if (account_game_name(discord, sizeof(discord))) name = discord;
    return name;
}

// Into the hub on the chosen relay (netplay_servers.h). Automatic waits for the relays' first
// answers (about a second, at most 2.5 s): S.connect_pending, and the caller asks again.
void connect_if_needed()
{
    enum NetplayState st = netplay_state();
    if (netplay_in_lobby() && st != NETPLAY_OFF && st != NETPLAY_FAILED) { S.connect_pending = false; return; }
    account_init();
    account_refresh();
    netservers_probe();
    if (netservers_reconsider() == -1) { S.connect_pending = true; return; }
    S.connect_pending = false;
    netservers_reconnect(hub_name().c_str());
}

// Move the hub to the relay chosen now (the picker, the quick-match hint, ranked).
void switch_server(const char *why)
{
    S.connect_pending = false;
    S.fallback_tried = true;            // a choice the player made: no automatic second try
    int r = netservers_reconnect(hub_name().c_str());
    if (r < 0) S.connect_pending = true;
    if (why && r != 0) say(why);
}

void open_servers()
{
    S.page = SERVERS;
    enum NetServerChoice c = server_choice();
    S.servers_sel = c == NETCHOICE_EU ? 1 : c == NETCHOICE_NA ? 2 : c == NETCHOICE_BOTH ? 3 : 0;
}

// ---- hub ----

const int HUB_ITEMS = 8, HUB_LEAVE = 7, HUB_RANKED = 1, HUB_VOICE = 6;

// RANKED MATCH waits until ranked play is switched on on the server (docs/ranked-design.md);
// RECOMP_RANKED=1 shows it for tests.
bool ranked_shown()
{
    static int on = -1;
    if (on < 0) on = getenv("RECOMP_RANKED") != nullptr;
    return on != 0 || (gallery_on() && G.ranked);
}

// The server chip at the bottom right, beside the status line: the relay the hub is on, its ping
// and who is online ("NORTH AMERICA  42 MS  -  12 ONLINE"); with `button` the X glyph
// in front says it opens the server picker (a click on it does too). Returns true when clicked.
bool online_count(ImDrawList *dl, const Frame &f, bool button = false)
{
    int online = -1, rooms = 0;
    lobby_players(nullptr, 0, &online, &rooms);
    NetServerInfo s[NETSERVER_COUNT];
    servers(s);
    int cur = connected_server();
    char line[128];
    int ping = -1;
    if (cur >= 0) {
        ping = s[cur].state == NETSERVER_UP ? s[cur].ping_ms : -1;
        if (online < 0 && s[cur].state == NETSERVER_UP) { online = s[cur].online; rooms = s[cur].rooms; }
        std::string where = upper(s[cur].name);
        if (online >= 0)
            snprintf(line, sizeof(line), "%s  %s  %d ONLINE", where.c_str(),
                     ping >= 0 ? (std::to_string(ping) + " MS  -").c_str() : "-", online);
        else
            snprintf(line, sizeof(line), "%s", where.c_str());
    } else if (online >= 0) {
        snprintf(line, sizeof(line), "%d PLAYER%s ONLINE  -  %d ROOM%s", online, online == 1 ? "" : "S", rooms,
                 rooms == 1 ? "" : "S");
    } else {
        return false;
    }
    float size = 12, y = g_game_door ? 428 : 450, w = text_width(f, g_title, size, line);
    // Both servers: the label says so, in front of the relay the hub is on now
    const char *tag = both_servers() ? "BOTH SERVERS" : "SERVER";
    bool tagged = button || both_servers();
    float label = tagged ? text_width(f, g_title, 10, tag) + 8 : 0;
    float bars = cur >= 0 ? 24 : 0, glyph = button ? 22 : 0, x1 = 606, x0 = x1 - w - bars - glyph - label - 16;
    glass(dl, f, x0, y - 3, x1 - x0, 21, 10, IM_COL32(10, 10, 13, 255), 0.62f);
    bool hot = button && mouse_in(f, x0, y - 3, x1 - x0, 21);
    if (hot) frame_rect(dl, f, x0, y - 3, x1 - x0, 21, with_alpha(ORANGE, 0.8f), 1.2f);
    float x = x0 + 8;
    if (button) {
        hint(dl, f, x, y - 1, "x", "");
        x += glyph;
    }
    if (tagged) {
        text(dl, f, g_title, 10, x, y + 1, ORANGE, tag, LEFT, 0.8f);
        x += label;
    }
    if (cur >= 0) { ping_bars(dl, f, x, y + 1, 11, ping); x += bars; }
    text(dl, f, g_title, size, x, y, cur >= 0 ? CREAM : ORANGE, line, LEFT, 1.0f);
    return hot && ImGui::IsMouseClicked(0);
}

// ---- players online ----

void draw_players(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    static NetPlayer list[40];
    int online = -1, rooms = 0;
    int n = lobby_players(list, 40, &online, &rooms);
    if (pad.x) netplay_lobby_who();
    page_title(dl, f, "PLAYERS ONLINE");
    online_count(dl, f);
    if (n < 0) {
        text(dl, f, g_title, 20, 320, 200, CREAM, "ASKING THE SERVER...", CENTER, 1.4f);
        return;
    }
    glass(dl, f, 40, 78, 566, 334);
    static const char *const states[] = { "IN THE MENU", "IN A ROOM", "FIGHTING" };
    const ImU32 colours[] = { CREAM, ORANGE, GREEN };
    for (int i = 0; i < n; i++) {
        float x = i < 20 ? 52 : 330, y = 90 + (i % 20) * 15.0f;
        int st = list[i].state >= 0 && list[i].state <= 2 ? list[i].state : 0;
        text(dl, f, g_title, 13, x, y, WHITE, list[i].name, LEFT, 1.1f);
        text(dl, f, g_title, 10, x + 262, y + 2, colours[st], states[st], RIGHT, 1.0f);
    }
    if (n == 0) text(dl, f, g_title, 14, 320, 200, GREY, "Nobody else with this version right now.", CENTER, 1.0f);
    if (online > n)
        text(dl, f, g_title, 10, 592, 394, GREY, "and others on other versions or not shown", RIGHT, 1.0f);
}

// ---- the server picker: Automatic, Europe, North America, each with its ping and players, and Both ----

void draw_servers(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    NetServerInfo s[NETSERVER_COUNT];
    servers(s);
    const int rows = 2 + NETSERVER_COUNT, BOTH_ROW = 1 + NETSERVER_COUNT;
    const float pitch = 66, row_h = 58;
    int cur = connected_server();
    enum NetServerChoice choice = server_choice();
    int chosen_row = choice == NETCHOICE_EU ? 1 : choice == NETCHOICE_NA ? 2 : choice == NETCHOICE_AUTO ? 0 :
                     choice == NETCHOICE_BOTH ? BOTH_ROW : -1;
    int nearest = -1;
    for (int i = 0; i < NETSERVER_COUNT; i++)
        if (s[i].state == NETSERVER_UP && (nearest < 0 || s[i].ping_ms < s[nearest].ping_ms)) nearest = i;
    bool checking = !gallery_on() && netservers_probing();
    if (pad.up) S.servers_sel = (S.servers_sel + rows - 1) % rows;
    if (pad.down) S.servers_sel = (S.servers_sel + 1) % rows;

    page_title(dl, f, "SERVER");
    text(dl, f, g_hand, 15, 54, 58, ORANGE, "Where you play online. Pick the one with the lowest ping.", LEFT, 0.8f, 280);
    glass(dl, f, 36, 82, 290, rows * pitch + 12);
    for (int k = 0; k < rows; k++) {
        float x = 50, y = 92 + k * pitch, w = 262, h = row_h;
        if (mouse_in(f, x, y, w, h)) S.servers_sel = k;
        bool sel = S.servers_sel == k;
        if (sel) selector(dl, f, x, y, w, h);
        bool both = k == BOTH_ROW;
        const NetServerInfo *si = k && !both ? &s[k - 1] : nullptr;
        const char *title = k == 0 ? "AUTOMATIC" : both ? "BOTH SERVERS" : si->name;
        text(dl, f, g_title, 21, x + 14, y + 3, sel ? CREAM : with_alpha(GREY, 0.9f), upper(title).c_str(), LEFT, 1.5f);
        // right: the ping, large, in its colour, with its bars (OFFLINE in red for a relay that did not answer)
        char big[32] = "", sub[96] = "";
        ImU32 tone = GREY;
        int ms = -1;
        const NetServerInfo *shown = k == 0 || both ? (nearest >= 0 ? &s[nearest] : nullptr) : si;
        if (shown && shown->state == NETSERVER_UP) {
            ms = shown->ping_ms;
            snprintf(big, sizeof(big), "%d MS", ms);
            tone = ping_colour(ms);
        } else if (shown && shown->state == NETSERVER_OFFLINE) {
            snprintf(big, sizeof(big), "OFFLINE");
            tone = IM_COL32(235, 70, 60, 255);
        } else {
            snprintf(big, sizeof(big), "...");
        }
        if (k == 0 && nearest >= 0)
            snprintf(sub, sizeof(sub), "THE LOWEST PING: %s", upper(s[nearest].name).c_str());
        else if (k == 0)
            snprintf(sub, sizeof(sub), "%s", checking ? "CHECKING THE SERVERS..." : "THE LOWEST PING");
        else if (both) {    // the rooms of both, and who waits for a match on either
            int waiting = 0;
            for (int i = 0; i < NETSERVER_COUNT; i++) if (s[i].state == NETSERVER_UP) waiting += s[i].waiting;
            if (waiting > 0) snprintf(sub, sizeof(sub), "ROOMS ON BOTH  -  %d WAITING FOR A MATCH", waiting);
            else snprintf(sub, sizeof(sub), "ROOMS AND QUICK MATCH ON BOTH");
        }
        else if (si->state == NETSERVER_UP && si->waiting > 0)
            snprintf(sub, sizeof(sub), "%d ONLINE  -  %d WAITING FOR A MATCH", si->online, si->waiting);
        else if (si->state == NETSERVER_UP)
            snprintf(sub, sizeof(sub), "%d ONLINE  -  %d ROOM%s", si->online, si->rooms, si->rooms == 1 ? "" : "S");
        else if (si->state == NETSERVER_OFFLINE)
            snprintf(sub, sizeof(sub), "NO ANSWER RIGHT NOW");
        else
            snprintf(sub, sizeof(sub), "CHECKING...");
        bool bars = !(shown && shown->state == NETSERVER_OFFLINE);
        float bx = x + w - (bars ? 30 : 8);
        if (bars) ping_bars(dl, f, bx + 6, y + 7, 14, ms);
        text(dl, f, g_title, 17, bx - (bars ? 4 : 0), y + 4, sel ? tone : with_alpha(tone, 0.75f), big, RIGHT, 1.2f);
        text(dl, f, g_hand, 13, x + 16, y + 26, sel ? ORANGE : with_alpha(ORANGE, 0.6f), sub, LEFT, 1.0f);
        // the tags: the player's choice, and where the hub is now (with Both: its relay, on that relay's row)
        float tx = x + 16;
        if (si && cur == k - 1) {
            const char *tag = "YOU ARE HERE";
            float tw = text_width(f, g_title, 9, tag);
            rect(dl, f, tx, y + 42, tw + 10, 13, IM_COL32(40, 120, 60, 230), 4);
            text(dl, f, g_title, 9, tx + 5, y + 43, WHITE, tag, LEFT, 0.6f);
            tx += tw + 16;
        }
        if (k == chosen_row) {
            const char *tag = "YOUR CHOICE";
            float tw = text_width(f, g_title, 9, tag);
            rect(dl, f, tx, y + 42, tw + 10, 13, with_alpha(RED, 0.9f), 4);
            text(dl, f, g_title, 9, tx + 5, y + 43, WHITE, tag, LEFT, 0.6f);
            tx += tw + 16;
        }
        if (mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
    }
    door_panel(dl, f);
    if (S.servers_sel == BOTH_ROW)
        door_text(dl, f, &tex("ui/online_icon.png"), "BOTH SERVERS", "YOU CHOOSE WHOM YOU PLAY",
                  "Rooms and Quick Match on Europe and North America; matches across the ocean have higher ping.");
    else
        door_text(dl, f, &tex("ui/online_icon.png"), "SERVER", "EUROPE OR NORTH AMERICA",
                  "You meet the players on the same server. The nearest one gives the lowest ping. Your account, stats and "
                  "the leaderboard are the same on both.");
    if (checking) {
        float t = (float)(GetTickCount64() % 1200) / 1200.0f;
        rect(dl, f, 50, 92 + rows * pitch - 6, 262, 2, IM_COL32(255, 255, 255, 25), 1);
        rect(dl, f, 50 + 222 * (t < 0.5f ? t * 2 : 2 - t * 2), 92 + rows * pitch - 6, 40, 2, ORANGE, 1);
    }
    if (pad.x && !gallery_on()) { netservers_probe(); say("Checking the servers..."); }
    if (pad.a && !gallery_on()) {
        int k = S.servers_sel;
        if (k == 0) netservers_set_choice(NETCHOICE_AUTO);
        else if (k == BOTH_ROW) netservers_set_choice(NETCHOICE_BOTH);
        else netservers_set_choice(k == 1 ? NETCHOICE_EU : NETCHOICE_NA);
        int to = netservers_current();
        S.page = HUB;
        if (to < 0) {
            S.connect_pending = true;
            say("Automatic: checking the servers...");
        } else if (to == cur && netplay_state() != NETPLAY_FAILED) {
            if (k == BOTH_ROW) say(std::string("Both servers: the rooms of both. The hub stays on ") + s[to].name + ".", 5000);
            else say(std::string("You stay on ") + s[to].name + ".");
        } else {
            std::string why = std::string("Connecting to ") + s[to].name +
                              (s[to].state == NETSERVER_OFFLINE ? " (it did not answer the last check)..." : "...");
            switch_server(why.c_str());
        }
    }
    if (pad.b) S.page = HUB;
}

void draw_hub(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    static const char *const items[] = { "QUICK MATCH", "RANKED MATCH", "FIND A ROOM", "CREATE A ROOM", "MY STATS",
                                         "PLAYERS ONLINE", "VOICE CHAT", "LEAVE" };
    static const char *const subs[] = { "STRAIGHT INTO A FIGHT", "ONE ON ONE FOR YOUR RATING", "OPEN ROOMS", "YOUR RULES",
                                        "RECORD AND LEADERBOARD", "WHO IS AROUND", "TALK IN ROOMS AND FIGHTS",
                                        "BACK TO THE MENU" };
    static const char *const descs[] = {
        "You are put into an open room of this mode, or one is opened for the next players.",
        "Pick your fighter, then the server finds an opponent with a similar rating. Wins and losses move your "
        "rating. Sign in with Discord in the launcher to play ranked.",
        "Every listed room with its mode and players. Pick one and join it.",
        "Pick the mode, the players and the venues to vote on. Others join from the list or by its code.",
        "Your wins, losses and last fights, and the leaderboard. Sign in with Discord in the launcher to keep them.",
        "Everyone online right now: in the menu, in a room or fighting.",
        "Talk with the players in your room, in the room and in the fight. Push-to-talk, open microphone or off, "
        "your microphone's and the others' volume.",
        "Back to the main menu. You stay reachable until you go offline in the settings.",
    };
    std::vector<int> quick_modes = mode_cycle();     // King of the Hill last, when the relay has it
    int mode_count = (int)quick_modes.size();
    if (S.quick_mode >= mode_count) S.quick_mode = 0;
    int shown[HUB_ITEMS], count = 0, at = 0;
    for (int i = 0; i < HUB_ITEMS; i++)
        if (i != HUB_RANKED || ranked_shown()) shown[count++] = i;
    for (int k = 0; k < count; k++) if (shown[k] == S.hub_sel) at = k;
    if (pad.up) at = (at + count - 1) % count;
    if (pad.down) at = (at + 1) % count;
    S.hub_sel = shown[at];
    if (S.hub_sel == 0 && (pad.left || pad.right) && mode_count)
        S.quick_mode = (S.quick_mode + (pad.right ? 1 : mode_count - 1)) % mode_count;

    page_title(dl, f, "ONLINE");
    if (online_count(dl, f, true) || pad.x) open_servers();
    // Ranked needs the account service's internal listener, which only the Europe relay has: on
    // North America the entry says so and switches over.
    bool ranked_elsewhere = false;
    {
        NetServerInfo s[NETSERVER_COUNT];
        servers(s);
        int cur = connected_server();
        ranked_elsewhere = cur == NETSERVER_NA && !(s[cur].state == NETSERVER_UP && s[cur].version && (s[cur].flags & NETSERVER_RANKED));
    }
    // Online fights need a Discord sign-in: matches against signed-out players did not count, about
    // half of them after the build-23 rules (2026-10-24). Y signs in from here (the browser opens).
    enum AccountState acc = acc_state();
    bool signed_in = acc == ACCOUNT_IN;
    // A newer build is out: this one starts no new matches (the relay refuses them too, since build 26).
    int newer = 0;
    {
        enum UpdaterState us = updater_state();
        if (us >= UPDATER_AVAILABLE && us <= UPDATER_STARTED) updater_info(&newer, nullptr, 0, nullptr, 0);
    }
    bool outdated = updater_local_build() > 0 && newer > updater_local_build();
    if (outdated) {
        char line[96];
        snprintf(line, sizeof(line), "BUILD %d IS OUT: UPDATE IN THE LAUNCHER TO PLAY ONLINE", newer);
        text(dl, f, g_title, 14, 50, 56, ORANGE, line, LEFT, 1.2f);
    } else if (!signed_in) {
        const char *line = acc == ACCOUNT_WAITING ? "FINISH SIGNING IN WITH DISCORD IN YOUR BROWSER..."
                         : acc == ACCOUNT_CHECKING ? "CHECKING YOUR DISCORD SIGN-IN..."
                         : "SIGN IN WITH DISCORD TO FIGHT ONLINE";
        text(dl, f, g_title, 14, 50, 56, ORANGE, line, LEFT, 1.2f);
        if (acc == ACCOUNT_OUT || acc == ACCOUNT_FAILED) hint(dl, f, 50 + text_width(f, g_title, 14, line) + 14, 55, "y", "SIGN IN");
        if (pad.y && (acc == ACCOUNT_OUT || acc == ACCOUNT_FAILED)) {
            account_sign_in();
            say("Your browser opens: sign in with Discord, then come back.");
        }
    }
    {   // the items on one glass panel
        float bottom = count > 7 ? 84 + (count - 1) * 41 + 39 : count > 6 ? 84 + (count - 1) * 45 + 42 : 88 + (count - 1) * 53 + 46;
        glass(dl, f, 36, 74, 290, bottom - 74 + 10);
    }
    for (int k = 0; k < count; k++) {
        int i = shown[k];
        // eight items (ranked shown) sit closer, so the panel still ends above the hints
        float x = 50, y = count > 7 ? 84 + k * 41 : count > 6 ? 84 + k * 45 : 88 + k * 53, w = 262;
        float h = count > 7 ? 39 : count > 6 ? 42 : 46;
        if (mouse_in(f, x, y, w, h)) S.hub_sel = i;
        bool sel = S.hub_sel == i;
        if (sel) selector(dl, f, x, y, w, h);
        text(dl, f, g_title, 23, x + 16, y + 3, sel ? CREAM : with_alpha(GREY, 0.85f), items[i], LEFT, 1.6f);
        text(dl, f, g_hand, 12, x + 18, y + 29, sel ? ORANGE : with_alpha(ORANGE, 0.45f), subs[i], LEFT, 1.0f);
        if (mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
    }
    // the door: what the chosen item does
    door_panel(dl, f);
    if (S.hub_sel == HUB_RANKED && ranked_elsewhere)
        door_text(dl, f, &tex("ui/online_icon.png"), items[S.hub_sel], "ON THE EUROPE SERVER",
                  "Ranked runs on the Europe server: your rating and the leaderboard live there. Press A to switch to "
                  "Europe, then choose Ranked Match again.");
    else
        door_text(dl, f, &tex("ui/online_icon.png"), items[S.hub_sel], subs[S.hub_sel], descs[S.hub_sel]);
    if (S.hub_sel == 0 && mode_count) {
        int type = quick_modes[S.quick_mode];
        const Tex &logo = tex(mode_logo(type));
        if (!logo.srv) text(dl, f, g_title, 16, 463, 404, CREAM, mode_name(type), CENTER);
        else image_fit(dl, f, logo, 383, 396, 160, 30);
        text(dl, f, g_title, 22, 370, 398, ORANGE, "<", CENTER, 1.2f);
        text(dl, f, g_title, 22, 556, 398, ORANGE, ">", CENTER, 1.2f);
    }

    if (pad.a) {
        bool needs_account = S.hub_sel == 0 || S.hub_sel == 2 || S.hub_sel == 3;
        if ((needs_account || S.hub_sel == 1) && outdated) {
            char line[120];
            snprintf(line, sizeof(line), "Build %d is out: close the game and update in the launcher to play online.", newer);
            say(line);
        } else if (needs_account && !signed_in) {
            say(acc == ACCOUNT_WAITING ? "Finish signing in with Discord in your browser first."
                                       : "Sign in with Discord first: press Y.");
        } else switch (S.hub_sel) {
        case 0:
            if (mode_count) {
                int type = quick_modes[S.quick_mode];
                const OnlineMode *qm = online_mode(type);
                // King of the Hill: the fullest open hill, or a new one with room for eight
                int qmax = NETPLAY_IS_KOTH(type) ? 8 : qm ? std::min(4, qm->fighters) : 2;
                S.last_quick = type;
                S.last_quick_max = qmax;
                S.auto_moves = 0;
                // Both servers: the search starts on the relay with the lower ping (the hub may be on the
                // other one after joining a room there)
                int home = both_servers() && !gallery_on() ? netservers_best() : -1, here = connected_server();
                if (home >= 0 && here >= 0 && home != here) {
                    NetServerInfo sv[NETSERVER_COUNT];
                    servers(sv);
                    netservers_quick_on(home, type, qmax, hub_name().c_str());
                    say(std::string("Searching on ") + sv[home].name + ", the lower ping...");
                } else {
                    netplay_lobby_quick(type, qmax);
                    S.quick_wanted = !NETPLAY_IS_KOTH(type);
                    say("Looking for a room...");
                }
            }
            break;
        case 1:
            if (ranked_elsewhere) {
                netservers_use(NETSERVER_EU);
                switch_server("Switching to the Europe server for ranked...");
                break;
            }
            if (acc_state() != ACCOUNT_IN) { say("Sign in with Discord in the launcher to play ranked."); break; }
            S.ranked_pick = true;
            S.page = FIGHTERS;
            {
                int fighter = saved_fighter();
                const auto &list = roster();
                for (size_t i = 0; i < list.size(); i++) if (list[i].id == fighter) S.fighter_sel = (int)i;
            }
            break;
        case 2: S.page = ROOMS; netplay_lobby_list(0xFF); S.last_list = GetTickCount64(); break;
        case 3:
            if (!S.settings_init) { default_settings(S.settings); S.settings_init = true; }
            S.settings_edit = false;
            S.settings_sel = 0;
            S.page = SETTINGS;
            break;
        case 4: S.page = STATS; S.stats_page = 0; account_fetch_stats_page(0); break;
        case 5: S.page = PLAYERS; netplay_lobby_who(); break;
        case HUB_VOICE: S.page = VOICE; S.voice_sel = 0; break;
        case HUB_LEAVE: break;      // handled by the caller (closes)
        }
    }
}

// ---- my stats ----

// The account service's statistics in its text form (account_stats): "key=value" lines.
struct StatsView {
    std::string name, favorite_name;
    int matches = 0, wins = 0, losses = 0, draws = 0, win_rate = 0, streak = 0, best = 0, rank = 0, min = 3;
    int favorite = -1;
    int ranked_rating = 0, ranked_games = 0;    // 0 games: not played ranked yet
    int board_page = 0, board_pages = 1;        // the leaderboard page shown (10 places each)
    std::vector<std::vector<std::string>> recent, board;
};

std::vector<std::string> split_bar(const std::string &v)
{
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        size_t bar = v.find('|', at);
        out.push_back(v.substr(at, bar == std::string::npos ? std::string::npos : bar - at));
        if (bar == std::string::npos) return out;
        at = bar + 1;
    }
}

bool read_stats(StatsView &v)
{
    static char buf[16384];
    if (!stats_text(buf, sizeof(buf))) return false;
    for (char *line = buf; line && *line;) {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            std::string k = line, val = eq + 1;
            int n = atoi(eq + 1);
            if (k == "name") v.name = val;
            else if (k == "matches") v.matches = n;
            else if (k == "wins") v.wins = n;
            else if (k == "losses") v.losses = n;
            else if (k == "draws") v.draws = n;
            else if (k == "win_rate") v.win_rate = n;
            else if (k == "streak") v.streak = n;
            else if (k == "best_streak") v.best = n;
            else if (k == "rank") v.rank = n;
            else if (k == "min") v.min = n;
            else if (k == "favorite") v.favorite = n;
            else if (k == "favorite_name") v.favorite_name = val;
            else if (k == "recent") v.recent.push_back(split_bar(val));
            else if (k == "board") v.board.push_back(split_bar(val));
            else if (k == "board_page") v.board_page = n;
            else if (k == "board_pages") v.board_pages = n > 0 ? n : 1;
            else if (k == "ranked") {
                auto r = split_bar(val);
                if (r.size() >= 2) { v.ranked_rating = atoi(r[0].c_str()); v.ranked_games = atoi(r[1].c_str()); }
            }
        }
        line = end ? end + 1 : nullptr;
    }
    return true;
}

void big_number(ImDrawList *dl, const Frame &f, float x, float y, const char *value, const char *label, ImU32 col)
{
    text(dl, f, g_title, 34, x, y, col, value, CENTER, 1.8f);
    text(dl, f, g_hand, 13, x, y + 40, ORANGE, label, CENTER, 1.0f);
}

void draw_stats(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    if (pad.x) account_fetch_stats_page(S.stats_page);
    page_title(dl, f, "MY STATS");
    enum AccountState acc = acc_state();
    StatsView v;
    bool have = (acc == ACCOUNT_IN || acc == ACCOUNT_CHECKING) && read_stats(v);
    if (acc != ACCOUNT_IN && acc != ACCOUNT_CHECKING) {
        text(dl, f, g_title, 24, 320, 180, CREAM, "SIGN IN TO KEEP YOUR STATS", CENTER, 1.6f);
        text(dl, f, g_hand, 15, 320, 222, ORANGE, "IN THE LAUNCHER: ONLINE, SIGN IN WITH DISCORD", CENTER, 1.0f);
        text(dl, f, g_title, 13, 320, 256, GREY,
             "Signed-in players get their wins, losses and last fights here, and a place on the leaderboard.",
             CENTER, 1.0f, 440);
        return;
    }
    if (!have) {
        text(dl, f, g_title, 22, 320, 200, CREAM, stats_busy() ? "LOADING..." : "NO STATS YET", CENTER, 1.6f);
        if (!stats_busy())
            text(dl, f, g_hand, 14, 320, 236, ORANGE, "PRESS X TO TRY AGAIN", CENTER, 1.0f);
        return;
    }
    char num[32];
    // left: the main fighter and the record
    glass(dl, f, 34, 78, 286, 328);
    rect(dl, f, 44, 86, 114, 155, IM_COL32(0, 0, 0, 120), 4);
    if (v.favorite < 0 || !portrait(dl, f, v.favorite, 46, 88, 110, 151))
        rect(dl, f, 46, 88, 110, 151, IM_COL32(20, 20, 20, 200), 3);
    text(dl, f, g_title, 22, 168, 92, CREAM, v.name.c_str(), LEFT, 1.6f);
    if (!v.favorite_name.empty()) {
        text(dl, f, g_hand, 13, 170, 124, ORANGE, "MAIN", LEFT, 1.0f);
        text(dl, f, g_title, 14, 170, 140, WHITE, v.favorite_name.c_str(), LEFT, 1.2f);
    }
    if (v.rank) snprintf(num, sizeof(num), "#%d", v.rank);
    else snprintf(num, sizeof(num), "%d MORE", std::max(0, v.min - v.matches));
    text(dl, f, g_hand, 13, 170, 172, ORANGE, "LEADERBOARD", LEFT, 1.0f);
    text(dl, f, g_title, 20, 170, 188, v.rank ? CREAM : GREY, num, LEFT, 1.4f);
    if (!v.rank) text(dl, f, g_title, 11, 170, 214, GREY, "matches to get on it", LEFT, 1.0f);
    snprintf(num, sizeof(num), "%d", v.wins);
    big_number(dl, f, 78, 246, num, "WINS", GREEN);
    snprintf(num, sizeof(num), "%d", v.losses);
    big_number(dl, f, 160, 246, num, "LOSSES", IM_COL32(235, 90, 80, 255));
    snprintf(num, sizeof(num), "%d%%", v.win_rate);
    big_number(dl, f, 248, 246, num, "WIN RATE", CREAM);
    if (v.ranked_games)
        snprintf(num, sizeof(num), "STREAK %d   BEST %d   RANKED %d", v.streak, v.best, v.ranked_rating);
    else
        snprintf(num, sizeof(num), "STREAK %d   BEST %d", v.streak, v.best);
    text(dl, f, g_title, 13, 46, 306, WHITE, num, LEFT, 1.1f);
    // last fights: the three lines end well inside the panel (bottom 406)
    text(dl, f, g_hand, 13, 46, 326, ORANGE, "LAST FIGHTS", LEFT, 1.0f);
    if (v.recent.empty()) text(dl, f, g_title, 12, 46, 344, GREY, "Play online while signed in.", LEFT, 1.0f);
    for (size_t i = 0; i < v.recent.size() && i < 3; i++) {
        const auto &r = v.recent[i];
        if (r.size() < 4) continue;
        bool win = r[0] == "win", draw = r[0] == "draw";
        std::string line = r[1] + " vs " + (r[2].empty() ? "?" : r[2]);
        float y = 342 + i * 17;
        text(dl, f, g_title, 13, 46, y, win ? GREEN : draw ? GREY : IM_COL32(235, 90, 80, 255), win ? "W" : draw ? "D" : "L",
             LEFT, 1.1f);
        text(dl, f, g_title, 12, 64, y + 1, CREAM, line.c_str(), LEFT, 1.0f, 250);
    }
    // right: the leaderboard, ten places a page (left/right or LB/RB turn the page)
    if (!stats_busy()) {
        int want = v.board_page;
        if ((pad.left || pad.lb) && v.board_page > 0) want = v.board_page - 1;
        if ((pad.right || pad.rb) && v.board_page + 1 < v.board_pages) want = v.board_page + 1;
        if (want != v.board_page) { S.stats_page = want; account_fetch_stats_page(want); }
    }
    glass(dl, f, 330, 78, 276, 328);
    text(dl, f, g_title, 20, 344, 92, ORANGE, "LEADERBOARD", LEFT, 1.4f);
    if (v.board_pages > 1) {
        char pg[32];
        snprintf(pg, sizeof(pg), "%s  %d / %d  %s", v.board_page > 0 ? "<" : " ", v.board_page + 1, v.board_pages,
                 v.board_page + 1 < v.board_pages ? ">" : " ");
        text(dl, f, g_title, 14, 468, 382, stats_busy() ? GREY : CREAM, pg, CENTER, 1.1f);
    }
    text(dl, f, g_title, 10, 592, 100, GREY, "MOST WINS", RIGHT, 1.0f);
    if (v.board.empty())
        text(dl, f, g_title, 12, 344, 130, GREY, "Nobody yet. Three online matches signed in put you on it.", LEFT,
             1.0f, 250);
    for (size_t i = 0; i < v.board.size() && i < 10; i++) {
        const auto &e = v.board[i];
        if (e.size() < 5) continue;
        float y = 120 + i * 26;
        bool me = e[1] == v.name;
        if (me) rect(dl, f, 336, y - 3, 264, 24, IM_COL32(255, 168, 26, 60), 4);
        ImU32 col = e[0] == "1" ? IM_COL32(255, 205, 60, 255) : me ? CREAM : WHITE;
        text(dl, f, g_title, 16, 362, y, col, e[0].c_str(), RIGHT, 1.2f);
        text(dl, f, g_title, 15, 372, y + 1, col, e[1].c_str(), LEFT, 1.2f);
        std::string wl = e[2] + "-" + e[3];
        text(dl, f, g_title, 14, 592, y + 2, me ? CREAM : GREY, wl.c_str(), RIGHT, 1.1f);
    }
}

// ---- room list ----
//
// With Both servers (build 31) the list holds the rooms of Europe and North America, the lower ping
// first; each row carries its server and this PC's ping there, and joining a room on the other server
// moves the hub there first (netservers_join_on).

void draw_rooms(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    static NetServerRoom list[48];
    int n = list_rooms(list, 48);
    NetServerInfo sv[NETSERVER_COUNT];
    servers(sv);
    int here = connected_server();
    bool both = both_servers();
    unsigned long long now = GetTickCount64();
    if (!gallery_on() && now - S.last_list > 3000) { netplay_lobby_list(0xFF); S.last_list = now; }
    if (pad.up && n) S.rooms_sel = (S.rooms_sel + n - 1) % n;
    if (pad.down && n) S.rooms_sel = (S.rooms_sel + 1) % n;
    if (S.rooms_sel >= n) S.rooms_sel = n ? n - 1 : 0;
    if (pad.x) { netplay_lobby_list(0xFF); S.last_list = now; }
    if (pad.y) { S.page = CODE; S.code_pos = 0; return; }

    page_title(dl, f, "FIND A ROOM");
    if (both) text(dl, f, g_hand, 14, 54, 58, ORANGE, "Both servers: the lower ping first.", LEFT, 0.8f);
    online_count(dl, f);
    if (n) glass(dl, f, 36, 76, 290, (std::min(n, 6) - 1) * 52 + 44 + 20);
    else glass(dl, f, 36, 112, 290, 76);
    if (!n) {
        text(dl, f, g_title, 16, 60, 130, GREY, both ? "No open rooms on either server." : "No open rooms right now.");
        text(dl, f, g_hand, 14, 60, 156, ORANGE, "Quick match, or open one yourself.", LEFT, 0.8f);
    }
    int top = std::max(0, std::min(S.rooms_sel - 2, n - 6));
    for (int i = top; i < n && i < top + 6; i++) {
        const NetRoomInfo &r = list[i].room;
        float x = 50, y = 86 + (i - top) * 52, w = 262, h = 44;
        if (mouse_in(f, x, y, w, h)) S.rooms_sel = i;
        bool sel = i == S.rooms_sel;
        // a King of the Hill room takes players in any phase: they join the back of the line
        bool open = (r.phase == 0 || NETPLAY_IS_KOTH(r.mode)) && r.players < r.max;
        if (sel) selector(dl, f, x, y, w, h);
        text(dl, f, g_title, 19, x + 12, y + 3, sel ? CREAM : with_alpha(GREY, 0.9f), r.name, LEFT, 1.2f);
        char line[64];
        snprintf(line, sizeof(line), "%s  %d/%d", mode_name(r.mode), r.players, r.max);
        text(dl, f, g_hand, 13, x + 14, y + 26, open ? ORANGE : with_alpha(ORANGE, 0.45f), line, LEFT, 0.8f);
        if (both && list[i].server >= 0) {     // the server's tag and this PC's ping there, bottom right
            int ms = list[i].ping_ms;
            char tag[32];
            snprintf(tag, sizeof(tag), ms >= 0 ? "%s  %d MS" : "%s", sv[list[i].server].short_name, ms);
            float tw = text_width(f, g_title, 10, tag), tx = x + w - 8 - tw - 24;
            bool away = list[i].server != here;
            rect(dl, f, tx - 4, y + 26, tw + 30, 15, away ? IM_COL32(60, 40, 18, 210) : IM_COL32(0, 0, 0, 150), 4);
            ping_bars(dl, f, tx, y + 28, 10, ms);
            text(dl, f, g_title, 10, x + w - 8, y + 28, ping_colour(ms), tag, RIGHT, 0.8f);
        }
        if (mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
    }
    if (n) {   // the door: the chosen room
        const NetServerRoom &e = list[S.rooms_sel];
        const NetRoomInfo &r = e.room;
        bool open = (r.phase == 0 || NETPLAY_IS_KOTH(r.mode)) && r.players < r.max;
        char players[160];
        snprintf(players, sizeof(players), "%d of %d players. %s", r.players, r.max,
                 NETPLAY_IS_KOTH(r.mode) ? (open ? "Winner stays on. Press A to join the line." : "Full.") :
                 r.phase == 1 ? "Voting on the venue right now." : r.phase == 2 ? "A match is on." :
                 open ? "Open: press A to join." : "Full.");
        {   // this PC's ping to the server the room is on; on the other server, that joining moves there
            int ms = gallery_on() ? e.ping_ms : e.server == here ? netplay_rtt() : e.ping_ms;
            size_t at = strlen(players);
            if (e.server >= 0 && e.server != here)
                snprintf(players + at, sizeof(players) - at, ms >= 0 ? " On %s: your ping %d ms. Joining moves you there." :
                         " On %s: joining moves you there.", sv[e.server].name, ms);
            else if (ms >= 0)
                snprintf(players + at, sizeof(players) - at, " Your ping: %d ms.", ms);
        }
        door_panel(dl, f);
        image_fit(dl, f, tex(mode_logo(r.mode)), 383, 150, 160, 34);
        door_text(dl, f, nullptr, r.name, mode_name(r.mode), players);
    }
    if (pad.a && n) {
        const NetServerRoom &e = list[S.rooms_sel];
        const NetRoomInfo &r = e.room;
        if (!((r.phase == 0 || NETPLAY_IS_KOTH(r.mode)) && r.players < r.max)) say("That room is not open right now.");
        else if (e.server < 0 || e.server == here) { netplay_lobby_join(r.code); say("Joining..."); }
        else if (!gallery_on()) {
            netservers_join_on(e.server, r.code, r.mode, hub_name().c_str());
            say(std::string("Moving to ") + sv[e.server].name + " to join " + r.name + "...", 6000);
        }
    }
    if (pad.b) S.page = HUB;
}
// ---- join by code: six characters, picked with the pad or typed ----

void draw_code(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    const int n = (int)sizeof(CODE_CHARS) - 1;
    auto index = [&](char c) { const char *p = strchr(CODE_CHARS, c); return p && c ? (int)(p - CODE_CHARS) : 0; };
    if (pad.left) S.code_pos = (S.code_pos + 5) % 6;
    if (pad.right) S.code_pos = (S.code_pos + 1) % 6;
    if (pad.up) S.code[S.code_pos] = CODE_CHARS[(index(S.code[S.code_pos]) + 1) % n];
    if (pad.down) S.code[S.code_pos] = CODE_CHARS[(index(S.code[S.code_pos]) + n - 1) % n];
    ImGuiIO &io = ImGui::GetIO();
    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        char c = (char)toupper((int)io.InputQueueCharacters[i]);
        if (strchr(CODE_CHARS, c) && c) { S.code[S.code_pos] = c; S.code_pos = std::min(5, S.code_pos + 1); }
    }

    page_title(dl, f, "JOIN BY CODE");
    text(dl, f, g_hand, 15, 54, 70, ORANGE, "The code is shown in your friend's room.", LEFT, 0.8f, 270);
    door_panel(dl, f);
    door_text(dl, f, nullptr, "JOIN BY CODE", "SIX CHARACTERS",
              "Up and down change a character, left and right move. Or type it. A joins the room.");
    for (int i = 0; i < 6; i++) {
        float x = 70 + i * 46, y = 150;
        glass(dl, f, x, y, 40, 56, 6, i == S.code_pos ? IM_COL32(40, 30, 22, 255) : IM_COL32(10, 10, 13, 255), 0.7f);
        char c[2] = { S.code[i], 0 };
        text(dl, f, g_stencil, 34, x + 20, y + 10, i == S.code_pos ? CREAM : GREY, c, CENTER, 1.4f);
        if (i == S.code_pos) {
            selector(dl, f, x, y, 40, 56);
            text(dl, f, g_title, 14, x + 20, y - 18, ORANGE, "^", CENTER);
            text(dl, f, g_title, 14, x + 20, y + 58, ORANGE, "v", CENTER);
        }
        if (mouse_click(f, x, y, 40, 56)) S.code_pos = i;
    }
    if (pad.a) { netplay_lobby_join(S.code); say(std::string("Joining ") + S.code + "..."); S.page = HUB; }
    if (pad.b) S.page = ROOMS;
}

// ---- the venues of a room's pool, on the create page's glass panel ----

// One venue: its card from the game's art (a tilted photo with the venue's logo under it, on a clear
// ground), cut to the drawn part (a 1.2:1 box) on a dark backing, with its name banner under it; the
// card and the banner as large as the cell allows, centred in it.
void venue_tile(ImDrawList *dl, const Frame &f, int venue, float x, float y, float w, float h, float alpha,
                bool lit = false)
{
    const float banner = h > 200 ? 24 : h > 120 ? 18 : h > 60 ? 13 : 0, pad = 3;
    // the card's size in menu units, keeping 1.2:1 in pixels (a stretched picture scales x and y apart)
    float k = std::min((w - 2 * pad) * f.sx / 1.2f, (h - banner - 2 * pad) * f.sy);
    float cw = k * 1.2f / f.sx, ch = k / f.sy;
    float bw = cw + 2 * pad, bh = ch + 2 * pad, bx = x + (w - bw) / 2, by = y + (h - bh - banner) / 2;
    rect(dl, f, bx, by, bw, bh, IM_COL32(0, 0, 0, (int)(110 * alpha)), 6);
    if (lit) dl->AddRect(f.p(bx, by), f.p(bx + bw, by + bh), with_alpha(ORANGE, 0.8f * alpha), 6 * f.s(), 0, 1.5f * f.s());
    const Tex &t = venue_tex(venue);
    ImU32 tint = IM_COL32(255, 255, 255, (int)(255 * alpha));
    if (!image(dl, f, t, bx + pad, by + pad, cw, ch, tint, ImVec2(0.02f, 0.04f), ImVec2(0.98f, 0.84f)))
        text(dl, f, g_title, 13, bx + bw / 2, by + bh / 2 - 8, with_alpha(CREAM, alpha), online_venue_name(venue), CENTER,
             1.0f, bw - 8);
    if (banner > 0) {
        const Tex &name = venue_name_tex(venue);
        if (name.srv) image_fit(dl, f, name, x, by + bh + 1, w, banner - 2, tint);
        else text(dl, f, g_hand, banner - 4, x + w / 2, by + bh + 1, with_alpha(ORANGE, alpha), online_venue_name(venue),
                  CENTER, 0.8f, w);
    }
}

// The pool on the create page, filling the box under a caption (the preset and the count): one large
// card; two to four in a 2x2 grid; five or six in a 2x3 grid where they stay large enough; more (every
// venue: 22), or six where the grid would be small, as a showcase: one large card crossfading to the
// next every three seconds over a strip of the next ones and the count.
void venue_showcase(ImDrawList *dl, const Frame &f, const NetRoomSettings &s, float x, float y, float w, float h)
{
    int n = std::min(s.pool_count, 32);
    if (n <= 0) return;
    int p = preset_of(s);
    char caption[64];
    snprintf(caption, sizeof(caption), "%s  -  %d VENUE%s", upper(p >= 0 ? presets()[p].name : "Custom").c_str(), n,
             n == 1 ? "" : "S");
    text(dl, f, g_hand, 13, x + w / 2, y, ORANGE, caption, CENTER, 1.0f);
    y += 18;
    h -= 18;
    if (n == 1) {
        venue_tile(dl, f, s.pool[0], x, y, w, h, 1.0f);
        return;
    }
    int rows = n <= 4 ? 2 : 3;
    float gap = 6, cw = (w - gap) / 2, rh = (h - gap * (rows - 1)) / rows;
    if (n <= 4 || (n <= 6 && rh - 13 >= 60)) {
        for (int k = 0; k < n && k < rows * 2; k++) {
            bool alone = k == n - 1 && n % 2;      // an odd last one in the middle of its row
            float cx = alone ? x + (w - cw) / 2 : x + (k % 2) * (cw + gap);
            venue_tile(dl, f, s.pool[k], cx, y + (k / 2) * (rh + gap), cw, rh, 1.0f);
        }
        return;
    }
    // the showcase
    const float strip = 30, period = 3.0f, fade = 0.6f;
    float t = (float)ImGui::GetTime(), phase = fmodf(t, period);
    int cur = (int)(t / period) % n, next = (cur + 1) % n;
    float big_h = h - strip - 8;
    float mix = phase > period - fade ? (phase - (period - fade)) / fade : 0.0f;
    venue_tile(dl, f, s.pool[cur], x, y, w, big_h, 1.0f - mix);
    if (mix > 0) venue_tile(dl, f, s.pool[next], x, y, w, big_h, mix);
    // the strip: the shown card and the next ones, then where in the pool it is
    const int shown = 5;
    float sw = 30, sgap = (w - 50 - shown * sw) / (shown - 1), sy = y + h - strip;
    for (int k = 0; k < shown && k < n; k++)
        venue_tile(dl, f, s.pool[(cur + k) % n], x + k * (sw + sgap), sy, sw, strip, k == 0 ? 1.0f : 0.55f, k == 0);
    char count[16];
    snprintf(count, sizeof(count), "%d / %d", cur + 1, n);
    text(dl, f, g_title, 12, x + w, sy + 8, GREY, count, RIGHT, 0.8f);
}

// ---- room settings (new room, or the host's changes) ----

void draw_settings(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &room)
{
    // the rows; a King of the Hill room has one more, how each fight's venue is chosen
    enum Row { R_MODE, R_VENUE_CHOICE, R_PLAYERS, R_POOL, R_LISTED, R_OPEN };
    static const char *const labels[] = { "MODE", "VENUE OF EACH FIGHT", "PLAYERS", "VENUES", "LISTED", "" };
    NetRoomSettings &s = S.settings;
    bool was_koth = NETPLAY_IS_KOTH(s.mode);
    // the host cannot turn a room into the other kind (the relay refuses it)
    std::vector<int> cycle = S.settings_edit ? mode_cycle(was_koth, !was_koth) : mode_cycle();
    std::vector<int> rows_of = { R_MODE, R_PLAYERS, R_POOL, R_LISTED, R_OPEN };
    if (was_koth) rows_of.insert(rows_of.begin() + 1, R_VENUE_CHOICE);
    const int rows = (int)rows_of.size();
    if (S.settings_sel >= rows) S.settings_sel = rows - 1;
    if (pad.up) S.settings_sel = (S.settings_sel + rows - 1) % rows;
    if (pad.down) S.settings_sel = (S.settings_sel + 1) % rows;
    int step = pad.right ? 1 : pad.left ? -1 : 0;
    const OnlineMode *m = online_mode(s.mode);
    // King of the Hill: 3..10 in the line (two fight, the others wait their turn)
    auto least_of = [](int mode) { return NETPLAY_IS_KOTH(mode) ? 3 : 2; };
    auto most_of = [](int mode) {
        if (NETPLAY_IS_KOTH(mode)) return NETPLAY_ROOM_SLOTS;
        const OnlineMode *om = online_mode(mode);
        return om ? std::min(4, om->fighters) : 2;
    };
    if (step) {
        switch (rows_of[S.settings_sel]) {
        case R_MODE:
            if (!cycle.empty()) {
                s.mode = cycle[(cycle_index(cycle, s.mode) + step + (int)cycle.size()) % (int)cycle.size()];
                if (NETPLAY_IS_KOTH(s.mode) && !was_koth) s.max = 6;     // a fresh hill: room for a line
            }
            break;
        case R_VENUE_CHOICE:
            s.mode = s.mode == NETPLAY_MODE_KOTH ? NETPLAY_MODE_KOTH_RANDOM : NETPLAY_MODE_KOTH;
            break;
        case R_PLAYERS: s.max = std::max(least_of(s.mode), std::min(most_of(s.mode), s.max + step)); break;
        case R_POOL: apply_preset(s, (preset_of(s) < 0 ? 0 : preset_of(s)) + step); break;
        case R_LISTED: s.is_public = !s.is_public; break;
        }
    }
    m = online_mode(s.mode);
    if (s.max > most_of(s.mode)) s.max = most_of(s.mode);
    if (s.max < least_of(s.mode)) s.max = least_of(s.mode);
    if (NETPLAY_IS_KOTH(s.mode) != was_koth) {     // the row list changed: keep the cursor on MODE
        S.settings_sel = 0;
        was_koth = NETPLAY_IS_KOTH(s.mode);
    }
    bool koth = NETPLAY_IS_KOTH(s.mode);
    float row_step = koth ? 46 : 54, row_h = koth ? 40 : 44;

    page_title(dl, f, S.settings_edit ? "ROOM SETTINGS" : "CREATE A ROOM");
    text(dl, f, g_hand, 15, 54, 66, ORANGE, s.name, LEFT, 0.8f);
    for (int i = 0; i < rows && i < (int)rows_of.size(); i++) {
        int row = rows_of[i];
        float x = 50, y = 96 + i * row_step, w = 270, h = row_h;
        if (mouse_in(f, x, y, w, h)) S.settings_sel = i;
        bool sel = S.settings_sel == i;
        glass(dl, f, x, y, w, h, 7, sel ? IM_COL32(44, 32, 22, 255) : IM_COL32(10, 10, 13, 255), sel ? 0.78f : 0.6f);
        if (sel) selector(dl, f, x, y, w, h);
        if (row == R_OPEN) {
            text(dl, f, g_title, 20, x + w / 2, y + h / 2 - 11, sel ? CREAM : GREY, S.settings_edit ? "SAVE" : "OPEN THE ROOM", CENTER);
            if (mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
            continue;
        }
        text(dl, f, g_title, 12, x + 12, y + 4, GREY, labels[row]);
        std::string value;
        char buf[64];
        switch (row) {
        case R_MODE: value = mode_name(s.mode); break;
        case R_VENUE_CHOICE: value = s.mode == NETPLAY_MODE_KOTH ? "The two fighters vote" : "At random"; break;
        case R_PLAYERS:
            if (koth) { snprintf(buf, sizeof(buf), "Up to %d in the line", s.max); value = buf; break; }
            snprintf(buf, sizeof(buf), "%d players", s.max); value = buf;
            if (m && s.max < m->fighters) { snprintf(buf, sizeof(buf), " + %d CPU", m->fighters - s.max); value += buf; } break;
        case R_POOL: { int p = preset_of(s); value = p >= 0 ? presets()[p].name : "Custom"; snprintf(buf, sizeof(buf), " (%d)", s.pool_count); value += buf; } break;
        case R_LISTED: value = s.is_public ? "In the room list" : "By code only"; break;
        }
        text(dl, f, g_title, 17, x + 12, y + 19, sel ? WHITE : GREY, value.c_str());
        if (sel) { text(dl, f, g_title, 17, x + w - 26, y + h / 2 - 10, ORANGE, "<", CENTER); text(dl, f, g_title, 17, x + w - 10, y + h / 2 - 10, ORANGE, ">", CENTER); }
    }
    // the door: the mode's logo (King of the Hill: its rules), then the venues filling the panel
    door_panel(dl, f);
    float venues_y;
    if (koth) {
        text(dl, f, g_title, 24, 463, 92, CREAM, "KING OF THE HILL", CENTER, 1.6f);
        text(dl, f, g_hand, 13, 463, 120, ORANGE, "WINNER STAYS ON", CENTER, 1.0f);
        text(dl, f, g_body, 12, 362, 138, WHITE,
             "TWO FIGHT ONE ON ONE, THE OTHERS WAIT IN LINE. THE LOSER GOES TO THE BACK. "
             "WHEN YOU'RE CALLED, PRESS A WITHIN 20 SECONDS.", LEFT, 1.0f, 202);
        venues_y = 196;
    } else {
        image_fit(dl, f, tex(mode_logo(s.mode)), 372, 96, 170, 36);
        venues_y = 142;
    }
    venue_showcase(dl, f, s, 356, venues_y, 214, 438 - venues_y);
    if (pad.a && rows_of[S.settings_sel] == R_OPEN) {
        if (S.settings_edit) { netplay_lobby_settings(&s); S.page = HUB; say("Settings saved; everyone readies again."); }
        else { netplay_lobby_create(&s); say("Opening the room..."); S.page = HUB; }
    }
    if (pad.b) S.page = HUB;
    (void)room;
}

// ---- fighter choice: the game's portrait grid ----

void draw_fighters(ImDrawList *dl, const Frame &f, const Pad &pad, bool *chosen, int *fighter)
{
    const auto &list = roster();
    int n = (int)list.size();
    const int cols = 7, rows = 4;
    *chosen = false;
    if (!n) { S.page = HUB; return; }
    if (S.fighter_sel >= n) S.fighter_sel = 0;
    if (pad.left) S.fighter_sel = (S.fighter_sel + n - 1) % n;
    if (pad.right) S.fighter_sel = (S.fighter_sel + 1) % n;
    if (pad.up) S.fighter_sel = std::max(0, S.fighter_sel - cols);
    if (pad.down) S.fighter_sel = std::min(n - 1, S.fighter_sel + cols);
    if (pad.lb) S.fighter_sel = std::max(0, S.fighter_sel - cols * rows);
    if (pad.rb) S.fighter_sel = std::min(n - 1, S.fighter_sel + cols * rows);
    int row = S.fighter_sel / cols;
    if (row < S.fighter_top) S.fighter_top = row;
    if (row >= S.fighter_top + rows) S.fighter_top = row - rows + 1;

    page_title(dl, f, "CHOOSE FIGHTER");
    const OnlineFighter &cur = list[S.fighter_sel];
    // the chosen fighter, large, with name and style
    glass(dl, f, 36, 74, 180, 308);
    if (!portrait(dl, f, cur.id, 46, 84, 160, 240))
        text(dl, f, g_title, 26, 126, 180, with_alpha(CREAM, 0.6f), cur.name, CENTER, 1.4f, 150);
    text(dl, f, g_hand, 22, 126, 330, ORANGE, cur.name, CENTER, 1.2f);
    text(dl, f, g_title, 13, 126, 356, WHITE, cur.style, CENTER);
    // the grid
    float gx = 228, gy = 82, cw = 52, ch = 76;
    glass(dl, f, gx - 8, gy - 8, cols * (cw + 4) + 12, rows * (ch + 6) + 26);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            int i = (S.fighter_top + r) * cols + c;
            if (i >= n) break;
            float x = gx + c * (cw + 4), y = gy + r * (ch + 6);
            if (mouse_in(f, x, y, cw, ch)) S.fighter_sel = i;
            bool sel = i == S.fighter_sel;
            rect(dl, f, x, y, cw, ch, IM_COL32(0, 0, 0, 150), 3);
            if (!portrait(dl, f, list[i].id, x + 1, y + 1, cw - 2, ch - 2, sel ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 255)))
                text(dl, f, g_body, 9, x + 3, y + 30, GREY, list[i].name, LEFT, 0, cw - 4);
            if (sel) selector(dl, f, x, y, cw, ch);
            if (mouse_click(f, x, y, cw, ch)) const_cast<Pad &>(pad).a = true;
        }
    char page[32];
    snprintf(page, sizeof(page), "%d / %d", S.fighter_sel + 1, n);
    text(dl, f, g_title, 12, gx + cols * (cw + 4) - 4, gy + rows * (ch + 6), GREY, page, RIGHT, 0.8f);
    if (pad.a) { *chosen = true; *fighter = cur.id; }
    if (pad.b) S.page = HUB;
}

// ---- ranked: the search ----

void draw_ranked(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    int waited = 0, searching = 0;
    char why[96];
    int st = lobby_queue(&waited, &searching, why, sizeof(why));
    int fighter = saved_fighter();
    page_title(dl, f, "RANKED MATCH");
    online_count(dl, f);
    glass(dl, f, 36, 74, 180, 308);
    if (!portrait(dl, f, fighter, 46, 84, 160, 240))
        text(dl, f, g_title, 26, 126, 180, with_alpha(CREAM, 0.6f), online_fighter_name(fighter), CENTER, 1.4f, 150);
    text(dl, f, g_hand, 22, 126, 330, ORANGE, online_fighter_name(fighter), CENTER, 1.2f);
    glass(dl, f, 226, 86, 378, 206);

    float hx = 54;
    if (st == NETQUEUE_REFUSED || st == NETQUEUE_NONE) {
        text(dl, f, g_hand, 24, 240, 110, RED, "NOT SEARCHING", LEFT, 1.0f);
        text(dl, f, g_title, 14, 240, 150, WHITE, st == NETQUEUE_REFUSED ? why : "The search ended.", LEFT, 0.8f, 360);
        hx += hint(dl, f, hx, 392, "a", "TRY AGAIN");
        hint(dl, f, hx, 392, "b", "BACK");
        if (pad.a) netplay_lobby_ranked(fighter);
        if (pad.b) S.page = HUB;
        return;
    }
    // a pulsing line while the server looks
    float t = (float)(GetTickCount64() % 1600) / 1600.0f;
    const char *head = st == NETQUEUE_FOUND ? "OPPONENT FOUND!" : st == NETQUEUE_SEARCHING ? "SEARCHING" : "CHECKING YOUR ACCOUNT";
    text(dl, f, g_hand, 26, 240, 104, st == NETQUEUE_FOUND ? GREEN : ORANGE, head, LEFT, 1.0f);
    if (st == NETQUEUE_SEARCHING) {
        rect(dl, f, 240, 148, 340, 4, IM_COL32(255, 255, 255, 30), 2);
        rect(dl, f, 240 + 300 * (t < 0.5f ? t * 2 : 2 - t * 2), 148, 40, 4, ORANGE);
        char line[64];
        snprintf(line, sizeof(line), "%d:%02d", waited / 60, waited % 60);
        text(dl, f, g_title, 34, 240, 170, CREAM, line, LEFT, 1.6f);
        snprintf(line, sizeof(line), "%d PLAYER%s SEARCHING", searching, searching == 1 ? "" : "S");
        text(dl, f, g_title, 14, 240, 222, GREY, line, LEFT, 1.0f);
        text(dl, f, g_title, 12, 240, 250, WHITE,
             "The server looks for an opponent with a rating close to yours; the longer the search, the wider it looks.",
             LEFT, 0.8f, 340);
    }
    hint(dl, f, hx, 392, "b", "STOP SEARCHING");
    if (pad.b) { netplay_lobby_leave(); S.page = HUB; say("You left the ranked queue."); }
}

// ---- the room ----

void draw_vote(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r)
{
    const NetRoomPlayer *me = nullptr;
    for (int i = 0; i < r.player_count; i++) if (r.players[i].slot == r.you) me = &r.players[i];
    int n = r.candidate_count;
    if (n <= 0) return;
    if (S.vote_sel >= n) S.vote_sel = 0;
    if (pad.left) S.vote_sel = (S.vote_sel + n - 1) % n;
    if (pad.right) S.vote_sel = (S.vote_sel + 1) % n;
    int left = r.ms_left - (int)(GetTickCount64() - r.received);
    if (left < 0) left = 0;

    page_title(dl, f, "VOTE FOR THE VENUE");
    char secs[16];
    snprintf(secs, sizeof(secs), "%d", (left + 999) / 1000);
    text(dl, f, g_title, 34, 590, 22, left < 5000 ? RED : CREAM, secs, RIGHT, 1.6f);
    rect(dl, f, 50, 70, 540, 5, IM_COL32(255, 255, 255, 30), 2.5f);
    rect(dl, f, 50, 70, 540 * (left / 15000.0f), 5, ORANGE, 2.5f);
    float w = 170, gap = 15, x0 = 320 - (n * w + (n - 1) * gap) / 2;
    for (int i = 0; i < n; i++) {
        float x = x0 + i * (w + gap), y = 100;
        if (mouse_in(f, x, y, w, 290)) S.vote_sel = i;
        bool sel = i == S.vote_sel, mine = me && me->voted == i;
        glass(dl, f, x, y, w, 290, 10, mine ? IM_COL32(56, 14, 20, 255) : IM_COL32(10, 10, 13, 255), mine ? 0.68f : 0.62f);
        const Tex &photo = venue_tex(r.candidates[i]);
        if (!image(dl, f, photo, x + 5, y + 5, w - 10, w - 10, sel || mine ? IM_COL32_WHITE : IM_COL32(170, 170, 170, 255)))
            text(dl, f, g_title, 15, x + w / 2, y + 70, CREAM, online_venue_name(r.candidates[i]), CENTER, 1.2f, w - 10);
        const Tex &banner = venue_name_tex(r.candidates[i]);
        if (banner.srv) image_fit(dl, f, banner, x + 2, y + w + 2, w - 4, 26);
        else text(dl, f, g_hand, 13, x + w / 2, y + w + 6, ORANGE, online_venue_name(r.candidates[i]), CENTER, 0.8f);
        // the votes, as stars
        for (int k = 0; k < r.votes[i]; k++)
            text(dl, f, g_title, 26, x + w / 2 + (k - (r.votes[i] - 1) / 2.0f) * 26, y + w + 34, ORANGE, "*", CENTER, 1.2f);
        char votes[24];
        snprintf(votes, sizeof(votes), "%d VOTE%s", r.votes[i], r.votes[i] == 1 ? "" : "S");
        text(dl, f, g_title, 13, x + w / 2, y + 262, mine ? CREAM : GREY, votes, CENTER);
        if (sel) selector(dl, f, x, y, w, 290);
        if (mouse_click(f, x, y, w, 290)) const_cast<Pad &>(pad).a = true;
    }
    {   // a way out while voting too: the others go back to the room's lobby (relay leaveRoom)
        float hx = 54;
        hx += hint(dl, f, hx, 396, "a", "VOTE");
        hint(dl, f, hx, 396, "b", "LEAVE ROOM");
    }
    if (pad.b) { netplay_lobby_leave(); S.page = HUB; say("You left the room."); return; }
    if (pad.a) netplay_lobby_vote(S.vote_sel);
}

// ---- King of the Hill: the hill screen, "YOU'RE UP", and watching ----

const ImU32 GOLD = IM_COL32(255, 205, 60, 255);

// A crown, drawn: a gold band with three points, centred on (cx, cy), `w` wide.
void crown(ImDrawList *dl, const Frame &f, float cx, float cy, float w, ImU32 col = GOLD)
{
    float h = w * 0.62f, x0 = cx - w / 2, x1 = cx + w / 2, base = cy + h / 2, band = base - h * 0.28f, top = cy - h / 2;
    ImU32 dark = IM_COL32(120, 80, 10, 255);
    dl->AddRectFilled(f.p(x0, band), f.p(x1, base), col, 1.5f * f.s());
    const float px[3] = { x0, cx, x1 }, py[3] = { top + h * 0.12f, top, top + h * 0.12f };
    const float lx[3] = { x0, cx - w * 0.22f, x1 - w * 0.36f }, rx[3] = { x0 + w * 0.36f, cx + w * 0.22f, x1 };
    for (int i = 0; i < 3; i++) {
        dl->AddTriangleFilled(f.p(lx[i], band + 0.5f), f.p(px[i], py[i]), f.p(rx[i], band + 0.5f), col);
        dl->AddCircleFilled(f.p(px[i], py[i]), w * 0.07f * f.s(), col);
    }
    dl->AddRect(f.p(x0, band), f.p(x1, base), dark, 1.5f * f.s(), 0, 1.0f * f.s());
    for (int i = 0; i < 3; i++) dl->AddCircleFilled(f.p(x0 + w * (0.22f + 0.28f * i), (band + base) / 2), w * 0.05f * f.s(), RED);
}

// What the hill screen needs from ROOM and HILL, in one place.
struct HillView {
    const NetHill *h = nullptr;
    int my_state = -1, my_index = -1;   // this PC in the line (NETLINE_*), -1 not in it yet
    int pair[2] = { -1, -1 };           // the called or fighting pair's slots, in line order
    int next = -1;                      // "you're up next": the first waiting after the pair
    int ms_left = 0;
};

const NetRoomPlayer *room_player(const NetRoom &r, int slot)
{
    for (int i = 0; i < r.player_count; i++) if (r.players[i].slot == slot) return &r.players[i];
    return nullptr;
}

const char *slot_name(const NetRoom &r, int slot)
{
    const NetRoomPlayer *p = room_player(r, slot);
    return p && p->name[0] ? p->name : "?";
}

// The fighter a member fights with: the server's, or this PC's own pick for this PC.
int slot_fighter(const NetRoom &r, int slot)
{
    const NetRoomPlayer *p = room_player(r, slot);
    if (p && p->fighter != 0xFF) return p->fighter;
    return slot == r.you ? saved_fighter() : -1;
}

HillView hill_view(const NetRoom &r)
{
    HillView v;
    v.h = &r.hill;
    int n = 0;
    for (int i = 0; i < r.hill.count; i++) {
        const NetHillEntry &e = r.hill.line[i];
        if (e.slot == r.you) { v.my_state = e.state; v.my_index = i; }
        if (e.state >= NETLINE_CALLED && e.state <= NETLINE_FIGHTING && n < 2) v.pair[n++] = e.slot;
    }
    if (n == 2)
        for (int i = 0; i < r.hill.count; i++) {
            const NetHillEntry &e = r.hill.line[i];
            if (e.state == NETLINE_WAITING && e.slot != v.pair[0] && e.slot != v.pair[1]) { v.next = e.slot; break; }
        }
    v.ms_left = r.hill.ms_left - (int)(GetTickCount64() - r.hill.received);
    if (v.ms_left < 0) v.ms_left = 0;
    return v;
}

// The last fight's result, as the card between fights says it ("CARL BEATS DANA").
std::string last_result(const NetRoom &r)
{
    const NetHill &h = r.hill;
    std::string w = h.last_winner >= 0 ? upper(slot_name(r, h.last_winner)) : "?",
                l = h.last_loser >= 0 ? upper(slot_name(r, h.last_loser)) : "?";
    switch (h.last_how) {
    case NETHOW_WON: return w + " BEATS " + l;
    case NETHOW_LEFT: return w + " WINS: " + l + " LEFT";
    case NETHOW_DRAW: return "DRAW: " + w + " AND " + l + " TO THE BACK";
    case NETHOW_VOID: return "NO RESULT: " + w + " AND " + l + " TO THE BACK";
    }
    return "";
}

// The watching screen: the fight this PC does not play in. Phase 1 shows who fights, the king's
// crown and streak, and the line; the fight itself runs on the fighters' PCs only.
void draw_watch(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r, const HillView &v)
{
    const NetHill &h = r.hill;
    int a = v.pair[0], b = v.pair[1];
    if (h.watch_seed && h.watch_seed == h.seed) { a = h.watch_slots[0]; b = h.watch_slots[1]; }
    if (b >= 0 && a == h.king) std::swap(a, b);     // the king on the right, the challenger on the left
    // the "WATCHING" tag, as a broadcast's
    rect(dl, f, 50, 26, 118, 28, with_alpha(RED, 0.95f), 3);
    dl->AddCircleFilled(f.p(64, 40), 5 * f.s(), (GetTickCount64() / 600) % 2 ? WHITE : with_alpha(WHITE, 0.3f));
    text(dl, f, g_title, 18, 76, 30, WHITE, "WATCHING", LEFT, 1.0f);
    text(dl, f, g_hand, 14, 180, 34, ORANGE, r.settings.name, LEFT, 0.8f);
    text(dl, f, g_title, 14, 590, 30, GOLD, "KING OF THE HILL", RIGHT, 1.1f);
    int sides[2] = { a, b };
    for (int k = 0; k < 2; k++) {
        int slot = sides[k];
        float x = k == 0 ? 70 : 410, y = 84, w = 160, ph = 206;
        glass(dl, f, x, y, w, ph + 64);
        int fighter = slot >= 0 ? slot_fighter(r, slot) : -1;
        if (slot >= 0 && h.watch_seed && h.watch_seed == h.seed)      // the fight's own pick, from WATCH
            fighter = h.watch_slots[0] == slot ? h.watch_fighters[0] : h.watch_slots[1] == slot ? h.watch_fighters[1] : fighter;
        if (fighter < 0 || !portrait(dl, f, fighter, x + 5, y + 5, w - 10, ph - 10))
            text(dl, f, g_title, 40, x + w / 2, y + 80, GREY, "?", CENTER);
        if (slot >= 0 && slot == h.king) {
            crown(dl, f, x + w / 2, y - 17, 40);
            char streak[32];
            snprintf(streak, sizeof(streak), "KING  x%d", h.streak);
            text(dl, f, g_title, 13, x + w - 8, y + 10, GOLD, streak, RIGHT, 1.2f);
        }
        text(dl, f, g_hand, 20, x + w / 2, y + ph + 2, slot == r.you ? ORANGE : CREAM, slot >= 0 ? slot_name(r, slot) : "?", CENTER, 1.2f);
        text(dl, f, g_title, 12, x + w / 2, y + ph + 34, GREY, fighter >= 0 ? online_fighter_name(fighter) : "", CENTER, 0.8f);
    }
    text(dl, f, g_stencil, 52, 320, 168, CREAM, "VS", CENTER, 2.0f);
    if (h.watch_seed && h.watch_seed == h.seed && h.watch_venue)
        text(dl, f, g_hand, 14, 320, 236, ORANGE, upper(online_venue_name(h.watch_venue)).c_str(), CENTER, 0.8f, 160);
    // the line behind the fight: the next three
    std::string line = "NEXT:";
    int shown = 0;
    for (int i = 0; i < h.count && shown < 3; i++) {
        const NetHillEntry &e = h.line[i];
        if (e.state != NETLINE_WAITING || e.slot == a || e.slot == b) continue;
        char one[48];
        snprintf(one, sizeof(one), "%s  %d. %s", shown ? "," : "", shown + 1, upper(slot_name(r, e.slot)).c_str());
        line += one;
        shown++;
    }
    if (!shown) line = "NOBODY ELSE IN THE LINE";
    glass(dl, f, 40, 382, 560, 28, 8);
    text(dl, f, g_title, 14, 52, 388, WHITE, line.c_str(), LEFT, 1.0f);
    if (v.next == r.you) text(dl, f, g_hand, 18, 588, 386, ORANGE, "YOU'RE UP NEXT", RIGHT, 1.2f);
    text(dl, f, g_title, 11, 320, 362, GREY, "The fight runs on the fighters' PCs. The result comes here when it is over.",
         CENTER, 0.8f);
    float hx = 54;
    hx += hint(dl, f, hx, 414, "b", "BACK TO THE HILL");
    (void)hx;
    if (pad.b) S.watch_hidden = h.seed ? h.seed : 1;
}

// "YOU'RE UP": this PC is called; one press readies it with its fighter.
void draw_up(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r, const HillView &v)
{
    int fighter = slot_fighter(r, r.you);     // the fighter the room knows (the last READY), else this PC's pick
    bool ready = v.my_state == NETLINE_READY;
    int other = v.pair[0] == r.you ? v.pair[1] : v.pair[0];
    float pulse = 0.65f + 0.35f * sinf((float)(GetTickCount64() % 1000) / 1000.0f * 6.2832f);
    glass(dl, f, 36, 66, 180, 308);
    glass(dl, f, 226, 60, 384, 316);
    if (!portrait(dl, f, fighter, 46, 76, 160, 240))
        text(dl, f, g_title, 26, 126, 170, with_alpha(CREAM, 0.6f), online_fighter_name(fighter), CENTER, 1.4f, 150);
    text(dl, f, g_hand, 20, 126, 324, ORANGE, online_fighter_name(fighter), CENTER, 1.2f);
    text(dl, f, g_stencil, 58, 240, 72, ready ? GREEN : with_alpha(CREAM, pulse), ready ? "READY" : "YOU'RE UP", LEFT, 2.2f);
    std::string vs = std::string("VS ") + (other >= 0 ? upper(slot_name(r, other)) : std::string("?"));
    if (other >= 0 && other == r.hill.king) {
        char s[32];
        snprintf(s, sizeof(s), "   (KING x%d)", r.hill.streak);
        vs += s;
    }
    text(dl, f, g_title, 22, 242, 146, WHITE, vs.c_str(), LEFT, 1.4f);
    if (r.you == r.hill.king) text(dl, f, g_hand, 16, 242, 176, GOLD, "DEFEND THE HILL", LEFT, 1.0f);
    char secs[16];
    snprintf(secs, sizeof(secs), "%d", (v.ms_left + 999) / 1000);
    text(dl, f, g_title, 64, 594, 206, v.ms_left < 6000 ? RED : CREAM, secs, RIGHT, 2.0f);
    rect(dl, f, 242, 290, 352, 6, IM_COL32(255, 255, 255, 30), 3);
    rect(dl, f, 242, 290, 352 * std::min(1.0f, v.ms_left / 20000.0f), 6, v.ms_left < 6000 ? RED : ORANGE, 3);
    const char *say_line = ready ? "Waiting for your opponent to press A..."
                                 : "Press A now: not ready in time sends you to the back of the line.";
    text(dl, f, g_hand, 16, 242, 304, ORANGE, say_line, LEFT, 0.8f, 360);
    float hx = 242;
    if (!ready) hx += hint(dl, f, hx, 352, "a", "READY");
    if (!ready) hx += hint(dl, f, hx, 352, "x", "CHOOSE FIGHTER");
    (void)hx;
    if (pad.a && !ready) netplay_lobby_ready(1, fighter);
    if (pad.x && !ready) {
        S.page = FIGHTERS;
        const auto &list = roster();
        for (size_t i = 0; i < list.size(); i++) if (list[i].id == fighter) S.fighter_sel = (int)i;
    }
}

// The hill screen: the line, the king, who is up, the countdown.
void draw_hill(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r)
{
    const NetHill &h = r.hill;
    HillView v = hill_view(r);
    bool host = r.you == r.host;
    int fighter = slot_fighter(r, r.you);     // the fighter the room knows (the last READY), else this PC's pick
    bool fighting_me = v.my_state == NETLINE_FIGHTING;
    // the fighters vote on the venue as in other rooms; everyone else stays on the hill
    if (r.phase == 1 && fighting_me) { draw_vote(dl, f, pad, r); return; }
    if (h.valid && h.phase == NETHILL_FIGHTING && !fighting_me && S.watch_hidden != (h.seed ? h.seed : 1)) {
        draw_watch(dl, f, pad, r, v);
        return;
    }
    if (h.valid && h.phase == NETHILL_CALLING && (v.my_state == NETLINE_CALLED || v.my_state == NETLINE_READY)) {
        draw_up(dl, f, pad, r, v);
        return;
    }
    if (h.phase != NETHILL_FIGHTING) S.watch_hidden = 0;

    page_title(dl, f, r.settings.name);
    text(dl, f, g_title, 16, 590, 26, GOLD, "KING OF THE HILL", RIGHT, 1.2f);
    char code[32];
    snprintf(code, sizeof(code), "CODE  %s", r.code);
    text(dl, f, g_title, 14, 590, 58, ORANGE, code, RIGHT);

    // left: the line, numbered from the top
    glass(dl, f, 36, 76, 296, 318, 12, IM_COL32(10, 10, 13, 255), 0.65f);
    glass(dl, f, 346, 78, 252, 316, 12, IM_COL32(10, 10, 13, 255), 0.5f);
    text(dl, f, g_hand, 14, 52, 84, ORANGE, "THE LINE", LEFT, 1.0f);
    char cap[32];
    snprintf(cap, sizeof(cap), "%d / %d", h.valid ? h.count : r.player_count, r.settings.max);
    text(dl, f, g_title, 11, 318, 86, GREY, cap, RIGHT, 0.8f);
    if (!h.valid) text(dl, f, g_title, 13, 52, 110, GREY, "Waiting for the server...", LEFT, 0.8f);
    for (int i = 0; h.valid && i < h.count; i++) {
        const NetHillEntry &e = h.line[i];
        float x = 46, y = 104 + i * 28, w = 278, rh = 26;
        bool me = e.slot == r.you, king = e.slot == h.king && i == 0;
        bool up = e.slot == v.pair[0] || e.slot == v.pair[1];
        if (up) rect(dl, f, x, y, w, rh, e.state == NETLINE_FIGHTING ? IM_COL32(110, 14, 26, 170) : IM_COL32(120, 80, 14, 150), 5);
        else if (me) rect(dl, f, x, y, w, rh, IM_COL32(255, 168, 26, 40), 5);
        ImU32 col = e.state == NETLINE_AWAY ? with_alpha(GREY, 0.7f) : me ? ORANGE : CREAM;
        if (king) crown(dl, f, x + 12, y + rh / 2, 18);
        else {
            char num[8];
            snprintf(num, sizeof(num), "%d", i + 1);
            text(dl, f, g_title, 15, x + 20, y + 5, e.state == NETLINE_AWAY ? GREY : WHITE, num, RIGHT, 1.0f);
        }
        int fid = slot_fighter(r, e.slot);
        if (fid >= 0) portrait(dl, f, fid, x + 28, y + 1, 17, rh - 2, e.state == NETLINE_AWAY ? IM_COL32(110, 110, 110, 255) : IM_COL32_WHITE);
        text(dl, f, g_title, 14, x + 52, y + 1, col, slot_name(r, e.slot), LEFT, 1.0f);
        text(dl, f, g_title, 9, x + 53, y + 15, GREY, fid >= 0 ? online_fighter_name(fid) : "", LEFT, 0.6f);
        char wins[16];
        snprintf(wins, sizeof(wins), "%dW", e.wins);
        text(dl, f, g_title, 11, x + 200, y + 7, GREY, wins, RIGHT, 0.8f);
        const char *tag = nullptr;
        ImU32 tcol = GREY;
        switch (e.state) {
        case NETLINE_CALLED: tag = "CALLED"; tcol = (GetTickCount64() / 400) % 2 ? ORANGE : with_alpha(ORANGE, 0.5f); break;
        case NETLINE_READY: tag = "READY"; tcol = GREEN; break;
        case NETLINE_FIGHTING: tag = h.phase == NETHILL_VOTING ? "VOTING" : "FIGHTING"; tcol = RED; break;
        case NETLINE_AWAY: tag = "AWAY"; break;
        default: if (e.slot == v.next) { tag = "NEXT"; tcol = CREAM; } break;
        }
        if (tag) text(dl, f, g_title, 11, x + w - 6, y + 7, tcol, tag, RIGHT, 0.9f);
    }

    // right: the king, with the crown and the streak
    float kx = 412, ky = 100;
    if (h.valid && h.king >= 0) {
        int kf = slot_fighter(r, h.king);
        rect(dl, f, kx, ky, 102, 152, IM_COL32(0, 0, 0, 150), 4);
        if (kf < 0 || !portrait(dl, f, kf, kx + 3, ky + 3, 96, 146)) text(dl, f, g_title, 34, kx + 51, ky + 50, GREY, "?", CENTER);
        crown(dl, f, kx + 51, ky - 4, 40);
        text(dl, f, g_hand, 20, 463, 256, h.king == r.you ? ORANGE : CREAM, slot_name(r, h.king), CENTER, 1.2f);
        char streak[48];
        snprintf(streak, sizeof(streak), h.streak == 1 ? "%d WIN IN A ROW" : "%d WINS IN A ROW", h.streak);
        text(dl, f, g_title, 15, 463, 280, GOLD, streak, CENTER, 1.2f);
    } else {
        crown(dl, f, 463, 140, 64, with_alpha(GREY, 0.5f));
        text(dl, f, g_title, 20, 463, 190, CREAM, "NO KING YET", CENTER, 1.4f);
        text(dl, f, g_hand, 14, 463, 218, ORANGE, "WIN A FIGHT TO TAKE THE HILL", CENTER, 1.0f);
    }
    // what happens now
    std::string now_line, sub;
    char buf[96];
    switch (h.valid ? h.phase : NETHILL_WAITING) {
    case NETHILL_WAITING:
        now_line = "WAITING FOR PLAYERS";
        sub = "Two players who are not away start the hill.";
        break;
    case NETHILL_CALLING:
        now_line = "UP: " + upper(slot_name(r, v.pair[0])) + " VS " + upper(slot_name(r, v.pair[1]));
        snprintf(buf, sizeof(buf), "Both press A within %d s or go to the back.", (v.ms_left + 999) / 1000);
        sub = buf;
        break;
    case NETHILL_VOTING:
        now_line = upper(slot_name(r, v.pair[0])) + " AND " + upper(slot_name(r, v.pair[1])) + " PICK THE VENUE";
        break;
    case NETHILL_FIGHTING:
        now_line = "FIGHT ON: " + upper(slot_name(r, v.pair[0])) + " VS " + upper(slot_name(r, v.pair[1]));
        sub = fighting_me ? "" : "You watch from here; the result comes when it is over.";
        break;
    }
    text(dl, f, g_title, 15, 463, 308, WHITE, now_line.c_str(), CENTER, 1.2f, 250);
    if (!sub.empty()) text(dl, f, g_title, 11, 463, 330, GREY, sub.c_str(), CENTER, 0.8f, 250);
    if (h.valid && h.last_how != NETHOW_NONE) {
        std::string res = last_result(r);
        text(dl, f, g_hand, 13, 463, 352, h.last_how == NETHOW_WON || h.last_how == NETHOW_LEFT ? ORANGE : GREY,
             ("LAST FIGHT: " + res).c_str(), CENTER, 0.8f, 250);
    }
    // this PC's place, under the door (the line can fill the left panel)
    if (v.my_state == NETLINE_AWAY)
        text(dl, f, g_hand, 13, 463, 370, RED, "YOU ARE AWAY: PRESS A TO GET BACK IN THE LINE", CENTER, 0.8f, 250);
    else if (v.next == r.you)
        text(dl, f, g_hand, 18, 463, 368, ORANGE, "YOU'RE UP NEXT", CENTER, 1.2f);

    float hx = 54, hy = 402;
    if (v.my_state == NETLINE_AWAY) hx += hint(dl, f, hx, hy, "a", "I'M BACK");
    hx += hint(dl, f, hx, hy, "x", "CHOOSE FIGHTER");
    bool fight_on = h.valid && h.phase == NETHILL_FIGHTING && !fighting_me;
    bool settings_ok = host && h.valid && h.phase != NETHILL_VOTING && h.phase != NETHILL_FIGHTING;
    if (fight_on) hx += hint(dl, f, hx, hy, "y", "WATCH");
    else if (settings_ok) hx += hint(dl, f, hx, hy, "y", "ROOM SETTINGS");
    hx += hint(dl, f, hx, hy, "rb", "VOICE");
    hint(dl, f, hx, hy, "b", "LEAVE ROOM");

    if (pad.a && v.my_state == NETLINE_AWAY) netplay_lobby_ready(1, fighter);
    if (pad.x) {
        S.page = FIGHTERS;
        const auto &list = roster();
        for (size_t i = 0; i < list.size(); i++) if (list[i].id == fighter) S.fighter_sel = (int)i;
    }
    if (pad.y && fight_on) S.watch_hidden = 0;
    else if (pad.y && settings_ok) {
        S.settings = r.settings;
        S.settings_edit = true;
        S.settings_sel = 0;
        S.page = SETTINGS;
    }
    if (pad.rb) { S.page = VOICE; S.voice_sel = 0; }
    if (pad.b) { netplay_lobby_leave(); S.page = HUB; say("You left the room."); }
}

void draw_room(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r)
{
    if (NETPLAY_IS_KOTH(r.settings.mode)) { draw_hill(dl, f, pad, r); return; }
    const NetRoomPlayer *me = nullptr;
    for (int i = 0; i < r.player_count; i++) if (r.players[i].slot == r.you) me = &r.players[i];
    bool host = r.you == r.host;
    int fighter = saved_fighter();
    if (S.room_version != r.version) S.room_version = r.version;

    if (r.phase == 1) { draw_vote(dl, f, pad, r); return; }

    page_title(dl, f, r.settings.name);
    image_fit(dl, f, tex(mode_logo(r.settings.mode)), 360, 22, 220, 30);
    char code[32];
    snprintf(code, sizeof(code), "CODE  %s", r.code);
    text(dl, f, g_title, 14, 590, 58, ORANGE, code, RIGHT);

    // The server, and a warning when a match here would have a high ping: the path between two
    // players goes through the relay, about half of each one's round trip.
    NetServerInfo sv[NETSERVER_COUNT];
    servers(sv);
    int cur = connected_server();
    int worst = -1;
    {
        int a = -1, b = -1;     // the two highest known pings
        for (int i = 0; i < r.player_count; i++) {
            int p = room_ping(r.players[i].slot);
            if (p < 0) continue;
            if (p > a) { b = a; a = p; } else if (p > b) b = p;
        }
        if (a >= 0 && b >= 0) worst = (a + b) / 2;
    }
    if (cur >= 0) text(dl, f, g_title, 10, 590, 73, GREY, (upper(sv[cur].name) + " SERVER").c_str(), RIGHT, 0.6f);
    if (worst > 150) {
        char warn[96];
        snprintf(warn, sizeof(warn), "HIGH PING (ABOUT %d MS): INPUTS WILL FEEL DELAYED", worst);
        float ww = text_width(f, g_title, 11, warn);
        rect(dl, f, 48, 61, ww + 14, 18, IM_COL32(150, 14, 30, 220), 5);
        text(dl, f, g_title, 11, 55, 63, WHITE, warn, LEFT, 0.8f);
    }

    // the players' cards
    int places = std::max(r.settings.max, r.player_count);
    float w = 128, gap = 10, x0 = 320 - (places * w + (places - 1) * gap) / 2;
    for (int i = 0; i < places; i++) {
        float x = x0 + i * (w + gap), y = 86;
        glass(dl, f, x, y, w, 270, 10, i < r.player_count && r.players[i].slot == r.you ?IM_COL32(44, 30, 20, 255) : IM_COL32(10, 10, 13, 255), 0.62f);
        if (i >= r.player_count) {
            text(dl, f, g_hand, 15, x + w / 2, y + 110, with_alpha(ORANGE, 0.6f), "WAITING", CENTER, 0.8f);
            text(dl, f, g_title, 11, x + w / 2, y + 134, GREY, "an open place", CENTER, 0.6f);
            continue;
        }
        const NetRoomPlayer &p = r.players[i];
        // the server knows a fighter from the first READY; until then this PC shows its own pick
        int shown = p.fighter != 0xFF ? p.fighter : p.slot == r.you ? fighter : -1;
        bool known = shown >= 0;
        if (!known || !portrait(dl, f, shown, x + 4, y + 4, w - 8, (w - 8) * 1.5f))
            text(dl, f, g_title, 40, x + w / 2, y + 70, GREY, "?", CENTER);
        text(dl, f, g_hand, 15, x + w / 2, y + 200, p.slot == r.you ? ORANGE : CREAM, p.name, CENTER, 1.0f);
        text(dl, f, g_title, 11, x + w / 2, y + 222, GREY, known ? online_fighter_name(shown) : "choosing", CENTER, 0.6f);
        if (p.slot == r.host) text(dl, f, g_title, 10, x + 6, y + 6, ORANGE, "HOST", LEFT, 1.0f);
        {   // the player's ping to the server, when it is known (builds from 30 report it)
            int ms = room_ping(p.slot);
            if (ms >= 0) {
                char line[16];
                snprintf(line, sizeof(line), "%d MS", ms);
                float lw = text_width(f, g_title, 10, line);
                rect(dl, f, x + w - lw - 31, y + 4, lw + 27, 15, IM_COL32(0, 0, 0, 160), 4);
                ping_bars(dl, f, x + w - lw - 28, y + 6, 10, ms);
                text(dl, f, g_title, 10, x + w - 5, y + 6, ping_colour(ms), line, RIGHT, 1.0f);
            }
        }
        if (p.ready) {
            rect(dl, f, x + 14, y + 240, w - 28, 22, with_alpha(RED, 0.9f), 5);
            text(dl, f, g_title, 15, x + w / 2, y + 242, WHITE, "READY", CENTER, 0.8f);
        }
    }
    // hints
    bool ready = me && me->ready;
    float hx = 54, hy = 392;
    // Alone in a quick room for 30 s while the other server has players: offer to search there (one
    // press; never by itself). A room this PC opened or joined by its code is not a quick room.
    if (S.quick_wanted && r.phase == 0) { snprintf(S.quick_room, sizeof(S.quick_room), "%s", r.code); S.quick_wanted = false; }
    bool alone = r.player_count < 2 && r.phase == 0 && !strcmp(S.quick_room, r.code);
    if (!alone) S.alone_since = 0;
    else if (!S.alone_since) S.alone_since = GetTickCount64();
    // Both servers: after 20 s alone, a player waiting for this mode on the other server is met there
    // (the player chose Both, so a higher ping is fine); at most three moves per QUICK MATCH. The
    // other choices keep the hint below, which never moves by itself.
    if (both_servers() && alone && !gallery_on() && S.auto_moves < 3 && !netservers_moving()) {
        int waiting = 0, ping = -1;
        int to = netservers_quick_move(S.last_quick, (int)((GetTickCount64() - S.alone_since) / 1000), r.code, &waiting, &ping);
        if (to >= 0) {
            char line[160];
            snprintf(line, sizeof(line), "Nobody here yet: moving to %s (%d ms), where %d player%s waiting for %s.",
                     sv[to].name, ping, waiting, waiting == 1 ? " is" : "s are", mode_name(S.last_quick));
            say(line, 8000);
            S.auto_moves++;
            S.quick_room[0] = 0;
            S.alone_since = 0;
            netservers_quick_on(to, S.last_quick, S.last_quick_max, hub_name().c_str());
            S.page = HUB;
            return;
        }
    }
    int there = alone && !both_servers() && GetTickCount64() - S.alone_since >= 30000 ? other_server(sv, cur) : -1;
    int others = there >= 0 ? sv[there].waiting + sv[there].idle : 0;
    if (both_servers() && alone && r.player_count < 2)
        text(dl, f, g_hand, 15, 320, 366, ORANGE, "Waiting for another player on Europe and North America...", CENTER, 0.8f);
    else if (there >= 0 && others > 0) {
        char line[128];
        if (sv[there].waiting > 0)
            snprintf(line, sizeof(line), "%d player%s waiting for a match on %s (%d ms).", sv[there].waiting,
                     sv[there].waiting == 1 ? " is" : "s are", sv[there].name, sv[there].ping_ms);
        else
            snprintf(line, sizeof(line), "%d player%s online on %s (%d ms).", sv[there].idle,
                     sv[there].idle == 1 ? " is" : "s are", sv[there].name, sv[there].ping_ms);
        float lw = text_width(f, g_hand, 14, line), bw = lw + 150, bx = 320 - bw / 2;
        glass(dl, f, bx, 359, bw, 27, 8, IM_COL32(44, 30, 20, 255), 0.8f);
        bool hot = mouse_in(f, bx, 359, bw, 27);
        if (hot) frame_rect(dl, f, bx, 359, bw, 27, with_alpha(ORANGE, 0.8f), 1.2f);
        text(dl, f, g_hand, 14, bx + 10, 364, CREAM, line, LEFT, 0.8f);
        hint(dl, f, bx + lw + 18, 364, "lb", "SEARCH THERE");
        if (pad.lb || (hot && ImGui::IsMouseClicked(0))) {
            S.quick_room[0] = 0;
            S.fallback_tried = true;            // a choice the player made: no automatic second try
            netservers_quick_on(there, S.last_quick, S.last_quick_max, hub_name().c_str());
            say(std::string("Searching on ") + sv[there].name + "...");
            S.page = HUB;
            return;
        }
    } else if (r.player_count < 2) text(dl, f, g_hand, 15, 320, 366, ORANGE, "Waiting for another player...", CENTER, 0.8f);
    else if (!ready) text(dl, f, g_hand, 15, 320, 366, ORANGE, "Ready up: when everyone is, the venue vote starts.", CENTER, 0.8f);
    hx += hint(dl, f, hx, hy, "a", ready ? "NOT READY" : "READY");
    if (!ready) hx += hint(dl, f, hx, hy, "x", "CHOOSE FIGHTER");
    if (host && !ready) hx += hint(dl, f, hx, hy, "y", "ROOM SETTINGS");
    hx += hint(dl, f, hx, hy, "rb", "VOICE");
    hint(dl, f, hx, hy, "b", "LEAVE ROOM");

    if (pad.a) netplay_lobby_ready(ready ? 0 : 1, fighter);
    if (pad.x && !ready) {
        S.page = FIGHTERS;
        const auto &list = roster();
        for (size_t i = 0; i < list.size(); i++) if (list[i].id == fighter) S.fighter_sel = (int)i;
    }
    if (pad.y && host && !ready) {
        S.settings = r.settings;
        S.settings_edit = true;
        S.settings_sel = 0;
        S.page = SETTINGS;
    }
    if (pad.rb) { S.page = VOICE; S.voice_sel = 0; }
    if (pad.b) { netplay_lobby_leave(); S.page = HUB; say("You left the room."); }
}

// ---- voice chat (voice.c): the settings, and each other player in the room ----

void draw_voice(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r)
{
    static const char *const modes[] = { "OFF", "PUSH TO TALK", "OPEN MICROPHONE" };
    int others[NETPLAY_ROOM_SLOTS], n_others = 0;
    if (r.in_room)
        for (int i = 0; i < r.player_count && n_others < NETPLAY_ROOM_SLOTS; i++)
            if (r.players[i].slot != r.you) others[n_others++] = i;
    static const char *const pads[] = { "OFF", "BACK", "LEFT STICK CLICK", "RIGHT STICK CLICK", "D-PAD UP", "D-PAD DOWN" };
    const int fixed = 5, rows = fixed + n_others + 1;           // the settings, the players, BACK
    if (S.voice_sel >= rows) S.voice_sel = rows - 1;
    if (pad.up) S.voice_sel = (S.voice_sel + rows - 1) % rows;
    if (pad.down) S.voice_sel = (S.voice_sel + 1) % rows;
    int step = pad.right ? 1 : pad.left ? -1 : 0;
    int mode = recomp_settings_get("online", "voice", 1), vol = recomp_settings_get("online", "voice_volume", 100);
    int mic = recomp_settings_get("online", "voice_mic_volume", 100), agc = recomp_settings_get("online", "voice_auto_level", 1);
    int vpad = recomp_settings_get("online", "voice_pad", 0);
    if (step) {
        switch (S.voice_sel) {
        case 0: recomp_settings_set("online", "voice", (mode + step + 3) % 3); break;
        case 1: recomp_settings_set("online", "voice_volume", std::max(0, std::min(200, vol + step * 10))); break;
        case 2: recomp_settings_set("online", "voice_mic_volume", std::max(0, std::min(400, mic + step * 10))); break;
        case 3: recomp_settings_set("online", "voice_auto_level", agc ? 0 : 1); break;
        case 4: recomp_settings_set("online", "voice_pad", (vpad + step + 6) % 6); break;
        default:
            if (S.voice_sel - fixed < n_others) {
                int slot = r.players[others[S.voice_sel - fixed]].slot;
                voice_set_player_volume(slot, voice_player_volume(slot) + step * 25);
            }
        }
        mode = recomp_settings_get("online", "voice", 1);
        vol = recomp_settings_get("online", "voice_volume", 100);
        mic = recomp_settings_get("online", "voice_mic_volume", 100);
        agc = recomp_settings_get("online", "voice_auto_level", 1);
        vpad = recomp_settings_get("online", "voice_pad", 0);
    }

    page_title(dl, f, "VOICE CHAT");
    std::string key = setting_text("voice_key", "T");
    // a room's players add rows (a full King of the Hill room nine): from eight rows on they get closer
    // together, so the rows and the microphone's meter end above the hints
    float row_step = rows > 7 ? 300.0f / rows : 39, row_h = row_step - 5;
    for (int i = 0; i < rows; i++) {
        float x = 50, y = 80 + i * row_step, w = 270, h = row_h;
        if (mouse_in(f, x, y, w, h)) S.voice_sel = i;
        bool sel = S.voice_sel == i;
        glass(dl, f, x, y, w, h, 7, sel ? IM_COL32(44, 32, 22, 255) : IM_COL32(10, 10, 13, 255), sel ? 0.78f : 0.6f);
        if (sel) selector(dl, f, x, y, w, h);
        if (i == rows - 1) {
            text(dl, f, g_title, row_h < 30 ? 13 : 18, x + w / 2, y + (row_h < 30 ? (row_h - 14) / 2 : 7), sel ? CREAM : GREY, "BACK", CENTER);
            if (mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
            continue;
        }
        char label[48], value[48];
        if (i == 0) {
            snprintf(label, sizeof(label), "VOICE CHAT");
            snprintf(value, sizeof(value), mode == 1 ? "%s  (%s)" : "%s", modes[mode >= 0 && mode < 3 ? mode : 1], key.c_str());
        } else if (i == 1) {
            snprintf(label, sizeof(label), "EVERYONE ELSE");
            snprintf(value, sizeof(value), vol ? "%d %%" : "MUTED", vol);
        } else if (i == 2) {
            snprintf(label, sizeof(label), "MY MICROPHONE");
            snprintf(value, sizeof(value), "%d %%", mic);
        } else if (i == 3) {
            snprintf(label, sizeof(label), "AUTO LEVEL");
            snprintf(value, sizeof(value), agc ? "ON  (quiet microphones louder)" : "OFF");
        } else if (i == 4) {
            snprintf(label, sizeof(label), "TALK BUTTON ON THE CONTROLLER");
            snprintf(value, sizeof(value), "%s", pads[vpad >= 0 && vpad < 6 ? vpad : 0]);
        } else {
            const NetRoomPlayer &p = r.players[others[i - fixed]];
            int pv = voice_player_volume(p.slot);
            snprintf(label, sizeof(label), "PLAYER");
            snprintf(value, sizeof(value), pv ? "%s  %d %%" : "%s  MUTED", p.name, pv);
        }
        if (row_h < 30) {   // compact: the value alone, on one line
            std::string one = i >= fixed ? std::string(value) : std::string(label) + ":  " + value;
            text(dl, f, g_title, 12, x + 12, y + (row_h - 13) / 2, sel ? WHITE : GREY, one.c_str());
        } else {
            text(dl, f, g_title, 10, x + 12, y + 3, GREY, label);
            text(dl, f, g_title, 15, x + 12, y + 15, sel ? WHITE : GREY, value);
        }
        if (sel) { text(dl, f, g_title, 17, x + w - 26, y + row_h / 2 - 9, ORANGE, "<", CENTER); text(dl, f, g_title, 17, x + w - 10, y + row_h / 2 - 9, ORANGE, ">", CENTER); }
    }
    // the door: how it works, and the microphone's level
    door_panel(dl, f);
    door_text(dl, f, &tex("ui/online_icon.png"), "VOICE CHAT", "TALK IN ROOMS AND FIGHTS",
              "Everyone in your room hears you. Hold the key, or the controller button you pick here, to talk. "
              "Turn a player to 0 to mute them.");
    // the microphone's level, under the rows
    float meter = voice_mic_meter(), my = 80 + rows * row_step + 6;
    if (meter >= 0.0f) {
        text(dl, f, g_title, 10, 52, my, GREY, meter > 0.95f ? "YOUR MICROPHONE: TOO LOUD" : "YOUR MICROPHONE");
        rect(dl, f, 52, my + 14, 266, 9, IM_COL32(255, 255, 255, 30), 4);
        rect(dl, f, 52, my + 14, 266 * meter, 9, meter > 0.95f ? RED : GREEN, 4);
    } else {
        text(dl, f, g_title, 10, 52, my, GREY, r.in_room ? "Press the talk key once: your microphone's level shows here."
                                                       : "Your microphone's level shows here in a room.");
    }
    if ((pad.a && S.voice_sel == rows - 1) || pad.b) S.page = HUB;
}

void draw_status(ImDrawList *dl, const Frame &f)
{
    char status[256];
    status_text(status, sizeof(status));
    text(dl, f, g_body, 11, 54, 452, GREY, status, LEFT, 0.6f, 270);
    NetRoom r;
    lobby_room(&r);
    unsigned long long now = GetTickCount64();
    const char *note = nullptr;
    if (r.error[0]) note = r.error;
    else if (now < S.note_until) note = S.note.c_str();
    if (note) text(dl, f, g_hand, 14, 54, 428, r.error[0] ? RED : ORANGE, note, LEFT, 0.8f, 270);
}

// The gallery's sample: a room of `count` with names and fighters, this PC in slot 0 and hosting.
NetRoom sample_room(int mode, int max, int count)
{
    static const char *const names[] = { "Ann", "Ben", "Cal", "Dee", "Eve", "Fox", "Gus", "Hal", "Ivy", "Jay" };
    const auto &list = roster();
    NetRoom r = {};
    r.in_room = 1;
    snprintf(r.code, sizeof(r.code), "GTEEAQ");
    r.you = 0;
    r.host = 0;
    snprintf(r.settings.name, sizeof(r.settings.name), "Ann's room");
    r.settings.is_public = 1;
    r.settings.mode = mode;
    r.settings.max = max;
    apply_preset(r.settings, 1);
    r.player_count = count;
    for (int i = 0; i < count && i < 10; i++) {
        NetRoomPlayer &p = r.players[i];
        p.slot = i;
        snprintf(p.name, sizeof(p.name), "%s", names[i]);
        p.ready = i % 2;
        p.fighter = list.empty() ? 0xFF : list[(i * 7 + 3) % list.size()].id;
        p.voted = -1;
    }
    r.received = GetTickCount64();
    r.version = 1;
    return r;
}

// Which page the gallery shows now, and the sample it shows with it.
void gallery_step()
{
    static const char *const pages[] = { "connecting", "offline", "hub", "hub-signin", "hub-voice", "rooms", "code",
                                         "create", "create-3", "create-6", "create-1", "create-koth",
                                         "create-koth-3", "create-koth-6", "create-koth-1",
                                         "ranked", "voice", "stats", "players", "fighters", "room", "vote", "hill",
                                         "up", "watch", "room-voice", "match",
                                         // build 30: the server choice
                                         "connecting-auto", "hub-server", "servers", "servers-offline", "hub-ranked-na",
                                         "room-ping", "room-quick-hint", "offline-server",
                                         // build 31: both servers
                                         "hub-both", "servers-both", "rooms-both", "rooms-both-na", "room-both-move" };
    const int count = (int)(sizeof(pages) / sizeof(pages[0]));
    static unsigned long long start;
    static int ms;
    unsigned long long now = GetTickCount64();
    if (!start) {
        start = now;
        ms = atoi(getenv("RECOMP_TEST_ONLINE_GALLERY"));
        const auto &list = roster();
        static const char *const room_names[] = { "Ann's room", "Uptown Brawl", "Hill of Pain", "Late night FFA", "Dee's rules" };
        static const int room_modes[] = { 0, 0, NETPLAY_MODE_KOTH_RANDOM, 2, 1 };
        for (int i = 0; i < 5; i++) {
            NetRoomInfo info = {};
            snprintf(info.code, sizeof(info.code), "ROOM%02d", i);
            snprintf(info.name, sizeof(info.name), "%s", room_names[i]);
            info.mode = room_modes[i];
            info.max = NETPLAY_IS_KOTH(info.mode) ? 8 : i == 3 ? 4 : 2;
            info.players = i == 1 ? 2 : 1 + i % 3;
            info.phase = i == 1 ? 2 : 0;
            G.rooms.push_back(info);
        }
        static const char *const who[] = { "Ann", "Ben", "Cal", "Dee", "Eve", "Fox", "Gus", "Hal", "Ivy", "Jay",
                                           "Kaze", "Lou", "Mo", "Nia", "Oz", "Pia", "Quin", "Rex", "Sol", "Tia",
                                           "Uma", "Vic" };
        for (int i = 0; i < (int)(sizeof(who) / sizeof(who[0])); i++) {
            NetPlayer p = {};
            snprintf(p.name, sizeof(p.name), "%s", who[i]);
            p.state = i % 3;
            G.players.push_back(p);
        }
        const OnlineFighter *main = list.empty() ? nullptr : &list[5 % list.size()];
        char buf[2048];
        snprintf(buf, sizeof(buf),
                 "name=Ann\nmatches=42\nwins=27\nlosses=14\ndraws=1\nwin_rate=64\nstreak=3\nbest_streak=7\nrank=12\nmin=3\n"
                 "favorite=%d\nfavorite_name=%s\nranked=1520|10\n"
                 "recent=win|Ann|Ben|0\nrecent=loss|Ann|Cal|0\nrecent=win|Ann|Dee|0\n"
                 "board=1|Kaze|88|20|0\nboard=2|Rex|71|33|0\nboard=3|Nia|64|12|0\nboard=4|Jay|58|41|0\n"
                 "board=5|Sol|51|27|0\nboard=6|Oz|47|30|0\nboard=7|Vic|40|22|0\nboard=8|Mo|38|19|0\n"
                 "board=9|Tia|31|25|0\nboard=10|Ann|27|14|0\nboard_page=0\nboard_pages=4\n",
                 main ? main->id : -1, main ? main->name : "");
        G.stats = buf;
        // Both servers: North America's rooms as a player in Europe sees them (112 ms there)
        static const char *const na_names[] = { "Brooklyn bruisers", "Quick: Rex", "West coast hill" };
        static const int na_modes[] = { 0, 0, NETPLAY_MODE_KOTH_RANDOM };
        for (int i = 0; i < 3; i++) {
            NetServerRoom e = {};
            snprintf(e.room.code, sizeof(e.room.code), "NAROM%d", i);
            snprintf(e.room.name, sizeof(e.room.name), "%s", na_names[i]);
            e.room.mode = na_modes[i];
            e.room.max = NETPLAY_IS_KOTH(e.room.mode) ? 8 : 2;
            e.room.players = i == 2 ? 4 : 1;
            e.server = NETSERVER_NA;
            e.ping_ms = 112;
            e.age_s = 40 + i * 15;
            G.other_rooms.push_back(e);
        }
    }
    int index = (int)((now - start) / (unsigned long long)ms) % count;
    const char *page = pages[index];
    G.page = page;
    G.state = NETPLAY_LOBBY;
    G.in_lobby = true;
    G.room = NetRoom();
    G.queue = NETQUEUE_SEARCHING;
    G.account = ACCOUNT_IN;
    S.watch_hidden = 0;
    S.note_until = 0;
    S.settings_edit = false;
    {   // the relays as a player in North America sees them, the hub on Europe
        NetServerInfo eu = {}, na = {};
        eu.id = "eu"; eu.name = "Europe"; eu.short_name = "EU"; eu.state = NETSERVER_UP; eu.ping_ms = 38;
        eu.online = 142; eu.rooms = 12; eu.waiting = 2; eu.idle = 5; eu.flags = NETSERVER_RANKED | NETSERVER_KOTH; eu.version = 1;
        na.id = "na"; na.name = "North America"; na.short_name = "NA"; na.state = NETSERVER_UP; na.ping_ms = 112;
        na.online = 12; na.rooms = 3; na.waiting = 3; na.idle = 1; na.flags = NETSERVER_KOTH; na.version = 1;
        G.servers[NETSERVER_EU] = eu;
        G.servers[NETSERVER_NA] = na;
        G.server_cur = NETSERVER_EU;
        G.choice = NETCHOICE_AUTO;
        G.ranked = false;
        G.pending = false;
        for (int i = 0; i < NETPLAY_ROOM_SLOTS; i++) G.room_pings[i] = -1;
        S.quick_room[0] = 0;
        S.alone_since = 0;
    }
    std::string p = page;
    if (p == "connecting") { G.state = NETPLAY_CONNECTING; G.in_lobby = false; }
    else if (p == "offline") { G.state = NETPLAY_FAILED; G.in_lobby = false; }
    else if (p == "hub") { S.page = HUB; S.hub_sel = 0; S.quick_mode = 0; }
    else if (p == "hub-signin") { S.page = HUB; S.hub_sel = 3; G.account = ACCOUNT_OUT; }
    else if (p == "hub-voice") { S.page = HUB; S.hub_sel = HUB_VOICE; }
    else if (p == "rooms") { S.page = ROOMS; S.rooms_sel = 1; }
    else if (p == "code") { S.page = CODE; snprintf(S.code, sizeof(S.code), "GTE4AQ"); S.code_pos = 3; }
    else if (p.compare(0, 6, "create") == 0) {     // every venue; -3, -6: two presets; -1: one venue
        S.page = SETTINGS;
        default_settings(S.settings);
        snprintf(S.settings.name, sizeof(S.settings.name), "Ann's room");
        if (p.find("koth") != std::string::npos) { S.settings.mode = NETPLAY_MODE_KOTH_RANDOM; S.settings.max = 8; }
        char last = p.back();
        if (last == '3') apply_preset(S.settings, 1);
        else if (last == '6') apply_preset(S.settings, 2);
        else if (last == '1') S.settings.pool_count = 1;
        S.settings_sel = 0;
    }
    else if (p == "ranked") S.page = RANKED;
    else if (p == "voice") { S.page = VOICE; S.voice_sel = 1; }
    else if (p == "stats") { S.page = STATS; S.stats_page = 0; }
    else if (p == "players") S.page = PLAYERS;
    else if (p == "fighters") { S.page = FIGHTERS; S.fighter_sel = 9; S.fighter_top = 0; }
    else if (p == "room") { S.page = HUB; G.room = sample_room(2, 4, 3); }
    else if (p == "vote") {
        S.page = HUB;
        G.room = sample_room(0, 2, 2);
        G.room.phase = 1;
        G.room.candidate_count = 3;
        G.room.candidates[0] = 1; G.room.candidates[1] = 6; G.room.candidates[2] = 11;
        G.room.votes[0] = 1; G.room.votes[2] = 1;
        G.room.players[0].voted = 0;
        G.room.ms_left = 9000;
        S.vote_sel = 0;
    } else if (p == "hill" || p == "up" || p == "watch") {
        S.page = HUB;
        NetRoom &r = G.room = sample_room(NETPLAY_MODE_KOTH_RANDOM, 10, 9);
        NetHill &h = r.hill;
        h.valid = 1;
        h.king = 2;
        h.streak = 3;
        h.ms_left = 14000;
        h.received = r.received;
        h.last_winner = 2; h.last_loser = 3; h.last_how = NETHOW_WON;
        h.count = 9;     // a long line: it must stay inside its panel
        const int order[9] = { 2, 1, 0, 3, 4, 5, 6, 7, 8 };
        for (int i = 0; i < 9; i++) { h.line[i].slot = order[i]; h.line[i].state = NETLINE_WAITING; h.line[i].wins = i == 0 ? 5 : i == 1 ? 2 : i == 3 ? 1 : 0; }
        h.line[4].state = NETLINE_AWAY;
        if (p == "hill") { h.phase = NETHILL_CALLING; h.line[0].state = NETLINE_READY; h.line[1].state = NETLINE_CALLED; }
        else if (p == "up") {
            h.phase = NETHILL_CALLING;
            h.line[0].state = NETLINE_READY;
            std::swap(h.line[1], h.line[2]);    // this PC (slot 0) is called against the king
            h.line[1].state = NETLINE_CALLED;
        } else {
            h.phase = NETHILL_FIGHTING;
            h.line[0].state = h.line[1].state = NETLINE_FIGHTING;
            h.seed = h.watch_seed = 1234;
            h.watch_venue = 6;
            h.watch_slots[0] = 1; h.watch_slots[1] = 2;
            h.watch_fighters[0] = r.players[1].fighter; h.watch_fighters[1] = r.players[2].fighter;
        }
    } else if (p == "room-voice") { S.page = VOICE; S.voice_sel = 5; G.room = sample_room(0, 4, 4); }
    else if (p == "match") { G.state = NETPLAY_CONNECTED; G.room = sample_room(0, 2, 2); G.room.phase = 2; }
    else if (p == "connecting-auto") {
        S.page = HUB; G.state = NETPLAY_OFF; G.in_lobby = false; G.pending = true;
        G.servers[NETSERVER_NA].state = NETSERVER_UNKNOWN; G.servers[NETSERVER_NA].ping_ms = -1;
    } else if (p == "hub-server") {     // a player in North America, on North America
        S.page = HUB; S.hub_sel = 0; S.quick_mode = 0;
        G.servers[NETSERVER_EU].ping_ms = 104; G.servers[NETSERVER_NA].ping_ms = 31;
        G.server_cur = NETSERVER_NA;
    } else if (p == "servers") {
        S.page = SERVERS; S.servers_sel = 2; G.server_cur = NETSERVER_EU; G.choice = NETCHOICE_AUTO;
    } else if (p == "servers-offline") {
        S.page = SERVERS; S.servers_sel = 1; G.server_cur = NETSERVER_EU; G.choice = NETCHOICE_EU;
        G.servers[NETSERVER_NA].state = NETSERVER_OFFLINE; G.servers[NETSERVER_NA].ping_ms = -1;
    } else if (p == "hub-ranked-na") {
        S.page = HUB; S.hub_sel = HUB_RANKED; G.ranked = true; G.server_cur = NETSERVER_NA;
    } else if (p == "room-ping") {      // one player far away: the high-ping warning
        S.page = HUB; G.room = sample_room(2, 4, 3);
        G.room_pings[0] = 38; G.room_pings[1] = 96; G.room_pings[2] = 263;
    } else if (p == "room-quick-hint") {    // alone in a quick room for 30 s; North America has players
        S.page = HUB; G.room = sample_room(0, 2, 1);
        G.room.players[0].ready = 1;
        G.room_pings[0] = 38;
        snprintf(S.quick_room, sizeof(S.quick_room), "%s", G.room.code);
        S.alone_since = now - 31000;
    } else if (p == "offline-server") { S.page = HUB; G.state = NETPLAY_FAILED; G.in_lobby = false; }
    else if (p == "hub-both") {         // Both servers: the chip says so, the hub is on Europe
        S.page = HUB; S.hub_sel = 0; S.quick_mode = 0; G.choice = NETCHOICE_BOTH;
    } else if (p == "servers-both") {
        S.page = SERVERS; S.servers_sel = 1 + NETSERVER_COUNT; G.choice = NETCHOICE_BOTH;
    } else if (p == "rooms-both" || p == "rooms-both-na") {     // the merged list; a room on the other server chosen
        S.page = ROOMS; G.choice = NETCHOICE_BOTH; S.rooms_sel = p == "rooms-both" ? 1 : (int)G.rooms.size();
    } else if (p == "room-both-move") {     // alone 20 s in a quick room; a player waits on North America
        S.page = HUB; G.choice = NETCHOICE_BOTH; G.room = sample_room(0, 2, 1);
        G.room.players[0].ready = 1;
        G.room_pings[0] = 38;
        snprintf(S.quick_room, sizeof(S.quick_room), "%s", G.room.code);
        S.alone_since = now - 21000;
        S.note = "Nobody here yet: moving to North America (112 ms), where 1 player is waiting for One on One.";
        S.note_until = now + 60000;
    }
}

// The look of the page draw() shows (the same choices, in the same order).
LookId page_look(const NetRoom &room, enum NetplayState st)
{
    if (st == NETPLAY_CONNECTED && net_in_lobby()) return LOOK_MATCH;
    if (st == NETPLAY_CONNECTING || st == NETPLAY_FAILED || st == NETPLAY_OFF || !net_in_lobby()) return LOOK_OFFLINE;
    switch (S.page) {
    case FIGHTERS: return LOOK_FIGHTERS;
    case SETTINGS: case CODE: return LOOK_LIST;
    case SERVERS: if (!room.in_room) return LOOK_LIST; break;
    case PLAYERS: if (!room.in_room) return LOOK_PLAYERS; break;
    case STATS: if (!room.in_room) return LOOK_STATS; break;
    case RANKED: if (!room.in_room) return LOOK_SEARCH; break;
    case VOICE: if (!(room.in_room && room.phase != 0)) return LOOK_VOICE; break;
    default: break;
    }
    if (room.in_room) {
        if (!NETPLAY_IS_KOTH(room.settings.mode)) return room.phase == 1 ? LOOK_VOTE : LOOK_ROOM;
        HillView v = hill_view(room);
        const NetHill &h = room.hill;
        bool fighting_me = v.my_state == NETLINE_FIGHTING;
        if (room.phase == 1 && fighting_me) return LOOK_VOTE;
        if (h.valid && h.phase == NETHILL_FIGHTING && !fighting_me && S.watch_hidden != (h.seed ? h.seed : 1)) return LOOK_WATCH;
        if (h.valid && h.phase == NETHILL_CALLING && (v.my_state == NETLINE_CALLED || v.my_state == NETLINE_READY)) return LOOK_UP;
        return LOOK_HILL;
    }
    return S.page == ROOMS ? LOOK_LIST : LOOK_HUB;
}

} // namespace

namespace online_screen {

void prepare(void *device)
{
    g_dev = (ID3D11Device *)device;
    if (!g_fonts_tried) {
        g_fonts_tried = true;
        g_title = add_font("TradeBld.ttf");             // items and door titles, like STORY
        g_hand = add_font("SchwartzHand_Bold2.ttf");    // the orange subtitles
        g_body = add_font("TradeCon.ttf");              // descriptions
        g_cond = g_body;
        g_stencil = add_font("FERRO___.TTF");           // page titles, like MAIN MENU
        if (!g_stencil) g_stencil = g_title;
        fprintf(stderr, "[ONLINE] art %s, fonts %s\n", tex("ui/online_poster.png").srv ? "found" : "missing",
                g_title ? "found" : "missing");
    }
}

// The menu reports itself before its first frame; the ticks say it is really on screen
// (without the patched menu's ticks there is no ONLINE entry to draw for either).
// The ticks stop when the menu is no longer drawn (a match the room started, a transition), so
// the art goes with them.
bool decor_wanted()
{
    return g_menu_up.load() != 0 && g_menu_ticks.load() >= 45 && now_ms() - g_last_tick_ms.load() < 300;
}

void draw_decor(ImDrawList *dl, ImVec2 display)
{
    if (!decor_wanted()) return;
    Frame f = menu_frame(display);
    bool focused = g_focus.load() == ONLINE_SLOT;
    // The poster itself is painted into the menu's wall textures (scripts/apt-patch.py v3), so it
    // slides and lights with its neighbours; only the door's text is drawn here.
    if (!focused) return;
    // the door: cover what it shows for the empty slot, then the entry's icon, title and text
    door_shutter(dl, f);
    door_text(dl, f, &tex("ui/online_icon.png"), "ONLINE", "MULTIPLAYER",
              "Fight players across the world. Quick match, rooms and venue votes.");
}

bool closed_for_match() { return g_closed_for_match; }

const char *shot_label() { return gallery_on() ? G.page : nullptr; }

bool take_open_request()
{
    if (!g_open_req.exchange(0)) return false;
    S = State();
    S.opened = GetTickCount64();
    if (!gallery_on()) connect_if_needed();
    return true;
}

bool draw(ImVec2 display)
{
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    Frame f = menu_frame(display);
    ImGui::GetIO().KeyRepeatDelay = 0.45f;     // a scripted or quick press moves one step
    Pad pad = read_pad();
    // the first frame takes no choice: the A that opened the screens is still down
    if (GetTickCount64() - S.opened < 250) pad = Pad();
    if (gallery_on()) {
        gallery_step();
        pad = Pad();
    }

    NetRoom room;
    lobby_room(&room);
    enum NetplayState st = net_state();
    // Over any screen but the main menu (after a match: the battle menu) every page is full-screen.
    {   // who is online: asked every five seconds while the online screens are up
        static unsigned long long asked;
        unsigned long long now = GetTickCount64();
        if (netplay_in_lobby() && now - asked > 5000) { asked = now; netplay_lobby_who(); }
    }
    backdrop(dl, f, display, S.page == FIGHTERS || S.page == STATS || S.page == PLAYERS || room.in_room ||
             st == NETPLAY_CONNECTED ||
             !g_menu_up.load(), page_look(room, st));

    // a match was agreed: show it, then give the menu back to the game, which starts it
    if (st == NETPLAY_CONNECTED && net_in_lobby()) {
        if (!S.start_seen) S.start_seen = GetTickCount64();
        page_title(dl, f, "MATCH FOUND");
        glass(dl, f, 170, 176, 300, 100, 14);
        text(dl, f, g_hand, 22, 320, 200, ORANGE, "Get ready to fight!", CENTER, 1.2f);
        text(dl, f, g_title, 14, 320, 240, WHITE, "The match starts by itself.", CENTER);
        g_closed_for_match = GetTickCount64() - S.start_seen >= 1800;
        return !g_closed_for_match;
    }
    S.start_seen = 0;
    g_closed_for_match = false;

    // the server: Automatic connects once the relays have answered; a quick match chosen on the
    // other server starts once the hub is there
    if (!gallery_on()) {
        if (S.connect_pending) connect_if_needed();
        int moved_mode = 0, moved = netservers_tick(&moved_mode);   // a join or quick match on the other relay
        if (moved == 2) {
            S.quick_wanted = !NETPLAY_IS_KOTH(moved_mode);
            say("Looking for a room...");
        } else if (moved == 1) {
            say("Joining...");
        } else if (moved < 0) {
            say("The other server did not answer in time. Try again.", 6000);
        }
        if (st == NETPLAY_LOBBY) S.fallback_tried = false;
    }
    // The picker works without a connection: a server that does not answer is left from here.
    if (S.page == SERVERS && !room.in_room && st != NETPLAY_CONNECTED) {
        draw_servers(dl, f, pad);
        float hx = 54;
        hx += hint(dl, f, hx, 392, "a", "CHOOSE");
        hx += hint(dl, f, hx, 392, "x", "CHECK AGAIN");
        hint(dl, f, hx, 392, "b", "BACK");
        draw_status(dl, f);
        return true;
    }

    bool pending = gallery_on() ? G.pending : S.connect_pending;
    if (pending || st == NETPLAY_CONNECTING || (!net_in_lobby() && st != NETPLAY_FAILED)) {
        NetServerInfo sv[NETSERVER_COUNT];
        servers(sv);
        int to = gallery_on() ? G.server_cur : -1;
        if (!gallery_on() && !pending) {
            char address[128];
            to = netplay_lobby_address(address, sizeof(address)) ? netservers_index_of(address) : -1;
        }
        page_title(dl, f, "ONLINE");
        glass(dl, f, 36, 104, 290, 76);
        std::string head = pending ? "Finding the nearest server..." :
                           to >= 0 ? std::string("Connecting to ") + sv[to].name + "..." : "Connecting...";
        text(dl, f, g_hand, 20, 54, 116, ORANGE, head.c_str(), LEFT, 1.0f, 265);
        if (pending) {      // the answers so far
            float y = 146;
            for (int i = 0; i < NETSERVER_COUNT; i++) {
                char line[64];
                snprintf(line, sizeof(line), sv[i].state == NETSERVER_UP ? "%s %d MS" : sv[i].state == NETSERVER_OFFLINE ?
                         "%s OFFLINE" : "%s ...", upper(sv[i].short_name).c_str(), sv[i].ping_ms);
                float x = 56 + i * 130;
                ping_bars(dl, f, x, y + 1, 11, sv[i].state == NETSERVER_UP ? sv[i].ping_ms : -1);
                text(dl, f, g_title, 12, x + 24, y, sv[i].state == NETSERVER_UP ? ping_colour(sv[i].ping_ms) : GREY, line, LEFT, 1.0f);
            }
        }
        door_panel(dl, f);
        door_text(dl, f, &tex("ui/online_icon.png"), "ONLINE", pending ? "CHOOSING A SERVER" : "CONNECTING",
                  pending ? "Europe or North America: the one with the lowest ping. Change it any time with X in the online menu."
                          : "To the FFNY Online server.");
        float hx = 54;
        hx += hint(dl, f, hx, 392, "x", "SERVER");
        hint(dl, f, hx, 392, "b", "BACK");
        if (pad.x) open_servers();
        return !pad.b;
    }
    if (st == NETPLAY_FAILED || st == NETPLAY_OFF) {
        // Automatic: one more try on the other server when it answers its status (a relay that is
        // down, or blocked on this network), before the player sees "no connection"
        if (!gallery_on() && st == NETPLAY_FAILED && !S.fallback_tried &&
            (server_choice() == NETCHOICE_AUTO || server_choice() == NETCHOICE_BOTH)) {
            NetServerInfo sv[NETSERVER_COUNT];
            servers(sv);
            int failed = netservers_current(), other = failed >= 0 ? other_server(sv, failed) : -1;
            S.fallback_tried = true;
            if (other >= 0) {
                netservers_use(other);
                fprintf(stderr, "[ONLINE] %s did not connect; trying %s\n", sv[failed].name, sv[other].name);
                say(std::string(sv[failed].name) + " did not answer: trying " + sv[other].name + "...");
                netservers_reconnect(hub_name().c_str());
                return true;
            }
        }
        char status[256];
        status_text(status, sizeof(status));
        page_title(dl, f, "ONLINE");
        glass(dl, f, 36, 96, 290, 120);
        text(dl, f, g_hand, 20, 54, 110, RED, "No connection", LEFT, 1.0f);
        text(dl, f, g_title, 14, 54, 145, WHITE, status, LEFT, 0.8f, 265);
        door_panel(dl, f);
        door_text(dl, f, &tex("ui/online_icon.png"), "OFFLINE", "NO CONNECTION",
                  "Check the internet connection, then press A to try again, or X to pick another server.");
        float hx = 54;
        hx += hint(dl, f, hx, 392, "a", "TRY AGAIN");
        hx += hint(dl, f, hx, 392, "x", "SERVER");
        hint(dl, f, hx, 392, "b", "BACK");
        if (pad.a) { S.fallback_tried = true; connect_if_needed(); }
        if (pad.x) open_servers();
        return !pad.b;
    }

    bool keep = true;
    if (S.page == FIGHTERS) {
        bool chosen = false;
        int fighter = 0;
        draw_fighters(dl, f, pad, &chosen, &fighter);
        if (chosen) {
            recomp_settings_set("online", "fighter", fighter);
            if (S.ranked_pick && !room.in_room) {
                netplay_lobby_ranked(fighter);
                S.page = RANKED;
            } else {
                if (room.in_room) {
                    // The pick tells the room the fighter but leaves "ready" as it was: players asked
                    // to choose without being readied at once (A readies).
                    int ready = 0;
                    for (int i = 0; i < room.player_count; i++)
                        if (room.players[i].slot == room.you) ready = room.players[i].ready;
                    netplay_lobby_ready(ready, fighter);
                }
                say(std::string(online_fighter_name(fighter)) + " it is.");
                S.page = HUB;
            }
            S.ranked_pick = false;
        }
        if (S.page == HUB) S.ranked_pick = false;
        if (S.page == FIGHTERS) { hint(dl, f, 54, 420, "a", "PICK"); hint(dl, f, 140, 420, "b", "BACK"); }
    } else if (S.page == SETTINGS) {
        draw_settings(dl, f, pad, room);
        float hx = 54;
        hx += hint(dl, f, hx, 392, "a", "CHOOSE");
        hint(dl, f, hx, 392, "b", "BACK");
    } else if (S.page == PLAYERS && !room.in_room) {
        draw_players(dl, f, pad);
        float hx = 54;
        hx += hint(dl, f, hx, 420, "x", "REFRESH");
        hint(dl, f, hx, 420, "b", "BACK");
        if (pad.b) S.page = HUB;
    } else if (S.page == STATS && !room.in_room) {
        draw_stats(dl, f, pad);
        float hx = 54;
        hx += hint(dl, f, hx, 420, "x", "REFRESH");
        hint(dl, f, hx, 420, "b", "BACK");
        if (pad.b) S.page = HUB;
    } else if (S.page == RANKED && !room.in_room) {
        draw_ranked(dl, f, pad);
    } else if (S.page == VOICE && !(room.in_room && room.phase != 0)) {
        draw_voice(dl, f, pad, room);
        float hx = 54;
        hx += hint(dl, f, hx, 420, "b", "BACK");
        (void)hx;
    } else if (S.page == CODE) {
        draw_code(dl, f, pad);
        float hx = 54;
        hx += hint(dl, f, hx, 392, "a", "JOIN");
        hint(dl, f, hx, 392, "b", "BACK");
    } else if (room.in_room) {
        if (S.page == RANKED) S.page = HUB;     // matched: leaving the room comes back to the menu
        draw_room(dl, f, pad, room);
    } else if (S.page == ROOMS) {
        draw_rooms(dl, f, pad);
        float hx = 54;
        hx += hint(dl, f, hx, 420, "a", "JOIN");
        hx += hint(dl, f, hx, 420, "x", "REFRESH");
        hx += hint(dl, f, hx, 420, "y", "JOIN BY CODE");
        hint(dl, f, hx, 420, "b", "BACK");
    } else {
        draw_hub(dl, f, pad);      // the menu's own "A SELECT  B BACK" stays visible above while its door shows
        if (!g_game_door) {
            float hx = 54;
            hx += hint(dl, f, hx, 424, "a", "SELECT");
            hint(dl, f, hx, 424, "b", "BACK");
        }
        if ((pad.a && S.hub_sel == HUB_LEAVE) || pad.b) keep = false;
    }
    draw_status(dl, f);
    return keep;
}

}   // namespace online_screen

#else   // the online screens are drawn on Windows only for now

namespace online_screen {
void prepare(void *) {}
bool decor_wanted() { return false; }
void draw_decor(ImDrawList *, ImVec2) {}
bool take_open_request() { return g_open_req.exchange(0) != 0 && false; }
bool draw(ImVec2) { return false; }
bool closed_for_match() { return false; }
const char *shot_label() { return nullptr; }
}

#endif
