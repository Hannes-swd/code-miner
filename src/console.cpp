#include "console.h"

#include "format.h"
#include "theme.h"
#include "world.h"

#include <cstdio>

namespace
{

// Der Startknopf: ein orangener Kreis mit Dreieck, im Lauf mit zwei Balken.
// Beides wird selbst gezeichnet - dadurch braucht das Programm keine
// Symbol-Schriftart. Er ist das einzige kraeftig Farbige an der Konsole und
// damit sofort zu finden.
bool PlayPauseButton(bool showPause)
{
    const float  h = 30.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    const bool pressed = ImGui::Button("##playpause", ImVec2(h, h));
    ImGui::PopStyleColor(3);

    ImDrawList* dl  = ImGui::GetWindowDrawList();
    const float cx  = p.x + h * 0.5f;
    const float cy  = p.y + h * 0.5f;
    const float s   = h * 0.22f;

    dl->AddRectFilled(p, ImVec2(p.x + h, p.y + h),
                      ImGui::IsItemHovered() ? ui::kAccentHot : ui::kAccent, 8.0f);

    const ImU32 col = IM_COL32(255, 255, 255, 255);
    if (showPause)
    {
        const float w = s * 0.42f;
        dl->AddRectFilled(ImVec2(cx - s * 0.75f, cy - s), ImVec2(cx - s * 0.75f + w, cy + s), col,
                          1.0f);
        dl->AddRectFilled(ImVec2(cx + s * 0.75f - w, cy - s), ImVec2(cx + s * 0.75f, cy + s), col,
                          1.0f);
    }
    else
    {
        dl->AddTriangleFilled(ImVec2(cx - s * 0.5f, cy - s), ImVec2(cx - s * 0.5f, cy + s),
                              ImVec2(cx + s * 0.95f, cy), col);
    }

    return pressed;
}

// Ein kleines graues Schildchen, z.B. "Schleife 1".
void Chip(const char* text)
{
    const ImVec2 p  = ImGui::GetCursorScreenPos();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float  px = 8.0f;
    const float  py = 3.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + ts.x + px * 2.0f, p.y + ts.y + py * 2.0f), ui::kSunken,
                      ui::kRoundS);
    dl->AddRect(p, ImVec2(p.x + ts.x + px * 2.0f, p.y + ts.y + py * 2.0f), ui::kBorder,
                ui::kRoundS, 0, 1.0f);
    dl->AddText(ImVec2(p.x + px, p.y + py), ui::kTextDim, text);

    ImGui::Dummy(ImVec2(ts.x + px * 2.0f, ts.y + py * 2.0f));
}

}  // namespace

Console::Console(int aId, ImVec2 aPos, bool withStarterCode) : id(aId), startPos(aPos)
{
    editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
    editor.SetPalette(TextEditor::GetLightPalette());
    editor.SetTabSize(4);
    editor.SetShowWhitespaces(false);

    if (withStarterCode)
    {
        // Am Anfang ist ALLES gesperrt - auch block.mine(). Deshalb steht hier
        // nur ein leeres main(): alles andere waere sofort eine Fehlermeldung.
        editor.SetText(
            "// At the start your program cannot do anything yet.\n"
            "//\n"
            "// Click the block to mine it by hand.\n"
            "// What you mined goes into your bag - you can\n"
            "// sell it there.\n"
            "//\n"
            "// Every command - block.mine(), item.sell(), while,\n"
            "// if - has to be bought in the skill tree first.\n"
            "\n"
            "int main() {\n"
            "\n"
            "}\n");
    }
    else
    {
        // Weitere Konsolen sind Beiwerk zum Programm - hier gehoert kein
        // zweites main() hin.
        editor.SetText("// This console belongs to the same program.\n"
                       "// The console with main() can use what you write here.\n"
                       "\n"
                       "int counter = 0;\n");
    }
}

