// TURF WAR's map page (Turf Map v2, the owner's approved design: logs/social-design/11-TurfMap-v2.png). Included
// into turf_ui.inc before draw_turf. A stylised New York in its own 1600x960 design space, fitted to the window
// (the water fills the rest): the five boroughs tinted by the crew holding most venues there, a pin and name
// plate per venue, the challenges (dashed yellow), the front lines where battles are (red), a turf battle under
// way (LIVE), the season, the standings, the player's crew and the street wire. LB / RB fly to a borough.
//
// Everything is drawn with the draw list; the map's shapes and places are the design's own coordinates.
// The look: the titles in the design's bold serif (the game's own AachenStd-Bold), the countdown in Anton
// (OFL, ffny-branding/fonts); a crew's borough in its brushed, grainy paint (a texture made once) with a
// soft outer glow and its sprayed tag; open turf breathing, so a map with every venue open still invites.

// ---- the design's palette ----

const ImU32 TM_PAL[8] = { IM_COL32(255, 46, 77, 255), IM_COL32(79, 224, 176, 255), IM_COL32(90, 170, 255, 255),
                          IM_COL32(255, 168, 26, 255), IM_COL32(185, 140, 255, 255), IM_COL32(255, 122, 200, 255),
                          IM_COL32(180, 224, 90, 255), IM_COL32(60, 216, 232, 255) };
const ImU32 TM_CREAM = IM_COL32(242, 230, 200, 255), TM_INK = IM_COL32(10, 6, 6, 255), TM_YELLOW = IM_COL32(255, 205, 60, 255),
            TM_LIVE = IM_COL32(255, 59, 84, 255), TM_OPEN = IM_COL32(154, 150, 166, 255), TM_ORANGE = IM_COL32(255, 168, 26, 255),
            TM_RED = IM_COL32(200, 16, 46, 255), TM_OPEN_LIT = IM_COL32(222, 214, 196, 255);

ImFont *tm_serif();     // the titles' serif (below)

ImU32 tm_rgba(int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)(255 * std::max(0.0f, std::min(1.0f, a)))); }

// a colour toward white by k (0..1)
ImU32 tm_light(ImU32 c, float k)
{
    int r = (c >> IM_COL32_R_SHIFT) & 0xFF, g = (c >> IM_COL32_G_SHIFT) & 0xFF, b = (c >> IM_COL32_B_SHIFT) & 0xFF;
    r += (int)((255 - r) * k); g += (int)((255 - g) * k); b += (int)((255 - b) * k);
    return IM_COL32(r, g, b, (c >> IM_COL32_A_SHIFT) & 0xFF);
}

// A crew's colour: the service's tint for it (tint= lines), else one from its id.
ImU32 tm_crew(const TurfMap &m, long long crew)
{
    if (!crew) return TM_OPEN;
    auto it = m.tints.find(crew);
    int t = it != m.tints.end() ? it->second : (int)(crew % 8);
    return TM_PAL[((t % 8) + 8) % 8];
}

// ---- the city (world = the design's 1600x960 at the "all five boroughs" view) ----

const ImVec2 TM_BRONX[] = { {690, 205}, {698, 140}, {706, 60}, {714, -60}, {1030, -60}, {1040, 70}, {1005, 150}, {945, 212},
                            {862, 242}, {770, 248}, {712, 236} };
const ImVec2 TM_MANH[] = { {478, 770}, {462, 730}, {472, 660}, {495, 560}, {528, 460}, {560, 370}, {595, 290}, {628, 215},
                           {650, 196}, {668, 212}, {664, 250}, {645, 310}, {618, 390}, {590, 470}, {565, 560}, {545, 650},
                           {525, 720}, {505, 765} };
const ImVec2 TM_QUEENS[] = { {705, 292}, {770, 278}, {880, 262}, {1000, 252}, {1120, 262}, {1600, 244}, {2100, 244}, {2100, 640},
                             {1600, 640}, {1100, 630}, {980, 655}, {880, 640}, {780, 670}, {700, 650}, {604, 662}, {612, 620},
                             {632, 545}, {655, 465}, {680, 380} };
const ImVec2 TM_BROOKLYN[] = { {604, 662}, {700, 650}, {780, 670}, {880, 640}, {980, 655}, {1100, 630}, {1600, 640}, {2100, 640},
                               {2100, 1160}, {700, 1160}, {640, 912}, {598, 860}, {578, 805}, {600, 768}, {585, 728}, {600, 680} };
const ImVec2 TM_SI[] = { {190, 640}, {280, 616}, {370, 626}, {430, 664}, {424, 734}, {372, 790}, {250, 802}, {176, 762}, {160, 700} };
const ImVec2 TM_NJ[] = { {-500, -60}, {606, -60}, {604, 0}, {592, 120}, {562, 240}, {522, 352}, {488, 462}, {458, 562}, {436, 596},
                         {330, 598}, {250, 600}, {150, 630}, {128, 700}, {146, 780}, {120, 1160}, {-500, 1160} };
const ImVec2 TM_PARK[] = { {560, 372}, {590, 300}, {606, 304}, {577, 376} };

struct TmBorough {
    const char *key, *label;
    const ImVec2 *pts;
    int n;
    float lx, ly, lsize, lrot;      // the dim stencil name
    float tx, ty, tsize;            // the sprayed tag of its crew
    float vx0, vy0, vx1, vy1;       // what LB / RB frame of it
};
const TmBorough TM_BOROUGHS[5] = {
    { "bronx", "BRONX", TM_BRONX, 11, 900, 205, 22, 0, 900, 92, 120, 640, -10, 1070, 270 },
    { "manhattan", "MANHATTAN", TM_MANH, 18, 396, 470, 21, -68, 540, 340, 58, 400, 180, 720, 790 },
    { "queens", "QUEENS", TM_QUEENS, 19, 960, 282, 22, 0, 830, 480, 110, 600, 240, 1080, 630 },
    { "brooklyn", "BROOKLYN", TM_BROOKLYN, 16, 965, 676, 22, 0, 910, 770, 120, 470, 620, 1090, 860 },
    { "staten_island", "STATEN ISLAND", TM_SI, 9, 196, 806, 18, 0, 280, 700, 76, 130, 590, 470, 830 },
};
int tm_borough_of(const std::string &key)
{
    for (int i = 0; i < 5; i++) if (key == TM_BOROUGHS[i].key) return i;
    return -1;
}

// Where each venue (feID) is: the design's pins, and the six venues the service placed later.
struct TmPin { int id; float x, y; bool left; };
const TmPin TM_PINS[] = {
    { 17, 792, 56, false }, { 14, 1000, 104, true }, { 6, 752, 170, false }, { 2, 722, 96, true },
    { 18, 620, 252, true }, { 11, 584, 345, true }, { 26, 548, 470, true }, { 4, 520, 600, true }, { 8, 494, 706, false },
    { 21, 762, 334, false }, { 16, 820, 400, false }, { 1, 990, 478, true }, { 7, 684, 566, false },
    { 10, 750, 694, false }, { 12, 1060, 746, true }, { 24, 652, 808, false }, { 13, 900, 812, false },
    { 19, 262, 690, false },
};

// A venue's place: its pin, or (a venue this build does not know) a spot in its borough.
TmPin tm_pin(const TurfVenue &v, int index)
{
    for (const TmPin &p : TM_PINS) if (p.id == v.id) return p;
    int b = tm_borough_of(v.borough);
    float x = b >= 0 ? TM_BOROUGHS[b].tx : 300, y = b >= 0 ? TM_BOROUGHS[b].ty : 300;
    return { v.id, x + (float)((index * 37) % 90) - 45, y + (float)((index * 53) % 70) - 35, false };
}

// The borders between boroughs (on the water between them), where a front line runs.
struct TmBorder { int a, b; const ImVec2 *pts; int n; };
const ImVec2 TM_B_BXMN[] = { {676, 300}, {686, 250}, {702, 205}, {716, 160} };
const ImVec2 TM_B_BXQN[] = { {712, 262}, {790, 262}, {880, 252}, {990, 234} };
const ImVec2 TM_B_MNQN[] = { {676, 262}, {655, 320}, {628, 400}, {602, 475}, {580, 555}, {562, 630} };
const ImVec2 TM_B_MNBK[] = { {562, 650}, {546, 705}, {528, 760} };
const ImVec2 TM_B_QNBK[] = { {598, 662}, {700, 650}, {780, 670}, {880, 640}, {980, 655}, {1090, 632} };
const ImVec2 TM_B_BKSI[] = { {430, 712}, {512, 740}, {594, 762} };
const TmBorder TM_BORDERS[] = { { 0, 1, TM_B_BXMN, 4 }, { 0, 2, TM_B_BXQN, 4 }, { 1, 2, TM_B_MNQN, 6 }, { 1, 3, TM_B_MNBK, 3 },
                                { 2, 3, TM_B_QNBK, 6 }, { 3, 4, TM_B_BKSI, 3 } };

// ---- the view: the design frame and the camera ----

struct TmView {
    Frame D;                        // the design's 1600x960 in the window
    float cx = 800, cy = 480, z = 1, mx = 800, my = 480;
    ImVec2 d(float x, float y) const { return ImVec2(mx + (x - cx) * z, my + (y - cy) * z); }     // world -> design
    ImVec2 w(float x, float y) const { ImVec2 q = d(x, y); return D.p(q.x, q.y); }               // world -> pixels
    ImVec2 w(ImVec2 p) const { return w(p.x, p.y); }
    float k() const { return D.s() * z; }       // pixels per world unit
};

Frame tm_frame(ImVec2 display)
{
    Frame d;
    float s = std::min(display.x / 1600.0f, display.y / 960.0f);
    if (s <= 0) s = 1;
    d.sx = d.sy = s;
    d.o = ImVec2((display.x - 1600 * s) / 2, (display.y - 960 * s) / 2);
    return d;
}

// the camera's goal for a region (0 all, 1..5 a borough): the borough close up in the map's part of the screen
// (left of the panels, above the street wire), panned to keep the picked venue (`at`, world) in view
const float TM_ZOOM[5] = { 2.0f, 1.75f, 1.8f, 1.9f, 2.3f };
void tm_goal(int region, const ImVec2 *at, float &cx, float &cy, float &z, float &mx, float &my)
{
    if (region <= 0 || region > 5) { cx = 800; cy = 480; z = 1; mx = 800; my = 480; return; }
    const TmBorough &b = TM_BOROUGHS[region - 1];
    z = TM_ZOOM[region - 1];
    mx = 555;
    my = 430;
    cx = (b.vx0 + b.vx1) / 2;
    cy = (b.vy0 + b.vy1) / 2;
    float hw = 470 / z, hh = 360 / z;       // what the map's part shows, half of it, in world units
    if (at) {
        if (b.vx1 - b.vx0 > 2 * hw) cx = std::max(b.vx0 + hw, std::min(b.vx1 - hw, at->x));
        if (b.vy1 - b.vy0 > 2 * hh) cy = std::max(b.vy0 + hh, std::min(b.vy1 - hh, at->y));
    }
}

// ---- drawing helpers (pixels) ----

ImVec2 tm_uv(ImDrawList *dl) { return dl->_Data->TexUvWhitePixel; }

// A polygon's triangles (concave too, ear-clipped), as indices into its points.
void tm_triangulate(const ImVec2 *p, int n, std::vector<int> &tris)
{
    tris.clear();
    if (n < 3) return;
    float area = 0;
    for (int i = 0; i < n; i++) { const ImVec2 &a = p[i], &b = p[(i + 1) % n]; area += a.x * b.y - b.x * a.y; }
    std::vector<int> idx(n);
    for (int i = 0; i < n; i++) idx[i] = area >= 0 ? i : n - 1 - i;
    auto cross = [](ImVec2 a, ImVec2 b, ImVec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
    int guard = 0;
    while (idx.size() > 3 && guard++ < 4 * n) {
        bool cut = false;
        for (size_t i = 0; i < idx.size(); i++) {
            int i0 = idx[(i + idx.size() - 1) % idx.size()], i1 = idx[i], i2 = idx[(i + 1) % idx.size()];
            ImVec2 a = p[i0], b = p[i1], c = p[i2];
            if (cross(a, b, c) <= 0) continue;          // a reflex corner
            bool inside = false;
            for (int j : idx) {
                if (j == i0 || j == i1 || j == i2) continue;
                if (cross(a, b, p[j]) >= 0 && cross(b, c, p[j]) >= 0 && cross(c, a, p[j]) >= 0) { inside = true; break; }
            }
            if (inside) continue;
            tris.push_back(i0); tris.push_back(i1); tris.push_back(i2);
            idx.erase(idx.begin() + i);
            cut = true;
            break;
        }
        if (!cut) break;
    }
    if (idx.size() == 3) { tris.push_back(idx[0]); tris.push_back(idx[1]); tris.push_back(idx[2]); }
}

// The triangles of one of the map's fixed shapes (world points), worked out once: the view only moves and
// scales the map, which keeps them.
const std::vector<int> &tm_tris_of(const ImVec2 *world, int n)
{
    static std::map<const ImVec2 *, std::vector<int>> cache;
    auto it = cache.find(world);
    if (it != cache.end()) return it->second;
    std::vector<int> &t = cache[world];
    tm_triangulate(world, n, t);
    return t;
}

void tm_fill_tris(ImDrawList *dl, const ImVec2 *p, int n, const std::vector<int> &tris, ImU32 col)
{
    if (tris.empty() || !(col & IM_COL32_A_MASK)) return;
    ImVec2 uv = tm_uv(dl);
    dl->PrimReserve((int)tris.size(), n);
    unsigned base = dl->_VtxCurrentIdx;
    for (int i = 0; i < n; i++) dl->PrimWriteVtx(p[i], uv, col);
    for (int t : tris) dl->PrimWriteIdx((ImDrawIdx)(base + t));
}

// A filled polygon (concave too), without the anti-aliased fringe (its outline is drawn apart).
void tm_fill(ImDrawList *dl, const ImVec2 *p, int n, ImU32 col)
{
    if (n < 3 || !(col & IM_COL32_A_MASK)) return;
    std::vector<int> tris;
    tm_triangulate(p, n, tris);
    tm_fill_tris(dl, p, n, tris, col);
}

// The paint on the map: white, its alpha a mottled wash (value noise, three octaves) with dry brush
// streaks and a fine grain. Made once (256 x 256), stretched over a borough and tinted with its crew's colour.
Tex g_tm_paint;
const Tex &tm_paint()
{
    static bool tried = false;
    if (tried || !g_dev) return g_tm_paint;
    tried = true;
    const int N = 256;
    std::vector<float> a((size_t)N * N, 0.0f);
    unsigned seed = 0x7E4F11u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f; };
    const int cells[3] = { 5, 11, 23 };
    const float amp[3] = { 0.55f, 0.28f, 0.17f };
    for (int o = 0; o < 3; o++) {
        int G = cells[o];
        std::vector<float> g((size_t)(G + 1) * (G + 1));
        for (float &e : g) e = rnd();
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                float fx = (float)x * G / N, fy = (float)y * G / N;
                int ix = (int)fx, iy = (int)fy;
                float tx = fx - ix, ty = fy - iy;
                tx = tx * tx * (3 - 2 * tx);
                ty = ty * ty * (3 - 2 * ty);
                float v0 = g[iy * (G + 1) + ix] + (g[iy * (G + 1) + ix + 1] - g[iy * (G + 1) + ix]) * tx;
                float v1 = g[(iy + 1) * (G + 1) + ix] + (g[(iy + 1) * (G + 1) + ix + 1] - g[(iy + 1) * (G + 1) + ix]) * tx;
                a[(size_t)y * N + x] += amp[o] * (v0 + (v1 - v0) * ty);
            }
    }
    // the brush: long thin streaks at about -20 degrees, some darker, some lighter
    const float ca = cosf(-0.35f), sa = sinf(-0.35f);
    for (int k = 0; k < 46; k++) {
        float cx = rnd() * N, cy = rnd() * N, len = 30 + 90 * rnd(), wid = 1.5f + 5 * rnd(), d = (rnd() < 0.55f ? -0.30f : 0.26f) * (0.5f + rnd());
        int x0 = std::max(0, (int)(cx - len)), x1 = std::min(N - 1, (int)(cx + len));
        int y0 = std::max(0, (int)(cy - len)), y1 = std::min(N - 1, (int)(cy + len));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float dx = x - cx, dy = y - cy, u = dx * ca + dy * sa, w = -dx * sa + dy * ca;
                if (fabsf(u) > len || fabsf(w) > wid) continue;
                float fall = (1 - fabsf(u) / len) * (1 - fabsf(w) / wid);
                a[(size_t)y * N + x] += d * fall;
            }
    }
    std::vector<unsigned char> px((size_t)N * N * 4);
    for (int i = 0; i < N * N; i++) {
        float v = 0.18f + 0.95f * (a[i] - 0.25f) + (rnd() - 0.5f) * 0.22f;     // the grain
        v = std::max(0.0f, std::min(1.0f, v));
        px[(size_t)i * 4 + 0] = px[(size_t)i * 4 + 1] = px[(size_t)i * 4 + 2] = 255;
        px[(size_t)i * 4 + 3] = (unsigned char)(v * 255 + 0.5f);
    }
    make_texture(px, N, N, g_tm_paint);
    return g_tm_paint;
}

