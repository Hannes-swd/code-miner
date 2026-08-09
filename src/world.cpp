#include "world.h"

#include "alloy.h"
#include "craft.h"
#include "ore.h"
#include "skilltree.h"

#include "imgui.h"

#include <cstdio>

namespace
{

// Wie gross der Block auf dem Schirm ist.
constexpr float kHalf = 62.0f;

// So viele Kaestchen pro Kante. Feiner sieht kaum besser aus, kostet aber
// Zeichenarbeit - 26 mal 26 sind schon 676 Rechtecke.
constexpr int kCells = 26;

// Wie hell ein Kaestchen wird, 0..1.
//
// Zwei Lagen Rauschen: eine grobe fuer den Grund (das Gestein "wolkt" leicht)
// und eine feine fuer die Adern. Wo die feine Lage ueber der Schwelle liegt,
// blitzt die helle Farbe durch - das sind die Erzadern. Ohne die Schwelle
// saehe alles nur nach Nebel aus.
float OrePixel(float x, float y, float pattern, unsigned seed)
{
    const float grund = OreNoise(x * pattern * 0.9f, y * pattern * 0.9f, seed);
    const float ader  = OreNoise(x * pattern * 2.2f + 13.0f, y * pattern * 2.2f + 7.0f,
                                seed ^ 0x9E3779B9u);

    float t = grund * 0.42f;  // ruhiger Grund

    const float schwelle = 0.58f;
    if (ader > schwelle)
        t = 0.62f + (ader - schwelle) * 2.6f;  // Ader: deutlich heller

    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

}  // namespace

// Steht ausserhalb des namenlosen Bereichs, weil auch das Wiki Erze anzeigt -
// siehe world.h.
const Ore& OreOf(const OrePlan& ores, int index)
{
    static const Ore ersatz;  // falls die Datei fehlt: schlichter grauer Stein
    if (index < 0 || index >= (int)ores.ores.size())
        return ersatz;
    return ores.ores[(std::size_t)index];
}

namespace
{

// Aus einer Zustands-Liste ("ein Bit je OreState") lesbaren Text machen:
// "Geschmolzen oder Legiert".
std::string StatesText(unsigned bits)
{
    std::string out;
    for (int i = 0; i < (int)OreState::Count; ++i)
    {
        if ((bits & (1u << (unsigned)i)) == 0)
            continue;
        if (!out.empty())
            out += " oder ";
        out += OreStateName((OreState)i);
    }
    return out.empty() ? std::string("-") : out;
}

ImU32 Mix(const Color& a, const Color& b, float t, int alpha)
{
    const int r = (int)((float)a.r + ((float)b.r - (float)a.r) * t);
    const int g = (int)((float)a.g + ((float)b.g - (float)a.g) * t);
    const int bl = (int)((float)a.b + ((float)b.b - (float)a.b) * t);
    return IM_COL32(r, g, bl, alpha);
}

}  // namespace

// Ein Erz als kleines Bild - dieselben Farben und dasselbe Muster wie der
// Block in der Welt. Der Startwert haengt am Erz und nicht am Zufall: derselbe
// Stoff sieht in der Tasche immer gleich aus - und im Wiki auch, deshalb steht
// die Funktion in world.h.
void DrawOreTile(ImDrawList* dl, ImVec2 pos, float size, const Ore& erz, int ore, int zellen)
{
    const float st = size / (float)zellen;
    for (int gy = 0; gy < zellen; ++gy)
        for (int gx = 0; gx < zellen; ++gx)
        {
            const float t = OrePixel((float)gx / (float)zellen, (float)gy / (float)zellen,
                                     erz.pattern, 777u + (unsigned)ore * 31u);
            const ImVec2 q(pos.x + (float)gx * st, pos.y + (float)gy * st);
            dl->AddRectFilled(q, ImVec2(q.x + st + 0.6f, q.y + st + 0.6f),
                              Mix(erz.color2, erz.color1, t, 255));
        }

    dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(16, 18, 22, 255), 5.0f, 0, 2.0f);
}

namespace
{

// Was auf der Taschen-Seite gerade in den Zaehlern steht. Beim Betreten wird
// alles auf die volle Anzahl gesetzt - dann ist ein Klick auf "Verkaufen"
// dasselbe wie "alles davon verkaufen".
struct SellUi
{
    std::map<World::Item, int> menge;
    World::Item                editing;      // in welcher Karte man tippt
    bool                       tippt = false;
    bool                       focus = false;
    int                        lastFrame = -10;
};

SellUi g_sell;

}  // namespace

bool World::mine()
{
    // In der Vorbereitung steht die Welt still. Ein Programm laeuft dort
    // ohnehin nicht - der Riegel ist trotzdem hier, damit es nur EINE Stelle
    // gibt, die das entscheidet.
    if (frozen)
        return false;

    if (!blockAlive)
        return false;

    // Schon dabei? Dann laeuft der Abbau einfach weiter. Mehrfach draufhauen
    // macht ihn nicht schneller.
    if (mining)
        return true;

    mining    = true;
    mineTimer = 0.0f;
    byHand    = false;
    return true;
}

bool World::mineByHand()
{
    // Der Handabbau ist die eine Ausnahme in der Vorbereitung: ganz am Anfang
    // hat man weder Programm noch Geld, der erste Block muss von Hand kommen.
    if (frozen && !handMine)
        return false;

    if (!blockAlive)
        return false;

    if (!mining)
    {
        mining    = true;
        mineTimer = 0.0f;
    }

    // Von Hand angefangen heisst: laeuft weiter, auch ohne Programm.
    byHand = true;
    return true;
}

