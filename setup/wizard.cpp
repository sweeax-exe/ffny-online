// The setup wizard: one window, drawn with Direct2D and DirectWrite in FFNY Online's own look (no
// game artwork: the setup stays game-free). Three steps (your game, folder, install) and the end
// screen. The work is the engine's (setup/engine.py): it identifies the chosen file (--identify),
// looks for the player's copies (--find-games), checks the folder (--plan-folder) and installs;
// this window only asks, shows its answers and follows the install's status files.
#include "bootstrap.h"
#include <d2d1.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace {

// ---- Small JSON reader for the engine's answers (json.dumps output: ASCII, \u escapes) ----------
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false; double number = 0; std::wstring text;
    std::vector<Json> items; std::vector<std::pair<std::string, Json>> fields;
    const Json& operator[](const char* key) const {
        static const Json none;
        for (const auto& f : fields) if (f.first == key) return f.second;
        return none;
    }
    std::wstring str() const { return type == String ? text : std::wstring(); }
    bool yes() const { return type == Bool ? boolean : false; }
    double num() const { return type == Number ? number : 0; }
};

struct JsonParser {
    const char* p; const char* end;
    void ws() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    bool lit(const char* word) { size_t n = strlen(word); if ((size_t)(end - p) >= n && !strncmp(p, word, n)) { p += n; return true; } return false; }
    unsigned hex4() {
        unsigned v = 0;
        for (int i = 0; i < 4 && p < end; ++i, ++p) v = v * 16 + (unsigned)(isdigit((unsigned char)*p) ? *p - '0' : (tolower(*p) - 'a' + 10));
        return v;
    }
    std::wstring string() {
        std::wstring out; std::string raw;
        ++p;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                if (!raw.empty()) { out += utf8(raw); raw.clear(); }
                char c = p[1]; p += 2;
                switch (c) {
                    case 'n': out += L'\n'; break; case 't': out += L'\t'; break; case 'r': out += L'\r'; break;
                    case 'b': out += L'\b'; break; case 'f': out += L'\f'; break;
                    case 'u': out += (wchar_t)hex4(); break;
                    default: out += (wchar_t)c;
                }
            } else raw += *p++;
        }
        if (!raw.empty()) out += utf8(raw);
        ++p;
        return out;
    }
    Json value() {
        Json v; ws();
        if (p >= end) return v;
        if (*p == '{') {
            v.type = Json::Object; ++p; ws();
            while (p < end && *p != '}') {
                ws(); auto key = narrow(string()); ws(); if (p < end && *p == ':') ++p;
                v.fields.emplace_back(key, value()); ws(); if (p < end && *p == ',') ++p; ws();
            }
            ++p;
        } else if (*p == '[') {
            v.type = Json::Array; ++p; ws();
            while (p < end && *p != ']') { v.items.push_back(value()); ws(); if (p < end && *p == ',') ++p; ws(); }
            ++p;
        } else if (*p == '"') { v.type = Json::String; v.text = string(); }
        else if (lit("true")) { v.type = Json::Bool; v.boolean = true; }
        else if (lit("false")) { v.type = Json::Bool; }
        else if (lit("null")) {}
        else { char* stop = nullptr; v.type = Json::Number; v.number = strtod(p, &stop); p = stop > p ? stop : p + 1; }
        return v;
    }
};

Json parseJson(const std::string& text) { JsonParser parser{text.data(), text.data() + text.size()}; return parser.value(); }

// ---- What the window shows ----------------------------------------------------------------------
enum Screen { S_GAME, S_IDENTITY, S_FOLDER, S_INSTALLING, S_DONE, S_FAILED, S_WINE };

struct Verdict {
    bool valid = false, ok = false;
    std::wstring kind, headline, message, advice, chosen, path, name, folder;
    double size = 0;
    static Verdict from(const Json& j) {
        Verdict v; v.valid = true; v.ok = j["ok"].yes(); v.kind = j["kind"].str(); v.headline = j["headline"].str();
        v.message = j["message"].str(); v.advice = j["advice"].str(); v.chosen = j["chosen"].str(); v.path = j["path"].str();
        v.name = j["name"].str(); v.folder = j["folder"].str(); v.size = j["size"].num();
        return v;
    }
};

struct Check { std::wstring id, text, fix; bool ok = false; };
struct Plan {
    bool valid = false, ok = false, adjusted = false, existing = false;
    std::wstring folder, installDir, dataDir;
    std::vector<Check> checks;
};

struct State {
    Screen screen = S_GAME;
    bool payloadReady = false, payloadFailed = false;
    std::wstring payloadError;
    bool searching = true;
    std::vector<Verdict> found;
    size_t foundIndex = 0;
    bool identifying = false;
    Verdict id;
    bool planning = false;
    Plan plan;
    bool hd = true, shortcut = true, prereq = true;
    // Installing
    int permille = 0;
    std::string stageKey;
    std::wstring statusText, estimate, failure;
    DWORD exitCode = 0;
    bool confirmStop = false, details = false, stopping = false;
    ULONGLONG buildStart = 0; int buildStartPermille = 0;
    bool update = false;
} st;

HWND window = nullptr, logView = nullptr;
HANDLE worker = nullptr;
HANDLE payloadEvent = nullptr;
int identityToken = 0, planToken = 0;
fs::path g_update_install, g_update_data;
DWORD g_wait_pid = 0;
bool preparationSmoke = false;
unsigned pollTicks = 0, preparationTicks = 0;
int smokeResult = 4;
bool notified = false;
unsigned long long logOffset = 0;
std::string logPending;
constexpr int LOG_VIEW_CHARS = 200000;
ULONGLONG startTick = 0;
UINT dpi = 96;

constexpr UINT WM_PAYLOAD = WM_APP + 10, WM_FOUND = WM_APP + 11, WM_IDENTITY = WM_APP + 12, WM_PLAN = WM_APP + 13,
               WM_SETUP_READY = WM_APP + 14, WM_SETUP_FAILED = WM_APP + 15, WM_TRAY = WM_APP + 16;
constexpr float W = 1100, H = 680;
const wchar_t* DISCORD = L"https://discord.gg/zCZKVDnKaP";
const wchar_t* DUMP_GUIDE = L"https://github.com/sweeax-exe/ffny-online/blob/main/docs/dump-your-disc.md";

struct Answer { int token; DWORD code; std::string text; };

// ---- Drawing ------------------------------------------------------------------------------------
ID2D1Factory* d2d = nullptr;
IDWriteFactory* dw = nullptr;
IDWriteFontCollection* antonFonts = nullptr;     // Anton from this exe's resources, when DirectWrite can load it
std::wstring bodyFamily = L"Segoe UI", antonFamily = L"Anton";
ID2D1HwndRenderTarget* windowTarget = nullptr;
bool windowDrawn = false;

struct Canvas { ID2D1RenderTarget* rt = nullptr; ID2D1SolidColorBrush* brush = nullptr; float t = 0; } cv;

D2D1_COLOR_F rgba(int r, int g, int b, float a = 1.f) { return D2D1::ColorF(r / 255.f, g / 255.f, b / 255.f, a); }
const D2D1_COLOR_F WHITE = rgba(255, 245, 230), RED = rgba(200, 16, 46), GOLD = rgba(255, 205, 60),
                   GREEN = rgba(123, 224, 123), ORANGE = rgba(255, 168, 26), BG = rgba(11, 7, 8);
D2D1_COLOR_F cream(float a) { return rgba(242, 230, 200, a); }
D2D1_RECT_F R(float x, float y, float w, float h) { return D2D1::RectF(x, y, x + w, y + h); }

ID2D1SolidColorBrush* paint(D2D1_COLOR_F c) { cv.brush->SetColor(c); return cv.brush; }

enum Family { ANTON, BODY };
struct Style { Family family; float size; DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL; float spacing = 0; float lineHeight = 0; };
std::map<std::tuple<int, int, int>, IDWriteTextFormat*> formats;

IDWriteTextFormat* format(const Style& s) {
    auto key = std::make_tuple((int)s.family, (int)(s.size * 10), (int)s.weight);
    auto it = formats.find(key);
    if (it != formats.end()) return it->second;
    IDWriteTextFormat* f = nullptr;
    bool anton = s.family == ANTON;
    dw->CreateTextFormat(anton ? antonFamily.c_str() : bodyFamily.c_str(), anton ? antonFonts : nullptr,
                         anton ? DWRITE_FONT_WEIGHT_NORMAL : s.weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                         s.size, L"en-us", &f);
    formats[key] = f;
    return f;
}

enum Align { LEFT, CENTER, RIGHT };
struct Span { std::wstring text; D2D1_COLOR_F color; int link = 0; bool bold = false; };

struct Hit { D2D1_RECT_F r; int id; int arg; };
std::vector<Hit> hits;
int hoverId = -1, hoverArg = 0, focusIndex = -1;

IDWriteTextLayout* layout(const std::wstring& text, const Style& s, float width, Align align = LEFT, bool oneLine = false) {
    IDWriteTextLayout* l = nullptr;
    dw->CreateTextLayout(text.c_str(), (UINT32)text.size(), format(s), std::max(1.f, width), 10000.f, &l);
    l->SetTextAlignment(align == CENTER ? DWRITE_TEXT_ALIGNMENT_CENTER : align == RIGHT ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    if (s.spacing) {
        IDWriteTextLayout1* l1 = nullptr;
        if (SUCCEEDED(l->QueryInterface(&l1))) { l1->SetCharacterSpacing(0, s.spacing * s.size, 0, {0, (UINT32)text.size()}); l1->Release(); }
    }
    if (s.lineHeight) l->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, s.size * s.lineHeight, s.size * s.lineHeight * 0.8f);
    if (oneLine) {
        l->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        IDWriteInlineObject* sign = nullptr;
        dw->CreateEllipsisTrimmingSign(format(s), &sign);
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        l->SetTrimming(&trimming, sign);
        if (sign) sign->Release();
    }
    return l;
}

D2D1_SIZE_F measure(const std::wstring& text, const Style& s, float width = 4000) {
    auto l = layout(text, s, width);
    DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m); l->Release();
    return {m.widthIncludingTrailingWhitespace, m.height};
}

float text(const std::wstring& s, const Style& st_, float x, float y, float w, D2D1_COLOR_F c, Align a = LEFT, bool oneLine = false) {
    auto l = layout(s, st_, w, a, oneLine);
    cv.rt->DrawTextLayout({x, y}, l, paint(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m); l->Release();
    return m.height;
}

// Text with coloured, underlined, clickable parts (links); returns its height.
float rich(const std::vector<Span>& spans, const Style& s, float x, float y, float w, Align a = LEFT) {
    std::wstring all;
    for (const auto& sp : spans) all += sp.text;
    auto l = layout(all, s, w, a);
    std::vector<ID2D1SolidColorBrush*> brushes;
    UINT32 at = 0;
    for (const auto& sp : spans) {
        DWRITE_TEXT_RANGE range{at, (UINT32)sp.text.size()};
        ID2D1SolidColorBrush* b = nullptr;
        auto color = sp.color;
        if (sp.link && hoverId == 1000 + sp.link) color = rgba(255, 224, 138);
        cv.rt->CreateSolidColorBrush(color, &b);
        brushes.push_back(b);
        l->SetDrawingEffect(b, range);
        if (sp.link) l->SetUnderline(TRUE, range);
        if (sp.bold) l->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
        at += range.length;
    }
    cv.rt->DrawTextLayout({x, y}, l, paint(cream(0.85f)));
    at = 0;
    for (const auto& sp : spans) {
        if (sp.link) {
            UINT32 count = 0;
            l->HitTestTextRange(at, (UINT32)sp.text.size(), x, y, nullptr, 0, &count);
            std::vector<DWRITE_HIT_TEST_METRICS> m(count);
            if (count) l->HitTestTextRange(at, (UINT32)sp.text.size(), x, y, m.data(), count, &count);
            for (const auto& r : m) hits.push_back({R(r.left, r.top, r.width, r.height), 1000 + sp.link, 0});
        }
        at += (UINT32)sp.text.size();
    }
    DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m); l->Release();
    for (auto b : brushes) if (b) b->Release();
    return m.height;
}

ID2D1Geometry* rounded(D2D1_RECT_F r, float radius) {
    ID2D1RoundedRectangleGeometry* g = nullptr;
    d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r, radius, radius), &g);
    return g;
}

void fillRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c) {
    cv.rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), paint(c));
}

D2D1_RECT_F grow(D2D1_RECT_F r, float d, float dy = 0) { return D2D1::RectF(r.left - d, r.top - d + dy, r.right + d, r.bottom + d + dy); }

// A soft shadow (or glow) outside a rounded box only, as CSS box-shadow draws it: stacked rings.
void softShadow(D2D1_RECT_F r, float radius, float blur, float dy, D2D1_COLOR_F c, float spread = 0) {
    int steps = 10;
    auto inner = rounded(r, radius);
    for (int i = steps; i >= 1; --i) {
        float d = spread + blur * i / steps;
        auto outer = rounded(grow(r, d, dy), radius + d);
        ID2D1Geometry* parts[] = {outer, inner};
        ID2D1GeometryGroup* ring = nullptr;
        d2d->CreateGeometryGroup(D2D1_FILL_MODE_ALTERNATE, parts, 2, &ring);
        float k = 1.f - (float)(i - 1) / steps;
        auto color = c; color.a = c.a * k * k * 0.32f;
        cv.rt->FillGeometry(ring, paint(color));
        ring->Release(); outer->Release();
    }
    inner->Release();
}

