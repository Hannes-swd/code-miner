#include "oregen.h"

#include "alloy.h"
#include "json.h"
#include "ore.h"

#include <windows.h>

#include <cmath>
#include <fstream>
#include <random>
#include <sstream>

namespace
{

std::vector<std::string> Candidates()
{
    std::vector<std::string> out;

    char        exe[MAX_PATH] = {0};
    const DWORD len           = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (len > 0)
    {
        std::string       dir(exe, len);
        const std::size_t cut = dir.find_last_of("\\/");
        if (cut != std::string::npos)
            dir.resize(cut);

        out.push_back(dir + "\\..\\..\\data\\erzgenerator.json");  // build/Debug -> Projekt
        out.push_back(dir + "\\..\\..\\..\\data\\erzgenerator.json");
        out.push_back(dir + "\\..\\data\\erzgenerator.json");
        out.push_back(dir + "\\data\\erzgenerator.json");
        out.push_back(dir + "\\erzgenerator.json");
    }

    out.push_back("data/erzgenerator.json");
    out.push_back("erzgenerator.json");
    return out;
}

// Eine Liste von Woertern aus einem JSON-Feld holen.
std::vector<std::string> Words(const JsonValue* v)
{
    std::vector<std::string> out;
    if (v == nullptr || v->type != JsonValue::Type::Array)
        return out;

    for (const JsonValue& e : v->items)
        if (e.type == JsonValue::Type::String && !e.str.empty())
            out.push_back(e.str);

    return out;
}

// Ein Zahlenpaar ("von, bis") aus einer Liste mit zwei Eintraegen.
void Range(const JsonValue* v, float& lo, float& hi)
{
    if (v == nullptr || v->type != JsonValue::Type::Array || v->items.size() < 2)
        return;
    lo = (float)v->items[0].num;
    hi = (float)v->items[1].num;
    if (hi < lo)
        std::swap(lo, hi);
}

// ---- Wuerfel ---------------------------------------------------------------
//
// Ein eigener kleiner Satz Helfer, damit ueberall derselbe Wuerfel benutzt
// wird - sonst haengt das Ergebnis davon ab, in welcher Reihenfolge jemand
// die Zufallszahlen zieht.

float Roll(std::mt19937& rng, float lo, float hi)
{
    if (hi <= lo)
        return lo;
    const float t = (float)(rng() % 100000u) / 99999.0f;
    return lo + (hi - lo) * t;
}

int RollInt(std::mt19937& rng, int lo, int hi)
{
    if (hi <= lo)
        return lo;
    return lo + (int)(rng() % (unsigned)(hi - lo + 1));
}

const std::string& Pick(std::mt19937& rng, const std::vector<std::string>& list)
{
    static const std::string leer;
    if (list.empty())
        return leer;
    return list[rng() % list.size()];
}

// ---- Farben ----------------------------------------------------------------

// Farbton (0-360), Sattheit und Helligkeit (0-1) in RGB. Beide Farben eines
// Erzes bekommen denselben Farbton und unterscheiden sich nur in der
// Helligkeit - deshalb koennen sie gar nicht zueinander passen wollen und es
// dann doch nicht tun.
Color FromHsv(float h, float s, float v)
{
    while (h < 0.0f)
        h += 360.0f;
    while (h >= 360.0f)
        h -= 360.0f;

    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 60.0f)       { r = c; g = x; }
    else if (h < 120.0f) { r = x; g = c; }
    else if (h < 180.0f) { g = c; b = x; }
    else if (h < 240.0f) { g = x; b = c; }
    else if (h < 300.0f) { r = x; b = c; }
    else                 { r = c; b = x; }

    Color out;
    out.r = (unsigned char)((r + m) * 255.0f);
    out.g = (unsigned char)((g + m) * 255.0f);
    out.b = (unsigned char)((b + m) * 255.0f);
    return out;
}

Color MixColor(const Color& a, const Color& b, float t)
{
    Color out;
    out.r = (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * t);
    out.g = (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * t);
    out.b = (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * t);
    return out;
}

// ---- Namen -----------------------------------------------------------------

