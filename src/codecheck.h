#pragma once

#include "engine.h"
#include "skilltree.h"

#include <string>
#include <vector>
//test
// Prueft, ob der Code nur benutzt, was im Skilltree freigeschaltet ist.
//
// Passiert VOR dem Kompilieren. Die Meldung landet dort, wo auch
// Compiler-Fehler landen - es gibt keinen zweiten Ort, an dem man nach
// Problemen suchen muss.
//
// Rueckgabe: leerer Text = alles in Ordnung.
std::string CheckLimits(const std::vector<SourceFile>& files, const Limits& limits,
                        int& errorConsole, int& errorLine);
