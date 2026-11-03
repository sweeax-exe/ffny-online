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
#include "created_fighter.h"
#include "voice.h"
#include "account.h"
#include "online_data.h"
#include "pc_settings.h"
#include "pc_ui.h"
#include "patches/widescreen.h"
#include "recomp_settings.h"
#include "rollback.h"
#include "updater.h"
#include "social.h"
#include <time.h>

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

// The picture is 4:3, or in widescreen the canvas's shape with the menus centred at 4:3 inside it
// (src/patches/widescreen.c); with "stretch" the 4:3 picture is stretched to the window.
Frame menu_frame(ImVec2 display)
{
    float wide = ws_aspect();
    int keep = wide > 0.0f || pc_display("aspect", 0) != 1;
    float num = wide > 0.0f ? wide : 4.0f, den = wide > 0.0f ? 1.0f : 3.0f;
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

// The size at which `s` fits into `w` (never larger than `size`): a created fighter's online name of
// 16 characters is wider than the 8 of a user ID.
float fit_size(const Frame &f, ImFont *font, float size, const char *s, float w)
{
    float tw = text_width(f, font, size, s);
    return tw > w && tw > 0 ? size * w / tw : size;
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

// The button symbols the player chose (ui.button_icons): PlayStation's, or the game's Xbox letters.
bool ps_icons() { return pc_ui_button_icons() == PC_UI_ICONS_PLAYSTATION; }

// Button names in on-screen text: {A} {B} {X} {Y} {LB} {RB} as the chosen set writes them, the Xbox
// letter or the PlayStation name: CROSS for {A} in capitals, Cross for {a} in a sentence.
std::string pad_text(const char *s)
{
    static const char *const xbox[] = { "A", "B", "X", "Y", "LB", "RB" };
    static const char *const ps[] = { "Cross", "Circle", "Square", "Triangle", "L1", "R1" };
    bool on = ps_icons();
    std::string out;
    for (const char *p = s; *p; p++) {
        const char *end = *p == '{' ? strchr(p, '}') : nullptr;
        int i = 6;
        if (end && end - p <= 3) {
            std::string key(p + 1, end);
            for (char &c : key) c = (char)toupper((unsigned char)c);
            for (i = 0; i < 6 && key != xbox[i]; i++) {}
        }
        if (i == 6) { out += *p; continue; }
        std::string name = on ? ps[i] : xbox[i];
        if (on && isupper((unsigned char)p[1]))
            for (char &c : name) c = (char)toupper((unsigned char)c);
        out += name;
        p = end;
    }
    return out;
}

// A PlayStation symbol drawn without its picture (an install whose online art predates them): a dark
// disc with the symbol in its colour, as scripts/online-art.py draws ui/ps_*.png.
void ps_symbol(ImDrawList *dl, const Frame &f, float x, float y, char button)
{
    float s = f.s(), r = 2.3f * s, w = 1.3f * s;
    ImVec2 c = f.p(x + 5.25f, y + 5.25f);      // where the 16-unit picture has its badge
    dl->AddCircleFilled(c, 5.25f * s, BLACK);
    dl->AddCircleFilled(c, 4.7f * s, IM_COL32(150, 150, 160, 255));
    dl->AddCircleFilled(c, 4.2f * s, IM_COL32(56, 56, 62, 255));
    switch (button) {
    case 'a': dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), IM_COL32(110, 168, 238, 255), w);
              dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), IM_COL32(110, 168, 238, 255), w); break;
    case 'b': dl->AddCircle(c, r * 1.1f, IM_COL32(255, 96, 104, 255), 16, w); break;
    case 'x': dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), IM_COL32(246, 120, 220, 255), 0, 0, w); break;
    default:  dl->AddTriangle(ImVec2(c.x, c.y - r * 1.2f), ImVec2(c.x - r * 1.15f, c.y + r * 0.8f),
                              ImVec2(c.x + r * 1.15f, c.y + r * 0.8f), IM_COL32(70, 222, 170, 255), w); break;
    }
}