// Bauplan:  vorne [mitte] KERN [hinten]  - also 2 bis 4 Woerter.
//
// Weil das Kernwort aus der Art kommt, heisst ein Metall immer wie ein Metall
// und ein Kristall wie ein Kristall. Und weil nur "vorne" gross geschrieben
// ist, kommt hinten ein ganz normales zusammengesetztes Wort heraus.
std::string MakeName(std::mt19937& rng, const OreGenPlan& gen,
                     const std::vector<std::string>& cores)
{
    std::vector<std::string> teile;
    teile.push_back(Pick(rng, gen.front));

    if (!gen.middle.empty() && (rng() % 100u) < 30u)
        teile.push_back(Pick(rng, gen.middle));

    teile.push_back(Pick(rng, cores));

    if (!gen.back.empty() && (rng() % 100u) < 45u)
        teile.push_back(Pick(rng, gen.back));

    // Jedes Wort gross, mit Leerzeichen dazwischen: "Ember Quartz Grain".
    std::string name;
    for (const std::string& w : teile)
    {
        if (w.empty())
            continue;
        if (!name.empty())
            name += ' ';

        std::string teil = w;
        if (teil[0] >= 'a' && teil[0] <= 'z')
            teil[0] = (char)(teil[0] - 'a' + 'A');
        name += teil;
    }

    return name;
}

// Denselben Namen zweimal darf es nicht geben: der Spieler ruft ein Erz ueber
// seinen Namen auf, item.sell("Glutquarz") wuerde sonst das falsche treffen.
std::string UniqueName(std::mt19937& rng, const OreGenPlan& gen,
                       const std::vector<std::string>& cores, const OrePlan& ores)
{
    for (int versuch = 0; versuch < 60; ++versuch)
    {
        const std::string name = MakeName(rng, gen, cores);
        if (!name.empty() && FindOre(ores, name) < 0)
            return name;
    }
    return std::string();  // aufgegeben - der Aufrufer laesst das Erz dann aus
}

// Zustands-Schluessel ("washed") in Bits umrechnen. "raw" kommt immer dazu:
// so kommt der Block ja aus dem Boden.
unsigned StatesOf(const std::vector<std::string>& keys, const std::string& wer,
                  std::vector<std::string>& problems)
{
    unsigned bits = 1u << (unsigned)OreState::Raw;

    for (const std::string& k : keys)
    {
        const int nummer = FindOreState(k);
        if (nummer < 0)
            problems.push_back("data/erzgenerator.json - " + wer + ": state \"" + k +
                               "\" is not something I know.");
        else
            bits |= (1u << (unsigned)nummer);
    }

    return bits;
}

}  // namespace