int StartPurity(const OrePlan& ores, const CraftPlan& craft, int ore)
{
    // Steht beim Erz nichts, gilt der Wert aus data/verarbeitung.json.
    const int eigen = OreOf(ores, ore).purity;
    return (eigen >= 0) ? eigen : craft.startPurity;
}

int StackValue(const OrePlan& ores, const CraftPlan& craft, int ore, int state, int purity,
               int anzahl, int moneyPerBlock)
{
    if (anzahl <= 0)
        return 0;

    const double stueck = (double)OreOf(ores, ore).value * (double)craft.valueOf(state) *
                          (double)craft.purityFactor(purity) * (double)moneyPerBlock;

    // Mindestens 1 pro Stueck: sonst waere ein oxidierter Stein gratis, und
    // "verkaufen" haette gar keine Wirkung mehr.
    int je = (int)(stueck + 0.5);
    if (je < 1)
        je = 1;
    return je * anzahl;
}

void World::noteOre(int ore, int state, int purity)
{
    if (purity < 0)
        purity = 0;
    if (purity > 100)
        purity = 100;

    // Nur der erste Fund zaehlt: von ihm aus rechnet das Wiki alle Wege, und
    // der Anfang soll sich nicht mehr aendern, nur weil spaeter ein Stapel
    // mit anderer Reinheit dazukommt.
    if (oreFirst.find(ore) == oreFirst.end())
        oreFirst[ore] = OreFirst{state, purity};
}

void World::addToBag(Item was, int anzahl, int reinheit)
{
    if (anzahl <= 0)
        return;
    if (reinheit < 0)
        reinheit = 0;
    if (reinheit > 100)
        reinheit = 100;

    Stack& stapel = inventory[was];

    // Gewichtet nach Anzahl: zehn schmutzige und ein sauberes Stueck ergeben
    // keinen halb sauberen Stapel.
    const long long summe = (long long)stapel.count * stapel.purity + (long long)anzahl * reinheit;
    stapel.count += anzahl;
    stapel.purity = (int)(summe / stapel.count);
}

int World::inventoryCount() const
{
    int summe = 0;
    for (const auto& e : inventory)
        summe += e.second.count;
    return summe;
}

int World::bagCount(int ore, unsigned states) const
{
    int summe = 0;
    for (const auto& e : inventory)
        if (e.first.ore == ore && (states & (1u << (unsigned)e.first.state)) != 0)
            summe += e.second.count;
    return summe;
}

int World::sell(const OrePlan& ores, const CraftPlan& craft, Item was, int anzahl)
{
    const auto it = inventory.find(was);
    if (it == inventory.end() || it->second.count <= 0)
        return 0;

    int wie = (anzahl < 0) ? it->second.count : anzahl;
    if (wie > it->second.count)
        wie = it->second.count;
    if (wie <= 0)
        return 0;

    const int geld =
        StackValue(ores, craft, was.ore, was.state, it->second.purity, wie, moneyPerBlock);
    money += geld;

    it->second.count -= wie;
    if (it->second.count <= 0)
        inventory.erase(it);

    lastSold = geld;
    sellFx   = 1.0f;
    return geld;
}

int World::inventoryOf(const OrePlan& ores, const std::string& name) const
{
    const int nummer = FindOre(ores, name);
    if (nummer < 0)
        return 0;

    // Ueber alle Zustaende: item.has("Stein") fragt nach dem Erz, nicht
    // danach, ob es gewaschen oder geschmolzen ist.
    int summe = 0;
    for (const auto& e : inventory)
        if (e.first.ore == nummer)
            summe += e.second.count;
    return summe;
}

int World::sell(const OrePlan& ores, const CraftPlan& craft, const std::string& name, int anzahl)
{
    const int nummer = FindOre(ores, name);
    if (nummer < 0)
        return 0;  // Erz gibt es nicht - dann eben kein Geld

    // Weniger als gewuenscht ist in Ordnung: es wird verkauft, was da ist.
    // Angefangen wird beim rohen Zeug - das Bearbeitete behaelt man lieber.
    int offen = anzahl;
    int geld  = 0;

    for (auto it = inventory.begin(); it != inventory.end();)
    {
        if (it->first.ore != nummer || (anzahl >= 0 && offen <= 0))
        {
            ++it;
            continue;
        }

        int wie = (anzahl < 0) ? it->second.count : offen;
        if (wie > it->second.count)
            wie = it->second.count;

        geld += StackValue(ores, craft, nummer, it->first.state, it->second.purity, wie,
                           moneyPerBlock);
        if (anzahl >= 0)
            offen -= wie;

        it->second.count -= wie;
        if (it->second.count <= 0)
            it = inventory.erase(it);
        else
            ++it;
    }

    if (geld > 0)
    {
        money += geld;
        lastSold = geld;
        sellFx   = 1.0f;
    }
    return geld;
}

int World::sell(const OrePlan& ores, const CraftPlan& craft)
{
    int geld = 0;
    for (const auto& e : inventory)
        geld += StackValue(ores, craft, e.first.ore, e.first.state, e.second.purity, e.second.count,
                           moneyPerBlock);

    inventory.clear();
    money += geld;

    if (geld > 0)
    {
        lastSold = geld;
        sellFx   = 1.0f;
    }
    return geld;
}

