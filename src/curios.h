#pragma once

#include <set>

// Kuriositaeten: sehr seltene Funde beim Abbau, ohne echten Nutzen. Die
// Fundmechanik steckt in World::tickMining (world.cpp) - eine feste, sehr
// kleine Chance je abgebautem Block, unabhaengig von Level, Skill oder
// Spielzeit.
//
// Eigene, kleine Datei wie das Erbe (siehe prestige.h) und aus demselben
// Grund: "Start over" und ein verlorenes Rundenziel setzen den normalen
// Spielstand (world.h) zurueck - ein Fund, der im Schnitt mehrere tausend
// Bloecke braucht, darf dabei nicht wieder verschwinden.
//
// Sehen sollen sie KEINE Erze sein: kein Muster, keine gewuerfelte Farbe.
// Gedacht ist an ein eigenes Bild je Nummer (png), das vor dem Fund schwarz
// daliegt und danach sichtbar wird - das gibt es noch nicht, deshalb bis
// dahin ein schlichtes schwarzes Feld je Fund (siehe DrawCuriosityPage).
struct Curios
{
    static constexpr int kCount = 20;

    std::set<int> found;
};

void SaveCurios(const Curios& curios);

// true = es gab eine Datei und sie wurde geladen.
bool LoadCurios(Curios& curios);

// Die Kuriositaeten-Seite: ein Kaestchen je gefundenem Stueck, kein
// gesperrter Platz, keine Zahl "von 20". Wird in main.cpp erst erreichbar,
// sobald curios.found nicht mehr leer ist (der Reiter "Finds").
void DrawCuriosityPage(const Curios& curios);
