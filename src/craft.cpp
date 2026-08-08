#include "craft.h"

#include "json.h"
#include "ore.h"
#include "skilltree.h"

#include <windows.h>

#include <fstream>
#include <sstream>

namespace
{

struct CommandSkill
{
    const char* command;
    Skill       skill;
};

// Die Methoden stehen fest im Header des Spielercodes - welche Schritte es
// wirklich gibt, entscheidet die Datei. Hier steht nur, welcher Befehl welchen
// Punkt im Baum braucht.
const CommandSkill kCommands[] = {
    {"wash", Skill::Wash},     {"smelt", Skill::Smelt},   {"cast", Skill::Cast},
    {"clean", Skill::Clean},   {"polish", Skill::Polish}, {"harden", Skill::Harden},
    {"refine", Skill::Refine}, {"press", Skill::Press},
};

Skill SkillFor(const std::string& command)
{
    for (const CommandSkill& e : kCommands)
        if (command == e.command)
            return e.skill;
    return Skill::None;
}

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

        out.push_back(dir + "\\..\\..\\data\\verarbeitung.json");  // build/Debug -> Projekt
        out.push_back(dir + "\\..\\..\\..\\data\\verarbeitung.json");
        out.push_back(dir + "\\..\\data\\verarbeitung.json");
        out.push_back(dir + "\\data\\verarbeitung.json");
        out.push_back(dir + "\\verarbeitung.json");
    }

    out.push_back("data/verarbeitung.json");
    out.push_back("verarbeitung.json");
    return out;
}

}  // namespace

const CraftStep* CraftPlan::find(const std::string& command) const
{
    for (const CraftStep& s : steps)
        if (s.command == command)
            return &s;
    return nullptr;
}

float CraftPlan::valueOf(int state) const
{
    if (state < 0 || state >= (int)stateValue.size())
        return 1.0f;
    return stateValue[(std::size_t)state];
}

float CraftPlan::purityFactor(int purity) const
{
    const float t = (float)(purity < 0 ? 0 : (purity > 100 ? 100 : purity)) / 100.0f;
    return valueMin + (valueMax - valueMin) * t;
}

bool CraftUnlocked(const CraftStep& step, const Limits& limits)
{
    switch (step.skill)
    {
    case Skill::Wash: return limits.allowWash;
    case Skill::Smelt: return limits.allowSmelt;
    case Skill::Cast: return limits.allowCast;
    case Skill::Clean: return limits.allowClean;
    case Skill::Polish: return limits.allowPolish;
    case Skill::Harden: return limits.allowHarden;
    case Skill::Refine: return limits.allowRefine;
    case Skill::Press: return limits.allowPress;
    default: return false;
    }
}

CraftPlan LoadCraftPlan()
{
    CraftPlan plan;

    // Ohne Datei bleibt jeder Zustand gleich viel wert - dann verkauft man eben
    // roh weiter, statt dass das Spiel abstuerzt.
    plan.stateValue.assign((std::size_t)OreState::Count, 1.0f);

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
        plan.problems.push_back("data/verarbeitung.json nicht gefunden.");
        return plan;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    JsonValue   wurzel;
    std::string fehler;
    if (!ParseJson(buffer.str(), wurzel, fehler))
    {
        plan.problems.push_back("data/verarbeitung.json - " + fehler);
        return plan;
    }

    if (const JsonValue* r = wurzel.find("reinheit"))
    {
        plan.startPurity = (int)r->number("start", plan.startPurity);
        plan.valueMin    = (float)r->number("wert_min", plan.valueMin);
        plan.valueMax    = (float)r->number("wert_max", plan.valueMax);

        if (plan.startPurity < 0)
            plan.startPurity = 0;
        if (plan.startPurity > 100)
            plan.startPurity = 100;
        if (plan.valueMin < 0.0f)
            plan.valueMin = 0.0f;
        if (plan.valueMax < plan.valueMin)
        {
            plan.problems.push_back("reinheit.wert_max ist kleiner als wert_min.");
            plan.valueMax = plan.valueMin;
        }
    }

    if (const JsonValue* z = wurzel.find("zustandswert"))
    {
        for (const auto& f : z->fields)
        {
            const int nummer = FindOreState(f.first);
            if (nummer < 0)
                plan.problems.push_back("zustandswert: \"" + f.first + "\" kenne ich nicht.");
            else if (f.second.type == JsonValue::Type::Number)
                plan.stateValue[(std::size_t)nummer] = (float)f.second.num;
        }
    }

    const JsonValue* liste = wurzel.find("schritte");
    if (liste == nullptr || liste->type != JsonValue::Type::Array)
    {
        plan.problems.push_back("Es fehlt die Liste \"schritte\": [ ... ].");
        return plan;
    }

    for (const JsonValue& e : liste->items)
    {
        if (e.type != JsonValue::Type::Object)
            continue;

        CraftStep step;
        step.command = e.text("befehl", "");
        step.name    = e.text("name", step.command.c_str());
        step.seconds = (float)e.number("dauer", 1.0);
        step.purity  = (int)e.number("reinheit", 0.0);
        step.skill   = SkillFor(step.command);

        if (step.skill == Skill::None)
        {
            // Aufrufen koennte man so einen Schritt nie: die Methoden im
            // Spielercode stehen fest.
            plan.problems.push_back("Den Befehl \"" + step.command + "\" gibt es nicht.");
            continue;
        }

        if (step.seconds < 0.05f)
            step.seconds = 0.05f;

        const int ziel = FindOreState(e.text("zu", ""));
        if (ziel < 0)
        {
            plan.problems.push_back(step.command + ": \"zu\" ist kein Zustand.");
            continue;
        }
        step.to = ziel;

        const JsonValue* von = e.find("von");
        if (von == nullptr || von->type != JsonValue::Type::Array)
        {
            plan.problems.push_back(step.command + ": \"von\" muss eine Liste sein.");
            continue;
        }

        for (const JsonValue& s : von->items)
        {
            const int nummer = FindOreState(s.str);
            if (nummer < 0)
                plan.problems.push_back(step.command + ": Zustand \"" + s.str +
                                        "\" kenne ich nicht.");
            else
                step.from |= (1u << (unsigned)nummer);
        }

        if (step.from == 0)
        {
            plan.problems.push_back(step.command + ": in \"von\" steht kein Zustand.");
            continue;
        }

        if (plan.find(step.command) != nullptr)
        {
            plan.problems.push_back("Den Befehl \"" + step.command + "\" gibt es zweimal.");
            continue;
        }

        plan.steps.push_back(step);
    }

    if (plan.steps.empty())
        plan.problems.push_back("In \"schritte\" steht kein einziger Schritt.");

    return plan;
}