void glass(D2D1_RECT_F r, float radius = 18) {
    softShadow(r, radius, 50, 18, rgba(0, 0, 0, 0.55f));
    fillRound(r, radius, rgba(18, 12, 14, 0.62f));
    // The faint highlight along the top edge.
    cv.rt->PushAxisAlignedClip(D2D1::RectF(r.left, r.top, r.right, r.top + radius * 0.6f), D2D1_ANTIALIAS_MODE_ALIASED);
    cv.rt->DrawRoundedRectangle(D2D1::RoundedRect(grow(r, -0.5f), radius - 0.5f, radius - 0.5f), paint(rgba(255, 255, 255, 0.08f)), 1.f);
    cv.rt->PopAxisAlignedClip();
}

enum Kind { RED_BUTTON, GHOST, OFF };
const Style BUTTON{BODY, 16, DWRITE_FONT_WEIGHT_BOLD, 0.06f};

bool focused(int id, int arg) {
    if (focusIndex < 0) return false;
    int n = 0;
    for (const auto& h : hits) if (h.id >= 0) { if (n == focusIndex) return h.id == id && h.arg == arg; ++n; }
    return false;
}

float buttonWidth(const std::wstring& label, float size = 16, float pad = 26, float spacing = 0.06f) {
    Style s = BUTTON; s.size = size; s.spacing = spacing;
    return measure(label, s).width + pad * 2;
}

void button(int id, D2D1_RECT_F r, const std::wstring& label, Kind kind, float size = 16, float spacing = 0.06f, int arg = 0) {
    bool hover = hoverId == id && hoverArg == arg && kind != OFF;
    float radius = 12;
    if (kind == RED_BUTTON) {
        softShadow(r, radius, 24, 8, rgba(200, 16, 46, hover ? 0.6f : 0.45f));
        fillRound(r, radius, hover ? rgba(222, 28, 60) : RED);
        cv.rt->PushAxisAlignedClip(D2D1::RectF(r.left, r.top, r.right, r.top + 6), D2D1_ANTIALIAS_MODE_ALIASED);
        cv.rt->DrawRoundedRectangle(D2D1::RoundedRect(grow(r, -0.5f), radius, radius), paint(rgba(255, 255, 255, 0.25f)), 1.f);
        cv.rt->PopAxisAlignedClip();
    } else fillRound(r, radius, kind == GHOST ? rgba(255, 255, 255, hover ? 0.13f : 0.07f) : rgba(255, 255, 255, 0.05f));
    Style s = BUTTON; s.size = size; s.spacing = spacing;
    auto color = kind == RED_BUTTON ? WHITE : kind == GHOST ? cream(1) : cream(0.35f);
    auto m = measure(label, s);
    text(label, s, r.left, (r.top + r.bottom) / 2 - m.height / 2, r.right - r.left, color, CENTER);
    hits.push_back({r, kind == OFF ? -1 : id, arg});
    if (kind != OFF && focused(id, arg))
        cv.rt->DrawRoundedRectangle(D2D1::RoundedRect(grow(r, 3), radius + 3, radius + 3), paint(GOLD), 1.5f);
}

void link(int id, const std::wstring& label, float x, float y, float size = 14, Align a = LEFT, float w = 600) {
    Style s{BODY, size};
    rich({{label, GOLD, id - 1000}}, s, x, y, w, a);
}

ID2D1PathGeometry* path(std::initializer_list<D2D1_POINT_2F> points, bool closed) {
    ID2D1PathGeometry* g = nullptr; ID2D1GeometrySink* sink = nullptr;
    d2d->CreatePathGeometry(&g); g->Open(&sink);
    auto it = points.begin();
    sink->BeginFigure(*it, closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    for (++it; it != points.end(); ++it) sink->AddLine(*it);
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    sink->Close(); sink->Release();
    return g;
}

ID2D1StrokeStyle* roundStroke() {
    static ID2D1StrokeStyle* style = nullptr;
    if (!style) d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                   D2D1_LINE_JOIN_ROUND), nullptr, 0, &style);
    return style;
}

// Icons in a 24-unit box at (x, y) of size s, as the mockup's SVGs.
D2D1_POINT_2F P(float x, float y, float s, float u, float v) { return {x + u * s / 24, y + v * s / 24}; }

void checkIcon(float x, float y, float s, D2D1_COLOR_F c, float width) {
    auto g = path({P(x, y, s, 5, 12.5f), P(x, y, s, 9.5f, 17), P(x, y, s, 19, 7.5f)}, false);
    cv.rt->DrawGeometry(g, paint(c), width * s / 24, roundStroke()); g->Release();
}

void warningIcon(float x, float y, float s, D2D1_COLOR_F c) {
    auto g = path({P(x, y, s, 12, 3.2f), P(x, y, s, 21.6f, 19.6f), P(x, y, s, 2.4f, 19.6f)}, true);
    cv.rt->DrawGeometry(g, paint(c), 2.2f * s / 24, roundStroke()); g->Release();
    cv.rt->DrawLine(P(x, y, s, 12, 9), P(x, y, s, 12, 13.5f), paint(c), 2.2f * s / 24, roundStroke());
    cv.rt->FillEllipse(D2D1::Ellipse(P(x, y, s, 12, 16.6f), 1.3f * s / 24, 1.3f * s / 24), paint(c));
}

void discIcon(float x, float y, float s, D2D1_COLOR_F c) {
    cv.rt->DrawEllipse(D2D1::Ellipse(P(x, y, s, 12, 12), 9 * s / 24, 9 * s / 24), paint(c), 1.6f * s / 24);
    cv.rt->DrawEllipse(D2D1::Ellipse(P(x, y, s, 12, 12), 2.5f * s / 24, 2.5f * s / 24), paint(c), 1.6f * s / 24);
}

void folderIcon(float x, float y, float s, D2D1_COLOR_F c) {
    auto g = path({P(x, y, s, 3, 7), P(x, y, s, 3.6f, 5.6f), P(x, y, s, 5, 5), P(x, y, s, 9, 5), P(x, y, s, 11, 7),
                   P(x, y, s, 19, 7), P(x, y, s, 20.4f, 7.6f), P(x, y, s, 21, 9), P(x, y, s, 21, 17), P(x, y, s, 20.4f, 18.4f),
                   P(x, y, s, 19, 19), P(x, y, s, 5, 19), P(x, y, s, 3.6f, 18.4f), P(x, y, s, 3, 17)}, true);
    cv.rt->DrawGeometry(g, paint(c), 1.8f * s / 24, roundStroke()); g->Release();
}