// A world shape filled with the paint in `col` (its alpha the paint's strength), the texture spread over
// the shape's box keeping its proportions (the overlay's sampler clamps, so it does not repeat).
void tm_paint_fill(ImDrawList *dl, const TmView &v, const ImVec2 *world, int n, ImU32 col)
{
    const Tex &t = tm_paint();
    const std::vector<int> &tris = tm_tris_of(world, n);
    if (!t.srv || tris.empty() || !(col & IM_COL32_A_MASK)) return;
    float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
    for (int i = 0; i < n; i++) { x0 = std::min(x0, world[i].x); y0 = std::min(y0, world[i].y); x1 = std::max(x1, world[i].x); y1 = std::max(y1, world[i].y); }
    float span = std::max(1.0f, std::max(x1 - x0, y1 - y0));
    dl->PushTexture(ImTextureRef((ImTextureID)(intptr_t)t.srv));
    dl->PrimReserve((int)tris.size(), n);
    unsigned base = dl->_VtxCurrentIdx;
    for (int i = 0; i < n; i++)
        dl->PrimWriteVtx(v.w(world[i]), ImVec2((world[i].x - x0) / span, (world[i].y - y0) / span), col);
    for (int k : tris) dl->PrimWriteIdx((ImDrawIdx)(base + k));
    dl->PopTexture();
}

// The film grain over the map (the online pages' own, a texel a pixel, tiled), faint.
void tm_grain(ImDrawList *dl, ImVec2 a, ImVec2 b, int alpha)
{
    if (!g_grain.srv) return;
    for (float y = a.y; y < b.y; y += g_grain.h)
        for (float x = a.x; x < b.x; x += g_grain.w)
            dl->AddImage((ImTextureID)(intptr_t)g_grain.srv, ImVec2(x, y), ImVec2(x + g_grain.w, y + g_grain.h), ImVec2(0, 0),
                         ImVec2(1, 1), IM_COL32(255, 255, 255, alpha));
}

// Two colours mixed half and half (a contested borough's glow).
ImU32 tm_mix(ImU32 a, ImU32 b, float k = 0.5f)
{
    auto ch = [&](int sh) { return (int)(((a >> sh) & 0xFF) * (1 - k) + ((b >> sh) & 0xFF) * k); };
    return IM_COL32(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), ch(IM_COL32_A_SHIFT));
}

// The vertices drawn since `from` coloured from `top` to `bottom` by their height between y0 and y1 (text
// with a painted light from above), keeping each vertex's alpha.
void tm_shade(ImDrawList *dl, int from, float y0, float y1, ImU32 top, ImU32 bottom)
{
    float span = std::max(1.0f, y1 - y0);
    for (int i = from; i < dl->VtxBuffer.Size; i++) {
        ImDrawVert &q = dl->VtxBuffer[i];
        float k = std::max(0.0f, std::min(1.0f, (q.pos.y - y0) / span));
        ImU32 c = tm_mix(top, bottom, k);
        unsigned al = (q.col >> IM_COL32_A_SHIFT) & 0xFF;
        q.col = (c & ~IM_COL32_A_MASK) | (al << IM_COL32_A_SHIFT);
    }
}

// A soft round glow: `in` at the centre fading to `out` at the rim (an ellipse, turned by `rot` degrees).
void tm_glow(ImDrawList *dl, ImVec2 c, float rx, float ry, ImU32 in, ImU32 out, float rot = 0, int segs = 40)
{
    if (rx < 0.5f || ry < 0.5f) return;
    ImVec2 uv = tm_uv(dl);
    float cr = cosf(rot * 3.14159265f / 180), sr = sinf(rot * 3.14159265f / 180);
    dl->PrimReserve(segs * 3, segs + 1);
    unsigned base = dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(c, uv, in);
    for (int i = 0; i < segs; i++) {
        float a = 6.2831853f * i / segs, x = cosf(a) * rx, y = sinf(a) * ry;
        dl->PrimWriteVtx(ImVec2(c.x + x * cr - y * sr, c.y + x * sr + y * cr), uv, out);
    }
    for (int i = 0; i < segs; i++) {
        dl->PrimWriteIdx((ImDrawIdx)base);
        dl->PrimWriteIdx((ImDrawIdx)(base + 1 + i));
        dl->PrimWriteIdx((ImDrawIdx)(base + 1 + (i + 1) % segs));
    }
}

// A ring between two ellipses (same centre), `in` on the inner edge to `out` on the outer.
void tm_ring(ImDrawList *dl, ImVec2 c, float rx0, float ry0, float rx1, float ry1, ImU32 in, ImU32 out, int segs = 48)
{
    ImVec2 uv = tm_uv(dl);
    dl->PrimReserve(segs * 6, segs * 2);
    unsigned base = dl->_VtxCurrentIdx;
    for (int i = 0; i < segs; i++) {
        float a = 6.2831853f * i / segs, ca = cosf(a), sa = sinf(a);
        dl->PrimWriteVtx(ImVec2(c.x + ca * rx0, c.y + sa * ry0), uv, in);
        dl->PrimWriteVtx(ImVec2(c.x + ca * rx1, c.y + sa * ry1), uv, out);
    }
    for (int i = 0; i < segs; i++) {
        unsigned a = base + 2 * i, b = base + 2 * ((i + 1) % segs);
        dl->PrimWriteIdx((ImDrawIdx)a); dl->PrimWriteIdx((ImDrawIdx)(a + 1)); dl->PrimWriteIdx((ImDrawIdx)(b + 1));
        dl->PrimWriteIdx((ImDrawIdx)a); dl->PrimWriteIdx((ImDrawIdx)(b + 1)); dl->PrimWriteIdx((ImDrawIdx)b);
    }
}

// A quad with a colour per corner.
void tm_quad(ImDrawList *dl, ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 ca, ImU32 cb, ImU32 cc, ImU32 cd)
{
    ImVec2 uv = tm_uv(dl);
    dl->PrimReserve(6, 4);
    unsigned base = dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(a, uv, ca); dl->PrimWriteVtx(b, uv, cb); dl->PrimWriteVtx(c, uv, cc); dl->PrimWriteVtx(d, uv, cd);
    const int ix[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i : ix) dl->PrimWriteIdx((ImDrawIdx)(base + i));
}

// Lines across a polygon (world coordinates, so the pattern moves with the map): every `step` world units at
// `angle` degrees, only inside it.
void tm_hatch(ImDrawList *dl, const TmView &v, const ImVec2 *p, int n, float angle, float step, ImU32 col, float thick)
{
    float a = angle * 3.14159265f / 180;
    ImVec2 dir(cosf(a), sinf(a)), nrm(-sinf(a), cosf(a));
    float lo = FLT_MAX, hi = -FLT_MAX;
    for (int i = 0; i < n; i++) { float q = p[i].x * nrm.x + p[i].y * nrm.y; lo = std::min(lo, q); hi = std::max(hi, q); }
    std::vector<float> cut;
    for (float c = floorf(lo / step) * step; c <= hi; c += step) {
        cut.clear();
        for (int i = 0; i < n; i++) {
            const ImVec2 &s = p[i], &e = p[(i + 1) % n];
            float u = s.x * nrm.x + s.y * nrm.y - c, w = e.x * nrm.x + e.y * nrm.y - c;
            if ((u < 0) == (w < 0)) continue;
            float t = u / (u - w);
            ImVec2 q(s.x + (e.x - s.x) * t, s.y + (e.y - s.y) * t);
            cut.push_back(q.x * dir.x + q.y * dir.y);
        }
        std::sort(cut.begin(), cut.end());
        for (size_t i = 0; i + 1 < cut.size(); i += 2) {
            ImVec2 q0(nrm.x * c + dir.x * cut[i], nrm.y * c + dir.y * cut[i]);
            ImVec2 q1(nrm.x * c + dir.x * cut[i + 1], nrm.y * c + dir.y * cut[i + 1]);
            dl->AddLine(v.w(q0), v.w(q1), col, thick);
        }
    }
}

bool tm_inside(const ImVec2 *p, int n, float x, float y)
{
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if ((p[i].y > y) != (p[j].y > y) && x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x) in = !in;
    return in;
}

// The street lights: a lattice of dots (every 26 world units, turned 29 degrees) inside the polygon.
void tm_lights(ImDrawList *dl, const TmView &v, const ImVec2 *p, int n)
{
    float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
    for (int i = 0; i < n; i++) { x0 = std::min(x0, p[i].x); y0 = std::min(y0, p[i].y); x1 = std::max(x1, p[i].x); y1 = std::max(y1, p[i].y); }
    x0 = std::max(x0, -100.0f); x1 = std::min(x1, 1700.0f); y0 = std::max(y0, -60.0f); y1 = std::min(y1, 1000.0f);
    const float a = 29 * 3.14159265f / 180, ca = cosf(a), sa = sinf(a), step = 26;
    float r = std::max(1.0f, 1.1f * v.k());
    // the lattice's cells covering the box (in the turned frame)
    float corners[4][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y1 } };
    float u0 = FLT_MAX, u1 = -FLT_MAX, w0 = FLT_MAX, w1 = -FLT_MAX;
    for (auto &c : corners) {
        float u = c[0] * ca + c[1] * sa, w = -c[0] * sa + c[1] * ca;
        u0 = std::min(u0, u); u1 = std::max(u1, u); w0 = std::min(w0, w); w1 = std::max(w1, w);
    }
    for (float u = floorf(u0 / step) * step; u <= u1; u += step)
        for (float w = floorf(w0 / step) * step; w <= w1; w += step)
            for (int half = 0; half < 2; half++) {
                float uu = u + half * 13, ww = w + half * 13;
                float x = uu * ca - ww * sa, y = uu * sa + ww * ca;
                if (x < x0 || x > x1 || y < y0 || y > y1 || !tm_inside(p, n, x, y)) continue;
                ImVec2 q = v.w(x, y);
                float rr = half ? r * 0.75f : r;
                dl->AddRectFilled(ImVec2(q.x - rr, q.y - rr), ImVec2(q.x + rr, q.y + rr),
                                  half ? IM_COL32(255, 210, 140, 80) : IM_COL32(255, 190, 90, 130), rr);
            }
}

// Points along a polyline, every `step` pixels (the dashes and the zigzag).
struct TmTrack {
    std::vector<ImVec2> p;
    std::vector<float> at;          // distance from the start
    float len = 0;
    void build(const std::vector<ImVec2> &pts)
    {
        p = pts;
        at.assign(p.size(), 0);
        for (size_t i = 1; i < p.size(); i++) {
            float dx = p[i].x - p[i - 1].x, dy = p[i].y - p[i - 1].y;
            at[i] = at[i - 1] + sqrtf(dx * dx + dy * dy);
        }
        len = at.empty() ? 0 : at.back();
    }
    ImVec2 pos(float d, ImVec2 *dir = nullptr) const
    {
        if (p.size() < 2) return p.empty() ? ImVec2() : p[0];
        d = std::max(0.0f, std::min(len, d));
        size_t i = 1;
        while (i + 1 < p.size() && at[i] < d) i++;
        float seg = std::max(0.001f, at[i] - at[i - 1]), t = (d - at[i - 1]) / seg;
        if (dir) *dir = ImVec2((p[i].x - p[i - 1].x) / seg, (p[i].y - p[i - 1].y) / seg);
        return ImVec2(p[i - 1].x + (p[i].x - p[i - 1].x) * t, p[i - 1].y + (p[i].y - p[i - 1].y) * t);
    }
};

void tm_dashes(ImDrawList *dl, const TmTrack &t, float on, float off, float phase, ImU32 col, float thick)
{
    float period = on + off;
    if (t.len <= 0 || period <= 0) return;
    float start = fmodf(phase, period);
    if (start > 0) start -= period;
    for (float d = start; d < t.len; d += period) {
        float a = std::max(0.0f, d), b = std::min(t.len, d + on);
        if (b <= a) continue;
        ImVec2 pts[8];
        int n = 0;
        for (int i = 0; i < 8 && n < 8; i++) pts[n++] = t.pos(a + (b - a) * i / 7.0f);
        dl->AddPolyline(pts, n, col, ImDrawFlags_None, thick);
    }
}

// Turn what was drawn since vertex `from` by `deg` degrees round `c` (the plates' tilt, the sprayed tags).
void tm_turn(ImDrawList *dl, int from, ImVec2 c, float deg)
{
    float a = deg * 3.14159265f / 180, ca = cosf(a), sa = sinf(a);
    for (int i = from; i < dl->VtxBuffer.Size; i++) {
        ImVec2 &q = dl->VtxBuffer[i].pos;
        float x = q.x - c.x, y = q.y - c.y;
        q = ImVec2(c.x + x * ca - y * sa, c.y + x * sa + y * ca);
    }
}

// Text at a pixel point, `px` pixels tall, no outline.
void tm_text(ImDrawList *dl, ImFont *font, float px, ImVec2 at, ImU32 col, const char *s, Align align = LEFT)
{
    font = font_or_default(font);
    if (align != LEFT) {
        float w = font->CalcTextSizeA(px, FLT_MAX, 0, s).x;
        at.x -= align == CENTER ? w / 2 : w;
    }
    dl->AddText(font, px, at, col, s);
}
float tm_width(ImFont *font, float px, const char *s) { return font_or_default(font)->CalcTextSizeA(px, FLT_MAX, 0, s).x; }

// ---- the small icons (pixels; `s` = pixels per design unit) ----

void tm_shield(ImDrawList *dl, ImVec2 o, float s, ImU32 col)     // 12 x 14
{
    ImVec2 p[8] = { {6, 0}, {12, 2}, {12, 7}, {10.5f, 10.8f}, {6, 14}, {1.5f, 10.8f}, {0, 7}, {0, 2} };
    for (auto &q : p) q = ImVec2(o.x + q.x * s, o.y + q.y * s);
    dl->AddConvexPolyFilled(p, 8, col);
}
void tm_flame(ImDrawList *dl, ImVec2 o, float s, ImU32 col, ImU32 core = 0)    // 12 x 16
{
    dl->AddCircleFilled(ImVec2(o.x + 6 * s, o.y + 10.5f * s), 5.2f * s, col, 16);
    ImVec2 t[3] = { ImVec2(o.x + 6.5f * s, o.y), ImVec2(o.x + 11 * s, o.y + 9.5f * s), ImVec2(o.x + 1.4f * s, o.y + 8.6f * s) };
    dl->AddTriangleFilled(t[0], t[1], t[2], col);
    ImVec2 l[3] = { ImVec2(o.x + 2.2f * s, o.y + 4.5f * s), ImVec2(o.x + 4.5f * s, o.y + 8 * s), ImVec2(o.x + 1 * s, o.y + 9.5f * s) };
    dl->AddTriangleFilled(l[0], l[1], l[2], col);
    if (core) dl->AddCircleFilled(ImVec2(o.x + 6 * s, o.y + 11.5f * s), 2.8f * s, core, 12);
}
void tm_lock(ImDrawList *dl, ImVec2 o, float s, ImU32 col)       // 12 x 13
{
    dl->AddRect(ImVec2(o.x + 1.5f * s, o.y + 5.5f * s), ImVec2(o.x + 10.5f * s, o.y + 12.5f * s), col, 1.5f * s, ImDrawFlags_None, 1.8f * s);
    dl->PathArcTo(ImVec2(o.x + 6 * s, o.y + 5.2f * s), 3.2f * s, 3.14159265f, 6.2831853f, 10);
    dl->PathStroke(col, ImDrawFlags_None, 1.8f * s);
}
void tm_crown(ImDrawList *dl, ImVec2 o, float s, ImU32 col)      // 18 x 15
{
    ImVec2 p[7] = { {1.5f, 12}, {3, 3.8f}, {6.8f, 7.5f}, {9, 1.5f}, {11.2f, 7.5f}, {15, 3.8f}, {16.5f, 12} };
    for (auto &q : p) q = ImVec2(o.x + q.x * s, o.y + q.y * s);
    tm_fill(dl, p, 7, col);
}

