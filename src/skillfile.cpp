#include "skillfile.h"
#include "datapath.h"


#include <cstdlib>
#include <fstream>
#include <sstream>

namespace
{

struct KeyEntry
{
    const char* key;
    Skill       skill;
};

// Beide Schreibweisen sind erlaubt: mit Umlaut und ohne. Wer die Datei mit
// einem Editor bearbeitet, der keine Umlaute mag, soll nicht auflaufen.
const KeyEntry kKeys[] = {
    {"while", Skill::While},
    {"for", Skill::For},
    {"if", Skill::If},
    {"else", Skill::Else},
    {"print", Skill::Print},
    {"check", Skill::Check},
    {"shared", Skill::Shared},
    {"mine", Skill::Mine},
    {"care", Skill::Care},
    {"sell", Skill::Sell},
    {"bag", Skill::Bag},
    {"variables", Skill::Variable},
    {"classes", Skill::Class},
    {"functions", Skill::Function},
    {"wash", Skill::Wash},
    {"smelt", Skill::Smelt},
    {"cast", Skill::Cast},
    {"clean", Skill::Clean},
    {"polish", Skill::Polish},
    {"harden", Skill::Harden},
    {"refine", Skill::Refine},
    {"press", Skill::Press},
    {"alloy", Skill::Alloy},
    {"loop+", Skill::ExtraLoop},
    {"condition+", Skill::ExtraIf},
    {"console+", Skill::ExtraConsole},
    {"variable+", Skill::ExtraVariable},
    {"class+", Skill::ExtraClass},
    {"function+", Skill::ExtraFunction},
    {"speed", Skill::Speed},
    {"money", Skill::MoneyPerBlock},
    {"regrow", Skill::FasterRespawn},

    // Sprache, die es frueher umsonst gab - siehe codecheck.cpp.
    {"switch", Skill::Switch},
    {"ternary", Skill::Ternary},
    {"goto", Skill::Goto},
    {"recursion", Skill::Recursion},
    {"container", Skill::Container},

    // Der Compiler als Ausbaustufe.
    {"inline", Skill::Inline},
    {"optimize", Skill::Optimize},

    // Fragen an die Welt.
    {"info", Skill::Info},
    {"assay", Skill::Assay},
    {"count", Skill::Count},
    {"job", Skill::JobQuery},
    {"wait", Skill::Wait},
    {"status", Skill::Status},
    {"market", Skill::Market},
    {"chart", Skill::Chart},

    // Sonderfaehigkeiten.
    {"rush_mine", Skill::RushMine},
    {"rush_grow", Skill::RushGrow},
    {"rush_work", Skill::RushWork},
    {"rush_market", Skill::RushMarket},

    {"furnace+", Skill::ExtraFurnace},

    {"quests", Skill::Quests},

    // Das Ende der Verarbeitungskette.
    {"etch", Skill::Etch},
    {"fuse", Skill::Fuse},
};

bool LookUp(const std::string& key, Skill& out)
{
    for (const KeyEntry& e : kKeys)
        if (key == e.key)
        {
            out = e.skill;
            return true;
        }
    return false;
}

bool IsOnce(const std::string& w)
{
    return w == "once";
}

bool IsOften(const std::string& w)
{
    return w == "often";
}

bool IsNeeds(const std::string& w)
{
    return w == "needs";
}

// "4", "2-6" oder "2-" (ab 2, ohne Ende).
bool ParseRange(const std::string& text, int& lo, int& hi)
{
    const std::size_t dash = text.find('-');
    if (dash == std::string::npos)
    {
        lo = hi = std::atoi(text.c_str());
        return lo > 0;
    }

    lo = std::atoi(text.substr(0, dash).c_str());

    const std::string rest = text.substr(dash + 1);
    hi = rest.empty() ? 1000000000 : std::atoi(rest.c_str());  // offenes Ende
    return lo > 0 && hi >= lo;
}

std::string Where(int line)
{
    return "line " + std::to_string(line) + ": ";
}

// Die ueblichen Stellen, an denen die Datei liegen kann. Der Projektordner
// kommt zuerst: wer dort etwas aendert, will es sofort im Spiel sehen.
std::vector<std::string> Candidates()
{
    return DataPaths("skills.txt");
}

}  // namespace

