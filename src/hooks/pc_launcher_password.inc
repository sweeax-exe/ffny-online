// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// Sign in without Discord (included by pc_ui.cpp, after the glass helpers): an FFNY Online account with a username
// and a password (server relay/accounts/password.go). One dialog for: SIGN IN, NEW ACCOUNT, FORGOT PASSWORD (no
// email: an admin's reset code), and, signed in, ADD A PASSWORD (a Discord account) or CHANGE PASSWORD, plus
// LINK DISCORD for an account without it. The launcher's Account card opens it on glass; the overlay's Online page
// (F1) opens the same dialog in the overlay's own style.
//
// A pad types on the dialog's own keyboard: the d-pad picks a field or a key, A types it, X deletes, Y is shift,
// START sends, B closes. A real keyboard types into the field as usual.
//
// Tests and pictures: RECOMP_LAUNCHER_PAGE=password (or password-new, password-forgot, password-change) opens it
// at start; RECOMP_TEST_PW_FILL="user|password" fills the fields (the repeat too), RECOMP_TEST_PW_SUBMIT=1 then
// presses the dialog's button once.

#include <mutex>
#include <thread>

namespace {

enum PwMode { PW_SIGNIN, PW_CREATE, PW_FORGOT, PW_ADD, PW_CHANGE };
enum PwField { PF_USER, PF_OLD, PF_CODE, PF_PASS, PF_REPEAT, PF_COUNT };

struct PwDialog {
    std::mutex mu;
    bool open_request = false;
    int mode = PW_SIGNIN;
    bool busy = false, error = false, done = false;
    std::string message;
    char f[PF_COUNT][136] = {};
    int field = PF_USER;        // the field the dialog's keyboard types into
    bool show = false, shift = false, sym = false;
};
PwDialog g_pw;

const int k_pw_max[PF_COUNT] = { ACCOUNT_PW_LOGIN_MAX, ACCOUNT_PW_MAX, 16, ACCOUNT_PW_MAX, ACCOUNT_PW_MAX };

void pw_wipe()      // under g_pw.mu
{
    for (int i = 0; i < PF_COUNT; i++) memset(g_pw.f[i], 0, sizeof(g_pw.f[i]));
}

std::vector<int> pw_fields(int mode)
{
    switch (mode) {
    case PW_SIGNIN: return { PF_USER, PF_PASS };
    case PW_CREATE: case PW_ADD: return { PF_USER, PF_PASS, PF_REPEAT };
    case PW_FORGOT: return { PF_USER, PF_CODE, PF_PASS, PF_REPEAT };
    default: return { PF_OLD, PF_PASS, PF_REPEAT };
    }
}

const char *pw_label(int mode, int field)
{
    switch (field) {
    case PF_USER: return "Username";
    case PF_OLD: return "Current password";
    case PF_CODE: return "Reset code from an admin";
    case PF_PASS: return mode == PW_SIGNIN ? "Password" : mode == PW_CREATE || mode == PW_ADD ? "Password" : "New password";
    default: return "Password again";
    }
}

void pw_set_mode(int mode)      // under g_pw.mu
{
    if (g_pw.mode != mode) {
        char user[136];
        memcpy(user, g_pw.f[PF_USER], sizeof(user));
        pw_wipe();
        if (mode != PW_ADD) memcpy(g_pw.f[PF_USER], user, sizeof(user));     // the username stays
    }
    g_pw.mode = mode;
    g_pw.field = pw_fields(mode)[0];
    g_pw.message.clear();
    g_pw.error = g_pw.done = false;
}

void pw_submit()
{
    int mode;
    std::string user, pass, repeat, code, old;
    {
        std::lock_guard<std::mutex> l(g_pw.mu);
        if (g_pw.busy) return;
        mode = g_pw.mode;
        user = g_pw.f[PF_USER]; pass = g_pw.f[PF_PASS]; repeat = g_pw.f[PF_REPEAT];
        code = g_pw.f[PF_CODE]; old = g_pw.f[PF_OLD];
        char why[200] = "";
        int bad;
        if (mode == PW_SIGNIN) bad = account_pw_check_new(user.c_str(), pass.c_str(), nullptr, why, sizeof(why));
        else if (mode == PW_CHANGE) {       // the username rule does not apply: the passwords alone
            bad = old.empty() ? (snprintf(why, sizeof(why), "Enter your current password."), 1)
                : pass.size() < ACCOUNT_PW_MIN ? (snprintf(why, sizeof(why), "Use at least 8 characters for your password."), 1)
                : pass != repeat ? (snprintf(why, sizeof(why), "The two passwords are not the same."), 1) : 0;
        } else bad = account_pw_check_new(user.c_str(), pass.c_str(), repeat.c_str(), why, sizeof(why));
        if (!bad && mode == PW_FORGOT && code.empty()) bad = (snprintf(why, sizeof(why), "Enter the reset code an admin gave you."), 1);
        if (bad) { g_pw.message = why; g_pw.error = true; return; }
        g_pw.busy = true;
        g_pw.error = false;
        g_pw.message = mode == PW_CREATE ? "Making your account..." : mode == PW_SIGNIN ? "Signing in..." : "Saving...";
    }
    std::thread([mode, user, pass, code, old]() mutable {
        char why[300] = "";
        int status;
        if (mode == PW_ADD) status = account_set_password(user.c_str(), pass.c_str(), 1, why, sizeof(why));
        else if (mode == PW_CHANGE) status = account_set_password(old.c_str(), pass.c_str(), 0, why, sizeof(why));
        else status = account_password(mode == PW_CREATE ? ACCOUNT_PW_CREATE : mode == PW_FORGOT ? ACCOUNT_PW_RESET
                                       : ACCOUNT_PW_SIGNIN, user.c_str(), pass.c_str(), code.c_str(), why, sizeof(why));
        std::fill(pass.begin(), pass.end(), 0);
        std::fill(old.begin(), old.end(), 0);
        std::lock_guard<std::mutex> l(g_pw.mu);
        g_pw.busy = false;
        if (status == 200 || status == 201) {
            g_pw.done = true;
            g_pw.error = false;
            g_pw.message = mode == PW_CREATE ? "Your account is ready. You are signed in."
                         : mode == PW_ADD ? "Saved. You can now sign in with this username and password too."
                         : mode == PW_CHANGE ? "Your password is changed. Other PCs are signed out."
                         : mode == PW_FORGOT ? "Your new password is set. You are signed in." : "You are signed in.";
            pw_wipe();
        } else {
            g_pw.error = true;
            g_pw.message = why[0] ? why : "That did not work; try again later.";
        }
    }).detach();
}

// The dialog's keyboard: letters and digits, or symbols; SHIFT, DEL, SPACE.
void pw_type(const char *chars)
{
    std::lock_guard<std::mutex> l(g_pw.mu);
    char *buf = g_pw.f[g_pw.field];
    size_t n = strlen(buf), max = (size_t)k_pw_max[g_pw.field];
    for (const char *c = chars; *c && n < max; c++) {
        char ch = *c;
        if (g_pw.shift && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        buf[n++] = ch;
    }
    buf[n] = 0;
    g_pw.shift = false;
}

void pw_delete()
{
    std::lock_guard<std::mutex> l(g_pw.mu);
    char *buf = g_pw.f[g_pw.field];
    size_t n = strlen(buf);
    if (n) buf[n - 1] = 0;
}

void pw_keyboard(float s, float width)
{
    static const char *letters[] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
    static const char *symbols[] = { "!@#$%^&*()", "-_=+[]{};:", "'\",.<>/?\\|", "`~" };
    bool sym, shift;
    { std::lock_guard<std::mutex> l(g_pw.mu); sym = g_pw.sym; shift = g_pw.shift; }
    const char **rows = sym ? symbols : letters;
    float gap = 4 * s, key = std::min(40 * s, (width - 9 * gap) / 10);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    for (int r = 0; r < 4; r++) {
        float row_w = strlen(rows[r]) * (key + gap) - gap;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - row_w) / 2);
        for (const char *c = rows[r]; *c; c++) {
            char label[2] = { *c, 0 };
            if (!sym && shift && *c >= 'a' && *c <= 'z') label[0] = (char)(*c - 32);
            ImGui::PushID(r * 100 + (int)(c - rows[r]));
            if (ImGui::Button(label, ImVec2(key, key))) pw_type(std::string(1, *c).c_str());
            ImGui::PopID();
            if (c[1]) ImGui::SameLine();
        }
    }
    float wide = (width - 5 * gap) / 6;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX());
    if (ImGui::Button(shift ? "SHIFT (on)##pwk_shift" : "SHIFT##pwk_shift", ImVec2(wide, key)))
        { std::lock_guard<std::mutex> l(g_pw.mu); g_pw.shift = !g_pw.shift; }
    ImGui::SameLine();
    if (ImGui::Button(sym ? "abc##pwk_sym" : "#+=##pwk_sym", ImVec2(wide, key)))
        { std::lock_guard<std::mutex> l(g_pw.mu); g_pw.sym = !g_pw.sym; }
    ImGui::SameLine();
    if (ImGui::Button(".##pwk_dot", ImVec2(wide * 0.5f - gap / 2, key))) pw_type(".");
    ImGui::SameLine();
    if (ImGui::Button("_##pwk_us", ImVec2(wide * 0.5f - gap / 2, key))) pw_type("_");
    ImGui::SameLine();
    if (ImGui::Button("SPACE##pwk_space", ImVec2(wide, key))) pw_type(" ");
    ImGui::SameLine();
    if (ImGui::Button("DEL##pwk_del", ImVec2(wide, key))) pw_delete();
    ImGui::PopStyleVar();
}