void spinner(float cx, float cy, float r, D2D1_COLOR_F c, float width = 3) {
    float a0 = cv.t * 5.5f;
    ID2D1PathGeometry* g = nullptr; ID2D1GeometrySink* sink = nullptr;
    d2d->CreatePathGeometry(&g); g->Open(&sink);
    sink->BeginFigure({cx + r * cosf(a0), cy + r * sinf(a0)}, D2D1_FIGURE_BEGIN_HOLLOW);
    float a1 = a0 + 4.2f;
    sink->AddArc(D2D1::ArcSegment({cx + r * cosf(a1), cy + r * sinf(a1)}, {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
    sink->EndFigure(D2D1_FIGURE_END_OPEN); sink->Close(); sink->Release();
    auto faint = c; faint.a *= 0.18f;
    cv.rt->DrawEllipse(D2D1::Ellipse({cx, cy}, r, r), paint(faint), width);
    cv.rt->DrawGeometry(g, paint(c), width, roundStroke()); g->Release();
}

void toggle(int id, float x, float y, bool on) {
    auto r = R(x, y, 46, 26);
    fillRound(r, 13, on ? RED : rgba(255, 255, 255, hoverId == id ? 0.2f : 0.14f));
    cv.rt->FillEllipse(D2D1::Ellipse({on ? x + 33 : x + 13, y + 13}, 10, 10), paint(on ? WHITE : cream(0.75f)));
    hits.push_back({grow(r, 4), id, 0});
    if (focused(id, 0)) cv.rt->DrawRoundedRectangle(D2D1::RoundedRect(grow(r, 3), 16, 16), paint(GOLD), 1.5f);
}

// ---- Background, header, embers -----------------------------------------------------------------
struct Ember { float x, y, size, alpha; };
const Ember EMBERS[] = {{880, 120, 4, 1}, {960, 260, 3, .7f}, {1010, 90, 5, .8f}, {820, 420, 3, .6f}, {1050, 360, 4, .5f},
                        {760, 210, 3, .45f}, {1075, 180, 3, .55f}};
const Ember DONE_EMBERS[] = {{300, 150, 4, 1}, {780, 200, 5, .8f}, {860, 430, 3, .6f}, {220, 470, 4, .6f}, {640, 110, 3, .7f},
                             {520, 330, 3, .4f}, {930, 290, 3, .5f}};

void background(bool centred) {
    cv.rt->Clear(BG);
    D2D1_GRADIENT_STOP stops[] = {{0, rgba(200, 16, 46, centred ? 0.42f : 0.38f)}, {0.7f, rgba(200, 16, 46, 0)}, {1, rgba(200, 16, 46, 0)}};
    ID2D1GradientStopCollection* collection = nullptr;
    cv.rt->CreateGradientStopCollection(stops, 3, &collection);
    ID2D1RadialGradientBrush* glow = nullptr;
    auto centre = centred ? D2D1::Point2F(W * 0.5f, H * 0.45f) : D2D1::Point2F(W * 0.85f, H * 0.3f);
    float rx = centred ? W * 0.55f : W * 0.6f, ry = centred ? H * 0.65f : H * 0.7f;
    cv.rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(centre, {0, 0}, rx, ry), collection, &glow);
    cv.rt->FillRectangle(R(0, 0, W, H), glow);
    glow->Release(); collection->Release();
    // Embers drift up and flicker; at t = 0 (snapshots) they sit where the mockup has them.
    const Ember* list = centred ? DONE_EMBERS : EMBERS;
    for (int i = 0; i < 7; ++i) {
        const auto& e = list[i];
        float rise = fmodf(cv.t * (6 + i * 1.7f), 520.f);
        float y = e.y - rise; if (y < 20) y += 520;
        float x = e.x + sinf(cv.t * 0.6f + i * 1.9f) * 10 * (cv.t > 0 ? 1.f : 0.f);
        float flicker = cv.t > 0 ? 0.75f + 0.25f * sinf(cv.t * 3.1f + i * 2.3f) : 1.f;
        float r = e.size / 2;
        D2D1_GRADIENT_STOP g[] = {{0, rgba(255, 120, 40, 0.7f * e.alpha * flicker)}, {1, rgba(255, 120, 40, 0)}};
        ID2D1GradientStopCollection* c = nullptr; ID2D1RadialGradientBrush* b = nullptr;
        cv.rt->CreateGradientStopCollection(g, 2, &c);
        cv.rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({x + r, y + r}, {0, 0}, r + 9, r + 9), c, &b);
        cv.rt->FillEllipse(D2D1::Ellipse({x + r, y + r}, r + 9, r + 9), b);
        b->Release(); c->Release();
        cv.rt->FillEllipse(D2D1::Ellipse({x + r, y + r}, r, r), paint(rgba(255, 138, 60, e.alpha * flicker)));
    }
}

void header(int active, bool pills = true) {
    Style logo{ANTON, 34, DWRITE_FONT_WEIGHT_NORMAL, 0.02f};
    // Baselines line up: both texts are placed by their own ascent.
    auto baseline = [](const std::wstring& s, const Style& st_) {
        auto l = layout(s, st_, 1000); DWRITE_LINE_METRICS lm{}; UINT32 n = 0; l->GetLineMetrics(&lm, 1, &n); l->Release();
        return lm.baseline;
    };
    float base = 54;
    float logoWidth = measure(L"FFNY ONLINE", logo).width;
    text(L"FFNY ONLINE", logo, 40, base - baseline(L"FFNY ONLINE", logo), 400, WHITE);
    Style fine{BODY, 15, DWRITE_FONT_WEIGHT_SEMI_BOLD, 0.18f};
    text(L"SETUP", fine, 40 + logoWidth + 14, base - baseline(L"SETUP", fine), 200, RED);
    if (!pills) return;
    const wchar_t* names[] = {L"YOUR GAME", L"FOLDER", L"INSTALL"};
    Style pill{BODY, 13, DWRITE_FONT_WEIGHT_BOLD, 0.08f};
    float x = W - 40;
    for (int i = 2; i >= 0; --i) {
        std::wstring label = (i < active ? std::wstring(L"\u2713 ") : std::to_wstring(i + 1) + L" ") + names[i];
        float w = measure(label, pill).width + 28;
        x -= w;
        auto r = R(x, 26, w, 32);
        D2D1_COLOR_F fill = i == active ? RED : i < active ? rgba(110, 210, 110, 0.16f) : rgba(255, 255, 255, 0.06f);
        D2D1_COLOR_F color = i == active ? WHITE : i < active ? GREEN : cream(0.55f);
        fillRound(r, 16, fill);
        auto m = measure(label, pill);
        text(label, pill, x, 26 + 16 - m.height / 2, w, color, CENTER);
        x -= 10;
    }
}

std::wstring sizeText(double bytes) {
    wchar_t b[64];
    double gb = bytes / (1024.0 * 1024 * 1024);
    if (gb >= 10) swprintf(b, 64, L"%.0f GB", gb);
    else if (gb >= 1) swprintf(b, 64, L"%.1f GB", gb);
    else swprintf(b, 64, L"%.0f MB", bytes / (1024.0 * 1024));
    return b;
}

std::wstring kindTag(const std::wstring& kind) {
    if (kind == L"pal") return L"European (PAL) version";
    if (kind == L"japan") return L"Japanese version";
    if (kind == L"incomplete" || kind == L"cut") return L"incomplete or damaged";
    if (kind == L"gamecube") return L"GameCube version";
    if (kind == L"ok") return L"";
    return L"not the USA Xbox version";
}

// Ids of what can be clicked.
enum {
    A_BROWSE = 1, A_BROWSE_FOLDER, A_USE_FOUND, A_FOUND_PREV, A_FOUND_NEXT, A_NEXT_GAME, A_CHOOSE_ANOTHER, A_NEXT_FOLDER,
    A_BACK, A_CHANGE, A_HD, A_SHORTCUT, A_PREREQ, A_INSTALL, A_DETAILS, A_CANCEL, A_KEEP, A_STOP, A_PLAY, A_RETRY,
    A_OPEN_LOG, A_CLOSE, A_USE_OTHER,
    L_DISCORD = 1001, L_GUIDE = 1002, L_DETAILS = 1003, L_FOLDER = 1004, L_USE_OTHER = 1005
};

const Style H1{ANTON, 40}, P16{BODY, 16, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.45f}, LABEL{BODY, 13, DWRITE_FONT_WEIGHT_BOLD, 0.14f};

void bottomLink(const std::wstring& label) { link(L_DISCORD, label, 40, 624 - 10); }

void nextButton(int id, const std::wstring& label, bool enabled, float height = 48, float size = 16, float pad = 26) {
    float w = buttonWidth(label, size, pad);
    button(id, R(W - 40 - w, 648 - height, w, height), label, enabled ? RED_BUTTON : OFF, size);
}

const Verdict* goodFound() {
    for (const auto& f : st.found) if (f.ok && f.path != st.id.path) return &f;
    return nullptr;
}

void drawGame() {
    background(false);
    header(0);
    auto panel = R(40, 104, 640, 476);
    glass(panel);
    float x = 76, w = 568, y = 136;
    y += text(L"YOUR GAME", H1, x, y, w, WHITE);
    y += 6;
    y += text(L"FFNY Online is built from your own copy of Def Jam: Fight for NY for the Xbox (USA).", P16, x, y, w, cream(0.75f));
    y += 20;
    auto zone = R(x, y, w, 150);
    fillRound(zone, 14, rgba(255, 205, 60, 0.04f));
    {
        static ID2D1StrokeStyle* dashed = nullptr;
        float dashes[] = {2.5f, 2.5f};
        if (!dashed) d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                            D2D1_LINE_JOIN_ROUND, 10, D2D1_DASH_STYLE_CUSTOM), dashes, 2, &dashed);
        cv.rt->DrawRoundedRectangle(D2D1::RoundedRect(grow(zone, -1), 13, 13), paint(rgba(255, 205, 60, 0.45f)), 2, dashed);
    }
    float cy = y + (150 - 124) / 2;
    discIcon(x + w / 2 - 20, cy, 40, GOLD);
    Style drop{BODY, 18, DWRITE_FONT_WEIGHT_SEMI_BOLD};
    text(L"Drag your game file (.iso) here", drop, x, cy + 52, w, WHITE, CENTER);
    float bw = buttonWidth(L"BROWSE\u2026", 14);
    button(A_BROWSE, R(x + w / 2 - bw / 2, cy + 86, bw, 38), L"BROWSE\u2026", GHOST, 14);
    y = zone.bottom + 20;
    text(L"FOUND ON THIS PC", LABEL, x, y, w, cream(0.6f));
    if (st.found.size() > 1) {
        Style fine{BODY, 13, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        auto counter = std::to_wstring(st.foundIndex + 1) + L" of " + std::to_wstring(st.found.size());
        float cw = measure(counter, fine).width;
        button(A_FOUND_NEXT, R(x + w - 30, y - 7, 30, 28), L"\u203A", GHOST, 18, 0);
        button(A_FOUND_PREV, R(x + w - 66, y - 7, 30, 28), L"\u2039", GHOST, 18, 0);
        text(counter, fine, x + w - 76 - cw, y - 1, cw + 2, cream(0.6f));
    }
    y += 26;
    auto row = R(x, y, w, 66);
    fillRound(row, 12, rgba(255, 255, 255, 0.06f));
    if (st.found.empty()) {
        if (st.searching || !st.payloadReady) {
            spinner(x + 30, y + 33, 9, GOLD, 2.5f);
            text(L"Looking on this PC\u2026", Style{BODY, 16, DWRITE_FONT_WEIGHT_SEMI_BOLD}, x + 52, y + 22, w - 70, cream(0.8f));
        } else {
            text(L"No copy found in the usual places. Drag your game here, or use Browse.", Style{BODY, 15},
                 x + 16, y + 23, w - 32, cream(0.6f), LEFT, true);
        }
    } else {
        const auto& f = st.found[std::min(st.foundIndex, st.found.size() - 1)];
        bool ok = f.ok;
        float bwidth = buttonWidth(ok ? L"USE THIS" : L"CHECK IT", 14);
        float tw = w - 32 - bwidth - 16;
        text(f.name.empty() ? f.path : f.name, Style{BODY, 16, DWRITE_FONT_WEIGHT_SEMI_BOLD}, x + 16, y + 13, tw, WHITE, LEFT, true);
        std::wstring sub = f.folder + L" \u00b7 " + sizeText(f.size);
        auto tag = kindTag(f.kind);
        if (!tag.empty()) {
            std::vector<Span> spans{{sub + L" \u00b7 ", cream(0.6f)}, {tag, ORANGE}};
            auto m = measure(sub + L" \u00b7 " + tag, Style{BODY, 13});
            if (m.width < tw) rich(spans, Style{BODY, 13}, x + 16, y + 37, tw);
            else text(tag, Style{BODY, 13}, x + 16, y + 37, tw, ORANGE, LEFT, true);
        } else text(sub, Style{BODY, 13}, x + 16, y + 37, tw, cream(0.6f), LEFT, true);
        button(A_USE_FOUND, R(x + w - 16 - bwidth, y + 13, bwidth, 40), ok ? L"USE THIS" : L"CHECK IT", ok ? RED_BUTTON : GHOST, 14);
    }
    auto side = R(712, 104, 348, 476);
    glass(side);
    float sx = 740, sw = 292, sy = 132;
    sy += text(L"WHAT WORKS", Style{ANTON, 22}, sx, sy, sw, WHITE);
    sy += 16;
    Style item{BODY, 15, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.4f};
    sy += rich({{L"\u2713\u00a0 The Xbox version from the USA", cream(0.85f)}}, item, sx, sy, sw) + 12;
    sy += rich({{L"\u2713\u00a0 As an .iso file, or a ", cream(0.85f)}, {L"folder with the extracted game", GOLD, L_FOLDER - 1000}},
               item, sx, sy, sw) + 12;
    rich({{L"\u2715\u00a0 PlayStation 2, GameCube or the European (PAL) version", cream(0.55f)}}, item, sx, sy, sw);
    Style note{BODY, 14, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.45f};
    std::vector<Span> disc{{L"Only have the disc? ", cream(0.65f)}, {L"How to make an .iso from your disc", GOLD, L_GUIDE - 1000}};
    auto l = layout(L"Only have the disc? How to make an .iso from your disc", note, sw);
    DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m); l->Release();
    rich(disc, note, sx, 552 - m.height, sw);
    bottomLink(L"Need help? Ask in our Discord");
    nextButton(A_NEXT_GAME, L"NEXT", st.id.valid && st.id.ok && !st.identifying);
}

void drawIdentity() {
    background(false);
    header(0);
    auto panel = R(40, 104, 1020, 476);
    glass(panel);
    float x = 80, w = 940, y = 140;
    bool checking = st.identifying;
    bool ok = !checking && st.id.ok;
    y += text(L"YOUR GAME", H1, x, y, w, WHITE);
    if (checking || ok) {
        y += 6;
        y += text(checking ? L"Checking the file you chose\u2026" : L"We checked the file. It's the right one.", P16, x, y, w, cream(0.75f));
        y += 24;
        Style title{ANTON, 26}, line{BODY, 16, DWRITE_FONT_WEIGHT_SEMI_BOLD}, fine{BODY, 14};
        float bw = buttonWidth(L"CHOOSE ANOTHER", 14);
        float cw = w - 56 - 64 - 24 - 24 - bw;
        float th = measure(L"X", title).height, lh = measure(L"X", line).height, sh = measure(L"X", fine).height;
        float column = th + 6 + lh + 6 + sh;
        float ch = std::max(64.f, column) + 52;
        auto card = R(x, y, w, ch);
        auto accent = checking ? GOLD : rgba(110, 210, 110);
        fillRound(card, 16, checking ? rgba(255, 205, 60, 0.05f) : rgba(110, 210, 110, 0.08f));
        cv.rt->DrawRoundedRectangle(D2D1::RoundedRect(grow(card, -0.5f), 15.5f, 15.5f),
                                    paint(checking ? rgba(255, 205, 60, 0.3f) : rgba(110, 210, 110, 0.35f)), 1);
        float cx = x + 28, cy = y + ch / 2;
        auto circle = accent; circle.a = checking ? 0.12f : 0.18f;
        cv.rt->FillEllipse(D2D1::Ellipse({cx + 32, cy}, 32, 32), paint(circle));
        if (checking) spinner(cx + 32, cy, 15, GOLD, 3);
        else checkIcon(cx + 32 - 17, cy - 17, 34, GREEN, 2.4f);
        float tx = cx + 64 + 24, ty = cy - column / 2;
        text(checking ? L"CHECKING YOUR GAME" : L"DEF JAM: FIGHT FOR NY", title, tx, ty, cw, WHITE, LEFT, true);
        text(checking ? L"This takes a few seconds." : st.id.message, line, tx, ty + th + 6, cw, checking ? GOLD : GREEN, LEFT, true);
        text(st.id.chosen, fine, tx, ty + th + 6 + lh + 6, cw, cream(0.6f), LEFT, true);
        button(A_CHOOSE_ANOTHER, R(x + w - 28 - bw, cy - 20, bw, 40), L"CHOOSE ANOTHER", checking ? OFF : GHOST, 14);
        y += ch + 24;
        if (ok) text(L"Setup reads this file once while installing. You can keep it wherever it is now; it is never changed.",
                     Style{BODY, 15, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.5f}, x, y, w, cream(0.7f));
        bottomLink(L"Need help? Ask in our Discord");
        nextButton(A_NEXT_FOLDER, L"NEXT", ok);
        return;
    }
    // Not the game FFNY Online is built from: say what it is (as far as the engine can tell) and what to do.
    y += 22;
    Style title{ANTON, 26}, msg{BODY, 16, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.5f}, fine{BODY, 14};
    float cw = w - 56 - 64 - 24;
    float column = measure(st.id.headline, title, cw).height + 8 + measure(st.id.message, msg, cw).height + 8 +
                   measure(L"X", fine).height;
    float ch = std::max(64.f, column) + 52;
    auto card = R(x, y, w, ch);
    fillRound(card, 16, rgba(255, 168, 26, 0.08f));
    cv.rt->DrawRoundedRectangle(D2D1::RoundedRect(grow(card, -0.5f), 15.5f, 15.5f), paint(rgba(255, 168, 26, 0.4f)), 1);
    cv.rt->FillEllipse(D2D1::Ellipse({x + 28 + 32, y + 26 + 32}, 32, 32), paint(rgba(255, 168, 26, 0.16f)));
    warningIcon(x + 28 + 32 - 17, y + 26 + 32 - 17, 34, ORANGE);
    float tx = x + 28 + 64 + 24, ty = y + 26;
    ty += text(st.id.headline, title, tx, ty, cw, WHITE) + 8;
    ty += text(st.id.message, msg, tx, ty, cw, cream(0.85f)) + 8;
    text(st.id.chosen, fine, tx, ty, cw, cream(0.55f), LEFT, true);
    y += ch + 22;
    text(L"WHAT YOU CAN DO", LABEL, x, y, w, cream(0.6f));
    y += 26;
    Style item{BODY, 15, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.5f};
    if (auto other = goodFound()) {
        y += rich({{L"\u2022 Setup found the USA version on this PC: " + other->name + L". ", cream(0.85f)},
                   {L"Use that one", GOLD, L_USE_OTHER - 1000}}, item, x, y, w) + 10;
    } else {
        std::wstring advice = L"\u2022 " + st.id.advice;
        if (!st.searching && st.payloadReady) advice += L" Setup already looked on this PC and found no other copy.";
        y += text(advice, item, x, y, w, cream(0.85f)) + 10;
    }
    y += rich({{L"\u2022 Not sure which one you have? ", cream(0.85f)}, {L"Ask in our Discord", GOLD, L_DISCORD - 1000},
               {L", we'll help.", cream(0.85f)}}, item, x, y, w) + 22;
    float bw = buttonWidth(L"CHOOSE ANOTHER FILE", 14);
    button(A_CHOOSE_ANOTHER, R(x, y, bw, 44), L"CHOOSE ANOTHER FILE", RED_BUTTON, 14);
    bottomLink(L"Need help? Ask in our Discord");
    nextButton(A_NEXT_FOLDER, L"NEXT", false);
}