// ---- what the map shows, worked out once a frame ----

struct TmBoard {
    std::vector<TmPin> pins;            // per venue (m.venues' order)
    int holder_n[5] = {};               // per borough: how many crews share the most venues there
    long long holders[5][4] = {};       // ... those crews (most venues; several: contested)
    int most[5] = {};                   // ... how many venues each of them holds there
};

TmBoard tm_board(const TurfMap &m)
{
    TmBoard b;
    std::map<long long, int> per[5];
    for (int i = 0; i < (int)m.venues.size(); i++) {
        const TurfVenue &v = m.venues[i];
        b.pins.push_back(tm_pin(v, i));
        int k = tm_borough_of(v.borough);
        if (k >= 0 && v.owner) per[k][v.owner]++;
    }
    for (int k = 0; k < 5; k++) {
        int top = 0;
        for (auto &e : per[k]) top = std::max(top, e.second);
        b.most[k] = top;
        if (!top) continue;
        for (auto &e : per[k]) if (e.second == top && b.holder_n[k] < 4) b.holders[k][b.holder_n[k]++] = e.first;
    }
    return b;
}

const TurfLive *tm_live_at(const TurfMap &m, int venue)
{
    for (auto &l : m.live) if (l.venue == venue) return &l;
    return nullptr;
}

// Two crews fight (a battle set up or under way) anywhere on the map.
bool tm_battle_between(const TurfMap &m, long long a, long long b)
{
    if (!a || !b || a == b) return false;
    for (auto &l : m.live)
        if ((l.crew[0] == a && l.crew[1] == b) || (l.crew[0] == b && l.crew[1] == a)) return true;
    for (auto &v : m.venues)
        if (v.chal && v.chal_battle && ((v.owner == a && v.chal_crew == b) || (v.owner == b && v.chal_crew == a))) return true;
    return false;
}

