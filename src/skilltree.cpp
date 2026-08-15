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
    case Skill::Root: return "Console";
    case Skill::Mine: return "block.mine()";
    case Skill::Care: return "block.ore()";
    case Skill::Sell: return "item.sell()";
    case Skill::While: return "while";
    case Skill::If: return "if";
    case Skill::Else: return "else";
    case Skill::For: return "for";
    case Skill::Print: return "print()";
    case Skill::Check: return "block.isThere()";
    case Skill::Bag: return "item.has()";
    case Skill::Shared: return "shared[...]";
    case Skill::Variable: return "Variables";
    case Skill::Class: return "Classes";
    case Skill::Function: return "Functions";
    case Skill::Wash: return "item.wash()";
    case Skill::Smelt: return "item.smelt()";
    case Skill::Cast: return "item.cast()";
    case Skill::Clean: return "item.clean()";
    case Skill::Polish: return "item.polish()";
    case Skill::Harden: return "item.harden()";
    case Skill::Refine: return "item.refine()";
    case Skill::Press: return "item.press()";
    case Skill::Alloy: return "item.alloy()";
    case Skill::ExtraLoop: return "+1 loop";
    case Skill::ExtraIf: return "+1 condition";
    case Skill::ExtraConsole: return "+1 console";
    case Skill::ExtraVariable: return "+1 variable";
    case Skill::ExtraClass: return "+1 class";
    case Skill::ExtraFunction: return "+1 function";
    case Skill::Speed: return "Speed";
    case Skill::MoneyPerBlock: return "Money per block";
    case Skill::FasterRespawn: return "Regrow";

    case Skill::Switch: return "switch";
    case Skill::Ternary: return "a ? b : c";
    case Skill::Goto: return "goto";
    case Skill::Recursion: return "Recursion";
    case Skill::Container: return "Containers";
    case Skill::Inline: return "inline";
    case Skill::Optimize: return "-O2";
    case Skill::Info: return "info()";
    case Skill::Assay: return "assay()";
    case Skill::Count: return "item.count()";
    case Skill::JobQuery: return "job.busy()";
    case Skill::Wait: return "wait()";
    case Skill::Status: return "money()";
    case Skill::Market: return "market.price()";
    case Skill::ExtraFurnace: return "+1 furnace";
    case Skill::Quests: return "Contracts";
    case Skill::Etch: return "item.etch()";
    case Skill::Fuse: return "item.fuse()";

    case Skill::None: return "-";
    }
    return "?";
}