void drawFolder() {
    background(false);
    header(1);
    auto panel = R(40, 104, 620, 476);
    glass(panel);
    float x = 76, w = 548, y = 136;
    y += text(L"WHERE TO INSTALL", H1, x, y, w, WHITE);
    y += 6;
    std::wstring intro = st.plan.valid && st.plan.existing ? L"FFNY Online is already here. Setup updates it and keeps your saves."
                         : L"We picked a good place. Your saves and settings go here too.";
    y += text(intro, P16, x, y, w, cream(0.75f));
    y += 20;
    auto row = R(x, y, w, 76);
    fillRound(row, 14, rgba(255, 255, 255, 0.06f));
    folderIcon(x + 20, y + 24, 28, GOLD);
    float bw = buttonWidth(L"CHANGE", 14);
    std::wstring shown = st.plan.valid ? st.plan.folder : L"Finding a good place\u2026";
    text(shown, Style{BODY, 18, DWRITE_FONT_WEIGHT_SEMI_BOLD}, x + 62, y + 26, w - 62 - 20 - bw - 16, WHITE, LEFT, true);
    button(A_CHANGE, R(x + w - 20 - bw, y + 18, bw, 40), L"CHANGE", st.planning ? OFF : GHOST, 14);
    y += 76;
    if (st.plan.valid && st.plan.adjusted && !st.planning) {
        y += 8;
        y += text(L"The folder you chose has other files in it, so FFNY Online gets its own folder inside it.",
                  Style{BODY, 13, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.4f}, x, y, w, cream(0.6f));
    }
    y += 20;
    Style item{BODY, 15};
    if (st.planning || !st.plan.valid) {
        spinner(x + 9, y + 10, 8, GOLD, 2.5f);
        text(L"Checking this folder\u2026", item, x + 28, y, w - 28, cream(0.85f));
    } else {
        for (const auto& c : st.plan.checks) {
            if (y > 548) break;
            if (c.ok) checkIcon(x - 2, y - 1, 20, GREEN, 3);
            else {
                cv.rt->FillEllipse(D2D1::Ellipse({x + 8, y + 9.5f}, 9, 9), paint(rgba(255, 168, 26, 0.2f)));
                text(L"!", Style{BODY, 14, DWRITE_FONT_WEIGHT_BOLD}, x, y + 1, 16, ORANGE, CENTER);
            }
            float h = text(c.text, Style{BODY, 15, c.ok ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_SEMI_BOLD, 0, 1.3f},
                           x + 26, y, w - 26, c.ok ? cream(0.85f) : WHITE);
            y += h;
            if (!c.ok && !c.fix.empty()) y += 3 + text(c.fix, Style{BODY, 13, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.4f}, x + 26, y + 3, w - 26, cream(0.65f));
            y += 12;
        }
    }
    auto side = R(692, 104, 368, 476);
    glass(side);
    float sx = 720, sw = 312, sy = 132;
    sy += text(L"OPTIONS", Style{ANTON, 22}, sx, sy, sw, WHITE);
    sy += 18;
    struct Option { int id; const wchar_t* title; const wchar_t* help; bool on; };
    Option options[] = {
        {A_HD, L"HD textures", L"Sharper fighters and venues. Recommended; adds about 6 GB.", st.hd},
        {A_SHORTCUT, L"Desktop shortcut", L"Start FFNY Online from your desktop.", st.shortcut},
        {A_PREREQ, L"Missing Windows parts", L"Installs what the game needs, if anything is missing.", st.prereq},
    };
    for (const auto& o : options) {
        float tw = sw - 46 - 14;
        float th = measure(o.title, Style{BODY, 16, DWRITE_FONT_WEIGHT_SEMI_BOLD}, tw).height;
        float hh = measure(o.help, Style{BODY, 13, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.4f}, tw).height;
        float rowH = th + 3 + hh;
        text(o.title, Style{BODY, 16, DWRITE_FONT_WEIGHT_SEMI_BOLD}, sx, sy, tw, WHITE);
        text(o.help, Style{BODY, 13, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.4f}, sx, sy + th + 3, tw, cream(0.6f));
        toggle(o.id, sx + sw - 46, sy + rowH / 2 - 13, o.on);
        sy += rowH + 18;
    }
    float back = buttonWidth(L"BACK");
    button(A_BACK, R(40, 600, back, 48), L"BACK", GHOST);
    nextButton(A_INSTALL, L"INSTALL", st.plan.valid && st.plan.ok && !st.planning, 54, 18, 40);
}

struct Step { const wchar_t* name; std::vector<const char*> keys; };

void drawInstalling() {
    background(false);
    header(2);
    auto panel = R(40, 104, 1020, 476);
    glass(panel);
    float x = 80, w = 940, y = 140;
    wchar_t percent[16]; swprintf(percent, 16, L"%d%%", st.permille / 10);
    Style big{ANTON, 56};
    auto pm = measure(percent, big);
    float leftW = w - pm.width - 20;
    float h1 = measure(L"X", H1).height;
    std::wstring intro = st.update ? L"FFNY Online is building the new version from the game files you installed before."
                                   : L"This takes a while the first time, up to an hour on slower PCs. You can leave it running.";
    float ph = measure(intro, P16, leftW).height;
    float rowH = std::max(h1 + 6 + ph, pm.height * 0.86f);
    text(st.update ? L"UPDATING YOUR GAME" : L"BUILDING YOUR GAME", H1, x, y + rowH - (h1 + 6 + ph), leftW, WHITE);
    text(intro, P16, x, y + rowH - ph, leftW, cream(0.75f));
    // Anton's line box is taller than its figures: sit the figures on the row's bottom.
    auto l = layout(percent, big, pm.width + 4);
    DWRITE_LINE_METRICS lm{}; UINT32 n = 0; l->GetLineMetrics(&lm, 1, &n); l->Release();
    text(percent, big, x + w - pm.width, y + rowH - lm.baseline, pm.width + 4, GOLD);
    y += rowH + 26;
    auto bar = R(x, y, w, 14);
    fillRound(bar, 7, rgba(255, 255, 255, 0.08f));
    float fw = w * std::clamp(st.permille, 0, 1000) / 1000.f;
    if (fw > 1) {
        auto fill = R(x, y, std::max(14.f, fw), 14);
        softShadow(fill, 7, 18, 0, rgba(200, 16, 46, 0.7f));
        fillRound(fill, 7, RED);
    }
    y += 14 + 12;
    if (!st.statusText.empty())
        text(st.statusText, Style{BODY, 13}, x, y, w, cream(0.5f), LEFT, true);
    y += 26;
    float bottomY = 544 - 20;
    if (st.details) {
        auto area = R(x, y, w, bottomY - 16 - y);
        fillRound(area, 12, rgba(0, 0, 0, 0.35f));
        if (!logView || !IsWindowVisible(logView)) {
            // Offscreen (snapshots): the log's last lines, drawn here.
            std::wstring sample = L"Preparing verified release source\nVerifying and preparing your dump\nChecking C++ compiler, Windows SDK, CMake and Ninja\n"
                                  L"Analysis already verified; reused\nLift already verified; reused\nBuild in progress (this may take several minutes)\n"
                                  L"[812/1764] Building C object CMakeFiles/defjam_recomp.dir/src/recomp/gen/recomp_0007.c.obj";
            text(sample, Style{BODY, 12, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.45f}, x + 14, area.top + 10, w - 28, cream(0.7f));
        }
    } else {
        std::vector<Step> steps{{L"Checking your game", {"source"}}, {L"Copying game files", {"extract"}},
                                {L"Building the game (the longest part)", {"toolchain", "analysis", "lift", "build"}}};
        if (st.hd) steps.push_back({L"Adding HD textures", {"hd"}});
        steps.push_back({L"Finishing up", {"activate"}});
        static const char* order[] = {"source", "extract", "toolchain", "analysis", "lift", "build", "hd", "activate"};
        int current = -1;
        for (int i = 0; i < 8; ++i) if (st.stageKey == order[i]) current = i;
        auto indexOf = [&](const char* k) { for (int i = 0; i < 8; ++i) if (!strcmp(order[i], k)) return i; return 0; };
        Style item{BODY, 16}, now{BODY, 16, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        float colW = (w - 40) / 2, rowStep = measure(L"X", item).height + 14;
        for (size_t i = 0; i < steps.size(); ++i) {
            int first = indexOf(steps[i].keys.front()), last = indexOf(steps[i].keys.back());
            bool done = current > last || (st.permille >= 1000);
            bool active = !done && current >= first && current <= last;
            float sx = i < 3 ? x : x + colW + 40, sy = y + (i < 3 ? i : i - 3) * rowStep;
            if (done) {
                checkIcon(sx - 2, sy, 20, GREEN, 3);
                text(steps[i].name, item, sx + 24, sy, colW - 24, cream(0.85f));
            } else if (active) {
                cv.rt->FillEllipse(D2D1::Ellipse({sx + 7, sy + 10.5f}, 5, 5), paint(GOLD));
                text(steps[i].name, now, sx + 24, sy, colW - 24, WHITE);
            } else {
                cv.rt->DrawEllipse(D2D1::Ellipse({sx + 7, sy + 10.5f}, 4.5f, 4.5f), paint(cream(0.45f)), 1.3f);
                text(steps[i].name, item, sx + 24, sy, colW - 24, cream(0.45f));
            }
        }
    }
    std::wstring left = (st.estimate.empty() ? L"" : st.estimate + L" \u00b7 ") + L"We'll show a notification when it's done.";
    text(left, Style{BODY, 15}, x, bottomY, w - 160, cream(0.65f), LEFT, true);
    link(L_DETAILS, st.details ? L"Hide details" : L"Show details", x + w - 300, bottomY + 1, 14, RIGHT, 300);
    bottomLink(L"Join our Discord while you wait");
    float cw = buttonWidth(L"CANCEL");
    button(A_CANCEL, R(W - 40 - cw, 600, cw, 48), L"CANCEL", st.stopping ? OFF : GHOST);
}

void drawDone() {
    background(true);
    header(3, false);
    Style label{BODY, 15, DWRITE_FONT_WEIGHT_BOLD, 0.22f};
    float y = 130;
    y += text(st.update ? L"\u2713 UPDATED" : L"\u2713 INSTALLED", label, 0, y, W, GREEN, CENTER) + 18;
    Style huge{ANTON, 84, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.0f};
    y += text(L"READY TO FIGHT", huge, 0, y, W, WHITE, CENTER) + 18;
    y += text(L"Sign in with Discord in the launcher to play online.", Style{BODY, 17}, 0, y, W, cream(0.8f), CENTER) + 36;
    float pw = buttonWidth(L"PLAY", 26, 70, 0.12f);
    button(A_PLAY, R(W / 2 - pw / 2, y, pw, 72), L"PLAY", RED_BUTTON, 26, 0.12f);
    auto box = R(250, 680 - 40 - 96, 600, 96);
    glass(box);
    std::wstring first = st.shortcut ? L"Desktop shortcut created" : L"Start FFNY Online from the Start menu";
    text(first, Style{BODY, 15}, box.left + 28, box.top + 28, 380, cream(0.85f));
    text(L"Updates install themselves from the launcher.", Style{BODY, 13}, box.left + 28, box.top + 52, 380, cream(0.55f));
    float jw = buttonWidth(L"JOIN THE DISCORD", 14);
    button(L_DISCORD, R(box.right - 28 - jw, box.top + 26, jw, 44), L"JOIN THE DISCORD", GHOST, 14);
}

void drawFailed() {
    background(false);
    header(2);
    auto panel = R(40, 104, 1020, 476);
    glass(panel);
    float x = 80, w = 940, y = 140;
    bool cancelled = st.exitCode == 6;
    y += text(L"SETUP STOPPED", H1, x, y, w, WHITE) + 22;
    Style title{ANTON, 26}, msg{BODY, 16, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.5f}, fine{BODY, 14};
    std::wstring headline = cancelled ? L"YOU STOPPED SETUP" : st.payloadFailed ? L"SETUP COULDN'T PREPARE ITS FILES" : L"SETUP COULDN'T FINISH";
    std::wstring message = cancelled ? L"Completed steps are kept. Try again to continue where it stopped." : st.failure;
    float cw = w - 56 - 64 - 24;
    float column = measure(headline, title, cw).height + 8 + std::min(110.f, measure(message, msg, cw).height) + 8 + measure(L"X", fine).height;
    float ch = std::max(64.f, column) + 52;
    auto card = R(x, y, w, ch);
    fillRound(card, 16, rgba(255, 168, 26, 0.08f));
    cv.rt->DrawRoundedRectangle(D2D1::RoundedRect(grow(card, -0.5f), 15.5f, 15.5f), paint(rgba(255, 168, 26, 0.4f)), 1);
    cv.rt->FillEllipse(D2D1::Ellipse({x + 60, y + 58}, 32, 32), paint(rgba(255, 168, 26, 0.16f)));
    warningIcon(x + 43, y + 41, 34, ORANGE);
    float tx = x + 116, ty = y + 26;
    ty += text(headline, title, tx, ty, cw, WHITE) + 8;
    cv.rt->PushAxisAlignedClip(R(tx, ty, cw, 110), D2D1_ANTIALIAS_MODE_ALIASED);
    ty += std::min(110.f, text(message, msg, tx, ty, cw, cream(0.85f))) + 8;
    cv.rt->PopAxisAlignedClip();
    if (!logPath.empty()) text(L"Log: " + logPath.wstring(), fine, tx, ty, cw, cream(0.55f), LEFT, true);
    y += ch + 22;
    if (st.details) {
        auto area = R(x, y, w, 544 - 60 - y);
        fillRound(area, 12, rgba(0, 0, 0, 0.35f));
    } else {
        text(L"WHAT YOU CAN DO", LABEL, x, y, w, cream(0.6f));
        y += 26;
        Style item{BODY, 15, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.5f};
        std::wstring first = st.exitCode == 3010 ? L"\u2022 Restart Windows, then run setup again: it continues where it stopped."
                             : st.exitCode == 5 ? L"\u2022 Close the game and any other setup, then select Try again."
                             : st.exitCode == 3 && !st.prereq && !st.update ? L"\u2022 Setup can add the missing Windows parts: select Turn on and try again (Windows asks for permission)."
                             : st.exitCode == 3 ? L"\u2022 Install the parts named above, then select Try again."
                             : L"\u2022 Select Try again: setup continues where it stopped.";
        y += text(first, item, x, y, w, cream(0.85f)) + 10;
        rich({{L"\u2022 Still stuck? ", cream(0.85f)}, {L"Ask in our Discord", GOLD, L_DISCORD - 1000},
              {L" and attach the log file (Open log file).", cream(0.85f)}}, item, x, y, w);
    }
    float by = 544 - 44;
    bool turnOn = st.exitCode == 3 && !st.prereq && !st.update;
    std::wstring retry = turnOn ? L"TURN ON AND TRY AGAIN" : L"TRY AGAIN";
    float rw = buttonWidth(retry, 14), dw2 = buttonWidth(st.details ? L"HIDE DETAILS" : L"SHOW DETAILS", 14),
          ow = buttonWidth(L"OPEN LOG FILE", 14);
    if (!st.payloadFailed) button(A_RETRY, R(x, by, rw, 44), retry, RED_BUTTON, 14);
    float bx = st.payloadFailed ? x : x + rw + 12;
    button(A_DETAILS, R(bx, by, dw2, 44), st.details ? L"HIDE DETAILS" : L"SHOW DETAILS", GHOST, 14);
    button(A_OPEN_LOG, R(bx + dw2 + 12, by, ow, 44), L"OPEN LOG FILE", GHOST, 14);
    bottomLink(L"Need help? Ask in our Discord");
    float cw2 = buttonWidth(L"CLOSE");
    button(A_CLOSE, R(W - 40 - cw2, 600, cw2, 48), L"CLOSE", GHOST);
}

void drawWine() {
    background(true);
    header(0, false);
    auto panel = R(190, 170, 720, 296);
    glass(panel);
    float x = 240, w = 620, y = 214;
    y += text(L"WINDOWS ONLY FOR NOW", H1, x, y, w, WHITE) + 10;
    y += text(L"This setup is running under Wine. FFNY Online supports Windows only for now, so setup stops here "
              L"before it unpacks or installs anything.", P16, x, y, w, cream(0.8f)) + 14;
    y += rich({{L"Questions about other systems? ", cream(0.75f)}, {L"Ask in our Discord", GOLD, L_DISCORD - 1000}, {L".", cream(0.75f)}},
              P16, x, y, w) + 28;
    float jw = buttonWidth(L"JOIN THE DISCORD", 14), cw = buttonWidth(L"CLOSE", 14);
    button(L_DISCORD, R(x, y, jw, 44), L"JOIN THE DISCORD", RED_BUTTON, 14);
    button(A_CLOSE, R(x + jw + 12, y, cw, 44), L"CLOSE", GHOST, 14);
}

void drawConfirm() {
    hits.clear();   // only the dialog can be clicked
    cv.rt->FillRectangle(R(0, 0, W, H), paint(rgba(0, 0, 0, 0.55f)));
    auto card = R(W / 2 - 280, H / 2 - 120, 560, 240);
    softShadow(card, 18, 50, 18, rgba(0, 0, 0, 0.6f));
    fillRound(card, 18, rgba(24, 16, 18, 0.97f));
    float x = card.left + 36, w = 560 - 72, y = card.top + 32;
    y += text(L"STOP SETUP?", Style{ANTON, 30}, x, y, w, WHITE) + 8;
    text(L"Completed steps are kept. Run setup again to continue where it stopped.", P16, x, y, w, cream(0.8f));
    float sw = buttonWidth(L"STOP SETUP", 14), kw = buttonWidth(L"KEEP GOING", 14);
    button(A_STOP, R(card.right - 36 - sw, card.bottom - 36 - 44, sw, 44), L"STOP SETUP", GHOST, 14);
    button(A_KEEP, R(card.right - 36 - sw - 12 - kw, card.bottom - 36 - 44, kw, 44), L"KEEP GOING", RED_BUTTON, 14);
}

void drawAll() {
    hits.clear();
    cv.rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    switch (st.screen) {
        case S_GAME: drawGame(); break;
        case S_IDENTITY: drawIdentity(); break;
        case S_FOLDER: drawFolder(); break;
        case S_INSTALLING: drawInstalling(); break;
        case S_DONE: drawDone(); break;
        case S_FAILED: drawFailed(); break;
        case S_WINE: drawWine(); break;
    }
    if (st.confirmStop) drawConfirm();
}

// ---- Fonts and render targets -------------------------------------------------------------------
bool initGraphics() {
    if (d2d) return true;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dw))) return false;
    // Anton (SIL Open Font License; ffny-branding/fonts) is embedded as resource 102 and loaded from
    // memory: nothing is installed or written. Without DirectWrite 3 (Windows 10 1703+) a bold
    // system face stands in.
    IDWriteFactory5* dw5 = nullptr;
    auto res = FindResourceW(nullptr, MAKEINTRESOURCEW(102), RT_RCDATA);
    if (res && SUCCEEDED(dw->QueryInterface(&dw5))) {
        auto bytes = LockResource(LoadResource(nullptr, res));
        auto size = SizeofResource(nullptr, res);
        IDWriteInMemoryFontFileLoader* loader = nullptr;
        IDWriteFontFile* file = nullptr;
        IDWriteFontSetBuilder1* builder = nullptr;
        IDWriteFontSet* set = nullptr;
        IDWriteFontCollection1* collection = nullptr;
        if (SUCCEEDED(dw5->CreateInMemoryFontFileLoader(&loader)) && SUCCEEDED(dw5->RegisterFontFileLoader(loader)) &&
            SUCCEEDED(loader->CreateInMemoryFontFileReference(dw5, bytes, size, nullptr, &file)) &&
            SUCCEEDED(dw5->CreateFontSetBuilder(&builder)) && SUCCEEDED(builder->AddFontFile(file)) &&
            SUCCEEDED(builder->CreateFontSet(&set)) && SUCCEEDED(dw5->CreateFontCollectionFromFontSet(set, &collection)))
            antonFonts = collection;
        if (set) set->Release();
        if (builder) builder->Release();
        if (file) file->Release();
        dw5->Release();
    }
    IDWriteFontCollection* system = nullptr;
    dw->GetSystemFontCollection(&system);
    auto has = [&](const wchar_t* family) { UINT32 i; BOOL exists = FALSE; system->FindFamilyName(family, &i, &exists); return exists != FALSE; };
    if (!antonFonts) antonFamily = has(L"Impact") ? L"Impact" : L"Segoe UI Black";
    // Bahnschrift (Windows 10 1709+) is the closest system face to the mockup's Barlow.
    bodyFamily = has(L"Bahnschrift") ? L"Bahnschrift" : L"Segoe UI";
    system->Release();
    return true;
}