void World::tickMining(float dt, const OrePlan& ores, const CraftPlan& craft)
{
    // Steht die Welt still, kommt auch ein angefangener Abbau nicht voran -
    // ausser er wurde von Hand begonnen und Handabbau ist erlaubt.
    if (frozen && !(byHand && handMine))
        return;

    if (!blockAlive || !mining)
        return;

    mineTimer += dt;

    const Ore& erz = OreOf(ores, ore);
    if (mineTimer < erz.mineSeconds)
        return;

    // Der Block wandert in die Tasche - Geld gibt es erst beim Verkaufen.
    // Frisch abgebaut heisst roh, alles andere kommt erst durch Bearbeiten.
    Item frisch;
    frisch.ore   = ore;
    frisch.state = (int)OreState::Raw;
    addToBag(frisch, 1, StartPurity(ores, craft, ore));

    // Ab jetzt kennt man dieses Erz - im Wiki bekommt es eine Seite.
    noteOre(ore, (int)OreState::Raw, StartPurity(ores, craft, ore));

    lastOre = ore;
    ++minedCount;

    blockAlive   = false;
    mining       = false;
    byHand       = false;
    mineTimer    = 0.0f;
    respawnTimer = respawnSeconds;
    fx           = 1.0f;
}

void World::cancelMining()
{
    mining    = false;
    mineTimer = 0.0f;
    byHand    = false;
}

int World::startCraft(const OrePlan& ores, const Limits& limits, const CraftStep& step, Item was,
                      int anzahl, bool byHandStart)
{
    // In der Vorbereitung wird nicht gearbeitet - sonst waere die Werkstatt
    // ein Weg, die Uhr zu umgehen.
    if (frozen)
        return 0;

    // Nur ein Auftrag gleichzeitig. Ein zweiter Aufruf macht nichts - so muss
    // sich der Spielercode selbst darum kuemmern, zu warten.
    if (crafting)
        return 0;

    if (!CraftUnlocked(step, limits))
        return 0;
    if (!step.fits(was.state))
        return 0;

    // Diamant schmilzt man nicht: was ein Erz werden darf, steht bei ihm.
    if (!OreOf(ores, was.ore).allows((OreState)step.to))
        return 0;

    const auto it = inventory.find(was);
    if (it == inventory.end() || it->second.count <= 0)
        return 0;

    int wie = (anzahl < 0) ? it->second.count : anzahl;
    if (wie > it->second.count)
        wie = it->second.count;
    if (wie <= 0)
        return 0;

    craftItem    = was;
    craftCount   = wie;
    craftPurity  = it->second.purity;
    craftTo      = step.to;
    craftDelta   = step.purity;
    craftName    = step.name;
    craftSeconds = step.seconds * (float)wie;
    craftTimer   = 0.0f;
    crafting     = true;
    craftByHand  = byHandStart;

    // Aus der Tasche heraus: waehrend der Arbeit gehoert es der Werkstatt.
    // Gemerkt wird es trotzdem - ein Abbruch muss es zurueckgeben koennen.
    craftTaken.clear();
    craftTaken.push_back({was, wie, it->second.purity});

    it->second.count -= wie;
    if (it->second.count <= 0)
        inventory.erase(it);

    return wie;
}

int World::startCraft(const OrePlan& ores, const CraftPlan& craft, const Limits& limits,
                      const std::string& befehl, const std::string& erz, int anzahl)
{
    const CraftStep* step = craft.find(befehl);
    if (step == nullptr)
        return 0;

    const int nummer = FindOre(ores, erz);
    if (nummer < 0)
        return 0;

    // Aus dem Code kommt nur der Name. Genommen wird der erste Stapel, aus dem
    // der Schritt ueberhaupt geht - die Zustaende stehen der Reihe nach, roh
    // kommt also zuerst.
    for (const auto& e : inventory)
    {
        if (e.first.ore != nummer || !step->fits(e.first.state))
            continue;
        const int wie = startCraft(ores, limits, *step, e.first, anzahl, false);
        if (wie > 0)
            return wie;
    }

    return 0;
}

// ---- Legieren -------------------------------------------------------------
//
// Es benutzt denselben einen Auftrags-Platz wie das Verarbeiten: es ist
// derselbe Ofen, und der kann nur eines auf einmal.

bool World::alloyPick(const AlloyRecipe& rezept, int anzahl, std::vector<Taken>& out,
                      int& reinheit) const
{
    out.clear();
    reinheit = 0;

    if (anzahl <= 0 || rezept.parts.empty())
        return false;

    // Die Reinheit des Ergebnisses ist das nach Anzahl gewichtete Mittel der
    // Zutaten: ein sauberes Stueck rettet keinen Haufen schmutziger.
    long long summe  = 0;
    long long stueck = 0;

    for (const AlloyPart& p : rezept.parts)
    {
        int offen = p.count * anzahl;

        for (const auto& e : inventory)
        {
            if (offen <= 0)
                break;
            if (e.first.ore != p.ore || !rezept.fits(e.first.state))
                continue;

            const int nimm = (e.second.count < offen) ? e.second.count : offen;
            out.push_back({e.first, nimm, e.second.purity});
            summe += (long long)nimm * e.second.purity;
            stueck += nimm;
            offen -= nimm;
        }

        if (offen > 0)
            return false;  // von dieser Zutat liegt zu wenig da
    }

    int rein = (stueck > 0) ? (int)(summe / stueck) : 0;
    rein += rezept.purity;
    if (rein < 0)
        rein = 0;
    if (rein > 100)
        rein = 100;

    reinheit = rein;
    return true;
}

