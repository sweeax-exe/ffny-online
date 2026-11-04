// The launcher's Change name dialog (included by pc_ui.cpp, after the glass helpers). The FFNY name is taken from
// Discord at the first sign-in and then stays; a player changes it here, once every 30 days. The account service
// checks everything (characters, duplicates, the chat filter, reserved names, the cooldown: server
// relay/accounts/names.go); the dialog shows its answer as it is.
//
// Tests and pictures: RECOMP_LAUNCHER_PAGE=name opens it at start (signed in, e.g. with a local test account
// service and RECOMP_ACCOUNT_TEST_TOKEN); RECOMP_TEST_NAME=<name> types that name and saves it once.

#include <mutex>
#include <thread>
#include <ctime>

namespace {

struct NameDialog {
    std::mutex mu;
    bool open_request = false;
    bool loading = false, saving = false, have = false;
    std::string name, formerly, message;
    long long next = 0;         // unix time of the next possible change (0: now)
    bool error = false, saved = false;
    char input[40] = {};
};
NameDialog g_nd;

// A string or number member of a flat JSON answer (the first with that key). Names are ASCII; \uXXXX below 0x80
// and the usual escapes are read, anything else is skipped.
bool name_json_str(const std::string &json, const char *key, std::string &out)
{
    std::string k = std::string("\"") + key + "\":";
    size_t p = json.find(k);
    if (p == std::string::npos) return false;
    p += k.size();
    while (p < json.size() && json[p] == ' ') p++;
    if (p >= json.size() || json[p] != '"') return false;
    out.clear();
    for (p++; p < json.size() && json[p] != '"'; p++) {
        char c = json[p];
        if (c == '\\' && p + 1 < json.size()) {
            char e = json[++p];
            if (e == 'u' && p + 4 < json.size()) {
                unsigned v = (unsigned)strtoul(json.substr(p + 1, 4).c_str(), nullptr, 16);
                p += 4;
                if (v >= 0x20 && v < 0x80) out += (char)v;
                continue;
            }
            c = e == 'n' ? ' ' : e;
        }
        out += c;
    }
    return true;
}

long long name_json_num(const std::string &json, const char *key)
{
    std::string k = std::string("\"") + key + "\":";
    size_t p = json.find(k);
    return p == std::string::npos ? 0 : strtoll(json.c_str() + p + k.size(), nullptr, 10);
}

std::string name_date(long long unix_s)
{
    time_t t = (time_t)unix_s;
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[64];
    strftime(buf, sizeof(buf), "%d %B %Y, %H:%M", &tmv);
    return buf;
}

void name_take_account(const std::string &json)     // under g_nd.mu
{
    name_json_str(json, "name", g_nd.name);
    if (!name_json_str(json, "formerly", g_nd.formerly)) g_nd.formerly.clear();
    g_nd.next = name_json_num(json, "name_next");
    g_nd.have = true;
}

void name_load()
{
    {
        std::lock_guard<std::mutex> l(g_nd.mu);
        if (g_nd.loading) return;
        g_nd.loading = true;
    }
    std::thread([] {
        std::vector<char> buf(8192);
        int status = account_call("GET", "/api/me", nullptr, buf.data(), (int)buf.size(), 0, nullptr);
        std::lock_guard<std::mutex> l(g_nd.mu);
        g_nd.loading = false;
        if (status == 200) name_take_account(buf.data());
        else { g_nd.message = "The account service did not answer; try again later."; g_nd.error = true; }
    }).detach();
}

void name_save(const std::string &want)
{
    {
        std::lock_guard<std::mutex> l(g_nd.mu);
        if (g_nd.saving) return;
        g_nd.saving = true;
        g_nd.message = "Saving...";
        g_nd.error = false;
    }
    std::thread([want] {
        std::string body = "{\"name\":\"";
        for (char c : want) {
            if (c == '"' || c == '\\') body += '\\';
            if ((unsigned char)c >= 0x20) body += c;
        }
        body += "\"}";
        std::vector<char> buf(8192);
        int status = account_call("POST", "/api/me/name", body.c_str(), buf.data(), (int)buf.size(), 0, nullptr);
        std::string json = buf.data();
        fprintf(stderr, "[ACCOUNT] change name: HTTP %d\n", status);
        if (status == 200) account_take_renamed(json.c_str());
        std::lock_guard<std::mutex> l(g_nd.mu);
        g_nd.saving = false;
        if (status == 200) {
            name_take_account(json);
            g_nd.message = "Saved. Rooms, chat and the boards show your new name.";
            g_nd.error = false;
            g_nd.saved = true;
        } else {
            std::string why;
            if (!name_json_str(json, "error", why) || why.empty())
                why = status == 0 ? "The account service did not answer; try again later." : "That did not work; try again later.";
            if (long long next = name_json_num(json, "next")) g_nd.next = next;
            g_nd.message = why;
            g_nd.error = true;
        }
    }).detach();
}

}  // namespace