OreGenPlan LoadOreGenPlan()
{
    OreGenPlan gen;

    std::ifstream in;
    for (const std::string& path : Candidates())
    {
        in.open(path.c_str(), std::ios::binary);
        if (in.is_open())
        {
            gen.file = path;
            break;
        }
        in.clear();
    }

    // Ohne Datei wuerfelt das Spiel eben nicht - das ist kein Fehler, sondern
    // einfach eine Welt aus lauter handgeschriebenen Erzen.
    if (!in.is_open())
        return gen;

    std::ostringstream buffer;
    buffer << in.rdbuf();

    JsonValue   wurzel;
    std::string fehler;
    if (!ParseJson(buffer.str(), wurzel, fehler))
    {
        gen.problems.push_back("data/erzgenerator.json - " + fehler);
        return gen;
    }

    gen.seed     = (unsigned)wurzel.number("startwert", 1.0);
    gen.minLevel = (int)wurzel.number("ab_level", 4.0);
    gen.perLevel = (int)wurzel.number("je_level", 3.0);

    if (gen.minLevel < 1)
        gen.minLevel = 1;
    if (gen.perLevel < 1)
        gen.perLevel = 1;

    if (const JsonValue* w = wurzel.find("woerter"))
    {
        gen.front  = Words(w->find("vorne"));
        gen.middle = Words(w->find("mitte"));
        gen.back   = Words(w->find("hinten"));
    }

    if (const JsonValue* s = wurzel.find("seltenheit"))
        gen.raritySpread = (float)s->number("streuung", gen.raritySpread);

    if (gen.raritySpread < 0.0f)
        gen.raritySpread = 0.0f;
    if (gen.raritySpread > 0.8f)
        gen.raritySpread = 0.8f;

    if (const JsonValue* v = wurzel.find("wert"))
    {
        gen.valueBase    = (float)v->number("grund", gen.valueBase);
        gen.valueSlope   = (float)v->number("steigung", gen.valueSlope);
        gen.valueScatter = (float)v->number("streuung", gen.valueScatter);
    }

    // Die Streuung darf die Reihenfolge nicht umwerfen: sonst waere doch wieder
    // ein haeufiges Erz teurer als ein seltenes, und genau das soll nicht sein.
    if (gen.valueScatter < 0.0f)
        gen.valueScatter = 0.0f;
    if (gen.valueScatter > 0.35f)
    {
        gen.problems.push_back(
            "data/erzgenerator.json - wert.streuung above 0.35 would be too much: then a "
            "common ore could get more expensive than a rare one. Capped at 0.35.");
        gen.valueScatter = 0.35f;
    }

    if (const JsonValue* d = wurzel.find("abbaudauer"))
    {
        gen.mineBase  = (float)d->number("grund", gen.mineBase);
        gen.mineSlope = (float)d->number("steigung", gen.mineSlope);
    }

    if (const JsonValue* l = wurzel.find("level"))
    {
        gen.levelSlope = (float)l->number("steigung", gen.levelSlope);
        gen.levelPer   = (float)l->number("je", gen.levelPer);
        if (gen.levelPer < 0.1f)
            gen.levelPer = 0.1f;
    }

    if (const JsonValue* a = wurzel.find("legierung"))
    {
        gen.alloyShare = (float)a->number("anteil", gen.alloyShare);
        gen.alloyCores = Words(a->find("kern"));
        Range(a->find("dauer"), gen.alloySecFrom, gen.alloySecTo);

        float pl = (float)gen.alloyPurFrom, ph = (float)gen.alloyPurTo;
        Range(a->find("reinheit"), pl, ph);
        gen.alloyPurFrom = (int)pl;
        gen.alloyPurTo   = (int)ph;

        Range(a->find("wertfaktor"), gen.alloyValFrom, gen.alloyValTo);
    }

    if (const JsonValue* liste = wurzel.find("arten"))
    {
        for (const JsonValue& e : liste->items)
        {
            if (e.type != JsonValue::Type::Object)
                continue;

            OreGenKind art;
            art.name   = e.text("name", "?");
            art.weight = (int)e.number("gewicht", 1.0);
            art.cores  = Words(e.find("kern"));
            art.states = Words(e.find("zustaende"));

            Range(e.find("farbton"), art.hueFrom, art.hueTo);
            Range(e.find("sattheit"), art.satFrom, art.satTo);
            Range(e.find("muster"), art.patFrom, art.patTo);

            float pl = (float)art.purFrom, ph = (float)art.purTo;
            Range(e.find("reinheit"), pl, ph);
            art.purFrom = (int)pl;
            art.purTo   = (int)ph;

            if (const JsonValue* leg = e.find("legieren"))
                art.alloyable = leg->flag;

            if (art.weight < 1)
                art.weight = 1;
            if (art.cores.empty())
            {
                gen.problems.push_back("data/erzgenerator.json - kind \"" + art.name +
                                       "\" has no core words, it drops out.");
                continue;
            }

            gen.kinds.push_back(art);
        }
    }

    if (gen.front.empty())
        gen.problems.push_back(
            "data/erzgenerator.json - woerter.vorne is empty, without it there are no names.");

    if (gen.kinds.empty())
        gen.problems.push_back("data/erzgenerator.json - there is not a single usable kind.");

    gen.active = (!gen.kinds.empty() && !gen.front.empty());
    return gen;
}

int NextRollLevel(const OreGenPlan& gen, int rolled)
{
    return gen.minLevel + gen.perLevel * rolled;
}