bool SkillFromKey(const std::string& key, Skill& out)
{
    return LookUp(key, out);
}

SkillPlan LoadSkillPlan()
{
    SkillPlan plan;

    std::ifstream in;
    for (const std::string& path : Candidates())
    {
        in.open(path.c_str());
        if (in.is_open())
        {
            plan.file = path;
            break;
        }
        in.clear();
    }

    if (!in.is_open())
    {
        plan.problems.push_back("data/skills.txt not found.");
        plan.problems.push_back("Without this file the game does not know");
        plan.problems.push_back("what it should unlock.");
        return plan;
    }

    std::string line;
    int         lineNo = 0;

    while (std::getline(in, line))
    {
        ++lineNo;

        const std::size_t hash = line.find('#');
        if (hash != std::string::npos)
            line.resize(hash);

        std::istringstream       parts(line);
        std::vector<std::string> words;
        std::string              word;
        while (parts >> word)
            words.push_back(word);

        if (words.empty())
            continue;

        // Wie tief der Baum wird.
        if (words[0] == "steps")
        {
            // 0 heisst: kein Ende. Der Baum waechst weiter, solange man kauft.
            const int n = (words.size() > 1) ? std::atoi(words[1].c_str()) : -1;
            if (n < 0)
                plan.problems.push_back(Where(lineNo) + "\"steps\" needs a number.");
            else
                plan.steps = n;
            continue;
        }

        // Wie stark der Preis pro Schritt steigt.
        if (words[0] == "growth")
        {
            const double v = (words.size() > 1) ? std::atof(words[1].c_str()) : 0.0;
            if (v < 1.0)
                plan.problems.push_back(Where(lineNo) +
                                        "\"growth\" needs a number of 1 or more, e.g. 1.5.");
            else
                plan.growth = (float)v;
            continue;
        }

        // Was die stapelbaren Punkte bringen.
        {
            struct Wert
            {
                const char* schluessel;
                float*      ziel;
                int*        zielGanz;
                float       kleinster;
            };

            const Wert werte[] = {
                {"speed_start", &plan.speedStart, nullptr, 0.1f},
                {"speed_plus", &plan.speedPlus, nullptr, 0.0f},
                {"money_plus", nullptr, &plan.moneyPlus, 0.0f},
                {"regrow_start", &plan.respawnStart, nullptr, 0.05f},
                {"regrow_mul", &plan.respawnMul, nullptr, 0.1f},
                {"optimize_mul", &plan.optimizeMul, nullptr, 1.0f},
                {"assay_cost", nullptr, &plan.assayCost, 0.0f},
                {"assay_seconds", &plan.assaySeconds, nullptr, 0.0f},
                {"market_swing", &plan.marketSwing, nullptr, 0.0f},
                {"market_speed", &plan.marketSpeed, nullptr, 0.0f},
                {"power_cost", nullptr, &plan.powerCost, 0.0f},
                {"power_seconds", &plan.powerSeconds, nullptr, 0.0f},
                {"power_market_boost", &plan.powerMarketBoost, nullptr, 1.0f},
                {"power_cooldown", &plan.powerCooldownSeconds, nullptr, 0.0f},
            };

            bool getroffen = false;
            for (const Wert& w : werte)
            {
                if (words[0] != w.schluessel)
                    continue;

                getroffen      = true;
                const double v = (words.size() > 1) ? std::atof(words[1].c_str()) : -1.0;

                if (v < (double)w.kleinster)
                    plan.problems.push_back(Where(lineNo) + "\"" + w.schluessel +
                                            "\" needs a bigger number.");
                else if (w.ziel != nullptr)
                    *w.ziel = (float)v;
                else
                    *w.zielGanz = (int)v;
            }

            if (getroffen)
                continue;
        }

        // Mindestabstand zwischen zwei einmaligen Punkten.
        if (words[0] == "spacing")
        {
            const int n = (words.size() > 1) ? std::atoi(words[1].c_str()) : -1;
            if (n < 0)
                plan.problems.push_back(Where(lineNo) + "\"spacing\" needs a number.");
            else
                plan.spacing = n;
            continue;
        }

        // Wie viele Punkte hoechstens gleichzeitig offenstehen.
        if (words[0] == "open")
        {
            const int n = (words.size() > 1) ? std::atoi(words[1].c_str()) : 0;
            if (n <= 0)
                plan.problems.push_back(Where(lineNo) + "\"open\" needs a number.");
            else
                plan.maxOpen = n;
            continue;
        }

        // Wie viele neue Punkte ein Kauf aufmacht.
        if (words[0] == "children")
        {
            const int lo = (words.size() > 1) ? std::atoi(words[1].c_str()) : 0;
            const int hi = (words.size() > 2) ? std::atoi(words[2].c_str()) : lo;
            if (lo <= 0 || hi < lo)
            {
                plan.problems.push_back(Where(lineNo) + "\"children\" needs two numbers, e.g. 1 3.");
                continue;
            }
            plan.minKids = lo;
            plan.maxKids = hi;
            continue;
        }

        if (words.size() < 4)
        {
            plan.problems.push_back(Where(lineNo) +
                                    "something is missing (key, step, price, how often).");
            continue;
        }

        SkillRule rule;
        if (!LookUp(words[0], rule.skill))
        {
            plan.problems.push_back(Where(lineNo) + "\"" + words[0] + "\" is not something I know.");
            continue;
        }

        if (!ParseRange(words[1], rule.minStep, rule.maxStep))
        {
            plan.problems.push_back(Where(lineNo) + "\"" + words[1] +
                                    "\" is not a step (e.g. 4 or 2-6).");
            continue;
        }

        rule.price = std::atoi(words[2].c_str());
        if (rule.price <= 0)
        {
            plan.problems.push_back(Where(lineNo) + "\"" + words[2] + "\" is not a price.");
            continue;
        }

        std::size_t next = 4;  // ab hier koennte "braucht ..." stehen

        if (IsOnce(words[3]))
        {
            rule.once   = true;
            rule.weight = 2;  // ohne Zahl: eher selten
            if (words.size() > 4 && std::atoi(words[4].c_str()) > 0)
            {
                rule.weight = std::atoi(words[4].c_str());
                next        = 5;
            }
        }
        else if (IsOften(words[3]))
        {
            rule.weight = (words.size() > 4) ? std::atoi(words[4].c_str()) : 0;
            if (rule.weight <= 0)
            {
                plan.problems.push_back(Where(lineNo) + "the weight after \"often\" is missing.");
                continue;
            }
            next = 5;
        }
        else
        {
            plan.problems.push_back(Where(lineNo) + "\"" + words[3] +
                                    "\" must be \"einmal\" or \"oft <zahl>\".");
            continue;
        }

        // Optional: "wachstum 1.15" - wie sich das Gewicht nach aussen aendert.
        if (next + 1 < words.size() && words[next] == "rise")
        {
            const double v = std::atof(words[next + 1].c_str());
            if (v <= 0.0)
                plan.problems.push_back(Where(lineNo) + "\"rise\" needs a number > 0.");
            else
                rule.weightGrowth = (float)v;
            next += 2;
        }

        if (next < words.size())
        {
            if (!IsNeeds(words[next]))
            {
                plan.problems.push_back(Where(lineNo) + "\"" + words[next] +
                                        "\" does not belong here (did you mean \"braucht\"?).");
                continue;
            }
            if (next + 1 >= words.size() || !LookUp(words[next + 1], rule.needs))
            {
                plan.problems.push_back(Where(lineNo) + "a key after \"needs\" is missing.");
                continue;
            }
        }

        plan.rules.push_back(rule);
    }

    if (plan.rules.empty())
        plan.problems.push_back("Not a single line with a point - the tree stays empty.");

    return plan;
}
