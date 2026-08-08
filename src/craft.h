#pragma once

#include "skill.h"

#include <string>
#include <vector>

struct Limits;

// Verarbeiten. Was von wo nach wo geht, steht in data/verarbeitung.json - nicht
// im Programm.
//
// Das ist keine feste Kette, sondern ein Netz: aus einem Zustand koennen
// mehrere Schritte fuehren, und ein Schritt kann aus mehreren Zustaenden heraus
// gehen. Daraus entsteht die Entscheidung, welchen Weg man mit einem Stapel
// geht - der kurze zu wenig Geld oder der lange zu viel.
struct CraftStep
{
    std::string command;  // "wash" - so heisst er im Spielercode
    std::string name;     // "Waschen" - so steht er in der Tasche

    // Aus welchen Zustaenden heraus, ein Bit je OreState. Eine Liste, weil ein
    // Schritt aus mehreren Zustaenden gehen darf.
    unsigned from = 0;
    int      to   = 0;  // OreState

    float seconds = 1.0f;  // pro Stueck
    int   purity  = 0;     // Aenderung in Prozentpunkten, darf negativ sein

    Skill skill = Skill::None;  // ohne den Punkt im Baum geht es nicht

    bool fits(int state) const
    {
        return state >= 0 && (from & (1u << (unsigned)state)) != 0;
    }
};

struct CraftPlan
{
    int   startPurity = 70;   // Reinheit frisch aus dem Boden
    float valueMin    = 0.5f;  // Preisfaktor bei 0 % Reinheit
    float valueMax    = 1.5f;  // ... und bei 100 %

    // Faktor auf den Grundwert des Erzes, je Zustand. Er haengt nur am Zustand
    // und nicht am Weg dorthin - zwei Wege zum selben Zustand geben deshalb
    // denselben Preis.
    std::vector<float> stateValue;

    std::vector<CraftStep> steps;

    std::vector<std::string> problems;  // was an der Datei nicht stimmte
    std::string              file;

    const CraftStep* find(const std::string& command) const;
    float            valueOf(int state) const;

    // Reinheitsfaktor fuer den Preis: linear zwischen wert_min und wert_max.
    float purityFactor(int purity) const;
};

// Sucht data/verarbeitung.json an den ueblichen Stellen und liest sie ein.
CraftPlan LoadCraftPlan();

// Ist der Schritt im Skilltree gekauft?
bool CraftUnlocked(const CraftStep& step, const Limits& limits);
