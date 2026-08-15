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

    // Angehaengt, nicht eingefuegt: im Spielstand steht die NUMMER eines
    // Zustands. Wer hier in der Mitte etwas einschiebt, macht aus jedem
    // gewaschenen Stein einen geschmolzenen.
    //
    // Die beiden hier sind das Ende der Kette. Sie sind mit Absicht teuer und
    // langsam: spaet im Spiel soll ein Stapel nicht breiter, sondern tiefer
    // werden - dasselbe Erz, aber viel mehr Arbeit und viel mehr Geld.
    Etched,  // geaetzt
    Fused,   // verschmolzen
    Count
};

// Was in der Anzeige steht ("Roh", "Gewaschen", ...).
const char* OreStateName(OreState state);

// Wie der Zustand in data/erze.json heisst ("raw", "washed", ...).
const char* OreStateKey(OreState state);

// Text zum Schluessel, -1 = kenne ich nicht.
int FindOreState(const std::string& key);

// Wie ein Block behandelt werden will, waehrend man ihn abbaut.
//
// Das haengt am ERZ und nicht am einzelnen Block: ein Diamant will immer
// dasselbe, egal welcher Block gerade dasteht. Entschieden wird es einmal, in
// dem Moment, in dem es das Erz zum ersten Mal gibt - danach steht es fest.
//
// Deshalb kann man es nicht am Block ablesen, sondern muss wissen, WELCHES Erz
// da steht. Im Programm heisst das: ein if je Erz.
//
//     if (block.is(Diamond)) block.mine(Heat);
//     else if (block.is(Gold)) block.mine(Cool);
//     else                     block.mine();
enum class BlockCare
{
    Plain,  // will gar nichts - einfach abbauen
    Cool,   // muss gekuehlt werden
    Heat,   // muss erhitzt werden
    Count
};

// Was in der Anzeige steht ("Cool", "Heat").
const char* BlockCareName(BlockCare care);

// Der Schluessel aus data/erze.json ("keine", "cool", "heat") als Behandlung.
// false = kenne ich nicht.
bool ParseBlockCare(const std::string& key, BlockCare& out);

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

    // Wie DIESES Erz abgebaut werden will. Fest, fuer jeden seiner Bloecke
    // gleich: gekuehlt, erhitzt oder gar nicht. Je wertvoller ein Erz, desto
    // wahrscheinlicher verlangt es etwas - gewuerfelt wird das aber nur EINMAL,
    // bei seiner Entstehung (siehe RollOreCare).
    BlockCare care = BlockCare::Plain;

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

// Alles zur Behandlung, aus data/erze.json. Hier stehen die Werte, die
// einspringen, wenn die Datei nichts dazu sagt.
struct CarePlan
{
    // Bis zu welchem Grundwert ein Erz nie etwas verlangt. Stein und Kohle
    // sollen einfach bleiben - die Mechanik kommt erst mit dem Wert.
    int fromValue = 8;

    // Wie wahrscheinlich eine Behandlung hoechstens wird, und bei welchem
    // Grundwert die Haelfte davon erreicht ist. Daraus wird eine Kurve, die
    // nie ganz bei 1 ankommt: auch das teuerste Erz laesst sich manchmal
    // einfach so abbauen.
    float chanceMax  = 0.8f;
    int   halfValue  = 45;

    // Wie viele Punkte Reinheit der Block verliert, wenn man nichts tut - und
    // wenn man das Falsche tut. Falsch ist teurer als gar nichts: kuehlen, was
    // erhitzt werden will, macht es schlimmer.
    //
    // Die Zeit bleibt davon unberuehrt: ein falsch abgebauter Block kommt genau
    // so schnell heraus wie sonst, er ist nur weniger wert. Das sieht man in
    // der Tasche - und mit Reinigen laesst es sich zum Teil wieder aufholen.
    int purityNone  = 25;
    int purityWrong = 50;
};

struct OrePlan
{
    std::vector<Ore> ores;

    CarePlan care;

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

// Wie wahrscheinlich verlangt ein Erz mit diesem Grundwert ueberhaupt eine
// Behandlung? 0..1. Daraus wird beim Entstehen EINMAL gewuerfelt.
float CareChance(const CarePlan& plan, int value);

// Wuerfelt die Behandlung fuer ein frisch entstandenes Erz. Nur der Wert
// entscheidet, wie wahrscheinlich es etwas verlangt - welche der beiden es dann
// wird, ist eine Muenze. Danach steht es fuer immer fest.
BlockCare RollOreCare(const CarePlan& plan, int value, std::mt19937& rng);

// Dasselbe ohne Wuerfel: aus dem Namen wird der Startwert. Fuer Erze, die aus
// einer Datei kommen und dort nichts zur Behandlung sagen, und fuer alte
// Spielstaende, in denen die Behandlung noch nicht drinsteht. Derselbe Name
// ergibt immer dieselbe Behandlung.
BlockCare DeriveOreCare(const CarePlan& plan, const std::string& name, int value);

// Was jeder Block dieses Erzes verlangt. Ausserhalb der Liste: Plain.
BlockCare OreCare(const OrePlan& plan, int ore);

// Wie viele Punkte Reinheit es kostet, wenn ein Block "verlangt" bekommt und
// man "getan" tut. 0 = richtig gemacht.
int CareLoss(const CarePlan& plan, BlockCare verlangt, BlockCare getan);

// Erz-Nummer zu einem Namen, -1 = kenne ich nicht. Gross- und Kleinschreibung
// ist egal: "stein" findet auch "Stein".
int FindOre(const OrePlan& plan, const std::string& name);

// Wie das Erz im Spielercode heisst: aus "White Gold" wird WhiteGold. Erze sind
// dort ein enum und kein Text - deshalb muss aus dem Namen ein gueltiger
// C++-Name werden. Zwei Erze bekommen nie denselben; entschieden wird nach der
// Reihenfolge in der Liste, das spaetere weicht aus.
//
// native.cpp baut daraus das enum, das Wiki zeigt denselben Namen an - beide
// muessen sich einig sein, deshalb steht es hier und nicht dort.
std::vector<std::string> OreCodeNames(const OrePlan& plan);
std::string              OreCodeName(const OrePlan& plan, int index);

// Perlin-artiges Rauschen, 0..1. Gleicher Startwert = gleiches Muster.
float OreNoise(float x, float y, unsigned seed);