const char* SkillInfo(Skill skill)
{
    switch (skill)
    {
    case Skill::Root: return "Your first console. At the start you can only mine by hand - click the block.";
    case Skill::Mine: return "block.mine() - let a program mine instead of clicking. The first step towards a machine.";
    case Skill::Care: return "block.is(Gold) and block.mine(Cool) - valuable ores want to be cooled or heated, every one of them the same way for ever. The wrong treatment is worse than none. Needs if.";
    case Skill::Sell: return "item.sell() sells everything in your bag at once. Without it you sell by hand.";
    case Skill::While: return "while and do. Comes with your first loop.";
    case Skill::If: return "if. Comes with your first condition.";
    case Skill::Else: return "else. Needs if.";
    case Skill::For: return "for. Comes with one more loop - all loops count together.";
    case Skill::Print: return "print(...) writes into the line below the editor.";
    case Skill::Check: return "block.isThere() and block.isLoading() - check whether the block is there.";
    case Skill::Bag: return "item.has(Stone) - look into your bag. With a count: item.has(Stone, 5). Needs if.";
    case Skill::Shared: return "shared[\"name\"] - values that survive a restart.";
    case Skill::Variable: return "Your own variables: int, float, bool, auto ... exactly one for now.";
    case Skill::Class: return "struct and class. Exactly one for now.";
    case Skill::Function: return "Your own functions and methods. Exactly one for now.";
    case Skill::Wash: return "item.wash(Stone) - washing. The first step that makes a block worth more.";
    case Skill::Smelt: return "item.smelt(Copper) - smelting. Costs purity, but adds a lot of value.";
    case Skill::Cast: return "item.cast(Copper) - casting. Only works on smelted material.";
    case Skill::Clean: return "item.clean(Stone) - cleaning. The big jump in purity.";
    case Skill::Polish: return "item.polish(Diamond) - polishing. Needs something cast, cleaned or hardened.";
    case Skill::Harden: return "item.harden(Iron) - hardening. Possible from several states.";
    case Skill::Refine: return "item.refine(Gold) - refining. The most valuable thing a single ore can become.";
    case Skill::Press: return "item.press(Coal) - pressing. Fast and cheap, but little profit.";
    case Skill::Alloy: return "item.alloy(Electrum) - melt two ores into a new material worth more than both together. item.canAlloy(Electrum) tells you beforehand how many would work. Needs smelting.";
    case Skill::ExtraLoop: return "One more loop allowed in your code.";
    case Skill::ExtraIf: return "One more condition allowed in your code.";
    case Skill::ExtraConsole: return "One more console - and that means one more PROGRAM. Every console with its own main() runs as its own process with its own line budget, so one can mine while the next one sells. It multiplies your throughput instead of adding to it, which is why it is the most expensive point in the tree.";
    case Skill::ExtraVariable: return "One more variable allowed in your code.";
    case Skill::ExtraClass: return "One more class allowed in your code.";
    case Skill::ExtraFunction: return "One more function allowed in your code.";
    case Skill::Speed: return "+0.5 lines per second.";
    case Skill::MoneyPerBlock: return "+1 money per mined block.";
    case Skill::FasterRespawn: return "The block regrows 8% faster.";

    case Skill::Switch: return "switch, case and default. A whole switch is ONE line, no matter how many cases - an else-if chain costs a line per rung. With twenty ores that is the biggest speed-up in the game. Needs if.";
    case Skill::Ternary: return "a ? b : c - a condition that fits inside another line, so it costs no line of its own. It does not count against your conditions either. Needs if.";
    case Skill::Goto: return "goto and labels. The cheapest loop there is: it uses up no loop. What it cannot do is ask a question - a goto only ever jumps. Needs while.";
    case Skill::Recursion: return "A function may call itself. A loop without spending a loop. Needs functions.";
    case Skill::Container: return "vector, array, map and set - many values inside one variable. The container counts as one variable, what is inside it is free. Needs variables.";
    case Skill::Inline: return "Every line INSIDE your own functions costs only half. Without it a function is slower than writing the code twice - with it, it finally pays off. Needs functions.";
    case Skill::Optimize: return "A higher optimisation level: lines per second TIMES 1.2 instead of plus a fixed amount. It multiplies, so it stacks with everything you already own.";
    case Skill::Info: return "info(ore) hands you a pointer to what an ore is: value, rarity and above all the treatment it wants. nullptr means you have not examined it yet. With it, one program works for EVERY ore - even the ones the game invents later. Needs care.";
    case Skill::Assay: return "assay(ore) examines an unknown ore. It takes a moment and costs money, and afterwards info() knows what it wants. That is how you meet a new ore without looking anything up. Needs info().";
    case Skill::Count: return "item.count(Gold) gives you the number, item.purity(Gold) the purity of the stack. has() only ever said yes or no. Needs item.has().";
    case Skill::JobQuery: return "job.busy(), job.free() and job.progress() - is the furnace working, how many are idle, how far along is it. Without it you only find out by trying, and the attempt costs a line. Needs washing.";
    case Skill::Wait: return "wait(0.5) waits without burning lines, block.loading() says how long until the block is back. A waiting loop costs a line every pass - this costs exactly one. Needs block.isThere().";
    case Skill::Status: return "money(), timeLeft() and roundTarget() - how much you have, how many seconds are left, how much the round wants. Now your program can decide for itself when to play it safe.";
    case Skill::Market: return "market.price(Gold) - prices move during the round, market.average(Gold) is the long-run mean. Selling at the right moment is worth real money. Needs item.sell().";
    case Skill::ExtraFurnace: return "One more job at the same time. Processing and alloying share the slots - until now there was exactly one, and that was the hardest ceiling in the game.";
    case Skill::Quests: return "Contracts. Before every round three offers are on the board - take one or none. Each says beforehand what it pays and what it costs if you fail, and it runs for that one round only. It is the first thing in this game you WANT instead of something you fend off. Needs selling.";
    case Skill::Etch: return "item.etch(Gold) - etching. It takes a long time and eats purity, but it is worth far more than refined. The first step past the end of the old chain. Needs refining.";
    case Skill::Fuse: return "item.fuse(Gold) - fusing. The last state there is, and the most valuable by a wide margin. Only works on something etched or hardened. Needs etching.";

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
    case Skill::Care: return "C/H";
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

    case Skill::Switch: return "SW";
    case Skill::Ternary: return "?:";
    case Skill::Goto: return "->";
    case Skill::Recursion: return "f^f";
    case Skill::Container: return "[..]";
    case Skill::Inline: return "IN";
    case Skill::Optimize: return "O2";
    case Skill::Info: return "i()";
    case Skill::Assay: return "AS";
    case Skill::Count: return "#";
    case Skill::JobQuery: return "JOB";
    case Skill::Wait: return "ZZ";
    case Skill::Status: return "$?";
    case Skill::Market: return "MKT";
    case Skill::ExtraFurnace: return "+F";
    case Skill::Quests: return "!";
    case Skill::Etch: return "ET";
    case Skill::Fuse: return "FU";

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
    selected   = -1;
    lastBought = -1;

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

    // Welche "einmal"-Zeilen schon liegen, wird aus dem BAUM zurueckgerechnet
    // und nicht aus dem Spielstand geglaubt.
    //
    // Im Spielstand steht ein Bit je Zeile aus data/skills.txt - und die Datei
    // aendert sich. Kommen Zeilen dazu, ist die geladene Liste zu kurz, und
    // grow() greift daneben: genau das war der Absturz beim Kaufen, nachdem
    // die Datei gewachsen war. Werden Zeilen mittendrin eingefuegt, verrutschen
    // alle Bits dahinter, und ein einmaliger Punkt taucht ein zweites Mal auf.
    //
    // Beides faellt weg, wenn man einfach nachsieht: eine "einmal"-Zeile ist
    // verbraucht, sobald ihr Punkt schon irgendwo im Baum haengt. Damit
    // ueberlebt ein Spielstand jede Aenderung an der Datei.
    usedOnce.assign(plan.rules.size(), false);
    for (std::size_t i = 0; i < plan.rules.size(); ++i)
    {
        if (!plan.rules[i].once)
            continue;

        for (const SkillNode& n : nodes)
            if (n.skill == plan.rules[i].skill)
            {
                usedOnce[i] = true;
                break;
            }
    }

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
    lastBought                   = id;

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
    limits.assayCost      = plan.assayCost;
    limits.assaySeconds   = plan.assaySeconds;

    int loops     = 0;
    int ifs       = 0;
    int consoles  = 1;
    int variables = 0;
    int classes   = 0;
    int functions = 0;
    int jobs      = 1;  // einen Auftragsplatz hat man von Anfang an
    int optimize  = 0;

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
        case Skill::Care: limits.allowCare = true; break;
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

        // Sprache, die es frueher umsonst gab.
        case Skill::Switch: limits.allowSwitch = true; break;
        case Skill::Ternary: limits.allowTernary = true; break;
        case Skill::Goto: limits.allowGoto = true; break;
        case Skill::Recursion: limits.allowRecursion = true; break;
        case Skill::Container: limits.allowContainer = true; break;

        case Skill::Inline: limits.allowInline = true; break;

        // Der einzige Punkt im Baum, der MAL rechnet statt PLUS. Deshalb wird
        // er hier nur gezaehlt und erst ganz zum Schluss angewandt - sonst
        // haenge das Ergebnis daran, in welcher Reihenfolge die Knoten stehen.
        case Skill::Optimize: ++optimize; break;

        case Skill::Info: limits.allowInfo = true; break;
        case Skill::Assay: limits.allowAssay = true; break;
        case Skill::Count: limits.allowCount = true; break;
        case Skill::JobQuery: limits.allowJob = true; break;
        case Skill::Wait: limits.allowWait = true; break;
        case Skill::Status: limits.allowStatus = true; break;
        case Skill::Market: limits.allowMarket = true; break;

        case Skill::ExtraFurnace: ++jobs; break;

        case Skill::Quests: limits.allowQuests = true; break;

        case Skill::Etch: limits.allowEtch = true; break;
        case Skill::Fuse: limits.allowFuse = true; break;

        default: break;
        }
    }

    // Erst hier, damit die Reihenfolge der Knoten nichts am Ergebnis aendert.
    for (int i = 0; i < optimize; ++i)
        limits.linesPerSecond *= plan.optimizeMul;

    limits.maxLoops     = loops;
    limits.maxIfs       = ifs;
    limits.maxConsoles  = consoles;
    limits.maxVariables = variables;
    limits.maxClasses   = classes;
    limits.maxFunctions = functions;
    limits.maxJobs      = jobs;
    return limits;
}
