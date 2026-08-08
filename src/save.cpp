#include "save.h"

#include "console.h"
#include "skilltree.h"
#include "world.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

// Der Spielstand liegt neben der exe. Dort darf das Programm schreiben, und
// man findet die Datei sofort, wenn man sie mal wegwerfen will.
std::string SavePath()
{
    char        exe[MAX_PATH] = {0};
    const DWORD len           = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (len == 0)
        return "spielstand.txt";

    std::string       dir(exe, len);
    const std::size_t cut = dir.find_last_of("\\/");
    if (cut != std::string::npos)
        dir.resize(cut);
    return dir + "\\spielstand.txt";
}

// Zeilenweise lesen, ohne am Ende ein \r zu behalten (falls die Datei mal in
// einem anderen Editor war).
bool ReadLine(std::istream& in, std::string& line)
{
    if (!std::getline(in, line))
        return false;
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    return true;
}

const int kVersion = 1;

}  // namespace

bool SaveGame(const World& world, const SkillTree& tree,
              const std::vector<std::unique_ptr<Console>>& consoles)
{
    std::ofstream out(SavePath().c_str(), std::ios::binary);
    if (!out.is_open())
        return false;

    out << "codeklicker " << kVersion << "\n";

    out << "geld " << world.money << "\n";
    out << "abgebaut " << world.minedCount << "\n";

    // Welcher Block gerade dasteht, gehoert dazu: sonst waere nach dem Laden
    // ploetzlich ein anderes Erz da.
    out << "block " << world.ore << " " << world.oreSeed << " " << (world.blockAlive ? 1 : 0)
        << "\n";

    // Die Tasche: was abgebaut, aber noch nicht verkauft ist.
    out << "tasche " << world.inventory.size() << "\n";
    for (const auto& e : world.inventory)
        out << e.first << " " << e.second << "\n";

    // shared[...] soll einen Neustart ueberleben - das ist der ganze Witz
    // daran, also gehoert es in den Spielstand.
    out << "geteilt " << world.shared.size() << "\n";
    for (const auto& e : world.shared)
        out << e.second << " " << e.first << "\n";  // Wert zuerst, Name ist der Rest

    // Der Wuerfel selbst: sonst wuerde der Baum nach dem Laden anders
    // weiterwachsen, als er es ohne Neustart getan haette.
    std::ostringstream rng;
    rng << tree.rng;
    out << "wuerfel " << rng.str() << "\n";

    out << "seit_einmal " << tree.sinceOnce << "\n";

    out << "einmal " << tree.usedOnce.size() << "\n";
    for (bool b : tree.usedOnce)
        out << (b ? 1 : 0) << " ";
    out << "\n";

    out << "knoten " << tree.nodes.size() << "\n";
    for (const SkillNode& n : tree.nodes)
        out << (int)n.skill << " " << n.depth << " " << n.parent << " " << n.cost << " "
            << (n.owned ? 1 : 0) << " " << n.gx << " " << n.gy << "\n";

    out << "konsolen " << consoles.size() << "\n";
    for (const auto& c : consoles)
    {
        const std::string        code = c->editor.GetText();
        std::istringstream       parts(code);
        std::vector<std::string> lines;
        std::string              line;
        while (std::getline(parts, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
        }

        out << "konsole " << c->id << " " << lines.size() << " " << (int)c->startPos.x << " "
            << (int)c->startPos.y << "\n";
        for (const std::string& l : lines)
            out << l << "\n";
    }

    return true;
}

bool LoadGame(World& world, SkillTree& tree, std::vector<std::unique_ptr<Console>>& consoles,
              int& nextConsoleId)
{
    std::ifstream in(SavePath().c_str(), std::ios::binary);
    if (!in.is_open())
        return false;

    std::string kopf;
    int         version = 0;
    if (!(in >> kopf >> version) || kopf != "codeklicker" || version != kVersion)
        return false;  // nichts oder etwas Fremdes - dann eben von vorne

    World     neueWelt;
    SkillTree neuerBaum;
    neuerBaum.plan = tree.plan;  // die Regeln kommen weiter aus data/skills.txt

    std::vector<std::unique_ptr<Console>> neueKonsolen;
    int                                   maxId = 0;

    std::string wort;
    while (in >> wort)
    {
        if (wort == "geld")
        {
            in >> neueWelt.money;
        }
        else if (wort == "abgebaut")
        {
            in >> neueWelt.minedCount;
        }
        else if (wort == "tasche")
        {
            std::size_t n = 0;
            in >> n;
            for (std::size_t i = 0; i < n; ++i)
            {
                int erz = 0, anzahl = 0;
                in >> erz >> anzahl;
                if (anzahl > 0)
                    neueWelt.inventory[erz] = anzahl;
            }
        }
        else if (wort == "block")
        {
            int lebt = 1;
            in >> neueWelt.ore >> neueWelt.oreSeed >> lebt;
            neueWelt.blockAlive = (lebt != 0);
        }
        else if (wort == "geteilt")
        {
            std::size_t n = 0;
            in >> n;
            for (std::size_t i = 0; i < n; ++i)
            {
                int wert = 0;
                in >> wert;
                std::string name;
                std::getline(in, name);
                if (!name.empty() && name.front() == ' ')
                    name.erase(0, 1);
                if (!name.empty() && name.back() == '\r')
                    name.pop_back();
                neueWelt.shared[name] = wert;
            }
        }
        else if (wort == "wuerfel")
        {
            in >> neuerBaum.rng;
        }
        else if (wort == "seit_einmal")
        {
            in >> neuerBaum.sinceOnce;
        }
        else if (wort == "einmal")
        {
            std::size_t n = 0;
            in >> n;
            neuerBaum.usedOnce.assign(n, false);
            for (std::size_t i = 0; i < n; ++i)
            {
                int b = 0;
                in >> b;
                neuerBaum.usedOnce[i] = (b != 0);
            }
        }
        else if (wort == "knoten")
        {
            std::size_t n = 0;
            in >> n;
            for (std::size_t i = 0; i < n; ++i)
            {
                SkillNode k;
                int       skill = 0;
                int       owned = 0;
                in >> skill >> k.depth >> k.parent >> k.cost >> owned >> k.gx >> k.gy;
                k.id    = (int)i;
                k.skill = (Skill)skill;
                k.owned = (owned != 0);
                neuerBaum.nodes.push_back(k);
            }
        }
        else if (wort == "konsolen")
        {
            std::size_t n = 0;
            in >> n;
            for (std::size_t i = 0; i < n; ++i)
            {
                std::string marke;
                int         id = 1, zeilen = 0, px = 40, py = 60;
                if (!(in >> marke >> id >> zeilen >> px >> py) || marke != "konsole")
                    break;

                std::string rest;
                std::getline(in, rest);  // Rest der Kopfzeile wegwerfen

                std::string code;
                for (int z = 0; z < zeilen; ++z)
                {
                    std::string line;
                    if (!ReadLine(in, line))
                        break;
                    code += line;
                    code += "\n";
                }

                auto c = std::make_unique<Console>(id, ImVec2((float)px, (float)py), false);
                c->editor.SetText(code);
                neueKonsolen.push_back(std::move(c));

                if (id > maxId)
                    maxId = id;
            }
        }
        else
        {
            std::string rest;  // unbekannte Zeile ueberspringen
            std::getline(in, rest);
        }
    }

    if (neuerBaum.nodes.empty() || neueKonsolen.empty())
        return false;  // halber Spielstand ist schlimmer als keiner

    // Belegte Rasterzellen und Kinderzahlen stehen nicht in der Datei - die
    // ergeben sich aus den Knoten.
    neuerBaum.rebuildCells();

    world         = neueWelt;
    tree          = neuerBaum;
    consoles      = std::move(neueKonsolen);
    nextConsoleId = maxId + 1;
    return true;
}

void DeleteSave()
{
    std::remove(SavePath().c_str());
}
