#pragma once

#include <random>
#include <string>
#include <vector>

// Erze. Welche es gibt, steht in data/erze.json - nicht im Programm.
//
// Jeder Block, der nachwaechst, wird neu gewuerfelt: welches Erz, und mit
// welchem Muster-Startwert. Dadurch sieht kein Block genau aus wie der davor,
// aber die Farben bleiben - Gold ist immer gelb-braun gesprenkelt, Kohle immer
// fast schwarz. Erkennen soll man es an Farbe und Muster, nicht an einer
// Beschriftung.

struct Color
{
    unsigned char r = 128, g = 128, b = 128;
};

// In welchem Zustand ein Block sein kann.
//
// Wie man von einem Zustand in den naechsten kommt, steht noch nirgends - das
// kommt spaeter. Hier gibt es erstmal nur die Zustaende selbst und die Frage,
// welche bei welchem Erz ueberhaupt erlaubt sind: Diamant schmilzt man nicht.
enum class OreState
{
    Raw,       // direkt aus dem Abbau
    Washed,    // gewaschen
    Smelted,   // geschmolzen
    Cast,      // gegossen
    Polished,  // poliert
    Hardened,  // gehaertet
    Refined,   // veredelt
    Pressed,   // gepresst
    Cleaned,   // gereinigt
    Oxidized,  // oxidiert
    Alloy,     // legiert
    Count
};

// Was in der Anzeige steht ("Roh", "Gewaschen", ...).
const char* OreStateName(OreState state);

// Wie der Zustand in data/erze.json heisst ("raw", "washed", ...).
const char* OreStateKey(OreState state);

// Text zum Schluessel, -1 = kenne ich nicht.
int FindOreState(const std::string& key);

struct Ore
{
    std::string name = "Stein";

    // Je groesser, desto seltener. Stein 1 ist der Normalfall, Gold 30 kommt
    // dreissigmal seltener vor als Stein.
    float rarity = 1.0f;

    float mineSeconds = 0.5f;  // wie lange block.mine() an ihm arbeitet
    int   minLevel    = 1;     // ab welchem Level er ueberhaupt auftaucht
    int   value       = 1;     // Geld pro Block, mal dem Boost aus dem Baum

    // Kommt der Stoff im Boden vor? Legierungen nicht: die entstehen nur in
    // der Werkstatt und duerfen nie als Block dastehen. Sie haengen trotzdem
    // in dieser Liste, damit sie sich in der Tasche wie jedes Erz verhalten.
    bool minable = true;

    // Mit welcher Reinheit der Block aus dem Boden kommt, in Prozent.
    // -1 = steht nicht in der Datei, dann gilt der Startwert aus
    // data/verarbeitung.json.
    int purity = -1;

    Color color1;      // helle Adern
    Color color2;      // dunkler Grund
    float pattern = 5.0f;  // wie fein das Muster ist

    // Welche Zustaende es bei diesem Erz geben darf - ein Bit je Zustand.
    // Steht in der Datei nichts, sind alle erlaubt.
    unsigned states = 0xFFFFFFFFu;

    // Womit sich das Erz legieren laesst. data/legierungen.json prueft jedes
    // Rezept dagegen: was hier nicht zueinander steht, laesst sich nicht
    // zusammenschmelzen.
    std::vector<std::string> alloyWith;

    // Steht der Name in "legierbar_mit"? Gross- und Kleinschreibung ist egal.
    bool alloyableWith(const std::string& andere) const;

    bool allows(OreState s) const
    {
        return (states & (1u << (unsigned)s)) != 0;
    }
};

struct OrePlan
{
    std::vector<Ore> ores;

    // Wie viele Erze aus den Dateien kommen: data/erze.json und die Ergebnisse
    // aus data/legierungen.json. Alles ab dieser Nummer hat sich das Spiel
    // selbst ausgedacht (siehe oregen.h) - und genau das steht im Spielstand,
    // denn beim naechsten Start wuerde es sonst fehlen.
    int handmade = 0;

    // Wie viele gewuerfelte Erze es schon gibt (ohne die Ergebnisse ihrer
    // Legierungen). Daran haengt, wann das naechste faellig ist.
    int rolled = 0;

    // Woraus sich das Level ergibt: "bloecke" (abgebaute Bloecke) oder
    // "skills" (gekaufte Punkte im Baum).
    std::string levelFrom = "bloecke";
    int         perLevel  = 25;

    std::vector<std::string> problems;  // was an der Datei nicht stimmte
    std::string              file;

    bool empty() const { return ores.empty(); }
};

// Sucht data/erze.json an den ueblichen Stellen und liest sie ein.
OrePlan LoadOrePlan();

// "#RRGGBB" oder "#RGB". false = kaputte Angabe, dann bleibt out unveraendert.
// Steht hier, weil auch data/legierungen.json Farben mitbringt.
bool ParseOreColor(const std::string& text, Color& out);

// Welches Erz kommt als naechstes? Nur was das Level erlaubt, nur was im Boden
// vorkommt, und je seltener, desto unwahrscheinlicher.
int RollOre(const OrePlan& plan, int level, std::mt19937& rng);

// Erz-Nummer zu einem Namen, -1 = kenne ich nicht. Gross- und Kleinschreibung
// ist egal: "stein" findet auch "Stein".
int FindOre(const OrePlan& plan, const std::string& name);

// Perlin-artiges Rauschen, 0..1. Gleicher Startwert = gleiches Muster.
float OreNoise(float x, float y, unsigned seed);