bool DrawConsole(Console& c, Engine& engine)
{
    bool trigger = false;

    ImGui::SetNextWindowSize(ImVec2(510.0f, 330.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(c.startPos, ImGuiCond_FirstUseEver);

    char title[64];
    std::snprintf(title, sizeof(title), "###konsole%d", c.id);

    // Die Konsole ist eine Karte, kein Fenster mit Titelleiste: Kopfzeile,
    // Code, Fusszeile. Verschieben geht ueber die Kopfzeile (siehe unten).
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    const ImGuiWindowFlags wflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin(title, nullptr, wflags))
    {
        ImGui::PopStyleVar();

        // Konsolen bleiben im freien Bereich. Ohne das legt sich eine, die
        // frueher einmal gross gezogen wurde, ueber die Rundenleiste - und
        // dann sieht man die Uhr nicht mehr.
        {
            const ImGuiViewport* vp    = ImGui::GetMainViewport();
            const float          unten = vp->WorkPos.y + vp->WorkSize.y;
            const ImVec2         wpos  = ImGui::GetWindowPos();
            const ImVec2         wsize = ImGui::GetWindowSize();

            if (wpos.y + wsize.y > unten)
            {
                float hoehe = unten - wpos.y;
                if (hoehe < 180.0f)
                    hoehe = 180.0f;
                ImGui::SetWindowSize(ImVec2(wsize.x, hoehe));
            }
        }

        const ImGuiIO& io      = ImGui::GetIO();
        const bool     focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const RunState st      = engine.state();
        const bool     running = st == RunState::Running;

        ImDrawList*  dl    = ImGui::GetWindowDrawList();
        const ImVec2 wp    = ImGui::GetWindowPos();
        const ImVec2 ws    = ImGui::GetWindowSize();
        const float  kopfH = 48.0f;
        const float  fussH = 30.0f;

        // ---- Kopfzeile: Name, Schildchen, Startknopf ---------------------
        dl->AddLine(ImVec2(wp.x + 1.0f, wp.y + kopfH), ImVec2(wp.x + ws.x - 1.0f, wp.y + kopfH),
                    ui::kBorder, 1.0f);

        char name[32];
        std::snprintf(name, sizeof(name), "Console %d", c.id);
        dl->AddText(ImVec2(wp.x + 18.0f, wp.y + (kopfH - ImGui::GetTextLineHeight()) * 0.5f),
                    ui::kText, name);

        ImGui::SetCursorScreenPos(
            ImVec2(wp.x + 18.0f + ImGui::CalcTextSize(name).x + 12.0f, wp.y + 14.0f));
        Chip(running ? "running" : "ready");

        ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x - 30.0f - 14.0f, wp.y + 9.0f));

        // Der eine Knopf. Er steuert das GANZE Programm, nicht nur diese
        // Konsole - deshalb sieht er in allen Konsolen gleich aus.
        trigger = PlayPauseButton(running);

        ImGui::SetCursorScreenPos(ImVec2(wp.x + 8.0f, wp.y + kopfH + 6.0f));

        if (focused && io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            trigger = true;

        // Strg+Alt+F formatiert - absichtlich ohne eigenen Knopf,
        // damit die Konsole bei einem einzigen Knopf bleibt.
        if (focused && io.KeyCtrl && io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F, false) &&
            !running)
        {
            c.editor.SetText(FormatCode(c.editor.GetText()));
            c.edited = true;
        }

        // Nur waehrend des Laufens gesperrt. Bei Pause darf man wieder tippen -
        // der naechste Druck auf den Knopf startet dann von vorne.
        c.editor.SetReadOnly(running);

        // Laufende Zeile markieren - aber nur in der Konsole, in der es gerade
        // laeuft. Bei einem Funktionsaufruf in eine andere Konsole wandert die
        // Markierung dorthin.
        const bool hereNow = engine.currentConsole() == c.id;
        const int  line    = hereNow ? engine.currentLine() : 0;

        TextEditor::Breakpoints marks;
        if (line > 0)
            marks.insert(line);
        c.editor.SetBreakpoints(marks);

        TextEditor::ErrorMarkers errors;
        if (engine.errorConsole() == c.id && engine.errorLine() > 0)
            errors[engine.errorLine()] = engine.message();
        c.editor.SetErrorMarkers(errors);

        // Der Editor soll mitscrollen - dafuer wandert der Cursor auf die
        // laufende Zeile. Das hat aber einen Haken: der Editor holt sich beim
        // naechsten Zeichnen den Fokus (ImGui::SetWindowFocus), und ImGui
        // raeumt dabei den gerade gedrueckten Knopf weg. Waehrend also ein
        // Programm laeuft, kaeme kein einziger Klick irgendwo sonst mehr an -
        // man haette die Seite nicht mehr wechseln koennen.
        //
        // Deshalb: solange die Maus gedrueckt ist oder irgendetwas angefasst
        // wird, bleibt der Cursor stehen. Sobald losgelassen wird, springt er
        // auf die dann aktuelle Zeile - man sieht davon nichts.
        const bool angefasst = ImGui::IsAnyItemActive() ||
                               ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                               ImGui::IsMouseDown(ImGuiMouseButton_Right);

        if (line > 0 && line != c.markedLine && !angefasst)
        {
            c.editor.SetCursorPosition(TextEditor::Coordinates(line - 1, 0));
            c.markedLine = line;
        }
        else if (line == 0)
        {
            c.markedLine = 0;
        }

        float editHeight = ws.y - kopfH - fussH - 12.0f;
        if (editHeight < 60.0f)
            editHeight = 60.0f;

        c.editor.Render("##code", ImVec2(ws.x - 16.0f, editHeight), false);

        if (c.editor.IsTextChanged())
            c.edited = true;

        // ---- Fusszeile: eine Zeile Ausgabe -------------------------------
        // Es ist EIN Programm, also steht in allen Konsolen dasselbe.
        dl->AddLine(ImVec2(wp.x + 1.0f, wp.y + ws.y - fussH),
                    ImVec2(wp.x + ws.x - 1.0f, wp.y + ws.y - fussH), ui::kBorder, 1.0f);

        const std::string& msg = engine.message();
        std::string        fuss;
        if (!msg.empty())
            fuss = msg;
        else
            fuss = "ready  -  Ctrl+Enter runs, Ctrl+Alt+F formats";

        const float fy = wp.y + ws.y - fussH + (fussH - ImGui::GetTextLineHeight()) * 0.5f;
        dl->PushClipRect(ImVec2(wp.x + 1.0f, wp.y + ws.y - fussH),
                         ImVec2(wp.x + ws.x - 1.0f, wp.y + ws.y), true);
        dl->AddText(ImVec2(wp.x + 16.0f, fy),
                    (st == RunState::Failed) ? ui::kBad : ui::kTextDim, fuss.c_str());
        dl->PopClipRect();
    }
    else
    {
        ImGui::PopStyleVar();
    }
    ImGui::End();

    return trigger;
}
