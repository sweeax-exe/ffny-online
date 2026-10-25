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

// Behind the lists: the game's own main menu, which keeps running under the overlay (wall, lamp,
// the door), with its poster wall and title covered and its door's text hidden by the shutter.
// The room, the fighter choice and the vote use the whole picture instead (`full`).
void backdrop(ImDrawList *dl, const Frame &f, ImVec2 display, bool full)
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

enum Page { HUB, ROOMS, SETTINGS, FIGHTERS, CODE, STATS, PLAYERS, RANKED, VOICE };

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
} S;

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

void say(const std::string &s) { S.note = s; S.note_until = GetTickCount64() + 3500; }

const char *mode_name(int type)
{
    if (NETPLAY_IS_KOTH(type)) return "King of the Hill";
    const OnlineMode *m = online_mode(type);
    return m ? m->name : "Match";
}

// The relay answered CAPS with King of the Hill (an older relay never offers it).
bool koth_offered()
{
    int caps = netplay_lobby_caps();
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


// The fighters that have a portrait, by name.
const std::vector<OnlineFighter> &roster()
{
    static std::vector<OnlineFighter> list;
    static bool built = false;
    if (!built) {
        built = true;
        // The fighters with a select portrait in the game. The few without one (Santos, Teck, O.E,
        // Eric Sermon) are not Battle-mode fighters: a match with them crashes at the load (build 13
        // offered them; players reported it).
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

void default_settings(NetRoomSettings &s)
{
    memset(&s, 0, sizeof(s));
    std::string player = setting_text("name", "Player");
    snprintf(s.name, sizeof(s.name), "%s's room", player.empty() ? "Player" : player.c_str());
    s.is_public = 1;
    s.mode = 0;
    s.max = 2;
    apply_preset(s, 0);
}

void connect_if_needed()
{
    enum NetplayState st = netplay_state();
    if (netplay_in_lobby() && st != NETPLAY_OFF && st != NETPLAY_FAILED) return;
    std::string server = setting_text("server", "play.defjam.online"), name = setting_text("name", "Player");
    char discord[32];
    account_init();
    account_refresh();
    if (account_game_name(discord, sizeof(discord))) name = discord;    // signed in: the Discord name
    netplay_lobby(server.c_str(), name.c_str());
}

// ---- hub ----

const int HUB_ITEMS = 8, HUB_LEAVE = 7, HUB_RANKED = 1, HUB_VOICE = 6;

// RANKED MATCH waits until ranked play is switched on on the server (docs/ranked-design.md);
// RECOMP_RANKED=1 shows it for tests.
bool ranked_shown()
{
    static int on = -1;
    if (on < 0) on = getenv("RECOMP_RANKED") != nullptr;
    return on != 0;
}

// "142 PLAYERS ONLINE - 12 ROOMS" at the bottom right, once the relay has answered a WHO.
void online_count(ImDrawList *dl, const Frame &f)
{
    int online = -1, rooms = 0;
    netplay_lobby_players(nullptr, 0, &online, &rooms);
    if (online < 0) return;
    char line[64];
    snprintf(line, sizeof(line), "%d PLAYER%s ONLINE  -  %d ROOM%s", online, online == 1 ? "" : "S", rooms,
             rooms == 1 ? "" : "S");
    text(dl, f, g_hand, 14, 606, 430, ORANGE, line, RIGHT, 1.0f);    // beside the status line, clear of the menu's hints
}

// ---- players online ----

void draw_players(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    static NetPlayer list[40];
    int online = -1, rooms = 0;
    int n = netplay_lobby_players(list, 40, &online, &rooms);
    if (pad.x) netplay_lobby_who();
    page_title(dl, f, "PLAYERS ONLINE");
    online_count(dl, f);
    if (n < 0) {
        text(dl, f, g_title, 20, 320, 200, CREAM, "ASKING THE SERVER...", CENTER, 1.4f);
        return;
    }
    dl->AddRectFilled(f.p(40, 84), f.p(606, 404), IM_COL32(0, 0, 0, 150));
    static const char *const states[] = { "IN THE MENU", "IN A ROOM", "FIGHTING" };
    const ImU32 colours[] = { CREAM, ORANGE, GREEN };
    for (int i = 0; i < n; i++) {
        float x = i < 20 ? 52 : 330, y = 94 + (i % 20) * 15.5f;
        int st = list[i].state >= 0 && list[i].state <= 2 ? list[i].state : 0;
        text(dl, f, g_title, 13, x, y, WHITE, list[i].name, LEFT, 1.1f);
        text(dl, f, g_title, 10, x + 262, y + 2, colours[st], states[st], RIGHT, 1.0f);
    }
    if (n == 0) text(dl, f, g_title, 14, 320, 200, GREY, "Nobody else with this version right now.", CENTER, 1.0f);
    if (online > n)
        text(dl, f, g_title, 10, 592, 392, GREY, "and others on other versions or not shown", RIGHT, 1.0f);
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
    online_count(dl, f);
    // Online fights need a Discord sign-in: matches against signed-out players did not count, about
    // half of them after the build-23 rules (2026-10-24). Y signs in from here (the browser opens).
    enum AccountState acc = account_state();
    bool signed_in = acc == ACCOUNT_IN;
    if (!signed_in) {
        const char *line = acc == ACCOUNT_WAITING ? "FINISH SIGNING IN WITH DISCORD IN YOUR BROWSER..."
                         : acc == ACCOUNT_CHECKING ? "CHECKING YOUR DISCORD SIGN-IN..."
                         : "SIGN IN WITH DISCORD TO FIGHT ONLINE";
        text(dl, f, g_title, 14, 50, 60, ORANGE, line, LEFT, 1.2f);
        if (acc == ACCOUNT_OUT || acc == ACCOUNT_FAILED) hint(dl, f, 50 + text_width(f, g_title, 14, line) + 14, 59, "y", "SIGN IN");
        if (pad.y && (acc == ACCOUNT_OUT || acc == ACCOUNT_FAILED)) {
            account_sign_in();
            say("Your browser opens: sign in with Discord, then come back.");
        }
    }
    for (int k = 0; k < count; k++) {
        int i = shown[k];
        float x = 50, y = count > 6 ? 84 + k * 45 : 88 + k * 53, w = 262, h = count > 6 ? 42 : 46;
        if (mouse_in(f, x, y, w, h)) S.hub_sel = i;
        bool sel = S.hub_sel == i;
        if (sel) selector(dl, f, x, y, w, h);
        text(dl, f, g_title, 23, x + 16, y + 3, sel ? CREAM : with_alpha(GREY, 0.85f), items[i], LEFT, 1.6f);
        text(dl, f, g_hand, 12, x + 18, y + 29, sel ? ORANGE : with_alpha(ORANGE, 0.45f), subs[i], LEFT, 1.0f);
        if (mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
    }
    // the door: what the chosen item does
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
        if (needs_account && !signed_in) {
            say(acc == ACCOUNT_WAITING ? "Finish signing in with Discord in your browser first."
                                       : "Sign in with Discord first: press Y.");
        } else switch (S.hub_sel) {
        case 0:
            if (mode_count) {
                int type = quick_modes[S.quick_mode];
                const OnlineMode *qm = online_mode(type);
                // King of the Hill: the fullest open hill, or a new one with room for eight
                netplay_lobby_quick(type, NETPLAY_IS_KOTH(type) ? 8 : qm ? std::min(4, qm->fighters) : 2);
                say("Looking for a room...");
            }
            break;
        case 1:
            if (account_state() != ACCOUNT_IN) { say("Sign in with Discord in the launcher to play ranked."); break; }
            S.ranked_pick = true;
            S.page = FIGHTERS;
            {
                int fighter = recomp_settings_get("online", "fighter", 55);
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
    if (!account_stats(buf, sizeof(buf))) return false;
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
    enum AccountState acc = account_state();
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
        text(dl, f, g_title, 22, 320, 200, CREAM, account_stats_busy() ? "LOADING..." : "NO STATS YET", CENTER, 1.6f);
        if (!account_stats_busy())
            text(dl, f, g_hand, 14, 320, 236, ORANGE, "PRESS X TO TRY AGAIN", CENTER, 1.0f);
        return;
    }
    char num[32];
    // left: the main fighter and the record
    if (v.favorite < 0 || !portrait(dl, f, v.favorite, 46, 84, 110, 165))
        dl->AddRectFilled(f.p(46, 84), f.p(156, 249), IM_COL32(20, 20, 20, 200));
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
    big_number(dl, f, 78, 262, num, "WINS", GREEN);
    snprintf(num, sizeof(num), "%d", v.losses);
    big_number(dl, f, 160, 262, num, "LOSSES", IM_COL32(235, 90, 80, 255));
    snprintf(num, sizeof(num), "%d%%", v.win_rate);
    big_number(dl, f, 248, 262, num, "WIN RATE", CREAM);
    if (v.ranked_games)
        snprintf(num, sizeof(num), "STREAK %d   BEST %d   RANKED %d", v.streak, v.best, v.ranked_rating);
    else
        snprintf(num, sizeof(num), "STREAK %d   BEST %d", v.streak, v.best);
    text(dl, f, g_title, 13, 46, 324, WHITE, num, LEFT, 1.1f);
    // last fights
    text(dl, f, g_hand, 13, 46, 344, ORANGE, "LAST FIGHTS", LEFT, 1.0f);
    if (v.recent.empty()) text(dl, f, g_title, 12, 46, 362, GREY, "Play online while signed in.", LEFT, 1.0f);
    for (size_t i = 0; i < v.recent.size() && i < 3; i++) {
        const auto &r = v.recent[i];
        if (r.size() < 4) continue;
        bool win = r[0] == "win", draw = r[0] == "draw";
        std::string line = r[1] + " vs " + (r[2].empty() ? "?" : r[2]);
        float y = 360 + i * 18;
        text(dl, f, g_title, 13, 46, y, win ? GREEN : draw ? GREY : IM_COL32(235, 90, 80, 255), win ? "W" : draw ? "D" : "L",
             LEFT, 1.1f);
        text(dl, f, g_title, 12, 64, y + 1, CREAM, line.c_str(), LEFT, 1.0f, 250);
    }
    // right: the leaderboard, ten places a page (left/right or LB/RB turn the page)
    if (!account_stats_busy()) {
        int want = v.board_page;
        if ((pad.left || pad.lb) && v.board_page > 0) want = v.board_page - 1;
        if ((pad.right || pad.rb) && v.board_page + 1 < v.board_pages) want = v.board_page + 1;
        if (want != v.board_page) { S.stats_page = want; account_fetch_stats_page(want); }
    }
    dl->AddRectFilled(f.p(330, 84), f.p(606, 404), IM_COL32(0, 0, 0, 150));
    text(dl, f, g_title, 20, 344, 92, ORANGE, "LEADERBOARD", LEFT, 1.4f);
    if (v.board_pages > 1) {
        char pg[32];
        snprintf(pg, sizeof(pg), "%s  %d / %d  %s", v.board_page > 0 ? "<" : " ", v.board_page + 1, v.board_pages,
                 v.board_page + 1 < v.board_pages ? ">" : " ");
        text(dl, f, g_title, 14, 468, 386, account_stats_busy() ? GREY : CREAM, pg, CENTER, 1.1f);
    }
    text(dl, f, g_title, 10, 592, 100, GREY, "MOST WINS", RIGHT, 1.0f);
    if (v.board.empty())
        text(dl, f, g_title, 12, 344, 130, GREY, "Nobody yet. Three online matches signed in put you on it.", LEFT,
             1.0f, 250);
    for (size_t i = 0; i < v.board.size() && i < 10; i++) {
        const auto &e = v.board[i];
        if (e.size() < 5) continue;
        float y = 124 + i * 27;
        bool me = e[1] == v.name;
        if (me) dl->AddRectFilled(f.p(334, y - 3), f.p(602, y + 21), IM_COL32(255, 168, 26, 60));
        ImU32 col = e[0] == "1" ? IM_COL32(255, 205, 60, 255) : me ? CREAM : WHITE;
        text(dl, f, g_title, 16, 362, y, col, e[0].c_str(), RIGHT, 1.2f);
        text(dl, f, g_title, 15, 372, y + 1, col, e[1].c_str(), LEFT, 1.2f);
        std::string wl = e[2] + "-" + e[3];
        text(dl, f, g_title, 14, 592, y + 2, me ? CREAM : GREY, wl.c_str(), RIGHT, 1.1f);
    }
}

// ---- room list ----

void draw_rooms(ImDrawList *dl, const Frame &f, const Pad &pad)
{
    NetRoomInfo rooms[32];
    unsigned version;
    int n = netplay_lobby_rooms(rooms, 32, &version);
    unsigned long long now = GetTickCount64();
    if (now - S.last_list > 3000) { netplay_lobby_list(0xFF); S.last_list = now; }
    if (pad.up && n) S.rooms_sel = (S.rooms_sel + n - 1) % n;
    if (pad.down && n) S.rooms_sel = (S.rooms_sel + 1) % n;
    if (S.rooms_sel >= n) S.rooms_sel = n ? n - 1 : 0;
    if (pad.x) { netplay_lobby_list(0xFF); S.last_list = now; }
    if (pad.y) { S.page = CODE; S.code_pos = 0; return; }

    page_title(dl, f, "FIND A ROOM");
    if (!n) {
        text(dl, f, g_title, 16, 60, 130, GREY, "No open rooms right now.");
        text(dl, f, g_hand, 14, 60, 156, ORANGE, "Quick match, or open one yourself.", LEFT, 0.8f);
    }
    int top = std::max(0, std::min(S.rooms_sel - 2, n - 6));
    for (int i = top; i < n && i < top + 6; i++) {
        float x = 50, y = 86 + (i - top) * 52, w = 262, h = 44;
        if (mouse_in(f, x, y, w, h)) S.rooms_sel = i;
        bool sel = i == S.rooms_sel;
        // a King of the Hill room takes players in any phase: they join the back of the line
        bool open = (rooms[i].phase == 0 || NETPLAY_IS_KOTH(rooms[i].mode)) && rooms[i].players < rooms[i].max;
        if (sel) selector(dl, f, x, y, w, h);
        text(dl, f, g_title, 19, x + 12, y + 3, sel ? CREAM : with_alpha(GREY, 0.9f), rooms[i].name, LEFT, 1.2f);
        char line[64];
        snprintf(line, sizeof(line), "%s  %d/%d", mode_name(rooms[i].mode), rooms[i].players, rooms[i].max);
        text(dl, f, g_hand, 13, x + 14, y + 26, open ? ORANGE : with_alpha(ORANGE, 0.45f), line, LEFT, 0.8f);
        if (mouse_click(f, x, y, w, h)) const_cast<Pad &>(pad).a = true;
    }
    if (n) {   // the door: the chosen room
        const NetRoomInfo &r = rooms[S.rooms_sel];
        bool open = (r.phase == 0 || NETPLAY_IS_KOTH(r.mode)) && r.players < r.max;
        char players[96];
        snprintf(players, sizeof(players), "%d of %d players. %s", r.players, r.max,
                 NETPLAY_IS_KOTH(r.mode) ? (open ? "Winner stays on. Press A to join the line." : "Full.") :
                 r.phase == 1 ? "Voting on the venue right now." : r.phase == 2 ? "A match is on." :
                 open ? "Open: press A to join." : "Full.");
        image_fit(dl, f, tex(mode_logo(r.mode)), 383, 150, 160, 34);
        door_text(dl, f, nullptr, r.name, mode_name(r.mode), players);
    }
    if (pad.a && n) {
        const NetRoomInfo &r = rooms[S.rooms_sel];
        if ((r.phase == 0 || NETPLAY_IS_KOTH(r.mode)) && r.players < r.max) { netplay_lobby_join(r.code); say("Joining..."); }
        else say("That room is not open right now.");
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
    door_text(dl, f, nullptr, "JOIN BY CODE", "SIX CHARACTERS",
              "Up and down change a character, left and right move. Or type it. A joins the room.");
    for (int i = 0; i < 6; i++) {
        float x = 70 + i * 46, y = 150;
        rect(dl, f, x, y, 40, 56, IM_COL32(15, 15, 15, 220));
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
        rect(dl, f, x, y, w, h, sel ? IM_COL32(25, 22, 18, 235) : IM_COL32(15, 15, 15, 170));
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
    // the door: the mode's logo (King of the Hill: its rules) and a few of the venues
    if (koth) {
        text(dl, f, g_title, 24, 463, 96, CREAM, "KING OF THE HILL", CENTER, 1.6f);
        text(dl, f, g_hand, 13, 463, 126, ORANGE, "WINNER STAYS ON", CENTER, 1.0f);
        text(dl, f, g_body, 13, 372, 146, WHITE,
             "TWO FIGHT ONE ON ONE, THE OTHERS WAIT IN LINE. THE LOSER GOES TO THE BACK. "
             "WHEN YOU'RE CALLED, PRESS A WITHIN 20 SECONDS.", LEFT, 1.0f, 182);
    } else {
        image_fit(dl, f, tex(mode_logo(s.mode)), 372, 96, 170, 36);
    }
    for (int k = 0; k < s.pool_count && k < 6; k++) {
        float vx = 380 + (k % 2) * 80, vy = (koth ? 236 : 150) + (k / 2) * (koth ? 56 : 80);
        image_fit(dl, f, venue_tex(s.pool[k]), vx, vy, koth ? 52 : 74, koth ? 52 : 74);
    }
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
    rect(dl, f, 40, 78, 172, 300, IM_COL32(10, 10, 10, 200));
    if (!portrait(dl, f, cur.id, 46, 84, 160, 240))
        text(dl, f, g_title, 26, 126, 180, with_alpha(CREAM, 0.6f), cur.name, CENTER, 1.4f, 150);
    text(dl, f, g_hand, 22, 126, 330, ORANGE, cur.name, CENTER, 1.2f);
    text(dl, f, g_title, 13, 126, 356, WHITE, cur.style, CENTER);
    // the grid
    float gx = 228, gy = 78, cw = 52, ch = 76;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            int i = (S.fighter_top + r) * cols + c;
            if (i >= n) break;
            float x = gx + c * (cw + 4), y = gy + r * (ch + 6);
            if (mouse_in(f, x, y, cw, ch)) S.fighter_sel = i;
            bool sel = i == S.fighter_sel;
            rect(dl, f, x, y, cw, ch, IM_COL32(12, 12, 12, 220));
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
    int st = netplay_lobby_queue(&waited, &searching, why, sizeof(why));
    int fighter = recomp_settings_get("online", "fighter", 55);
    page_title(dl, f, "RANKED MATCH");
    online_count(dl, f);
    rect(dl, f, 40, 78, 172, 300, IM_COL32(10, 10, 10, 200));
    if (!portrait(dl, f, fighter, 46, 84, 160, 240))
        text(dl, f, g_title, 26, 126, 180, with_alpha(CREAM, 0.6f), online_fighter_name(fighter), CENTER, 1.4f, 150);
    text(dl, f, g_hand, 22, 126, 330, ORANGE, online_fighter_name(fighter), CENTER, 1.2f);

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
        rect(dl, f, 240, 148, 340, 4, IM_COL32(40, 40, 40, 255));
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
    rect(dl, f, 50, 70, 540, 5, IM_COL32(40, 40, 40, 255));
    rect(dl, f, 50, 70, 540 * (left / 15000.0f), 5, ORANGE);
    float w = 170, gap = 15, x0 = 320 - (n * w + (n - 1) * gap) / 2;
    for (int i = 0; i < n; i++) {
        float x = x0 + i * (w + gap), y = 100;
        if (mouse_in(f, x, y, w, 290)) S.vote_sel = i;
        bool sel = i == S.vote_sel, mine = me && me->voted == i;
        rect(dl, f, x, y, w, 290, mine ? IM_COL32(60, 8, 14, 235) : IM_COL32(12, 12, 12, 220));
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
    return slot == r.you ? recomp_settings_get("online", "fighter", 55) : -1;
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
        rect(dl, f, x, y, w, ph + 64, IM_COL32(12, 12, 12, 220));
        int fighter = slot >= 0 ? slot_fighter(r, slot) : -1;
        if (slot >= 0 && h.watch_seed && h.watch_seed == h.seed)      // the fight's own pick, from WATCH
            fighter = h.watch_slots[0] == slot ? h.watch_fighters[0] : h.watch_slots[1] == slot ? h.watch_fighters[1] : fighter;
        if (fighter < 0 || !portrait(dl, f, fighter, x + 5, y + 5, w - 10, ph - 10))
            text(dl, f, g_title, 40, x + w / 2, y + 80, GREY, "?", CENTER);
        if (slot >= 0 && slot == h.king) {
            crown(dl, f, x + w / 2, y - 2, 44);
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
    rect(dl, f, 40, 384, 560, 24, IM_COL32(0, 0, 0, 170));
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
    dl->AddRectFilled(f.p(0, 0), f.p(640, 480), IM_COL32(0, 0, 0, 215));
    rect(dl, f, 40, 70, 172, 300, IM_COL32(10, 10, 10, 220));
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
    text(dl, f, g_title, 64, 600, 206, v.ms_left < 6000 ? RED : CREAM, secs, RIGHT, 2.0f);
    rect(dl, f, 242, 290, 358, 6, IM_COL32(40, 40, 40, 255));
    rect(dl, f, 242, 290, 358 * std::min(1.0f, v.ms_left / 20000.0f), 6, v.ms_left < 6000 ? RED : ORANGE);
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
    dl->AddRectFilled(f.p(40, 80), f.p(330, 384), IM_COL32(0, 0, 0, 160));
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
        if (up) rect(dl, f, x, y, w, rh, e.state == NETLINE_FIGHTING ? IM_COL32(70, 10, 18, 230) : IM_COL32(60, 40, 8, 230), 2);
        else if (me) rect(dl, f, x, y, w, rh, IM_COL32(255, 168, 26, 40), 2);
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
    float kx = 412, ky = 96;
    if (h.valid && h.king >= 0) {
        int kf = slot_fighter(r, h.king);
        rect(dl, f, kx, ky, 102, 152, IM_COL32(12, 12, 12, 220));
        if (kf < 0 || !portrait(dl, f, kf, kx + 3, ky + 3, 96, 146)) text(dl, f, g_title, 34, kx + 51, ky + 50, GREY, "?", CENTER);
        crown(dl, f, kx + 51, ky - 4, 40);
        text(dl, f, g_hand, 20, 463, 254, h.king == r.you ? ORANGE : CREAM, slot_name(r, h.king), CENTER, 1.2f);
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
    // this PC's place
    if (v.my_state == NETLINE_AWAY)
        text(dl, f, g_hand, 16, 320, 366, RED, "YOU ARE AWAY: PRESS A TO GET BACK IN THE LINE", CENTER, 0.8f);
    else if (v.next == r.you)
        text(dl, f, g_hand, 18, 185, 388 - 22, ORANGE, "YOU'RE UP NEXT", CENTER, 1.2f);

    float hx = 54, hy = 392;
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
    int fighter = recomp_settings_get("online", "fighter", 55);
    if (S.room_version != r.version) S.room_version = r.version;

    if (r.phase == 1) { draw_vote(dl, f, pad, r); return; }

    page_title(dl, f, r.settings.name);
    image_fit(dl, f, tex(mode_logo(r.settings.mode)), 360, 22, 220, 30);
    char code[32];
    snprintf(code, sizeof(code), "CODE  %s", r.code);
    text(dl, f, g_title, 14, 590, 58, ORANGE, code, RIGHT);

    // the players' cards
    int places = std::max(r.settings.max, r.player_count);
    float w = 128, gap = 10, x0 = 320 - (places * w + (places - 1) * gap) / 2;
    for (int i = 0; i < places; i++) {
        float x = x0 + i * (w + gap), y = 86;
        rect(dl, f, x, y, w, 270, IM_COL32(12, 12, 12, 215));
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
        if (p.ready) {
            rect(dl, f, x + 14, y + 240, w - 28, 22, with_alpha(RED, 0.9f), 2);
            text(dl, f, g_title, 15, x + w / 2, y + 242, WHITE, "READY", CENTER, 0.8f);
        }
    }
    // hints
    bool ready = me && me->ready;
    float hx = 54, hy = 392;
    if (r.player_count < 2) text(dl, f, g_hand, 15, 320, 366, ORANGE, "Waiting for another player...", CENTER, 0.8f);
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
    // a full King of the Hill room has nine others: the rows get closer together
    float row_step = rows > 9 ? 300.0f / rows : 39, row_h = row_step - 5;
    for (int i = 0; i < rows; i++) {
        float x = 50, y = 80 + i * row_step, w = 270, h = row_h;
        if (mouse_in(f, x, y, w, h)) S.voice_sel = i;
        bool sel = S.voice_sel == i;
        rect(dl, f, x, y, w, h, sel ? IM_COL32(25, 22, 18, 235) : IM_COL32(15, 15, 15, 170));
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
    door_text(dl, f, &tex("ui/online_icon.png"), "VOICE CHAT", "TALK IN ROOMS AND FIGHTS",
              "Everyone in your room hears you. Hold the key, or the controller button you pick here, to talk. "
              "Turn a player to 0 to mute them.");
    // the microphone's level, under the rows
    float meter = voice_mic_meter(), my = 80 + rows * row_step + 6;
    if (meter >= 0.0f) {
        text(dl, f, g_title, 10, 52, my, GREY, meter > 0.95f ? "YOUR MICROPHONE: TOO LOUD" : "YOUR MICROPHONE");
        rect(dl, f, 52, my + 14, 266, 9, IM_COL32(40, 40, 40, 255));
        rect(dl, f, 52, my + 14, 266 * meter, 9, meter > 0.95f ? RED : GREEN);
    } else {
        text(dl, f, g_title, 10, 52, my, GREY, r.in_room ? "Press the talk key once: your microphone's level shows here."
                                                       : "Your microphone's level shows here in a room.");
    }
    if ((pad.a && S.voice_sel == rows - 1) || pad.b) S.page = HUB;
}

void draw_status(ImDrawList *dl, const Frame &f)
{
    char status[256];
    netplay_status(status, sizeof(status));
    text(dl, f, g_body, 11, 54, 452, GREY, status, LEFT, 0.6f, 270);
    NetRoom r;
    netplay_lobby_room(&r);
    unsigned long long now = GetTickCount64();
    const char *note = nullptr;
    if (r.error[0]) note = r.error;
    else if (now < S.note_until) note = S.note.c_str();
    if (note) text(dl, f, g_hand, 14, 54, 428, r.error[0] ? RED : ORANGE, note, LEFT, 0.8f, 270);
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

bool take_open_request()
{
    if (!g_open_req.exchange(0)) return false;
    S = State();
    S.opened = GetTickCount64();
    connect_if_needed();
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

    NetRoom room;
    netplay_lobby_room(&room);
    enum NetplayState st = netplay_state();
    // Over any screen but the main menu (after a match: the battle menu) every page is full-screen.
    {   // who is online: asked every five seconds while the online screens are up
        static unsigned long long asked;
        unsigned long long now = GetTickCount64();
        if (netplay_in_lobby() && now - asked > 5000) { asked = now; netplay_lobby_who(); }
    }
    backdrop(dl, f, display, S.page == FIGHTERS || S.page == STATS || S.page == PLAYERS || room.in_room ||
             st == NETPLAY_CONNECTED ||
             !g_menu_up.load());

    // a match was agreed: show it, then give the menu back to the game, which starts it
    if (st == NETPLAY_CONNECTED && netplay_in_lobby()) {
        if (!S.start_seen) S.start_seen = GetTickCount64();
        page_title(dl, f, "MATCH FOUND");
        text(dl, f, g_hand, 22, 320, 200, ORANGE, "Get ready to fight!", CENTER, 1.2f);
        text(dl, f, g_title, 14, 320, 240, WHITE, "The match starts by itself.", CENTER);
        g_closed_for_match = GetTickCount64() - S.start_seen >= 1800;
        return !g_closed_for_match;
    }
    S.start_seen = 0;
    g_closed_for_match = false;

    if (st == NETPLAY_CONNECTING || (!netplay_in_lobby() && st != NETPLAY_FAILED)) {
        page_title(dl, f, "ONLINE");
        text(dl, f, g_hand, 20, 54, 120, ORANGE, "Connecting...", LEFT, 1.0f);
        door_text(dl, f, &tex("ui/online_icon.png"), "ONLINE", "CONNECTING", "To the FFNY Online server.");
        hint(dl, f, 54, 392, "b", "BACK");
        return !pad.b;
    }
    if (st == NETPLAY_FAILED || st == NETPLAY_OFF) {
        char status[256];
        netplay_status(status, sizeof(status));
        page_title(dl, f, "ONLINE");
        text(dl, f, g_hand, 20, 54, 110, RED, "No connection", LEFT, 1.0f);
        text(dl, f, g_title, 14, 54, 145, WHITE, status, LEFT, 0.8f, 265);
        door_text(dl, f, &tex("ui/online_icon.png"), "OFFLINE", "NO CONNECTION",
                  "Check the internet connection, then press A to try again.");
        float hx = 54;
        hx += hint(dl, f, hx, 392, "a", "TRY AGAIN");
        hint(dl, f, hx, 392, "b", "BACK");
        if (pad.a) connect_if_needed();
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
        draw_hub(dl, f, pad);      // the menu's own "A SELECT  B BACK" stays visible above
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
}

#endif