namespace
{

// Ein einziges Erz wuerfeln, passend zu dem Level, in dem es entsteht.
//
// Rueckwaerts gerechnet: die Erze aus data/erze.json folgen der Kurve
// level = 1 + seltenheit^steigung / je. Nach der Seltenheit aufgeloest ergibt
// das die Seltenheit, die zu einem Level gehoert - und weil das Level endlos
// weiter steigt, tut es die Seltenheit auch. Level 40 bringt so etwas, das
// weit jenseits von Diamant liegt.
bool RollOneOre(OrePlan& ores, AlloyPlan& alloys, OreGenPlan& gen, int level, std::mt19937& rng)
{
    int gewichtSumme = 0;
    for (const OreGenKind& k : gen.kinds)
        gewichtSumme += k.weight;
    if (gewichtSumme <= 0)
        return false;

    int roll = RollInt(rng, 0, gewichtSumme - 1);
    const OreGenKind* art = &gen.kinds[0];
    for (const OreGenKind& k : gen.kinds)
    {
        roll -= k.weight;
        if (roll < 0)
        {
            art = &k;
            break;
        }
    }

    const std::string name = UniqueName(rng, gen, art->cores, ores);
    if (name.empty())
    {
        gen.problems.push_back(
            "data/erzgenerator.json - no free name left to find. Put more words into the lists "
            "and it will carry on.");
        return false;
    }

    // DIE eine gewuerfelte Zahl. Alles andere haengt daran.
    float stufe = (float)(level - gen.minLevel + 1);
    if (stufe < 1.0f)
        stufe = 1.0f;

    float selten = std::pow(gen.levelPer * stufe, 1.0f / gen.levelSlope);
    selten *= Roll(rng, 1.0f - gen.raritySpread, 1.0f + gen.raritySpread);
    if (selten < 1.0f)
        selten = 1.0f;

    {
        Ore o;
        o.name   = name;
        o.rarity = selten;

        // Wert aus der Seltenheit - dieselbe Kurve wie bei den Erzen aus
        // data/erze.json. Die Streuung ist klein genug, dass sie die
        // Reihenfolge nicht umwirft.
        const float streu = Roll(rng, 1.0f - gen.valueScatter, 1.0f + gen.valueScatter);
        o.value = (int)(gen.valueBase * std::pow(selten, gen.valueSlope) * streu + 0.5f);
        if (o.value < 1)
            o.value = 1;

        o.mineSeconds = gen.mineBase * std::pow(selten, gen.mineSlope);
        if (o.mineSeconds < 0.05f)
            o.mineSeconds = 0.05f;

        // Es liegt ab genau dem Level im Boden, in dem es entstanden ist -
        // sonst wuerfelte das Spiel etwas aus, das der Spieler erst zwanzig
        // Level spaeter zu sehen bekaeme.
        o.minLevel = level;
        if (o.minLevel < 1)
            o.minLevel = 1;

        o.purity  = RollInt(rng, art->purFrom, art->purTo);
        o.pattern = Roll(rng, art->patFrom, art->patTo);
        if (o.pattern < 0.5f)
            o.pattern = 0.5f;

        // Ein Farbton fuer beide Farben: die helle Ader ist derselbe Ton,
        // nur heller und blasser als der dunkle Grund.
        const float ton  = Roll(rng, art->hueFrom, art->hueTo);
        const float satt = Roll(rng, art->satFrom, art->satTo);
        o.color1 = FromHsv(ton, satt * 0.75f, Roll(rng, 0.72f, 0.92f));
        o.color2 = FromHsv(ton, satt, Roll(rng, 0.16f, 0.30f));

        o.states  = StatesOf(art->states, name, gen.problems);
        o.minable = true;

        ores.ores.push_back(o);
        ++ores.rolled;
    }

    const int neu = (int)ores.ores.size() - 1;

    // ---- Und vielleicht noch eine Legierung dazu ---------------------------
    //
    // Ein Partner muss geschmolzen UND legiert werden koennen - sonst greift
    // das Rezept nie. Damit kommen nur Metalle in Frage, aus data/erze.json
    // genauso wie gewuerfelte.
    auto taugtAlsPartner = [&](int idx)
    {
        const Ore& o = ores.ores[(std::size_t)idx];
        return o.minable && o.allows(OreState::Smelted) && o.allows(OreState::Alloy);
    };

    if (art->alloyable && !gen.alloyCores.empty() && Roll(rng, 0.0f, 1.0f) <= gen.alloyShare &&
        taugtAlsPartner(neu))
    {
        // Alle moeglichen Partner einsammeln und einen ziehen.
        std::vector<int> partner;
        for (int i = 0; i < (int)ores.ores.size(); ++i)
            if (i != neu && taugtAlsPartner(i))
                partner.push_back(i);

        if (partner.empty())
            return true;

        const int p = partner[rng() % partner.size()];

        Ore& a = ores.ores[(std::size_t)neu];
        Ore& b = ores.ores[(std::size_t)p];

        const std::string legName = UniqueName(rng, gen, gen.alloyCores, ores);
        if (legName.empty())
            return true;

        // Beide muessen einander kennen - sonst faellt das Rezept durch die
        // Probe, die data/legierungen.json auch bestehen muss.
        a.alloyWith.push_back(b.name);
        b.alloyWith.push_back(a.name);

        AlloyRecipe r;
        r.name    = legName;
        r.from    = 1u << (unsigned)OreState::Smelted;
        r.to      = (int)OreState::Alloy;
        r.seconds = Roll(rng, gen.alloySecFrom, gen.alloySecTo);
        r.purity  = RollInt(rng, gen.alloyPurFrom, gen.alloyPurTo);

        AlloyPart pa;
        pa.ore   = neu;
        pa.count = RollInt(rng, 1, 2);
        AlloyPart pb;
        pb.ore   = p;
        pb.count = RollInt(rng, 1, 2);
        r.parts.push_back(pa);
        r.parts.push_back(pb);

        // Das Ergebnis ist mehr wert als seine Teile - sonst wuerde niemand
        // legieren.
        Ore erg;
        erg.name    = legName;
        erg.minable = false;
        erg.purity  = -1;  // kommt nie aus dem Boden

        const float teile = (float)(a.value * pa.count + b.value * pb.count);
        erg.value = (int)(teile * Roll(rng, gen.alloyValFrom, gen.alloyValTo) + 0.5f);
        if (erg.value < 1)
            erg.value = 1;

        erg.pattern = Roll(rng, 3.0f, 7.0f);
        erg.color1  = MixColor(a.color1, b.color1, 0.5f);
        erg.color2  = MixColor(a.color2, b.color2, 0.5f);

        // Weiterverarbeiten geht mit dem, was BEIDE Zutaten koennen - was nur
        // eine von beiden kann, kann die Mischung schliesslich nicht.
        erg.states = (a.states & b.states) | (1u << (unsigned)OreState::Alloy);

        r.result = (int)ores.ores.size();
        ores.ores.push_back(erg);

        alloys.recipes.push_back(r);
    }

    return true;
}

}  // namespace

