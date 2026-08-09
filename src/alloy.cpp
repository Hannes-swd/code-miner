#include "alloy.h"

#include "json.h"
#include "ore.h"

#include <windows.h>

#include <fstream>
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

        out.push_back(dir + "\\..\\..\\data\\legierungen.json");  // build/Debug -> Projekt
        out.push_back(dir + "\\..\\..\\..\\data\\legierungen.json");
        out.push_back(dir + "\\..\\data\\legierungen.json");
        out.push_back(dir + "\\data\\legierungen.json");
        out.push_back(dir + "\\legierungen.json");
    }

    out.push_back("data/legierungen.json");
    out.push_back("legierungen.json");
    return out;
}

// Liest die Zutaten. Rueckgabe false = das Rezept taugt nichts, dann steht der
// Grund schon in problems.
bool ReadParts(const JsonValue& e, const OrePlan& ores, AlloyRecipe& r,
               std::vector<std::string>& problems)
{
    const JsonValue* liste = e.find("aus");
    if (liste == nullptr || liste->type != JsonValue::Type::Array)
    {
        problems.push_back(r.name + ": \"aus\" muss eine Liste von Zutaten sein.");
        return false;
    }

    for (const JsonValue& z : liste->items)
    {
        if (z.type != JsonValue::Type::Object)
            continue;

        AlloyPart teil;
        const std::string name = z.text("erz", "");
        teil.ore               = FindOre(ores, name);
        teil.count             = (int)z.number("anzahl", 1.0);

        if (teil.ore < 0)
        {
            problems.push_back(r.name + ": the ore \"" + name + "\" is not something I know.");
            return false;
        }
        if (teil.count < 1)
            teil.count = 1;

        // Zweimal dasselbe Erz waere kein Legieren, sondern ein Haufen.
        for (const AlloyPart& da : r.parts)
            if (da.ore == teil.ore)
            {
                problems.push_back(r.name + ": \"" + name + "\" appears twice in \"aus\".");
                return false;
            }

        r.parts.push_back(teil);
    }

    if (r.parts.size() < 2)
    {
        problems.push_back(r.name + ": in \"aus\" needs at least two different ores.");
        return false;
    }

    return true;
}

// Die Probe gegen data/erze.json: was sich nicht gegenseitig in
// "legierbar_mit" stehen hat, gehoert nicht in einen Tiegel. Faellt das auf,
// widersprechen sich die beiden Dateien - und genau das soll man sofort sehen.
bool PartsMatch(const OrePlan& ores, const AlloyRecipe& r, std::vector<std::string>& problems)
{
    bool ok = true;

    for (std::size_t i = 0; i < r.parts.size(); ++i)
        for (std::size_t j = i + 1; j < r.parts.size(); ++j)
        {
            const Ore& a = ores.ores[(std::size_t)r.parts[i].ore];
            const Ore& b = ores.ores[(std::size_t)r.parts[j].ore];

            if (!a.alloyableWith(b.name))
            {
                problems.push_back(r.name + ": bei " + a.name + " fehlt \"" + b.name +
                                   "\" in legierbar_mit.");
                ok = false;
            }
            if (!b.alloyableWith(a.name))
            {
                problems.push_back(r.name + ": bei " + b.name + " fehlt \"" + a.name +
                                   "\" in legierbar_mit.");
                ok = false;
            }
        }

    return ok;
}

// Das Ergebnis als Erz: eigener Name, eigene Farben, eigener Wert - aber nie
// im Boden.
int AddResultOre(const JsonValue& e, OrePlan& ores, const AlloyRecipe& r,
                 std::vector<std::string>& problems)
{
    Ore erg;
    erg.name    = r.name;
    erg.value   = (int)e.number("wert", 1.0);
    erg.pattern = (float)e.number("muster", 5.0);
    erg.minable = false;
    erg.purity  = -1;  // kommt nie aus dem Boden, also gibt es keine Startreinheit

    if (erg.value < 1)
        erg.value = 1;
    if (erg.pattern < 0.5f)
        erg.pattern = 0.5f;

    if (!ParseOreColor(e.text("farbe1", ""), erg.color1))
        problems.push_back(r.name + ": farbe1 is missing or is not #RRGGBB.");
    if (!ParseOreColor(e.text("farbe2", ""), erg.color2))
        problems.push_back(r.name + ": farbe2 is missing or is not #RRGGBB.");

    if (const JsonValue* z = e.find("zustaende"))
    {
        erg.states = 0;
        if (z->type != JsonValue::Type::Array)
        {
            problems.push_back(r.name + ": \"zustaende\" must be a list.");
            erg.states = 0xFFFFFFFFu;
        }
        else
        {
            for (const JsonValue& s : z->items)
            {
                const int nummer = FindOreState(s.str);
                if (nummer < 0)
                    problems.push_back(r.name + ": state \"" + s.str + "\" is not something I know.");
                else
                    erg.states |= (1u << (unsigned)nummer);
            }
        }

        // Der Zustand aus "zu" muss dabei sein - sonst koennte das Ergebnis
        // gar nicht entstehen.
        if ((erg.states & (1u << (unsigned)r.to)) == 0)
        {
            problems.push_back(r.name + ": \"" + OreStateKey((OreState)r.to) +
                               "\" fehlte in \"zustaende\" - ich habe es dazugenommen.");
            erg.states |= (1u << (unsigned)r.to);
        }
    }

    if (const JsonValue* l = e.find("legierbar_mit"))
    {
        if (l->type != JsonValue::Type::Array)
            problems.push_back(r.name + ": \"legierbar_mit\" must be a list.");
        else
            for (const JsonValue& s : l->items)
                if (s.type == JsonValue::Type::String)
                    erg.alloyWith.push_back(s.str);
    }

    ores.ores.push_back(erg);
    return (int)ores.ores.size() - 1;
}

}  // namespace

