#include "skillfile.h"

#include <windows.h>

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
    {"prüfen", Skill::Check},
    {"pruefen", Skill::Check},
    {"platzieren", Skill::Place},
    {"geteilt", Skill::Shared},
    {"abbauen", Skill::Mine},
    {"verkaufen", Skill::Sell},
    {"tasche", Skill::Bag},
    {"variablen", Skill::Variable},
    {"klassen", Skill::Class},
    {"funktionen", Skill::Function},
    {"waschen", Skill::Wash},
    {"schmelzen", Skill::Smelt},
    {"giessen", Skill::Cast},
    {"gießen", Skill::Cast},
    {"reinigen", Skill::Clean},
    {"polieren", Skill::Polish},
    {"haerten", Skill::Harden},
    {"härten", Skill::Harden},
    {"veredeln", Skill::Refine},
    {"pressen", Skill::Press},
    {"legieren", Skill::Alloy},
    {"schleife+", Skill::ExtraLoop},
    {"bedingung+", Skill::ExtraIf},
    {"konsole+", Skill::ExtraConsole},
    {"variable+", Skill::ExtraVariable},
    {"klasse+", Skill::ExtraClass},
    {"funktion+", Skill::ExtraFunction},
    {"tempo", Skill::Speed},
    {"geld", Skill::MoneyPerBlock},
    {"nachwachsen", Skill::FasterRespawn},
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
    return w == "einmal";
}

bool IsOften(const std::string& w)
{
    return w == "oft";
}

bool IsNeeds(const std::string& w)
{
    return w == "braucht";
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
    return "Zeile " + std::to_string(line) + ": ";
}

// Die ueblichen Stellen, an denen die Datei liegen kann. Der Projektordner
// kommt zuerst: wer dort etwas aendert, will es sofort im Spiel sehen.
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

        out.push_back(dir + "\\..\\..\\data\\skills.txt");  // build/Debug -> Projekt
        out.push_back(dir + "\\..\\..\\..\\data\\skills.txt");
        out.push_back(dir + "\\..\\data\\skills.txt");
        out.push_back(dir + "\\data\\skills.txt");  // neben der exe
        out.push_back(dir + "\\skills.txt");
    }

    out.push_back("data/skills.txt");
    out.push_back("skills.txt");
    return out;
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
        plan.problems.push_back("data/skills.txt nicht gefunden.");
        plan.problems.push_back("Ohne diese Datei weiss das Spiel nicht,");
        plan.problems.push_back("was es freischalten soll.");
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
        if (words[0] == "schritte")
        {
            // 0 heisst: kein Ende. Der Baum waechst weiter, solange man kauft.
            const int n = (words.size() > 1) ? std::atoi(words[1].c_str()) : -1;
            if (n < 0)
                plan.problems.push_back(Where(lineNo) + "\"schritte\" braucht eine Zahl.");
            else
                plan.steps = n;
            continue;
        }

        // Wie stark der Preis pro Schritt steigt.
        if (words[0] == "steigerung")
        {
            const double v = (words.size() > 1) ? std::atof(words[1].c_str()) : 0.0;
            if (v < 1.0)
                plan.problems.push_back(Where(lineNo) +
                                        "\"steigerung\" braucht eine Zahl ab 1, z.B. 1.5.");
            else
                plan.growth = (float)v;
            continue;
        }

        // Mindestabstand zwischen zwei einmaligen Punkten.
        if (words[0] == "abstand")
        {
            const int n = (words.size() > 1) ? std::atoi(words[1].c_str()) : -1;
            if (n < 0)
                plan.problems.push_back(Where(lineNo) + "\"abstand\" braucht eine Zahl.");
            else
                plan.spacing = n;
            continue;
        }

        // Wie viele Punkte hoechstens gleichzeitig offenstehen.
        if (words[0] == "offen")
        {
            const int n = (words.size() > 1) ? std::atoi(words[1].c_str()) : 0;
            if (n <= 0)
                plan.problems.push_back(Where(lineNo) + "\"offen\" braucht eine Zahl.");
            else
                plan.maxOpen = n;
            continue;
        }

        // Wie viele neue Punkte ein Kauf aufmacht.
        if (words[0] == "kinder")
        {
            const int lo = (words.size() > 1) ? std::atoi(words[1].c_str()) : 0;
            const int hi = (words.size() > 2) ? std::atoi(words[2].c_str()) : lo;
            if (lo <= 0 || hi < lo)
            {
                plan.problems.push_back(Where(lineNo) + "\"kinder\" braucht zwei Zahlen, z.B. 1 3.");
                continue;
            }
            plan.minKids = lo;
            plan.maxKids = hi;
            continue;
        }

        if (words.size() < 4)
        {
            plan.problems.push_back(Where(lineNo) +
                                    "es fehlt etwas (schluessel, schritt, preis, wie oft).");
            continue;
        }

        SkillRule rule;
        if (!LookUp(words[0], rule.skill))
        {
            plan.problems.push_back(Where(lineNo) + "\"" + words[0] + "\" kenne ich nicht.");
            continue;
        }

        if (!ParseRange(words[1], rule.minStep, rule.maxStep))
        {
            plan.problems.push_back(Where(lineNo) + "\"" + words[1] +
                                    "\" ist kein Schritt (z.B. 4 oder 2-6).");
            continue;
        }

        rule.price = std::atoi(words[2].c_str());
        if (rule.price <= 0)
        {
            plan.problems.push_back(Where(lineNo) + "\"" + words[2] + "\" ist kein Preis.");
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
                plan.problems.push_back(Where(lineNo) + "nach \"oft\" fehlt das Gewicht.");
                continue;
            }
            next = 5;
        }
        else
        {
            plan.problems.push_back(Where(lineNo) + "\"" + words[3] +
                                    "\" muss \"einmal\" oder \"oft <zahl>\" sein.");
            continue;
        }

        if (next < words.size())
        {
            if (!IsNeeds(words[next]))
            {
                plan.problems.push_back(Where(lineNo) + "\"" + words[next] +
                                        "\" verstehe ich hier nicht (gemeint war \"braucht\"?).");
                continue;
            }
            if (next + 1 >= words.size() || !LookUp(words[next + 1], rule.needs))
            {
                plan.problems.push_back(Where(lineNo) + "nach \"braucht\" fehlt ein Schluessel.");
                continue;
            }
        }

        plan.rules.push_back(rule);
    }

    if (plan.rules.empty())
        plan.problems.push_back("Keine einzige Zeile mit einem Punkt - der Baum bleibt leer.");

    return plan;
}
