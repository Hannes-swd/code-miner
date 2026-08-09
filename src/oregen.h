#pragma once

#include <string>
#include <vector>

struct OrePlan;
struct AlloyPlan;

// Erze, die in keiner Datei stehen: das Spiel wuerfelt sie sich selbst
// zusammen. Was dabei erlaubt ist, steht in data/erzgenerator.json - die
// Wortlisten fuer die Namen, die Arten (Metall, Kristall, Gestein) und die
// Kurven fuer Wert, Seltenheit und Level.
//
// ES HOERT NIE AUF. Am Anfang gibt es nur die Erze aus data/erze.json - Stein,
// Kohle, Kupfer bis Diamant. Ab "ab_level" kommt alle "je_level" Level EIN
// neues gewuerfeltes Erz dazu, und zwar fuer immer. Die Seltenheit wird aus dem
// Level gerechnet, in dem das Erz entsteht: Level 40 bringt also etwas, das
// weit jenseits von Diamant liegt. Ein Ende der Fahnenstange gibt es nicht.
//
// Gewuerfelt wird nur EINE Zahl je Erz: die Seltenheit. Wert, Abbaudauer und
// Level haengen daran. Deshalb kann kein Erz gleichzeitig haeufig und teuer
// sein - die haesslichste Falle beim Wuerfeln von Werten.
//
// Ein fertig gewuerfeltes Erz wandert in den Spielstand, mit allem was es
// ausmacht. Es ist damit ab dem Moment seiner Entstehung fest - selbst wenn
// jemand spaeter die Wortlisten oder die Kurven aendert, bleibt es, wie es war.
// Es MUSS so sein: im Spielstand steht in der Tasche nur die NUMMER eines
// Erzes.

// Eine Art: wonach ein Erz aussieht und was man damit machen darf.
struct OreGenKind
{
    std::string              name;
    int                      weight = 1;
    std::vector<std::string> cores;   // Kernwoerter fuer den Namen
    std::vector<std::string> states;  // Zustands-Schluessel wie in data/erze.json

    float hueFrom = 0.0f, hueTo = 360.0f;
    float satFrom = 0.3f, satTo = 0.8f;
    float patFrom = 4.0f, patTo = 10.0f;
    int   purFrom = 50, purTo = 85;

    bool alloyable = false;
};

struct OreGenPlan
{
    bool     active = false;  // false = Datei fehlt oder ist leer, dann passiert nichts
    unsigned seed     = 0;
    int      minLevel = 4;  // ab diesem Level kommt das erste gewuerfelte Erz
    int      perLevel = 3;  // und danach alle so viele Level eines mehr

    std::vector<std::string> front, middle, back;
    std::vector<OreGenKind>  kinds;

    // Wie stark die Seltenheit um den Wert streuen darf, der zum Level passt.
    float raritySpread = 0.35f;

    float valueBase = 0.45f, valueSlope = 1.35f, valueScatter = 0.12f;
    float mineBase = 0.5f, mineSlope = 0.38f;
    float levelSlope = 0.85f, levelPer = 6.0f;

    float alloyShare = 0.55f;
    std::vector<std::string> alloyCores;
    float alloySecFrom = 2.0f, alloySecTo = 4.5f;
    int   alloyPurFrom = -6, alloyPurTo = 8;
    float alloyValFrom = 1.3f, alloyValTo = 1.7f;

    std::vector<std::string> problems;  // was an der Datei nicht stimmte
    std::string              file;
};

// Sucht data/erzgenerator.json an den ueblichen Stellen und liest sie ein.
OreGenPlan LoadOreGenPlan();

// Ab welchem Level das naechste gewuerfelte Erz faellig ist.
int NextRollLevel(const OreGenPlan& gen, int rolled);

// Wuerfelt so viele Erze, wie das Level inzwischen hergibt - meistens keines,
// manchmal eines. Wird jedes Bild aufgerufen; wenn nichts faellig ist, tut es
// nichts.
//
// Rueckgabe: wie viele Erze dazugekommen sind. Neues haengt immer HINTEN an
// der Liste, in der Reihenfolge seiner Entstehung. Dadurch bleibt jede Nummer,
// die schon einmal vergeben wurde, fuer immer dieselbe - und genau darauf
// verlaesst sich der Spielstand.
int RollNewOres(OrePlan& ores, AlloyPlan& alloys, OreGenPlan& gen, int level);
