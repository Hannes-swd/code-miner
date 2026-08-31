#pragma once

#include <map>
#include <random>
#include <string>
#include <vector>

// Das Erbe: was ein Verlieren dauerhaft bringt.
//
// "Start over" wirft Geld, Baum, Konsolen und gewuerfelte Erze weg (siehe
// resetAll() in main.cpp) - das hier ueberlebt es. Wer verliert, bekommt
// Erbe-Punkte (mehr, je weiter die Runde kam) und kann damit kleine,
// dauerhafte Boni kaufen: mehr Geld pro Block, mehr Rundenzeit und
// aehnliches. Jede Stufe bleibt fuer immer - wie im Skilltree gibt es kein
// Zurueck.
//
// Die Datei ist bewusst klein und listet ihre Wirkungen einzeln auf, statt
// generisch zu sein wie skilltree.h/codecheck.cpp: es sind wenige, feste
// Werte, keine Sprache, die durchgesetzt werden muss.

// Eine Art von Upgrade, wie sie in data/erbe.json steht.
struct PrestigeUpgradeDef
{
    std::string key;   // "geld_prozent" - taucht so auch im Spielstand auf
    std::string name;  // "Geld pro Block" - fuer die Karte
    std::string unit;  // "%", "s" oder leer - fuer die Anzeige

    int   baseCost = 10;
    float growth   = 1.15f;  // Preis je Stufe: baseCost * growth^stufe
    float effect   = 1.0f;   // was eine Stufe bringt - siehe ComputePrestigeEffects
    int   maxLevel = 0;      // 0 = unbegrenzt stapelbar
};

// Alles, was aus data/erbe.json kommt.
struct PrestigePlan
{
    int offerCount = 3;  // wie viele Angebote gleichzeitig zur Auswahl stehen

    // Punkte = pointsBase * Rundennummer^pointsExponent
    //        + Geld beim Verlieren / moneyDivisor
    float pointsBase     = 15.0f;
    float pointsExponent = 1.3f;
    float moneyDivisor   = 800.0f;

    std::vector<PrestigeUpgradeDef> upgrades;

    std::vector<std::string> problems;  // was an der Datei nicht stimmte
    std::string              file;
};

// Sucht data/erbe.json an den ueblichen Stellen und liest sie ein.
PrestigePlan LoadPrestigePlan();

// Der gesuchte Punkt in der Liste, nullptr = Schluessel unbekannt (Datei
// wurde geaendert, ein gespeicherter Schluessel gibt es nicht mehr).
const PrestigeUpgradeDef* FindPrestigeUpgrade(const PrestigePlan& plan, const std::string& key);

// Was die naechste Stufe eines Upgrades kostet.
int PrestigeUpgradeCost(const PrestigeUpgradeDef& def, int level);

// Wie viele Erbe-Punkte ein Verlieren in dieser Runde bringt.
int PrestigePointsEarned(const PrestigePlan& plan, int roundNumber, int moneyAtLoss);

// Der Spielstand des Erbes - ueberlebt "Start over", liegt in einer eigenen
// Datei (siehe SavePrestige/LoadPrestige), nicht in spielstand.txt.
struct Prestige
{
    int points      = 0;  // was gerade da ist, zum Ausgeben
    int totalEarned = 0;  // ueber das ganze Spiel hinweg - nur fuer die Anzeige

    std::map<std::string, int> levels;  // Schluessel -> gekaufte Stufe

    // Die aktuellen Angebote. Bleiben stehen, bis RerollOffers() sie ersetzt -
    // das passiert nur bei einer Niederlage.
    std::vector<std::string> offers;

    std::mt19937 rng{20260808u};
};

int  PrestigeLevel(const Prestige& prestige, const std::string& key);  // 0 = nicht gekauft
bool PrestigeMaxed(const Prestige& prestige, const PrestigeUpgradeDef& def);

// Wuerfelt plan.offerCount Angebote neu - bevorzugt welche, die noch nicht
// ausgereizt sind. Wird beim allerersten Start und nach jeder Niederlage
// gerufen.
void RerollOffers(Prestige& prestige, const PrestigePlan& plan);

// Kauft eine Stufe eines gerade angebotenen Upgrades. false = ging nicht
// (kein Geld, Maximum erreicht, oder der Schluessel steht gar nicht mehr in
// der Datei).
bool BuyPrestigeOffer(Prestige& prestige, const PrestigePlan& plan, const std::string& key);

// Was der Baum an Erbe-Boni gerade hergibt - aus allen gekauften Stufen
// zusammengerechnet. Wird jedes Bild neu ausgerechnet, wie Limits in
// skilltree.h.
struct PrestigeEffects
{
    float moneyMul     = 1.0f;  // auf world.moneyPerBlock
    float respawnMul   = 1.0f;  // auf world.respawnSeconds
    float speedMul     = 1.0f;  // auf Zeilen pro Sekunde
    float assayCostMul = 1.0f;  // auf limits.assayCost
    float targetMul    = 1.0f;  // auf den Rundenziel-Startwert

    float extraSeconds    = 0.0f;  // zusaetzliche Rundenzeit
    int   startMoneyBonus = 0;     // obendrauf auf kStartGeld
    int   extraJobs       = 0;     // zusaetzliche Ofenplaetze
    int   extraConsoles   = 0;     // zusaetzlich erlaubte Konsolen
};

PrestigeEffects ComputePrestigeEffects(const Prestige& prestige, const PrestigePlan& plan);

// Eigene Datei neben der exe, komplett getrennt von spielstand.txt - "Start
// over" (resetAll()/DeleteSave() in main.cpp) ruehrt sie nicht an.
void SavePrestige(const Prestige& prestige);

// true = es gab schon eine Datei. Gibt es noch keine (allererster Start),
// bleibt prestige unveraendert und der Aufrufer wuerfelt die ersten Angebote
// selbst (RerollOffers).
bool LoadPrestige(Prestige& prestige);

// Die Seite in der Menueleiste.
void DrawPrestigePage(Prestige& prestige, const PrestigePlan& plan);
