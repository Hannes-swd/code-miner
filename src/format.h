#pragma once

#include <string>

// Formatiert C++-Code mit clang-format (Strg+Alt+F in der Konsole).
//
// clang-format.exe liegt bei Visual Studio 2022 bereits dabei, es muss also
// nichts installiert werden. Wird es nicht gefunden oder schlaegt der Aufruf
// fehl, kommt der Text unveraendert zurueck.
std::string FormatCode(const std::string& code);
