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

struct Ore
{
    std::string name = "Stein";

    // Je groesser, desto seltener. Stein 1 ist der Normalfall, Gold 30 kommt
    // dreissigmal seltener vor als Stein.
    float rarity = 1.0f;

    float mineSeconds = 0.5f;  // wie lange block.mine() an ihm arbeitet
    int   minLevel    = 1;     // ab welchem Level er ueberhaupt auftaucht
    int   value       = 1;     // Geld pro Block, mal dem Boost aus dem Baum

    Color color1;      // helle Adern
    Color color2;      // dunkler Grund
    float pattern = 5.0f;  // wie fein das Muster ist
};

struct OrePlan
{
    std::vector<Ore> ores;

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

// Welches Erz kommt als naechstes? Nur was das Level erlaubt, und je seltener,
// desto unwahrscheinlicher.
int RollOre(const OrePlan& plan, int level, std::mt19937& rng);

// Erz-Nummer zu einem Namen, -1 = kenne ich nicht. Gross- und Kleinschreibung
// ist egal: "stein" findet auch "Stein".
int FindOre(const OrePlan& plan, const std::string& name);

// Perlin-artiges Rauschen, 0..1. Gleicher Startwert = gleiches Muster.
float OreNoise(float x, float y, unsigned seed);
