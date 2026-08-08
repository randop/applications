// pinentry_raylib.cpp
//
// A `pinentry` implementation: speaks the Assuan protocol on stdin/stdout
// (same commands PinentryClient sends: SETDESC, SETPROMPT, GETPIN, ...)
// and draws the actual prompt with raylib + raygui instead of shelling
// out to pinentry-gtk/-qt/-curses.
//
// Point gpg-agent (or methuselah) at this binary directly:
//   pinentry-program /path/to/pinentry-raylib
// or drive it through PinentryClient::create("pinentry-raylib").
//
// Masking note: raygui's GuiTextBox/GuiTextInputBox show the real
// characters while the field has edit focus and only mask when unfocused,
// which is wrong for a passphrase prompt. This file does its own key
// capture (GetCharPressed/KEY_BACKSPACE) into a private buffer and only
// ever draws '*' for it, so the plaintext never touches the screen.

#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// -------------------------------------------------------------- protocol --

// GPG_ERR_SOURCE_PINENTRY(5) << 24 | GPG_ERR_CANCELED(99) — the standard
// code real pinentry returns when the user cancels.
constexpr int kErrCanceled = (5 << 24) | 99;

std::string percentEncode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '%':  out += "%25"; break;
            case '\r': out += "%0D"; break;
            case '\n': out += "%0A"; break;
            default:   out += static_cast<char>(c);
        }
    }
    return out;
}

std::string percentDecode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            std::string hex(s.substr(i + 1, 2));
            out += static_cast<char>(std::strtoul(hex.c_str(), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

void reply(std::string_view line) {
    std::cout << line << "\n" << std::flush;
}

struct PinentryState {
    std::string title = "pinentry";
    std::string desc;
    std::string prompt;
    std::string ok_label = "OK";
    std::string cancel_label = "Cancel";
    std::string error_msg;
};

// ------------------------------------------------------------------- gui --

std::vector<std::string> wrapText(const std::string& text, int fontSize, int maxWidth) {
    std::vector<std::string> lines;
    std::istringstream paragraphs(text);
    std::string paragraph;
    // Respect explicit newlines (SETDESC arrives with \n as literal
    // paragraph breaks after percent-decoding), then word-wrap each one.
    while (std::getline(paragraphs, paragraph, '\n')) {
        std::istringstream words(paragraph);
        std::string word, current;
        while (words >> word) {
            std::string candidate = current.empty() ? word : current + " " + word;
            if (MeasureText(candidate.c_str(), fontSize) > maxWidth && !current.empty()) {
                lines.push_back(current);
                current = word;
            } else {
                current = candidate;
            }
        }
        lines.push_back(current); // may be empty -> blank line, preserves paragraph breaks
    }
    return lines;
}

enum class DialogResult { Ok, Cancel };

struct DialogOutcome {
    DialogResult result = DialogResult::Cancel;
    std::string pin;
};

// wantPin: GETPIN draws a masked input field; CONFIRM just shows desc + buttons.
DialogOutcome runDialog(const PinentryState& st, bool wantPin) {
    constexpr int kWidth = 440;
    const int kHeight = wantPin ? 260 : 190;

    InitWindow(kWidth, kHeight, st.title.c_str());
    SetTargetFPS(60);

    DialogOutcome outcome;
    std::string pin;
    bool done = false;

    while (!done && !WindowShouldClose()) {
        if (wantPin) {
            int ch = GetCharPressed();
            while (ch > 0) {
                if (ch >= 32 && ch < 127 && pin.size() < 255) pin.push_back(static_cast<char>(ch));
                ch = GetCharPressed();
            }
            if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !pin.empty())
                pin.pop_back();
        }
        if (IsKeyPressed(KEY_ENTER)) { outcome.result = DialogResult::Ok; done = true; }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        int y = 16;
        if (!st.error_msg.empty()) {
            for (const auto& line : wrapText(st.error_msg, 18, kWidth - 32)) {
                DrawText(line.c_str(), 16, y, 18, MAROON);
                y += 22;
            }
            y += 8;
        }
        for (const auto& line : wrapText(st.desc, 18, kWidth - 32)) {
            DrawText(line.c_str(), 16, y, 18, DARKGRAY);
            y += 22;
        }
        y += 12;

        if (wantPin) {
            if (!st.prompt.empty()) {
                DrawText(st.prompt.c_str(), 16, y, 16, GRAY);
                y += 20;
            }
            Rectangle box{16, static_cast<float>(y), static_cast<float>(kWidth - 32), 32};
            DrawRectangleRec(box, LIGHTGRAY);
            DrawRectangleLinesEx(box, 1, GRAY);

            std::string masked(pin.size(), '*');
            DrawText(masked.c_str(), static_cast<int>(box.x) + 8, static_cast<int>(box.y) + 8, 18, BLACK);
            if (static_cast<int>(GetTime() * 2) % 2 == 0) { // blinking caret
                int cx = static_cast<int>(box.x) + 8 + MeasureText(masked.c_str(), 18) + 1;
                DrawLine(cx, static_cast<int>(box.y) + 6, cx, static_cast<int>(box.y) + 26, BLACK);
            }
            y += 44;
        }

        Rectangle cancelBtn{kWidth - 16.0f - 100, kHeight - 44.0f, 100, 30};
        Rectangle okBtn{cancelBtn.x - 10 - 100, kHeight - 44.0f, 100, 30};

        if (GuiButton(okBtn, st.ok_label.c_str())) { outcome.result = DialogResult::Ok; done = true; }
        if (GuiButton(cancelBtn, st.cancel_label.c_str())) { outcome.result = DialogResult::Cancel; done = true; }

        EndDrawing();
    }

    CloseWindow();
    outcome.pin = pin;
    return outcome;
}

} // namespace