bool renderOffscreen(const std::wstring& out, float scale) {
    IWICImagingFactory* wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    UINT pw = (UINT)(W * scale), ph = (UINT)(H * scale);
    IWICBitmap* bitmap = nullptr;
    wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    ID2D1RenderTarget* rt = nullptr;
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * scale, 96 * scale);
    bool ok = SUCCEEDED(d2d->CreateWicBitmapRenderTarget(bitmap, props, &rt));
    if (ok) {
        Canvas saved = cv;
        cv.rt = rt; cv.brush = nullptr; rt->CreateSolidColorBrush(WHITE, &cv.brush);
        rt->BeginDraw();
        drawAll();
        ok = SUCCEEDED(rt->EndDraw());
        cv.brush->Release(); rt->Release();
        auto t = cv.t; cv = saved; cv.t = t;
    }
    if (ok && !out.empty()) {
        bool png = out.size() < 4 || _wcsicmp(out.c_str() + out.size() - 4, L".bmp") != 0;
        IWICStream* stream = nullptr; IWICBitmapEncoder* encoder = nullptr; IWICBitmapFrameEncode* frame = nullptr;
        ok = SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(out.c_str(), GENERIC_WRITE)) &&
             SUCCEEDED(wic->CreateEncoder(png ? GUID_ContainerFormatPng : GUID_ContainerFormatBmp, nullptr, &encoder)) &&
             SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
             SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(pw, ph));
        WICPixelFormatGUID format = png ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat32bppBGR;
        ok = ok && SUCCEEDED(frame->SetPixelFormat(&format));
        IWICFormatConverter* converter = nullptr;
        ok = ok && SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
             SUCCEEDED(converter->Initialize(bitmap, format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
             SUCCEEDED(frame->WriteSource(converter, nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (encoder) encoder->Release();
        if (stream) stream->Release();
    }
    if (bitmap) bitmap->Release();
    wic->Release();
    return ok;
}

float scaleOf() { return dpi / 96.f; }

void paintWindow() {
    if (!windowTarget) {
        RECT rc; GetClientRect(window, &rc);
        auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), (float)dpi, (float)dpi);
        if (FAILED(d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(rc.right, rc.bottom)),
                                              &windowTarget))) return;
    }
    cv.rt = windowTarget;
    if (!cv.brush) windowTarget->CreateSolidColorBrush(WHITE, &cv.brush);
    cv.t = (GetTickCount64() - startTick) / 1000.f;
    windowTarget->BeginDraw();
    drawAll();
    HRESULT drawn = windowTarget->EndDraw();
    windowDrawn = SUCCEEDED(drawn);
    if (drawn == D2DERR_RECREATE_TARGET) {
        cv.brush->Release(); cv.brush = nullptr;
        windowTarget->Release(); windowTarget = nullptr;
    }
}

void redraw() { if (window) InvalidateRect(window, nullptr, FALSE); }

// ---- The engine's questions ---------------------------------------------------------------------
bool waitPayload() {
    WaitForSingleObject(payloadEvent, INFINITE);
    return st.payloadReady;
}

void ask(std::vector<std::wstring> args, UINT message, int token) {
    std::thread([args, message, token]() mutable {
        auto answer = new Answer{token, 4, {}};
        try {
            if (waitPayload()) {
                static std::atomic<int> serial{0};
                auto out = scratch / (L"answer-" + std::to_wstring(++serial) + L".json");
                args.push_back(L"--out"); args.push_back(out.wstring());
                answer->code = wait(startEngine(args));
                readShared(out, answer->text);
                std::error_code e; fs::remove(out, e);
            }
        } catch (const std::exception& ex) { nativeLog(std::string("Setup question failed: ") + ex.what()); }
        PostMessageW(window, message, 0, (LPARAM)answer);
    }).detach();
}

void identify(const std::wstring& chosen) {
    st.id = Verdict{}; st.id.chosen = chosen; st.identifying = true; st.screen = S_IDENTITY;
    st.plan = Plan{};
    ask({L"--identify", chosen}, WM_IDENTITY, ++identityToken);
    redraw();
}

void planFor(const std::wstring& folder) {
    st.planning = true;
    std::vector<std::wstring> args{L"--plan-folder"};
    if (!folder.empty()) { args.push_back(L"--folder"); args.push_back(folder); }
    if (!st.id.path.empty()) { args.push_back(L"--dump"); args.push_back(st.id.path); }
    if (st.hd) args.push_back(L"--hd-textures");
    ask(args, WM_PLAN, ++planToken);
    redraw();
}

