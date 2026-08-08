#pragma once

#include <map>
#include <random>
#include <string>
#include <vector>

struct OrePlan;

// Die Welt. Aktuell: genau ein Block in der Mitte - aber jedes Mal ein anderer.
struct World
{
    // Geteilte Variablen. Jede Konsole laeuft in einem eigenen Prozess und hat
    // deshalb ihren eigenen Speicher - hier ist der gemeinsame Ablageort, ueber
    // den sich mehrere Konsolen etwas sagen koennen.
    std::map<std::string, int> shared;

    bool blockAlive = true;
    int  minedCount = 0;

    // Welches Erz gerade dasteht und mit welchem Muster. Der Startwert wird bei
    // jedem Nachwachsen neu gewuerfelt - deshalb sieht kein Block aus wie der
    // davor, obwohl die Farben zum Erz gehoeren.
    int      ore     = 0;
    unsigned oreSeed = 1;

    // Abbau braucht Zeit. Solange der Zaehler laeuft, steht der Block noch da.
    bool  mining    = false;
    float mineTimer = 0.0f;

    // Von Hand angeklickt statt vom Programm abgebaut. Das laeuft auch dann
    // weiter, wenn kein Programm laeuft - am Anfang hat man ja noch keins.
    bool byHand = false;

    // Was man abgebaut, aber noch nicht verkauft hat: Erz-Nummer -> Anzahl.
    // Abbauen bringt kein Geld, erst das Verkaufen tut das.
    std::map<int, int> inventory;

    // Steigt mit dem Spielstand. Welche Erze es ueberhaupt geben kann, haengt
    // daran. Ausgerechnet wird es woanders (siehe main), hier steht nur das
    // Ergebnis.
    int level = 1;

    // Geld gibt es beim Verkaufen: anzahl * wert * moneyPerBlock.
    int money         = 0;
    int moneyPerBlock = 1;

    int   lastOre  = 0;     // was zuletzt in die Tasche ging (fuer die Anzeige)
    int   lastSold = 0;     // was der letzte Verkauf gebracht hat
    float sellFx   = 0.0f;  // laeuft nach einem Verkauf von 1 auf 0

    // Nachwachsen: nach dem Abbauen kommt der Block von selbst zurueck.
    float respawnSeconds = 0.6f;
    float respawnTimer   = 0.0f;  // laeuft runter, solange der Block weg ist

    float fx = 0.0f;  // Abbau-Effekt, laeuft von 1 auf 0

    std::mt19937 rng{20260808u};  // wuerfelt das naechste Erz

    bool mine();       // faengt an abzubauen; false = da ist nichts (mehr)
    bool mineByHand();  // dasselbe, aber per Mausklick - laeuft ohne Programm
    bool place();      // true = jetzt hingesetzt, false = stand schon da

    // Verkaufen. Ohne Erz-Nummer geht alles ueber die Theke.
    // Rueckgabe: wie viel Geld es gab.
    int sell(const OrePlan& ores);
    int sell(const OrePlan& ores, int oreIndex);

    // Fuer block.sell("Stein", 3): nur diese Sorte, hoechstens so viele.
    // anzahl < 0 heisst alles davon.
    int sell(const OrePlan& ores, const std::string& name, int anzahl);

    int inventoryCount() const;  // wie viele Bloecke insgesamt in der Tasche

    // Fuer block.has("Stein"): wie viele davon liegen in der Tasche.
    int inventoryOf(const OrePlan& ores, const std::string& name) const;

    // Einen Knopf "Block zuruecksetzen" gibt es mit Absicht nicht: damit
    // koennte man von Hand schneller abbauen als jedes Programm.

    // Der Abbau gehoert zum laufenden Programm: er kommt nur voran, solange das
    // Programm laeuft. Sonst wuerde ein angehaltenes Programm weiter abbauen -
    // man druckt auf Pause und der Block geht trotzdem kaputt.
    void tickMining(float dt, const OrePlan& ores);

    // Abbau abbrechen, der Block bleibt ganz. Beim Stoppen des Programms.
    void cancelMining();

    // Nachwachsen und Effekte. Laeuft immer, auch im Skilltree - der Block
    // waechst ja auch nach, waehrend man dort etwas kauft. Beim Nachwachsen
    // wird das naechste Erz gewuerfelt, deshalb die Liste.
    void update(float dt, const OrePlan& ores);
};

// Zeichnet die Welt in den Hintergrund (hinter alle Konsolen).
// Ein Klick auf den Block baut ihn ab - dafuer braucht man kein Programm.
void DrawWorld(World& world, const OrePlan& ores);

// Die Tasche: was man abgebaut hat. Rechtsklick auf eine Zeile verkauft sie.
void DrawInventory(World& world, const OrePlan& ores);