int RollNewOres(OrePlan& ores, AlloyPlan& alloys, OreGenPlan& gen, int level)
{
    if (!gen.active || ores.ores.empty())
        return 0;

    int gemacht = 0;

    // Eine Schleife, kein "if": wer den Spielstand aus einer Datei laedt oder
    // mehrere Level auf einmal ueberspringt, soll alles bekommen, was ihm
    // zusteht - und nicht eines pro Bild.
    while (level >= NextRollLevel(gen, ores.rolled))
    {
        // Gewuerfelt wird mit dem Level, in dem das Erz FAELLIG war, nicht mit
        // dem, in dem man gerade steht. Sonst kaeme beim Nachholen ein ganzer
        // Stapel Spitzenerze auf einmal - und der Anfang der Reihe fehlte fuer
        // immer.
        const int faellig = NextRollLevel(gen, ores.rolled);

        // Jedes Erz bekommt seinen eigenen Wuerfel, abgeleitet aus dem
        // Startwert und seiner laufenden Nummer. Dadurch haengt es nicht davon
        // ab, wie oft vorher gewuerfelt wurde - und mit demselben Startwert
        // kommt in einem neuen Spiel dieselbe Reihe heraus.
        std::mt19937 rng(gen.seed + (unsigned)ores.rolled * 2654435761u);

        const int vorher = ores.rolled;
        if (!RollOneOre(ores, alloys, gen, faellig, rng))
            break;

        // Ist trotz allem nichts entstanden, waere die Schleife endlos.
        if (ores.rolled == vorher)
            break;

        ++gemacht;
    }

    return gemacht;
}
