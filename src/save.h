#pragma once

#include <memory>
#include <vector>

struct AlloyPlan;
struct Console;
struct OrePlan;
struct SkillTree;
struct World;

// Spielstand: Geld, der gewachsene Baum, die geteilten Werte und der Code in
// den Konsolen.
//
// Gespeichert wird neben der exe in "spielstand.txt" - eine Textdatei, die man
// zur Not auch von Hand anschauen oder loeschen kann.
//
// Warum der Baum mitgespeichert wird: er wird ja erst beim Spielen gewuerfelt.
// Ohne ihn saehe der Baum nach einem Neustart anders aus als vorher.
//
// Aus demselben Grund stehen auch die gewuerfelten Erze darin (siehe
// oregen.h). Sie entstehen beim Spielen, und in der Tasche steht nur die
// NUMMER eines Erzes - ohne sie waere nach dem Laden alles verschoben.

bool SaveGame(const World& world, const SkillTree& tree,
              const std::vector<std::unique_ptr<Console>>& consoles, const OrePlan& ores,
              const AlloyPlan& alloys);

// true = es gab einen Spielstand und er wurde geladen.
//
// Erz- und Legierungsliste kommen mit: was einmal gewuerfelt wurde, wird hier
// wieder angehaengt. Beide muessen vorher aus ihren Dateien geladen sein.
bool LoadGame(World& world, SkillTree& tree, std::vector<std::unique_ptr<Console>>& consoles,
              int& nextConsoleId, OrePlan& ores, AlloyPlan& alloys);

void DeleteSave();
