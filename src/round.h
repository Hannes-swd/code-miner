#pragma once

#include <string>
#include <vector>

struct World;
struct OrePlan;
struct CraftPlan;

// Runden geben dem Spiel einen Rhythmus: erst nachdenken, dann laufen lassen.
//
// In der Vorbereitung steht die Welt still - Zeit kostet dort nichts. Erst im
// Lauf tickt die Uhr, und dann zaehlt jede Sekunde. Deshalb lohnt es sich, ein
// Programm VORHER fertig zu haben.
enum class RoundPhase
{
    Prepare,  // unbegrenzt Zeit, die Welt steht still
    Run,      // die Uhr laeuft, alles laeuft
    Report    // Zeit um: was hat die Runde gebracht?
};

// Alles Einstellbare steht in data/runden.json - hier stehen nur die Werte,
// die einspringen, wenn die Datei fehlt.
struct RoundPlan
{
    float seconds       = 900.0f;  // Dauer einer Runde in Sekunden
    bool  sellAtEnd     = true;    // am Ende die ganze Tasche verkaufen
    bool  freezeWorld   = true;    // Vorbereitung: Welt steht still
    bool  handInPrepare = true;    // ... nur der Klick auf den Block geht

    std::vector<std::string> problems;  // was an der Datei nicht stimmte
    std::string              file;
};

// Sucht data/runden.json an den ueblichen Stellen und liest sie ein.
RoundPlan LoadRoundPlan();

// Runde starten: die Uhr beginnt zu laufen, und die Zahlen fuer die Abrechnung
// werden festgehalten.
void StartRound(World& world, const RoundPlan& plan);

// Zeit um. Laufender Auftrag zurueck in die Tasche, Tasche verkaufen, Zahlen
// einsammeln. Das Programm stoppt der Aufrufer - die Runde kennt den Motor
// nicht.
void FinishRound(World& world, const RoundPlan& plan, const OrePlan& ores,
                 const CraftPlan& craft);

// Abrechnung weggeklickt: zurueck in die Vorbereitung, Rundennummer hoch.
void NextRound(World& world);

// Restzeit als mm:ss.
std::string RoundClock(float seconds);

// Gehoert in die Menueleiste: dort ist die Runde von jeder Seite aus zu sehen.
// Rueckgabe: true = "Runde starten" wurde gedrueckt.
bool DrawRoundBar(const World& world, const RoundPlan& plan);

// Die Abrechnung, mitten im Bild. Rueckgabe: true = "Weiter" wurde gedrueckt.
bool DrawRoundReport(const World& world, const RoundPlan& plan);
