#pragma once

#include "engine.h"

#include "TextEditor.h"
#include "imgui.h"

struct World;

// Eine Konsole: ein verschiebbares Fenster mit genau drei Dingen -
// ein Knopf (Play/Pause), der Code-Editor, eine Ausgabezeile.
//
// Wichtig: Eine Konsole ist KEIN eigenes Programm, sondern ein Stueck davon -
// wie eine Datei in einem normalen C++-Projekt. Alle Konsolen zusammen werden
// als ein Programm uebersetzt. Deshalb gibt es hier auch keinen eigenen Motor:
// den teilen sich alle.
struct Console
{
    int        id = 1;
    TextEditor editor;
    bool       open       = true;
    ImVec2     startPos   = ImVec2(40.0f, 60.0f);
    int        markedLine = 0;      // damit nur beim Wechsel gescrollt wird
    bool       edited     = false;  // seit dem Start geaendert -> Neustart

    Console(int aId, ImVec2 aPos, bool withStarterCode);
};

// Zeichnet die Konsole. Rueckgabe: true, wenn der Knopf (oder Strg+Enter)
// gedrueckt wurde - was mit dem GANZEN Programm passiert, entscheidet main.
bool DrawConsole(Console& console, Engine& engine);