std::wstring pick(bool folder, const std::wstring& start = {}) {
    IFileOpenDialog* dialog = nullptr;
    std::wstring result;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return result;
    DWORD options; dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    if (!folder) {
        COMDLG_FILTERSPEC filter[]{{L"Game disc images (*.iso, *.xiso)", L"*.iso;*.xiso"}, {L"All files", L"*.*"}};
        dialog->SetFileTypes(2, filter);
        dialog->SetTitle(L"Choose your Def Jam: Fight for NY disc image");
    } else dialog->SetTitle(start.empty() ? L"Choose the folder with the extracted game" : L"Choose where to install FFNY Online");
    if (!start.empty()) {
        fs::path p = start;
        while (!p.empty() && !fs::is_directory(p) && p.has_parent_path() && p.parent_path() != p) p = p.parent_path();
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(p.c_str(), nullptr, IID_PPV_ARGS(&item)))) { dialog->SetFolder(item); item->Release(); }
    }
    if (SUCCEEDED(dialog->Show(window))) {
        IShellItem* item = nullptr; PWSTR path = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { result = path; CoTaskMemFree(path); }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

// ---- Install ------------------------------------------------------------------------------------
void layoutLogView();

// Appends what the engine has written to the session log since the last tick, whole lines only
// (a UTF-8 character never splits), keeping the newest LOG_VIEW_CHARS. A reader scrolled up stays
// where they are; at the bottom, the view follows the build.
void tailLog() {
    if (logPath.empty() || !logView) return;
    std::ifstream in(logPath, std::ios::binary);
    if (!in) return;
    in.seekg(0, std::ios::end);
    auto size = (unsigned long long)in.tellg();
    if (size < logOffset) { logOffset = 0; logPending.clear(); }
    if (size == logOffset) return;
    in.seekg((std::streamoff)logOffset);
    std::string chunk((size_t)(size - logOffset), '\0');
    in.read(chunk.data(), (std::streamsize)chunk.size());
    chunk.resize((size_t)in.gcount());
    logOffset += chunk.size();
    logPending += chunk;
    auto end = logPending.rfind('\n');
    if (end == std::string::npos) return;
    std::wstring text_;
    for (auto c : utf8(logPending.substr(0, end + 1))) {
        if (c == L'\n') text_ += L"\r\n";
        else if (c != L'\r') text_ += c;
    }
    logPending.erase(0, end + 1);
    if (text_.size() > (size_t)LOG_VIEW_CHARS) text_.erase(0, text_.size() - LOG_VIEW_CHARS);
    SCROLLINFO si{sizeof(si), SIF_ALL};
    bool follow = !GetScrollInfo(logView, SB_VERT, &si) || si.nPos + (int)si.nPage > si.nMax;
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(logView, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    int first = (int)SendMessageW(logView, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageW(logView, WM_SETREDRAW, FALSE, 0);
    int length = GetWindowTextLengthW(logView);
    if (length + (int)text_.size() > LOG_VIEW_CHARS) {
        int line = (int)SendMessageW(logView, EM_LINEFROMCHAR, length + (int)text_.size() - LOG_VIEW_CHARS, 0) + 1;
        int cut = (int)SendMessageW(logView, EM_LINEINDEX, line, 0);
        if (cut < 0) { cut = length; line = (int)SendMessageW(logView, EM_GETLINECOUNT, 0, 0); }
        SendMessageW(logView, EM_SETSEL, 0, cut);
        SendMessageW(logView, EM_REPLACESEL, FALSE, (LPARAM)L"");
        length -= cut; first = first > line ? first - line : 0;
        selStart = selStart > (DWORD)cut ? selStart - cut : 0; selEnd = selEnd > (DWORD)cut ? selEnd - cut : 0;
    }
    SendMessageW(logView, EM_SETSEL, length, length);
    SendMessageW(logView, EM_REPLACESEL, FALSE, (LPARAM)text_.c_str());
    if (follow) {
        length = GetWindowTextLengthW(logView);
        SendMessageW(logView, EM_SETSEL, length, length);
        SendMessageW(logView, WM_VSCROLL, SB_BOTTOM, 0);
    } else {
        SendMessageW(logView, EM_SETSEL, selStart, selEnd);
        SendMessageW(logView, EM_LINESCROLL, 0, first - (int)SendMessageW(logView, EM_GETFIRSTVISIBLELINE, 0, 0));
    }
    SendMessageW(logView, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(logView, nullptr, TRUE);
}

void startInstall() {
    std::vector<std::wstring> args{L"--status-file", statusPath.wstring(), L"--cancel-file", cancelPath.wstring()};
    if (st.update) {
        args.insert(args.end(), {L"--install-dir", g_update_install.wstring(), L"--data-dir", g_update_data.wstring(), L"--reuse-extracted"});
    } else {
        args.insert(args.end(), {L"--install-dir", st.plan.installDir, L"--data-dir", st.plan.dataDir, L"--dump", st.id.path});
        if (st.hd) args.push_back(L"--hd-textures");
    }
    if (st.prereq) args.push_back(L"--install-prerequisites");
    if (!st.shortcut) args.push_back(L"--no-desktop-shortcut");
    st.screen = S_INSTALLING; st.permille = 0; st.stageKey.clear(); st.statusText = L"Preparing setup files\u2026";
    st.estimate.clear(); st.buildStart = 0; st.exitCode = 0; st.confirmStop = false; st.stopping = false; st.failure.clear();
    notified = false;
    std::error_code e;
    for (auto suffix : {L"", L".progress", L".stage"}) fs::remove(statusPath.wstring() + suffix, e);
    fs::remove(cancelPath, e);
    if (logView) { SetWindowTextW(logView, L""); logOffset = 0; logPending.clear(); }
    {
        // The session log already has the questions' lines; the details view starts at the install.
        std::error_code size_error;
        auto size = fs::file_size(logPath, size_error);
        logOffset = size_error ? 0 : size;
    }
    pollTicks = 0;
    SetTimer(window, 2, 400, nullptr);
    SetTimer(window, 1, 80, nullptr);
    std::thread([args]() {
        try {
            if (g_wait_pid) {
                // The game that asked for this update closes itself; its files must be free.
                HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, g_wait_pid);
                if (old) { WaitForSingleObject(old, 30000); CloseHandle(old); }
            }
            if (!waitPayload()) throw std::runtime_error(narrow(st.payloadError));
            if (fs::exists(cancelPath)) {
                nativeLog("Setup cancelled during preparation.");
                PostMessageW(window, WM_SETUP_FAILED, 6, (LPARAM)new std::wstring(L"Setup cancelled."));
                return;
            }
            PostMessageW(window, WM_SETUP_READY, (WPARAM)startEngine(args), 0);
        } catch (const std::exception& ex) {
            nativeLog(std::string("Setup stopped: ") + ex.what());
            PostMessageW(window, WM_SETUP_FAILED, 4, (LPARAM)new std::wstring(utf8(ex.what())));
        }
    }).detach();
    layoutLogView();
    redraw();
}

void notifyDone(bool ok) {
    if (!window || GetForegroundWindow() == window) return;
    FLASHWINFO flash{sizeof(flash), window, FLASHW_ALL | FLASHW_TIMERNOFG, 0, 0};
    FlashWindowEx(&flash);
    NOTIFYICONDATAW icon{sizeof(icon)};
    icon.hWnd = window; icon.uID = 1; icon.uFlags = NIF_ICON | NIF_TIP | NIF_INFO | NIF_MESSAGE;
    icon.uCallbackMessage = WM_TRAY;
    icon.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    wcscpy_s(icon.szTip, L"FFNY Online Setup");
    wcscpy_s(icon.szInfoTitle, ok ? L"FFNY Online is ready to fight" : L"FFNY Online setup stopped");
    wcscpy_s(icon.szInfo, ok ? L"Setup finished. Select Play to start." : L"Open the setup window to see why.");
    icon.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    notified = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
}

void updateEstimate() {
    // An estimate only from the build's own pace (Ninja's file count), once it has run a minute and
    // done a twentieth: files differ in size, so it stays "about".
    if (st.stageKey != "build") { st.estimate.clear(); return; }
    float lo = 260, hi = st.hd ? 800.f : 950.f;
    if (!st.buildStart) { st.buildStart = GetTickCount64(); st.buildStartPermille = st.permille; return; }
    float f0 = (st.buildStartPermille - lo) / (hi - lo), f = (st.permille - lo) / (hi - lo);
    double elapsed = (GetTickCount64() - st.buildStart) / 1000.0;
    if (f - f0 < 0.05f || elapsed < 60 || f >= 1) return;
    double minutes = elapsed * (1 - f) / (f - f0) / 60.0;
    int shown = minutes < 1 ? 1 : minutes <= 10 ? (int)ceil(minutes) : (int)(ceil(minutes / 5) * 5);
    st.estimate = minutes < 1 ? L"Less than a minute left in the build"
                              : L"About " + std::to_wstring(shown) + (shown == 1 ? L" minute" : L" minutes") + L" left" +
                                    (st.hd ? L" in the build, then HD textures" : L"");
}

void finishInstall(DWORD code) {
    KillTimer(window, 2);
    SetTimer(window, 1, 33, nullptr);
    tailLog();
    st.exitCode = code;
    st.confirmStop = false;
    if (code == 0) {
        st.permille = 1000; st.screen = S_DONE;
    } else {
        std::string status;
        st.failure.clear();
        if (readShared(statusPath, status) && !status.empty()) st.failure = utf8(status);
        auto prefix = std::wstring(L"Setup stopped: ");
        if (st.failure.rfind(prefix, 0) == 0) st.failure = st.failure.substr(prefix.size());
        if (st.failure.empty()) st.failure = L"Setup stopped (exit " + std::to_wstring(code) + L"). The log has the details.";
        st.screen = S_FAILED;
    }
    notifyDone(code == 0);
    layoutLogView();
    redraw();
}

// The log view: a read-only edit control (it can be selected and copied), over the panel's dark area.
void layoutLogView() {
    if (!logView) return;
    bool show = st.details && (st.screen == S_INSTALLING || st.screen == S_FAILED) && !st.confirmStop;
    if (show) {
        float s = scaleOf();
        float top = st.screen == S_INSTALLING ? 140.f : 0.f;
        if (st.screen == S_INSTALLING) {
            Style big{ANTON, 56};
            float h1 = measure(L"X", H1).height;
            float ph = measure(L"This takes a while the first time, up to an hour on slower PCs. You can leave it running.", P16,
                               940 - measure(L"100%", big).width - 20).height;
            top += std::max(h1 + 6 + ph, measure(L"0%", big).height * 0.86f) + 26 + 14 + 12 + 26;
            float bottom = 544 - 20 - 16;
            SetWindowPos(logView, nullptr, (int)((80 + 14) * s), (int)((top + 10) * s), (int)((940 - 28) * s),
                         (int)((bottom - top - 20) * s), SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            // The failure card's height depends on the message; it is measured the way drawFailed does.
            Style title{ANTON, 26}, msg{BODY, 16, DWRITE_FONT_WEIGHT_NORMAL, 0, 1.5f}, fine{BODY, 14};
            float cw = 940 - 56 - 64 - 24;
            std::wstring headline = st.exitCode == 6 ? L"YOU STOPPED SETUP" : L"SETUP COULDN'T FINISH";
            std::wstring message = st.exitCode == 6 ? L"Completed steps are kept. Try again to continue where it stopped." : st.failure;
            float column = measure(headline, title, cw).height + 8 + std::min(110.f, measure(message, msg, cw).height) + 8 + measure(L"X", fine).height;
            top = 140 + measure(L"X", H1).height + 22 + std::max(64.f, column) + 52 + 22;
            float bottom = 544 - 60;
            SetWindowPos(logView, nullptr, (int)((80 + 14) * s), (int)((top + 10) * s), (int)((940 - 28) * s),
                         (int)((bottom - top - 20) * s), SWP_NOZORDER | SWP_NOACTIVATE);
        }
        static HFONT font = nullptr; static UINT fontDpi = 0;
        if (fontDpi != dpi) {
            if (font) DeleteObject(font);
            font = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
            fontDpi = dpi;
            SendMessageW(logView, WM_SETFONT, (WPARAM)font, TRUE);
        }
    }
    ShowWindow(logView, show ? SW_SHOWNA : SW_HIDE);
}

// ---- Input --------------------------------------------------------------------------------------
void openUrl(const wchar_t* url) { ShellExecuteW(window, L"open", url, nullptr, nullptr, SW_SHOWNORMAL); }

void play() {
    auto install = st.update ? g_update_install : fs::path(st.plan.installDir);
    ShellExecuteW(window, L"open", (install / L"DefJamLauncher.exe").c_str(), L"--launch", install.c_str(), SW_SHOWNORMAL);
    DestroyWindow(window);
}

void requestStop() {
    if (st.screen == S_INSTALLING && !st.stopping) { st.confirmStop = true; layoutLogView(); redraw(); }
}

void act(int id, int arg) {
    Screen before = st.screen;
    switch (id) {
        case A_BROWSE: { auto p = pick(false); if (!p.empty()) identify(p); break; }
        case L_FOLDER: case A_BROWSE_FOLDER: { auto p = pick(true); if (!p.empty()) identify(p); break; }
        case A_USE_FOUND: if (!st.found.empty()) {
            const auto& f = st.found[std::min(st.foundIndex, st.found.size() - 1)];
            ++identityToken; st.identifying = false; st.id = f; st.id.chosen = f.chosen.empty() ? f.path : f.chosen;
            st.plan = Plan{}; st.screen = S_IDENTITY;
        } break;
        case L_USE_OTHER: if (auto other = goodFound()) {
            ++identityToken; st.identifying = false; st.id = *other; st.plan = Plan{}; st.screen = S_IDENTITY;
        } break;
        case A_FOUND_PREV: if (!st.found.empty()) st.foundIndex = (st.foundIndex + st.found.size() - 1) % st.found.size(); break;
        case A_FOUND_NEXT: if (!st.found.empty()) st.foundIndex = (st.foundIndex + 1) % st.found.size(); break;
        case A_CHOOSE_ANOTHER: st.screen = S_GAME; break;
        case A_NEXT_GAME: case A_NEXT_FOLDER:
            if (st.id.valid && st.id.ok && !st.identifying) {
                st.screen = S_FOLDER;
                if (!st.plan.valid && !st.planning) planFor(L"");
            }
            break;
        case A_BACK: st.screen = S_IDENTITY; break;
        case A_CHANGE: if (!st.planning) { auto p = pick(true, st.plan.folder); if (!p.empty()) planFor(p); } break;
        case A_HD: st.hd = !st.hd; if (st.plan.valid || st.planning) planFor(st.plan.valid ? st.plan.folder : L""); break;
        case A_SHORTCUT: st.shortcut = !st.shortcut; break;
        case A_PREREQ: st.prereq = !st.prereq; break;
        case A_INSTALL: if (st.plan.valid && st.plan.ok && !st.planning) startInstall(); break;
        case L_DETAILS: case A_DETAILS: st.details = !st.details; tailLog(); layoutLogView(); break;
        case A_CANCEL: requestStop(); break;
        case A_KEEP: st.confirmStop = false; layoutLogView(); break;
        case A_STOP: {
            st.confirmStop = false; st.stopping = true;
            std::ofstream marker(cancelPath); marker << "cancel";
            st.statusText = L"Stopping after the current step\u2026";
            layoutLogView();
            break;
        }
        case A_PLAY: play(); return;
        case A_RETRY:
            if (st.exitCode == 3 && !st.update) st.prereq = true;      // "Turn on and try again"
            if (st.update || (st.plan.valid && st.id.ok)) startInstall();
            break;
        case A_OPEN_LOG: {
            auto p = logPath.empty() ? localAppData() / L"DefJamSetup/logs" : logPath;
            ShellExecuteW(window, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case A_CLOSE: DestroyWindow(window); return;
        case L_DISCORD: openUrl(DISCORD); break;
        case L_GUIDE: openUrl(DUMP_GUIDE); break;
    }
    (void)arg;
    if (st.screen != before) focusIndex = -1;
    redraw();
}

const Hit* hitAt(float x, float y) {
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        if (it->id >= 0 && x >= it->r.left && x < it->r.right && y >= it->r.top && y < it->r.bottom) return &*it;
    return nullptr;
}

int primaryAction() {
    switch (st.screen) {
        case S_GAME: return A_NEXT_GAME;
        case S_IDENTITY: return st.id.ok ? A_NEXT_FOLDER : A_CHOOSE_ANOTHER;
        case S_FOLDER: return A_INSTALL;
        case S_DONE: return A_PLAY;
        case S_FAILED: return A_RETRY;
        default: return 0;
    }
}

void enableDarkFrame(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    COLORREF caption = RGB(11, 7, 8), textColor = RGB(242, 230, 200);
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &textColor, sizeof(textColor));
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); paintWindow(); EndPaint(hwnd, &ps); return 0; }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            if (windowTarget) windowTarget->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            return 0;
        case WM_DPICHANGED: {
            dpi = HIWORD(wp);
            auto r = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (windowTarget) windowTarget->SetDpi((float)dpi, (float)dpi);
            layoutLogView();
            redraw();
            return 0;
        }
        case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: {
            static HBRUSH dark = CreateSolidBrush(RGB(18, 13, 15));
            SetBkColor((HDC)wp, RGB(18, 13, 15));
            SetTextColor((HDC)wp, RGB(222, 210, 184));
            return (LRESULT)dark;
        }
        case WM_MOUSEMOVE: {
            float s = scaleOf();
            auto h = hitAt(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
            int id = h ? h->id : -1, arg = h ? h->arg : 0;
            if (id != hoverId || arg != hoverArg) { hoverId = id; hoverArg = arg; redraw(); }
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t);
            return 0;
        }
        case WM_MOUSELEAVE: hoverId = -1; redraw(); return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(nullptr, hoverId >= 0 ? IDC_HAND : IDC_ARROW)); return TRUE; }
            break;
        case WM_LBUTTONUP: {
            float s = scaleOf();
            if (auto h = hitAt(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s)) { focusIndex = -1; act(h->id, h->arg); }
            return 0;
        }
        case WM_KEYDOWN: {
            std::vector<const Hit*> focusable;
            for (const auto& h : hits) if (h.id >= 0) focusable.push_back(&h);
            if (wp == VK_TAB && !focusable.empty()) {
                int n = (int)focusable.size();
                focusIndex = GetKeyState(VK_SHIFT) < 0 ? (focusIndex <= 0 ? n - 1 : focusIndex - 1) : (focusIndex + 1) % n;
                redraw();
            } else if (wp == VK_RETURN || wp == VK_SPACE) {
                if (focusIndex >= 0 && focusIndex < (int)focusable.size()) act(focusable[focusIndex]->id, focusable[focusIndex]->arg);
                else if (wp == VK_RETURN) {
                    if (st.confirmStop) act(A_KEEP, 0);
                    else if (int id = primaryAction()) act(id, 0);
                }
            } else if (wp == VK_ESCAPE) {
                if (st.confirmStop) act(A_KEEP, 0);
                else if (st.screen == S_INSTALLING) requestStop();
            }
            return 0;
        }
        case WM_DROPFILES: {
            wchar_t file[32768];
            if (DragQueryFileW((HDROP)wp, 0, file, 32768) && (st.screen == S_GAME || st.screen == S_IDENTITY)) identify(file);
            DragFinish((HDROP)wp);
            return 0;
        }
        case WM_TIMER:
            if (wp == 1) {
                if (!IsIconic(hwnd)) redraw();
                return 0;
            }
            if (message == WM_TIMER && wp == 2) {
                ++pollTicks;
                if (st.details) tailLog();
                if (!worker) return 0;
                {
                    std::string text_;
                    if (readShared(statusPath, text_) && !text_.empty() && !st.stopping) st.statusText = utf8(text_);
                }
                {
                    std::string text_;
                    int permille = readShared(statusPath.wstring() + L".progress", text_) && !text_.empty() ? atoi(text_.c_str()) : -1;
                    if (permille >= 0 && permille <= 1000) st.permille = permille;
                    if (readShared(statusPath.wstring() + L".stage", text_) && !text_.empty()) {
                        // "<index> <total> <key> <name>"
                        size_t a = text_.find(' '), b = a == std::string::npos ? a : text_.find(' ', a + 1);
                        size_t c = b == std::string::npos ? b : text_.find(' ', b + 1);
                        if (c != std::string::npos) st.stageKey = text_.substr(b + 1, c - b - 1);
                    }
                }
                updateEstimate();
                if (WaitForSingleObject(worker, 0) == WAIT_OBJECT_0) {
                    DWORD code; GetExitCodeProcess(worker, &code); CloseHandle(worker); worker = nullptr;
                    if (preparationSmoke) {
                        std::ifstream in(logPath); std::string log((std::istreambuf_iterator<char>(in)), {});
                        smokeResult = code == 2 && pollTicks >= 3 && log.find("separate, non-nested") != std::string::npos ? 0 : 4;
                        DestroyWindow(hwnd); return 0;
                    }
                    finishInstall(code);
                }
                return 0;
            }
            break;
        case WM_PAYLOAD:
            st.payloadReady = wp != 0;
            if (!st.payloadReady) {
                auto error = reinterpret_cast<std::wstring*>(lp);
                st.payloadFailed = true; st.searching = false; st.failure = *error; delete error;
                st.screen = S_FAILED; st.exitCode = 4;
            }
            redraw();
            return 0;
        case WM_FOUND: {
            std::unique_ptr<Answer> answer((Answer*)lp);
            st.searching = false;
            if (answer->code == 0) {
                auto j = parseJson(answer->text);
                st.found.clear();
                for (const auto& g : j["games"].items) st.found.push_back(Verdict::from(g));
                st.foundIndex = 0;
            }
            redraw();
            return 0;
        }
        case WM_IDENTITY: {
            std::unique_ptr<Answer> answer((Answer*)lp);
            if (answer->token != identityToken) return 0;
            st.identifying = false;
            auto chosen = st.id.chosen;
            if (answer->code == 0) st.id = Verdict::from(parseJson(answer->text));
            else {
                st.id = Verdict{}; st.id.valid = true; st.id.kind = L"unknown";
                st.id.headline = L"SETUP COULDN'T CHECK THIS FILE";
                st.id.message = L"Something went wrong while reading it. The setup log has the details.";
                st.id.advice = L"Choose the file again, or ask in our Discord.";
            }
            if (st.id.chosen.empty()) st.id.chosen = chosen;
            st.screen = S_IDENTITY;
            redraw();
            return 0;
        }
        case WM_PLAN: {
            std::unique_ptr<Answer> answer((Answer*)lp);
            if (answer->token != planToken) return 0;
            st.planning = false;
            Plan p;
            if (answer->code == 0) {
                auto j = parseJson(answer->text);
                p.valid = true; p.ok = j["ok"].yes(); p.adjusted = j["adjusted"].yes(); p.existing = j["existing"].yes();
                p.folder = j["folder"].str(); p.installDir = j["install_dir"].str(); p.dataDir = j["data_dir"].str();
                for (const auto& c : j["checks"].items) p.checks.push_back({c["id"].str(), c["text"].str(), c["fix"].str(), c["ok"].yes()});
            } else {
                p.valid = true; p.ok = false;
                p.checks.push_back({L"error", L"Setup couldn't check this folder", L"Choose another folder, or see the setup log.", false});
            }
            st.plan = p;
            redraw();
            return 0;
        }
        case WM_SETUP_READY:
            preparationTicks = pollTicks; worker = (HANDLE)wp;
            if (st.stopping) { std::ofstream marker(cancelPath); marker << "cancel"; }
            return 0;
        case WM_SETUP_FAILED: {
            auto error = reinterpret_cast<std::wstring*>(lp);
            nativeLog("Setup stopped before the engine started.");
            KillTimer(hwnd, 2);
            if (preparationSmoke) { delete error; DestroyWindow(hwnd); return 0; }
            st.exitCode = (DWORD)wp; st.failure = *error; delete error;
            st.screen = S_FAILED; layoutLogView(); redraw();
            return 0;
        }
        case WM_TRAY:
            if (LOWORD(lp) == NIN_BALLOONUSERCLICK || LOWORD(lp) == WM_LBUTTONUP) { ShowWindow(hwnd, SW_RESTORE); SetForegroundWindow(hwnd); }
            return 0;
        case WM_CLOSE:
            // While installing (or still preparing to), closing asks first: an engine must never run on unseen.
            if (st.screen == S_INSTALLING && !st.stopping) { requestStop(); return 0; }
            if (st.screen == S_INSTALLING && st.stopping) return 0;     // it closes once the engine has stopped
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (notified) { NOTIFYICONDATAW icon{sizeof(icon)}; icon.hWnd = hwnd; icon.uID = 1; Shell_NotifyIconW(NIM_DELETE, &icon); }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

// ---- Sample states for --ui-snapshot (review images; nothing is read from the PC) ---------------
Verdict sampleFound(bool ok) {
    Verdict v; v.valid = true; v.ok = ok; v.kind = ok ? L"ok" : L"pal";
    v.name = ok ? L"Def Jam - Fight for NY (USA).iso" : L"Def Jam - Fight for NY (Europe).iso";
    v.folder = L"D:\\Games"; v.path = v.chosen = v.folder + L"\\" + v.name; v.size = 7.3 * 1024 * 1024 * 1024;
    v.headline = ok ? L"DEF JAM: FIGHT FOR NY" : L"THIS IS THE EUROPEAN (PAL) VERSION";
    v.message = ok ? L"Xbox \u00b7 USA \u00b7 complete and undamaged"
                   : L"FFNY Online needs the USA Xbox version. Everyone online plays the same game data, so other versions can't connect.";
    v.advice = L"Pick the USA Xbox version if you have it.";
    return v;
}

Plan samplePlan(bool good) {
    Plan p; p.valid = true; p.ok = good;
    p.folder = good ? L"C:\\Games\\FFNY Online" : L"C:\\Users\\Player\\OneDrive\\Games\\FFNY Online";
    p.installDir = p.folder + L"\\app"; p.dataDir = p.folder + L"\\data";
    p.checks.push_back({L"space", good ? L"Enough free space on this drive (412 GB free, needs about 35 GB)"
                                       : L"Enough free space on this drive (96 GB free, needs about 35 GB)", L"", true});
    p.checks.push_back({L"write", L"Setup can write here, no admin rights needed", L"", true});
    if (good) p.checks.push_back({L"place", L"Not in OneDrive, Program Files or a system folder", L"", true});
    else p.checks.push_back({L"place", L"This folder is in OneDrive, which would upload the 20 GB or more the build writes",
                             L"Choose a folder outside it, for example D:\\Games\\FFNY Online.", false});
    p.checks.push_back({L"dump", L"Your game file stays outside this folder", L"", true});
    return p;
}

bool sampleState(const std::wstring& name) {
    st = State{};
    st.payloadReady = true; st.searching = false;
    st.found = {sampleFound(true)};
    if (name == L"game" || name == L"main") { st.screen = S_GAME; return true; }
    if (name == L"game-searching") { st.screen = S_GAME; st.found.clear(); st.searching = true; return true; }
    if (name == L"game-empty") { st.screen = S_GAME; st.found.clear(); return true; }
    if (name == L"game-several") { st.screen = S_GAME; st.found = {sampleFound(true), sampleFound(false)}; st.foundIndex = 1; return true; }
    if (name == L"checking") { st.screen = S_IDENTITY; st.identifying = true; st.id.chosen = sampleFound(true).path; return true; }
    if (name == L"recognized") { st.screen = S_IDENTITY; st.id = sampleFound(true); return true; }
    if (name == L"wrong" || name == L"wrong-pal") { st.found.clear(); st.screen = S_IDENTITY; st.id = sampleFound(false); return true; }
    if (name == L"wrong-pal-found") { st.screen = S_IDENTITY; st.id = sampleFound(false); return true; }
    if (name == L"wrong-ps2" || name == L"wrong-generic" || name == L"wrong-gamecube") {
        st.found.clear(); st.screen = S_IDENTITY; st.id = sampleFound(false);
        if (name == L"wrong-ps2") {
            st.id.kind = L"ps2"; st.id.headline = L"THIS IS A PLAYSTATION 2 DISC IMAGE";
            st.id.message = L"FFNY Online is built from the Xbox version (USA) only."; st.id.chosen = L"D:\\Games\\Def Jam FFNY (PS2).iso";
        } else if (name == L"wrong-gamecube") {
            st.id.kind = L"gamecube"; st.id.headline = L"THIS IS THE GAMECUBE VERSION";
            st.id.message = L"FFNY Online is built from the Xbox version (USA) only."; st.id.chosen = L"D:\\Games\\Def Jam FFNY.iso";
        } else {
            st.id.kind = L"unknown"; st.id.headline = L"THIS ISN'T THE USA XBOX VERSION";
            st.id.message = L"Setup couldn't recognise it as the USA Xbox version of Def Jam: Fight for NY.";
            st.id.advice = L"Pick the USA Xbox version: an .iso file, or the folder with the extracted game.";
            st.id.chosen = L"D:\\Downloads\\game.iso";
        }
        return true;
    }
    st.id = sampleFound(true);
    if (name == L"folder") { st.screen = S_FOLDER; st.plan = samplePlan(true); return true; }
    if (name == L"folder-checking") { st.screen = S_FOLDER; st.planning = true; return true; }
    if (name == L"folder-problem") { st.screen = S_FOLDER; st.plan = samplePlan(false); return true; }
    if (name == L"installing" || name == L"installing-details" || name == L"cancel-confirm") {
        st.screen = S_INSTALLING; st.plan = samplePlan(true); st.permille = 460; st.stageKey = "build";
        st.statusText = L"Build in progress (this may take several minutes)";
        st.estimate = L"About 25 minutes left in the build, then HD textures";
        st.details = name == L"installing-details"; st.confirmStop = name == L"cancel-confirm";
        return true;
    }
    if (name == L"done") { st.screen = S_DONE; st.plan = samplePlan(true); return true; }
    if (name == L"failed" || name == L"cancelled") {
        st.screen = S_FAILED; st.plan = samplePlan(true); st.exitCode = name == L"failed" ? 3 : 6;
        st.failure = L"Visual Studio Build Tools are missing; turn on \u201cMissing Windows parts\u201d in setup (or use --install-prerequisites)";
        st.prereq = false;
        if (logPath.empty()) logPath = L"C:\\Users\\Player\\AppData\\Local\\DefJamSetup\\logs\\{5C1D7E0A-2B9F-4C51-9E7A-0D3F6B8A1C24}.log";
        return true;
    }
    if (name == L"wine") { st.screen = S_WINE; return true; }
    return false;
}

const wchar_t* SNAPSHOT_STATES[] = {L"game", L"game-searching", L"game-empty", L"game-several", L"checking", L"recognized",
    L"wrong-pal", L"wrong-pal-found", L"wrong-ps2", L"wrong-gamecube", L"wrong-generic", L"folder", L"folder-checking",
    L"folder-problem", L"installing", L"installing-details", L"cancel-confirm", L"done", L"failed", L"cancelled", L"wine"};

HWND createWindow(HINSTANCE instance, bool visible) {
    WNDCLASSW wc{}; wc.lpfnWndProc = windowProc; wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = nullptr;
    wc.hIcon = LoadIcon(instance, MAKEINTRESOURCE(1));  // setup/app.rc.in
    wc.lpszClassName = L"FFNYOnlineSetup"; RegisterClassW(&wc);
    // The window opens on the monitor under the mouse, at that monitor's scale.
    POINT cursor; GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor, &info);
    using DpiForMonitor = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    UINT dx = 96, dy = 96;
    if (auto shcore = LoadLibraryW(L"shcore.dll"))
        if (auto f = (DpiForMonitor)GetProcAddress(shcore, "GetDpiForMonitor")) f(monitor, 0, &dx, &dy);
    dpi = dx ? dx : 96;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT r{0, 0, (LONG)(W * dpi / 96), (LONG)(H * dpi / 96)};
    using AdjustForDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjust = (AdjustForDpi)GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
    if (adjust) adjust(&r, style, FALSE, 0, dpi); else AdjustWindowRectEx(&r, style, FALSE, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    auto& work = info.rcWork;
    int x = visible ? work.left + std::max(0, (int)(work.right - work.left - ww) / 2) : -32000;
    int y = visible ? work.top + std::max(0, (int)(work.bottom - work.top - wh) / 2) : -32000;
    window = CreateWindowExW(0, wc.lpszClassName, L"FFNY Online Setup", style, x, y, ww, wh, nullptr, nullptr, instance, nullptr);
    if (!window) return nullptr;
    using DpiForWindow = UINT(WINAPI*)(HWND);
    if (auto f = (DpiForWindow)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")) dpi = f(window);
    enableDarkFrame(window);
    DragAcceptFiles(window, TRUE);
    // Explorer can drop files on this window even when setup runs elevated.
    ChangeWindowMessageFilterEx(window, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(window, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(window, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, nullptr);
    logView = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                              0, 0, 10, 10, window, (HMENU)118, instance, nullptr);
    SendMessageW(logView, EM_SETLIMITTEXT, 2 * LOG_VIEW_CHARS, 0);
    SetWindowTheme(logView, L"DarkMode_Explorer", nullptr);
    return window;
}

void startPreparation(bool search) {
    payloadEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread([search]() {
        try {
            unpack();
            PostMessageW(window, WM_PAYLOAD, 1, 0);
            st.payloadReady = true;
        } catch (const std::exception& ex) {
            nativeLog(std::string("Setup stopped: ") + ex.what());
            st.payloadError = utf8(ex.what());
            PostMessageW(window, WM_PAYLOAD, 0, (LPARAM)new std::wstring(utf8(ex.what())));
        }
        SetEvent(payloadEvent);
    }).detach();
    if (search) ask({L"--find-games"}, WM_FOUND, 0);
}

int messageLoop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}

// --ui-demo DUMP OUT-DIR: the real flow offscreen with the real engine (payload, search, identify,
// folder plan), a picture of each step; nothing is installed.
int demo(HINSTANCE instance, const std::wstring& dump, const fs::path& out) {
    prepareSession();
    if (!createWindow(instance, false)) return 4;
    fs::create_directories(out);
    int step = 0;
    startPreparation(true);
    // Each answer moves the script on; the timer takes the pictures.
    SetTimer(window, 7, 250, nullptr);
    MSG msg;
    ULONGLONG deadline = GetTickCount64() + 180000;
    while (GetTickCount64() < deadline && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
        if (msg.message == WM_TIMER && msg.wParam == 7) {
            cv.t = 0;
            if (step == 0 && !st.searching && st.payloadReady) {
                renderOffscreen((out / L"demo-1-game.png").wstring(), 2); identify(dump); step = 1;
            } else if (step == 1 && !st.identifying) {
                renderOffscreen((out / L"demo-2-identity.png").wstring(), 2);
                if (!st.id.ok) { DestroyWindow(window); return 2; }
                act(A_NEXT_FOLDER, 0); step = 2;
            } else if (step == 2 && !st.planning && st.plan.valid) {
                renderOffscreen((out / L"demo-3-folder.png").wstring(), 2);
                nativeLog("Demo plan: " + narrow(st.plan.folder) + " install=" + narrow(st.plan.installDir) +
                          " data=" + narrow(st.plan.dataDir) + (st.plan.ok ? " ok" : " not ok"));
                DestroyWindow(window); return 0;
            }
        }
    }
    return 4;
}

}  // namespace

int wizardMain(HINSTANCE instance, const std::vector<std::wstring>& args) {
    startTick = GetTickCount64();
    std::wstring mode = args.empty() ? L"" : args[0];
    if (!initGraphics()) {
        if (mode.empty() && runningUnderWine())
            MessageBoxW(nullptr, L"This setup is running under Wine. FFNY Online supports Windows only for now.", L"FFNY Online Setup", MB_OK | MB_ICONINFORMATION);
        else MessageBoxW(nullptr, L"Setup could not start its window (Direct2D is unavailable).", L"FFNY Online Setup", MB_OK | MB_ICONERROR);
        return 4;
    }
    if (mode == L"--ui-snapshot") {
        // --ui-snapshot STATE|all OUT [--scale N]: review images of each screen, drawn offscreen.
        if (args.size() < 3) return 2;
        float scale = 2;
        for (size_t i = 3; i + 1 < args.size(); ++i) if (args[i] == L"--scale") scale = (float)_wtof(args[i + 1].c_str());
        if (args[1] == L"all") {
            fs::create_directories(args[2]);
            int index = 0;
            for (auto name : SNAPSHOT_STATES) {
                sampleState(name);
                wchar_t file[64]; swprintf(file, 64, L"%02d-%s.png", ++index, name);
                if (!renderOffscreen((fs::path(args[2]) / file).wstring(), scale)) return 4;
            }
            return 0;
        }
        if (!sampleState(args[1])) return 2;
        return renderOffscreen(args[2], scale) ? 0 : 4;
    }
    if (mode == L"--ui-shot") {
        // README pictures: the first screen, as a player sees it at 100 %.
        if (args.size() < 2) return 2;
        sampleState(L"game");
        return renderOffscreen(args[1], 1) ? 0 : 4;
    }
    if (mode == L"--ui-demo") {
        if (args.size() < 3) return 2;
        return demo(instance, args[1], args[2]);
    }
    if (mode == L"--ui-smoke") {
        // Hardware-free CI probe: the window and every screen draw, without Python, a D3D device,
        // a compiler or the payload.
        if (!createWindow(instance, false)) return 4;
        wchar_t title[128]{}; GetWindowTextW(window, title, 128);
        bool valid = wcscmp(title, L"FFNY Online Setup") == 0 && logView && !IsWindowVisible(logView);
        for (auto name : SNAPSHOT_STATES) valid = valid && sampleState(name) && renderOffscreen(L"", 1);
        // The window's own Direct2D target draws too (off the desktop, never shown).
        sampleState(L"game");
        paintWindow();
        valid = valid && windowTarget && windowDrawn;
        valid = valid && st.hd && st.shortcut && st.prereq;
        DestroyWindow(window);
        return valid ? 0 : 4;
    }
    if (mode == L"--update") {
        // From the game's launcher: --update --install-dir X --data-dir Y [--wait-pid N].
        for (size_t i = 1; i + 1 < args.size(); i += 2) {
            if (args[i] == L"--install-dir") g_update_install = args[i + 1];
            else if (args[i] == L"--data-dir") g_update_data = args[i + 1];
            else if (args[i] == L"--wait-pid") g_wait_pid = (DWORD)wcstoul(args[i + 1].c_str(), nullptr, 10);
        }
        if (g_update_install.empty() || g_update_data.empty()) throw std::runtime_error("An update needs --install-dir and --data-dir.");
        st.update = true;
    }
    preparationSmoke = mode == L"--ui-preparation-smoke";
    if (runningUnderWine() && !preparationSmoke) {
        // Nothing is unpacked or started: FFNY Online supports Windows only for now.
        if (!createWindow(instance, true)) return 3;
        st.screen = S_WINE;
        ShowWindow(window, SW_SHOW);
        SetTimer(window, 1, 50, nullptr);
        messageLoop();
        return 3;
    }
    prepareSession();
    if (!createWindow(instance, !preparationSmoke)) return 4;
    if (preparationSmoke) {
        // A dump inside the install location: the engine must refuse it ("separate, non-nested"),
        // while the window keeps ticking during preparation.
        auto probe = localAppData() / L"DefJamSetup/probe";
        st.id.valid = st.id.ok = true; st.id.path = st.id.chosen = (probe / L"app/missing.iso").wstring();
        st.plan.valid = st.plan.ok = true; st.plan.folder = probe.wstring();
        st.plan.installDir = (probe / L"app").wstring(); st.plan.dataDir = (probe / L"data").wstring();
        st.hd = false;
        startPreparation(false);
        startInstall();
        messageLoop();
        return smokeResult;
    }
    startPreparation(!st.update);
    if (st.update) startInstall();
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    // Embers move at 30 frames a second, at 12 while the game builds (the PC is busy then).
    SetTimer(window, 1, st.update ? 80 : 33, nullptr);
    messageLoop();
    // A failed install keeps its session folder for diagnosis (wWinMain removes it only after 0).
    return st.screen == S_FAILED ? (st.exitCode ? (int)st.exitCode : 4) : 0;
}