// "21:00" today, "SAT 21:00" within the week, else "NOV 26" (local time)
std::string tm_when(long long t, long long now)
{
    time_t tt = (time_t)t, tn = (time_t)now;
    struct tm a = {}, b = {};
    localtime_s(&a, &tt);
    localtime_s(&b, &tn);
    static const char *const days[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    static const char *const months[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    char buf[32];
    if (a.tm_yday == b.tm_yday && a.tm_year == b.tm_year) snprintf(buf, sizeof(buf), "%02d:%02d", a.tm_hour, a.tm_min);
    else if (t - now < 6 * 86400) snprintf(buf, sizeof(buf), "%s %02d:%02d", days[a.tm_wday], a.tm_hour, a.tm_min);
    else snprintf(buf, sizeof(buf), "%s %d", months[a.tm_mon], a.tm_mday);
    return buf;
}

// "2M AGO", "14M AGO", "3H AGO", "2D AGO"
std::string tm_ago(long long secs)
{
    char buf[24];
    if (secs < 60) return "JUST NOW";
    if (secs < 3600) snprintf(buf, sizeof(buf), "%lldm ago", secs / 60);
    else if (secs < 86400) snprintf(buf, sizeof(buf), "%lldh ago", secs / 3600);
    else snprintf(buf, sizeof(buf), "%lldd ago", secs / 86400);
    return buf;
}

// The season's start (the map's started=, else the first of its month, UTC).
long long tm_started(const TurfMap &m)
{
    if (m.started) return m.started;
    int y = 0, mo = 0;
    if (sscanf(m.month.c_str(), "%d-%d", &y, &mo) != 2) return 0;
    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = 1;
    return (long long)_mkgmtime(&t);
}

// ---- the city ----

void tm_city(ImDrawList *dl, const TmView &v, ImVec2 display, const TurfMap &m, const TmBoard &b, float t)
{
    // water: the river's dark gradient over the whole window, its ripples
    dl->AddRectFilledMultiColor(ImVec2(0, 0), display, IM_COL32(6, 16, 27, 255), IM_COL32(5, 12, 21, 255), IM_COL32(3, 6, 11, 255),
                                IM_COL32(4, 9, 16, 255));
    {
        unsigned seed = 0x7A11u;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f; };
        float ca = cosf(-8 * 3.14159265f / 180), sa = sinf(-8 * 3.14159265f / 180);
        for (int i = 0; i < 260; i++) {     // the ripples glint in turn, drifting with the current
            float x = -300 + 2300 * rnd(), y = -60 + 1120 * rnd(), l = rnd() < 0.6f ? 14.0f : 9.0f, ph = rnd() * 6.2832f;
            float glint = 0.5f + 0.5f * sinf(t * 1.4f + ph);
            x += fmodf(t * 3.0f + ph * 10, 40.0f) - 20;
            dl->AddLine(v.w(x, y), v.w(x + l * ca, y + l * sa), IM_COL32(130, 180, 240, (int)(10 + 34 * glint * glint)),
                        std::max(1.0f, 1.4f * v.D.s()));
        }
    }
    // sodium light on the rivers
    const float k = v.k();
    tm_glow(dl, v.w(520, 560), 22 * k, 130 * k, tm_rgba(255, 150, 40, 0.16f), tm_rgba(255, 150, 40, 0), 20);
    tm_glow(dl, v.w(600, 420), 16 * k, 100 * k, tm_rgba(255, 150, 40, 0.13f), tm_rgba(255, 150, 40, 0), 22);
    tm_glow(dl, v.w(560, 790), 80 * k, 18 * k, tm_rgba(255, 150, 40, 0.12f), tm_rgba(255, 150, 40, 0));
    tm_glow(dl, v.w(760, 262), 100 * k, 12 * k, tm_rgba(255, 59, 84, 0.16f), tm_rgba(255, 59, 84, 0));
    // New Jersey: dim, not in play
    {
        ImVec2 p[16];
        for (int i = 0; i < 16; i++) p[i] = v.w(TM_NJ[i]);
        tm_fill(dl, p, 16, IM_COL32(12, 13, 17, 255));
        dl->AddPolyline(p, 16, IM_COL32(242, 230, 200, 20), ImDrawFlags_Closed, 2 * v.D.s());
        tm_hatch(dl, v, TM_NJ, 16, 29, 13, IM_COL32(255, 176, 80, 10), std::max(1.0f, v.D.s()));
        tm_hatch(dl, v, TM_NJ, 16, 119, 13, IM_COL32(255, 176, 80, 10), std::max(1.0f, v.D.s()));
    }
    tm_glow(dl, v.w(520, 420), 30 * k, 80 * k, tm_rgba(255, 150, 40, 0.16f), tm_rgba(255, 150, 40, 0), 20);
    tm_glow(dl, v.w(455, 600), 36 * k, 28 * k, tm_rgba(255, 150, 40, 0.18f), tm_rgba(255, 150, 40, 0));
    tm_glow(dl, v.w(575, 170), 26 * k, 70 * k, tm_rgba(255, 150, 40, 0.13f), tm_rgba(255, 150, 40, 0), 15);
    // the boroughs: asphalt, the crews' turf, its neon edge, the street grid and lights
    for (int i = 0; i < 5; i++) {
        const TmBorough &bo = TM_BOROUGHS[i];
        ImVec2 p[24];
        for (int j = 0; j < bo.n; j++) p[j] = v.w(bo.pts[j]);
        const std::vector<int> &tris = tm_tris_of(bo.pts, bo.n);
        tm_fill_tris(dl, p, bo.n, tris, IM_COL32(22, 20, 26, 255));
        if (b.holder_n[i] == 1) {      // the crew's paint: a wash and the brushed, grainy coat over it
            ImU32 c = tm_crew(m, b.holders[i][0]);
            tm_fill_tris(dl, p, bo.n, tris, with_alpha(c, 0.22f));
            tm_paint_fill(dl, v, bo.pts, bo.n, with_alpha(c, 0.58f));
        } else if (b.holder_n[i] == 0) {    // open: a cool slate coat, so the city still has texture
            tm_paint_fill(dl, v, bo.pts, bo.n, IM_COL32(120, 128, 160, 34));
            tm_paint_fill(dl, v, bo.pts, bo.n, IM_COL32(255, 196, 90, 40));     // warm: the city lights, nobody's yet
        }
        if (b.holder_n[i] > 1) {    // contested: the tied crews' colours mixed in the paint, and in stripes
            ImU32 mix = tm_crew(m, b.holders[i][0]);
            for (int h = 1; h < b.holder_n[i]; h++) mix = tm_mix(mix, tm_crew(m, b.holders[i][h]), 1.0f / (h + 1));
            tm_paint_fill(dl, v, bo.pts, bo.n, with_alpha(mix, 0.34f));
            // the stripes march: a borough in dispute never sits still
            float step = 9.0f * b.holder_n[i], drift = fmodf(t * 7.0f, step);
            for (int h = 0; h < b.holder_n[i]; h++) {
                ImU32 c = tm_crew(m, b.holders[i][h]);
                std::vector<ImVec2> q(bo.pts, bo.pts + bo.n);
                float off = 9.0f * h + drift;
                for (auto &e : q) { e.x += off * 0.7071f; e.y -= off * 0.7071f; }
                tm_hatch(dl, v, q.data(), bo.n, 45, step, with_alpha(c, 0.36f), std::max(1.0f, 2.6f * k));
            }
        }
        tm_hatch(dl, v, bo.pts, bo.n, 29, 13, IM_COL32(255, 176, 80, 26), std::max(1.0f, 0.9f * v.D.s()));
        tm_hatch(dl, v, bo.pts, bo.n, 119, 13, IM_COL32(255, 176, 80, 26), std::max(1.0f, 0.9f * v.D.s()));
        tm_lights(dl, v, bo.pts, bo.n);
    }
    // sodium pools over the land
    tm_glow(dl, v.w(560, 470), 120 * k, 120 * k, tm_rgba(255, 150, 40, 0.22f), tm_rgba(255, 150, 40, 0));
    tm_glow(dl, v.w(510, 700), 90 * k, 90 * k, tm_rgba(255, 150, 40, 0.22f), tm_rgba(255, 150, 40, 0));
    tm_glow(dl, v.w(760, 760), 140 * k, 140 * k, tm_rgba(255, 150, 40, 0.16f), tm_rgba(255, 150, 40, 0));
    tm_glow(dl, v.w(840, 420), 120 * k, 120 * k, tm_rgba(255, 150, 40, 0.14f), tm_rgba(255, 150, 40, 0));
    tm_glow(dl, v.w(830, 130), 130 * k, 130 * k, tm_rgba(255, 150, 40, 0.14f), tm_rgba(255, 150, 40, 0));
    // each held venue lights its block in its crew's colour
    for (int i = 0; i < (int)m.venues.size() && i < (int)b.pins.size(); i++) {
        if (!m.venues[i].owner) continue;
        ImU32 c = tm_crew(m, m.venues[i].owner);
        tm_glow(dl, v.w(b.pins[i].x, b.pins[i].y), 85 * k, 85 * k, with_alpha(c, 0.20f), with_alpha(c, 0), 0, 28);
    }
    // Manhattan: the lit strip down the island (the city's lights along its spine)
    {
        const float spine[4][2] = { { 512, 690 }, { 540, 570 }, { 572, 450 }, { 610, 320 } };
        for (auto &c : spine)
            tm_glow(dl, v.w(c[0], c[1]), 95 * k, 34 * k, tm_rgba(255, 160, 50, 0.20f), tm_rgba(255, 160, 50, 0), -74, 28);
    }
    // the turf's soft outer glow and neon edge in its crew's colour (a contested borough's crews mixed),
    // and each borough's outline
    const float gk = v.D.s() * std::min(1.6f, sqrtf(v.z));
    for (int i = 0; i < 5; i++) {
        const TmBorough &bo = TM_BOROUGHS[i];
        ImVec2 p[24];
        for (int j = 0; j < bo.n; j++) p[j] = v.w(bo.pts[j]);
        if (b.holder_n[i] >= 1) {
            ImU32 c = tm_crew(m, b.holders[i][0]);
            for (int h = 1; h < b.holder_n[i]; h++) c = tm_mix(c, tm_crew(m, b.holders[i][h]), 1.0f / (h + 1));
            float beat = b.holder_n[i] > 1 ? 0.75f + 0.25f * sinf(t * 2.6f) : 1.0f;     // a contested edge pulses
            dl->AddPolyline(p, bo.n, with_alpha(c, 0.045f * beat), ImDrawFlags_Closed, 80 * gk);
            dl->AddPolyline(p, bo.n, with_alpha(c, 0.08f * beat), ImDrawFlags_Closed, 46 * gk);
            dl->AddPolyline(p, bo.n, with_alpha(c, 0.14f * beat), ImDrawFlags_Closed, 24 * gk);
            dl->AddPolyline(p, bo.n, with_alpha(c, 0.32f * beat), ImDrawFlags_Closed, 10 * gk);
            dl->AddPolyline(p, bo.n, with_alpha(tm_light(c, 0.45f), 0.95f), ImDrawFlags_Closed, 2.6f * v.D.s());
        } else {    // nobody's turf: a slow yellow beat along its edge, up for grabs
            float beat = 0.6f + 0.4f * sinf(t * 1.6f + i);
            dl->AddPolyline(p, bo.n, with_alpha(TM_YELLOW, 0.035f * beat), ImDrawFlags_Closed, 46 * gk);
            dl->AddPolyline(p, bo.n, with_alpha(TM_YELLOW, 0.07f * beat), ImDrawFlags_Closed, 18 * gk);
            dl->AddPolyline(p, bo.n, IM_COL32(242, 230, 200, 90), ImDrawFlags_Closed, 2 * v.D.s());
        }
    }
    // the avenues' light trails
    {
        const float tr[4][8] = { { 640, 210, 600, 330, 560, 470, 500, 760 }, { 720, 120, 800, 140, 900, 120, 1030, 60 },
                                 { 612, 600, 700, 560, 820, 500, 1100, 470 }, { 600, 800, 700, 760, 840, 780, 1100, 720 } };
        for (int r = 0; r < 4; r++) {
            const float *c = tr[r];
            dl->AddBezierCubic(v.w(c[0], c[1]), v.w(c[2], c[3]), v.w(c[4], c[5]), v.w(c[6], c[7]), IM_COL32(255, 190, 90, 40), 6 * v.D.s(), 40);
            dl->AddBezierCubic(v.w(c[0], c[1]), v.w(c[2], c[3]), v.w(c[4], c[5]), v.w(c[6], c[7]), IM_COL32(255, 225, 160, 125), 1.4f * v.D.s(), 40);
            // the traffic: head lights one way, tail lights the other
            for (int j = 0; j < 7; j++) {
                bool back = (j % 2) == 1;
                float u = fmodf(t * (0.020f + 0.004f * r) + j / 7.0f + r * 0.11f, 1.0f);
                if (back) u = 1 - u;
                float a = 1 - u, w0 = a * a * a, w1 = 3 * a * a * u, w2 = 3 * a * u * u, w3 = u * u * u;
                ImVec2 q = v.w(c[0] * w0 + c[2] * w1 + c[4] * w2 + c[6] * w3, c[1] * w0 + c[3] * w1 + c[5] * w2 + c[7] * w3);
                ImU32 lc = back ? IM_COL32(255, 70, 60, 255) : IM_COL32(255, 244, 220, 255);
                tm_glow(dl, q, 7 * v.D.s(), 7 * v.D.s(), with_alpha(lc, 0.5f), with_alpha(lc, 0), 0, 10);
                dl->AddCircleFilled(q, 1.5f * v.D.s(), lc, 6);
            }
        }
    }
    {   // Central Park
        ImVec2 p[4];
        for (int j = 0; j < 4; j++) p[j] = v.w(TM_PARK[j]);
        tm_fill(dl, p, 4, IM_COL32(11, 18, 14, 255));
        dl->AddPolyline(p, 4, IM_COL32(120, 200, 140, 64), ImDrawFlags_Closed, 1.0f);
    }
    // the bridges
    {
        const float br[5][4] = { { 598, 236, 568, 228 }, { 512, 742, 590, 752 }, { 608, 420, 664, 438 }, { 428, 712, 594, 762 }, { 720, 248, 740, 282 } };
        for (auto &c : br) dl->AddLine(v.w(c[0], c[1]), v.w(c[2], c[3]), IM_COL32(255, 225, 170, 190), 2.4f * v.D.s());
        const float dots[5][2] = { { 568, 228 }, { 598, 236 }, { 428, 712 }, { 594, 762 }, { 511, 737 } };
        for (auto &c : dots) dl->AddCircleFilled(v.w(c[0], c[1]), 2.4f * v.D.s(), IM_COL32(255, 231, 176, 255), 8);
    }
    // haze
    tm_glow(dl, v.w(300, 560), 260 * k, 90 * k, tm_rgba(150, 170, 210, 0.06f), tm_rgba(150, 170, 210, 0));
    tm_glow(dl, v.w(560, 860), 300 * k, 70 * k, tm_rgba(150, 170, 210, 0.07f), tm_rgba(150, 170, 210, 0));
    tm_glow(dl, v.w(900, 300), 320 * k, 60 * k, tm_rgba(150, 170, 210, 0.05f), tm_rgba(150, 170, 210, 0));
    // two searchlights sweeping the sky over the city, slowly (from below the map)
    const float ds = v.D.s();
    for (int i = 0; i < 2; i++) {
        ImVec2 base = v.D.p(i ? 960.0f : 300.0f, 1010);
        float ang = (i ? -0.32f : 0.30f) + 0.36f * sinf(t * (i ? 0.11f : 0.09f) + i * 2.1f), len = 1250 * ds;
        ImVec2 dir(sinf(ang), -cosf(ang)), nrm(cosf(ang), sinf(ang));
        float w0 = 8 * ds, w1 = 120 * ds;
        ImVec2 tip(base.x + dir.x * len, base.y + dir.y * len);
        tm_quad(dl, ImVec2(base.x - nrm.x * w0, base.y - nrm.y * w0), ImVec2(base.x + nrm.x * w0, base.y + nrm.y * w0),
                ImVec2(tip.x + nrm.x * w1, tip.y + nrm.y * w1), ImVec2(tip.x - nrm.x * w1, tip.y - nrm.y * w1),
                IM_COL32(200, 215, 255, 26), IM_COL32(200, 215, 255, 26), IM_COL32(200, 215, 255, 0), IM_COL32(200, 215, 255, 0));
    }
}

// The crews' tags sprayed over their boroughs, and the boroughs' names.
void tm_names(ImDrawList *dl, const TmView &v, const TurfMap &m, const TmBoard &b)
{
    for (int i = 0; i < 5; i++) {
        const TmBorough &bo = TM_BOROUGHS[i];
        if (b.holder_n[i] == 1) {
            std::string tag;
            for (auto &e : m.venues) if (e.owner == b.holders[i][0]) tag = upper(e.otag.c_str());
            if (!tag.empty()) {
                // the painted tag: a dark undercoat a little off, the crew's colour over it, and its light edge
                int from = dl->VtxBuffer.Size;
                ImVec2 c = v.w(bo.tx, bo.ty);
                float px = bo.tsize * 1.15f * v.k(), o = px * 0.035f;
                ImU32 col = tm_crew(m, b.holders[i][0]);
                tm_text(dl, g_hand, px, ImVec2(c.x + o, c.y - px * 0.5f + o), IM_COL32(0, 0, 0, 70), tag.c_str(), CENTER);
                tm_text(dl, g_hand, px, ImVec2(c.x, c.y - px * 0.5f), with_alpha(col, 0.30f), tag.c_str(), CENTER);
                tm_text(dl, g_hand, px, ImVec2(c.x - o * 0.4f, c.y - px * 0.5f - o * 0.4f), with_alpha(tm_light(col, 0.5f), 0.10f), tag.c_str(), CENTER);
                tm_turn(dl, from, c, i == 1 ? -66.0f : -8.0f);
            }
        }
        if (b.holder_n[i] == 0) {      // nobody's turf: OPEN sprayed where a crew's tag would be
            bool any = false;
            for (auto &e : m.venues) any = any || (!e.owner && !e.boss && tm_borough_of(e.borough) == i);
            if (any) {
                int from = dl->VtxBuffer.Size;
                ImVec2 c = v.w(bo.tx, bo.ty);
                float px = bo.tsize * 0.95f * v.k(), o = px * 0.035f;
                tm_text(dl, g_hand, px, ImVec2(c.x + o, c.y - px * 0.5f + o), IM_COL32(0, 0, 0, 60), "OPEN", CENTER);
                tm_text(dl, g_hand, px, ImVec2(c.x, c.y - px * 0.5f), with_alpha(TM_YELLOW, 0.17f), "OPEN", CENTER);
                tm_turn(dl, from, c, i == 1 ? -66.0f : -8.0f);
            }
        }
        int from = dl->VtxBuffer.Size;
        ImVec2 at = v.w(bo.lx, bo.ly);
        float px = bo.lsize * v.D.s() * std::min(1.4f, sqrtf(v.z));
        std::string spaced;
        for (const char *c = bo.label; *c; c++) { spaced += *c; if (c[1]) spaced += ' '; }
        px *= 0.85f;
        tm_text(dl, tm_serif(), px, ImVec2(at.x + 2 * v.D.s(), at.y + 2 * v.D.s()), IM_COL32(0, 0, 0, 110), spaced.c_str());
        tm_text(dl, tm_serif(), px, at, IM_COL32(242, 230, 200, b.holder_n[i] ? 92 : 70), spaced.c_str());
        if (bo.lrot) tm_turn(dl, from, at, bo.lrot);
    }
    ImVec2 nj = v.w(60, 540);
    tm_text(dl, tm_serif(), 17 * v.D.s(), nj, IM_COL32(242, 230, 200, 40), "N E W   J E R S E Y");
}

// The front lines (a battle between two boroughs' holders) and the challenges (dashed yellow, pin to pin).
void tm_lines(ImDrawList *dl, const TmView &v, const TurfMap &m, const TmBoard &b, float t)
{
    const float s = v.D.s();
    for (const TmBorder &br : TM_BORDERS) {
        bool front = false;
        for (int i = 0; i < b.holder_n[br.a] && !front; i++)
            for (int j = 0; j < b.holder_n[br.b] && !front; j++)
                front = tm_battle_between(m, b.holders[br.a][i], b.holders[br.b][j]);
        if (!front) continue;
        std::vector<ImVec2> pts;
        for (int i = 0; i < br.n; i++) pts.push_back(v.w(br.pts[i]));
        float beat = 0.8f + 0.2f * sinf(t * 3.2f);      // the front line breathes
        dl->AddPolyline(pts.data(), (int)pts.size(), with_alpha(TM_LIVE, 0.06f * beat), ImDrawFlags_None, 54 * s);
        dl->AddPolyline(pts.data(), (int)pts.size(), with_alpha(TM_LIVE, 0.12f * beat), ImDrawFlags_None, 30 * s);
        dl->AddPolyline(pts.data(), (int)pts.size(), with_alpha(TM_LIVE, 0.22f * beat), ImDrawFlags_None, 14 * s);
        dl->AddPolyline(pts.data(), (int)pts.size(), with_alpha(TM_LIVE, 0.45f * beat), ImDrawFlags_None, 6 * s);
        TmTrack tr;
        tr.build(pts);
        std::vector<ImVec2> zig;
        float step = 20 * s * std::min(1.6f, sqrtf(v.z));
        for (float d = 0; d <= tr.len; d += step / 2) {
            ImVec2 dir;
            ImVec2 q = tr.pos(d, &dir);
            float side = ((int)(d / (step / 2)) & 1) ? 7.0f * s : -7.0f * s;
            zig.push_back(ImVec2(q.x - dir.y * side, q.y + dir.x * side));
        }
        dl->AddPolyline(zig.data(), (int)zig.size(), IM_COL32(255, 227, 227, 255), ImDrawFlags_None, 2.4f * s);
        tm_dashes(dl, tr, 16 * s, 12 * s, -t * 35 * s, TM_LIVE, 4 * s);
    }
    // challenges: from the challenger's nearest venue to the venue it called out
    std::vector<TmPin> pins;
    for (int i = 0; i < (int)m.venues.size(); i++) pins.push_back(tm_pin(m.venues[i], i));
    for (int i = 0; i < (int)m.venues.size(); i++) {
        const TurfVenue &tv = m.venues[i];
        if (!tv.chal || !tv.chal_crew || tm_live_at(m, tv.id)) continue;
        int from = -1;
        float best = FLT_MAX;
        for (int j = 0; j < (int)m.venues.size(); j++) {
            if (m.venues[j].owner != tv.chal_crew) continue;
            float dx = pins[j].x - pins[i].x, dy = pins[j].y - pins[i].y, d = dx * dx + dy * dy;
            if (d < best) { best = d; from = j; }
        }
        if (from < 0) continue;
        ImVec2 a(pins[from].x, pins[from].y), c(pins[i].x, pins[i].y);
        float dx = c.x - a.x, dy = c.y - a.y, len = sqrtf(dx * dx + dy * dy);
        if (len < 1) continue;
        // a gentle bow; the line stops short of both pins
        ImVec2 mid((a.x + c.x) / 2 + dy / len * len * 0.12f, (a.y + c.y) / 2 - dx / len * len * 0.12f);
        std::vector<ImVec2> pts;
        for (int k = 2; k <= 18; k++) {
            float u = k / 20.0f, w0 = (1 - u) * (1 - u), w1 = 2 * u * (1 - u), w2 = u * u;
            pts.push_back(v.w(a.x * w0 + mid.x * w1 + c.x * w2, a.y * w0 + mid.y * w1 + c.y * w2));
        }
        dl->AddPolyline(pts.data(), (int)pts.size(), with_alpha(TM_YELLOW, 0.22f), ImDrawFlags_None, 10 * s);
        TmTrack tr;
        tr.build(pts);
        tm_dashes(dl, tr, 16 * s, 12 * s, -t * 35 * s, TM_YELLOW, 4 * s);
    }
}

// ---- the pins and their plates ----

struct TmPlate { float x0, y0, x1, y1; };      // a plate's box, design units (for the mouse)

// The coloured strip over a name: pieces of text and icons on the strip's colour.
struct TmBit { int kind; std::string text; ImU32 bg; };     // kind 0 text, 1 shield, 2 flame, 3 lock, 4 the LIVE badge

void tm_venue(ImDrawList *dl, const TmView &v, const TurfMap &m, const TurfVenue &tv, const TmPin &pin, bool sel, bool mine_next,
              long long now, float t, TmPlate &plate)
{
    const float s = v.D.s();
    ImVec2 c = v.w(pin.x, pin.y);
    const TurfLive *live = tm_live_at(m, tv.id);
    ImU32 col = tv.owner ? tm_crew(m, tv.owner) : tv.boss ? TM_CREAM : TM_OPEN_LIT;
    bool open = !tv.owner && !tv.boss && !live;
    bool claim = open && m.officer && m.my_crew && tv.can == 1;     // this player's crew may claim it now
    // the pin
    if (open) {     // open turf breathes: a soft light and a ring going out now and then (each pin its own beat)
        ImU32 lit = claim ? TM_YELLOW : TM_OPEN_LIT;
        float ph = fmodf(t / 2.6f + (float)(tv.id % 7) * 0.143f, 1.0f);
        tm_glow(dl, c, 30 * s, 30 * s, with_alpha(lit, 0.16f + 0.08f * sinf(t * 2.4f + tv.id)), with_alpha(lit, 0));
        dl->AddCircle(c, (15 + 22 * ph) * s, with_alpha(lit, 0.55f * (1 - ph) * (1 - ph)), 32, 2 * s);
    }
    if (live) {
        tm_glow(dl, c, 120 * s, 120 * s, tm_rgba(255, 59, 84, 0.30f), tm_rgba(255, 59, 84, 0));
        for (int r = 0; r < 3; r++) dl->AddCircle(c, (26 + 16 * r + r * r * 1.0f) * s, with_alpha(TM_LIVE, 0.8f - 0.27f * r), 40, (2 - 0.5f * r) * s);
        for (int r = 0; r < 3; r++) {
            float ph = fmodf(t / 1.8f + r / 3.0f, 1.0f);
            dl->AddCircle(c, 14 * s * (1 + 2.4f * ph), with_alpha(TM_LIVE, 0.95f * (1 - ph)), 32, 2 * s);
        }
    } else if (tv.owner) {
        tm_glow(dl, c, 44 * s, 44 * s, with_alpha(col, sel ? 0.65f : 0.45f), with_alpha(col, 0));
    }
    float r = 9 * s;
    ImU32 ring = live ? TM_LIVE : sel ? TM_YELLOW : col;
    if (tv.owner || live) tm_glow(dl, c, 26 * s, 26 * s, with_alpha(live ? TM_LIVE : sel ? TM_YELLOW : col, 0.55f), with_alpha(col, 0));
    if (tv.chal && !live) {    // called out: a yellow ring of dashes turning round it, and a beat of light
        float beat = 0.5f + 0.5f * sinf(t * 4.0f);
        tm_glow(dl, c, 44 * s, 44 * s, with_alpha(TM_YELLOW, 0.18f + 0.22f * beat), with_alpha(TM_YELLOW, 0));
        const float R = 21 * s, rot = t * 1.6f;
        for (int q = 0; q < 10; q++) {
            float a0 = rot + q * 0.6283f;
            dl->PathArcTo(c, R, a0, a0 + 0.36f, 6);
            dl->PathStroke(with_alpha(TM_YELLOW, 0.95f), ImDrawFlags_None, 2.6f * s);
        }
        dl->AddCircle(c, R + (6 + 8 * beat) * s, with_alpha(TM_YELLOW, 0.35f * (1 - beat)), 32, 2 * s);
    }
    dl->AddCircleFilled(c, r + 6 * s, ring, 28);
    dl->AddCircleFilled(c, r + 3 * s, IM_COL32(8, 7, 10, 255), 28);
    dl->AddCircleFilled(c, r, tv.owner ? col : IM_COL32(42, 38, 48, 255), 28);
    if (tv.owner) dl->AddCircleFilled(ImVec2(c.x - 2.5f * s, c.y - 2.5f * s), 3 * s, with_alpha(tm_light(col, 0.7f), 0.8f), 10);
    if (open) {     // the open pin: a cream core, ready to take a colour
        dl->AddCircle(c, r - 2.5f * s, with_alpha(claim ? TM_YELLOW : TM_OPEN_LIT, 0.8f), 20, 1.6f * s);
    }
    if (live) {     // the flame over the fight
        float fl = 1 + 0.06f * sinf(t * 18);
        ImVec2 o(c.x - 15 * s, c.y - 50 * s * fl);
        tm_flame(dl, o, 2.5f * s, TM_LIVE, 0);
        tm_flame(dl, ImVec2(o.x + 7.5f * s, o.y + 15 * s), 1.25f * s, TM_YELLOW, 0);
    }
    if (sel) {      // the reticle
        float a = t * 1.05f, R = 30 * s, L = 14 * s;
        for (int q = 0; q < 4; q++) {
            float qa = a + q * 1.5707963f;
            ImVec2 corner(c.x + cosf(qa + 0.785398f) * R * 1.414f, c.y + sinf(qa + 0.785398f) * R * 1.414f);
            ImVec2 d1(-cosf(qa) , -sinf(qa)), d2(sinf(qa), -cosf(qa));
            ImVec2 pts[3] = { ImVec2(corner.x + d2.x * L, corner.y + d2.y * L), corner, ImVec2(corner.x + d1.x * L, corner.y + d1.y * L) };
            dl->AddPolyline(pts, 3, TM_YELLOW, ImDrawFlags_None, 3 * s);
        }
    }
    // the plate: a strip (holder and streak, OPEN TURF, the boss lock, LIVE) over the name
    std::vector<TmBit> bits;
    ImU32 strip = col;
    if (live) {
        int a = 0, bside = 1;
        if (live->crew[0] != tv.owner && live->crew[1] == tv.owner) { a = 1; bside = 0; }    // the holder first
        bits.push_back({ 4, "LIVE", TM_LIVE });
        std::string sa = upper(live->tag[a].c_str()), sb = upper(live->tag[bside].c_str());
        if (live->score[0] >= 0) { sa += " " + std::to_string(live->score[a]); sb = std::to_string(live->score[bside]) + " " + sb; }
        bits.push_back({ 0, sa, tm_crew(m, live->crew[a]) });
        bits.push_back({ 0, sb, tm_crew(m, live->crew[bside]) });
    } else if (tv.boss && !tv.owner) {
        bits.push_back({ 3, "KINGS OF NY ONLY", 0 });
        strip = TM_CREAM;
    } else if (!tv.owner) {
        bits.push_back({ 0, "OPEN TURF", 0 });
        if (claim) bits.push_back({ 6, "CLAIM IT", 0 });
    } else {
        bits.push_back({ 0, upper(tv.otag.c_str()), 0 });
        bool fresh = tv.since && now - tv.since < 24 * 3600 && tv.defended == 0;
        if (fresh) bits.push_back({ 5, "NEW", 0 });
        else if (tv.defended > 0) bits.push_back({ tv.defended >= 5 ? 2 : 1, std::to_string(tv.defended), 0 });
    }
    if (tv.chal && !live && (sel || mine_next || tv.chal_crew == m.my_crew || tv.owner == m.my_crew)) {
        if (sel || mine_next) strip = TM_YELLOW;
        std::string ch = upper(tv.ctag.c_str()) + " CHALLENGE " + (tv.deadline > now ? tm_when(tv.deadline, now) : std::string("DUE"));
        bits.push_back({ 0, ch, 0 });
    }
    const float spx = (sel ? 16 : 14) * s, sh = (sel ? 24 : 20) * s;
    float npx = (live ? 20 : sel ? 26 : 17) * s * 1.3f;     // the hand font is smaller than the design's marker
    std::string name = upper(tv.name.c_str());
    float nw = tm_width(g_hand, npx, name.c_str()) + 18 * s, nh = npx * 1.25f + 4 * s;
    // the strip's width
    float sw = 7 * s;
    for (auto &bt : bits) {
        if (bt.kind == 4) sw += tm_width(g_title, spx, "LIVE") + 22 * s;
        else if (bt.kind == 5) sw += tm_width(g_title, spx * 0.85f, "NEW") + 15 * s;
        else if (bt.kind == 6) sw += tm_width(g_title, spx, bt.text.c_str()) + 15 * s;
        else sw += (bt.kind == 1 || bt.kind == 2 || bt.kind == 3 ? 15 * s : 0) + tm_width(g_title, spx, bt.text.c_str()) + 7 * s;
        if (live && bt.kind == 0) sw += 8 * s;
    }
    std::string extra;
    if (live) {
        char buf[96];
        if (live->fight > 0 && live->fights > 0 && live->watching >= 0)
            snprintf(buf, sizeof(buf), "FIGHT %d OF %d  -  %d WATCHING", live->fight, live->fights, live->watching);
        else if (live->fight > 0 && live->fights > 0) snprintf(buf, sizeof(buf), "FIGHT %d OF %d", live->fight, live->fights);
        else snprintf(buf, sizeof(buf), "CREW BATTLE  -  %d V %d", live->format, live->format);
        extra = buf;
    }
    float bw = std::max(sw, nw);
    int from = dl->VtxBuffer.Size;
    float lx, ly = c.y + (live ? -30 : sel ? -2 : -24) * s;
    if (sel) {      // the picked plate stays above the street wire
        float lim = v.D.p(0, 836).y - (extra.empty() ? 0 : 20 * s);
        if (ly + sh + nh > lim) ly = lim - sh - nh;
    }
    bool left = pin.left && !sel;
    if (sel && !pin.left) lx = c.x + 34 * s;
    else if (sel) lx = c.x - 34 * s - bw;
    else lx = left ? c.x - 20 * s - bw : c.x + 20 * s;
    if (sel && pin.left) left = true;
    float ax = left ? lx + bw : lx;         // the plate's edge at the pin
    // the strip
    float sx0 = left ? ax - sw : ax;
    {
        float x = sx0, y = ly;
        if (live) {
            // pieces each on their own colour: the blinking badge, then the two crews
            for (size_t i = 0; i < bits.size(); i++) {
                const TmBit &bt = bits[i];
                float w = bt.kind == 4 ? tm_width(g_title, spx, "LIVE") + 22 * s : tm_width(g_title, spx, bt.text.c_str()) + 15 * s;
                ImU32 bg = bt.kind == 4 ? (fmodf(t, 1.0f) < 0.5f ? TM_LIVE : IM_COL32(179, 18, 42, 255)) : bt.bg;
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + sh), bg);
                if (bt.kind == 4) {
                    dl->AddCircleFilled(ImVec2(x + 9 * s, y + sh / 2), 4 * s, IM_COL32_WHITE, 10);
                    tm_text(dl, g_title, spx, ImVec2(x + 16 * s, y + (sh - spx) / 2 - 1 * s), IM_COL32_WHITE, "LIVE");
                } else {
                    tm_text(dl, g_title, spx, ImVec2(x + 7 * s, y + (sh - spx) / 2 - 1 * s), TM_INK, bt.text.c_str());
                }
                x += w;
            }
        } else {
            ImVec2 q[4] = { ImVec2(x, y), ImVec2(x + sw, y), ImVec2(x + sw * 0.97f, y + sh), ImVec2(x + sw * 0.03f, y + sh) };
            dl->AddConvexPolyFilled(q, 4, strip);
            x += 7 * s;
            for (auto &bt : bits) {
                float ty = y + (sh - spx) / 2 - 1 * s;
                if (bt.kind == 1 || bt.kind == 2 || bt.kind == 3) {
                    ImVec2 o(x, y + (sh - 14 * s) / 2);
                    if (bt.kind == 1) tm_shield(dl, o, s * 0.95f, TM_INK);
                    else if (bt.kind == 2) tm_flame(dl, ImVec2(o.x, o.y - 1 * s), s * 0.95f, TM_INK);
                    else tm_lock(dl, o, s, TM_INK);
                    x += 15 * s;
                }
                if (bt.kind == 5 || bt.kind == 6) {     // NEW, CLAIM IT: a chip on the strip
                    const char *word = bt.kind == 5 ? "NEW" : bt.text.c_str();
                    float cpx = bt.kind == 5 ? spx * 0.85f : spx;
                    float w = tm_width(g_title, cpx, word) + 8 * s;
                    dl->AddRectFilled(ImVec2(x, y + 2 * s), ImVec2(x + w, y + sh - 2 * s), TM_INK);
                    tm_text(dl, g_title, cpx, ImVec2(x + 4 * s, y + (sh - cpx) / 2 - 1 * s), bt.kind == 6 ? TM_YELLOW : col, word);
                    x += w + 7 * s;
                    continue;
                }
                tm_text(dl, g_title, spx, ImVec2(x, ty), TM_INK, bt.text.c_str());
                x += tm_width(g_title, spx, bt.text.c_str()) + 7 * s;
            }
        }
    }
    // the name
    float ny = ly + sh;
    float nx0 = left ? ax - nw : ax;
    ImU32 edge = live ? TM_LIVE : sel ? TM_YELLOW : strip;
    dl->AddRectFilled(ImVec2(nx0, ny), ImVec2(nx0 + nw, ny + nh), IM_COL32(10, 8, 10, 235));
    dl->AddRectFilled(ImVec2(nx0, ny), ImVec2(nx0 + 3 * s, ny + nh), edge);
    if (sel) dl->AddRect(ImVec2(nx0, ny), ImVec2(nx0 + nw, ny + nh), with_alpha(TM_YELLOW, 0.5f), 0, ImDrawFlags_None, 2 * s);
    tm_text(dl, g_hand, npx, ImVec2(nx0 + 10 * s, ny + 2 * s), TM_CREAM, name.c_str());
    float bottom = ny + nh;
    if (!extra.empty()) {
        float epx = 13 * s, ew = tm_width(g_title, epx, extra.c_str()) + 12 * s;
        float ex = left ? ax - ew : ax;
        dl->AddRectFilled(ImVec2(ex, bottom + 2 * s), ImVec2(ex + ew, bottom + 2 * s + epx * 1.4f), IM_COL32(10, 8, 10, 230));
        tm_text(dl, g_title, epx, ImVec2(ex + 6 * s, bottom + 2 * s + epx * 0.15f), IM_COL32(255, 138, 154, 255), extra.c_str());
        bottom += 2 * s + epx * 1.4f;
    }
    // the tick from the pin to the plate
    float tx = left ? ax : ax - 22 * s;
    dl->AddLine(ImVec2(tx, ly + sh + 2 * s), ImVec2(tx + 22 * s, ly + sh + 2 * s), edge, 2 * s);
    tm_turn(dl, from, ImVec2(ax, ly + sh), sel ? -3.0f : -2.5f);
    // the box, in design units
    float x0 = std::min(sx0, nx0), x1 = std::max(sx0 + sw, nx0 + nw);
    plate = { (x0 - v.D.o.x) / s, (ly - v.D.o.y) / s, (x1 - v.D.o.x) / s, (bottom - v.D.o.y) / s };
}