// A button glyph and its word, as in the game's header ("A SELECT"); PlayStation's symbol instead
// of the letter with ui.button_icons (A cross, B circle, X square, Y triangle, LB L1, RB R1).
float hint(ImDrawList *dl, const Frame &f, float x, float y, const char *button, const char *word)
{
    bool ps = ps_icons();
    static const char *const ps_files[] = { "ui/ps_cross.png", "ui/ps_circle.png", "ui/ps_square.png", "ui/ps_triangle.png" };
    std::string name = std::string("ui/button_") + button + ".png";
    if (ps && !button[1]) name = ps_files[button[0] == 'a' ? 0 : button[0] == 'b' ? 1 : button[0] == 'x' ? 2 : 3];
    const Tex &t = tex(name);
    if (button[0] && button[1] && !t.srv) {     // "rb", "lb": a small plate with both letters (R1, L1)
        char up[3] = { (char)(button[0] - 32), ps ? '1' : (char)(button[1] - 32), 0 };
        rect(dl, f, x - 2, y + 1, 22, 14, IM_COL32(70, 70, 70, 255), 4);
        text(dl, f, g_body, 10, x + 9, y + 2, WHITE, up, CENTER, 0);
        text(dl, f, g_title, 13, x + 24, y + 1, WHITE, word, LEFT, 1.0f);
        return 24 + text_width(f, g_title, 13, word) + 12;
    }
    if (!image(dl, f, t, x, y, 16, 16) && ps) {
        ps_symbol(dl, f, x, y, button[0]);
    } else if (!t.srv) {
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

// Our glass door on the right (door_panel, door_text): level with the list panels' top (y 74) and as far
// from the right edge as they are from the left (x 36), so the two panels sit symmetrically.
const float DOOR_X = 346, DOOR_Y = 74, DOOR_W = 258, DOOR_H = 358;
extern bool g_game_door;

// The door's text, laid out as the game lays out STORY or OPTIONS: an icon, the title, an orange
// subtitle and a few lines in capitals.
void door_text(ImDrawList *dl, const Frame &f, const Tex *icon, const char *title, const char *sub, const char *desc)
{
    // In the game's own door the text sits where the game puts it; on our glass door (door_panel)
    // it is centred on the panel and starts higher.
    float cx = g_game_door ? 463 : DOOR_X + DOOR_W / 2, dy = g_game_door ? 0 : -28, wrap = g_game_door ? 182 : DOOR_W - 52;
    if (icon) image_fit(dl, f, *icon, cx - 63, 140 + dy, 110, 100, IM_COL32(255, 255, 255, 235));
    if (title) text(dl, f, g_title, 32, cx, 236 + dy, CREAM, title, CENTER, 1.8f);
    if (sub) text(dl, f, g_hand, 17, cx, 274 + dy, ORANGE, sub, CENTER, 1.2f);
    if (desc) {
        std::string up(desc);
        for (char &c : up) c = (char)toupper((unsigned char)c);
        text(dl, f, g_body, 17, g_game_door ? 372 : DOOR_X + 26, 304 + dy, WHITE, up.c_str(), LEFT, 1.2f, wrap);
    }
}

// A created fighter (NETPLAY_CREATED_FIGHTER) has no portrait: a silhouette on the card, in the
// portraits' dark tones, with the tint dimming it as it dims a portrait.
void silhouette(ImDrawList *dl, const Frame &f, float x, float y, float w, float h, ImU32 tint)
{
    float dim = ((tint >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
    auto shade = [dim](int r, int g, int b, int a) {
        return IM_COL32((int)(r * dim), (int)(g * dim), (int)(b * dim), a);
    };
    dl->AddRectFilledMultiColor(f.p(x, y), f.p(x + w, y + h), shade(46, 40, 38, 255), shade(46, 40, 38, 255),
                                shade(14, 13, 15, 255), shade(14, 13, 15, 255));
    float cx = x + w / 2, head = std::min(w, h) * 0.17f;
    ImU32 body = shade(92, 88, 92, 255);
    dl->AddCircleFilled(f.p(cx, y + h * 0.30f), head * f.s(), body, 32);
    rect(dl, f, cx - head * 0.45f, y + h * 0.30f + head * 0.8f, head * 0.9f, head * 0.7f, body);
    rect(dl, f, x + w * 0.14f, y + h * 0.30f + head * 1.35f, w * 0.72f, h - (h * 0.30f + head * 1.35f), body, head * 0.9f);
    dl->AddRectFilledMultiColor(f.p(x, y + h * 0.78f), f.p(x + w, y + h), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 170), IM_COL32(0, 0, 0, 170));
}

// A fighter's portrait filling a box: the top of the picture, at its own proportions.
bool portrait(ImDrawList *dl, const Frame &f, int id, float x, float y, float w, float h, ImU32 tint = IM_COL32_WHITE)
{
    if (id == NETPLAY_CREATED_FIGHTER) { silhouette(dl, f, x, y, w, h, tint); return true; }
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
// How far our glass door's content sits right of where the game's door has it (centre 463).
float door_dx() { return g_game_door ? 0 : DOOR_X + DOOR_W / 2 - 463; }

void door_panel(ImDrawList *dl, const Frame &f)
{
    if (!g_game_door) glass(dl, f, DOOR_X, DOOR_Y, DOOR_W, DOOR_H, 14, IM_COL32(10, 10, 13, 255), 0.58f);
}

// ---- input --------------------------------------------------------------------------------------

struct Pad { bool up, down, left, right, a, b, x, y, lb, rb, lb_once, lb_down; };

// Mouse: hovering a box picks it, a click chooses it.
bool gallery_on();
bool chat_mouse_blocked();          // the chat window is over the page (social_ui.inc)
bool mouse_in(const Frame &f, float x, float y, float w, float h)
{
    if (gallery_on()) return false;     // the gallery takes no input: a resting mouse moved the grid's selection
    if (chat_mouse_blocked()) return false;
    return ImGui::IsMouseHoveringRect(f.p(x, y), f.p(x + w, y + h), false);
}
// Hover picks only when the mouse moved (or clicked) this frame: a pointer resting on a row must not
// pull the selection back every frame, which left the pad stuck on that row (owner, 2026-10-30).
bool mouse_pick(const Frame &f, float x, float y, float w, float h)
{
    ImVec2 d = ImGui::GetIO().MouseDelta;
    return (d.x != 0 || d.y != 0 || ImGui::IsMouseClicked(0)) && mouse_in(f, x, y, w, h);
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

enum Page { HUB, ROOMS, SETTINGS, FIGHTERS, CODE, STATS, PLAYERS, RANKED, VOICE, SERVERS, NAME, RANKED_MODES, RANKED_RESULTS };

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
    // NAME: the online name of a created fighter (cp_cards index name_cp), edited in name[]
    char name[CP_NAME_MAX + 1] = "";
    int name_pos = 0, name_cp = -1;
    int fighters_cp = -1;           // FIGHTERS: the selected card's cp_cards index, -1 a roster fighter
    uint32_t watch_hidden = 0;      // King of the Hill: the fight whose watching screen B closed
    unsigned long long give_up_at = 0, lb_since = 0;    // King of the Hill: LB gave up the spot; LB held since
    bool lb_long_done = false;      // the long LB press has acted
    // the server (netplay_servers.h)
    int servers_sel = 0;            // SERVERS: 0 Automatic, 1 Europe, 2 US East, 3 US West, 4 All servers
    bool connect_pending = false;   // Automatic waits for the relays' first answers before connecting
    bool fallback_tried = false;    // Automatic: another relay was tried once after a failed connection
    int auto_moves = 0;             // quick matches moved to another relay since QUICK MATCH
    bool quick_wanted = false;      // QUICK MATCH pressed: the next room is a quick room
    int last_quick = 0, last_quick_max = 2;     // that quick match's mode and players
    char quick_room[7] = "";        // the quick room this PC waits in
    unsigned long long alone_since = 0;         // alone in that room since
    // The staged search (build 38): since when this QUICK MATCH waits alone (kept across moves to
    // another relay), and the relays it searched when the screen last said so
    unsigned long long quick_since = 0;
    unsigned quick_scope = 0;
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
    std::vector<NetServerRoom> other_rooms;     // All servers: the sample's rooms on US East and US West
    std::vector<NetServerPlayer> other_players; // All servers: the sample's players on US East and US West
    NetVersus versus = {};          // the versus pages' sample (count 0: the plain MATCH FOUND)
    int page_ms = 0;                // how long the page has been up
    unsigned cp_slots = 0;          // created fighters: the sample's room slots that have one (0: none offered)
    int cp_pick = 0;                // the sample player's picked created fighter (cp_cards index)
    bool cp_long = false;           // the samples use 16-character online names (the "-long" pages)
    std::string cp_online[3];       // the sample cards' online names ("" the user ID; NAME saves here)
    bool results_wait = false;      // the room still plays: this PC is back, another is on its results
    unsigned results_slots = 0;     // ... the room slots still there (netplay_room_results_wait)
    bool rollback = false;          // the sample relay allows rollback (NETPLAY_CAP_ROLLBACK)
    int room_rb[NETPLAY_ROOM_SLOTS] = {};   // each sample member's rollback byte (netplay_room_rollback)
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

// Ranked play's pages (ranked_ui.inc, included below the gallery's samples).
bool ranked_room(const NetRoom &r, NetRankInfo *ri);
void ranked_banner(ImDrawList *dl, const Frame &f, const NetRoom &r, const NetRankInfo &ri, float y);
const char *ranked_picker_line(const NetRoom &r, const NetRankInfo &ri, char *buf, int size);
void ranked_card(ImDrawList *dl, const Frame &f, const NetRankInfo &ri, int slot, float x, float y, float w);
void ranked_versus(ImDrawList *dl, const Frame &f, const NetVersus &v, float alpha);
bool ranked_created_ok(const NetRoom &r);
void draw_ranked_stats(ImDrawList *dl, const Frame &f, int mode);
int &ranked_stats_tab();
int ranked_relay();
bool ranked_test_switch();
void open_ranked_modes();
void ranked_search(int fighter);
void ranked_tick(const NetRoom &r);
bool draft_shown(const NetRoom &r, NetDraft *d);
void draw_draft(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r, const NetDraft &d);
bool ranked_gallery(const std::string &page);
void draw_ranked_modes(ImDrawList *dl, const Frame &f, const Pad &pad);
void draw_ranked(ImDrawList *dl, const Frame &f, const Pad &pad);
void draw_ranked_results(ImDrawList *dl, const Frame &f, const Pad &pad);

enum NetplayState net_state() { return gallery_on() ? G.state : netplay_state(); }
bool net_in_lobby() { return gallery_on() ? G.in_lobby : netplay_in_lobby() != 0; }
void lobby_room(NetRoom *r)
{
    if (gallery_on()) *r = G.room;
    else netplay_lobby_room(r);
}
// Created fighters: whether this player may pick theirs, and a room member's (name and styles). The
// gallery's samples use this PC's own created fighters when it has some (RECOMP_CP_RECORD).
bool cp_available() { return gallery_on() ? (G.cp_slots & 1u) != 0 : netplay_created_available() != 0; }

// This player's created fighters, one card each: every Story save with one, newest first; the
// gallery shows three samples when this PC has none.
// name: what it plays under online (the online name, else the user ID); online: "" when none is set.
struct CpCard { std::string name, style, saved, user_id, online; };
const std::vector<CpCard> &cp_cards()
{
    static std::vector<CpCard> cards;
    static CpLocal local[CP_LOCAL_MAX];
    int n = std::min(cp_local_list(local, CP_LOCAL_MAX), (int)CP_LOCAL_MAX);
    cards.clear();
    for (int i = 0; i < n; i++)
        cards.push_back({ local[i].name, cp_record_style(local[i].rec), local[i].saved, local[i].user_id, local[i].online });
    if (gallery_on() && cards.empty()) {
        static const char *const ids[] = { "AW5J3555", "CPTWO", "NIGHTOWL" };
        static const char *const styles[] = { "KICKBOXING / STREETFIGHTING", "WRESTLING / SUBMISSIONS", "MARTIAL ARTS / KICKBOXING" };
        static const char *const saved[] = { "2026-10-28 14:30", "2026-10-27 21:05", "2026-10-20 18:12" };
        for (int i = 0; i < 3; i++) {
            const std::string &on = G.cp_online[i];
            cards.push_back({ on.empty() ? ids[i] : on, styles[i], saved[i], ids[i], on });
        }
    }
    return cards;
}
// The card the player picked (remembered across sessions; the newest until one is picked).
int cp_picked()
{
    if (gallery_on()) return std::max(0, std::min(G.cp_pick, (int)cp_cards().size() - 1));
    return std::max(0, cp_local_pick());
}
void cp_choose(int index)
{
    if (gallery_on()) G.cp_pick = index;
    else netplay_created_pick(index);
}

bool cp_slot(int slot, char *name, int size, const char **style)
{
    if (!gallery_on()) return netplay_slot_created(slot, name, size, style) != 0;
    if (slot < 0 || slot >= 32 || !(G.cp_slots & (1u << slot))) return false;
    static std::string st;
    const auto &cards = cp_cards();
    if (slot == 0 && !cards.empty()) {
        const CpCard &c = cards[cp_picked()];
        snprintf(name, (size_t)size, "%s", c.name.c_str());
        st = c.style;
        *style = st.c_str();
    } else {
        snprintf(name, (size_t)size, "%s", slot == 0 ? "AW5J3555" : G.cp_long ? "Brooklyn Bruiser" : "CPTWO");
        *style = slot == 0 ? "KICKBOXING / STREETFIGHTING" : "WRESTLING / SUBMISSIONS";
    }
    return true;
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
// All servers (build 47): who is online on every relay, each tagged with its relay (netservers_players).
int all_players(NetServerPlayer *out, int max, int *online)
{
    if (!gallery_on()) {
        netservers_fetch_players();     // the other relays' lists, every 5 s
        return netservers_players(out, max, online);
    }
    int n = 0, sum = 0;
    for (const NetPlayer &p : G.players) if (n < max) { out[n].player = p; out[n].server = G.server_cur; n++; }
    for (const NetServerPlayer &p : G.other_players) if (n < max) out[n++] = p;
    for (int i = 0; i < NETSERVER_COUNT; i++) if (G.servers[i].state == NETSERVER_UP) sum += G.servers[i].online;
    if (online) *online = sum;
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
int lobby_caps()
{
    return gallery_on() ? NETPLAY_CAP_KOTH | NETPLAY_CAP_YIELD | (G.rollback ? NETPLAY_CAP_ROLLBACK : 0) : netplay_lobby_caps();
}
// spectator mode: the relay takes spectators (docs/research/spectator-mode.md)
bool can_watch() { int c = lobby_caps(); return c > 0 && (c & NETPLAY_CAP_SPECTATE); }
enum AccountState acc_state() { return gallery_on() ? G.account : account_state(); }
bool stats_busy() { return gallery_on() ? false : account_stats_busy() != 0; }
bool stats_text(char *out, int size)
{
    if (!gallery_on()) return account_stats(out, size) != 0;
    std::string s = G.stats;
    if (G.page && !strcmp(G.page, "stats-1v1")) {     // an account service before Team and FFA had records
        for (const char *key : { "team=", "ffa=" }) {
            size_t at = s.find(std::string("\n") + key);
            if (at != std::string::npos) s.erase(at + 1, s.find('\n', at + 1) - at);
        }
    }
    snprintf(out, (size_t)size, "%s", s.c_str());
    return true;
}
void status_text(char *out, int size)
{
    if (gallery_on()) snprintf(out, (size_t)size, "Online as Ann. Ping 38 ms.");
    else netplay_status(out, size);
}

// The relays (Europe, US East, US West): their state, the one the hub is on, the choice.
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
bool all_servers() { return server_choice() == NETCHOICE_ALL; }
// The room list FIND A ROOM shows: the relay the hub is on, and with All servers the others' too
// (netservers_rooms: the lowest ping first).
int list_rooms(NetServerRoom *out, int max)
{
    if (!gallery_on()) {
        unsigned version;
        netservers_fetch_rooms();       // All servers: the other relays' lists, every 3 s
        return netservers_rooms(out, max, &version);
    }
    int n = 0;
    for (const NetRoomInfo &r : G.rooms)
        if (n < max) { out[n].room = r; out[n].server = G.server_cur; out[n].ping_ms = G.servers[G.server_cur].ping_ms; out[n].age_s = -1; n++; }
    if (G.choice == NETCHOICE_ALL)
        for (const NetServerRoom &r : G.other_rooms) if (n < max) out[n++] = r;
    return n;
}
int room_ping(int slot)
{
    if (gallery_on()) return slot >= 0 && slot < NETPLAY_ROOM_SLOTS ? G.room_pings[slot] : -1;
    return netplay_room_ping(slot);
}
// A room member's rollback byte (netplay_room_rollback): -1 unknown, bit 0 rolls back, bit 1 needs a restart.
int room_rb(int slot)
{
    if (gallery_on()) return slot >= 0 && slot < NETPLAY_ROOM_SLOTS ? G.room_rb[slot] : -1;
    return netplay_room_rollback(slot);
}
// The relay still has the room playing while this PC is back from the match (netplay_room_results_wait).
bool results_wait(unsigned *slots)
{
    if (gallery_on()) { *slots = G.results_slots; return G.results_wait; }
    return netplay_room_results_wait(slots) != 0;
}

// Ping colours: good, fair, high.
ImU32 ping_colour(int ms) { return ms < 0 ? GREY : ms < 80 ? GREEN : ms < 150 ? ORANGE : IM_COL32(235, 70, 60, 255); }

// The answering relay with the lowest ping besides `than` (Automatic's second try), -1 none.
int other_server(const NetServerInfo *s, int than)
{
    int o = -1;
    for (int i = 0; i < NETSERVER_COUNT; i++)
        if (i != than && s[i].state == NETSERVER_UP && (o < 0 || s[i].ping_ms < s[o].ping_ms)) o = i;
    return o;
}

// The relays a quick match searches (a netservers_scope mask) in words: "US East", "US East and US
// West", or "every server" when it covers every relay that answers.
std::string scope_words(const NetServerInfo *s, unsigned scope)
{
    unsigned up = 0;
    int n = 0;
    for (int i = 0; i < NETSERVER_COUNT; i++) {
        if (s[i].state == NETSERVER_UP) up |= 1u << i;
        if (scope >> i & 1u) n++;
    }
    if (n > 1 && (scope | up) == scope) return "every server";
    std::string words;
    for (int i = 0, k = 0; i < NETSERVER_COUNT; i++) {
        if (!(scope >> i & 1u)) continue;
        words += k == 0 ? "" : k == n - 1 ? " and " : ", ";
        words += s[i].name;
        k++;
    }
    return words;
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
    // Letter keys follow the game's keyboard layout (WASD, K/L/J/I); on the code and name pages they type.
    bool letters = S.page != CODE && S.page != NAME;
    auto key = [&](ImGuiKey k) { return letters ? k : ImGuiKey_None; };
    p.up = any({ ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp, ImGuiKey_UpArrow, key(ImGuiKey_W) }, true);
    p.down = any({ ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown, ImGuiKey_DownArrow, key(ImGuiKey_S) }, true);
    p.left = any({ ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft, ImGuiKey_LeftArrow, key(ImGuiKey_A) }, true);
    p.right = any({ ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight, ImGuiKey_RightArrow, key(ImGuiKey_D) }, true);
    // With the chat (social_ui.inc) ENTER opens it, as the design has it; the code and name pages keep
    // ENTER for their own confirm. Without it nothing changes.
    bool enter_chooses = !letters || !social_enabled();
    auto enter = [&](ImGuiKey k) { return enter_chooses ? k : ImGuiKey_None; };
    p.a = any({ ImGuiKey_GamepadFaceDown, enter(ImGuiKey_Enter), enter(ImGuiKey_KeypadEnter), key(ImGuiKey_Space), key(ImGuiKey_K) }, false);
    p.b = any({ ImGuiKey_GamepadFaceRight, ImGuiKey_Escape, letters ? ImGuiKey_Backspace : ImGuiKey_None, key(ImGuiKey_L) }, false);
    p.x = any({ ImGuiKey_GamepadFaceLeft, key(ImGuiKey_J), key(ImGuiKey_F) }, false);
    p.y = any({ ImGuiKey_GamepadFaceUp, key(ImGuiKey_I), key(ImGuiKey_R) }, false);
    p.lb = any({ ImGuiKey_GamepadL1, ImGuiKey_PageUp, ImGuiKey_Q }, true);
    p.rb = any({ ImGuiKey_GamepadR1, ImGuiKey_PageDown, ImGuiKey_E }, true);
    p.lb_once = any({ ImGuiKey_GamepadL1, ImGuiKey_PageUp, ImGuiKey_Q }, false);
    p.lb_down = ImGui::IsKeyDown(ImGuiKey_GamepadL1) || ImGui::IsKeyDown(ImGuiKey_PageUp) || ImGui::IsKeyDown(ImGuiKey_Q);
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

// A special match's own venue (Demolition, Inferno, Subway, Window, Cage, Ringout), 0 when the room
// votes; whether a mode has more than two fighters (Team, 3-Way, Free for All: never on the venues
// with a one-on-one start table only); Team, 2 vs 2.
int fixed_venue(int type)
{
    const OnlineMode *m = online_mode(type);
    return m ? m->venue : 0;
}
bool many_fighters(int type)
{
    const OnlineMode *m = online_mode(type);
    return m && m->fighters > 2;
}
bool is_team(int type) { return type == ONLINE_MODE_TEAM; }

// What a mode is, in a line, for the create page's door and a room of it.
const char *mode_blurb(int type)
{
    switch (type) {
    case 1: return "Two on two: places 1 and 2 against 3 and 4. Four players, no CPU.";
    case 6: return "One on one in the underground parkade. The cars are part of the fight.";
    case 7: return "One on one in the burning tire warehouse. The fire is part of the fight.";
    case 8: return "One on one on the subway platform. Mind the train.";
    case 9: return "One on one in Crow's office, high over the street. Mind the windows.";
    case 10: return "One on one in Crow's cage at Club Murder.";
    case 11: return "One on one at The Dragon House. Ring out rules are always in effect.";
    }
    return nullptr;
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
static int saved_fighter()
{
    int id = online_fighter_id(recomp_settings_get("online", "fighter", 55));
    return id == NETPLAY_CREATED_FIGHTER && !cp_available() ? 55 : id;
}

// A fighter's name for the screen: a created fighter's is its record's (the member in relay slot
// `slot`; this player's own when slot is -1).
std::string fighter_label(int id, int slot = -1)
{
    if (id != NETPLAY_CREATED_FIGHTER) return online_fighter_name(id);
    char name[CP_NAME_MAX + 1];
    const char *style = "";
    NetRoom r;
    lobby_room(&r);
    if (cp_slot(slot >= 0 ? slot : r.you, name, sizeof(name), &style) && name[0]) return name;
    return "Created fighter";
}

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

// A preset's venues for a mode: a mode with more than two fighters leaves out the venues with a
// one-on-one start table only (the relay does the same with any pool it gets).
std::vector<int> preset_ids(int index, int mode)
{
    std::vector<int> out;
    for (int id : presets()[index].ids)
        if (!many_fighters(mode) || !ONLINE_ONE_ON_ONE_VENUE(id)) out.push_back(id);
    return out;
}

int preset_of(const NetRoomSettings &s)
{
    const auto &p = presets();
    if (fixed_venue(s.mode)) return -1;
    for (size_t i = 0; i < p.size(); i++) {
        std::vector<int> ids = preset_ids((int)i, s.mode);
        if ((int)ids.size() != s.pool_count) continue;
        bool same = true;
        for (int k = 0; k < s.pool_count; k++)
            if (std::find(ids.begin(), ids.end(), (int)s.pool[k]) == ids.end()) same = false;
        if (same) return (int)i;
    }
    return -1;
}

void apply_preset(NetRoomSettings &s, int index)
{
    const auto &p = presets();
    if (p.empty()) return;
    s.pool_count = 0;
    if (int v = fixed_venue(s.mode)) {      // a special match: its own venue, nothing to choose
        s.pool[s.pool_count++] = (unsigned char)v;
        return;
    }
    index = (index % (int)p.size() + (int)p.size()) % (int)p.size();
    for (int id : preset_ids(index, s.mode)) if (s.pool_count < 32) s.pool[s.pool_count++] = (unsigned char)id;
}

// After the mode changed from `old_mode`: the pool the new mode votes on, the same preset where there
// is one (a custom pool loses the venues the mode cannot have), every venue after a special match.
void fit_pool(NetRoomSettings &s, int old_mode, int old_preset)
{
    if (fixed_venue(s.mode) || fixed_venue(old_mode)) { apply_preset(s, 0); return; }
    if (old_preset >= 0) { apply_preset(s, old_preset); return; }
    int n = 0;
    for (int k = 0; k < s.pool_count; k++)
        if (!many_fighters(s.mode) || !ONLINE_ONE_ON_ONE_VENUE(s.pool[k])) s.pool[n++] = s.pool[k];
    s.pool_count = n;
    if (!n) apply_preset(s, 0);
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
    int fixed = netservers_choice_relay(c);
    S.servers_sel = fixed >= 0 ? 1 + fixed : c == NETCHOICE_ALL ? 1 + NETSERVER_COUNT : 0;
}

// ---- hub ----

const int HUB_ITEMS = 8, HUB_LEAVE = 7, HUB_RANKED = 1, HUB_VOICE = 6;

// RANKED MATCH shows once a relay's STATUS says its ranked queue is on (the account service's switch,
// server/docs/ranked-design.md); RECOMP_RANKED=1 shows it for tests (a local relay is not probed).
bool ranked_shown()
{
    if (gallery_on()) return G.ranked;
    return ranked_test_switch() || ranked_relay() >= 0;
}

// The server chip at the bottom right, beside the status line: the relay the hub is on, its ping
// and who is online ("US EAST  42 MS  -  12 ONLINE"); with `button` the X glyph
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
    // All servers: the label says so, in front of the relay the hub is on now
    const char *tag = all_servers() ? "ALL SERVERS" : "SERVER";
    bool tagged = button || all_servers();
    float label = tagged ? text_width(f, g_title, 10, tag) + 8 : 0;
    float bars = cur >= 0 ? 24 : 0, glyph = button ? 22 : 0, content = w + bars + glyph + label + 16;
    // On our glass door the chip is as wide as the door above it, its content centred (owner, 2026-11-03);
    // in the game's own door it keeps hugging its text at the right
    float x1 = g_game_door ? 606 : DOOR_X + DOOR_W, x0 = g_game_door ? x1 - content : std::min(DOOR_X, x1 - content);
    glass(dl, f, x0, y - 3, x1 - x0, 21, 10, IM_COL32(10, 10, 13, 255), 0.62f);
    bool hot = button && mouse_in(f, x0, y - 3, x1 - x0, 21);
    if (hot) frame_rect(dl, f, x0, y - 3, x1 - x0, 21, with_alpha(ORANGE, 0.8f), 1.2f);
    float x = x0 + 8 + ((x1 - x0) - content) / 2, dy = 2;    // the glyphs sat 2 units above the chip's middle
    if (button) {
        hint(dl, f, x, y - 1 + dy, "x", "");
        x += glyph;
    }
    if (tagged) {
        text(dl, f, g_title, 10, x, y + 1 + dy, ORANGE, tag, LEFT, 0.8f);
        x += label;
    }
    if (cur >= 0) { ping_bars(dl, f, x, y + 1 + dy, 11, ping); x += bars; }
    text(dl, f, g_title, size, x, y + dy, cur >= 0 ? CREAM : ORANGE, line, LEFT, 1.0f);
    return hot && ImGui::IsMouseClicked(0);
}

// ---- players online ----

void draw_players(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    // All servers (build 47): every relay's players, each with its server's tag; otherwise the hub's
    // relay's, and a line with how many are on the others (their STATUS counts)
    static NetServerPlayer list[40];
    static NetPlayer own[40];
    bool all = all_servers();
    int online = -1, rooms = 0, n;
    if (all) n = all_players(list, 40, &online);
    else {
        n = lobby_players(own, 40, &online, &rooms);
        for (int i = 0; i < n; i++) { list[i].player = own[i]; list[i].server = -1; }
    }
    if (pad.x) netplay_lobby_who();
    NetServerInfo sv[NETSERVER_COUNT];
    servers(sv);
    int here = connected_server();
    page_title(dl, f, "PLAYERS ONLINE");
    if (all) text(dl, f, g_hand, 14, 54, 58, ORANGE, "All servers: everyone, with their server.", LEFT, 0.8f);
    online_count(dl, f);
    if (n < 0) {
        text(dl, f, g_title, 20, 320, 200, CREAM, "ASKING THE SERVER...", CENTER, 1.4f);
        return;
    }
    glass(dl, f, 40, 78, 566, 334);
    static const char *const states[] = { "IN THE MENU", "IN A ROOM", "FIGHTING" };
    const ImU32 colours[] = { CREAM, ORANGE, GREEN };
    // the tags are as wide as the widest server's name, so the names line up
    float tag_w = 0;
    if (all)
        for (int i = 0; i < NETSERVER_COUNT; i++) tag_w = std::max(tag_w, text_width(f, g_title, 9, sv[i].short_name));
    for (int i = 0; i < n; i++) {
        float x = i < 20 ? 52 : 330, y = 90 + (i % 20) * 15.0f, nx = x;
        const NetPlayer &p = list[i].player;
        int st = p.state >= 0 && p.state <= 2 ? p.state : 0;
        if (all && list[i].server >= 0 && list[i].server < NETSERVER_COUNT) {
            bool away = list[i].server != here;
            rect(dl, f, x - 2, y + 1, tag_w + 8, 12, away ? IM_COL32(60, 40, 18, 210) : IM_COL32(0, 0, 0, 150), 3);
            text(dl, f, g_title, 9, x + 2 + tag_w / 2, y + 2, away ? ORANGE : GREY, sv[list[i].server].short_name,
                 CENTER, 0.8f);
            nx = x + tag_w + 12;
        }
        text(dl, f, g_title, 13, nx, y, WHITE, p.name, LEFT, 1.1f);
        text(dl, f, g_title, 10, x + 262, y + 2, colours[st], states[st], RIGHT, 1.0f);
    }
    if (n == 0)
        text(dl, f, g_title, 14, 320, 200, GREY, all ? "Nobody with this version on any server right now." :
             "Nobody else with this version right now.", CENTER, 1.0f);
    if (!all && here >= 0) {    // the other servers' players, from their STATUS
        std::string more;
        for (int i = 0; i < NETSERVER_COUNT; i++) {
            if (i == here || sv[i].state != NETSERVER_UP || sv[i].online <= 0) continue;
            char part[64];
            snprintf(part, sizeof(part), "%s%d more on %s", more.empty() ? "" : ", ", sv[i].online, sv[i].name);
            more += part;
        }
        if (!more.empty()) text(dl, f, g_title, 10, 54, 394, ORANGE, (more + ".").c_str(), LEFT, 1.0f);
    }
    if (online > n)
        text(dl, f, g_title, 10, 592, 394, GREY, "and others on other versions or not shown", RIGHT, 1.0f);
}

// ---- the server picker: Automatic, Europe, US East, US West, each with its ping and players, and All ----

void draw_servers(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    NetServerInfo s[NETSERVER_COUNT];
    servers(s);
    // five rows (build 38: three relays) sit closer, so the panel still ends above the hints
    const int rows = 2 + NETSERVER_COUNT, ALL_ROW = 1 + NETSERVER_COUNT;
    const float pitch = 58, row_h = 52;
    int cur = connected_server();
    enum NetServerChoice choice = server_choice();
    int fixed = netservers_choice_relay(choice);
    int chosen_row = fixed >= 0 ? 1 + fixed : choice == NETCHOICE_AUTO ? 0 : choice == NETCHOICE_ALL ? ALL_ROW : -1;
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
        if (mouse_pick(f, x, y, w, h)) S.servers_sel = k;
        bool sel = S.servers_sel == k;
        if (sel) selector(dl, f, x, y, w, h);
        bool all = k == ALL_ROW;
        const NetServerInfo *si = k && !all ? &s[k - 1] : nullptr;
        const char *title = k == 0 ? "AUTOMATIC" : all ? "ALL SERVERS" : si->name;
        text(dl, f, g_title, 21, x + 14, y + 3, sel ? CREAM : with_alpha(GREY, 0.9f), upper(title).c_str(), LEFT, 1.5f);
        // right: the ping, large, in its colour, with its bars (OFFLINE in red for a relay that did not answer)
        char big[32] = "", sub[96] = "";
        ImU32 tone = GREY;
        int ms = -1;
        const NetServerInfo *shown = k == 0 || all ? (nearest >= 0 ? &s[nearest] : nullptr) : si;
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
        else if (all) {     // the rooms of every relay, and who waits for a match on any
            int waiting = 0;
            for (int i = 0; i < NETSERVER_COUNT; i++) if (s[i].state == NETSERVER_UP) waiting += s[i].waiting;
            if (waiting > 0) snprintf(sub, sizeof(sub), "ROOMS ON ALL  -  %d WAITING FOR A MATCH", waiting);
            else snprintf(sub, sizeof(sub), "THE ROOMS OF EVERY SERVER");
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
        if (bars) ping_bars(dl, f, bx + 6, y + 6, 14, ms);
        text(dl, f, g_title, 17, bx - (bars ? 4 : 0), y + 3, sel ? tone : with_alpha(tone, 0.75f), big, RIGHT, 1.2f);
        text(dl, f, g_hand, 13, x + 16, y + 24, sel ? ORANGE : with_alpha(ORANGE, 0.6f), sub, LEFT, 1.0f);
        // the tags: the player's choice, and where the hub is now (with All: its relay, on that relay's row)
        float tx = x + 16;
        if (si && cur == k - 1) {
            const char *tag = "YOU ARE HERE";
            float tw = text_width(f, g_title, 9, tag);
            rect(dl, f, tx, y + 38, tw + 10, 13, IM_COL32(40, 120, 60, 230), 4);
            text(dl, f, g_title, 9, tx + 5, y + 39, WHITE, tag, LEFT, 0.6f);
            tx += tw + 16;
        }
        if (k == chosen_row) {
            const char *tag = "YOUR CHOICE";
            float tw = text_width(f, g_title, 9, tag);
            rect(dl, f, tx, y + 38, tw + 10, 13, with_alpha(RED, 0.9f), 4);
            text(dl, f, g_title, 9, tx + 5, y + 39, WHITE, tag, LEFT, 0.6f);
            tx += tw + 16;
        }
        if (mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
    }
    door_panel(dl, f);
    if (S.servers_sel == ALL_ROW)
        door_text(dl, f, &tex("ui/online_icon.png"), "ALL SERVERS", "YOU CHOOSE WHOM YOU PLAY",
                  "The rooms of Europe, US East and US West in one list; joining one moves you there. Matches across "
                  "the ocean have higher ping.");
    else
        door_text(dl, f, &tex("ui/online_icon.png"), "SERVER", "EUROPE, US EAST OR US WEST",
                  "The nearest one gives the lowest ping. When nobody is there, Quick Match looks on the other servers "
                  "too. Your account, stats and the leaderboard are the same on all.");
    if (checking) {
        float t = (float)(GetTickCount64() % 1200) / 1200.0f;
        rect(dl, f, 50, 92 + rows * pitch - 6, 262, 2, IM_COL32(255, 255, 255, 25), 1);
        rect(dl, f, 50 + 222 * (t < 0.5f ? t * 2 : 2 - t * 2), 92 + rows * pitch - 6, 40, 2, ORANGE, 1);
    }
    if (pad.x && !gallery_on()) { netservers_probe(); say("Checking the servers..."); }
    if (pad.a && !gallery_on()) {
        int k = S.servers_sel;
        if (k == 0) netservers_set_choice(NETCHOICE_AUTO);
        else if (k == ALL_ROW) netservers_set_choice(NETCHOICE_ALL);
        else netservers_set_choice(netservers_relay_choice(k - 1));
        int to = netservers_current();
        S.page = HUB;
        if (to < 0) {
            S.connect_pending = true;
            say("Automatic: checking the servers...");
        } else if (to == cur && netplay_state() != NETPLAY_FAILED) {
            if (k == ALL_ROW) say(std::string("All servers: the rooms of every server. You stay on ") + s[to].name + ".", 5000);
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
        "Classic (stock fighters) or Open (your created fighter too), best of three sets against a player of a "
        "similar rating, each mode with its own rank. Sign in with Discord in the launcher to play ranked.",
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
    // Ranked runs on the relays whose STATUS offers it (every relay reaches the central matchmaker
    // once its tunnel is up): on one that does not, the entry says so and switches over.
    int ranked_at = ranked_relay();
    bool ranked_elsewhere = false;
    {
        int cur = connected_server();
        ranked_elsewhere = cur >= 0 && ranked_at >= 0 && ranked_at != cur;
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
        if (mouse_pick(f, x, y, w, h)) S.hub_sel = i;
        bool sel = S.hub_sel == i;
        if (sel) selector(dl, f, x, y, w, h);
        text(dl, f, g_title, 23, x + 16, y + 3, sel ? CREAM : with_alpha(GREY, 0.85f), items[i], LEFT, 1.6f);
        text(dl, f, g_hand, 12, x + 18, y + 29, sel ? ORANGE : with_alpha(ORANGE, 0.45f), subs[i], LEFT, 1.0f);
        if (mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
    }
    // the door: what the chosen item does
    door_panel(dl, f);
    if (S.hub_sel == HUB_RANKED && ranked_elsewhere) {
        NetServerInfo s[NETSERVER_COUNT];
        servers(s);
        std::string at = upper(s[ranked_at].name), name = s[ranked_at].name;
        door_text(dl, f, &tex("ui/online_icon.png"), items[S.hub_sel], ("ON THE " + at + " SERVER").c_str(),
                  pad_text(("Ranked is not offered on this server right now. Press {a} to switch to " + name +
                            ", then choose Ranked Match again.").c_str()).c_str());
    }
    else
        door_text(dl, f, &tex("ui/online_icon.png"), items[S.hub_sel], subs[S.hub_sel], descs[S.hub_sel]);
    if (S.hub_sel == 0 && mode_count) {
        int type = quick_modes[S.quick_mode];
        const Tex &logo = tex(mode_logo(type));
        float dx = door_dx();
        if (!logo.srv) text(dl, f, g_title, 16, 463 + dx, 404, CREAM, mode_name(type), CENTER);
        else image_fit(dl, f, logo, 383 + dx, 396, 160, 30);
        text(dl, f, g_title, 22, 370 + dx, 398, ORANGE, "<", CENTER, 1.2f);
        text(dl, f, g_title, 22, 556 + dx, 398, ORANGE, ">", CENTER, 1.2f);
        // Team and the special matches: what the mode is (a special match: where), over the logo
        if (int v = fixed_venue(type)) {
            std::string at = "ONE ON ONE AT " + upper(online_venue_name(v));
            text(dl, f, g_hand, 12, 463 + door_dx(), 378, ORANGE, at.c_str(), CENTER, 0.8f, 200);
        } else if (is_team(type))
            text(dl, f, g_hand, 12, 463 + door_dx(), 378, ORANGE, "TWO ON TWO, FOUR PLAYERS", CENTER, 0.8f, 200);
    }

    if (pad.a) {
        bool needs_account = S.hub_sel == 0 || S.hub_sel == 2 || S.hub_sel == 3;
        if ((needs_account || S.hub_sel == 1) && outdated) {
            char line[120];
            snprintf(line, sizeof(line), "Build %d is out: close the game and update in the launcher to play online.", newer);
            say(line);
        } else if (needs_account && !signed_in) {
            say(acc == ACCOUNT_WAITING ? "Finish signing in with Discord in your browser first."
                                       : pad_text("Sign in with Discord first: press {y}."));
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
                S.quick_since = 0;              // the staged search starts when this PC waits alone
                S.quick_scope = 0;
                // The search starts on the home relay: the chosen one, or with Automatic and All the
                // lowest ping (the hub may be on another one after a quick match or a room there)
                int home = !gallery_on() ? netservers_home() : -1, here = connected_server();
                if (home >= 0 && here >= 0 && home != here) {
                    NetServerInfo sv[NETSERVER_COUNT];
                    servers(sv);
                    netservers_quick_on(home, type, qmax, hub_name().c_str());
                    say(std::string("Searching on ") + sv[home].name + " first...");
                } else {
                    netplay_lobby_quick(type, qmax);
                    S.quick_wanted = !NETPLAY_IS_KOTH(type);
                    say("Looking for a room...");
                }
            }
            break;
        case 1:
            if (ranked_elsewhere) {
                NetServerInfo s[NETSERVER_COUNT];
                servers(s);
                netservers_use(ranked_at);
                switch_server((std::string("Switching to the ") + s[ranked_at].name + " server for ranked...").c_str());
                break;
            }
            if (acc_state() != ACCOUNT_IN) { say("Sign in with Discord in the launcher to play ranked."); break; }
            open_ranked_modes();
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
    // Team (2 vs 2) and Free for All / 3-Way, kept apart from the record above (which, like the
    // leaderboard, is one on one): matches, wins, losses, draws; [0] < 0 when the service sent none.
    int team[4] = { -1, 0, 0, 0 }, ffa[4] = { -1, 0, 0, 0 };
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
            else if (k == "team" || k == "ffa") {       // matches|wins|losses|draws (accounts StatsText)
                auto r = split_bar(val);
                int *rec = k == "team" ? v.team : v.ffa;
                if (r.size() >= 4)
                    for (int i = 0; i < 4; i++) rec[i] = std::max(0, atoi(r[i].c_str()));
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
    // Y: the casual record, then one tab per ranked mode (ranked phase 2a), once ranked is offered
    int &tab = ranked_stats_tab();
    if (pad.y && ranked_shown()) tab = (tab + 1) % (1 + NETRANK_MODES);
    if (!ranked_shown()) tab = 0;
    if (ranked_shown()) {
        static const char *const tabs[] = { "CASUAL", "OPEN", "CLASSIC", "DRAFT" };
        float tx = 360;
        for (int i = 0; i < 4; i++) {
            float w = text_width(f, g_title, 12, tabs[i]) + 16;
            rect(dl, f, tx, 54, w, 18, i == tab ? IM_COL32(120, 12, 26, 230) : IM_COL32(0, 0, 0, 150), 5);
            text(dl, f, g_title, 12, tx + w / 2, 56, i == tab ? WHITE : GREY, tabs[i], CENTER, 0.8f);
            tx += w + 4;
        }
    }
    if (tab > 0 && have) { draw_ranked_stats(dl, f, tab - 1); return; }
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
            text(dl, f, g_hand, 14, 320, 236, ORANGE, pad_text("PRESS {X} TO TRY AGAIN").c_str(), CENTER, 1.0f);
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
    // the record above and the leaderboard are one on one; Team and Free for All have their own rows below
    text(dl, f, g_hand, 13, 308, 227, ORANGE, "ONE ON ONE", RIGHT, 1.0f);
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
    // Team and Free for All keep their own records (accounts, build 40): a small row each, only when
    // the service sends them (an older one does not, and nothing shows).
    float y0 = 326;
    for (int k = 0; k < 2; k++) {
        const int *rec = k == 0 ? v.team : v.ffa;
        if (rec[0] < 0) continue;
        char wl[48];
        if (rec[3]) snprintf(wl, sizeof(wl), "%d W   %d L   %d D", rec[1], rec[2], rec[3]);
        else snprintf(wl, sizeof(wl), "%d W   %d L", rec[1], rec[2]);
        text(dl, f, g_hand, 12, 46, y0, ORANGE, k == 0 ? "TEAM 2 VS 2" : "FREE FOR ALL", LEFT, 1.0f);
        text(dl, f, g_title, 12, 118, y0, WHITE, wl, LEFT, 1.0f);
        y0 += 15;
    }
    // last fights: the lines end inside the panel (bottom 406); below Team or Free for All, two of them
    bool extra = y0 > 326;
    int fights = extra ? 2 : 3;
    float pitch = extra ? 16.0f : 17.0f;
    text(dl, f, g_hand, 13, 46, y0, ORANGE, "LAST FIGHTS", LEFT, 1.0f);
    if (v.recent.empty()) text(dl, f, g_title, 12, 46, y0 + 18, GREY, "Play online while signed in.", LEFT, 1.0f);
    for (size_t i = 0; i < v.recent.size() && (int)i < fights; i++) {
        const auto &r = v.recent[i];
        if (r.size() < 4) continue;
        bool win = r[0] == "win", draw = r[0] == "draw";
        std::string line = r[1] + " vs " + (r[2].empty() ? "?" : r[2]);
        float y = y0 + 16 + i * pitch;
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
    text(dl, f, g_title, 10, 592, 100, GREY, "ONE ON ONE, MOST WINS", RIGHT, 1.0f);
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
// With All servers (build 31 "both"; build 38 three relays) the list holds the rooms of Europe, US
// East and US West, the lowest ping first; each row carries its server and this PC's ping there, and
// joining a room on another server moves the hub there first (netservers_join_on).

void draw_rooms(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    static NetServerRoom list[48];
    int n = list_rooms(list, 48);
    NetServerInfo sv[NETSERVER_COUNT];
    servers(sv);
    int here = connected_server();
    bool all = all_servers();
    unsigned long long now = GetTickCount64();
    if (!gallery_on() && now - S.last_list > 3000) { netplay_lobby_list(0xFF); S.last_list = now; }
    if (pad.up && n) S.rooms_sel = (S.rooms_sel + n - 1) % n;
    if (pad.down && n) S.rooms_sel = (S.rooms_sel + 1) % n;
    if (S.rooms_sel >= n) S.rooms_sel = n ? n - 1 : 0;
    if (pad.x) { netplay_lobby_list(0xFF); S.last_list = now; }
    if (pad.y) { S.page = CODE; S.code_pos = 0; return; }

    page_title(dl, f, "FIND A ROOM");
    if (all) text(dl, f, g_hand, 14, 54, 58, ORANGE, "All servers: the lowest ping first.", LEFT, 0.8f);
    online_count(dl, f);
    if (n) glass(dl, f, 36, 76, 290, (std::min(n, 6) - 1) * 52 + 44 + 20);
    else glass(dl, f, 36, 112, 290, 76);
    if (!n) {
        text(dl, f, g_title, 16, 60, 130, GREY, all ? "No open rooms on any server." : "No open rooms right now.");
        text(dl, f, g_hand, 14, 60, 156, ORANGE, "Quick match, or open one yourself.", LEFT, 0.8f);
    }
    int top = std::max(0, std::min(S.rooms_sel - 2, n - 6));
    for (int i = top; i < n && i < top + 6; i++) {
        const NetRoomInfo &r = list[i].room;
        float x = 50, y = 86 + (i - top) * 52, w = 262, h = 44;
        if (mouse_pick(f, x, y, w, h)) S.rooms_sel = i;
        bool sel = i == S.rooms_sel;
        // a King of the Hill room takes players in any phase: they join the back of the line
        bool open = (r.phase == 0 || NETPLAY_IS_KOTH(r.mode)) && r.players < r.max;
        if (sel) selector(dl, f, x, y, w, h);
        text(dl, f, g_title, 19, x + 12, y + 3, sel ? CREAM : with_alpha(GREY, 0.9f), r.name, LEFT, 1.2f);
        char line[96];
        if (int v = fixed_venue(r.mode))        // a special match: where it is fought
            snprintf(line, sizeof(line), "%s at %s  %d/%d", mode_name(r.mode), online_venue_name(v), r.players, r.max);
        else
            snprintf(line, sizeof(line), "%s  %d/%d", mode_name(r.mode), r.players, r.max);
        text(dl, f, g_hand, 13, x + 14, y + 26, open ? ORANGE : with_alpha(ORANGE, 0.45f), line, LEFT, 0.8f);
        if (all && list[i].server >= 0) {      // the server's tag and this PC's ping there, bottom right
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
        char players[160], where[80] = "";
        if (int v = fixed_venue(r.mode)) {      // "At Red Hook Tire Co. " (no second full stop)
            const char *vn = online_venue_name(v);
            size_t len = strlen(vn);
            snprintf(where, sizeof(where), "At %s%s ", vn, len && vn[len - 1] == '.' ? "" : ".");
        }
        else if (is_team(r.mode)) snprintf(where, sizeof(where), "Two on two. ");
        snprintf(players, sizeof(players), "%s%d of %d players. %s", where, r.players, r.max,
                 pad_text(NETPLAY_IS_KOTH(r.mode) ? (open ? "Winner stays on. Press {a} to join the line." : "Full.") :
                          r.phase == 1 ? "Voting on the venue right now." : r.phase == 2 ? "A match is on." :
                          open ? "Open: press {a} to join." : "Full.").c_str());
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
        image_fit(dl, f, tex(mode_logo(r.mode)), 383 + door_dx(), 150, 160, 34);
        door_text(dl, f, nullptr, r.name, mode_name(r.mode), players);
    }
    if (pad.rb && n && can_watch()) {     // spectator mode: watch the chosen room's matches
        const NetServerRoom &e = list[S.rooms_sel];
        const NetRoomInfo &r = e.room;
        if (NETPLAY_IS_KOTH(r.mode)) say("King of the Hill rooms are watched from inside the hill.");
        else if (e.server >= 0 && e.server != here) say(std::string("Watching works on this server's rooms for now: ") +
                                                         "switch to " + sv[e.server].name + " to watch this one.", 6000);
        else if (!gallery_on()) {
            netplay_lobby_spectate(r.code);
            say(r.phase == 2 ? "Watching... a match is on: you watch from the next one." : "Watching...");
        }
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
    if (pad.rb && can_watch()) { netplay_lobby_spectate(S.code); say(std::string("Watching ") + S.code + "..."); S.page = HUB; }
    if (pad.b) S.page = ROOMS;
}

// ---- a created fighter's online name: up to 16 characters, typed or picked with the pad ----
//
// Only the record the other players get carries it; the Story save keeps its user ID (8 characters,
// the game's own limit). Empty: the user ID. Typing takes any printable ASCII character; up and
// down step through these.
const char NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-_'!?&";

void draw_name(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    const auto &cards = cp_cards();
    if (S.name_cp < 0 || S.name_cp >= (int)cards.size()) { S.page = FIGHTERS; return; }
    const CpCard card = cards[S.name_cp];
    const int nc = (int)sizeof(NAME_CHARS) - 1;
    auto index = [&](char c) { const char *p = c ? strchr(NAME_CHARS, c) : nullptr; return p ? (int)(p - NAME_CHARS) : 0; };
    auto len = [&]() { return (int)strlen(S.name); };
    auto last = [&]() { return std::min(len(), CP_NAME_MAX - 1); };     // the cursor: on a character, or after the last
    S.name_pos = std::max(0, std::min(S.name_pos, last()));
    if (pad.left) S.name_pos = std::max(0, S.name_pos - 1);
    if (pad.right) S.name_pos = std::min(last(), S.name_pos + 1);
    if (pad.up || pad.down) {
        if (S.name_pos == len()) S.name[S.name_pos + 1] = 0;     // a new character at the end
        char &c = S.name[S.name_pos];
        c = NAME_CHARS[(index(c) + (pad.up ? 1 : nc - 1)) % nc];
    }
    if (pad.x) { memset(S.name, 0, sizeof(S.name)); S.name_pos = 0; }
    ImGuiIO &io = ImGui::GetIO();
    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        ImWchar ch = io.InputQueueCharacters[i];
        int n = len();
        if (ch < 0x20 || ch > 0x7E) continue;
        if (n < CP_NAME_MAX) {          // inserted at the cursor
            memmove(S.name + S.name_pos + 1, S.name + S.name_pos, (size_t)(n - S.name_pos + 1));
            S.name[S.name_pos] = (char)ch;
        } else S.name[S.name_pos] = (char)ch;   // full: over the character at the cursor
        S.name_pos = std::min(S.name_pos + 1, last());
    }
    if (!gallery_on() && ImGui::IsKeyPressed(ImGuiKey_Backspace, true) && S.name_pos > 0) {
        int at = S.name_pos < len() && S.name_pos == CP_NAME_MAX - 1 && len() == CP_NAME_MAX ? S.name_pos : S.name_pos - 1;
        memmove(S.name + at, S.name + at + 1, (size_t)(len() - at));
        S.name_pos = at;
    }
    if (!gallery_on() && ImGui::IsKeyPressed(ImGuiKey_Delete, true) && S.name_pos < len())
        memmove(S.name + S.name_pos, S.name + S.name_pos + 1, (size_t)(len() - S.name_pos));

    char clean[CP_NAME_MAX + 1];
    bool own = cp_name_clean(S.name, clean, (int)sizeof(clean)) > 0;
    page_title(dl, f, "ONLINE NAME");
    text(dl, f, g_hand, 15, 54, 70, ORANGE, "The name your fighter fights under online.", LEFT, 0.8f, 280);
    door_panel(dl, f);
    door_text(dl, f, nullptr, "ONLINE NAME", "UP TO 16 CHARACTERS",
              "Type it, or pick each letter with up and down. Empty uses the user ID. Your Story save keeps its user ID.");
    // the characters, one box each
    glass(dl, f, 36, 104, 300, 214);
    text(dl, f, g_title, 12, 48, 116, GREY, ("USER ID  " + card.user_id).c_str(), LEFT, 0.8f);
    for (int i = 0; i < CP_NAME_MAX; i++) {
        float x = 48 + i * 17.5f, y = 150, w = 15.5f, h = 34;
        bool at = i == S.name_pos, filled = i < len();
        glass(dl, f, x, y, w, h, 3, at ? IM_COL32(40, 30, 22, 255) : IM_COL32(10, 10, 13, 255), 0.7f);
        if (filled) {
            char c[2] = { S.name[i], 0 };
            text(dl, f, g_title, 18, x + w / 2, y + 7, at ? CREAM : WHITE, c, CENTER, 0.8f);
        }
        if (at) {
            selector(dl, f, x, y, w, h);
            text(dl, f, g_title, 12, x + w / 2, y - 16, ORANGE, "^", CENTER);
            text(dl, f, g_title, 12, x + w / 2, y + h + 1, ORANGE, "v", CENTER);
        }
        if (mouse_click(f, x, y, w, h)) S.name_pos = std::min(i, last());
    }
    char count[16];
    snprintf(count, sizeof(count), "%d / %d", len(), CP_NAME_MAX);
    text(dl, f, g_title, 11, 328, 202, GREY, count, RIGHT, 0.8f);
    // how it shows: the card's orange name, and the user ID when none is set
    const char *shown = own ? clean : card.user_id.c_str();
    float size = fit_size(f, g_hand, 24, shown, 270);
    text(dl, f, g_hand, size, 186, 236 + (24 - size) * 0.5f, own ? ORANGE : with_alpha(CREAM, 0.6f), shown, CENTER, 1.2f);
    // The fight HUD writes the name in capitals and has room for about sixteen M; sixteen W ran under the
    // timer (measured, build 39). The summary cuts a name that wide to about thirteen letters.
    bool wide = own && text_width(f, g_title, 11, upper(clean).c_str()) > text_width(f, g_title, 11, "MMMMMMMMMMMMMMMM");
    text(dl, f, g_title, 11, 186, 272, wide ? ORANGE : GREY,
         wide ? "VERY WIDE: IT MAY RUN UNDER THE FIGHT'S CLOCK" :
         own ? "THE OTHER PLAYERS SEE THIS NAME" : "NO ONLINE NAME: THE USER ID IS USED", CENTER, 0.8f);
    if (pad.a) {
        if (gallery_on()) G.cp_online[S.name_cp] = clean;
        else if (cp_local_set_name(S.name_cp, clean)) {
            if (S.name_cp == cp_picked()) cp_choose(S.name_cp);     // the relay gets the record with the new name
        } else {
            say("The name could not be saved.");
            return;
        }
        say(own ? std::string("Online you are ") + clean + "." : "Online you use your user ID, " + card.user_id + ".");
        S.page = FIGHTERS;
    }
    if (pad.b) S.page = FIGHTERS;
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
    if (fixed_venue(s.mode)) snprintf(caption, sizeof(caption), "ALWAYS AT THIS VENUE");
    else snprintf(caption, sizeof(caption), "%s  -  %d VENUE%s", upper(p >= 0 ? presets()[p].name : "Custom").c_str(), n,
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
    // Team: four, no CPU partners (the relay starts a Team room with four only)
    auto least_of = [](int mode) { return NETPLAY_IS_KOTH(mode) ? 3 : is_team(mode) ? 4 : 2; };
    auto most_of = [](int mode) {
        if (NETPLAY_IS_KOTH(mode)) return NETPLAY_ROOM_SLOTS;
        const OnlineMode *om = online_mode(mode);
        return om ? std::min(4, om->fighters) : 2;
    };
    if (step) {
        switch (rows_of[S.settings_sel]) {
        case R_MODE:
            if (!cycle.empty()) {
                int old_mode = s.mode, old_preset = preset_of(s);
                s.mode = cycle[(cycle_index(cycle, s.mode) + step + (int)cycle.size()) % (int)cycle.size()];
                if (NETPLAY_IS_KOTH(s.mode) && !was_koth) s.max = 6;     // a fresh hill: room for a line
                fit_pool(s, old_mode, old_preset);
            }
            break;
        case R_VENUE_CHOICE:
            s.mode = s.mode == NETPLAY_MODE_KOTH ? NETPLAY_MODE_KOTH_RANDOM : NETPLAY_MODE_KOTH;
            break;
        case R_PLAYERS: s.max = std::max(least_of(s.mode), std::min(most_of(s.mode), s.max + step)); break;
        case R_POOL: if (!fixed_venue(s.mode)) apply_preset(s, (preset_of(s) < 0 ? 0 : preset_of(s)) + step); break;
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
        if (mouse_pick(f, x, y, w, h)) S.settings_sel = i;
        bool sel = S.settings_sel == i;
        glass(dl, f, x, y, w, h, 7, sel ? IM_COL32(44, 32, 22, 255) : IM_COL32(10, 10, 13, 255), sel ? 0.78f : 0.6f);
        if (sel) selector(dl, f, x, y, w, h);
        if (row == R_OPEN) {
            text(dl, f, g_title, 20, x + w / 2, y + h / 2 - 11, sel ? CREAM : GREY, S.settings_edit ? "SAVE" : "OPEN THE ROOM", CENTER);
            if (mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
            continue;
        }
        text(dl, f, g_title, 12, x + 12, y + 4, GREY,
             row == R_POOL && fixed_venue(s.mode) ? "VENUE (THE MODE'S OWN)" : labels[row]);
        std::string value;
        char buf[64];
        switch (row) {
        case R_MODE: value = mode_name(s.mode); break;
        case R_VENUE_CHOICE: value = s.mode == NETPLAY_MODE_KOTH ? "The two fighters vote" : "At random"; break;
        case R_PLAYERS:
            if (koth) { snprintf(buf, sizeof(buf), "Up to %d in the line", s.max); value = buf; break; }
            snprintf(buf, sizeof(buf), is_team(s.mode) ? "%d players, 2 vs 2" : "%d players", s.max); value = buf;
            if (m && s.max < m->fighters) { snprintf(buf, sizeof(buf), " + %d CPU", m->fighters - s.max); value += buf; } break;
        case R_POOL:
            if (int v = fixed_venue(s.mode)) { value = online_venue_name(v); break; }
            { int p = preset_of(s); value = p >= 0 ? presets()[p].name : "Custom"; snprintf(buf, sizeof(buf), " (%d)", s.pool_count); value += buf; } break;
        case R_LISTED: value = s.is_public ? "In the room list" : "By code only"; break;
        }
        text(dl, f, g_title, 17, x + 12, y + 19, sel ? WHITE : GREY, value.c_str());
        // a fixed row (a special match's venue and players) has nothing to step through
        bool fixed = (fixed_venue(s.mode) && (row == R_POOL || row == R_PLAYERS)) || (is_team(s.mode) && row == R_PLAYERS);
        if (sel && !fixed) { text(dl, f, g_title, 17, x + w - 26, y + h / 2 - 10, ORANGE, "<", CENTER); text(dl, f, g_title, 17, x + w - 10, y + h / 2 - 10, ORANGE, ">", CENTER); }
    }
    // the door: the mode's logo (King of the Hill: its rules), then the venues filling the panel
    door_panel(dl, f);
    float venues_y, dx = door_dx();
    if (koth) {
        text(dl, f, g_title, 24, 463 + dx, 92, CREAM, "KING OF THE HILL", CENTER, 1.6f);
        text(dl, f, g_hand, 13, 463 + dx, 120, ORANGE, "WINNER STAYS ON", CENTER, 1.0f);
        text(dl, f, g_body, 12, 362 + dx, 138, WHITE,
             pad_text("TWO FIGHT ONE ON ONE, THE OTHERS WAIT IN LINE. THE LOSER GOES TO THE BACK. "
                      "WHEN YOU'RE CALLED, PRESS {A} WITHIN 20 SECONDS.").c_str(), LEFT, 1.0f, 202);
        venues_y = 196;
    } else {
        image_fit(dl, f, tex(mode_logo(s.mode)), 372 + dx, 96, 170, 36);
        venues_y = 142;
        if (const char *blurb = mode_blurb(s.mode)) {   // Team and the special matches: what they are
            text(dl, f, g_body, 12, 362 + dx, 138, WHITE, upper(blurb).c_str(), LEFT, 1.0f, 202);
            venues_y = 178;
        }
    }
    venue_showcase(dl, f, s, 356 + dx, venues_y, 214, (g_game_door ? 438 : DOOR_Y + DOOR_H - 6) - venues_y);
    if (pad.a && rows_of[S.settings_sel] == R_OPEN) {
        if (S.settings_edit) { netplay_lobby_settings(&s); S.page = HUB; say("Settings saved; everyone readies again."); }
        else { netplay_lobby_create(&s); say("Opening the room..."); S.page = HUB; }
    }
    if (pad.b) S.page = HUB;
    (void)room;
}

// ---- fighter choice: the game's portrait grid ----

// The grid's fighters: the roster, with this player's created fighters first (one card each,
// newest first) when the relay plays them here (not in ranked play, not in King of the Hill rooms).
// g_grid_cp: per grid entry, the created fighter's cp_cards index, -1 for a roster fighter.
std::vector<int> g_grid_cp;
const std::vector<OnlineFighter> &grid_fighters()
{
    static std::vector<OnlineFighter> list;
    static std::vector<std::string> styles;
    list = roster();
    g_grid_cp.assign(list.size(), -1);
    NetRoom r;
    lobby_room(&r);
    if (cp_available() && ranked_created_ok(r) && !(r.in_room && NETPLAY_IS_KOTH(r.settings.mode))) {
        const std::vector<CpCard> cards = cp_cards();
        styles.assign(cards.size(), std::string());
        for (size_t i = 0; i < cards.size(); i++)
            styles[i] = "YOUR FIGHTER" + (cards[i].style.empty() ? std::string() : ": " + cards[i].style);
        for (size_t i = cards.size(); i-- > 0;) {
            OnlineFighter cp{};
            cp.id = NETPLAY_CREATED_FIGHTER;
            snprintf(cp.name, sizeof(cp.name), "%s", cards[i].name.empty() ? "Your fighter" : cards[i].name.c_str());
            cp.style = styles[i].c_str();
            list.insert(list.begin(), cp);
            g_grid_cp.insert(g_grid_cp.begin(), (int)i);
        }
    }
    return list;
}

// Where `fighter` sits in the grid list (a created fighter: the picked card); 0 if not there.
int grid_pos(const std::vector<OnlineFighter> &list, int fighter)
{
    int pick = fighter == NETPLAY_CREATED_FIGHTER ? cp_picked() : -1, at = 0;
    bool seen = false;
    for (size_t i = 0; i < list.size(); i++) {
        if (list[i].id != fighter) continue;
        if (pick < 0 || (i < g_grid_cp.size() && g_grid_cp[i] == pick)) return (int)i;
        if (!seen) { at = (int)i; seen = true; }
    }
    return at;
}

void draw_fighters(ImDrawList *dl, const Frame &f, const Pad &pad, bool *chosen, int *fighter)
{
    const auto &list = grid_fighters();
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
    int cur_cp = S.fighter_sel < (int)g_grid_cp.size() ? g_grid_cp[S.fighter_sel] : -1;
    // the chosen fighter, large, with name and style
    glass(dl, f, 36, 74, 180, 308);
    if (!portrait(dl, f, cur.id, 46, 84, 160, 240))
        text(dl, f, g_title, 26, 126, 180, with_alpha(CREAM, 0.6f), cur.name, CENTER, 1.4f, 150);
    S.fighters_cp = cur_cp;
    if (cur_cp >= 0) {      // a created fighter: which Story save it comes from, and its user ID under an online name
        const auto &cards = cp_cards();
        if (cur_cp < (int)cards.size() && !cards[cur_cp].online.empty())
            text(dl, f, g_title, 10, 126, 290, with_alpha(CREAM, 0.8f), ("USER ID " + cards[cur_cp].user_id).c_str(), CENTER, 1.0f);
        if (cur_cp < (int)cards.size() && !cards[cur_cp].saved.empty())
            text(dl, f, g_title, 10, 126, 304, with_alpha(CREAM, 0.8f), ("SAVED " + cards[cur_cp].saved).c_str(), CENTER, 1.0f);
        if (pad.y && cur_cp < (int)cards.size()) {     // Y: the online name of this card
            S.name_cp = cur_cp;
            snprintf(S.name, sizeof(S.name), "%s", cards[cur_cp].online.c_str());
            S.name_pos = std::min((int)strlen(S.name), CP_NAME_MAX - 1);
            S.page = NAME;
            return;
        }
    }
    {
        float size = fit_size(f, g_hand, 22, cur.name, 168);
        text(dl, f, g_hand, size, 126, 330 + (22 - size) * 0.5f, ORANGE, cur.name, CENTER, 1.2f);
    }
    text(dl, f, g_title, 13, 126, 356, WHITE, cur.style, CENTER, 0, 170);
    // the grid
    float gx = 228, gy = 82, cw = 52, ch = 76;
    glass(dl, f, gx - 8, gy - 8, cols * (cw + 4) + 12, rows * (ch + 6) + 26);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            int i = (S.fighter_top + r) * cols + c;
            if (i >= n) break;
            float x = gx + c * (cw + 4), y = gy + r * (ch + 6);
            if (mouse_pick(f, x, y, cw, ch)) S.fighter_sel = i;
            bool sel = i == S.fighter_sel;
            rect(dl, f, x, y, cw, ch, IM_COL32(0, 0, 0, 150), 3);
            if (!portrait(dl, f, list[i].id, x + 1, y + 1, cw - 2, ch - 2, sel ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 255)))
                text(dl, f, g_body, 9, x + 3, y + 30, GREY, list[i].name, LEFT, 0, cw - 4);
            if (list[i].id == NETPLAY_CREATED_FIGHTER) {     // the fighter's name on an orange band
                float tw = text_width(f, g_title, 9, list[i].name);
                float size = tw > cw - 6 ? 9 * (cw - 6) / tw : 9;
                if (size >= 7) {
                    rect(dl, f, x + 1, y + ch - 15, cw - 2, 14, with_alpha(ORANGE, sel ? 0.95f : 0.6f));
                    text(dl, f, g_title, size, x + cw / 2, y + ch - 14 + (9 - size) * 0.5f, WHITE, list[i].name, CENTER, 0.6f);
                } else {    // a long online name: two lines, split at the blank nearest the middle
                    std::string s = list[i].name;
                    size_t cut = s.size() / 2, best = std::string::npos;
                    for (size_t k = 1; k + 1 < s.size(); k++)
                        if (s[k] == ' ' && (best == std::string::npos || (k > cut ? k - cut : cut - k) < (best > cut ? best - cut : cut - best)))
                            best = k;
                    std::string a = best != std::string::npos ? s.substr(0, best) : s.substr(0, cut);
                    std::string b = best != std::string::npos ? s.substr(best + 1) : s.substr(cut);
                    float sz = std::min(fit_size(f, g_title, 9, a.c_str(), cw - 6), fit_size(f, g_title, 9, b.c_str(), cw - 6));
                    rect(dl, f, x + 1, y + ch - 23, cw - 2, 22, with_alpha(ORANGE, sel ? 0.95f : 0.6f));
                    text(dl, f, g_title, sz, x + cw / 2, y + ch - 22 + (9 - sz) * 0.5f, WHITE, a.c_str(), CENTER, 0.6f);
                    text(dl, f, g_title, sz, x + cw / 2, y + ch - 12 + (9 - sz) * 0.5f, WHITE, b.c_str(), CENTER, 0.6f);
                }
                text(dl, f, g_title, 8, x + cw / 2, y + 3, with_alpha(ORANGE, sel ? 1.0f : 0.7f), "YOURS", CENTER, 0.6f);
            }
            if (sel) selector(dl, f, x, y, cw, ch);
            if (mouse_click(f, x, y, cw, ch)) const_cast<Pad &>(pad).a = true;
        }
    char page[32];
    snprintf(page, sizeof(page), "%d / %d", S.fighter_sel + 1, n);
    text(dl, f, g_title, 12, gx + cols * (cw + 4) - 4, gy + rows * (ch + 6), GREY, page, RIGHT, 0.8f);
    if (pad.a) {
        if (cur_cp >= 0) cp_choose(cur_cp);     // which created fighter: remembered, and sent to the relay
        *chosen = true;
        *fighter = cur.id;
    }
    // BACK from a ranked search's pick goes back to the mode chooser, not to the online menu (a player
    // who wanted another fighter for ranked landed on the hub, 2026-11-03)
    if (pad.b) S.page = S.ranked_pick ? RANKED_MODES : HUB;
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

    NetRankInfo ri;
    bool ranked = ranked_room(r, &ri);
    bool picks = !ranked || ri.game <= 1 || ri.picker < 0 || ri.picker == r.you;   // a ranked set: the loser picks
    page_title(dl, f, ranked && ri.game <= 1 ? "GAME 1: THE VENUE" : ranked && picks ? "PICK THE VENUE" :
                      ranked ? "THE LOSER PICKS" : "VOTE FOR THE VENUE");
    if (ranked) {
        char why[96];
        ranked_banner(dl, f, r, ri, 76);
        text(dl, f, g_hand, 13, 320, 52, ORANGE, ranked_picker_line(r, ri, why, sizeof(why)), CENTER, 0.8f);
        if (!picks) const_cast<Pad &>(pad).a = false;
    }
    char secs[16];
    snprintf(secs, sizeof(secs), "%d", (left + 999) / 1000);
    text(dl, f, g_title, 34, 590, 22, left < 5000 ? RED : CREAM, secs, RIGHT, 1.6f);
    rect(dl, f, 50, 70, 540, 5, IM_COL32(255, 255, 255, 30), 2.5f);
    rect(dl, f, 50, 70, 540 * (left / 15000.0f), 5, ORANGE, 2.5f);
    float w = 170, gap = 15, x0 = 320 - (n * w + (n - 1) * gap) / 2;
    for (int i = 0; i < n; i++) {
        float x = x0 + i * (w + gap), y = 100;
        if (mouse_pick(f, x, y, w, 290)) S.vote_sel = i;
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
        if (sel && picks) selector(dl, f, x, y, w, 290);
        if (mouse_click(f, x, y, w, 290)) const_cast<Pad &>(pad).a = true;
    }
    {   // a way out while voting too: the others go back to the room's lobby (relay leaveRoom)
        float hx = 54;
        if (picks) hx += hint(dl, f, hx, 396, "a", ranked && ri.game <= 1 ? "START NOW" : ranked ? "PICK" : "VOTE");
        hint(dl, f, hx, 396, "b", ranked ? "LEAVE (FORFEIT)" : "LEAVE ROOM");
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

// "Give up my spot" (LB), on every King of the Hill screen this PC is not fighting on: the first
// press sends it to the back of the line (a king gives up the crown); a second press within 5 s,
// or holding LB, leaves the hill. Only on a relay that takes YIELD (build 36 on). Draws the hint
// at (x, y) and returns its width; *left says LB left the hill (the caller takes no more input).
bool yield_offered()
{
    int caps = lobby_caps();
    return caps > 0 && (caps & NETPLAY_CAP_YIELD);
}

void leave_hill(const char *why)
{
    netplay_lobby_leave();
    S.page = HUB;
    S.give_up_at = 0;
    say(why);
}

float give_up(ImDrawList *dl, const Frame &f, const Pad &pad, float x, float y, bool *left)
{
    *left = false;
    if (!yield_offered()) return 0;
    unsigned long long now = GetTickCount64();
    bool armed = S.give_up_at && now - S.give_up_at < 5000;
    if (!pad.lb_down) { S.lb_since = 0; S.lb_long_done = false; }
    else if (!S.lb_since) S.lb_since = now;
    bool held = pad.lb_down && S.lb_since && now - S.lb_since >= 1200 && !S.lb_long_done;
    float w = hint(dl, f, x, y, "lb", armed ? "AGAIN: LEAVE THE HILL" : "GIVE UP MY SPOT");
    if ((pad.lb_once && armed) || held) {
        S.lb_long_done = true;
        *left = true;
        leave_hill("You left the hill.");
    } else if (pad.lb_once) {
        netplay_lobby_yield();
        S.give_up_at = now;
        say(pad_text("You gave up your spot: back of the line. {LB} again (or hold {LB}) leaves the hill."), 5000);
    }
    return w;
}

// Opens the fighter list on this PC's fighter (King of the Hill: the pick counts from the next fight).
void choose_fighter(int fighter)
{
    S.page = FIGHTERS;
    const auto &list = roster();
    for (size_t i = 0; i < list.size(); i++) if (list[i].id == fighter) S.fighter_sel = (int)i;
}

// The watching screen: the fight this PC does not play in. Phase 1 shows who fights, the king's
// crown and streak, and the line; the fight itself runs on the fighters' PCs only.
void draw_watch(ImDrawList *dl, const Frame &f, const Pad &pad, const NetRoom &r, const HillView &v)
{
    const NetHill &h = r.hill;
    int a = v.pair[0], b = v.pair[1];
    if (h.watch_seed && h.watch_seed == h.seed) { a = h.watch_slots[0]; b = h.watch_slots[1]; }
    if (b >= 0 && a == h.king) std::swap(a, b);     // the king on the right, the challenger on the left
    // the "WAITING" tag, as a broadcast's: the queue sees the result only, not the fight (players took
    // "WATCHING" for a live view, F9 083315 and a Discord question, 2026-11-03)
    rect(dl, f, 50, 26, 118, 28, with_alpha(RED, 0.95f), 3);
    dl->AddCircleFilled(f.p(64, 40), 5 * f.s(), (GetTickCount64() / 600) % 2 ? WHITE : with_alpha(WHITE, 0.3f));
    text(dl, f, g_title, 18, 76, 30, WHITE, "WAITING", LEFT, 1.0f);
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
    bool left = false;
    hx += hint(dl, f, hx, 414, "x", "CHOOSE FIGHTER");
    hx += give_up(dl, f, pad, hx, 414, &left);
    hx += hint(dl, f, hx, 414, "b", "BACK TO THE HILL");
    (void)hx;
    if (left) return;
    if (pad.x) choose_fighter(slot_fighter(r, r.you));
    else if (pad.b) S.watch_hidden = h.seed ? h.seed : 1;
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
    std::string say_line = pad_text(ready ? "Waiting for your opponent to press {a}..."
                                          : "Press {a} now: not ready in time sends you to the back of the line.");
    text(dl, f, g_hand, 16, 242, 304, ORANGE, say_line.c_str(), LEFT, 0.8f, 360);
    float hx = 242;
    if (!ready) hx += hint(dl, f, hx, 352, "a", "READY");
    hx += hint(dl, f, hx, 352, "x", "CHOOSE FIGHTER");
    hx = 242;
    bool left = false;
    hx += give_up(dl, f, pad, hx, 396, &left);
    hx += hint(dl, f, hx, 396, "b", "LEAVE ROOM");
    (void)hx;
    if (left) return;
    if (pad.a && !ready) netplay_lobby_ready(1, fighter);
    if (pad.x) choose_fighter(fighter);
    else if (pad.b) leave_hill("You left the room.");
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
        snprintf(buf, sizeof(buf), "Both press %s within %d s or go to the back.", pad_text("{a}").c_str(),
                 (v.ms_left + 999) / 1000);
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
        text(dl, f, g_hand, 13, 463, 370, RED, pad_text("YOU ARE AWAY: PRESS {A} TO GET BACK IN THE LINE").c_str(), CENTER, 0.8f, 250);
    else if (v.next == r.you)
        text(dl, f, g_hand, 18, 463, 368, ORANGE, "YOU'RE UP NEXT", CENTER, 1.2f);

    // two rows: what this PC can do in the line, then the room
    float hx = 54, hy = 398;
    bool left = false;
    if (v.my_state == NETLINE_AWAY) hx += hint(dl, f, hx, hy, "a", "I'M BACK");
    hx += hint(dl, f, hx, hy, "x", "CHOOSE FIGHTER");
    if (h.valid && v.my_index >= 0) hx += give_up(dl, f, pad, hx, hy, &left);
    hint(dl, f, hx, hy, "b", "LEAVE ROOM");
    if (left) return;
    bool fight_on = h.valid && h.phase == NETHILL_FIGHTING && !fighting_me;
    bool settings_ok = host && h.valid && h.phase != NETHILL_VOTING && h.phase != NETHILL_FIGHTING;
    hx = 54, hy = 414;
    if (fight_on) hx += hint(dl, f, hx, hy, "y", "WATCH");
    else if (settings_ok) hx += hint(dl, f, hx, hy, "y", "ROOM SETTINGS");
    hint(dl, f, hx, hy, "rb", "VOICE");

    if (pad.a && v.my_state == NETLINE_AWAY) netplay_lobby_ready(1, fighter);
    if (pad.x) choose_fighter(fighter);
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
    {   // a Draft set's pick and ban (ranked_ui.inc)
        NetDraft d;
        if (draft_shown(r, &d)) { draw_draft(dl, f, pad, r, d); return; }
    }
    const NetRoomPlayer *me = nullptr;
    for (int i = 0; i < r.player_count; i++) if (r.players[i].slot == r.you) me = &r.players[i];
    bool host = r.you == r.host;
    int fighter = saved_fighter();
    bool watcher = r.you == NETPLAY_SLOT_SPECTATOR;     // spectator mode: this PC only watches
    if (S.room_version != r.version) S.room_version = r.version;

    if (r.phase == 1) {
        draw_vote(dl, f, watcher ? Pad() : pad, r);
        if (watcher && pad.b) { netplay_lobby_leave(); S.page = HUB; }
        return;
    }
    if (watcher) host = false;
    // The relay still plays the room's match while this PC is back: who is still on the results
    unsigned results_slots = 0;
    bool results_back = !watcher && r.phase == 2 && results_wait(&results_slots);

    NetRankInfo ri;
    bool ranked = ranked_room(r, &ri);
    page_title(dl, f, r.settings.name);
    image_fit(dl, f, tex(mode_logo(r.settings.mode)), 360, 22, 220, 30);
    if (ranked) ranked_banner(dl, f, r, ri, 56);
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

    // Rollback (build 46): each player's PC reports whether it rolls back (P_PINGINFO); a match rolls back
    // only when every player does, so players see why theirs plays in lockstep. Shown while the relay
    // allows rollback and at least one player has the setting on; never in a match of the hill.
    bool rb_room = false, rb_any_on = false;
    std::string rb_off;
    int rb_off_count = 0, rb_restart = 0;
    if (lobby_caps() > 0 && (lobby_caps() & NETPLAY_CAP_ROLLBACK) && r.phase == 0) {
        for (int i = 0; i < r.player_count; i++) {
            int v = room_rb(r.players[i].slot);
            if (v > 0) rb_room = true;
            if (v >= 0 && (v & 1)) rb_any_on = true;
        }
        for (int i = 0; rb_room && i < r.player_count; i++) {
            int v = room_rb(r.players[i].slot);
            if (v < 0 || (v & 1)) continue;
            rb_off += std::string(rb_off_count++ ? ", " : "") + (r.players[i].slot == r.you ? "you" : r.players[i].name);
            if (v & 2) rb_restart++;
        }
    }
    if (rb_room && rb_any_on && rb_off_count && !ranked) {
        char line[160];
        bool you_only = rb_off_count == 1 && rb_off == "you";
        if (rb_restart == rb_off_count)
            snprintf(line, sizeof(line), "%s MUST RESTART THE GAME FOR ROLLBACK: THIS MATCH PLAYS IN LOCKSTEP",
                     upper(rb_off.c_str()).c_str());
        else
            snprintf(line, sizeof(line), "%s %s ROLLBACK OFF: THIS MATCH PLAYS IN LOCKSTEP", upper(rb_off.c_str()).c_str(),
                     you_only || rb_off_count > 1 ? "HAVE" : "HAS");
        float bx = 48;
        if (worst > 150) {      // after the high-ping warning
            char warn[96];
            snprintf(warn, sizeof(warn), "HIGH PING (ABOUT %d MS): INPUTS WILL FEEL DELAYED", worst);
            bx += text_width(f, g_title, 11, warn) + 20;
        }
        float room_w = 470 - bx - 14;
        float size = fit_size(f, g_title, 11, line, room_w);
        float lw = text_width(f, g_title, size, line);
        rect(dl, f, bx, 61, lw + 14, 18, IM_COL32(20, 70, 150, 220), 5);
        text(dl, f, g_title, size, bx + 7, 63 + (11 - size) * 0.5f, WHITE, line, LEFT, 0.8f);
    }

    // the players' cards. Team: the two sides apart, with their names under them (room slots 0 and 1
    // against 2 and 3; the room holds four, in slot order). A special match: the two players on the
    // left, its venue on the right.
    int places = std::max(r.settings.max, r.player_count);
    bool team = is_team(r.settings.mode) && places == 4;
    int venue = fixed_venue(r.settings.mode);
    float w = 128, gap = 10, x0 = 320 - (places * w + (places - 1) * gap) / 2, top = 86;
    if (team) { gap = 6; x0 = 320 - (4 * w + 2 * gap + 30) / 2; top = 80; }
    if (venue) x0 = 52;
    auto card_x = [&](int i) { return x0 + i * (w + gap) + (team && i >= 2 ? 30 - gap : 0); };
    if (team) {
        for (int side = 0; side < 2; side++) {
            float bx = card_x(2 * side), bw = 2 * w + gap;
            rect(dl, f, bx, top + 272, bw, 18, side ? IM_COL32(150, 14, 30, 210) : IM_COL32(20, 70, 150, 210), 5);
            text(dl, f, g_title, 13, bx + bw / 2, top + 273, WHITE, side ? "TEAM 2" : "TEAM 1", CENTER, 1.0f);
        }
        text(dl, f, g_stencil, 24, 320, top + 120, CREAM, "VS", CENTER, 1.4f);
    }
    if (venue) {
        glass(dl, f, 346, 86, 254, 270, 10);
        venue_tile(dl, f, venue, 356, 96, 234, 168, 1.0f);
        text(dl, f, g_hand, 15, 473, 272, ORANGE, ("ALWAYS AT " + upper(online_venue_name(venue))).c_str(), CENTER, 1.0f, 240);
        if (const char *blurb = mode_blurb(r.settings.mode))
            text(dl, f, g_body, 12, 360, 294, WHITE, upper(blurb).c_str(), LEFT, 1.0f, 226);
    }
    for (int i = 0; i < places; i++) {
        float x = card_x(i), y = top;
        // a Team room's card is its room slot (the side), the others list the players in order
        int at = i;
        if (team) {
            at = r.player_count;
            for (int k = 0; k < r.player_count; k++) if (r.players[k].slot == i) at = k;
        }
        glass(dl, f, x, y, w, 270, 10, at < r.player_count && r.players[at].slot == r.you ?IM_COL32(44, 30, 20, 255) : IM_COL32(10, 10, 13, 255), 0.62f);
        if (at >= r.player_count) {
            text(dl, f, g_hand, 15, x + w / 2, y + 110, with_alpha(ORANGE, 0.6f), "WAITING", CENTER, 0.8f);
            text(dl, f, g_title, 11, x + w / 2, y + 134, GREY, "an open place", CENTER, 0.6f);
            continue;
        }
        const NetRoomPlayer &p = r.players[at];
        // the server knows a fighter from the first READY; until then this PC shows its own pick
        int shown = p.fighter != 0xFF ? p.fighter : p.slot == r.you ? fighter : -1;
        bool known = shown >= 0;
        if (!known || !portrait(dl, f, shown, x + 4, y + 4, w - 8, (w - 8) * 1.5f))
            text(dl, f, g_title, 40, x + w / 2, y + 70, GREY, "?", CENTER);
        text(dl, f, g_hand, 15, x + w / 2, y + 200, p.slot == r.you ? ORANGE : CREAM, p.name, CENTER, 1.0f);
        {
            std::string label = known ? fighter_label(shown, p.slot) : "choosing";
            float size = fit_size(f, g_title, 11, label.c_str(), w - 10);     // a 16-character online name
            text(dl, f, g_title, size, x + w / 2, y + 222 + (11 - size) * 0.5f, GREY, label.c_str(), CENTER, 0.6f);
        }
        if (shown == NETPLAY_CREATED_FIGHTER) {     // a created fighter: its styles under the name
            char cn[CP_NAME_MAX + 1];
            const char *st = "";
            if (cp_slot(p.slot, cn, sizeof(cn), &st) && st[0])
                text(dl, f, g_title, 9, x + w / 2, y + 236, with_alpha(ORANGE, 0.8f), st, CENTER, 0.5f, w - 10);
        }
        if (ranked) ranked_card(dl, f, ri, p.slot, x, y + 160, w);
        else if (p.slot == r.host) text(dl, f, g_title, 10, x + 6, y + 6, ORANGE, "HOST", LEFT, 1.0f);
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
        if (rb_room) {      // rollback (build 46): this player's next match, as its PC reports it
            int v = room_rb(p.slot);
            if (v >= 0) {
                const char *tag = (v & 1) ? "ROLLBACK" : "LOCKSTEP";
                float tw = text_width(f, g_title, 9, tag);
                rect(dl, f, x + w - tw - 14, y + 22, tw + 10, 14, IM_COL32(0, 0, 0, 160), 4);
                text(dl, f, g_title, 9, x + w - 9, y + 24, (v & 1) ? GREEN : GREY, tag, RIGHT, 1.0f);
            }
        }
        if (r.phase == 2 && p.slot < 32 && (results_slots & (1u << p.slot))) {   // still on the last match's results
            rect(dl, f, x + 14, y + 240, w - 28, 22, IM_COL32(60, 60, 66, 230), 5);
            text(dl, f, g_title, 13, x + w / 2, y + 243, CREAM, "ON RESULTS", CENTER, 0.8f);
        } else if (p.ready && r.phase != 2) {   // while the relay still plays the match the flags are the last one's
            rect(dl, f, x + 14, y + 240, w - 28, 22, with_alpha(RED, 0.9f), 5);
            text(dl, f, g_title, 15, x + w / 2, y + 242, WHITE, "READY", CENTER, 0.8f);
        }
    }
    // who watches (spectator mode), on the hints' line at the right
    if (r.spectator_count > 0) {
        std::string line = "WATCHING:";
        for (int i = 0; i < r.spectator_count; i++) line += std::string(i ? ", " : " ") + r.spectators[i];
        float size = fit_size(f, g_title, 11, line.c_str(), 250);
        text(dl, f, g_title, size, 590, 396, GREY, line.c_str(), RIGHT, 0.6f);
    }
    if (watcher) {      // a spectator: the players' cards, a banner, and only LEAVE
        rect(dl, f, 48, 61, text_width(f, g_title, 11, "SPECTATING") + 14, 18, IM_COL32(20, 70, 150, 220), 5);
        text(dl, f, g_title, 11, 55, 63, WHITE, "SPECTATING", LEFT, 0.8f);
        text(dl, f, g_hand, 15, 320, team ? 374 : 366, ORANGE,
             r.phase == 2 ? "A match is on: you watch from the next one." : "You watch: the match plays here when it starts.",
             CENTER, 0.8f);
        hint(dl, f, 54, 392, "b", "LEAVE ROOM");
        if (pad.b) { netplay_lobby_leave(); S.page = HUB; say("You stopped watching the room."); }
        return;
    }
    if (r.phase == 2) {
        // The relay keeps the room playing until every player is back from the match (60 s after the
        // first at most, since build 46): READY, the fighter and the settings are refused until then, so
        // none is offered. Said who it waits for (their No on the results has not come).
        std::string who;
        int named = 0, total = 0;
        for (int i = 0; i < r.player_count; i++)
            if (r.players[i].slot < 32 && (results_slots & (1u << r.players[i].slot))) total++;
        for (int i = 0; i < r.player_count; i++) {
            if (r.players[i].slot >= 32 || !(results_slots & (1u << r.players[i].slot))) continue;
            named++;
            if (named > 1) who += named == total ? " and " : ", ";
            who += r.players[i].name;
        }
        std::string line = !results_back ? "A match is on in this room..." :
                           who.empty() ? "Waiting for the others to leave the results..." :
                                         "Waiting for " + who + " to leave the results...";
        float size = fit_size(f, g_hand, 15, line.c_str(), 540);
        text(dl, f, g_hand, size, 320, team ? 374 : 366, ORANGE, line.c_str(), CENTER, 0.8f);
        hint(dl, f, 54, 392, "b", "LEAVE ROOM");     // leaving a ranked room while it plays is no forfeit
        if (pad.b) { netplay_lobby_leave(); S.page = HUB; say("You left the room."); }
        return;
    }
    // hints
    bool ready = me && me->ready;
    float hx = 54, hy = 392;
    // The staged quick-match search (build 38, netplay_servers_rules.h): alone in a quick room, the
    // search covers this relay first, then (Automatic and All) the other North American relay from 10 s
    // and every relay from 30 s (a fixed choice too); a player waiting there for this mode is met (the
    // younger room's player moves to the older room's relay), at most three moves per QUICK MATCH. A
    // room this PC opened or joined by its code is not a quick room.
    if (S.quick_wanted && r.phase == 0) { snprintf(S.quick_room, sizeof(S.quick_room), "%s", r.code); S.quick_wanted = false; }
    bool alone = r.player_count < 2 && r.phase == 0 && !strcmp(S.quick_room, r.code);
    unsigned long long tick = GetTickCount64();
    if (!alone) S.alone_since = 0;
    else if (!S.alone_since) S.alone_since = tick;
    if (r.player_count >= 2) S.quick_since = 0;         // met: a later wait alone starts over
    else if (alone && !S.quick_since) S.quick_since = tick;
    unsigned scope = 0;
    if (alone) {
        int search_s = (int)((tick - S.quick_since) / 1000), alone_s = (int)((tick - S.alone_since) / 1000);
        if (gallery_on()) {
            unsigned up = 0;
            for (int i = 0; i < NETSERVER_COUNT; i++) if (sv[i].state == NETSERVER_UP) up |= 1u << i;
            scope = NETPLAY_IS_KOTH(S.last_quick) ? 0 : netservers_scope(G.choice, cur, search_s, up);
        } else if (!netservers_moving()) {
            int waiting = 0, ping = -1;
            int to = netservers_quick_move(S.last_quick, search_s, alone_s, r.code, &waiting, &ping, &scope);
            if (to >= 0 && S.auto_moves < 3) {
                char line[160];
                snprintf(line, sizeof(line), "Nobody here yet: moving to %s (%d ms), where %d player%s waiting for %s.",
                         sv[to].name, ping, waiting, waiting == 1 ? " is" : "s are", mode_name(S.last_quick));
                say(line, 8000);
                S.auto_moves++;
                S.quick_room[0] = 0;
                S.alone_since = 0;
                S.quick_scope = 0;
                netservers_quick_on(to, S.last_quick, S.last_quick_max, hub_name().c_str());
                S.page = HUB;
                return;
            }
        }
        // the notice when the search widens ("Looking on US West too...", "Looking on every server...")
        if (!gallery_on() && S.quick_scope && (scope & ~S.quick_scope)) {
            std::string all = scope_words(sv, scope), added = scope_words(sv, scope & ~S.quick_scope);
            say(all == "every server" ? std::string("Looking on every server...") : "Looking on " + added + " too...", 5000);
        }
        if (scope) S.quick_scope = scope;
    }
    float my = team ? 374 : 366;
    if (alone && scope) {       // where it searches now
        std::string line = "Waiting for another player on " + scope_words(sv, scope) + "...";
        text(dl, f, g_hand, 15, 320, my, ORANGE, line.c_str(), CENTER, 0.8f);
    } else if (team && r.player_count < 4) {
        char line[96];
        snprintf(line, sizeof(line), "Team is two on two: waiting for %d more player%s...", 4 - r.player_count,
                 4 - r.player_count == 1 ? "" : "s");
        text(dl, f, g_hand, 15, 320, my, ORANGE, line, CENTER, 0.8f);
    } else if (r.player_count < 2) text(dl, f, g_hand, 15, 320, my, ORANGE, "Waiting for another player...", CENTER, 0.8f);
    else if (ranked) {
        char why[96];
        text(dl, f, g_hand, 15, 320, my, ORANGE, ready ? ranked_picker_line(r, ri, why, sizeof(why)) :
             "Ready up for the next game (a new fighter if you like).", CENTER, 0.8f);
    }
    else if (!ready) text(dl, f, g_hand, 15, 320, my, ORANGE, venue ? "Ready up: when both are, the fight starts."
                                                              : "Ready up: when everyone is, the venue vote starts.", CENTER, 0.8f);
    hx += hint(dl, f, hx, hy, "a", ready ? "NOT READY" : "READY");
    if (!ready) hx += hint(dl, f, hx, hy, "x", "CHOOSE FIGHTER");
    if (ranked) host = false;       // a ranked room's settings are the set's
    if (host && !ready) hx += hint(dl, f, hx, hy, "y", "ROOM SETTINGS");
    hx += hint(dl, f, hx, hy, "rb", "VOICE");
    hint(dl, f, hx, hy, "b", ranked ? "LEAVE (FORFEIT)" : "LEAVE ROOM");

    if (pad.a) netplay_lobby_ready(ready ? 0 : 1, fighter);
    if (pad.x && !ready) {
        S.page = FIGHTERS;
        S.fighter_sel = grid_pos(grid_fighters(), fighter);
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
        if (mouse_pick(f, x, y, w, h)) S.voice_sel = i;
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

// ---- the versus screen: who fights whom, where, while the match loads ----------------------------
//
// The original game shows the fighters and the venue before a fight; online the room's START goes
// straight from the room into the load (automatch.c drives the game's own selection screens), so the
// port draws its own: from the START until the fight's first steps have run on every PC
// (netplay_versus), first over the MATCH FOUND moment of the online screens, then with the online
// screens closed, over the game's loading screen. Drawing only: it reads a copy of what the START
// set up and takes no input, so the player's controller belongs to the game the whole time the
// screens are closed, and nothing about the match, its inputs or its timing changes. The music
// under it is the game's own: StartGame stops the menu music and the game plays the loading part of
// the fight's track (docs/launcher-and-overlay.md, "The versus screen").

float ease_out(float t)
{
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

// Moves and fades what was drawn since `from` (an index into the draw list's vertices): the
// entrance and the fade work on whole groups (a card, the venue) without a parameter on every helper.
void shift_fade(ImDrawList *dl, const Frame &f, int from, float dx, float dy, float alpha)
{
    if (alpha >= 0.999f && dx == 0 && dy == 0) return;
    float ox = dx * f.sx, oy = dy * f.sy;
    unsigned scale = (unsigned)((alpha < 0 ? 0 : alpha > 1 ? 1 : alpha) * 256.0f + 0.5f);
    for (int i = from; i < dl->VtxBuffer.Size; i++) {
        ImDrawVert &v = dl->VtxBuffer[i];
        v.pos.x += ox;
        v.pos.y += oy;
        unsigned a = (v.col >> IM_COL32_A_SHIFT) & 0xFF;
        v.col = (v.col & ~IM_COL32_A_MASK) | (((a * scale) >> 8) << IM_COL32_A_SHIFT);
    }
}

const char *fighter_style(int id)
{
    for (const OnlineFighter &o : roster()) if (o.id == id) return o.style ? o.style : "";
    const OnlineFighter *all;
    int n = online_fighters(&all);
    for (int i = 0; i < n; i++) if (all[i].id == id) return all[i].style ? all[i].style : "";
    return "";
}

// One fighter's card: the portrait, the player's name, the fighter and the style; the king of the
// hill gets the crown and his streak. `ph`: the portrait's height.
void versus_card(ImDrawList *dl, const Frame &f, const NetVersus &v, int i, float x, float y, float w, float ph, bool big)
{
    bool me = i == v.you && !v.cpu[i], king = i == v.king;
    float h = ph + (big ? 92 : 78);
    glass(dl, f, x, y, w, h, 12, me ? IM_COL32(56, 14, 20, 255) : IM_COL32(10, 10, 13, 255), me ? 0.70f : 0.62f);
    if (king) frame_rect(dl, f, x + 1, y + 1, w - 2, h - 2, with_alpha(GOLD, 0.55f), 1.5f);
    rect(dl, f, x + 6, y + 6, w - 12, ph, IM_COL32(0, 0, 0, 120), 6);
    if (!portrait(dl, f, v.fighter[i], x + 6, y + 6, w - 12, ph))
        text(dl, f, g_title, 40, x + w / 2, y + ph / 2 - 20, GREY, "?", CENTER);
    // the portrait fades into the card at its foot, so the name plate reads as part of it
    dl->AddRectFilledMultiColor(f.p(x + 6, y + 6 + ph * 0.72f), f.p(x + w - 6, y + 6 + ph), IM_COL32(8, 8, 10, 0),
                                IM_COL32(8, 8, 10, 0), IM_COL32(8, 8, 10, 215), IM_COL32(8, 8, 10, 215));
    if (king) {
        crown(dl, f, x + w / 2, y - 14, big ? 46 : 34);
        char streak[40];
        snprintf(streak, sizeof(streak), v.streak == 1 ? "%d WIN IN A ROW" : "%d WINS IN A ROW", v.streak);
        rect(dl, f, x + 6, y + 6, w - 12, 20, IM_COL32(60, 40, 4, 200), 6);
        text(dl, f, g_title, big ? 13 : 11, x + w / 2, y + (big ? 9 : 10), GOLD, streak, CENTER, 1.0f);
    }
    if (me) {   // "YOU" on the portrait's corner
        text(dl, f, g_title, 11, x + w - 12, y + (king ? 30 : 12), ORANGE, "YOU", RIGHT, 1.0f);
    }
    float ty = y + 6 + ph - (big ? 26 : 22);
    const char *who = v.cpu[i] ? "CPU" : v.name[i][0] ? v.name[i] : "?";
    text(dl, f, g_hand, big ? 24 : 18, x + w / 2, ty, v.cpu[i] ? GREY : me ? ORANGE : CREAM, who, CENTER, 1.3f, 0);
    bool created = v.fighter[i] == NETPLAY_CREATED_FIGHTER;     // a Story-mode fighter: its record's name
    std::string fighter = upper(created ? (v.cp_name[i][0] ? v.cp_name[i] : "Created fighter") : online_fighter_name(v.fighter[i]));
    {
        float size = fit_size(f, g_title, big ? 18 : 14, fighter.c_str(), w - 8);     // a 16-character online name
        text(dl, f, g_title, size, x + w / 2, ty + (big ? 34 : 28) + ((big ? 18 : 14) - size) * 0.5f,
             created ? ORANGE : WHITE, fighter.c_str(), CENTER, 1.2f);
    }
    std::string style = created ? std::string(v.cp_style[i]) : upper(fighter_style(v.fighter[i]));
    if (!style.empty())
        text(dl, f, g_body, big ? 14 : 12, x + w / 2, ty + (big ? 58 : 48), with_alpha(CREAM, 0.75f), style.c_str(), CENTER, 0.8f);
    if (!v.cpu[i] && v.ping[i] >= 0) {     // the player's own ping to the server, as it reported it
        char ms[16];
        snprintf(ms, sizeof(ms), "%d MS", v.ping[i]);
        float pw = text_width(f, g_title, 10, ms);
        ping_bars(dl, f, x + w - 14 - pw - 22, y + h - 17, 9, v.ping[i]);
        text(dl, f, g_title, 10, x + w - 12, y + h - 18, ping_colour(v.ping[i]), ms, RIGHT, 0.8f);
    }
}

// "VS", the game's stencil over a soft red light (steady: no flashing), `s` its scale.
void versus_mark(ImDrawList *dl, const Frame &f, float cx, float cy, float size, float s)
{
    if (g_light.srv) {
        float r = size * 1.6f * s;
        dl->AddImage((ImTextureID)(intptr_t)g_light.srv, f.p(cx - r, cy - r * 0.8f), f.p(cx + r, cy + r * 0.8f),
                     ImVec2(0, 0), ImVec2(1, 1), IM_COL32(220, 20, 30, 150));
    }
    float px = size * s;
    text(dl, f, g_stencil, px, cx, cy - px * 0.55f, CREAM, "VS", CENTER, 2.4f * s);
}

// The venue: its photo and name banner (the vote's art), or its name in words without the art.
// Every venue photo carries the venue's name on its sign. The banner the game keeps beside it is
// mostly the venue's subtitle ("OLD SCHOOL CLUB" under The Terror Dome, "BLAZE'S GYM" under
// Stapleton Athletics), but for these venue.xml feIDs it only repeats the name: The Foundation,
// The Limit and its Intense, Chopshop, Ambush Alley, Red Hook Tire Co. Shown, the name read twice.
bool banner_repeats_name(int fe)
{
    switch (fe) { case 1: case 2: case 3: case 13: case 23: case 24: return true; }
    return false;
}

// The venue: its photo and, when it says something the photo does not, its subtitle banner.
void versus_venue(ImDrawList *dl, const Frame &f, int venue, float x, float y, float w, float photo, bool row)
{
    const Tex &pic = venue_tex(venue), &banner = venue_name_tex(venue);
    bool subtitle = banner.srv && !banner_repeats_name(venue);
    std::string name = upper(online_venue_name(venue));
    if (row) {      // a wide panel: the photo left (small: its sign is hard to read), the label and banner right
        glass(dl, f, x, y, w, photo + 12, 12);
        if (!image(dl, f, pic, x + 6, y + 6, photo, photo)) text(dl, f, g_title, 12, x + 6 + photo / 2, y + photo / 2, GREY, "?", CENTER);
        float bx = x + photo + 18, bw = w - photo - 30;
        text(dl, f, g_title, 11, bx, y + 10, GREY, "TONIGHT'S VENUE", LEFT, 0.8f);
        if (subtitle) image_fit(dl, f, banner, bx - 4, y + 24, bw, photo - 26);
        else text(dl, f, g_hand, 22, bx, y + 30, ORANGE, name.c_str(), LEFT, 1.2f, bw);
        return;
    }
    glass(dl, f, x, y, w, photo + 62, 12);
    text(dl, f, g_title, 11, x + w / 2, y + 7, GREY, "TONIGHT'S VENUE", CENTER, 0.8f);
    // without a subtitle the photo takes the banner's place too
    float side = subtitle ? photo : std::min(photo + 32, w - 12), top = y + 22 + (subtitle ? 0 : (photo + 32 - side) / 2);
    if (!image(dl, f, pic, x + (w - side) / 2, top, side, side))
        text(dl, f, g_title, 14, x + w / 2, y + 22 + photo / 2 - 8, CREAM, name.c_str(), CENTER, 1.2f, photo);
    if (subtitle) image_fit(dl, f, banner, x + 4, y + photo + 26, w - 8, 30);
}

// The versus page, full screen. `v.ms` drives the entrance, `v.alpha` the fade at the fight's start.
void versus_page(ImDrawList *dl, ImVec2 display, const NetVersus &v)
{
    Frame f = menu_frame(display);
    float t = v.ms / 1000.0f;
    int all = dl->VtxBuffer.Size;
    backdrop(dl, f, display, true, LOOK_MATCH);
    bool koth = NETPLAY_IS_KOTH(v.room_mode) || v.king >= 0;
    int n = v.count < 2 ? 2 : v.count > 4 ? 4 : v.count;

    // the header: the mode's logo (King of the Hill: the crown and its name), the room, the server
    int k = dl->VtxBuffer.Size;
    if (koth) {
        crown(dl, f, 320, 24, 30);
        text(dl, f, g_title, 22, 320, 38, GOLD, "KING OF THE HILL", CENTER, 1.4f);
    } else if (v.type != 2 && tex(mode_logo(v.type)).srv) {    // (3-Way shares Free for All's logo: in words)
        image_fit(dl, f, tex(mode_logo(v.type)), 200, 12, 240, 46);
    } else {
        text(dl, f, g_stencil, 30, 320, 20, CREAM, upper(mode_name(v.type)).c_str(), CENTER, 1.6f);
    }
    if (v.room[0]) text(dl, f, g_hand, 15, 44, 26, ORANGE, v.room, LEFT, 0.9f, 150);
    if (v.you < 0) {    // spectator mode: this PC watches the match
        rect(dl, f, 44, 46, text_width(f, g_title, 11, "SPECTATING") + 14, 18, IM_COL32(20, 70, 150, 220), 5);
        text(dl, f, g_title, 11, 51, 48, WHITE, "SPECTATING", LEFT, 0.8f);
    }
    {
        NetServerInfo sv[NETSERVER_COUNT];
        int cur = connected_server(), ms = gallery_on() ? (cur >= 0 ? G.servers[cur].ping_ms : -1) : netplay_rtt();
        if (gallery_on()) { for (int i = 0; i < NETSERVER_COUNT; i++) sv[i] = G.servers[i]; }
        else netservers_get(sv, NETSERVER_COUNT);
        std::string where = cur >= 0 ? upper(sv[cur].name) : "ONLINE";
        char line[64];
        if (ms >= 0) snprintf(line, sizeof(line), "%s  %d MS", where.c_str(), ms);
        else snprintf(line, sizeof(line), "%s", where.c_str());
        float lw = text_width(f, g_title, 11, line);
        text(dl, f, g_title, 11, 596, 28, ms >= 0 ? ping_colour(ms) : GREY, line, RIGHT, 0.8f);
        ping_bars(dl, f, 596 - lw - 24, 28, 10, ms);
    }
    shift_fade(dl, f, k, 0, -12 * (1 - ease_out(t / 0.35f)), ease_out(t / 0.35f));

    // the fighters
    int order[4] = { 0, 1, 2, 3 };
    if (n == 2 && v.king == 0) std::swap(order[0], order[1]);     // the king on the right, as on the hill
    if (n == 2) {
        const float w = 196, ph = 214, y = 70;
        for (int s = 0; s < 2; s++) {
            k = dl->VtxBuffer.Size;
            versus_card(dl, f, v, order[s], s == 0 ? 36 : 640 - 36 - w, y, w, ph, true);
            float e = ease_out((t - 0.05f - 0.08f * s) / 0.45f);
            shift_fade(dl, f, k, (s == 0 ? -70.0f : 70.0f) * (1 - e), 0, e);
        }
        k = dl->VtxBuffer.Size;
        float e = ease_out((t - 0.30f) / 0.30f);
        versus_mark(dl, f, 320, 128, 64, 1.0f + 0.35f * (1 - e));
        shift_fade(dl, f, k, 0, 0, e);
        k = dl->VtxBuffer.Size;
        versus_venue(dl, f, v.venue, 246, 186, 148, 116, false);
        e = ease_out((t - 0.45f) / 0.40f);
        shift_fade(dl, f, k, 0, 16 * (1 - e), e);
    } else {
        // Team (2 vs 2): fighters 0 and 1 against 2 and 3, the sides apart with one VS between them
        bool team = n == 4 && is_team(v.type);
        const float w = n == 3 ? 172 : team ? 128 : 136, gap = n == 3 ? 18 : team ? 8 : 12, ph = n == 3 ? 168 : 150, y = 72;
        const float split = team ? 56 : 0;
        float x0 = 320 - (n * w + (n - 1) * gap + split) / 2;
        auto card_x = [&](int s) { return x0 + s * (w + gap) + (team && s >= 2 ? split : 0); };
        for (int s = 0; s < n; s++) {
            k = dl->VtxBuffer.Size;
            versus_card(dl, f, v, order[s], card_x(s), y, w, ph, false);
            float e = ease_out((t - 0.05f - 0.07f * s) / 0.45f);
            shift_fade(dl, f, k, 0, 40 * (1 - e), e);
        }
        k = dl->VtxBuffer.Size;
        float e = ease_out((t - 0.40f) / 0.30f);
        if (team) {
            for (int side = 0; side < 2; side++) {
                float bx = card_x(2 * side), bw = 2 * w + gap;
                rect(dl, f, bx, y + ph + 82, bw, 16, side ? IM_COL32(150, 14, 30, 210) : IM_COL32(20, 70, 150, 210), 5);
                text(dl, f, g_title, 12, bx + bw / 2, y + ph + 83, WHITE, side ? "TEAM 2" : "TEAM 1", CENTER, 1.0f);
            }
            versus_mark(dl, f, 320, y + ph * 0.55f, 40, 1.0f + 0.35f * (1 - e));
        } else {
            for (int s = 0; s + 1 < n; s++)     // a small VS between each pair of cards
                versus_mark(dl, f, x0 + (s + 1) * (w + gap) - gap / 2, y + ph * 0.55f, 26, 1.0f + 0.35f * (1 - e));
        }
        shift_fade(dl, f, k, 0, 0, e);
        k = dl->VtxBuffer.Size;
        versus_venue(dl, f, v.venue, 150, y + ph + (team ? 104 : 92), 340, 64, true);
        e = ease_out((t - 0.50f) / 0.40f);
        shift_fade(dl, f, k, 0, 16 * (1 - e), e);
    }

    // the load: what this PC waits for (no flashing: the dots step slowly)
    k = dl->VtxBuffer.Size;
    glass(dl, f, 36, 418, 568, 30, 8);
    static const char *const dots[] = { "", ".", "..", "..." };
    std::string status = v.loaded ? std::string("WAITING FOR THE OTHER PLAYERS") : std::string("LOADING THE FIGHT");
    status += dots[(v.ms / 450) % 4];
    text(dl, f, g_title, 13, 50, 425, WHITE, status.c_str(), LEFT, 1.0f);
    const char *sub = !v.loaded && v.ms > 8000 ? "The first match after the game starts can take up to half a minute."
                                               : "The fight starts by itself.";
    text(dl, f, g_hand, 13, 590, 426, ORANGE, sub, RIGHT, 0.8f);
    shift_fade(dl, f, k, 0, 0, ease_out((t - 0.2f) / 0.4f));

    ranked_versus(dl, f, v, std::max(0.0f, std::min(1.0f, ease_out((t - 0.2f) / 0.4f))));   // a ranked set: ranks, score
    shift_fade(dl, f, all, 0, 0, v.alpha);     // the fade as the fight starts
}

// The gallery's versus samples: "versus-1v1", "versus-3way", "versus-ffa", "versus-team", "versus-koth".
NetVersus sample_versus(const std::string &page)
{
    static const char *const names[] = { "Ann", "Ben", "Cal", "Dee" };
    const auto &list = roster();
    NetVersus v = {};
    v.count = page == "versus-3way" ? 3 : page == "versus-ffa" || page == "versus-team" ? 4 : 2;
    v.type = page == "versus-3way" ? 2 : page == "versus-ffa" ? 3 : page == "versus-team" ? 1 : 0;
    // The Foundation and Red Hook Tire Co. (no subtitle: the photo alone, the name in words in a row),
    // The Subway and The Terror Dome (their subtitle banners)
    v.venue = page == "versus-koth" ? 6 : page == "versus-ffa" ? 11 : page == "versus-3way" ? 24 : 1;
    v.you = 0;
    v.king = -1;
    snprintf(v.room, sizeof(v.room), "%s", page == "versus-koth" ? "Hill of Pain" : "Ann's room");
    for (int i = 0; i < v.count; i++) {
        v.fighter[i] = list.empty() ? 55 : list[(i * 7 + 3) % list.size()].id;
        v.cpu[i] = page == "versus-ffa" && i == 3;
        if (!v.cpu[i]) snprintf(v.name[i], sizeof(v.name[i]), "%s", names[i]);
        v.ping[i] = v.cpu[i] ? -1 : 38 + 41 * i;
    }
    if (page == "versus-koth") {
        v.room_mode = NETPLAY_MODE_KOTH_RANDOM;
        v.king = 1;
        v.streak = 3;
    }
    if (page == "versus-created" || page == "versus-created-long") {     // Ann with her created fighter against Ben's
        for (int i = 0; i < 2; i++) {
            const char *st = "";
            v.fighter[i] = NETPLAY_CREATED_FIGHTER;
            G.cp_slots = 3;
            cp_slot(i, v.cp_name[i], sizeof(v.cp_name[i]), &st);
            snprintf(v.cp_style[i], sizeof(v.cp_style[i]), "%s", st);
        }
    }
    v.alpha = 1;
    v.loaded = 0;
    return v;
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

#include "ranked_ui.inc"
#include "social_ui.inc"

// Which page the gallery shows now, and the sample it shows with it.
void gallery_step()
{
    static const char *const pages[] = { "connecting", "offline", "hub", "hub-signin", "hub-voice", "hub-team",
                                         "hub-special", "rooms", "rooms-special", "code",
                                         "create", "create-3", "create-6", "create-1", "create-koth",
                                         "create-koth-3", "create-koth-6", "create-koth-1",
                                         // Team (2 vs 2) and the special matches
                                         "create-team", "create-special", "create-subway", "room-team", "room-team-3",
                                         "room-special", "vote-team",
                                         "ranked", "voice", "stats", "stats-1v1", "players", "fighters",
                                         // build 47: every server's players (All servers)
                                         "players-all",
                                         // build 33: the grid at Eric Sermon, O.E, Santos and Teck
                                         "fighters-91", "fighters-92", "fighters-93", "fighters-85", "room", "vote", "hill",
                                         "up", "watch", "room-voice", "match",
                                         // build 30: the server choice
                                         "connecting-auto", "hub-server", "servers", "servers-offline", "hub-ranked-na",
                                         "room-ping", "offline-server",
                                         // build 31: both servers (build 38: all three)
                                         "hub-both", "servers-both", "rooms-both", "rooms-both-na", "room-both-move",
                                         // build 38: the staged quick-match search
                                         "room-quick-near", "room-quick-wide",
                                         // the versus screen while a match loads
                                         "versus-1v1", "versus-3way", "versus-ffa", "versus-team", "versus-koth",
                                         // build 37: created fighters
                                         "grid-created", "room-created", "versus-created",
                                         // several created fighters, the second one picked
                                         "grid-created-pick",
                                         // build 39: online names of 16 characters, and their entry
                                         "name-created", "name-created-wide", "grid-created-long", "room-created-long",
                                         "versus-created-long",
                                         // ranked phase 2a: the mode chooser, the search, ACCEPT, the set, results
                                         "ranked-modes", "ranked-search", "ranked-accept", "ranked-room", "ranked-vote",
                                         "ranked-versus", "ranked-results", "ranked-results-placement", "stats-ranked",
                                         // ranked phase 2b: Draft
                                         "ranked-draft-ban", "ranked-draft-venue", "ranked-draft-blind",
                                         "ranked-draft-pick", "ranked-draft-reveal", "stats-ranked-draft",
                                         // build 46: back from a match while another player is on its results
                                         "room-results", "room-results-ffa",
                                         // build 46: each player's rollback in the room
                                         "room-rollback", "room-rollback-restart", "room-rollback-all",
                                         // build 46: the server allows rollback again (the notice)
                                         "hub-rollback-restart" };
    const int count = (int)(sizeof(pages) / sizeof(pages[0]));
    static unsigned long long start;
    static int ms;
    unsigned long long now = GetTickCount64();
    if (!start) {
        start = now;
        ms = atoi(getenv("RECOMP_TEST_ONLINE_GALLERY"));
        const auto &list = roster();
        static const char *const room_names[] = { "Ann's room", "Uptown Brawl", "Hill of Pain", "Late night FFA", "Dee's rules",
                                                  "Burn it down" };
        static const int room_modes[] = { 0, 0, NETPLAY_MODE_KOTH_RANDOM, 2, 1, 7 };
        for (int i = 0; i < 6; i++) {
            NetRoomInfo info = {};
            snprintf(info.code, sizeof(info.code), "ROOM%02d", i);
            snprintf(info.name, sizeof(info.name), "%s", room_names[i]);
            info.mode = room_modes[i];
            info.max = NETPLAY_IS_KOTH(info.mode) ? 8 : i == 3 || i == 4 ? 4 : 2;
            info.players = i == 1 ? 2 : i == 5 ? 1 : 1 + i % 3;
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
                 "favorite=%d\nfavorite_name=%s\nranked=1520|10\nteam=12|8|4|0\nffa=9|3|5|1\n"
                 "recent=win|Ann|Ben|0\nrecent=loss|Ann|Cal|0\nrecent=win|Ann|Dee|0\n"
                 "board=1|Kaze|88|20|0\nboard=2|Rex|71|33|0\nboard=3|Nia|64|12|0\nboard=4|Jay|58|41|0\n"
                 "board=5|Sol|51|27|0\nboard=6|Oz|47|30|0\nboard=7|Vic|40|22|0\nboard=8|Mo|38|19|0\n"
                 "board=9|Tia|31|25|0\nboard=10|Ann|27|14|0\nboard_page=0\nboard_pages=4\n",
                 main ? main->id : -1, main ? main->name : "");
        G.stats = buf;
        // All servers: US East's and US West's rooms as a player in Europe sees them (112 and 168 ms)
        static const char *const na_names[] = { "Brooklyn bruisers", "Quick: Rex", "West coast hill" };
        static const int na_modes[] = { 0, 0, NETPLAY_MODE_KOTH_RANDOM };
        for (int i = 0; i < 3; i++) {
            NetServerRoom e = {};
            snprintf(e.room.code, sizeof(e.room.code), "NAROM%d", i);
            snprintf(e.room.name, sizeof(e.room.name), "%s", na_names[i]);
            e.room.mode = na_modes[i];
            e.room.max = NETPLAY_IS_KOTH(e.room.mode) ? 8 : 2;
            e.room.players = i == 2 ? 4 : 1;
            e.server = i == 2 ? NETSERVER_WEST : NETSERVER_NA;
            e.ping_ms = i == 2 ? 168 : 112;
            e.age_s = 40 + i * 15;
            G.other_rooms.push_back(e);
        }
        // All servers: US East's and US West's players (build 47)
        static const char *const far_who[] = { "Brooklyn Bruiser", "Kingpin", "Rex", "Dmx Fan", "Queens", "NightOwl",
                                               "Cali", "Oak", "Tre" };
        for (int i = 0; i < (int)(sizeof(far_who) / sizeof(far_who[0])); i++) {
            NetServerPlayer e = {};
            snprintf(e.player.name, sizeof(e.player.name), "%s", far_who[i]);
            e.player.state = i % 3;
            e.server = i < 6 ? NETSERVER_NA : NETSERVER_WEST;
            G.other_players.push_back(e);
        }
    }
    // RECOMP_TEST_ONLINE_GALLERY_ONLY=<prefix>: only the pages whose names start with it ("versus")
    static std::vector<int> shown;
    if (shown.empty()) {
        const char *only = getenv("RECOMP_TEST_ONLINE_GALLERY_ONLY");
        for (int i = 0; i < count; i++)
            if (!only || !*only || !strncmp(pages[i], only, strlen(only))) shown.push_back(i);
        if (shown.empty()) for (int i = 0; i < count; i++) shown.push_back(i);
    }
    int index = shown[(size_t)((now - start) / (unsigned long long)ms) % shown.size()];
    const char *page = pages[index];
    G.page_ms = (int)((now - start) % (unsigned long long)ms);
    G.page = page;
    G.state = NETPLAY_LOBBY;
    G.in_lobby = true;
    G.room = NetRoom();
    G.queue = NETQUEUE_SEARCHING;
    G.account = ACCOUNT_IN;
    S.watch_hidden = 0;
    S.note_until = 0;
    S.settings_edit = false;
    {   // the relays as a player in Europe sees them, the hub on Europe
        NetServerInfo eu = {}, na = {}, west = {};
        eu.id = "eu"; eu.name = "Europe"; eu.short_name = "EU"; eu.state = NETSERVER_UP; eu.ping_ms = 38;
        eu.online = 142; eu.rooms = 12; eu.waiting = 2; eu.idle = 5; eu.flags = NETSERVER_RANKED | NETSERVER_KOTH; eu.version = 1;
        na.id = "na"; na.name = "US East"; na.short_name = "US EAST"; na.state = NETSERVER_UP; na.ping_ms = 112;
        na.online = 12; na.rooms = 3; na.waiting = 3; na.idle = 1; na.flags = NETSERVER_KOTH; na.version = 1;
        west.id = "west"; west.name = "US West"; west.short_name = "US WEST"; west.state = NETSERVER_UP; west.ping_ms = 168;
        west.online = 4; west.rooms = 1; west.waiting = 0; west.idle = 2; west.flags = NETSERVER_KOTH; west.version = 1;
        G.servers[NETSERVER_EU] = eu;
        G.servers[NETSERVER_NA] = na;
        G.servers[NETSERVER_WEST] = west;
        G.server_cur = NETSERVER_EU;
        G.choice = NETCHOICE_AUTO;
        G.ranked = false;
        G.pending = false;
        for (int i = 0; i < NETPLAY_ROOM_SLOTS; i++) G.room_pings[i] = -1;
        S.quick_room[0] = 0;
        S.alone_since = 0;
        S.quick_since = 0;
        S.last_quick = 0;
    }
    G.cp_slots = 0;
    G.cp_pick = 0;
    G.results_wait = false;
    G.results_slots = 0;
    G.rollback = false;
    for (int i = 0; i < NETPLAY_ROOM_SLOTS; i++) G.room_rb[i] = -1;
    std::string p = page;
    // the "-long" pages: Ann's first card plays as the widest 16 characters, Ben's as "Brooklyn Bruiser"
    G.cp_long = p.size() > 5 && p.compare(p.size() - 5, 5, "-long") == 0;
    for (auto &o : G.cp_online) o.clear();
    if (G.cp_long) { G.cp_online[0] = "WWWWWWWWWWWWWWWW"; G.cp_online[1] = "Brooklyn Bruiser"; }
    {   // the ranked pages add their ranks to the sample statistics
        static const std::string base_stats = G.stats;
        G.stats = base_stats;
        ranked_stats_tab() = 0;
        if (ranked_gallery(p)) return;
    }
    if (p == "connecting") { G.state = NETPLAY_CONNECTING; G.in_lobby = false; }
    else if (p == "offline") { G.state = NETPLAY_FAILED; G.in_lobby = false; }
    else if (p == "hub") { S.page = HUB; S.hub_sel = 0; S.quick_mode = 0; }
    else if (p == "hub-signin") { S.page = HUB; S.hub_sel = 3; G.account = ACCOUNT_OUT; }
    else if (p == "hub-voice") { S.page = HUB; S.hub_sel = HUB_VOICE; }
    else if (p == "hub-rollback-restart") { S.page = HUB; S.hub_sel = 0; S.quick_mode = 0; }   // build 46: the restart notice
    else if (p == "hub-team" || p == "hub-special") {     // QUICK MATCH with Team, or Inferno, chosen
        S.page = HUB;
        S.hub_sel = 0;
        std::vector<int> cycle = mode_cycle();
        S.quick_mode = cycle_index(cycle, p == "hub-team" ? 1 : 7);
    }
    else if (p == "rooms" || p == "rooms-special") { S.page = ROOMS; S.rooms_sel = p == "rooms" ? 1 : 5; }
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
        // Team (every venue but the one-on-one ones); a special match on its own venue
        if (p == "create-team") { S.settings.mode = 1; S.settings.max = 4; apply_preset(S.settings, 0); }
        if (p == "create-special" || p == "create-subway") {
            S.settings.mode = p == "create-subway" ? 8 : 7;
            S.settings.max = 2;
            apply_preset(S.settings, 0);
        }
        // the venue row of a special match, which cannot be changed
        S.settings_sel = p == "create-special" ? 2 : 0;
    }
    else if (p == "voice") { S.page = VOICE; S.voice_sel = 1; }
    else if (p == "stats" || p == "stats-1v1") { S.page = STATS; S.stats_page = 0; }
    else if (p == "players") S.page = PLAYERS;
    else if (p == "players-all") { S.page = PLAYERS; G.choice = NETCHOICE_ALL; }
    else if (p == "fighters") { S.page = FIGHTERS; S.fighter_sel = 9; S.fighter_top = 0; }
    else if (p.rfind("fighters-", 0) == 0) {    // the grid scrolled to one fighter (by Battle-mode ID)
        const auto &list = roster();
        int id = atoi(p.c_str() + 9);
        S.page = FIGHTERS;
        S.fighter_sel = 0;
        S.fighter_top = 0;
        for (size_t i = 0; i < list.size(); i++) if (list[i].id == id) S.fighter_sel = (int)i;
    }
    else if (p == "room") { S.page = HUB; G.room = sample_room(2, 4, 3); }
    else if (p == "name-created" || p == "name-created-wide") {     // the first card's online name: half typed, or too wide
        G.cp_slots = 1;
        G.room = sample_room(0, 2, 2);
        S.page = NAME; S.name_cp = 0;
        snprintf(S.name, sizeof(S.name), "%s", p == "name-created" ? "Brooklyn Bruis" : "WWWWWWWWWWWWWWWW");
        S.name_pos = (int)strlen(S.name);
    }
    else if (p == "grid-created-long") {    // the grid: the first card with a 16-character online name
        G.cp_slots = 1;
        G.room = sample_room(0, 2, 2);
        S.page = FIGHTERS; S.fighter_sel = 0; S.fighter_top = 0;
    }
    else if (p == "room-created-long") {    // One on One: both created fighters with 16-character names
        G.cp_slots = 3;
        S.page = HUB;
        G.room = sample_room(0, 2, 2);
        G.room.players[0].fighter = G.room.players[1].fighter = NETPLAY_CREATED_FIGHTER;
    }
    else if (p == "grid-created") {         // the grid with this player's created fighter first
        G.cp_slots = 1;
        G.room = sample_room(0, 2, 2);
        S.page = FIGHTERS; S.fighter_sel = 0; S.fighter_top = 0;
    }
    else if (p == "grid-created-pick") {    // three created fighters, the second one picked and remembered
        G.cp_slots = 1;
        G.cp_pick = 1;
        G.room = sample_room(0, 2, 2);
        S.page = FIGHTERS; S.fighter_top = 0;
        S.fighter_sel = grid_pos(grid_fighters(), NETPLAY_CREATED_FIGHTER);
    }
    else if (p == "room-created") {         // One on One: Ann with her created fighter, Ben with his
        G.cp_slots = 3;
        S.page = HUB;
        G.room = sample_room(0, 2, 2);
        G.room.players[0].fighter = G.room.players[1].fighter = NETPLAY_CREATED_FIGHTER;
    }
    else if (p == "room-team") { S.page = HUB; G.room = sample_room(1, 4, 4); }
    else if (p == "room-team-3") {      // three in, the one in room slot 2 gone: its card waits
        S.page = HUB;
        G.room = sample_room(1, 4, 4);
        G.room.players[2] = G.room.players[3];
        G.room.player_count = 3;
    }
    else if (p == "room-special") { S.page = HUB; G.room = sample_room(10, 2, 2); }
    else if (p == "room-rollback" || p == "room-rollback-restart" || p == "room-rollback-all") {
        // build 46: who rolls back. Free for All of three: Ann and Cal have it on, Ben off (or Ben's game
        // needs a restart for it); -all: everyone on, the match rolls back
        S.page = HUB;
        G.room = sample_room(3, 4, 3);
        G.rollback = true;
        G.room_rb[0] = G.room_rb[2] = 1;
        G.room_rb[1] = p == "room-rollback-all" ? 1 : p == "room-rollback-restart" ? 2 : 0;
        for (int i = 0; i < 3; i++) G.room_pings[i] = 30 + 12 * i;
    }
    else if (p == "room-results" || p == "room-results-ffa") {
        // build 46: back from the match while the relay still plays it (Ben, and in the FFA also Cal,
        // still on the results): no READY offered, the wait said
        bool ffa = p == "room-results-ffa";
        S.page = HUB;
        G.room = ffa ? sample_room(3, 4, 3) : sample_room(0, 2, 2);
        G.room.phase = 2;
        for (int i = 0; i < G.room.player_count; i++) G.room.players[i].ready = 1;
        G.results_wait = true;
        G.results_slots = ffa ? 6u : 2u;
    }
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
    } else if (p == "vote-team") {
        S.page = HUB;
        G.room = sample_room(1, 4, 4);
        G.room.phase = 1;
        G.room.candidate_count = 3;
        G.room.candidates[0] = 4; G.room.candidates[1] = 10; G.room.candidates[2] = 18;
        G.room.votes[1] = 2; G.room.votes[2] = 1;
        G.room.players[0].voted = 1;
        G.room.ms_left = 11000;
        S.vote_sel = 1;
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
    else if (p == "match") { G.state = NETPLAY_CONNECTED; G.room = sample_room(0, 2, 2); G.room.phase = 2; G.versus.count = 0; }
    else if (p.compare(0, 6, "versus") == 0) {
        G.state = NETPLAY_CONNECTED;
        G.room = sample_room(0, 2, 2);
        G.room.phase = 2;
        G.versus = sample_versus(p);
        G.versus.ms = G.page_ms;
    }
    else if (p == "connecting-auto") {
        S.page = HUB; G.state = NETPLAY_OFF; G.in_lobby = false; G.pending = true;
        G.servers[NETSERVER_NA].state = NETSERVER_UNKNOWN; G.servers[NETSERVER_NA].ping_ms = -1;
    } else if (p == "hub-server") {     // a player on the US east coast, on US East
        S.page = HUB; S.hub_sel = 0; S.quick_mode = 0;
        G.servers[NETSERVER_EU].ping_ms = 104; G.servers[NETSERVER_NA].ping_ms = 31; G.servers[NETSERVER_WEST].ping_ms = 72;
        G.server_cur = NETSERVER_NA;
    } else if (p == "servers") {
        S.page = SERVERS; S.servers_sel = 2; G.server_cur = NETSERVER_EU; G.choice = NETCHOICE_AUTO;
    } else if (p == "servers-offline") {
        S.page = SERVERS; S.servers_sel = 1; G.server_cur = NETSERVER_EU; G.choice = NETCHOICE_EU;
        G.servers[NETSERVER_WEST].state = NETSERVER_OFFLINE; G.servers[NETSERVER_WEST].ping_ms = -1;
    } else if (p == "hub-ranked-na") {
        S.page = HUB; S.hub_sel = HUB_RANKED; G.ranked = true; G.server_cur = NETSERVER_NA;
    } else if (p == "room-ping") {      // one player far away: the high-ping warning
        S.page = HUB; G.room = sample_room(2, 4, 3);
        G.room_pings[0] = 38; G.room_pings[1] = 96; G.room_pings[2] = 263;
    } else if (p == "room-quick-near" || p == "room-quick-wide") {
        // build 38's staged search: a player on US East (Automatic) alone in a quick room for 12 s (US
        // West too) or 31 s (every server)
        S.page = HUB; G.room = sample_room(0, 2, 1);
        G.room.players[0].ready = 1;
        G.room_pings[0] = 31;
        G.servers[NETSERVER_EU].ping_ms = 104; G.servers[NETSERVER_NA].ping_ms = 31; G.servers[NETSERVER_WEST].ping_ms = 72;
        G.server_cur = NETSERVER_NA;
        snprintf(S.quick_room, sizeof(S.quick_room), "%s", G.room.code);
        S.alone_since = S.quick_since = now - (p == "room-quick-near" ? 12000 : 31000);
    } else if (p == "offline-server") { S.page = HUB; G.state = NETPLAY_FAILED; G.in_lobby = false; }
    else if (p == "hub-both") {         // Both servers: the chip says so, the hub is on Europe
        S.page = HUB; S.hub_sel = 0; S.quick_mode = 0; G.choice = NETCHOICE_ALL;
    } else if (p == "servers-both") {
        S.page = SERVERS; S.servers_sel = 1 + NETSERVER_COUNT; G.choice = NETCHOICE_ALL;
    } else if (p == "rooms-both" || p == "rooms-both-na") {     // the merged list; a room on the other server chosen
        S.page = ROOMS; G.choice = NETCHOICE_ALL; S.rooms_sel = p == "rooms-both" ? 1 : (int)G.rooms.size();
    } else if (p == "room-both-move") {     // alone 31 s in a quick room; a player waits on US East
        S.page = HUB; G.choice = NETCHOICE_ALL; G.room = sample_room(0, 2, 1);
        G.room.players[0].ready = 1;
        G.room_pings[0] = 38;
        snprintf(S.quick_room, sizeof(S.quick_room), "%s", G.room.code);
        S.alone_since = S.quick_since = now - 31000;
        S.note = "Nobody here yet: moving to US East (112 ms), where 1 player is waiting for One on One.";
        S.note_until = now + 60000;
    }
}

// The look of the page draw() shows (the same choices, in the same order).
LookId page_look(const NetRoom &room, enum NetplayState st)
{
    if (st == NETPLAY_CONNECTED && net_in_lobby()) return LOOK_MATCH;
    if (st == NETPLAY_CONNECTING || st == NETPLAY_FAILED || st == NETPLAY_OFF || !net_in_lobby()) return LOOK_OFFLINE;
    switch (S.page) {
    case FIGHTERS: case NAME: return LOOK_FIGHTERS;
    case SETTINGS: case CODE: return LOOK_LIST;
    case SERVERS: if (!room.in_room) return LOOK_LIST; break;
    case PLAYERS: if (!room.in_room) return LOOK_PLAYERS; break;
    case STATS: if (!room.in_room) return LOOK_STATS; break;
    case RANKED: case RANKED_MODES: if (!room.in_room) return LOOK_SEARCH; break;
    case RANKED_RESULTS: return LOOK_SEARCH;
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

// The versus screen with the online screens closed: read once a frame, drawn by draw_versus.
NetVersus g_versus;
bool g_versus_up = false;

bool versus_wanted()
{
    g_versus_up = !gallery_on() && netplay_versus(&g_versus) != 0;
    return g_versus_up;
}

void draw_versus(ImDrawList *dl, ImVec2 display)
{
    if (g_versus_up) versus_page(dl, display, g_versus);
}

const char *shot_label()
{
    if (const char *chat = chat_shot_label()) return chat;
    return gallery_on() ? G.page : g_versus_up ? "versus" : nullptr;
}

bool take_open_request()
{
    if (!g_open_req.exchange(0)) return false;
    S = State();
    S.opened = GetTickCount64();
    C.open = C.kb = C.menu = false;
    if (!gallery_on()) {
        connect_if_needed();
        social_start();     // the chat's threads: from the first time the online screens open, never at boot
    }
    return true;
}

bool draw_pages(ImVec2 display);

// The online pages, and over them the chat window (social_ui.inc) while it is open: then the pages
// run as usual but take no input.
bool draw(ImVec2 display)
{
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    Frame f = menu_frame(display);
    // a match starting closes the chat: the versus screen comes, then the fight
    if (net_state() == NETPLAY_CONNECTED && net_in_lobby()) C.open = C.kb = C.menu = false;
    chat_begin();
    bool keep = draw_pages(display);
    if (!keep || g_closed_for_match) {
        C.open = C.kb = C.menu = false;
        C.mouse_off = false;
        social_set_typing(0);
        return keep;
    }
    if (!(net_state() == NETPLAY_CONNECTED && net_in_lobby())) chat_draw(dl, f, display);
    C.mouse_off = false;
    return keep;
}

bool draw_pages(ImVec2 display)
{
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    Frame f = menu_frame(display);
    ImGui::GetIO().KeyRepeatDelay = 0.45f;     // a scripted or quick press moves one step
    Pad pad = read_pad();
    // the first frame takes no choice: the A that opened the screens is still down
    if (GetTickCount64() - S.opened < 250) pad = Pad();
    if (C.open) pad = Pad();       // the chat window has the input
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

    // A match was agreed: the versus screen, then the menu goes back to the game, which starts it.
    // The versus screen stays up with the online screens closed (draw_versus) until the fight is
    // under way; closing them here gives the controller back to the game as before, after 1.8 s,
    // or at once when this PC's fight is already set up (a quick load).
    if (st == NETPLAY_CONNECTED && net_in_lobby()) {
        if (!S.start_seen) S.start_seen = GetTickCount64();
        NetVersus v;
        bool have = gallery_on() ? G.versus.count > 0 : netplay_versus(&v) != 0;
        if (gallery_on()) v = G.versus;
        if (have) {
            versus_page(dl, display, v);
        } else {
            backdrop(dl, f, display, true, LOOK_MATCH);
            page_title(dl, f, "MATCH FOUND");
            glass(dl, f, 170, 176, 300, 100, 14);
            text(dl, f, g_hand, 22, 320, 200, ORANGE, "Get ready to fight!", CENTER, 1.2f);
            text(dl, f, g_title, 14, 320, 240, WHITE, "The match starts by itself.", CENTER);
        }
        // (the gallery keeps the page up: it is there to be looked at)
        g_closed_for_match = !gallery_on() && (GetTickCount64() - S.start_seen >= 1800 || (have && v.loaded));
        return !g_closed_for_match;
    }
    backdrop(dl, f, display, S.page == FIGHTERS || S.page == NAME || S.page == STATS || S.page == PLAYERS || room.in_room ||
             st == NETPLAY_CONNECTED ||
             !g_menu_up.load(), page_look(room, st));
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
        } else if (moved == 3) {
            say("On the set's server: joining the set...");
        } else if (moved < 0) {
            say("The other server did not answer in time. Try again.", 6000);
        }
        if (st == NETPLAY_LOBBY) S.fallback_tried = false;
        ranked_tick(room);
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
        glass(dl, f, 36, 104, 290, pending ? 96 : 76);
        std::string head = pending ? "Finding the nearest server..." :
                           to >= 0 ? std::string("Connecting to ") + sv[to].name + "..." : "Connecting...";
        text(dl, f, g_hand, 20, 54, 116, ORANGE, head.c_str(), LEFT, 1.0f, 265);
        if (pending) {      // the answers so far
            for (int i = 0; i < NETSERVER_COUNT; i++) {
                char line[64];
                snprintf(line, sizeof(line), sv[i].state == NETSERVER_UP ? "%s %d MS" : sv[i].state == NETSERVER_OFFLINE ?
                         "%s OFFLINE" : "%s ...", upper(sv[i].short_name).c_str(), sv[i].ping_ms);
                float x = 56, y = 144 + i * 16.0f;
                ping_bars(dl, f, x, y + 1, 11, sv[i].state == NETSERVER_UP ? sv[i].ping_ms : -1);
                text(dl, f, g_title, 12, x + 24, y, sv[i].state == NETSERVER_UP ? ping_colour(sv[i].ping_ms) : GREY, line, LEFT, 1.0f);
            }
        }
        door_panel(dl, f);
        door_text(dl, f, &tex("ui/online_icon.png"), "ONLINE", pending ? "CHOOSING A SERVER" : "CONNECTING",
                  pending ? pad_text("Europe, US East or US West: the one with the lowest ping. Change it any time with "
                                     "{x} in the online menu.").c_str()
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
        // (not when the relay refused a second session from this PC: another relay would only hide it)
        if (!gallery_on() && st == NETPLAY_FAILED && !S.fallback_tried && !netplay_online_twice() &&
            (server_choice() == NETCHOICE_AUTO || server_choice() == NETCHOICE_ALL)) {
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
                  pad_text("Check the internet connection, then press {a} to try again, or {x} to pick another "
                           "server.").c_str());
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
                ranked_search(fighter);
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
                say(fighter_label(fighter) +
                    (room.in_room && NETPLAY_IS_KOTH(room.settings.mode) ? " it is, from your next fight." : " it is."));
                S.page = HUB;
            }
            S.ranked_pick = false;
        }
        if (S.page == HUB) S.ranked_pick = false;
        if (S.page == FIGHTERS) {
            float hx = 54;
            hx += hint(dl, f, hx, 420, "a", "PICK");
            if (S.fighters_cp >= 0) hx += hint(dl, f, hx, 420, "y", "ONLINE NAME");     // a created fighter's card
            hint(dl, f, hx, 420, "b", "BACK");
        }
    } else if (S.page == NAME) {
        draw_name(dl, f, pad);
        float hx = 54;
        hx += hint(dl, f, hx, 392, "a", "SAVE");
        hx += hint(dl, f, hx, 392, "x", "CLEAR");
        hint(dl, f, hx, 392, "b", "BACK");
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
        if (ranked_shown()) hx += hint(dl, f, hx, 420, "y", ranked_stats_tab() + 1 < 1 + NETRANK_MODES ? "NEXT TAB" : "CASUAL");
        hint(dl, f, hx, 420, "b", "BACK");
        if (pad.b) S.page = HUB;
    } else if (S.page == RANKED && !room.in_room) {
        draw_ranked(dl, f, pad);
    } else if (S.page == RANKED_MODES && !room.in_room) {
        draw_ranked_modes(dl, f, pad);
    } else if (S.page == RANKED_RESULTS) {
        draw_ranked_results(dl, f, pad);
    } else if (S.page == VOICE && !(room.in_room && room.phase != 0)) {
        draw_voice(dl, f, pad, room);
        float hx = 54;
        hx += hint(dl, f, hx, 420, "b", "BACK");
        (void)hx;
    } else if (S.page == CODE) {
        draw_code(dl, f, pad);
        float hx = 54;
        hx += hint(dl, f, hx, 392, "a", "JOIN");
        if (can_watch()) hx += hint(dl, f, hx, 392, "rb", "WATCH");
        hint(dl, f, hx, 392, "b", "BACK");
    } else if (room.in_room) {
        if (S.page == RANKED) S.page = HUB;     // matched: leaving the room comes back to the menu
        draw_room(dl, f, pad, room);
    } else if (S.page == ROOMS) {
        draw_rooms(dl, f, pad);
        float hx = 54;
        hx += hint(dl, f, hx, 420, "a", "JOIN");
        if (can_watch()) hx += hint(dl, f, hx, 420, "rb", "WATCH");
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
    {   // build 46: the server allows rollback again, but this session started without it (once per session)
        static bool told;
        int caps = lobby_caps();
        if (!told && !gallery_on() && net_state() == NETPLAY_LOBBY && caps > 0 && (caps & NETPLAY_CAP_ROLLBACK) &&
            rollback_restart_hint()) {
            told = true;
            say("Rollback is allowed again: restart the game to use it.", 9000);
        }
        if (gallery_on() && G.page && !strcmp(G.page, "hub-rollback-restart") && !S.note_until)
            say("Rollback is allowed again: restart the game to use it.", 9000);
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
bool versus_wanted() { return false; }
void draw_versus(ImDrawList *, ImVec2) {}
const char *shot_label() { return nullptr; }
}

#endif