int World::canAlloy(const OrePlan& ores, const AlloyRecipe& rezept, const Limits& limits) const
{
    if (!limits.allowAlloy || rezept.result < 0 || rezept.parts.empty())
        return 0;

    // Der neue Stoff muss den Zielzustand ueberhaupt kennen.
    if (!OreOf(ores, rezept.result).allows((OreState)rezept.to))
        return 0;

    int moeglich = -1;
    for (const AlloyPart& p : rezept.parts)
    {
        if (p.count <= 0)
            return 0;

        const int wie = bagCount(p.ore, rezept.from) / p.count;
        if (moeglich < 0 || wie < moeglich)
            moeglich = wie;
    }

    return (moeglich > 0) ? moeglich : 0;
}

int World::canAlloy(const OrePlan& ores, const AlloyPlan& alloys, const Limits& limits,
                    const std::string& name) const
{
    const AlloyRecipe* rezept = alloys.find(name);
    return (rezept != nullptr) ? canAlloy(ores, *rezept, limits) : 0;
}

int World::startAlloy(const OrePlan& ores, const AlloyRecipe& rezept, const Limits& limits,
                      int anzahl, bool byHandStart)
{
    if (frozen)
        return 0;

    // Nur ein Auftrag gleichzeitig - genau wie beim Verarbeiten.
    if (crafting)
        return 0;

    const int moeglich = canAlloy(ores, rezept, limits);
    if (moeglich <= 0)
        return 0;

    int wie = (anzahl < 0) ? moeglich : anzahl;
    if (wie > moeglich)
        wie = moeglich;
    if (wie <= 0)
        return 0;

    std::vector<Taken> nehmen;
    int                rein = 0;
    if (!alloyPick(rezept, wie, nehmen, rein))
        return 0;

    // Jetzt erst aus der Tasche nehmen: waehrend der Arbeit gehoert es der
    // Werkstatt und laesst sich nicht verkaufen.
    for (const Taken& t : nehmen)
    {
        const auto it = inventory.find(t.was);
        if (it == inventory.end())
            continue;
        it->second.count -= t.count;
        if (it->second.count <= 0)
            inventory.erase(it);
    }

    craftItem.ore   = rezept.result;
    craftItem.state = rezept.to;
    craftCount      = wie;
    craftPurity     = rein;
    craftTo         = rezept.to;
    craftDelta      = 0;  // der Aufschlag steckt schon in rein
    craftName       = "Legieren";
    craftSeconds    = rezept.seconds * (float)wie;
    craftTimer      = 0.0f;
    crafting        = true;
    craftByHand     = byHandStart;
    craftTaken      = nehmen;

    return wie;
}

int World::startAlloy(const OrePlan& ores, const AlloyPlan& alloys, const Limits& limits,
                      const std::string& name, int anzahl, bool byHandStart)
{
    const AlloyRecipe* rezept = alloys.find(name);
    if (rezept == nullptr)
        return 0;
    return startAlloy(ores, *rezept, limits, anzahl, byHandStart);
}

void World::tickCraft(float dt)
{
    if (frozen)
        return;

    if (!crafting)
        return;

    craftTimer += dt;
    if (craftTimer < craftSeconds)
        return;

    // Beim Legieren ist craftItem schon das Ergebnis-Erz - deshalb tut hier
    // beides dasselbe.
    Item fertig;
    fertig.ore   = craftItem.ore;
    fertig.state = craftTo;
    addToBag(fertig, craftCount, craftPurity + craftDelta);

    // Fuers Wiki: das hier hat der Spieler gerade selbst herausgefunden.
    //
    // Beim Legieren lernt er den neuen Stoff ueberhaupt erst kennen - deshalb
    // steht noteOre vor der Kante. Eine Kante gibt es dabei nicht: aus zwei
    // Erzen wird ein drittes, das ist kein Schritt von einem Zustand in den
    // naechsten.
    noteOre(fertig.ore, fertig.state, craftPurity + craftDelta);
    if (craftTaken.size() == 1 && craftTaken[0].was.ore == fertig.ore &&
        craftTaken[0].was.state != fertig.state)
        oreSteps.insert(OreStep{fertig.ore, craftTaken[0].was.state, fertig.state});

    crafting     = false;
    craftByHand  = false;
    craftTimer   = 0.0f;
    craftCount   = 0;
    craftTaken.clear();
}

void World::cancelCraft()
{
    if (!crafting)
        return;

    // Unveraendert zurueck: ein abgebrochener Auftrag darf nichts kosten.
    for (const Taken& t : craftTaken)
        addToBag(t.was, t.count, t.purity);

    crafting     = false;
    craftByHand  = false;
    craftTimer   = 0.0f;
    craftCount   = 0;
    craftTaken.clear();
}

void World::update(float dt, const OrePlan& ores)
{
    // ---- Nachwachsen ----------------------------------------------------
    // Steht die Welt still, waechst nichts nach - ausser man darf trotzdem von
    // Hand abbauen. Sonst waere der erste Block ein Einwegblock: einmal
    // angeklickt, und bis zum Rundenstart steht da nichts mehr.
    //
    // Die Effekte darunter laufen immer aus: sie sind nur Anzeige, und ein
    // eingefrorener Rahmen mitten im Bild saehe nach Fehler aus.
    // Kein Block und kein laufender Zaehler: das darf es eigentlich nicht
    // geben. Falls doch (alter Spielstand, abgebrochene Runde), waere der
    // Block fuer immer weg - deshalb hier die Notbremse.
    if (!blockAlive && respawnTimer <= 0.0f)
        respawnTimer = (respawnSeconds > 0.0f) ? respawnSeconds : 0.01f;

    if ((!frozen || handMine) && !blockAlive && respawnTimer > 0.0f)
    {
        respawnTimer -= dt;
        if (respawnTimer <= 0.0f)
        {
            respawnTimer = 0.0f;
            blockAlive   = true;

            // Der neue Block wird frisch gewuerfelt: welches Erz, und welches
            // Muster. Deshalb ist nie zweimal dasselbe da.
            if (!ores.empty())
                ore = RollOre(ores, level, rng);
            oreSeed = (unsigned)rng();
        }
    }

    if (fx > 0.0f)
    {
        fx -= dt * 1.4f;
        if (fx < 0.0f)
            fx = 0.0f;
    }

    if (sellFx > 0.0f)
    {
        sellFx -= dt * 0.9f;
        if (sellFx < 0.0f)
            sellFx = 0.0f;
    }
}

