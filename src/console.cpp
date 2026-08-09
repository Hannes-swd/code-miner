#include "console.h"

#include "format.h"
#include "world.h"

#include <cstdio>

namespace
{

// Ein Knopf, der zwischen Play-Dreieck und Pause-Balken wechselt.
// Beides wird selbst gezeichnet - dadurch braucht das Programm keine
// Symbol-Schriftart.
bool PlayPauseButton(bool showPause)
{
    const float  h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();

    const bool pressed = ImGui::Button("##playpause", ImVec2(h, h));

    ImDrawList* dl  = ImGui::GetWindowDrawList();
    const ImU32 col = IM_COL32(232, 234, 240, 255);
    const float cx  = p.x + h * 0.5f;
    const float cy  = p.y + h * 0.5f;
    const float s   = h * 0.26f;

    if (showPause)
    {
        const float w = s * 0.45f;
        dl->AddRectFilled(ImVec2(cx - s * 0.8f, cy - s), ImVec2(cx - s * 0.8f + w, cy + s), col);
        dl->AddRectFilled(ImVec2(cx + s * 0.8f - w, cy - s), ImVec2(cx + s * 0.8f, cy + s), col);
    }
    else
    {
        dl->AddTriangleFilled(ImVec2(cx - s * 0.55f, cy - s), ImVec2(cx - s * 0.55f, cy + s),
                              ImVec2(cx + s * 0.9f, cy), col);
    }

    return pressed;
}

}  // namespace

Console::Console(int aId, ImVec2 aPos, bool withStarterCode) : id(aId), startPos(aPos)
{
    editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
    editor.SetPalette(TextEditor::GetDarkPalette());
    editor.SetTabSize(4);
    editor.SetShowWhitespaces(false);

    if (withStarterCode)
    {
        // Am Anfang ist ALLES gesperrt - auch block.mine(). Deshalb steht hier
        // nur ein leeres main(): alles andere waere sofort eine Fehlermeldung.
        editor.SetText(
            "// Am Anfang kann dein Programm noch gar nichts.\n"
            "//\n"
            "// Klick auf den Block, um ihn von Hand abzubauen.\n"
            "// Was du abgebaut hast, liegt in der Tasche - dort\n"
            "// kannst du es mit Rechtsklick verkaufen.\n"
            "//\n"
            "// Jeden Befehl - block.mine(), item.sell(), while,\n"
            "// if - musst du dir erst im Skilltree kaufen.\n"
            "\n"
            "int main() {\n"
            "\n"
            "}\n");
    }
    else
    {
        // Weitere Konsolen sind Beiwerk zum Programm - hier gehoert kein
        // zweites main() hin.
        editor.SetText("// Diese Konsole gehoert zum selben Programm.\n"
                       "// Was hier steht, kann die Konsole mit main() benutzen.\n"
                       "\n"
                       "int zaehler = 0;\n");
    }
}

bool DrawConsole(Console& c, Engine& engine)
{
    bool trigger = false;

    ImGui::SetNextWindowSize(ImVec2(470.0f, 340.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(c.startPos, ImGuiCond_FirstUseEver);

    char title[64];
    std::snprintf(title, sizeof(title), "Konsole %d###konsole%d", c.id, c.id);

    if (ImGui::Begin(title, &c.open))
    {
        const ImGuiIO& io      = ImGui::GetIO();
        const bool     focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const RunState st      = engine.state();
        const bool     running = st == RunState::Running;

        // Der eine Knopf. Er steuert das GANZE Programm, nicht nur diese
        // Konsole - deshalb sieht er in allen Konsolen gleich aus.
        trigger = PlayPauseButton(running);

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

        const float outHeight = ImGui::GetTextLineHeightWithSpacing();
        float       editHeight =
            ImGui::GetContentRegionAvail().y - outHeight - ImGui::GetStyle().ItemSpacing.y;
        if (editHeight < 60.0f)
            editHeight = 60.0f;

        c.editor.Render("##code", ImVec2(0.0f, editHeight), true);

        if (c.editor.IsTextChanged())
            c.edited = true;

        // Eine Zeile Ausgabe. Es ist ein Programm, also steht ueberall dasselbe.
        const std::string& msg = engine.message();
        if (st == RunState::Failed)
            ImGui::TextColored(ImVec4(1.0f, 0.44f, 0.40f, 1.0f), "%s", msg.c_str());
        else if (!msg.empty())
            ImGui::TextUnformatted(msg.c_str());
        else
            ImGui::TextDisabled("bereit  -  Strg+Enter startet, Strg+Alt+F formatiert");
    }
    ImGui::End();

    return trigger;
}