// ---- the panels (design units, fixed on the screen) ----

void tm_glass(ImDrawList *dl, const Frame &D, float x, float y, float w, float h, ImU32 edge = 0)
{
    ImVec2 a = D.p(x, y), b = D.p(x + w, y + h);
    float s = D.s(), r = 16 * s;
    for (int i = 4; i >= 1; i--)        // the drop shadow
        dl->AddRectFilled(ImVec2(a.x - i * 3 * s, a.y + (18 - i * 3) * s), ImVec2(b.x + i * 3 * s, b.y + (18 + i * 6) * s),
                          IM_COL32(0, 0, 0, 22), r + i * 4 * s);
    dl->AddRectFilled(a, b, IM_COL32(14, 10, 12, 232), r);
    dl->AddLine(ImVec2(a.x + r, a.y + 1), ImVec2(b.x - r, a.y + 1), IM_COL32(255, 255, 255, 18), 1);
    if (edge) {
        dl->AddRect(ImVec2(a.x - 4 * s, a.y - 4 * s), ImVec2(b.x + 4 * s, b.y + 4 * s), with_alpha(edge, 0.10f), r + 4 * s, ImDrawFlags_None, 8 * s);
        dl->AddRect(a, b, with_alpha(edge, 0.55f), r, ImDrawFlags_None, 2 * s);
    }
}

// Turf War's titles: the design's bold serif (the game's AachenStd-Bold), else the stencil.
ImFont *tm_serif() { return g_serif ? g_serif : g_stencil; }

// A title in the serif: a dark red-brown drop under it, cream lit from above, a little worn (dark flecks
// where the paint wore off; on the dark ground they do not show). `px` pixels tall.
void tm_serif_text(ImDrawList *dl, float px, ImVec2 at, const char *s, float drop)
{
    ImFont *f = tm_serif();
    tm_text(dl, f, px, ImVec2(at.x + drop * 0.7f, at.y + drop), IM_COL32(48, 10, 14, 235), s);
    tm_text(dl, f, px, ImVec2(at.x + drop * 0.25f, at.y + drop * 0.45f), IM_COL32(0, 0, 0, 120), s);
    int from = dl->VtxBuffer.Size;
    tm_text(dl, f, px, at, TM_CREAM, s);
    tm_shade(dl, from, at.y + px * 0.15f, at.y + px * 0.95f, IM_COL32(252, 244, 222, 255), IM_COL32(222, 200, 160, 255));
    ImVec2 sz = font_or_default(f)->CalcTextSizeA(px, FLT_MAX, 0, s);
    unsigned seed = 0xF1EC5u + (unsigned)strlen(s);
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f; };
    int n = (int)(sz.x * sz.y / (px * px) * 26);
    for (int i = 0; i < n; i++) {
        ImVec2 q(at.x + rnd() * sz.x, at.y + px * (0.18f + 0.72f * rnd()));
        float r = px * (0.006f + 0.016f * rnd() * rnd());
        dl->AddCircleFilled(q, r, IM_COL32(70, 52, 40, 150), 6);
    }
}

float tm_title_px(const Frame &D) { return fit_size(D, tm_serif(), 80, "TURF WAR", 430); }

void tm_header(ImDrawList *dl, const Frame &D, int region)
{
    const float s = D.s();
    float px = tm_title_px(D);
    ImVec2 at = D.p(46, 22);
    for (int i = 0; i < 6; i++) {
        float a = i * 1.0472f;
        tm_text(dl, tm_serif(), px * s, ImVec2(at.x + cosf(a) * 6 * s, at.y + sinf(a) * 6 * s), IM_COL32(255, 59, 84, 12), "TURF WAR");
    }
    tm_serif_text(dl, px * s, at, "TURF WAR", 5 * s);
    // the red brush stroke and its drips
    float y0 = 22 + px * 1.02f;
    dl->AddBezierCubic(D.p(48, y0 + 8), D.p(136, y0 + 3), D.p(246, y0 + 12), D.p(422, y0 + 5), TM_RED, 9 * s, 40);
    dl->AddCircleFilled(D.p(48, y0 + 8), 4.5f * s, TM_RED, 12);
    dl->AddCircleFilled(D.p(422, y0 + 5), 4.5f * s, TM_RED, 12);
    const float drips[3][2] = { { 106, 9 }, { 196, 12 }, { 346, 7 } };
    for (auto &d : drips) {
        dl->AddLine(D.p(d[0], y0 + 9), D.p(d[0], y0 + 9 + d[1]), TM_RED, 3 * s);
        dl->AddCircleFilled(D.p(d[0], y0 + 9 + d[1]), 1.6f * s, TM_RED, 8);
    }
    (void)region;
}

// A small grey plate with a button's name (LB, RB, R1...); its width.
float tm_plate_key(ImDrawList *dl, const Frame &D, float x, float y, const char *key)
{
    const float s = D.s();
    std::string k = pad_text(key);
    float w = tm_width(g_title, 14 * s, k.c_str()) / s + 16;
    dl->AddRectFilled(D.p(x, y), D.p(x + w, y + 24), IM_COL32(58, 55, 66, 255), 7 * s);
    tm_text(dl, g_title, 14 * s, D.p(x + w / 2, y + 4), IM_COL32_WHITE, k.c_str(), CENTER);
    return w;
}

void tm_legend(ImDrawList *dl, const Frame &D)
{
    const float s = D.s();
    const char *const lines[4] = { "Defenses in a row", "Hot streak, 5 or more", "Front line, battles here", "Challenge called" };
    for (int i = 0; i < 4; i++) {
        float y = 250 + i * 31.0f;
        ImVec2 ic = D.p(50, y);
        if (i == 0) tm_shield(dl, ImVec2(ic.x + 6 * s, ic.y + 2 * s), 1.1f * s, TM_CREAM);
        else if (i == 1) tm_flame(dl, ImVec2(ic.x + 6 * s, ic.y + 1 * s), 1.1f * s, TM_ORANGE);
        else if (i == 2) {
            ImVec2 z[7];
            for (int k = 0; k < 7; k++) z[k] = D.p(50 + k * 4.0f, y + ((k & 1) ? 3 : 10));
            dl->AddPolyline(z, 7, TM_LIVE, ImDrawFlags_None, 2.4f * s);
        } else {
            for (int k = 0; k < 3; k++) dl->AddLine(D.p(51 + k * 9.0f, y + 8), D.p(57 + k * 9.0f, y + 8), TM_YELLOW, 3 * s);
        }
        tm_text(dl, g_body, 17 * s, D.p(86, y - 2), IM_COL32(242, 230, 200, 158), lines[i]);
    }
}