void DrawWorld(World& world, const OrePlan& ores, const CraftPlan& craft, const AlloyPlan& alloys,
               const RoundPlan& rounds)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList*    dl = ImGui::GetBackgroundDrawList();

    const ImVec2 c(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                   vp->WorkPos.y + vp->WorkSize.y * 0.5f);
    const ImVec2 a(c.x - kHalf, c.y - kHalf);
    const ImVec2 b(c.x + kHalf, c.y + kHalf);

    const Ore& erz = OreOf(ores, world.ore);

    if (world.blockAlive)
    {
        // Der Block wird aus lauter kleinen Kaestchen gemalt. Wie hell ein
        // Kaestchen ist, sagt das Rauschen - daraus werden die Adern.
        const float step = (b.x - a.x) / (float)kCells;

        for (int gy = 0; gy < kCells; ++gy)
            for (int gx = 0; gx < kCells; ++gx)
            {
                const float t = OrePixel((float)gx / (float)kCells, (float)gy / (float)kCells,
                                         erz.pattern, world.oreSeed);

                const ImVec2 p(a.x + (float)gx * step, a.y + (float)gy * step);
                dl->AddRectFilled(p, ImVec2(p.x + step + 0.6f, p.y + step + 0.6f),
                                  Mix(erz.color2, erz.color1, t, 255));
            }

        // Oberkante heller, unten ein Schatten - dadurch sieht die Flaeche wie
        // ein Block aus und nicht wie ein Aufkleber.
        dl->AddRectFilled(a, ImVec2(b.x, a.y + 10.0f), IM_COL32(255, 255, 255, 34));
        dl->AddRectFilled(ImVec2(a.x, b.y - 12.0f), b, IM_COL32(0, 0, 0, 60));
        dl->AddRect(a, b, IM_COL32(18, 20, 24, 255), 6.0f, 0, 3.0f);

        // Abbau laeuft: ein Balken unter dem Block zeigt, wie weit.
        if (world.mining && erz.mineSeconds > 0.0f)
        {
            const float t = world.mineTimer / erz.mineSeconds;
            const ImVec2 pa(a.x, b.y + 10.0f);
            const ImVec2 pb(b.x, b.y + 16.0f);
            dl->AddRectFilled(pa, pb, IM_COL32(40, 44, 52, 220), 3.0f);
            dl->AddRectFilled(pa, ImVec2(pa.x + (pb.x - pa.x) * t, pb.y),
                              IM_COL32(150, 214, 92, 255), 3.0f);
        }
    }
    else
    {
        // Der Block waechst nach: von unten steigt er wieder auf.
        const float progress = (world.respawnSeconds > 0.0f)
                                   ? 1.0f - (world.respawnTimer / world.respawnSeconds)
                                   : 1.0f;

        const float top = b.y - (b.y - a.y) * progress;
        if (progress > 0.0f)
            dl->AddRectFilled(ImVec2(a.x, top), b, Mix(erz.color2, erz.color1, 0.35f, 120), 6.0f);

        dl->AddRect(a, b, IM_COL32(96, 102, 116, 90), 6.0f, 0, 2.0f);
    }

    // Abbau-Effekt: aufgehender Rahmen plus "+1 Stein" in der Farbe des Erzes.
    // Geld steht da bewusst nicht - das gibt es erst beim Verkaufen.
    if (world.fx > 0.0f)
    {
        const Ore&  letzt = OreOf(ores, world.lastOre);
        const float t     = 1.0f - world.fx;
        const float grow  = kHalf + t * 48.0f;
        const int   alpha = (int)(world.fx * 220.0f);

        dl->AddRect(ImVec2(c.x - grow, c.y - grow), ImVec2(c.x + grow, c.y + grow),
                    Mix(letzt.color2, letzt.color1, 0.9f, alpha), 6.0f, 0, 3.0f);

        char gain[64];
        std::snprintf(gain, sizeof(gain), "+1 %s", letzt.name.c_str());
        const ImVec2 gs = ImGui::CalcTextSize(gain);
        dl->AddText(ImVec2(c.x - gs.x * 0.5f, c.y - kHalf - 18.0f - t * 34.0f),
                    Mix(letzt.color2, letzt.color1, 1.0f, alpha), gain);
    }

    // Verkauft: die Zahl steigt in Richtung Geldanzeige auf.
    if (world.sellFx > 0.0f)
    {
        const float t     = 1.0f - world.sellFx;
        const int   alpha = (int)(world.sellFx * 230.0f);

        char text[48];
        std::snprintf(text, sizeof(text), "+%d Geld", world.lastSold);
        const ImVec2 ts = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y + kHalf + 24.0f - t * 40.0f),
                    IM_COL32(255, 214, 120, alpha), text);
    }

    // ---- Von Hand abbauen -------------------------------------------------
    // Am Anfang hat man noch kein Programm. Ein Klick auf den Block tut
    // dasselbe wie block.mine() - nur eben mit der Maus.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool   ueber = mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y;

    if (ueber && !ImGui::GetIO().WantCaptureMouse)
    {
        if (world.blockAlive)
        {
            dl->AddRect(a, b, IM_COL32(255, 255, 255, 90), 6.0f, 0, 2.0f);

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                world.mineByHand();
        }

        const int rein = StartPurity(ores, craft, world.ore);

        ImGui::BeginTooltip();
        ImGui::TextUnformatted(erz.name.c_str());
        ImGui::TextDisabled("%d Geld beim Verkaufen  -  %.1f s Abbau",
                            StackValue(ores, craft, world.ore, (int)OreState::Raw, rein, 1,
                                       world.moneyPerBlock),
                            erz.mineSeconds);
        ImGui::TextDisabled("Reinheit %d%%  -  verarbeitet ist er mehr wert", rein);
        if (world.blockAlive && (!world.frozen || world.handMine))
            ImGui::TextDisabled("Klick zum Abbauen");
        else if (world.frozen)
            ImGui::TextDisabled("Erst die Runde starten");
        ImGui::EndTooltip();
    }

    // Die Welt steht still. Das muss man sofort sehen - sonst wartet man
    // darauf, dass der Block nachwaechst, und versteht nicht, warum nichts
    // passiert.
    if (world.frozen && world.phase == RoundPhase::Prepare)
    {
        const char* zeile1 = "Vorbereitung  -  die Welt steht still";
        const char* zeile2 = world.handMine
                                 ? "Oben \"Runde starten\". Auf den Block klicken darfst du auch so."
                                 : "Oben \"Runde starten\".";

        const ImVec2 s1 = ImGui::CalcTextSize(zeile1);
        const ImVec2 s2 = ImGui::CalcTextSize(zeile2);
        const float  y0 = b.y + 40.0f;

        dl->AddText(ImVec2(c.x - s1.x * 0.5f, y0), IM_COL32(150, 165, 200, 235), zeile1);
        dl->AddText(ImVec2(c.x - s2.x * 0.5f, y0 + ImGui::GetTextLineHeight() + 4.0f),
                    IM_COL32(120, 130, 152, 235), zeile2);
    }

    // Ein Auftrag laeuft: man soll auch hier sehen, wie weit er ist - sonst
    // wartet man auf der Welt-Seite blind.
    if (world.crafting)
    {
        const float t = (world.craftSeconds > 0.0f) ? world.craftTimer / world.craftSeconds : 1.0f;

        // Beim Legieren steht in craftItem schon das Ergebnis - so liest sich
        // beides richtig: "Waschen: Gold x3" und "Legieren: Elektrum x2".
        char text[128];
        std::snprintf(text, sizeof(text), "%s: %s x%d", world.craftName.c_str(),
                      OreOf(ores, world.craftItem.ore).name.c_str(), world.craftCount);
        const ImVec2 ts = ImGui::CalcTextSize(text);

        const ImVec2 pa(c.x - 90.0f, b.y + 44.0f);
        const ImVec2 pb(c.x + 90.0f, b.y + 50.0f);
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, pa.y - ImGui::GetTextLineHeight() - 4.0f),
                    IM_COL32(180, 200, 240, 235), text);
        dl->AddRectFilled(pa, pb, IM_COL32(40, 44, 52, 220), 3.0f);
        dl->AddRectFilled(pa, ImVec2(pa.x + (pb.x - pa.x) * t, pb.y), IM_COL32(120, 170, 240, 255),
                          3.0f);
    }

    // Stimmt an den Dateien etwas nicht, muss man das sehen - sonst sucht man
    // den Fehler im Spiel statt in der Datei.
    float y = vp->WorkPos.y + 12.0f;
    auto  meldungen = [&](const char* datei, const std::vector<std::string>& liste)
    {
        if (liste.empty())
            return;
        dl->AddText(ImVec2(vp->WorkPos.x + 16.0f, y), IM_COL32(250, 140, 108, 255), datei);
        for (const std::string& p : liste)
        {
            y += ImGui::GetTextLineHeight() + 2.0f;
            dl->AddText(ImVec2(vp->WorkPos.x + 16.0f, y), IM_COL32(250, 140, 108, 255), p.c_str());
        }
        y += ImGui::GetTextLineHeight() + 8.0f;
    };

    meldungen("data/erze.json:", ores.problems);
    meldungen("data/verarbeitung.json:", craft.problems);
    meldungen("data/legierungen.json:", alloys.problems);
    meldungen("data/runden.json:", rounds.problems);
}