const AlloyRecipe* AlloyPlan::find(const std::string& name) const
{
    auto klein = [](std::string s)
    {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 'a');
        return s;
    };

    const std::string gesucht = klein(name);
    for (const AlloyRecipe& r : recipes)
        if (klein(r.name) == gesucht)
            return &r;
    return nullptr;
}

AlloyPlan LoadAlloyPlan(OrePlan& ores)
{
    AlloyPlan plan;

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

    // Ohne Datei gibt es eben keine Legierungen. Das Spiel laeuft trotzdem.
    if (!in.is_open())
    {
        plan.problems.push_back("data/legierungen.json not found.");
        return plan;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    JsonValue   wurzel;
    std::string fehler;
    if (!ParseJson(buffer.str(), wurzel, fehler))
    {
        plan.problems.push_back("data/legierungen.json - " + fehler);
        return plan;
    }

    const JsonValue* liste = wurzel.find("legierungen");
    if (liste == nullptr || liste->type != JsonValue::Type::Array)
    {
        plan.problems.push_back("Missing the list \"legierungen\": [ ... ].");
        return plan;
    }

    for (const JsonValue& e : liste->items)
    {
        if (e.type != JsonValue::Type::Object)
            continue;

        AlloyRecipe r;
        r.name    = e.text("name", "?");
        r.seconds = (float)e.number("dauer", 2.0);
        r.purity  = (int)e.number("reinheit", 0.0);

        if (r.seconds < 0.05f)
            r.seconds = 0.05f;

        if (FindOre(ores, r.name) >= 0)
        {
            plan.problems.push_back("\"" + r.name + "\" already exists as an ore.");
            continue;
        }

        const int ziel = FindOreState(e.text("zu", "alloy"));
        if (ziel < 0)
        {
            plan.problems.push_back(r.name + ": \"zu\" is not a state.");
            continue;
        }
        r.to = ziel;

        const JsonValue* von = e.find("von");
        if (von == nullptr || von->type != JsonValue::Type::Array)
        {
            plan.problems.push_back(r.name + ": \"von\" must be a list.");
            continue;
        }

        for (const JsonValue& s : von->items)
        {
            const int nummer = FindOreState(s.str);
            if (nummer < 0)
                plan.problems.push_back(r.name + ": state \"" + s.str + "\" is not something I know.");
            else
                r.from |= (1u << (unsigned)nummer);
        }

        // Roh legieren gibt es nicht: erst schmelzen, dann zusammen.
        if (r.from == 0 || r.from == (1u << (unsigned)OreState::Raw))
        {
            plan.problems.push_back(r.name +
                                    ": in \"von\" needs a state other than \"raw\".");
            continue;
        }

        const bool zutatenOk = ReadParts(e, ores, r, plan.problems);

        // Das Ergebnis kommt in die Erzliste, auch wenn das Rezept nicht taugt:
        // sonst wuerden sich die Erznummern verschieben, sobald jemand einen
        // Tippfehler in der Datei hat - und die stehen im Spielstand.
        r.result = AddResultOre(e, ores, r, plan.problems);

        if (!zutatenOk)
            continue;

        // Kann die Zutat ueberhaupt in einen der Zustaende kommen? Diamant
        // schmilzt man nicht - ein Rezept mit Diamant aus "smelted" waere tot.
        for (const AlloyPart& p : r.parts)
        {
            const Ore& erz = ores.ores[(std::size_t)p.ore];
            if ((erz.states & r.from) == 0)
                plan.problems.push_back(r.name + ": " + erz.name +
                                        " can never reach a state from \"von\" kommen.");
        }

        if (!PartsMatch(ores, r, plan.problems))
            continue;  // die beiden Dateien widersprechen sich

        plan.recipes.push_back(r);
    }

    return plan;
}