// The dialog's content (both looks). Returns true when it should close.
bool pw_body(float s, float width, bool glassy)
{
    int mode;
    std::string message, forgot;
    bool busy, error, done, show;
    {
        std::lock_guard<std::mutex> l(g_pw.mu);
        mode = g_pw.mode; message = g_pw.message; busy = g_pw.busy; error = g_pw.error; done = g_pw.done; show = g_pw.show;
    }
    char forgot_text[400];
    int create = 0;
    bool offered = account_password_offered(&create, forgot_text, sizeof(forgot_text)) != 0;
    forgot = forgot_text[0] ? forgot_text
                            : "There is no email reset yet. Ask an admin for a reset code: in the FFNY Online Discord, or ask "
                              "a friend who has Discord to open a ticket for you. Then enter it here.";
    bool signed_in = account_state() == ACCOUNT_IN;
    bool close = false;
    const ImVec4 quiet(0.80f, 0.80f, 0.82f, 1);

    const char *title = mode == PW_SIGNIN ? "SIGN IN WITHOUT DISCORD" : mode == PW_CREATE ? "NEW ACCOUNT"
                      : mode == PW_FORGOT ? "FORGOT PASSWORD" : mode == PW_ADD ? "ADD A PASSWORD" : "CHANGE PASSWORD";
    if (glassy) {
        ImGui::PushFont(display_font(), 30 * s);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
    } else {
        ImGui::SeparatorText(title);
    }
    // the three ways in, as tabs (signed out)
    if (!signed_in && !done) {
        const char *tabs[] = { "Sign in", "New account", "Forgot password" };
        int ids[] = { PW_SIGNIN, PW_CREATE, PW_FORGOT };
        for (int i = 0; i < 3; i++) {
            if (i) ImGui::SameLine();
            bool on = mode == ids[i];
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.06f, 0.16f, glassy ? 0.85f : 1.0f));
            if (ImGui::Button(tabs[i], ImVec2(0, 0)) && !busy) { std::lock_guard<std::mutex> l(g_pw.mu); pw_set_mode(ids[i]); }
            if (on) ImGui::PopStyleColor();
        }
        ImGui::Spacing();
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    if (!offered && !signed_in) {
        ImGui::TextColored(k_gold, "Signing in without Discord isn't open yet. Sign in with Discord for now.");
    } else if (mode == PW_SIGNIN) {
        ImGui::TextColored(quiet, "Your FFNY Online account with a username and a password: for players without "
                                  "Discord. Everything works the same online.");
    } else if (mode == PW_CREATE) {
        ImGui::TextColored(quiet, "No email and no Discord needed. Your username is also your name online. Use at least "
                                  "8 characters for the password and write it down: without email, only an admin can "
                                  "help you back in.");
        if (!create) ImGui::TextColored(k_gold, "New accounts can't be made right now. Try again later.");
    } else if (mode == PW_FORGOT) {
        ImGui::TextColored(quiet, "%s", forgot.c_str());
    } else if (mode == PW_ADD) {
        ImGui::TextColored(quiet, "Add a username and password to this account, so you can also sign in without Discord "
                                  "(for example where Discord is blocked). It stays one account.");
    } else {
        ImGui::TextColored(quiet, "Changing your password signs out every other PC.");
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    // the fields
    if (!done) {
        auto fields = pw_fields(mode);
        float label_w = 190 * s;
        for (int fld : fields) {
            bool target;
            { std::lock_guard<std::mutex> l(g_pw.mu); target = g_pw.field == fld; }
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(target ? k_gold : ImVec4(1, 1, 1, 0.85f), "%s %s", target ? ">" : " ", pw_label(mode, fld));
            ImGui::SameLine(label_w);
            ImGui::SetNextItemWidth(width - label_w);
            ImGuiInputTextFlags flags = 0;
            if ((fld == PF_PASS || fld == PF_REPEAT || fld == PF_OLD) && !show) flags |= ImGuiInputTextFlags_Password;
            if (fld == PF_USER) flags |= ImGuiInputTextFlags_CharsNoBlank;
            if (fld == PF_CODE) flags |= ImGuiInputTextFlags_CharsUppercase;
            char id[24];
            snprintf(id, sizeof(id), "##pwf%d", fld);
            if (busy) ImGui::BeginDisabled();
            bool enter;
            {
                std::lock_guard<std::mutex> l(g_pw.mu);
                enter = ImGui::InputText(id, g_pw.f[fld], (size_t)k_pw_max[fld] + 1, flags | ImGuiInputTextFlags_EnterReturnsTrue);
            }
            if (busy) ImGui::EndDisabled();
            if (ImGui::IsItemActive() || ImGui::IsItemFocused()) { std::lock_guard<std::mutex> l(g_pw.mu); g_pw.field = fld; }
            if (enter) pw_submit();
        }
        ImGui::Checkbox("Show password", &show);
        { std::lock_guard<std::mutex> l(g_pw.mu); g_pw.show = show; }
        if (mode == PW_SIGNIN) {
            ImGui::SameLine();
            ImGui::TextDisabled("  Forgot it? Choose Forgot password above.");
        }
        ImGui::Spacing();
        if (busy) ImGui::BeginDisabled();
        pw_keyboard(s, width);
        if (busy) ImGui::EndDisabled();
    }
    ImGui::Spacing();
    if (!message.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextColored(error ? k_error : done ? k_green : k_gold, "%s", message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    // the buttons
    const char *go = mode == PW_SIGNIN ? "SIGN IN" : mode == PW_CREATE ? "MAKE ACCOUNT" : mode == PW_FORGOT ? "SET PASSWORD" : "SAVE";
    bool can = !busy && !done && (offered || signed_in) && !(mode == PW_CREATE && !create);
    if (!can) ImGui::BeginDisabled();
    if (ImGui::Button(go, ImVec2(160 * s, 36 * s))) pw_submit();
    if (!can) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(done ? "Done" : "Cancel", ImVec2(110 * s, 36 * s))) close = true;
    if (signed_in && !account_has_discord() && !busy) {     // an account without Discord: link one (optional)
        ImGui::SameLine();
        if (ImGui::Button("Link Discord...", ImVec2(140 * s, 36 * s))) { account_link_discord(); close = true; }
        ImGui::SetItemTooltip("Opens your browser to sign in with Discord once; afterwards both ways lead to this account.");
    }
    ImGui::TextDisabled("Pad: d-pad picks, A types, X deletes, Y shift, START sends, B closes.");

    // the pad's shortcuts (the keyboard's own keys go to the fields)
    if (!busy && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, true)) pw_delete();
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false)) { std::lock_guard<std::mutex> l(g_pw.mu); g_pw.shift = !g_pw.shift; }
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) && can) pw_submit();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) && !busy) close = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !busy && !ImGui::GetIO().WantTextInput) close = true;
    if (done && account_state() == ACCOUNT_IN && (mode == PW_SIGNIN || mode == PW_CREATE || mode == PW_FORGOT)) {
        static double shown_at = 0;     // a moment to read the line, then closed
        if (shown_at == 0) shown_at = ImGui::GetTime();
        if (ImGui::GetTime() - shown_at > 1.5) { close = true; shown_at = 0; }
    }
    {   // tests: RECOMP_TEST_PW_FILL="user|password" fills the fields once (the repeat too)
        static bool filled;
        const char *t = getenv("RECOMP_TEST_PW_FILL");
        if (!filled && t && *t) {
            filled = true;
            const char *bar = strchr(t, '|');
            std::lock_guard<std::mutex> l(g_pw.mu);
            snprintf(g_pw.f[PF_USER], sizeof(g_pw.f[0]), "%.*s", bar ? (int)(bar - t) : (int)strlen(t), t);
            if (bar) {
                snprintf(g_pw.f[PF_PASS], sizeof(g_pw.f[0]), "%s", bar + 1);
                snprintf(g_pw.f[PF_REPEAT], sizeof(g_pw.f[0]), "%s", bar + 1);
            }
        }
    }
    {   // tests: RECOMP_TEST_PW_SUBMIT=1 then presses the dialog's own button once (after the fill)
        static int frames;
        const char *t = getenv("RECOMP_TEST_PW_SUBMIT");
        if (t && *t == '1' && frames >= 0 && ++frames > 20 && account_password_offered(nullptr, nullptr, 0)) {
            frames = -1;
            pw_submit();
        }
    }
    if (close) {
        std::lock_guard<std::mutex> l(g_pw.mu);
        pw_wipe();
        g_pw.message.clear();
        g_pw.done = g_pw.error = false;
    }
    return close;
}

}  // namespace

