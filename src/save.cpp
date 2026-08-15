#include "save.h"
#include "proc.h"

#include "alloy.h"
#include "console.h"
#include "ore.h"
#include "skilltree.h"
#include "world.h"


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
    const std::string dir = ExeDir();
    return dir.empty() ? std::string("spielstand.txt") : (dir + "/spielstand.txt");
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

// 4: Legierungen. Sie haengen hinten an der Erzliste, und im Baum ist ein
//    Punkt dazugekommen - beides verschiebt Nummern, die in der Datei stehen.
// 5: Runden. Phase, Restzeit und Rundennummer gehoeren dazu - wer mitten im
//    Lauf beendet, macht beim naechsten Start dort weiter.
const int kVersion = 8;  // 8: Ergebnis der Runde (Ziel bezahlt)

}  // namespace

bool SaveGame(const World& world, const SkillTree& tree,
              const std::vector<std::unique_ptr<Console>>& consoles, const OrePlan& ores,
              const AlloyPlan& alloys)
{
    std::ofstream out(SavePath().c_str(), std::ios::binary);
    if (!out.is_open())
        return false;

    out << "codeklicker " << kVersion << "\n";

    out << "geld " << world.money << "\n";
    out << "abgebaut " << world.minedCount << "\n";

    // Die Runde: Phase, Restzeit, Nummer. Wer mitten im Lauf beendet, soll
    // dort weitermachen - und nicht mit einer geschenkten Vorbereitung.
    out << "runde_ergebnis " << (world.roundWon ? 1 : 0) << " " << world.roundPaid
        << "\n";
    out << "runde " << world.roundNumber << " " << (int)world.phase << " " << world.roundLeft
        << "\n";
    out << "runde_start " << world.roundMoneyStart << " " << world.roundMinedStart << "\n";
    out << "runde_ende " << world.roundMoneyEnd << " " << world.roundMined << " "
        << world.roundSoldCount << " " << world.roundSoldMoney << "\n";

    // Welcher Block gerade dasteht, gehoert dazu: sonst waere nach dem Laden
    // ploetzlich ein anderes Erz da.
    out << "block " << world.ore << " " << world.oreSeed << " " << (world.blockAlive ? 1 : 0)
        << " " << world.respawnTimer
        << "\n";

    // Was der Block verlangt, steht in einer EIGENEN Zeile und nicht hinten an
    // "block" dran: ein aelterer Spielstand hat sie einfach nicht, und ein
    // aelteres Spiel ueberliest sie. Haengte die Zahl an "block", waere jeder
    // alte Stand beim Einlesen kaputt.
    out << "block_care " << (int)world.care << "\n";

    // Die Tasche: was abgebaut, aber noch nicht verkauft ist. Ein laufender
    // Auftrag steht mit Absicht nicht drin - beim Beenden bricht er ab und das
    // Material liegt wieder in der Tasche.
    out << "tasche " << world.inventory.size() << "\n";
    for (const auto& e : world.inventory)
        out << e.first.ore << " " << e.first.state << " " << e.second.count << " "
            << e.second.purity << "\n";

    // Welche Wiki-Seiten schon gelesen sind. Ohne das waere nach jedem Start
    // wieder alles als neu markiert.
    out << "wiki " << world.wikiSeen.size() << "\n";
    for (const std::string& t : world.wikiSeen)
        out << t << "\n";

    // Was das Wiki ueber die Erze weiss. Das ist erspielt und nicht
    // ausgerechnet - ohne diese beiden Listen stuende die Erz-Seite nach jedem
    // Start wieder leer da.
    //
    // Die Versionsnummer bleibt dabei, wo sie ist: unbekannte Zeilen
    // ueberspringt der Leser weiter unten sowieso, und fehlen die Zeilen in
    // einem aelteren Stand, faengt die Sammlung eben bei null an. Ein
    // Versionssprung wuerde dagegen den ganzen Spielstand wegwerfen.
    out << "erz_fund " << world.oreFirst.size() << "\n";
    for (const auto& e : world.oreFirst)
        out << e.first << " " << e.second.state << " " << e.second.purity << "\n";

    out << "erz_schritt " << world.oreSteps.size() << "\n";
    for (const World::OreStep& s : world.oreSteps)
        out << s.ore << " " << s.from << " " << s.to << "\n";

    // Die gewuerfelten Erze. Sie stehen in keiner Datei - sie sind beim Spielen
    // entstanden, und ohne sie waere nach dem Laden jede Nummer in der Tasche
    // um so viele Plaetze verschoben.
    //
    // Der Name steht auf einer eigenen Zeile, die Zahlen auf der naechsten. So
    // faellt es nicht auf die Nase, falls je ein Name mit Leerzeichen entsteht.
    {
        const int erste = ores.handmade;
        out << "erz_gewuerfelt " << (int)ores.ores.size() - erste << " " << ores.rolled << "\n";

        for (int i = erste; i < (int)ores.ores.size(); ++i)
        {
            const Ore& o = ores.ores[(std::size_t)i];
            out << o.name << "\n";
            out << o.rarity << " " << o.value << " " << o.mineSeconds << " " << o.minLevel << " "
                << o.purity << " " << o.pattern << " " << o.states << " " << (o.minable ? 1 : 0)
                << " " << (int)o.color1.r << " " << (int)o.color1.g << " " << (int)o.color1.b << " "
                << (int)o.color2.r << " " << (int)o.color2.g << " " << (int)o.color2.b << " "
                << (int)o.care << "\n";
        }
    }

    // Und die Rezepte dazu. Wer mit wem darf ("legierbar_mit"), ergibt sich
    // daraus von selbst und muss nicht extra mitgeschrieben werden.
    {
        int eigene = 0;
        for (const AlloyRecipe& r : alloys.recipes)
            if (r.result >= ores.handmade)
                ++eigene;

        out << "erz_rezept " << eigene << "\n";
        for (const AlloyRecipe& r : alloys.recipes)
        {
            if (r.result < ores.handmade)
                continue;  // das stand in data/legierungen.json

            out << r.name << "\n";
            out << r.from << " " << r.to << " " << r.seconds << " " << r.purity << " " << r.result
                << " " << r.parts.size();
            for (const AlloyPart& p : r.parts)
                out << " " << p.ore << " " << p.count;
            out << "\n";
        }
    }

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
              int& nextConsoleId, OrePlan& ores, AlloyPlan& alloys)
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

    // Erst am Ende angehaengt - ein halber Spielstand darf die Erzliste nicht
    // schon halb umgebaut haben.
    std::vector<Ore>         neueOre;
    std::vector<AlloyRecipe> neueRezepte;
    int                      neueErze = 0;  // wie viele davon gewuerfelte Erze sind

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
        else if (wort == "runde_ergebnis")
        {
            int gewonnen = 1;
            in >> gewonnen >> neueWelt.roundPaid;
            neueWelt.roundWon = (gewonnen != 0);
        }
        else if (wort == "runde")
        {
            int phase = 0;
            in >> neueWelt.roundNumber >> phase >> neueWelt.roundLeft;

            // Eine kaputte Zahl darf nicht zu einer Phase werden, die es nicht
            // gibt - dann haenge man fuer immer fest.
            if (phase < 0 || phase > (int)RoundPhase::Report)
                phase = (int)RoundPhase::Prepare;
            neueWelt.phase = (RoundPhase)phase;

            if (neueWelt.roundNumber < 1)
                neueWelt.roundNumber = 1;
            if (neueWelt.roundLeft < 0.0f)
                neueWelt.roundLeft = 0.0f;
        }
        else if (wort == "runde_start")
        {
            in >> neueWelt.roundMoneyStart >> neueWelt.roundMinedStart;
        }
        else if (wort == "runde_ende")
        {
            in >> neueWelt.roundMoneyEnd >> neueWelt.roundMined >> neueWelt.roundSoldCount >>
                neueWelt.roundSoldMoney;
        }
        else if (wort == "tasche")
        {
            std::size_t n = 0;
            in >> n;
            for (std::size_t i = 0; i < n; ++i)
            {
                World::Item was;
                int         anzahl   = 0;
                int         reinheit = 0;
                in >> was.ore >> was.state >> anzahl >> reinheit;
                if (anzahl > 0)
                    neueWelt.addToBag(was, anzahl, reinheit);
            }
        }
        else if (wort == "block")
        {
            int lebt = 1;
            in >> neueWelt.ore >> neueWelt.oreSeed >> lebt >> neueWelt.respawnTimer;
            neueWelt.blockAlive = (lebt != 0);
        }
        else if (wort == "block_care")
        {
            int c = 0;
            in >> c;
            if (c > 0 && c < (int)BlockCare::Count)
                neueWelt.care = (BlockCare)c;
        }
        else if (wort == "wiki")
        {
            std::size_t n = 0;
            in >> n;
            std::string rest;
            std::getline(in, rest);  // Rest der Kopfzeile wegwerfen

            for (std::size_t i = 0; i < n; ++i)
            {
                std::string titel;
                if (!ReadLine(in, titel))
                    break;
                if (!titel.empty())
                    neueWelt.wikiSeen.insert(titel);
            }
        }
        else if (wort == "erz_fund")
        {
            std::size_t n = 0;
            in >> n;
            for (std::size_t i = 0; i < n; ++i)
            {
                int erz = 0, zustand = 0, reinheit = 0;
                in >> erz >> zustand >> reinheit;
                if (erz >= 0)
                    neueWelt.noteOre(erz, zustand, reinheit);
            }
        }
        else if (wort == "erz_schritt")
        {
            std::size_t n = 0;
            in >> n;
            for (std::size_t i = 0; i < n; ++i)
            {
                World::OreStep s;
                in >> s.ore >> s.from >> s.to;
                if (s.ore >= 0)
                    neueWelt.oreSteps.insert(s);
            }
        }
        else if (wort == "erz_gewuerfelt")
        {
            int anzahl = 0;
            in >> anzahl >> neueErze;

            std::string rest;
            std::getline(in, rest);  // Rest der Kopfzeile wegwerfen

            for (int i = 0; i < anzahl; ++i)
            {
                std::string name;
                if (!ReadLine(in, name) || name.empty())
                    break;

                Ore o;
                o.name = name;

                // Die Zahlenzeile wird erst geholt und dann zerlegt. Sonst
                // wuerde ein Spielstand aus einer aelteren Fassung, in dem
                // hinten ein Wert fehlt, die naechste Zeile mitlesen - und ab
                // da waere alles verschoben.
                std::string zeile;
                if (!ReadLine(in, zeile))
                    break;

                std::istringstream werte(zeile);

                int r1 = 0, g1 = 0, b1 = 0, r2 = 0, g2 = 0, b2 = 0, abbaubar = 1;
                werte >> o.rarity >> o.value >> o.mineSeconds >> o.minLevel >> o.purity >>
                    o.pattern >> o.states >> abbaubar >> r1 >> g1 >> b1 >> r2 >> g2 >> b2;

                if (!werte)
                    break;

                // Die Behandlung kam spaeter dazu. Steht sie nicht da, wird sie
                // aus dem Namen abgeleitet - dieselbe Rechnung wie bei den
                // Erzen aus der Datei, und damit immer dieselbe.
                int behandlung = -1;
                if (!(werte >> behandlung) || behandlung < 0 ||
                    behandlung >= (int)BlockCare::Count)
                    o.care = DeriveOreCare(ores.care, o.name, o.value);
                else
                    o.care = (BlockCare)behandlung;

                o.minable = (abbaubar != 0);
                o.color1  = Color{(unsigned char)r1, (unsigned char)g1, (unsigned char)b1};
                o.color2  = Color{(unsigned char)r2, (unsigned char)g2, (unsigned char)b2};

                if (o.rarity < 0.01f)
                    o.rarity = 0.01f;
                if (o.value < 1)
                    o.value = 1;
                if (o.mineSeconds < 0.05f)
                    o.mineSeconds = 0.05f;
                if (o.pattern < 0.5f)
                    o.pattern = 0.5f;

                neueOre.push_back(o);
            }
        }
        else if (wort == "erz_rezept")
        {
            int anzahl = 0;
            in >> anzahl;

            std::string rest;
            std::getline(in, rest);

            for (int i = 0; i < anzahl; ++i)
            {
                std::string name;
                if (!ReadLine(in, name) || name.empty())
                    break;

                AlloyRecipe r;
                r.name = name;

                std::size_t teile = 0;
                in >> r.from >> r.to >> r.seconds >> r.purity >> r.result >> teile;
                if (!in)
                    break;

                for (std::size_t k = 0; k < teile; ++k)
                {
                    AlloyPart p;
                    in >> p.ore >> p.count;
                    r.parts.push_back(p);
                }

                neueRezepte.push_back(r);
                std::getline(in, rest);
            }
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

    // Die gewuerfelten Erze wieder anhaengen - genau in der Reihenfolge, in der
    // sie entstanden sind, sonst stimmen die Nummern in der Tasche nicht mehr.
    ores.ores.resize((std::size_t)ores.handmade);
    for (const Ore& o : neueOre)
        ores.ores.push_back(o);
    ores.rolled = neueErze;

    // Rezepte dazu, und die beiden Zutaten muessen einander wieder kennen -
    // ohne "legierbar_mit" wuerde das Rezept nicht greifen.
    for (const AlloyRecipe& r : neueRezepte)
    {
        if (r.result < 0 || r.result >= (int)ores.ores.size())
            continue;

        bool gut = true;
        for (const AlloyPart& p : r.parts)
            if (p.ore < 0 || p.ore >= (int)ores.ores.size())
                gut = false;
        if (!gut)
            continue;

        for (const AlloyPart& a : r.parts)
            for (const AlloyPart& b : r.parts)
                if (a.ore != b.ore)
                {
                    Ore& erz = ores.ores[(std::size_t)a.ore];
                    if (!erz.alloyableWith(ores.ores[(std::size_t)b.ore].name))
                        erz.alloyWith.push_back(ores.ores[(std::size_t)b.ore].name);
                }

        alloys.recipes.push_back(r);
    }

    // Was der Block verlangt, gehoert zum Erz und nicht mehr zum einzelnen
    // Block. In einem Spielstand von frueher steht dort noch ein gewuerfelter
    // Wert - der wird hier geradegerueckt, sonst wollte der Block etwas
    // anderes, als seine Wikiseite sagt.
    neueWelt.care = OreCare(ores, neueWelt.ore);

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