void tm_season(ImDrawList *dl, const Frame &D, const TurfMap &m, const TurfSeason &ss)
{
    const float s = D.s(), x = 1100, y = 24, w = 460, h = 206;
    tm_glass(dl, D, x, y, w, h);
    tm_glow(dl, D.p(x + w - 80, y + 70), 110 * s, 110 * s, tm_rgba(255, 168, 26, 0.18f), tm_rgba(255, 168, 26, 0));
    int season = m.season ? m.season : ss.season;
    std::string head = "SEASON " + std::to_string(season);
    tm_text(dl, g_title, 17 * s, D.p(x + 24, y + 18), TM_ORANGE, head.c_str());
    long long now = turf_now(m), ends = m.ends ? m.ends : ss.ends, started = tm_started(m);
    if (ends) {
        time_t te = (time_t)ends;
        struct tm a = {};
        localtime_s(&a, &te);
        static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        char line[48];
        snprintf(line, sizeof(line), "Map resets %s %d", months[a.tm_mon], a.tm_mday);
        tm_text(dl, g_body, 17 * s, D.p(x + w - 24, y + 18), IM_COL32(242, 230, 200, 153), line, RIGHT);
    }
    std::string name = m.season_name.empty() ? turf_month(m.month.empty() ? ss.month : m.month) : upper(m.season_name.c_str());
    float npx = fit_size(D, tm_serif(), 46, name.c_str(), w - 48);
    tm_serif_text(dl, npx * s, D.p(x + 24, y + 40), name.c_str(), 3 * s);
    // the weeks (their width first: the countdown is fitted beside them)
    int weeks = 0, week = 0;
    if (started && ends > started) {
        weeks = (int)((ends - started + 7 * 86400 - 1) / (7 * 86400));
        week = (int)((now - started) / (7 * 86400)) + 1;
        weeks = std::max(1, std::min(8, weeks));
        week = std::max(1, std::min(weeks, week));
    }
    const float step = 22, bar = 18;
    float wx0 = x + w - 24 - weeks * step + (step - bar);
    // the countdown: big condensed numbers with a soft glow, the units under them in orange
    long long left = std::max(0ll, ends - now);
    long long parts[3] = { left / 86400, (left % 86400) / 3600, (left % 3600) / 60 };
    const char *const units[3] = { "DAYS", "HRS", "MIN" };
    ImFont *nf = g_numbers ? g_numbers : g_title;
    char nums[3][8];
    for (int i = 0; i < 3; i++) snprintf(nums[i], sizeof(nums[i]), "%02lld", parts[i]);
    float npx2 = 100, gap = 26;     // (ImGui sizes a font by its line, Anton's caps are about 0.65 of it)
    {
        float total = 2 * gap;
        for (auto &n : nums) total += tm_width(nf, npx2 * s, n) / s;
        float room = (weeks ? wx0 - 12 : x + w - 24) - (x + 24);
        if (total > room) { npx2 *= room / total; gap *= room / total; }
    }
    float ny = y + 160 - npx2 * 0.85f, cx = x + 24;
    for (int i = 0; i < 3; i++) {
        float nw = tm_width(nf, npx2 * s, nums[i]) / s;
        ImVec2 c = D.p(cx + nw / 2, ny + npx2 * 0.53f);
        tm_glow(dl, c, nw * 0.95f * s, npx2 * 0.62f * s, tm_rgba(255, 196, 110, 0.20f), tm_rgba(255, 196, 110, 0), 0, 24);
        for (int k = 0; k < 8; k++) {      // the halo: the number again, faint, a little off on every side
            float a = k * 0.7854f;
            tm_text(dl, nf, npx2 * s, D.p(cx + cosf(a) * 2.4f, ny + sinf(a) * 2.4f), IM_COL32(255, 214, 150, 22), nums[i]);
        }
        int from = dl->VtxBuffer.Size;
        tm_text(dl, nf, npx2 * s, D.p(cx, ny), TM_CREAM, nums[i]);
        tm_shade(dl, from, D.p(0, ny + npx2 * 0.2f).y, D.p(0, ny + npx2 * 1.1f).y, IM_COL32(255, 248, 228, 255), IM_COL32(236, 214, 172, 255));
        tm_text(dl, g_title, 13 * s, D.p(cx, y + 168), TM_ORANGE, units[i]);
        cx += nw;
        if (i < 2) {        // the separators: two small squares
            float sx = cx + gap / 2 - 3, sy = ny + npx2 * 0.52f;
            dl->AddRectFilled(D.p(sx, sy - 9), D.p(sx + 6, sy - 3), IM_COL32(242, 230, 200, 90));
            dl->AddRectFilled(D.p(sx, sy + 7), D.p(sx + 6, sy + 13), IM_COL32(242, 230, 200, 90));
            cx += gap;
        }
    }
    if (weeks) {
        for (int i = 0; i < weeks; i++) {
            float bx = wx0 + i * step;
            ImU32 c = i < week ? TM_ORANGE : IM_COL32(242, 230, 200, 38);
            if (i == week - 1) {
                tm_glow(dl, D.p(bx + bar / 2, y + 154), 18 * s, 8 * s, tm_rgba(255, 168, 26, 0.45f), tm_rgba(255, 168, 26, 0), 0, 16);
                dl->AddRectFilled(D.p(bx - 2, y + 149), D.p(bx + bar + 2, y + 159), with_alpha(TM_ORANGE, 0.3f));
            }
            dl->AddRectFilled(D.p(bx, y + 150), D.p(bx + bar, y + 158), c);
        }
        char wl[32];
        snprintf(wl, sizeof(wl), "WEEK %d OF %d", week, weeks);
        tm_text(dl, g_title, 13 * s, D.p(x + w - 24, y + 168), IM_COL32(242, 230, 200, 140), wl, RIGHT);
    }
}

// The standings' rows on the map: the top five, the player's crew in the last row when it is lower.
std::vector<TurfCrew> tm_top(const TurfMap &m, const TurfSeason &ss)
{
    std::vector<TurfCrew> out(ss.crews.begin(), ss.crews.begin() + std::min<size_t>(5, ss.crews.size()));
    if (ss.crews.empty()) {     // not loaded yet: the venues each crew holds
        std::map<long long, TurfCrew> held;
        for (auto &v : m.venues) {
            if (!v.owner) continue;
            TurfCrew &c = held[v.owner];
            c.id = v.owner; c.tag = v.otag; c.name = v.oname; c.venues++;
        }
        for (auto &e : held) out.push_back(e.second);
        std::sort(out.begin(), out.end(), [](const TurfCrew &a, const TurfCrew &b) { return a.venues > b.venues; });
        for (size_t i = 0; i < out.size(); i++) out[i].place = (int)i + 1;
        if (out.size() > 5) out.resize(5);
    }
    bool in = false;
    for (auto &c : out) in = in || c.id == m.my_crew;
    if (!in && m.my_crew)
        for (auto &c : ss.crews)
            if (c.id == m.my_crew) { if (out.size() == 5) out.back() = c; else out.push_back(c); }
    return out;
}

bool tm_standings(ImDrawList *dl, const Frame &D, const TurfMap &m, const TurfSeason &ss)
{
    const float s = D.s(), x = 1100, y = 242, w = 460, h = 262;
    tm_glass(dl, D, x, y, w, h);
    tm_text(dl, g_title, 15 * s, D.p(x + 24, y + 16), IM_COL32(242, 230, 200, 140), "WHO RUNS NEW YORK");
    tm_text(dl, g_title, 15 * s, D.p(x + w - 24, y + 16), IM_COL32(242, 230, 200, 140), "VENUES", RIGHT);
    std::vector<TurfCrew> top = tm_top(m, ss);
    bool held = false;
    for (auto &c : top) held = held || c.venues > 0;
    if (!held) top.clear();
    if (top.empty()) {      // nobody holds turf yet: the whole city is up for grabs
        int open = 0;
        for (auto &v : m.venues) open += !v.owner && !v.boss;
        int from = dl->VtxBuffer.Size;
        ImVec2 c = D.p(x + w / 2, y + 96);
        tm_text(dl, g_hand, 40 * s, ImVec2(c.x, c.y - 26 * s), TM_YELLOW, "UP FOR GRABS", CENTER);
        tm_turn(dl, from, c, -3);
        char line[96];
        snprintf(line, sizeof(line), "%d venue%s open turf, no crew holds one yet.", open, open == 1 ? " is" : "s are");
        tm_text(dl, g_body, 18 * s, D.p(x + w / 2, y + 140), IM_COL32(242, 230, 200, 200), line, CENTER);
        tm_text(dl, g_body, 18 * s, D.p(x + w / 2, y + 164), IM_COL32(242, 230, 200, 150),
                m.officer ? "Claim one and put your crew on the map." : "A Boss or a Lieutenant claims open turf for a crew.", CENTER);
    }
    for (size_t i = 0; i < top.size(); i++) {
        const TurfCrew &c = top[i];
        float ry = y + 44 + i * 42.0f;
        bool mine = m.my_crew && c.id == m.my_crew;
        ImU32 col = tm_crew(m, c.id);
        if (mine) {
            dl->AddRectFilled(D.p(x + 14, ry), D.p(x + w - 14, ry + 40), with_alpha(col, 0.13f), 8 * s);
            dl->AddRectFilled(D.p(x + 14, ry), D.p(x + 17, ry + 40), col, 2 * s);
        }
        if (c.place == 1) tm_crown(dl, D.p(x + 24, ry + 12), s, TM_YELLOW);
        else tm_text(dl, g_title, 18 * s, D.p(x + 26, ry + 9), mine ? col : IM_COL32(242, 230, 200, 115), std::to_string(c.place).c_str());
        dl->AddRectFilled(D.p(x + 52, ry + 6), D.p(x + 58, ry + 34), col);
        tm_glow(dl, D.p(x + 55, ry + 20), 10 * s, 18 * s, with_alpha(col, 0.5f), with_alpha(col, 0));
        std::string cn = upper(c.name.c_str());
        float npx = fit_size(D, g_title, 20, cn.c_str(), 210) * s;
        tm_text(dl, g_title, npx, D.p(x + 68, ry + 8), mine ? tm_light(col, 0.6f) : TM_CREAM, cn.c_str());
        float tx = x + 68 + tm_width(g_title, npx, cn.c_str()) / s + 8;
        std::string tag = mine ? "YOU" : upper(c.tag.c_str());
        float tw = tm_width(g_title, 12 * s, tag.c_str()) / s + 10;
        dl->AddRectFilled(D.p(tx, ry + 12), D.p(tx + tw, ry + 28), mine ? col : tm_light(col, 0.25f), 3 * s);
        tm_text(dl, g_title, 12 * s, D.p(tx + 5, ry + 13), TM_INK, tag.c_str());
        // a bar per venue held (up to 8), then the count
        int n = std::min(8, c.venues);
        float bx = x + w - 66 - n * 12;
        for (int k = 0; k < n; k++) {
            ImVec2 a = D.p(bx + k * 12 + 3, ry + 13), b = D.p(bx + k * 12 + 12, ry + 27);
            ImVec2 q[4] = { ImVec2(a.x + 1.5f * s, a.y), ImVec2(b.x + 1.5f * s, a.y), ImVec2(b.x - 1.5f * s, b.y), ImVec2(a.x - 1.5f * s, b.y) };
            dl->AddConvexPolyFilled(q, 4, col);
        }
        tm_text(dl, g_title, 28 * s, D.p(x + w - 24, ry + 2), TM_CREAM, std::to_string(c.venues).c_str(), RIGHT);
    }
    return mouse_click(D, x, y, w, h);
}