// Opens the dialog: mode PW_SIGNIN (signed out) or, signed in, PW_ADD / PW_CHANGE as the account has.
static void password_dialog_open(int mode)
{
    std::lock_guard<std::mutex> l(g_pw.mu);
    if (account_state() == ACCOUNT_IN && mode != PW_ADD && mode != PW_CHANGE)
        mode = account_has_password() ? PW_CHANGE : PW_ADD;
    g_pw.open_request = true;
    g_pw.mode = -1;
    pw_set_mode(mode);
    account_password_offer_refresh();
}

static void password_open_from_env()
{
    static bool asked;
    const char *want = getenv("RECOMP_LAUNCHER_PAGE");
    if (asked || !want || strncmp(want, "password", 8)) return;
    asked = true;
    int mode = !strcmp(want, "password-new") ? PW_CREATE : !strcmp(want, "password-forgot") ? PW_FORGOT
             : !strcmp(want, "password-change") ? PW_CHANGE : PW_SIGNIN;
    password_dialog_open(mode);
}

// The launcher's look: glass, centred on the launcher.
static void password_dialog(ImVec2 o, float w, float h, float s)
{
    password_open_from_env();
    {
        bool req;
        { std::lock_guard<std::mutex> l(g_pw.mu); req = g_pw.open_request; g_pw.open_request = false; }
        if (req && !ImGui::IsPopupOpen("Sign in without Discord")) ImGui::OpenPopup("Sign in without Discord");
    }
    ImVec2 size(std::min(w - 40 * s, 640 * s), std::min(h - 30 * s, 660 * s));
    ImGui::SetNextWindowPos(ImVec2(o.x + (w - size.x) / 2, o.y + (h - size.y) / 2));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30 * s, 20 * s));
    bool open = ImGui::BeginPopupModal("Sign in without Discord", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!open) return;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetWindowPos(), b(a.x + size.x, a.y + size.y);
    glass(dl, a, b, 16 * s, k_glass, 0.86f);
    push_glass_style();
    bool close = pw_body(s, size.x - 60 * s, true);
    pop_glass_style();
    if (close) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// The overlay's look (F1 > Online): the same content in a plain modal.
static void password_dialog_plain()
{
    password_open_from_env();
    {
        bool req;
        { std::lock_guard<std::mutex> l(g_pw.mu); req = g_pw.open_request; g_pw.open_request = false; }
        if (req && !ImGui::IsPopupOpen("Sign in without Discord##plain")) ImGui::OpenPopup("Sign in without Discord##plain");
    }
    float s = ImGui::GetFontSize() / 15.0f;
    ImGui::SetNextWindowSize(ImVec2(600 * s, 0));
    if (!ImGui::BeginPopupModal("Sign in without Discord##plain", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar))
        return;
    if (pw_body(s, 600 * s - ImGui::GetStyle().WindowPadding.x * 2, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
