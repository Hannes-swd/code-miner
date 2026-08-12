#include "ore.h"
#include "datapath.h"

#include "json.h"


#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace
{

// Fuer den Vergleich von Namen: Gross- und Kleinschreibung soll egal sein.
std::string Lower(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
    return s;
}

}  // namespace

// "#RRGGBB" oder "#RGB". Kaputte Angaben bleiben grau, damit man es sieht.
bool ParseOreColor(const std::string& text, Color& out)
{
    std::string hex = text;
    if (!hex.empty() && hex[0] == '#')
        hex.erase(0, 1);

    auto digit = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };

    if (hex.size() == 3)
    {
        const int r = digit(hex[0]), g = digit(hex[1]), b = digit(hex[2]);
        if (r < 0 || g < 0 || b < 0)
            return false;
        out.r = (unsigned char)(r * 17);
        out.g = (unsigned char)(g * 17);
        out.b = (unsigned char)(b * 17);
        return true;
    }

    if (hex.size() != 6)
        return false;

    int teil[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i)
    {
        const int hi = digit(hex[(std::size_t)i * 2]);
        const int lo = digit(hex[(std::size_t)i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        teil[i] = hi * 16 + lo;
    }

    out.r = (unsigned char)teil[0];
    out.g = (unsigned char)teil[1];
    out.b = (unsigned char)teil[2];
    return true;
}

namespace
{

std::vector<std::string> Candidates()
{
    return DataPaths("erze.json");
}

// ---- Rauschen -------------------------------------------------------------

// Aus zwei Gitterpunkten und dem Startwert eine Zufallszahl 0..1 machen.
// Immer dieselbe fuer dieselbe Stelle - sonst wuerde das Muster flackern.
float Hash(int x, int y, unsigned seed)
{
    unsigned h = seed + 374761393u;
    h += (unsigned)x * 668265263u;
    h += (unsigned)y * 2246822519u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFFu) / (float)0xFFFFFF;
}

float Smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);  // weiche Kanten statt Karo-Muster
}

// Eine Lage: zwischen vier Gitterpunkten weich ueberblenden.
float Layer(float x, float y, unsigned seed)
{
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int   ix = (int)fx;
    const int   iy = (int)fy;

    const float tx = Smooth(x - fx);
    const float ty = Smooth(y - fy);

    const float a = Hash(ix, iy, seed);
    const float b = Hash(ix + 1, iy, seed);
    const float c = Hash(ix, iy + 1, seed);
    const float d = Hash(ix + 1, iy + 1, seed);

    const float oben  = a + (b - a) * tx;
    const float unten = c + (d - c) * tx;
    return oben + (unten - oben) * ty;
}

}  // namespace

float OreNoise(float x, float y, unsigned seed)
{
    // Drei Lagen uebereinander: die grobe macht die Adern, die feinen die
    // Koernung. Zusammen sieht es nach Gestein aus und nicht nach Wolken.
    float wert   = 0.0f;
    float staerke = 0.5f;
    float dichte = 1.0f;
    float summe  = 0.0f;

    for (int i = 0; i < 3; ++i)
    {
        wert += Layer(x * dichte, y * dichte, seed + (unsigned)i * 7919u) * staerke;
        summe += staerke;
        staerke *= 0.5f;
        dichte *= 2.0f;
    }

    return wert / summe;
}

