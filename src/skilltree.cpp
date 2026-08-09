#include "skilltree.h"

#include "world.h"

#include <cmath>
#include <cstdlib>

namespace
{

// Hoechster Preis, den ein Punkt haben kann. Siehe CostFor.
const int kMaxCost = 1000000000;

// Was ein Punkt in Schritt "step" kostet.
//
// Jeder Schritt nach aussen kostet das growth-fache. Der Grundpreis macht nur
// noch den Unterschied INNERHALB eines Schrittes aus - dadurch ist alles weiter
// draussen zuverlaessig teurer als das, was naeher dran liegt.

int CostFor(int price, int step, float growth)
{
    const double wert = (double)price * std::pow((double)growth, (double)(step - 1)) + 0.5;

    // Der Baum hat kein Ende, der Preis waechst also unbegrenzt weiter. In
    // einen int passt er irgendwann nicht mehr - ohne Deckel wuerde er
    // ueberlaufen und negativ werden, und dann waere alles gratis.
    //
    // Der Deckel liegt unter dem Testgeld, damit man beim Ausprobieren nicht
    // ploetzlich vor einer Karte steht, die man nicht bezahlen kann.
    return (int)(wert > (double)kMaxCost ? (double)kMaxCost : wert);
}

// ---- Raster ---------------------------------------------------------------

// Oben, rechts, unten, links - im Uhrzeigersinn, damit "+1" auf der Nachbar-
// richtung landet.
const int kDir[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

using Cell  = std::pair<int, int>;
using Taken = std::set<Cell>;

// Setzt n gerade neben parentId. Bevorzugt geradeaus weiter nach aussen,
// danach zur Seite; zurueck zum Grosselternteil nur zur Not.
//
// Ist direkt daneben alles belegt, darf es auch zwei Zellen weiter sein - aber
// nur, wenn die Zelle dazwischen frei ist. Die wird dann gleich mit belegt:
// durch sie laeuft die Verbindungslinie, und dort darf nie wieder etwas
// hinkommen. So kreuzt keine Linie eine Karte.
bool PlaceNextTo(std::vector<SkillNode>& nodes, Taken& taken, std::vector<int>& children,
                 int parentId, SkillNode& n)
{
    const SkillNode& p = nodes[(std::size_t)parentId];

    // Richtung, in die dieser Ast bisher waechst. Die Wurzel waechst nach oben.
    int out = 0;
    if (p.parent >= 0)
    {
        const SkillNode& gp = nodes[(std::size_t)p.parent];
        for (int d = 0; d < 4; ++d)
            if (kDir[d][0] == p.gx - gp.gx && kDir[d][1] == p.gy - gp.gy)
                out = d;
    }

    // Zweige wechseln sich ab, sonst haengt alles auf derselben Seite.
    const int side    = (children[(std::size_t)parentId] % 2 == 0) ? 1 : 3;
    const int order[] = {out, (out + side) % 4, (out + 4 - side) % 4, (out + 2) % 4};

    for (int dist = 1; dist <= 2; ++dist)
        for (int d : order)
        {
            bool free = true;
            for (int s = 1; s <= dist && free; ++s)
                if (taken.count(Cell(p.gx + kDir[d][0] * s, p.gy + kDir[d][1] * s)) != 0)
                    free = false;

            if (!free)
                continue;

            for (int s = 1; s <= dist; ++s)
                taken.insert(Cell(p.gx + kDir[d][0] * s, p.gy + kDir[d][1] * s));

            n.gx = p.gx + kDir[d][0] * dist;
            n.gy = p.gy + kDir[d][1] * dist;
            ++children[(std::size_t)parentId];
            return true;
        }

    return false;  // rundherum voll - dann waechst dieser Ast eben nicht weiter
}

}  // namespace

const char* SkillName(Skill skill)
{
    switch (skill)
    {
    case Skill::Root: return "Konsole";
    case Skill::Mine: return "block.mine()";
    case Skill::Sell: return "item.sell()";
    case Skill::While: return "while";
    case Skill::If: return "if";
    case Skill::Else: return "else";
    case Skill::For: return "for";
    case Skill::Print: return "print()";
    case Skill::Check: return "block.isThere()";
    case Skill::Bag: return "item.has()";
    case Skill::Shared: return "shared[...]";
    case Skill::Variable: return "Variablen";
    case Skill::Class: return "Klassen";
    case Skill::Function: return "Funktionen";
    case Skill::Wash: return "item.wash()";
    case Skill::Smelt: return "item.smelt()";
    case Skill::Cast: return "item.cast()";
    case Skill::Clean: return "item.clean()";
    case Skill::Polish: return "item.polish()";
    case Skill::Harden: return "item.harden()";
    case Skill::Refine: return "item.refine()";
    case Skill::Press: return "item.press()";
    case Skill::Alloy: return "item.alloy()";
    case Skill::ExtraLoop: return "+1 Schleife";
    case Skill::ExtraIf: return "+1 Bedingung";
    case Skill::ExtraConsole: return "+1 Konsole";
    case Skill::ExtraVariable: return "+1 Variable";
    case Skill::ExtraClass: return "+1 Klasse";
    case Skill::ExtraFunction: return "+1 Funktion";
    case Skill::Speed: return "Tempo";
    case Skill::MoneyPerBlock: return "Geld pro Block";
    case Skill::FasterRespawn: return "Nachwachsen";
    case Skill::None: return "-";
    }
    return "?";
}

const char* SkillInfo(Skill skill)
{
    switch (skill)
    {
    case Skill::Root: return "Deine erste Konsole. Abbauen kannst du am Anfang nur von Hand - klick auf den Block.";
    case Skill::Mine: return "block.mine() - abbauen lassen, statt selbst zu klicken. Der erste Schritt zur Maschine.";
    case Skill::Sell: return "item.sell() verkauft alles aus der Tasche auf einmal. Ohne das musst du von Hand verkaufen.";
    case Skill::While: return "while und do. Bringt gleich die erste Schleife mit.";
    case Skill::If: return "if. Bringt gleich die erste Bedingung mit.";
    case Skill::Else: return "else. Braucht if.";
    case Skill::For: return "for. Bringt eine weitere Schleife mit - alle Schleifen zählen zusammen.";
    case Skill::Print: return "print(...) schreibt in die Zeile unter dem Editor.";
    case Skill::Check: return "block.isThere() und block.isLoading() - nachschauen, ob der Block da ist.";
    case Skill::Bag: return "item.has(\"Stein\") - in die Tasche schauen. Mit Anzahl: item.has(\"Stein\", 5). Braucht if.";
    case Skill::Shared: return "shared[\"name\"] - Werte, die einen Neustart überleben.";
    case Skill::Variable: return "Eigene Variablen: int, float, bool, auto ... Erstmal genau eine.";
    case Skill::Class: return "struct und class. Erstmal genau eine.";
    case Skill::Function: return "Eigene Funktionen und Methoden. Erstmal genau eine.";
    case Skill::Wash: return "item.wash(\"Stein\") - waschen. Der erste Schritt, der einen Block mehr wert macht.";
    case Skill::Smelt: return "item.smelt(\"Kupfer\") - schmelzen. Kostet Reinheit, bringt aber viel Wert.";
    case Skill::Cast: return "item.cast(\"Kupfer\") - gießen. Geht nur mit Geschmolzenem.";
    case Skill::Clean: return "item.clean(\"Stein\") - reinigen. Der große Sprung bei der Reinheit.";
    case Skill::Polish: return "item.polish(\"Diamant\") - polieren. Braucht etwas Gegossenes, Gereinigtes oder Gehärtetes.";
    case Skill::Harden: return "item.harden(\"Eisen\") - härten. Aus mehreren Zuständen heraus möglich.";
    case Skill::Refine: return "item.refine(\"Gold\") - veredeln. Das Wertvollste, was ein einzelnes Erz werden kann.";
    case Skill::Press: return "item.press(\"Kohle\") - pressen. Schnell und billig, dafür wenig Gewinn.";
    case Skill::Alloy: return "item.alloy(\"Elektrum\") - zwei Erze zu einem neuen Stoff verschmelzen, der mehr wert ist als beide zusammen. item.canAlloy(\"Elektrum\") sagt vorher, wie viele gingen. Braucht schmelzen.";
    case Skill::ExtraLoop: return "Eine Schleife mehr im Code erlaubt.";
    case Skill::ExtraIf: return "Eine Bedingung mehr im Code erlaubt.";
    case Skill::ExtraConsole: return "Eine Konsole mehr.";
    case Skill::ExtraVariable: return "Eine Variable mehr im Code erlaubt.";
    case Skill::ExtraClass: return "Eine Klasse mehr im Code erlaubt.";
    case Skill::ExtraFunction: return "Eine Funktion mehr im Code erlaubt.";
    case Skill::Speed: return "+0,5 Zeilen pro Sekunde.";
    case Skill::MoneyPerBlock: return "+1 Geld pro abgebautem Block.";
    case Skill::FasterRespawn: return "Der Block wächst 8% schneller nach.";
    case Skill::None: return "";
    }
    return "";
}

const char* SkillTag(Skill skill)
{
    switch (skill)
    {
    case Skill::Root: return ">_";
    case Skill::Mine: return "M";
    case Skill::Sell: return "$>";
    case Skill::While: return "W";
    case Skill::If: return "IF";
    case Skill::Else: return "EL";
    case Skill::For: return "FO";
    case Skill::Print: return "PR";
    case Skill::Check: return "?";
    case Skill::Bag: return "[]";
    case Skill::Shared: return "SH";
    case Skill::Variable: return "x=";
    case Skill::Class: return "{}";
    case Skill::Function: return "f()";
    case Skill::Wash: return "WA";
    case Skill::Smelt: return "SM";
    case Skill::Cast: return "CA";
    case Skill::Clean: return "CL";
    case Skill::Polish: return "PO";
    case Skill::Harden: return "HA";
    case Skill::Refine: return "RF";
    case Skill::Press: return "PS";
    case Skill::Alloy: return "AL";
    case Skill::ExtraLoop: return "+L";
    case Skill::ExtraIf: return "+I";
    case Skill::ExtraConsole: return "+K";
    case Skill::ExtraVariable: return "+x";
    case Skill::ExtraClass: return "+{}";
    case Skill::ExtraFunction: return "+f";
    case Skill::Speed: return ">>";
    case Skill::MoneyPerBlock: return "$";
    case Skill::FasterRespawn: return "R";
    case Skill::None: return "-";
    }
    return "?";
}

void SkillTree::start(const SkillPlan& aPlan, unsigned seed)
{
    plan     = aPlan;
    problems = aPlan.problems;
    rng.seed(seed);

    nodes.clear();
    taken.clear();
    branches.clear();
    selected = -1;

    usedOnce.assign(plan.rules.size(), false);
    sinceOnce = 1000;

    SkillNode root;
    root.id    = 0;
    root.skill = Skill::Root;
    root.owned = true;
    nodes.push_back(root);
    branches.push_back(0);
    taken.insert(Cell(0, 0));

    // Der erste Schritt liegt sofort da - sonst waere die Seite leer.
    grow(0);
}

void SkillTree::grow(int id)
{
    if (id < 0 || id >= (int)nodes.size())
        return;

    const int step = nodes[(std::size_t)id].depth + 1;
    if (plan.steps > 0 && step > plan.steps)
        return;  // hier endet der Baum (bei "schritte 0" nie)

    // Wie viele Abzweigungen dieser Kauf aufmacht.
    int wanted = plan.minKids;
    if (plan.maxKids > plan.minKids)
        wanted += (int)(rng() % (unsigned)(plan.maxKids - plan.minKids + 1));

    // Stehen schon genug Karten offen, kommt nur noch eine nach. Sonst waechst
    // der Baum schneller, als man kaufen kann, und die Seite ist zu.
    int open = 0;
    for (const SkillNode& n : nodes)
        if (!n.owned)
            ++open;
    if (open >= plan.maxOpen)
        wanted = 1;

    // Passt eine Regel ueberhaupt hierher?
    //
    // - der Schritt muss im Bereich liegen
    // - "einmal" darf es nur geben, solange es den Punkt noch nicht gibt
    // - zwei einmalige Punkte brauchen Abstand: erst das eine freischalten,
    //   ein Stueck weiterspielen, dann das naechste
    // - "braucht" muss GEKAUFT sein: eine zweite Bedingung ohne "if" waere
    //   sinnlos, und genau deshalb taucht sie vorher auch nicht auf
    auto fits = [&](std::size_t i, bool ohneAbstand)
    {
        const SkillRule& r = plan.rules[i];

        if (step < r.minStep)
            return false;

        // Nach dem Bereich ist Schluss - ausser bei einmaligen Sachen, die es
        // noch gar nicht gibt. Die soll man nicht fuer immer verlieren, bloss
        // weil zufaellig nie Platz war.
        if (step > r.maxStep && !r.once)
            return false;

        if (r.once && usedOnce[i])
            return false;

        // Ein fester Platz ("schritt 3" statt "3-8") gilt genau dort - der
        // Abstand darf ihn nicht wegdruecken, sonst faellt er ganz aus.
        const bool fest = (r.minStep == r.maxStep);

        if (r.once && !fest && !ohneAbstand && sinceOnce < plan.spacing)
            return false;

        if (r.needs != Skill::None && !owns(r.needs))
            return false;

        return true;
    };

    // Je weiter man rauskommt, desto wahrscheinlicher wird ein einmaliger
    // Punkt: am Anfang seines Bereichs selten, am Ende fast sicher.
    auto weightOf = [&](std::size_t i)
    {
        const SkillRule& r = plan.rules[i];

        // Einmalige Punkte werden nach hinten raus wahrscheinlicher, damit sie
        // ihren Bereich nicht ungenutzt verstreichen lassen.
        if (r.once)
            return r.weight * (1 + step - r.minStep);

        // Alles andere folgt seinem "wachstum": ueber 1 wird es draussen
        // haeufiger, unter 1 seltener.
        double w = (double)r.weight;
        for (int k = r.minStep; k < step; ++k)
            w *= (double)r.weightGrowth;

        if (w < 1.0)
            w = 1.0;
        if (w > 100000.0)
            w = 100000.0;

        return (int)(w + 0.5);
    };

    std::vector<std::size_t> forced;  // letzte Gelegenheit
    for (std::size_t i = 0; i < plan.rules.size(); ++i)
        if (plan.rules[i].once && !usedOnce[i] && plan.rules[i].maxStep == step && fits(i, true))
            forced.push_back(i);

    for (int made = 0; made < wanted; ++made)
    {
        std::size_t pick = plan.rules.size();

        if (!forced.empty())
        {
            pick = forced.back();
            forced.pop_back();
        }
        else
        {
            int total = 0;
            for (std::size_t i = 0; i < plan.rules.size(); ++i)
                if (fits(i, false))
                    total += weightOf(i);

            if (total <= 0)
                break;  // hier passt nichts mehr

            int roll = (int)(rng() % (unsigned)total);
            for (std::size_t i = 0; i < plan.rules.size(); ++i)
            {
                if (!fits(i, false))
                    continue;
                roll -= weightOf(i);
                if (roll < 0)
                {
                    pick = i;
                    break;
                }
            }
        }

        if (pick >= plan.rules.size())
            break;

        const SkillRule& rule = plan.rules[pick];

        SkillNode n;
        n.id     = (int)nodes.size();
        n.skill  = rule.skill;
        n.depth  = step;
        n.parent = id;
        n.cost   = CostFor(rule.price, step, plan.growth);

        if (!PlaceNextTo(nodes, taken, branches, id, n))
            break;  // rundherum alles voll

        if (rule.once)
        {
            usedOnce[pick] = true;
            sinceOnce      = 0;  // ab hier zaehlt der Abstand wieder von vorn
        }
        else
        {
            ++sinceOnce;
        }

        nodes.push_back(n);
        branches.push_back(0);
    }
}

void SkillTree::rebuildCells()
{
    // Alte Spielstaende koennen Preise enthalten, die inzwischen ueber dem
    // Deckel liegen. Die werden hier mitgezogen.
    for (SkillNode& n : nodes)
        if (n.cost > kMaxCost)
            n.cost = kMaxCost;

    taken.clear();
    branches.assign(nodes.size(), 0);

    for (const SkillNode& n : nodes)
    {
        taken.insert(Cell(n.gx, n.gy));

        if (n.parent < 0)
            continue;

        ++branches[(std::size_t)n.parent];

        // Haengt ein Knoten zwei Zellen vom Elternteil weg, laeuft die Linie
        // durch die Zelle dazwischen. Die muss belegt bleiben.
        const SkillNode& p = nodes[(std::size_t)n.parent];
        if (std::abs(n.gx - p.gx) + std::abs(n.gy - p.gy) == 2)
            taken.insert(Cell((n.gx + p.gx) / 2, (n.gy + p.gy) / 2));
    }
}

bool SkillTree::reachable(int id) const
{
    if (id < 0 || id >= (int)nodes.size())
        return false;
    const SkillNode& n = nodes[(std::size_t)id];
    if (n.parent < 0)
        return true;
    return nodes[(std::size_t)n.parent].owned;
}

bool SkillTree::visible(int id) const
{
    if (id < 0 || id >= (int)nodes.size())
        return false;
    // Gekauft, oder genau ein Schritt dahinter. Weiter gibt es noch gar nichts:
    // was danach kommt, wird erst beim Kaufen gewuerfelt.
    return nodes[(std::size_t)id].owned || reachable(id);
}

bool SkillTree::canBuy(int id, int money) const
{
    if (id < 0 || id >= (int)nodes.size())
        return false;
    const SkillNode& n = nodes[(std::size_t)id];
    return !n.owned && reachable(id) && money >= n.cost;
}

bool SkillTree::buy(int id, World& world)
{
    if (!canBuy(id, world.money))
        return false;

    world.money -= nodes[(std::size_t)id].cost;
    nodes[(std::size_t)id].owned = true;

    // Jetzt erst wird der naechste Schritt gewuerfelt - mit dem, was man ab
    // sofort besitzt.
    grow(id);
    return true;
}

bool SkillTree::owns(Skill skill) const
{
    for (const SkillNode& n : nodes)
        if (n.owned && n.skill == skill)
            return true;
    return false;
}

Limits SkillTree::limits() const
{
    Limits limits;

    // Die Startwerte stehen in data/skills.txt, nicht hier.
    limits.linesPerSecond = plan.speedStart;
    limits.respawnSeconds = plan.respawnStart;

    int loops     = 0;
    int ifs       = 0;
    int consoles  = 1;
    int variables = 0;
    int classes   = 0;
    int functions = 0;

    for (const SkillNode& n : nodes)
    {
        if (!n.owned)
            continue;

        switch (n.skill)
        {
        // Jede Freischaltung bringt genau eine Verwendung mit.
        case Skill::While:
            limits.allowWhile = true;
            ++loops;
            break;
        case Skill::For:
            limits.allowFor = true;
            ++loops;
            break;
        case Skill::If:
            limits.allowIf = true;
            ++ifs;
            break;
        case Skill::Variable:
            limits.allowVariable = true;
            ++variables;
            break;
        case Skill::Class:
            limits.allowClass = true;
            ++classes;
            break;
        case Skill::Function:
            limits.allowFunction = true;
            ++functions;
            break;

        case Skill::Mine: limits.allowMine = true; break;
        case Skill::Sell: limits.allowSell = true; break;
        case Skill::Bag: limits.allowBag = true; break;
        case Skill::Else: limits.allowElse = true; break;
        case Skill::Print: limits.allowPrint = true; break;
        case Skill::Check: limits.allowCheck = true; break;
        case Skill::Shared: limits.allowShared = true; break;

        case Skill::Wash: limits.allowWash = true; break;
        case Skill::Smelt: limits.allowSmelt = true; break;
        case Skill::Cast: limits.allowCast = true; break;
        case Skill::Clean: limits.allowClean = true; break;
        case Skill::Polish: limits.allowPolish = true; break;
        case Skill::Harden: limits.allowHarden = true; break;
        case Skill::Refine: limits.allowRefine = true; break;
        case Skill::Press: limits.allowPress = true; break;
        case Skill::Alloy: limits.allowAlloy = true; break;

        case Skill::ExtraLoop: ++loops; break;
        case Skill::ExtraIf: ++ifs; break;
        case Skill::ExtraConsole: ++consoles; break;
        case Skill::ExtraVariable: ++variables; break;
        case Skill::ExtraClass: ++classes; break;
        case Skill::ExtraFunction: ++functions; break;

        case Skill::Speed: limits.linesPerSecond += plan.speedPlus; break;
        case Skill::MoneyPerBlock: limits.moneyPerBlock += plan.moneyPlus; break;
        case Skill::FasterRespawn: limits.respawnSeconds *= plan.respawnMul; break;

        default: break;
        }
    }

    limits.maxLoops     = loops;
    limits.maxIfs       = ifs;
    limits.maxConsoles  = consoles;
    limits.maxVariables = variables;
    limits.maxClasses   = classes;
    limits.maxFunctions = functions;
    return limits;
}
