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

    // Am Ende der Runde musst du mindestens so viel Geld haben, sonst ist das
    // Spiel vorbei. Der Betrag waechst mit jeder Runde - sonst waere Runde 10
    // so leicht wie Runde 1.
    // Das Wachstum flacht ab. Am Anfang verdoppelt sich dein Verdienst fast von
    // Runde zu Runde: der erste Ofen, das erste Legieren, die ersten Schleifen
    // sind je ein Sprung. Spaeter bleiben es dieselben Techniken, nur ein
    // bisschen schneller - und dann waere ein Ziel, das stur mal 1.85 nimmt,
    // irgendwann nicht mehr einzuholen.
    //
    // Deshalb faellt der Faktor je Runde von targetGrowth in Richtung
    // targetGrowthEnd, mit targetFlatten als Tempo. Der Kurve sieht man das an:
    // erst steil, dann immer flacher.
    int   targetStart     = 500;
    float targetGrowth    = 2.4f;   // Faktor fuer die erste Runde
    float targetGrowthEnd = 1.2f;   // ... auf den er ganz spaet zulaeuft
    float targetFlatten   = 0.85f;  // wie schnell er dorthin faellt (0..1)

    // Was passiert, wenn du es nicht schaffst:
    //   true  = alles auf Anfang (so ist es gedacht)
    //   false = nur eine Meldung, dieselbe Runde noch einmal (zum Ausprobieren)
    bool resetOnLoss = true;

    // Das Ziel wird am Ende der Runde abgezogen: es ist die Miete, nicht nur
    // eine Huerde. Dadurch haeuft sich Geld nicht endlos an - nur was ueber dem
    // Ziel liegt, bleibt dir zum Einkaufen.
    bool deductTarget = true;

    std::vector<std::string> problems;  // was an der Datei nicht stimmte
    std::string              file;
};

// Sucht data/runden.json an den ueblichen Stellen und liest sie ein.
RoundPlan LoadRoundPlan();

// Wie viel Geld am Ende von Runde n dastehen muss.
int RoundTarget(const RoundPlan& plan, int round);

// Hat die letzte Runde gereicht? Gilt erst in der Abrechnung.
bool RoundWon(const World& world, const RoundPlan& plan);

// Runde starten: die Uhr beginnt zu laufen, und die Zahlen fuer die Abrechnung
// werden festgehalten.
void StartRound(World& world, const RoundPlan& plan);

// Zeit um. Laufender Auftrag zurueck in die Tasche, Tasche verkaufen, Zahlen
// einsammeln. Das Programm stoppt der Aufrufer - die Runde kennt den Motor
// nicht.
void FinishRound(World& world, const RoundPlan& plan, const OrePlan& ores,
                 const CraftPlan& craft);

// Abrechnung weggeklickt: zurueck in die Vorbereitung. Die Rundennummer steigt
// nur, wenn das Ziel geschafft war.
void NextRound(World& world, const RoundPlan& plan);

// Restzeit als mm:ss.
std::string RoundClock(float seconds);

// Die Runden-Anzeige, oben in der Mitte ueber der Seite. Sie steht bewusst
// NICHT in der Menueleiste: dort war sie eine Textzeile zwischen Reitern, und
// gerade die Uhr und der Abstand zum Ziel gehen so unter.
// Rueckgabe: true = "Runde starten" wurde gedrueckt.
bool DrawRoundHud(const World& world, const RoundPlan& plan);

// Die Abrechnung, mitten im Bild.
// Rueckgabe: true = weitergeklickt. Bei einer Niederlage mit "alles auf
// Anfang" heisst das: neu anfangen - das macht der Aufrufer, die Runde kennt
// den Rest des Spiels nicht.
bool DrawRoundReport(const World& world, const RoundPlan& plan);