OrePlan LoadOrePlan()
{
    OrePlan plan;

    std::ifstream in;
    for (const std::string& path : Candidates())
    {
        in.open(path.c_str(), std::ios::binary);
        if (in.is_open())
        {
            plan.file = path;
            break;
        }
        in.clear();
    }

    if (!in.is_open())
    {
        plan.problems.push_back("data/erze.json not found.");
        return plan;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    JsonValue   wurzel;
    std::string fehler;
    if (!ParseJson(buffer.str(), wurzel, fehler))
    {
        plan.problems.push_back("data/erze.json - " + fehler);
        return plan;
    }

    if (const JsonValue* level = wurzel.find("level"))
    {
        plan.levelFrom = level->text("art", plan.levelFrom.c_str());
        plan.perLevel  = (int)level->number("pro_level", plan.perLevel);
        if (plan.perLevel < 1)
            plan.perLevel = 1;
        if (plan.levelFrom != "bloecke" && plan.levelFrom != "skills")
        {
            plan.problems.push_back("level.art must be \"bloecke\" or \"skills\".");
            plan.levelFrom = "bloecke";
        }
    }

    if (const JsonValue* b = wurzel.find("behandlung"))
    {
        plan.care.fromValue    = (int)b->number("ab_wert", plan.care.fromValue);
        plan.care.chanceMax    = (float)b->number("chance_max", plan.care.chanceMax);
        plan.care.halfValue    = (int)b->number("wert_halb", plan.care.halfValue);
        plan.care.purityNone  = (int)b->number("verlust_ohne", plan.care.purityNone);
        plan.care.purityWrong = (int)b->number("verlust_falsch", plan.care.purityWrong);

        if (plan.care.chanceMax < 0.0f)
            plan.care.chanceMax = 0.0f;
        if (plan.care.chanceMax > 1.0f)
            plan.care.chanceMax = 1.0f;
        if (plan.care.halfValue < 1)
            plan.care.halfValue = 1;

        // Unter 0 waere die Strafe eine Belohnung - dann lohnte es sich, alles
        // falsch zu machen. Ueber 100 ist sinnlos: tiefer als 0 % geht nicht.
        if (plan.care.purityNone < 0)
            plan.care.purityNone = 0;
        if (plan.care.purityWrong < 0)
            plan.care.purityWrong = 0;
        if (plan.care.purityNone > 100)
            plan.care.purityNone = 100;
        if (plan.care.purityWrong > 100)
            plan.care.purityWrong = 100;
    }

    const JsonValue* liste = wurzel.find("erze");
    if (liste == nullptr || liste->type != JsonValue::Type::Array)
    {
        plan.problems.push_back("Missing the list \"erze\": [ ... ].");
        return plan;
    }

    for (const JsonValue& e : liste->items)
    {
        if (e.type != JsonValue::Type::Object)
            continue;

        Ore erz;
        erz.name        = e.text("name", "?");
        erz.rarity      = (float)e.number("seltenheit", 1.0);
        erz.mineSeconds = (float)e.number("abbaudauer", 0.5);
        erz.minLevel    = (int)e.number("ab_level", 1);
        erz.value       = (int)e.number("wert", 1);
        erz.pattern     = (float)e.number("muster", 5.0);
        erz.purity      = (int)e.number("reinheit", -1.0);

        if (erz.rarity < 0.01f)
            erz.rarity = 0.01f;
        if (erz.mineSeconds < 0.05f)
            erz.mineSeconds = 0.05f;
        if (erz.value < 1)
            erz.value = 1;
        if (erz.pattern < 0.5f)
            erz.pattern = 0.5f;
        if (erz.purity > 100)
            erz.purity = 100;

        if (!ParseOreColor(e.text("farbe1", ""), erz.color1))
            plan.problems.push_back(erz.name + ": farbe1 is missing or is not #RRGGBB.");
        if (!ParseOreColor(e.text("farbe2", ""), erz.color2))
            plan.problems.push_back(erz.name + ": farbe2 is missing or is not #RRGGBB.");

        // Welche Zustaende es bei diesem Erz geben darf. Fehlt die Liste, sind
        // alle erlaubt - so muss man nicht bei jedem Erz alles hinschreiben.
        if (const JsonValue* z = e.find("zustaende"))
        {
            erz.states = 0;
            if (z->type != JsonValue::Type::Array)
            {
                plan.problems.push_back(erz.name + ": \"zustaende\" must be a list.");
                erz.states = 0xFFFFFFFFu;
            }
            else
            {
                for (const JsonValue& s : z->items)
                {
                    const int nummer = FindOreState(s.str);
                    if (nummer < 0)
                        plan.problems.push_back(erz.name + ": state \"" + s.str +
                                                "\" is not something I know.");
                    else
                        erz.states |= (1u << (unsigned)nummer);
                }
            }

            // Roh geht immer - so kommt der Block ja aus dem Boden.
            erz.states |= (1u << (unsigned)OreState::Raw);
        }

        // Womit sich das Erz legieren laesst. Geprueft wird das erst in
        // data/legierungen.json - dort steht, welche Rezepte es gibt.
        if (const JsonValue* l = e.find("legierbar_mit"))
        {
            if (l->type != JsonValue::Type::Array)
                plan.problems.push_back(erz.name + ": \"legierbar_mit\" must be a list.");
            else
                for (const JsonValue& s : l->items)
                    if (s.type == JsonValue::Type::String)
                        erz.alloyWith.push_back(s.str);
        }

        plan.ores.push_back(erz);
    }

    if (plan.ores.empty())
        plan.problems.push_back("In \"erze\" there is not a single ore.");

    return plan;
}

const char* OreStateName(OreState state)
{
    switch (state)
    {
    case OreState::Raw: return "Raw";
    case OreState::Washed: return "Washed";
    case OreState::Smelted: return "Smelted";
    case OreState::Cast: return "Cast";
    case OreState::Polished: return "Polished";
    case OreState::Hardened: return "Hardened";
    case OreState::Refined: return "Refined";
    case OreState::Pressed: return "Pressed";
    case OreState::Cleaned: return "Cleaned";
    case OreState::Oxidized: return "Oxidized";
    case OreState::Alloy: return "Alloyed";
    default: return "?";
    }
}

const char* OreStateKey(OreState state)
{
    switch (state)
    {
    case OreState::Raw: return "raw";
    case OreState::Washed: return "washed";
    case OreState::Smelted: return "smelted";
    case OreState::Cast: return "cast";
    case OreState::Polished: return "polished";
    case OreState::Hardened: return "hardened";
    case OreState::Refined: return "refined";
    case OreState::Pressed: return "pressed";
    case OreState::Cleaned: return "cleaned";
    case OreState::Oxidized: return "oxidized";
    case OreState::Alloy: return "alloy";
    default: return "?";
    }
}

int FindOreState(const std::string& key)
{
    for (int i = 0; i < (int)OreState::Count; ++i)
        if (key == OreStateKey((OreState)i))
            return i;
    return -1;
}

bool Ore::alloyableWith(const std::string& andere) const
{
    const std::string gesucht = Lower(andere);
    for (const std::string& w : alloyWith)
        if (Lower(w) == gesucht)
            return true;
    return false;
}

int FindOre(const OrePlan& plan, const std::string& name)
{
    const std::string gesucht = Lower(name);
    for (std::size_t i = 0; i < plan.ores.size(); ++i)
        if (Lower(plan.ores[i].name) == gesucht)
            return (int)i;

    return -1;
}

namespace
{

// Alles wegwerfen, was in einem C++-Namen nichts zu suchen hat.
std::string Ident(const std::string& name)
{
    std::string out;
    for (const char c : name)
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
            out.push_back(c);

    // Mit einer Ziffer darf kein Name anfangen - und leer schon gar nicht.
    if (out.empty() || (out[0] >= '0' && out[0] <= '9'))
        out.insert(out.begin(), '_');

    return out;
}

// Namen, die es im erzeugten Header schon gibt oder die C++ fuer sich behaelt.
bool IsTakenIdent(const std::string& w)
{
    static const char* const taken[] = {
        "Any",   "Ore",    "CkOreName", "CkItem", "CkBlock", "CkShared", "CkSharedValue",
        "item",  "block",  "shared",    "print",  "main",    "ck",
        "int",   "float",  "double",    "bool",   "char",    "void",     "auto",
        "class", "struct", "if",        "else",   "while",   "for",      "do",
        "true",  "false",  "return",    "const",  "static",  "namespace"};

    for (const char* t : taken)
        if (w == t)
            return true;
    return false;
}

}  // namespace

std::vector<std::string> OreCodeNames(const OrePlan& plan)
{
    std::vector<std::string> out;
    out.reserve(plan.ores.size());

    for (const Ore& o : plan.ores)
    {
        const std::string roh = Ident(o.name);
        std::string       id  = IsTakenIdent(roh) ? (roh + "_") : roh;

        // Schon vergeben? Dann haengt eine Nummer dran. Verglichen wird nur mit
        // den Erzen davor - so bleibt der Name eines Erzes derselbe, auch wenn
        // spaeter eines mit aehnlichem Namen dazukommt.
        int versuch = 2;
        while (std::find(out.begin(), out.end(), id) != out.end())
            id = roh + std::to_string(versuch++);

        out.push_back(id);
    }

    return out;
}

std::string OreCodeName(const OrePlan& plan, int index)
{
    if (index < 0 || index >= (int)plan.ores.size())
        return "Any";

    return OreCodeNames(plan)[(std::size_t)index];
}

const char* BlockCareName(BlockCare care)
{
    switch (care)
    {
    case BlockCare::Cool: return "Cool";
    case BlockCare::Heat: return "Heat";
    default: return "Plain";
    }
}

float CareChance(const OrePlan& plan, int ore)
{
    if (ore < 0 || ore >= (int)plan.ores.size())
        return 0.0f;

    const int wert = plan.ores[(std::size_t)ore].value;
    if (wert <= plan.care.fromValue)
        return 0.0f;

    // Eine Saettigungskurve: ueber fromValue steigt sie schnell, kommt aber nie
    // ganz bei chanceMax an. Auch das teuerste Erz steht deshalb ab und zu
    // einfach so da - sonst waere die Behandlung keine Frage mehr, sondern nur
    // noch eine zweite Pflichtzeile.
    const float ueber = (float)(wert - plan.care.fromValue);
    const float halb  = (plan.care.halfValue > 0) ? (float)plan.care.halfValue : 1.0f;

    return plan.care.chanceMax * (ueber / (ueber + halb));
}

BlockCare RollCare(const OrePlan& plan, int ore, std::mt19937& rng)
{
    const float chance = CareChance(plan, ore);
    if (chance <= 0.0f)
        return BlockCare::Plain;

    const float wurf = (float)(rng() % 1000000u) / 1000000.0f;
    if (wurf >= chance)
        return BlockCare::Plain;

    // Welche der beiden Behandlungen es wird, ist reiner Zufall - man kann es
    // also nicht am Erz ablesen, sondern muss den Block fragen.
    return ((rng() & 1u) != 0u) ? BlockCare::Cool : BlockCare::Heat;
}

int CareLoss(const CarePlan& plan, BlockCare verlangt, BlockCare getan)
{
    if (verlangt == getan)
        return 0;  // genau richtig - auch "beide nichts" zaehlt hier

    // Behandelt, obwohl der Block nichts wollte: das stoert. Deshalb steht die
    // Abfrage im Programm nicht umsonst da.
    if (verlangt == BlockCare::Plain)
        return plan.purityWrong;

    // Der Block wollte etwas und bekam nichts.
    if (getan == BlockCare::Plain)
        return plan.purityNone;

    // Er wollte etwas und bekam das Gegenteil.
    return plan.purityWrong;
}

int RollOre(const OrePlan& plan, int level, std::mt19937& rng)
{
    // Gewicht = 1 / Seltenheit. Was das Level noch nicht hergibt, faellt raus -
    // und was gar nicht im Boden vorkommt (Legierungen) sowieso.
    double summe = 0.0;
    for (const Ore& o : plan.ores)
        if (o.minable && o.minLevel <= level)
            summe += 1.0 / (double)o.rarity;

    if (summe <= 0.0)
        return 0;  // zur Not das erste Erz - meistens Stein

    double wurf = (double)(rng() % 1000000u) / 1000000.0 * summe;

    for (std::size_t i = 0; i < plan.ores.size(); ++i)
    {
        if (!plan.ores[i].minable || plan.ores[i].minLevel > level)
            continue;

        wurf -= 1.0 / (double)plan.ores[i].rarity;
        if (wurf <= 0.0)
            return (int)i;
    }

    return 0;
}
