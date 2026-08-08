#pragma once

#include <memory>
#include <vector>

struct Console;
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

bool SaveGame(const World& world, const SkillTree& tree,
              const std::vector<std::unique_ptr<Console>>& consoles);

// true = es gab einen Spielstand und er wurde geladen.
bool LoadGame(World& world, SkillTree& tree, std::vector<std::unique_ptr<Console>>& consoles,
              int& nextConsoleId);

void DeleteSave();