static void name_dialog_open()
{
    std::lock_guard<std::mutex> l(g_nd.mu);
    g_nd.open_request = true;
    g_nd.message.clear();
    g_nd.error = g_nd.saved = false;
    g_nd.input[0] = 0;
}

static void name_dialog(ImVec2 o, float w, float h, float s)
{
    {
        static bool asked;
        const char *want = getenv("RECOMP_LAUNCHER_PAGE");
        if (!asked && want && !strcmp(want, "name") && account_state() == ACCOUNT_IN) { asked = true; name_dialog_open(); }
        bool req;
        { std::lock_guard<std::mutex> l(g_nd.mu); req = g_nd.open_request; g_nd.open_request = false; }
        if (req && !ImGui::IsPopupOpen("Change name")) { ImGui::OpenPopup("Change name"); name_load(); }
    }
    ImVec2 size(std::min(w - 40 * s, 560 * s), std::min(h - 40 * s, 330 * s));
    ImGui::SetNextWindowPos(ImVec2(o.x + (w - size.x) / 2, o.y + (h - size.y) / 2));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::BeginPopupModal("Change name", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!open) return;

    std::string name, formerly, message;
    long long next;
    bool have, busy, error, saved;
    {
        std::lock_guard<std::mutex> l(g_nd.mu);
        name = g_nd.name; formerly = g_nd.formerly; message = g_nd.message; next = g_nd.next;
        have = g_nd.have; busy = g_nd.loading || g_nd.saving; error = g_nd.error; saved = g_nd.saved;
    }
    bool waiting = next > (long long)time(nullptr);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetWindowPos(), b(a.x + size.x, a.y + size.y);
    float lx = a.x + 30 * s, inner = size.x - 60 * s;
    glass(dl, a, b, 16 * s, k_glass, 0.80f);
    dl->AddText(display_font(), 30 * s, ImVec2(lx, a.y + 18 * s), col(k_white), "CHANGE NAME");
    dl->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(lx, a.y + 58 * s), col(k_white, 0.6f),
                "Your FFNY name is what players see in rooms, chat and on the boards.\nIt no longer follows your Discord name.",
                nullptr, inner);

    float y = a.y + 104 * s;
    std::string now_line = have ? "Now: " + name + (formerly.empty() ? "" : "   (formerly " + formerly + ")") : "Now: ...";
    card_line(dl, ImVec2(lx, y), s, k_white, now_line.c_str(), true);
    y += 30 * s;

    push_glass_style();
    ImGui::SetCursorScreenPos(ImVec2(lx, y));
    ImGui::SetNextItemWidth(inner);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool enter;
    {
        std::lock_guard<std::mutex> l(g_nd.mu);
        enter = ImGui::InputTextWithHint("##ffny_name", "New name", g_nd.input, 17,
                                         ImGuiInputTextFlags_EnterReturnsTrue);
    }
    pop_glass_style();
    y += 36 * s;
    dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(lx, y), col(k_white, 0.55f),
                "3 to 16 characters: letters, digits, single spaces and simple punctuation.", nullptr, inner);
    y += 22 * s;
    std::string when = !have ? "" : waiting ? "Next change possible on " + name_date(next) + "."
                                            : "You can change it now; after that, once every 30 days.";
    dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(lx, y), col(waiting ? k_gold : ImVec4(0.80f, 0.80f, 0.82f, 1)), when.c_str(),
                nullptr, inner);

    float foot = b.y - 34 * s;
    if (!message.empty())
        dl->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(lx, foot - 52 * s), col(error ? k_error : saved ? k_green : k_gold),
                    message.c_str(), nullptr, inner - 260 * s);
    std::string want;
    { std::lock_guard<std::mutex> l(g_nd.mu); want = g_nd.input; }
    {   // tests: RECOMP_TEST_NAME types a name and saves it once the account is read
        static bool typed;
        const char *t = getenv("RECOMP_TEST_NAME");
        if (!typed && t && *t && have && !busy) {
            typed = true;
            std::lock_guard<std::mutex> l(g_nd.mu);
            snprintf(g_nd.input, sizeof(g_nd.input), "%.16s", t);
            want = g_nd.input;
            enter = true;
        }
    }
    bool can = have && !busy && !waiting && !want.empty() && want != name;
    if ((glass_button("##name_save", "SAVE", ImVec2(b.x - 30 * s - 130 * s, foot - 19 * s), ImVec2(130 * s, 38 * s), s, GLASS_RED,
                      display_font(), 18 * s, can) || (enter && can)))
        name_save(want);
    if (glass_button("##name_close", saved ? "Done" : "Cancel", ImVec2(b.x - 30 * s - 250 * s, foot - 19 * s), ImVec2(110 * s, 38 * s),
                     s, GLASS_PLAIN, bold_font(), 15 * s) ||
        (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