// The player's crew and its next turf battle.
void tm_mycrew(ImDrawList *dl, const Frame &D, const TurfMap &m, const TurfSeason &ss, bool signed_in)
{
    const float s = D.s(), x = 1100, y = 516, w = 460, h = 308;
    const TurfCrew *mc = nullptr;
    for (auto &c : ss.crews) if (c.id == m.my_crew) mc = &c;
    std::string tag, name;
    int held = 0;
    for (auto &v : m.venues) if (m.my_crew && v.owner == m.my_crew) { held++; tag = v.otag; name = v.oname; }
    if (mc) { tag = mc->tag; name = mc->name; }
    if (tag.empty() && m.my_crew && C.v.crew == m.my_crew) { tag = C.v.crew_tag; name = C.v.crew_name; }
    for (auto &v : m.venues) if (tag.empty() && m.my_crew && v.chal_crew == m.my_crew) { tag = v.ctag; name = v.cname; }
    ImU32 col = m.my_crew ? tm_crew(m, m.my_crew) : TM_ORANGE;
    tm_glass(dl, D, x, y, w, h, col);
    if (!signed_in || !m.my_crew) {
        tm_text(dl, g_title, 13 * s, D.p(x + 24, y + 22), IM_COL32(242, 230, 200, 140), "YOUR CREW");
        tm_text(dl, g_title, 28 * s, D.p(x + 24, y + 42), TM_CREAM, signed_in ? "NOT IN A CREW YET" : "NOT SIGNED IN");
        text(dl, D, g_body, 18, x + 24, y + 86, IM_COL32(242, 230, 200, 170),
             signed_in ? "Join a crew or start one in the chat's CREW tab to fight for turf." : "Sign in with Discord to fight for turf with your crew.",
             LEFT, 0, w - 48);
        return;
    }
    // the crew's badge, in its colour: a glowing box with the crew's flame and its tag on a plate under it
    tm_glow(dl, D.p(x + 53, y + 45), 46 * s, 46 * s, with_alpha(col, 0.22f), with_alpha(col, 0));
    dl->AddRectFilledMultiColor(D.p(x + 24, y + 16), D.p(x + 82, y + 74), IM_COL32(30, 20, 14, 255), IM_COL32(30, 20, 14, 255),
                                tm_mix(IM_COL32(30, 20, 14, 255), col, 0.22f), tm_mix(IM_COL32(30, 20, 14, 255), col, 0.22f));
    dl->AddRect(D.p(x + 23, y + 15), D.p(x + 83, y + 75), with_alpha(col, 0.25f), 13 * s, ImDrawFlags_None, 5 * s);
    dl->AddRect(D.p(x + 25.5f, y + 17.5f), D.p(x + 80.5f, y + 72.5f), col, 11 * s, ImDrawFlags_None, 3 * s);
    tm_flame(dl, D.p(x + 53 - 6 * 2.1f, y + 21), 2.1f * s, col, tm_light(col, 0.55f));
    std::string ut = upper(tag.c_str());
    {
        float tpx = fit_size(D, g_title, 13, ut.c_str(), 46);
        float tw = tm_width(g_title, tpx * s, ut.c_str()) / s + 8;
        dl->AddRectFilled(D.p(x + 53 - tw / 2, y + 62), D.p(x + 53 + tw / 2, y + 78), col, 3 * s);
        tm_text(dl, g_title, tpx * s, D.p(x + 53, y + 63), TM_INK, ut.c_str(), CENTER);
    }
    tm_text(dl, g_title, 13 * s, D.p(x + 96, y + 16), IM_COL32(242, 230, 200, 140), "YOUR CREW");
    std::string un = upper(name.c_str());
    tm_text(dl, g_title, fit_size(D, g_title, 28, un.c_str(), w - 120) * s, D.p(x + 96, y + 32), tm_light(col, 0.6f), un.c_str());
    char line[96];
    if (mc && mc->place > 0) snprintf(line, sizeof(line), "Rank %d with %d venue%s", mc->place, held, held == 1 ? "" : "s");
    else snprintf(line, sizeof(line), "%d venue%s, no points yet", held, held == 1 ? "" : "s");
    std::string l = line;
    if (m.members > 0) l += ", " + std::to_string(m.online) + " member" + (m.online == 1 ? "" : "s") + " online";
    tm_text(dl, g_body, 17 * s, D.p(x + 96, y + 66), IM_COL32(242, 230, 200, 165), l.c_str());
    // the next turf battle: a battle under way, else the challenge due first
    long long now = turf_now(m);
    const TurfLive *lv = nullptr;
    for (auto &e : m.live) if (e.crew[0] == m.my_crew || e.crew[1] == m.my_crew) lv = &e;
    const TurfVenue *next = nullptr;
    for (auto &v : m.venues)
        if (v.chal && (v.chal_crew == m.my_crew || v.owner == m.my_crew) && (!next || v.deadline < next->deadline)) next = &v;
    float bx = x + 24, by = y + 98, bw = w - 48, bh = 186;
    ImVec2 a = D.p(bx, by), b = D.p(bx + bw, by + bh);
    dl->AddRectFilledMultiColor(a, b, tm_rgba(255, 168, 26, 0.22f), tm_rgba(200, 16, 46, 0.28f), tm_rgba(200, 16, 46, 0.28f),
                                tm_rgba(255, 168, 26, 0.22f));
    {
        int from = dl->VtxBuffer.Size;
        tm_text(dl, g_hand, 96 * s, D.p(bx + bw - 120, by - 14), IM_COL32(0, 0, 0, 64), "VS");
        tm_turn(dl, from, D.p(bx + bw - 70, by + 40), -8);
    }
    if (!lv && !next) {
        tm_text(dl, g_title, 15 * s, D.p(bx + 14, by + 12), TM_YELLOW, "NEXT CHALLENGE");
        int open = 0;
        for (auto &v : m.venues) open += !v.owner && !v.boss;
        bool held_any = false;
        for (auto &v : m.venues) held_any = held_any || v.owner;
        tm_text(dl, g_hand, 26 * s, D.p(bx + 14, by + 44), TM_CREAM, !held_any && open ? "THE CITY IS OPEN" : "NONE CALLED");
        const char *hintl = m.officer ? (!held_any ? "Pick an open venue on the map and claim it."
                                         : open ? "Pick an open venue on the map and claim it, or call out a crew that holds one."
                                                : "Pick a venue on the map and call its crew out.")
                                      : "Your Boss or a Lieutenant claims open turf and calls out other crews.";
        text(dl, D, g_body, 17, bx + 14, by + 90, IM_COL32(242, 230, 200, 170), hintl, LEFT, 0, bw - 28);
        return;
    }
    long long them = 0;
    std::string ttag, venue;
    long long due = 0;
    int format = 3;
    if (lv) {
        int me = lv->crew[0] == m.my_crew ? 0 : 1;
        them = lv->crew[1 - me];
        ttag = lv->tag[1 - me];
        format = lv->format ? lv->format : 3;
        for (auto &v : m.venues) if (v.id == lv->venue) venue = v.name;
    } else {
        them = next->chal_crew == m.my_crew ? next->owner : next->chal_crew;
        ttag = next->chal_crew == m.my_crew ? next->otag : next->ctag;
        venue = next->name;
        due = next->deadline;
    }
    tm_text(dl, g_title, 15 * s, D.p(bx + 14, by + 12), TM_YELLOW, lv ? "LIVE NOW" : "NEXT CHALLENGE");
    float hx = bx + 14;
    tm_text(dl, g_title, 26 * s, D.p(hx, by + 32), col, ut.c_str());
    hx += tm_width(g_title, 26 * s, ut.c_str()) / s + 10;
    tm_text(dl, g_hand, 22 * s, D.p(hx, by + 36), TM_CREAM, "vs");
    hx += tm_width(g_hand, 22 * s, "vs") / s + 10;
    std::string tt = upper(ttag.c_str());
    tm_text(dl, g_title, 26 * s, D.p(hx, by + 32), tm_light(tm_crew(m, them), 0.25f), tt.c_str());
    hx += tm_width(g_title, 26 * s, tt.c_str()) / s + 14;
    tm_text(dl, g_title, 18 * s, D.p(hx, by + 39), IM_COL32(242, 230, 200, 153), "FOR");
    std::string vn = upper(venue.c_str());
    tm_text(dl, g_hand, fit_size(D, g_hand, 30, vn.c_str(), bw - 28) * s, D.p(bx + 14, by + 68), IM_COL32(255, 245, 230, 255), vn.c_str());
    char big[32], small_line[64];
    if (lv) {
        snprintf(big, sizeof(big), "ON NOW");
        snprintf(small_line, sizeof(small_line), "%d v %d, watch it from the map", format, format);
    } else {
        long long d = std::max(0ll, due - now);
        if (d >= 86400) snprintf(big, sizeof(big), "%lldd %02lld:%02lld", d / 86400, (d % 86400) / 3600, (d % 3600) / 60);
        else snprintf(big, sizeof(big), "%02lld:%02lld:%02lld", d / 3600, (d % 3600) / 60, d % 60);
        snprintf(small_line, sizeof(small_line), "%s %s", next->chal_battle ? "battle set up, due" : "play it by", tm_when(due, now).c_str());
    }
    tm_text(dl, g_title, 30 * s, D.p(bx + 14, by + 120), TM_CREAM, big);
    tm_text(dl, g_body, 15 * s, D.p(bx + 14, by + 154), IM_COL32(242, 230, 200, 165), small_line);
    // who of the crew is on: one box per fighter the battle needs
    int on = std::min(format, std::max(0, m.online));
    for (int i = 0; i < format; i++) {
        float qx = bx + bw - 14 - (format - i) * 20.0f;
        if (i < on) dl->AddRectFilled(D.p(qx, by + 124), D.p(qx + 16, by + 146), col, 3 * s);
        else dl->AddRect(D.p(qx + 1, by + 125), D.p(qx + 15, by + 145), IM_COL32(242, 230, 200, 128), 3 * s, ImDrawFlags_None, 2 * s);
    }
    char ready[48];
    snprintf(ready, sizeof(ready), "%d of %d online", on, format);
    tm_text(dl, g_body, 15 * s, D.p(bx + bw - 14, by + 154), IM_COL32(242, 230, 200, 190), ready, RIGHT);
}

float tm_hint(ImDrawList *dl, const Frame &D, float x, float y, const char *button, const char *word);     // below

// The call to open turf, under the legend while any venue is open: how many, and how to take one. True on
// a click (the page then picks the next open venue).
bool tm_open_call(ImDrawList *dl, const Frame &D, const TurfMap &m, bool signed_in, float t)
{
    int open = 0, all = 0;
    for (auto &v : m.venues) if (!v.boss) { all++; open += !v.owner; }
    if (!open) return false;
    const float s = D.s(), x = 46, y = 404, w = 330, h = 166;
    float beat = 0.5f + 0.5f * sinf(t * 2.4f);
    tm_glass(dl, D, x, y, w, h, tm_mix(TM_YELLOW, TM_ORANGE, beat));
    tm_glow(dl, D.p(x + w - 70, y + 50), 120 * s, 80 * s, tm_rgba(255, 205, 60, 0.10f + 0.06f * beat), tm_rgba(255, 205, 60, 0));
    // the beacon and the kicker
    dl->AddCircleFilled(D.p(x + 30, y + 27), 6 * s, TM_YELLOW, 14);
    dl->AddCircle(D.p(x + 30, y + 27), (8 + 10 * beat) * s, with_alpha(TM_YELLOW, 0.7f * (1 - beat)), 20, 2 * s);
    tm_text(dl, g_title, 16 * s, D.p(x + 46, y + 17), TM_YELLOW, "OPEN TURF");
    // the count, big, in the serif
    char big[40];
    if (open == all) snprintf(big, sizeof(big), "ALL %d VENUES", open);
    else snprintf(big, sizeof(big), "%d VENUE%s", open, open == 1 ? "" : "S");
    float bpx = fit_size(D, tm_serif(), 42, big, w - 48);
    tm_serif_text(dl, bpx * s, D.p(x + 24, y + 44), big, 3 * s);
    {
        int from = dl->VtxBuffer.Size;
        ImVec2 c = D.p(x + w - 92, y + 112);
        tm_text(dl, g_hand, 26 * s, ImVec2(c.x, c.y - 14 * s), TM_YELLOW, open == 1 ? "UP FOR GRABS" : "COME CLAIM IT", CENTER);
        tm_turn(dl, from, c, -5);
    }
    // how to take one, for who may
    bool officer = signed_in && m.my_crew && m.officer;
    if (officer) {
        float hx = x + 22;
        hx += tm_hint(dl, D, hx, y + 98, "a", "CLAIM");
        tm_text(dl, g_body, 16 * s, D.p(x + 22, y + 134), IM_COL32(242, 230, 200, 190), "on a venue with a pulsing ring");
    } else {
        const char *why = !signed_in ? "Sign in with Discord to fight for turf." : !m.my_crew ? "Join a crew to fight for turf."
                                                                                                : "Your Boss or a Lieutenant can claim it.";
        text(dl, D, g_body, 16, x + 22, y + 104, IM_COL32(242, 230, 200, 200), why, LEFT, 0, w - 150);
    }
    return mouse_click(D, x, y, w, h);
}

// The street wire: the map's recent events running along the bottom.
struct TmSeg { std::string text; ImU32 col; ImFont *font; float px; };

void tm_event_segs(const TurfMap &m, const TurfEv &e, long long now, std::vector<TmSeg> &out, float s)
{
    ImU32 cc = tm_light(tm_crew(m, e.crew), 0.2f), oc = tm_light(tm_crew(m, e.other), 0.2f);
    ImU32 plain = IM_COL32(242, 230, 200, 218);
    std::string C = upper(e.cname.c_str()), O = upper(e.oname.c_str()), V = upper(e.vname.c_str());
    auto add = [&](const std::string &t, ImU32 c) { out.push_back({ t, c, g_title, 19 * s }); };
    if (e.kind == "taken") { add(C, cc); add(" took " + V + " from ", plain); add(O, oc); }
    else if (e.kind == "claimed") { add(C, cc); add(" claimed " + V, plain); }
    else if (e.kind == "defended") {
        int n = 0;
        for (auto &v : m.venues) if (v.id == e.venue && v.owner == e.crew) n = v.defended;
        add(C, cc);
        add(" held " + V + (n >= 2 ? ", " + std::to_string(n) + " in a row" : ""), plain);
    } else if (e.kind == "challenged") { add(C, cc); add(" called out ", plain); add(O, oc); add(" for " + V, plain); }
    else if (e.kind == "handed") { add(C, cc); add(" took " + V + ", ", plain); add(O, oc); add(" never showed", plain); }
    else if (e.kind == "lapsed") { add(C, cc); add("'s challenge for " + V + " ran out", plain); }
    else if (e.kind == "cancelled") { add(C, cc); add(" called off the challenge for " + V, plain); }
    else if (e.kind == "freed") { add(V + " is open turf again", plain); }
    else if (e.kind == "season" && e.crew) { add(C, cc); add(" ran New York last season", plain); }
    else add(upper(e.text.c_str()), plain);
    out.push_back({ "  " + tm_ago(now - e.at), IM_COL32(242, 230, 200, 115), g_body, 17 * s });
}

void tm_ticker(ImDrawList *dl, const Frame &D, ImVec2 display, const TurfMap &m, float t)
{
    const float s = D.s();
    float y0 = D.p(0, 842).y, y1 = D.p(0, 880).y;
    dl->AddRectFilledMultiColor(ImVec2(0, y0), ImVec2(display.x, y1), IM_COL32(6, 4, 6, 242), IM_COL32(14, 8, 10, 230),
                                IM_COL32(14, 8, 10, 230), IM_COL32(6, 4, 6, 242));
    dl->AddLine(ImVec2(0, y0), ImVec2(display.x, y0), IM_COL32(255, 59, 84, 128), 1);
    dl->AddLine(ImVec2(0, y1), ImVec2(display.x, y1), IM_COL32(255, 59, 84, 64), 1);
    // the run of events, twice for a seamless loop
    long long now = turf_now(m);
    std::vector<TmSeg> run;
    for (auto &e : m.events) {
        tm_event_segs(m, e, now, run, s);
        run.push_back({ "", 0, g_title, 46 * s });     // the gap
    }
    float head_w = D.p(0, 0).x + 230 * s;      // the red head's right edge (pixels)
    if (run.empty()) {
        run.push_back({ "Quiet streets: no turf has changed hands this season yet.", IM_COL32(242, 230, 200, 160), g_body, 18 * s });
        run.push_back({ "", 0, g_title, 160 * s });
    }
    float total = 0;
    for (auto &g : run) total += g.text.empty() ? g.px : tm_width(g.font, g.px, g.text.c_str());
    float speed = 60 * s, x = head_w + 30 * s - (gallery_on() ? 0 : fmodf(t * speed, std::max(1.0f, total)));
    dl->PushClipRect(ImVec2(head_w, y0), ImVec2(display.x, y1), true);
    for (int rep = 0; rep < 3 && x < display.x; rep++)
        for (auto &g : run) {
            if (g.text.empty()) { x += g.px; continue; }
            float w = tm_width(g.font, g.px, g.text.c_str());
            if (x + w > head_w && x < display.x) tm_text(dl, g.font, g.px, ImVec2(x, (y0 + y1) / 2 - g.px * 0.55f), g.col, g.text.c_str());
            x += w;
        }
    dl->PopClipRect();
    // the head
    ImVec2 q[4] = { ImVec2(0, y0), ImVec2(head_w, y0), ImVec2(head_w - 18 * s, y1), ImVec2(0, y1) };
    dl->AddConvexPolyFilled(q, 4, TM_RED);
    ImVec2 hp = D.p(48, 861);
    dl->AddCircleFilled(ImVec2(hp.x + 7 * s, hp.y), 7 * s, IM_COL32(255, 245, 230, 255), 14);
    tm_text(dl, g_title, 18 * s, ImVec2(hp.x + 22 * s, hp.y - 10 * s), IM_COL32(255, 245, 230, 255), "STREET WIRE");
}

// A pad hint in the design's size (the game's glyphs, or the PlayStation ones); its width in design units.
float tm_hint(ImDrawList *dl, const Frame &D, float x, float y, const char *button, const char *word)
{
    Frame h = D;
    const float k = 1.62f;
    h.sx = h.sy = D.s() * k;
    return hint(dl, h, x / k, y / k, button, word) * k;
}

// ---- the page ----

// The venue in a direction from the selected one (the nearest within about 60 degrees of it).
int tm_step(const TurfMap &m, const TmBoard &b, int from, float dx, float dy, int region)
{
    if (from < 0) return m.venues.empty() ? -1 : 0;
    int best = -1;
    float best_score = FLT_MAX;
    for (int i = 0; i < (int)m.venues.size(); i++) {
        if (i == from) continue;
        if (region > 0 && tm_borough_of(m.venues[i].borough) != region - 1) continue;
        float vx = b.pins[i].x - b.pins[from].x, vy = b.pins[i].y - b.pins[from].y, d = sqrtf(vx * vx + vy * vy);
        if (d < 1) continue;
        float cosa = (vx * dx + vy * dy) / d;
        if (cosa < 0.45f) continue;
        float score = d * (1 + 2.2f * (1 - cosa));
        if (score < best_score) { best_score = score; best = i; }
    }
    return best;
}

int tm_index_of(const TurfMap &m, int id)
{
    for (int i = 0; i < (int)m.venues.size(); i++) if (m.venues[i].id == id) return i;
    return -1;
}

const char *const TM_REGIONS[6] = { "ALL FIVE BOROUGHS", "THE BRONX", "MANHATTAN", "QUEENS", "BROOKLYN", "STATEN ISLAND" };

