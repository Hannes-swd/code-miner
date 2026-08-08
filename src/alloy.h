#pragma once

#include <string>
#include <vector>

struct OrePlan;

// Legieren. Welche Rezepte es gibt, steht in data/legierungen.json - nicht im
// Programm.
//
// Zwei verschiedene Erze werden zu einem NEUEN Stoff zusammengefuehrt, der mehr
// wert ist als seine Teile und sich danach ganz normal weiterverarbeiten laesst.
// Das Ergebnis haengt hinten an der Erzliste: in der Tasche verhaelt es sich
// damit wie jedes andere Erz, nur abbaubar ist es nicht (Ore::minable).
//
// Der Reiz daran ist die Entscheidung: legieren kostet beide Zutaten und Zeit.
// Wer sich nicht sicher ist, ob beides da ist, muss vorher nachfragen - dafuer
// gibt es block.canAlloy(...) und damit einen Grund fuer "if".
struct AlloyPart
{
    int ore   = 0;  // Nummer in OrePlan::ores
    int count = 1;  // so viele Stueck davon je Stueck Ergebnis
};

struct AlloyRecipe
{
    std::string name;  // so heisst das Ergebnis und so ruft man es auf

    std::vector<AlloyPart> parts;

    // In welchen Zustaenden die Zutaten vorliegen muessen, ein Bit je OreState.
    // Eine Liste, weil die Zutaten verschieden weit sein duerfen: eine schon
    // legiert, die daneben nur geschmolzen.
    unsigned from = 0;

    int   to      = 0;     // OreState des Ergebnisses
    float seconds = 1.0f;  // pro Stueck
    int   purity  = 0;     // Aufschlag auf das Mittel der Zutaten

    int result = -1;  // Nummer des Ergebnis-Erzes in OrePlan::ores

    bool fits(int state) const
    {
        return state >= 0 && (from & (1u << (unsigned)state)) != 0;
    }
};

struct AlloyPlan
{
    std::vector<AlloyRecipe> recipes;

    std::vector<std::string> problems;  // was an der Datei nicht stimmte
    std::string              file;

    // Rezept zum Namen, nullptr = kenne ich nicht. Gross- und Kleinschreibung
    // ist egal, genau wie bei den Erzen.
    const AlloyRecipe* find(const std::string& name) const;
};

// Sucht data/legierungen.json an den ueblichen Stellen und liest sie ein.
//
// Die Ergebnisse werden hinten an die Erzliste angehaengt - deshalb ist die
// Erzliste hier nicht const. Die Reihenfolge muss stabil bleiben: im
// Spielstand steht die Nummer des Erzes.
AlloyPlan LoadAlloyPlan(OrePlan& ores);