void DrawInventory(World& world, const OrePlan& ores, const CraftPlan& craft,
                   const Limits& limits)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.047f, 0.058f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 18.0f));

    if (ImGui::Begin("##tasche", nullptr, flags))
    {
        // Kommt man neu auf die Seite, stehen alle Zaehler wieder auf voller
        // Anzahl. Erkennen kann man das daran, dass die Seite im Bild davor
        // nicht gezeichnet wurde.
        const int frame = ImGui::GetFrameCount();
        if (frame - g_sell.lastFrame > 1)
        {
            g_sell.menge.clear();
            g_sell.tippt = false;
        }
        g_sell.lastFrame = frame;

        // ---- Kopfzeile ----------------------------------------------------
        int wert = 0;
        for (const auto& e : world.inventory)
            wert += StackValue(ores, craft, e.first.ore, e.first.state, e.second.purity,
                               e.second.count, world.moneyPerBlock);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.97f, 0.99f, 1.0f));
        ImGui::TextUnformatted("Tasche");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextDisabled("  %d Blöcke, zusammen %d Geld", world.inventoryCount(), wert);

        if (!world.inventory.empty())
        {
            ImGui::SameLine(ImGui::GetWindowWidth() - 190.0f);
            if (ImGui::Button("Alles verkaufen", ImVec2(160.0f, 0.0f)))
                world.sell(ores, craft);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ---- Laeuft gerade ein Auftrag? ------------------------------------
        // Was in Arbeit ist, liegt nicht in der Tasche. Ohne diese Zeile waere
        // es einfach verschwunden.
        if (world.crafting)
        {
            const float t =
                (world.craftSeconds > 0.0f) ? world.craftTimer / world.craftSeconds : 1.0f;

            ImGui::TextColored(ImVec4(0.62f, 0.74f, 0.96f, 1.0f), "%s: %d x %s",
                               world.craftName.c_str(), world.craftCount,
                               OreOf(ores, world.craftItem.ore).name.c_str());

            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.47f, 0.67f, 0.94f, 1.0f));
            char rest[48];
            std::snprintf(rest, sizeof(rest), "noch %.1f s",
                          (double)(world.craftSeconds - world.craftTimer));
            ImGui::ProgressBar(t, ImVec2(320.0f, 0.0f), rest);
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        if (world.inventory.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Noch nichts abgebaut.");
            ImGui::TextDisabled("Geh auf die Welt-Seite und klick auf den Block -");
            ImGui::TextDisabled("oder lass ein Programm block.mine() machen.");
        }
        else
        {
            ImGui::TextDisabled(
                "Mit den Pfeilen die Anzahl einstellen, auf die Zahl klicken zum Eintippen.");
            ImGui::TextDisabled(
                "Rechtsklick auf eine Karte: verarbeiten - das macht den Stapel wertvoller.");
            ImGui::Spacing();
            ImGui::Spacing();

            // ---- Karten, nebeneinander mit Umbruch -------------------------
            const float kartenBreite = 186.0f;
            const float bild         = 96.0f;
            const float luft         = 16.0f;

            // Die Hoehe wird ausgerechnet, nicht geraten: sonst faellt der
            // Verkaufen-Knopf unten heraus, sobald eine Zeile dazukommt
            // (so ist es der Reinheit ergangen).
            const ImGuiStyle& stil   = ImGui::GetStyle();
            const float       zeile  = ImGui::GetTextLineHeight() + stil.ItemSpacing.y;
            const float       knopf  = ImGui::GetFrameHeight() + stil.ItemSpacing.y;
            const float kartenHoehe  = stil.WindowPadding.y * 2.0f  // Rand oben und unten
                                      + bild + stil.ItemSpacing.y   // Bild vom Erz
                                      + zeile * 4.0f                // Name, Zustand, Reinheit, Anzahl
                                      + stil.ItemSpacing.y          // die Spacing-Zeile davor
                                      + knopf * 2.0f                // Zaehler und Verkaufen
                                      + 4.0f;

            const float platz = ImGui::GetContentRegionAvail().x;
            int         proZeile = (int)((platz + luft) / (kartenBreite + luft));
            if (proZeile < 1)
                proZeile = 1;

            // Erst nach der Schleife handeln, sonst wackelt sie einem unter
            // den Fingern weg.
            World::Item verkaufen;
            int         verkaufenWie = 0;
            bool        verkaufenJa  = false;

            World::Item      arbeitAn;
            const CraftStep* arbeitSchritt = nullptr;
            int              arbeitWie     = 0;

            int spalte = 0;

            for (const auto& e : world.inventory)
            {
                const World::Item stapel = e.first;
                const Ore&        erz    = OreOf(ores, stapel.ore);
                const int         anzahl = e.second.count;
                const int         rein   = e.second.purity;
                const OreState    zust   = (OreState)stapel.state;

                if (spalte > 0)
                    ImGui::SameLine(0.0f, luft);
                ++spalte;
                if (spalte > proZeile)
                {
                    ImGui::NewLine();
                    spalte = 1;
                }

                ImGui::PushID(stapel.ore * 100 + stapel.state);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.141f, 0.157f, 0.188f, 1.0f));
                ImGui::BeginChild("karte", ImVec2(kartenBreite, kartenHoehe),
                                  ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse);

                const float innen = ImGui::GetContentRegionAvail().x;

                // ---- Bild vom Erz -----------------------------------------
                {
                    const ImVec2 ba(ImGui::GetCursorScreenPos().x + (innen - bild) * 0.5f,
                                    ImGui::GetCursorScreenPos().y);
                    DrawOreTile(ImGui::GetWindowDrawList(), ba, bild, erz, stapel.ore, 18);
                    ImGui::Dummy(ImVec2(innen, bild));
                }

                // ---- Name, Zustand, Anzahl --------------------------------
                {
                    const float w = ImGui::CalcTextSize(erz.name.c_str()).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (innen - w) * 0.5f);
                    ImGui::TextUnformatted(erz.name.c_str());
                }

                {
                    const char* zn = OreStateName(zust);
                    const float w  = ImGui::CalcTextSize(zn).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (innen - w) * 0.5f);
                    ImGui::TextDisabled("%s", zn);
                }

                {
                    // Die Reinheit haengt direkt am Preis, also gehoert sie auf
                    // die Karte. Gruen ab sauber, sonst gedaempft.
                    char text[48];
                    std::snprintf(text, sizeof(text), "Reinheit %d%%", rein);
                    const float w = ImGui::CalcTextSize(text).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (innen - w) * 0.5f);
                    ImGui::TextColored(rein >= 70 ? ImVec4(0.62f, 0.82f, 0.96f, 1.0f)
                                                  : ImVec4(0.62f, 0.64f, 0.70f, 1.0f),
                                       "%s", text);
                }

                char haben[48];
                std::snprintf(haben, sizeof(haben), "x %d", anzahl);
                const float hw = ImGui::CalcTextSize(haben).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (innen - hw) * 0.5f);
                ImGui::TextColored(ImVec4(0.70f, 0.89f, 0.48f, 1.0f), "%s", haben);

                ImGui::Spacing();

                // ---- Wie viele verkaufen? ---------------------------------
                auto it = g_sell.menge.find(stapel);
                if (it == g_sell.menge.end())
                    it = g_sell.menge.insert(std::make_pair(stapel, anzahl)).first;

                int& wie = it->second;
                if (wie > anzahl)
                    wie = anzahl;
                if (wie < 1)
                    wie = 1;

                const float pfeil = ImGui::GetFrameHeight();
                const float mitte = innen - 2.0f * pfeil - 2.0f * ImGui::GetStyle().ItemSpacing.x;

                ImGui::PushButtonRepeat(true);  // gedrueckt halten zaehlt weiter
                if (ImGui::ArrowButton("weniger", ImGuiDir_Left) && wie > 1)
                    --wie;
                ImGui::PopButtonRepeat();

                ImGui::SameLine();

                const bool tippeHier = g_sell.tippt && !(g_sell.editing < stapel) &&
                                       !(stapel < g_sell.editing);

                if (tippeHier)
                {
                    ImGui::SetNextItemWidth(mitte);
                    if (g_sell.focus)
                    {
                        ImGui::SetKeyboardFocusHere();
                        g_sell.focus = false;
                    }
                    // Kein EnterReturnsTrue: das mag InputInt nicht. Enter und
                    // Wegklicken fangen wir beide mit IsItemDeactivated ab.
                    ImGui::InputInt("##zahl", &wie, 0, 0);
                    if (ImGui::IsItemDeactivated())
                        g_sell.tippt = false;
                }
                else
                {
                    char zahl[32];
                    std::snprintf(zahl, sizeof(zahl), "%d", wie);
                    if (ImGui::Button(zahl, ImVec2(mitte, 0.0f)))
                    {
                        g_sell.editing = stapel;  // Klick auf die Zahl: selbst tippen
                        g_sell.tippt   = true;
                        g_sell.focus   = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Klick zum Eintippen");
                }

                ImGui::SameLine();

                ImGui::PushButtonRepeat(true);
                if (ImGui::ArrowButton("mehr", ImGuiDir_Right) && wie < anzahl)
                    ++wie;
                ImGui::PopButtonRepeat();

                // ---- Verkaufen --------------------------------------------
                char knopf[64];
                std::snprintf(knopf, sizeof(knopf), "Verkaufen  %d",
                              StackValue(ores, craft, stapel.ore, stapel.state, rein, wie,
                                         world.moneyPerBlock));

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.42f, 0.30f, 1.0f));
                if (ImGui::Button(knopf, ImVec2(innen, 0.0f)))
                {
                    verkaufen    = stapel;
                    verkaufenWie = wie;
                    verkaufenJa  = true;
                }
                ImGui::PopStyleColor();

                // Verarbeiten. Angeboten wird genau das, was von hier aus
                // wirklich geht: der Zustand muss passen, das Erz muss das Ziel
                // erlauben, und gekauft sein muss der Schritt auch.
                if (ImGui::BeginPopupContextWindow("verarbeiten"))
                {
                    ImGui::TextDisabled("Verarbeiten");
                    ImGui::Separator();

                    int moeglich = 0;
                    for (const CraftStep& s : craft.steps)
                    {
                        if (!s.fits(stapel.state) || !erz.allows((OreState)s.to) ||
                            !CraftUnlocked(s, limits))
                            continue;

                        ++moeglich;

                        char zeile[96];
                        std::snprintf(zeile, sizeof(zeile), "%s  ->  %s", s.name.c_str(),
                                      OreStateName((OreState)s.to));

                        if (ImGui::MenuItem(zeile, nullptr, false, !world.crafting &&
                                                                       !world.frozen))
                        {
                            arbeitAn      = stapel;
                            arbeitSchritt = &s;
                            arbeitWie     = wie;
                        }

                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("%d Stück, %.1f s  -  Reinheit %+d%%\nDanach %d Geld",
                                              wie, (double)(s.seconds * (float)wie), s.purity,
                                              StackValue(ores, craft, stapel.ore, s.to,
                                                         rein + s.purity, wie,
                                                         world.moneyPerBlock));
                    }

                    if (moeglich == 0)
                        ImGui::TextDisabled("Von hier aus geht nichts.");
                    else if (world.frozen)
                        ImGui::TextDisabled("Erst die Runde starten.");
                    else if (world.crafting)
                        ImGui::TextDisabled("Es läuft schon ein Auftrag.");

                    ImGui::EndPopup();
                }

                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
                ImGui::PopID();
            }

            if (verkaufenJa)
            {
                world.sell(ores, craft, verkaufen, verkaufenWie);
                g_sell.menge.erase(verkaufen);  // beim naechsten Mal wieder voll
            }

            // Von Hand gestartet: der Auftrag laeuft auch ohne Programm weiter.
            if (arbeitSchritt != nullptr)
            {
                if (world.startCraft(ores, limits, *arbeitSchritt, arbeitAn, arbeitWie, true) > 0)
                    g_sell.menge.clear();
            }
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}