void tm_set_region(const TurfMap &m, int region)
{
    TW.region = (region % 6 + 6) % 6;
    if (TW.region == 0) return;
    int at = tm_index_of(m, TW.sel_id);
    if (at >= 0 && tm_borough_of(m.venues[at].borough) == TW.region - 1) return;
    for (auto &v : m.venues)
        if (tm_borough_of(v.borough) == TW.region - 1) { TW.sel_id = v.id; break; }
}

// The live battle the page offers to watch: the selected venue's, else the first.
const TurfLive *tm_watchable(const TurfMap &m)
{
    const TurfLive *l = tm_live_at(m, TW.sel_id);
    if (!l && !m.live.empty()) l = &m.live[0];
    return l && !l->room.empty() ? l : nullptr;
}

// The note under the page's hints (the service's answers: "Challenge sent ..."), drawn by draw_status.
void turf_board_note(ImDrawList *dl, const char *note, bool error)
{
    Frame D = tm_frame(ImGui::GetIO().DisplaySize);
    const float s = D.s();
    float w = tm_width(g_hand, 20 * s, note) + 36 * s;
    float x = D.p(800, 0).x - w / 2, y = D.p(0, 800).y;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 34 * s), IM_COL32(14, 10, 12, 235), 10 * s);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + 34 * s), error ? TM_LIVE : with_alpha(TM_ORANGE, 0.7f), 10 * s, ImDrawFlags_None, 2 * s);
    tm_text(dl, g_hand, 20 * s, ImVec2(x + 18 * s, y + 5 * s), error ? IM_COL32(255, 120, 130, 255) : TM_ORANGE, note);
}

void draw_turf_board(ImDrawList *dl, ImVec2 display, const Frame &f, const Pad &pad_in, const TurfMap &m, const TurfSeason &ss,
                     bool signed_in)
{
    Frame D = tm_frame(display);
    const float s = D.s();
    const float t = (float)ImGui::GetTime();
    TmBoard b = tm_board(m);
    long long now = turf_now(m);
    Pad pad = TW.picker ? Pad() : pad_in;
    if (tm_index_of(m, TW.sel_id) < 0 && !m.venues.empty()) TW.sel_id = m.venues[0].id;
    int sel = tm_index_of(m, TW.sel_id);

    // the input: the region (LB / RB), moving between pins, the details
    if (pad.lb) { tm_set_region(m, TW.region - 1); sel = tm_index_of(m, TW.sel_id); }
    if (pad.rb) { tm_set_region(m, TW.region + 1); sel = tm_index_of(m, TW.sel_id); }
    {
        float dx = pad.left ? -1.0f : pad.right ? 1.0f : 0.0f, dy = pad.up ? -1.0f : pad.down ? 1.0f : 0.0f;
        if (dx != 0 || dy != 0) {
            int to = tm_step(m, b, sel, dx, dy, TW.region);
            if (to < 0 && TW.region > 0) to = tm_step(m, b, sel, dx, dy, 0);    // leaving the borough: the whole map
            if (to >= 0) {
                TW.sel_id = m.venues[to].id;
                sel = to;
                TW.armed.clear();
                if (TW.region > 0 && tm_borough_of(m.venues[to].borough) != TW.region - 1) TW.region = 0;
            }
        }
    }
    // the camera glides to its region (the gallery takes it at once)
    {
        float gx, gy, gz, gmx, gmy;
        ImVec2 at;
        if (sel >= 0) at = ImVec2(b.pins[sel].x, b.pins[sel].y);
        tm_goal(TW.region, sel >= 0 ? &at : nullptr, gx, gy, gz, gmx, gmy);
        float dt = ImGui::GetIO().DeltaTime, a = TW.cam_snap || gallery_on() ? 1.0f : 1.0f - expf(-dt * 6.0f);
        TW.cam_snap = false;
        TW.cam[0] += (gx - TW.cam[0]) * a; TW.cam[1] += (gy - TW.cam[1]) * a;
        TW.cam[2] *= powf(gz / TW.cam[2], a);
        TW.cam[3] += (gmx - TW.cam[3]) * a; TW.cam[4] += (gmy - TW.cam[4]) * a;
    }
    TmView v;
    v.D = D;
    v.cx = TW.cam[0]; v.cy = TW.cam[1]; v.z = TW.cam[2]; v.mx = TW.cam[3]; v.my = TW.cam[4];

    // the city, its lines, its names
    tm_city(dl, v, display, m, b, t);
    tm_names(dl, v, m, b);
    tm_lines(dl, v, m, b, t);
    // the spotlight on a live fight
    for (auto &l : m.live) {
        int i = tm_index_of(m, l.venue);
        if (i < 0) continue;
        float px = b.pins[i].x, py = b.pins[i].y;
        tm_quad(dl, v.w(px - 344, py - 446), v.w(px - 294, py - 466), v.w(px + 26, py - 16), v.w(px - 24, py + 24),
                IM_COL32(255, 240, 220, 0), IM_COL32(255, 240, 220, 0), IM_COL32(255, 225, 200, 50), IM_COL32(255, 225, 200, 50));
        tm_glow(dl, v.w(px, py), 58 * s, 40 * s, IM_COL32(255, 235, 215, 30), IM_COL32(255, 235, 215, 0));
    }
    tm_grain(dl, ImVec2(0, 0), display, 26);
    // the vignette and the pins
    {
        ImVec2 c = D.p(576, 480);
        float rx = 1200 * s, ry = 720 * s;
        tm_ring(dl, c, rx * 0.55f, ry * 0.55f, rx, ry, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 200));
        tm_ring(dl, c, rx, ry, rx * 3, ry * 3, IM_COL32(0, 0, 0, 200), IM_COL32(0, 0, 0, 200));
    }
    // the player's next challenge's venue gets the yellow strip
    int next_id = 0;
    long long next_due = 0;
    for (auto &tv : m.venues)
        if (m.my_crew && tv.chal && (tv.chal_crew == m.my_crew || tv.owner == m.my_crew) && (!next_id || tv.deadline < next_due)) {
            next_id = tv.id;
            next_due = tv.deadline;
        }
    // (the pins stay on the map's part of the screen: left of the panels, above the street wire)
    std::vector<TmPlate> plates(m.venues.size());
    ImVec2 clip1(D.p(1088, 0).x, D.p(0, 842).y);
    dl->PushClipRect(ImVec2(0, 0), clip1, true);
    for (int i = 0; i < (int)m.venues.size(); i++)
        if (i != sel) tm_venue(dl, v, m, m.venues[i], b.pins[i], false, m.venues[i].id == next_id, now, t, plates[i]);
    if (sel >= 0) tm_venue(dl, v, m, m.venues[sel], b.pins[sel], true, m.venues[sel].id == next_id, now, t, plates[sel]);
    dl->PopClipRect();
    for (int i = 0; i < (int)m.venues.size(); i++) {      // a pin out of that part takes no mouse
        ImVec2 c = v.w(b.pins[i].x, b.pins[i].y);
        if (c.x > clip1.x || c.y > clip1.y || c.x < 0 || c.y < 0) plates[i] = { -9999, -9999, -9999, -9999 };
    }

    // the mouse: a pin or its plate picks the venue, a click opens its details
    if (!TW.picker) {
        Frame px;        // pixels
        for (int i = (int)m.venues.size() - 1; i >= 0; i--) {
            ImVec2 c = v.w(b.pins[i].x, b.pins[i].y);
            const TmPlate &p = plates[i];
            if (plates[i].x0 < -9000) continue;
            bool on_pin = mouse_pick(px, c.x - 16 * s, c.y - 16 * s, 32 * s, 32 * s) ||
                          mouse_pick(D, p.x0, p.y0, p.x1 - p.x0, p.y1 - p.y0);
            bool click = mouse_click(px, c.x - 16 * s, c.y - 16 * s, 32 * s, 32 * s) ||
                         mouse_click(D, p.x0, p.y0, p.x1 - p.x0, p.y1 - p.y0);
            if (on_pin && TW.sel_id != m.venues[i].id) { TW.sel_id = m.venues[i].id; sel = i; TW.armed.clear(); }
            if (click) { TW.details = true; break; }
        }
    }

    // the header (on a shade of night, so the map under it never fights the title), the legend
    tm_glow(dl, D.p(250, 110), 560 * s, 250 * s, IM_COL32(3, 3, 7, 215), IM_COL32(3, 3, 7, 0), 0, 40);
    tm_header(dl, D, TW.region);
    float y0 = 22 + tm_title_px(D) * 1.02f + 30;
    {
        float lx = 46;
        float lw = tm_plate_key(dl, D, lx, y0, "{LB}");
        if (mouse_click(D, lx, y0, lw, 24)) tm_set_region(m, TW.region - 1);
        lx += lw + 12;
        tm_text(dl, g_title, 26 * s, D.p(lx, y0 - 3), TM_CREAM, TM_REGIONS[TW.region]);
        lx += tm_width(g_title, 26 * s, TM_REGIONS[TW.region]) / s + 12;
        float rw = tm_plate_key(dl, D, lx, y0, "{RB}");
        if (mouse_click(D, lx, y0, rw, 24)) tm_set_region(m, TW.region + 1);
    }
    tm_legend(dl, D);
    if (TW.region == 0 && !TW.picker && tm_open_call(dl, D, m, signed_in, t)) {     // a click: the next open venue
        int n = (int)m.venues.size();
        for (int k = 1; k <= n; k++) {
            const TurfVenue &e = m.venues[((sel < 0 ? 0 : sel) + k) % n];
            if (!e.owner && !e.boss) { TW.sel_id = e.id; TW.armed.clear(); break; }
        }
    }

    // the right column: the season, and the standings and the player's crew, or the venue's details
    tm_season(dl, D, m, ss);
    sel = tm_index_of(m, TW.sel_id);
    if (pad.x) { TW.details = !TW.details; TW.armed.clear(); }
    bool opened_now = false;
    if (pad.a && !TW.details && sel >= 0) { TW.details = true; opened_now = true; }
    (void)opened_now;
    std::vector<TurfAction> acts;
    std::string why;
    if (sel >= 0) turf_actions(m, m.venues[sel], signed_in, acts, why);
    if (TW.details && sel >= 0) {
        // turf_detail's 260 x TURF_DETAIL_H panel at 346,124 of the menu's space, over the standings and the crew panel
        const float kk = 460.0f / 260.0f;
        Frame fd;
        fd.sx = fd.sy = s * kk;
        ImVec2 at = D.p(1100, 242);
        fd.o = ImVec2(at.x - 346 * fd.sx, at.y - 124 * fd.sy);
        dl->AddRectFilled(D.p(1100, 242), D.p(1560, 242 + TURF_DETAIL_H * kk), IM_COL32(14, 10, 12, 140), 16 * s);
        Pad dp = pad;
        if (acts.size() < 2) dp.y = false;
        turf_detail(dl, fd, dp, m, m.venues[sel], signed_in);
    } else {
        if (tm_standings(dl, D, m, ss) && !TW.picker) { TW.tab = 1; turf_fetch_season(); }
        tm_mycrew(dl, D, m, ss, signed_in);
    }

    // the street wire and the buttons
    tm_ticker(dl, D, display, m, t);
    if (!TW.picker) {
        float hx = 48, hy = 908;
        bool details = TW.details && sel >= 0;
        std::string a_word = !acts.empty() ? acts[0].label : "CHALLENGE";
        if (!acts.empty() && acts[0].id == "claim") a_word = "CLAIM";
        if (details && !acts.empty()) a_word = TW.armed == acts[0].id + ":" + std::to_string(TW.sel_id) ? acts[0].confirm : acts[0].label;
        const char *map_a = !acts.empty() && acts[0].id == "claim" ? "CLAIM" : "CHALLENGE";
        if (!details || !acts.empty()) hx += tm_hint(dl, D, hx, hy, "a", details ? a_word.c_str() : map_a) + 18;
        hx += tm_hint(dl, D, hx, hy, "x", details ? "MAP" : "DETAILS") + 18;
        if (details && acts.size() > 1) hx += tm_hint(dl, D, hx, hy, "y", acts[1].label.c_str()) + 18;
        else hx += tm_hint(dl, D, hx, hy, "y", "CREW") + 18;
        {
            float w = tm_plate_key(dl, D, hx, hy + 2, "{LB}");
            w += 6 + tm_plate_key(dl, D, hx + w + 6, hy + 2, "{RB}");
            tm_text(dl, g_title, 21 * s, D.p(hx + w + 8, hy + 1), TM_CREAM, "REGION");
            hx += w + 8 + tm_width(g_title, 21 * s, "REGION") / s + 26;
        }
        hx += tm_hint(dl, D, hx, hy, "b", details ? "CLOSE" : "BACK") + 18;
        const TurfLive *wl = tm_watchable(m);
        if (wl && (gallery_on() || can_watch())) {
            std::string vn;
            for (auto &e : m.venues) if (e.id == wl->venue) vn = upper(e.name.c_str());
            std::string word = "WATCH " + vn + " LIVE";
            float ww = tm_width(g_title, 21 * s, word.c_str()) / s;
            float rx = 1560 - ww;
            tm_text(dl, g_title, 21 * s, D.p(rx, hy + 1), IM_COL32(255, 138, 154, 255), word.c_str());
            dl->AddCircleFilled(D.p(rx - 22, hy + 14), 14 * s, IM_COL32(85, 85, 85, 255), 20);
            tm_text(dl, g_title, 14 * s, D.p(rx - 22, hy + 5), IM_COL32_WHITE, ps_icons() ? "R3" : "R", CENTER);
            bool click = mouse_click(D, rx - 36, hy, ww + 36, 28);
            if ((pad.r3 || click) && !gallery_on() && can_watch()) {
                netplay_lobby_spectate(wl->room.c_str());
                say("Watching " + vn + " (" + wl->room + ")...");
                fprintf(stderr, "[ONLINE] turf: watch venue %d room %s on %s\n", wl->venue, wl->room.c_str(), wl->relay.c_str());
            }
        }
    }
    // the rest of the buttons
    if (!TW.picker) {
        if (pad.y && !(TW.details && acts.size() > 1)) { TW.tab = 1; TW.armed.clear(); turf_fetch_season(); }
        if (pad.b) {
            if (TW.details) { TW.details = false; TW.armed.clear(); }
            else { S.page = TW.back; TW.armed.clear(); }
        }
    }
    if (TW.picker && sel >= 0) {
        dl->AddRectFilled(ImVec2(0, 0), display, IM_COL32(0, 0, 0, 110));
        draw_turf_picker(dl, f, pad_in, m, ss, m.venues[sel]);
    }
}

// The SEASON tab's backdrop: the whole city as the map shows it (the boroughs in their crews' paint, the
// tags, a dot per venue in its holder's colour), dimmed under the standings.
void turf_season_backdrop(ImDrawList *dl, ImVec2 display, const TurfMap &m)
{
    TmView v;
    v.D = tm_frame(display);
    TmBoard b = tm_board(m);
    const float t = (float)ImGui::GetTime(), s = v.D.s();
    tm_city(dl, v, display, m, b, t);
    tm_names(dl, v, m, b);
    for (int i = 0; i < (int)m.venues.size(); i++) {
        const TurfVenue &tv = m.venues[i];
        ImVec2 c = v.w(b.pins[i].x, b.pins[i].y);
        ImU32 col = tv.owner ? tm_crew(m, tv.owner) : TM_OPEN_LIT;
        if (tv.owner) tm_glow(dl, c, 26 * s, 26 * s, with_alpha(col, 0.4f), with_alpha(col, 0));
        dl->AddCircleFilled(c, 9 * s, col, 20);
        dl->AddCircleFilled(c, 5 * s, IM_COL32(8, 7, 10, 255), 16);
    }
    tm_grain(dl, ImVec2(0, 0), display, 26);
    dl->AddRectFilled(ImVec2(0, 0), display, IM_COL32(6, 4, 8, 150));
    ImVec2 c = v.D.p(800, 480);
    float rx = 1200 * s, ry = 720 * s;
    tm_ring(dl, c, rx * 0.5f, ry * 0.5f, rx, ry, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 210));
    tm_ring(dl, c, rx, ry, rx * 3, ry * 3, IM_COL32(0, 0, 0, 210), IM_COL32(0, 0, 0, 210));
}
