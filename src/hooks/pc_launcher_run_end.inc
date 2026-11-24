// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// ---- "Help us find crashes?" (build 110, run_end.h) ----------------------------------------------------
// Nothing about a run's end is kept or sent before the player says yes ([online] run_end_reports, ask by default). The
// launcher asks once, signed in, after the update offer and the crash report: Yes turns it on, No off. "Not now" (its
// button, Escape, B, or closing the launcher with the question open) sends nothing and asks at a later start once more;
// the second not-now counts as No ([online] run_end_asked counts the times it was open). The setting is on the launcher's
// Online page and in the basics (the overlay too), with the same lines.
// Tests: RECOMP_TEST_RUN_END="<frame>:<step>;..." steps: yes|no|later (that button), esc|b (that key, pressed), a|enter
// (pressed on what has the focus), left|right (the pad's), click-yes|click-no|click-later (the mouse), set-on|set-off|
// set-ask (the setting), quit (the launcher closes), play; "show" anywhere keeps the
// setting in view. Included by pc_ui.cpp after the glass dialogs.

static const char *const k_run_end_sent =
    "Sent when the launcher starts: how the last run of the game ended (closed, crashed and where in the program, or "
    "ended from outside, and whether it stood still), the build, the compiler version, the time and your account.";
static const char *const k_run_end_kept = "No logs, no pictures, no files. Kept for 30 days, and deleted with your account.";

// RECOMP_TEST_RUN_END: the step due at this frame, "" none. A key step goes down now and up at the next frame.
static std::string run_end_test_step()
{
    static std::vector<std::pair<int, std::string>> steps;
    static bool read;
    static ImGuiKey down = ImGuiKey_None;
    ImGuiIO &io = ImGui::GetIO();
    if (!read) {
        read = true;
        const char *t = getenv("RECOMP_TEST_RUN_END");
        for (std::string s = t ? t : ""; !s.empty();) {
            size_t end = s.find(';');
            std::string one = s.substr(0, end);
            size_t colon = one.find(':');
            if (colon != std::string::npos) steps.push_back({ atoi(one.c_str()), one.substr(colon + 1) });
            s = end == std::string::npos ? "" : s.substr(end + 1);
        }
    }
    if (down != ImGuiKey_None) {
        io.AddKeyEvent(down, false);
        down = ImGuiKey_None;
    }
    if (steps.empty() || ImGui::GetFrameCount() < steps[0].first) return "";
    std::string step = steps[0].second;
    steps.erase(steps.begin());
    fprintf(stderr, "[RUN-END] test: %s\n", step.c_str());
    static const struct { const char *name; ImGuiKey key; } keys[] = {
        { "esc", ImGuiKey_Escape }, { "b", ImGuiKey_GamepadFaceRight }, { "a", ImGuiKey_GamepadFaceDown },
        { "enter", ImGuiKey_Enter }, { "left", ImGuiKey_GamepadDpadLeft }, { "right", ImGuiKey_GamepadDpadRight } };
    for (auto &k : keys)
        if (step == k.name) {
            down = k.key;
            io.AddKeyEvent(down, true);
            return "";
        }
    return step;
}

// The setting, with the lines of the question.
static void run_end_setting()
{
    static int before = -1;
    settings_group("online", { "run_end_reports" });
    if (const char *t = getenv("RECOMP_TEST_RUN_END"); t && strstr(t, "show")) ImGui::SetScrollHereY(0.5f);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", k_run_end_sent);
    ImGui::TextDisabled("%s", k_run_end_kept);
    ImGui::PopTextWrapPos();
    int now = recomp_settings_get("online", "run_end_reports", RUN_END_ASK);
    if (before >= 0 && now != before) {
        if (now == RUN_END_ASK) recomp_settings_set("online", "run_end_asked", 0);      // chosen: it may ask again
        commit();
        run_end_setting_changed();
    }
    before = now;
}

static void run_end_answer(int mode)
{
    recomp_settings_set("online", "run_end_reports", mode);
    commit();
    run_end_setting_changed();
    fprintf(stderr, "[RUN-END] the question: %s\n", mode == RUN_END_ON ? "yes" : "no");
    fflush(stderr);
}

static void run_end_question(UiResult &result)
{
    static bool decided, clicking;
    static ULONGLONG opened_at;
    std::string step = getenv("RECOMP_TEST_RUN_END") ? run_end_test_step() : "";
    if (step.rfind("set-", 0) == 0) {
        recomp_settings_set("online", "run_end_reports", step == "set-on" ? RUN_END_ON : step == "set-off" ? RUN_END_OFF : RUN_END_ASK);
        commit();
        run_end_setting_changed();
    }
    if (step == "quit") result.leave = true;
    if (step == "play") result.play = true;
    if (!decided && account_state() == ACCOUNT_IN && notice_may_open()) {
        decided = true;
        int asked = recomp_settings_get("online", "run_end_asked", 0);
        int due = run_end_ask_due(recomp_settings_get("online", "run_end_reports", RUN_END_ASK), asked);
        if (due == 2) run_end_answer(RUN_END_OFF);      // the second not-now was closing the launcher
        else if (due == 1) {
            recomp_settings_set("online", "run_end_asked", asked + 1);
            commit();
            fprintf(stderr, "[RUN-END] the question opens (time %d)\n", asked + 1);
            fflush(stderr);
            opened_at = GetTickCount64();
            ImGui::OpenPopup("Help us find crashes?");
        }
    }
    if (!begin_glass_modal("Help us find crashes?", ImGui::GetFontSize() * 32)) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
    ImGui::TextUnformatted("When the game crashes, we often never hear about it. May the launcher tell us?");
    ImGui::Spacing();
    ImGui::TextUnformatted(k_run_end_sent);
    ImGui::TextUnformatted(k_run_end_kept);
    ImGui::Spacing();
    ImGui::TextDisabled("You can change this any time in the settings under Online. Esc or B: not now.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    // no answer is made for the player: the three look alike and the focus starts on Not now
    static const struct { const char *label, *step; } k_buttons[3] = { { "Yes", "yes" }, { "No", "no" }, { "Not now", "later" } };
    int choice = -1;
    bool fresh = GetTickCount64() - opened_at < 400;        // a press from before the question opened answers nothing
    for (int i = 0; i < 3; i++) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(k_buttons[i].label, ImVec2(ImGui::GetFontSize() * 8, 0)) && !fresh) choice = i;
        if (i == 2) ImGui::SetItemDefaultFocus();
        if (step == k_buttons[i].step) choice = i;
        if (step == std::string("click-") + k_buttons[i].step) {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetIO().AddMousePosEvent((a.x + b.x) / 2, (a.y + b.y) / 2);
            ImGui::GetIO().AddMouseButtonEvent(0, true);
            clicking = true;
        } else if (clicking && step.empty() && i == 2) {
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            clicking = false;
        }
    }
    if (!fresh && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))) choice = 2;
    if (choice >= 0) {
        if (choice == 2) {
            fprintf(stderr, "[RUN-END] the question: not now\n");
            fflush(stderr);
            if (recomp_settings_get("online", "run_end_asked", 0) >= RUN_END_ASK_TIMES) run_end_answer(RUN_END_OFF);
        } else {
            run_end_answer(choice == 0 ? RUN_END_ON : RUN_END_OFF);
        }
        ImGui::CloseCurrentPopup();
    }
    end_glass_modal();
}