// -------------------------------------------------------------------- main --

int main() {
    reply("OK Pleased to meet you");

    PinentryState state;
    std::string line;

    while (std::getline(std::cin, line)) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (line == "BYE") {
            reply("OK closing connection");
            break;
        } else if (line.rfind("SETDESC ", 0) == 0) {
            state.desc = percentDecode(line.substr(8));
            reply("OK");
        } else if (line.rfind("SETPROMPT ", 0) == 0) {
            state.prompt = percentDecode(line.substr(10));
            reply("OK");
        } else if (line.rfind("SETTITLE ", 0) == 0) {
            state.title = percentDecode(line.substr(9));
            reply("OK");
        } else if (line.rfind("SETOK ", 0) == 0) {
            state.ok_label = percentDecode(line.substr(6));
            reply("OK");
        } else if (line.rfind("SETCANCEL ", 0) == 0) {
            state.cancel_label = percentDecode(line.substr(10));
            reply("OK");
        } else if (line.rfind("SETERROR ", 0) == 0) {
            state.error_msg = percentDecode(line.substr(9));
            reply("OK");
        } else if (line == "RESET") {
            state = PinentryState{};
            reply("OK");
        } else if (line.rfind("OPTION", 0) == 0 || line.rfind("GETINFO", 0) == 0) {
            reply("OK"); // accepted/ignored — enough for a barebones client
        } else if (line == "GETPIN") {
            auto outcome = runDialog(state, /*wantPin=*/true);
            if (outcome.result == DialogResult::Ok) {
                reply("D " + percentEncode(outcome.pin));
                reply("OK");
            } else {
                reply("ERR " + std::to_string(kErrCanceled) + " Operation cancelled <pinentry-raylib>");
            }
            explicit_bzero(outcome.pin.data(), outcome.pin.size());
            state.error_msg.clear();
        } else if (line == "CONFIRM") {
            auto outcome = runDialog(state, /*wantPin=*/false);
            reply(outcome.result == DialogResult::Ok
                      ? "OK"
                      : "ERR " + std::to_string(kErrCanceled) + " Operation cancelled <pinentry-raylib>");
            state.error_msg.clear();
        } else {
            reply("ERR 100 Unknown command");
        }
    }

    return 0;
}
