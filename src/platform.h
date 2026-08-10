#pragma once

#include "imgui.h"

// Fenster und Grafik - das Einzige, was ausser proc.h noch am System haengt.
//
// Unter Windows ist das Win32 + DirectX 11 (beides gehoert zu Windows, es muss
// also nichts installiert werden), unter Linux GLFW + OpenGL 3. Das Spiel
// selbst sieht davon nichts: es macht Init, dann Frame fuer Frame, dann
// Shutdown - und zeichnet dazwischen mit ImGui.
namespace plat
{

// Fenster aufmachen, Grafik starten, ImGui anmelden. false = ging nicht,
// der Grund steht dann schon auf der Konsole bzw. in einem Meldungsfenster.
bool Init(const char* title, int width, int height);

// Die Schrift laden. Steht hier, weil jedes System seine Schriften woanders
// hat - Consolas unter Windows, DejaVu Sans Mono unter Linux.
void LoadFont();

// Ein Bild anfangen. false = das Fenster wurde zugemacht, dann ist Schluss.
// Danach darf gezeichnet werden.
bool BeginFrame();

// Das Bild fertig machen und zeigen. grund ist die Farbe des Seitengrunds.
void EndFrame(ImVec4 grund);

// Alles wieder zumachen.
void Shutdown();

}  // namespace plat
