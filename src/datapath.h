#pragma once

#include <string>
#include <vector>

// Wo eine Datei aus data/ liegen koennte - in der Reihenfolge, in der gesucht
// wird.
//
// Das stand vorher in acht Dateien acht Mal fast gleich da, jedes Mal mit
// GetModuleFileName und Backslashes. Jetzt steht es einmal hier: der Ort haengt
// nur davon ab, wo die Programmdatei liegt, und das weiss proc.h auf beiden
// Systemen.
//
// Gesucht wird von "neben dem Programm" bis "zwei Ordner darueber", weil die
// Exe beim Entwickeln in build/Debug liegt und beim Spielen direkt neben data/.
std::vector<std::string> DataPaths(const std::string& name);
