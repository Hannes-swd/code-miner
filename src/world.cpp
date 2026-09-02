#include "world.h"

#include "alloy.h"
#include "craft.h"
#include "ore.h"
#include "skilltree.h"
#include "theme.h"

#include "imgui.h"

#include <cfloat>
#include <cmath>
#include <cstdio>

namespace
{

// Wie gross der Block auf dem Schirm ist.
constexpr float kHalf = 62.0f;

// Heisst der Name "any"? Das ist kein Erz, sondern "egal was" - siehe das enum
// Ore im Spielercode. Gross- und Kleinschreibung ist egal, wie bei Erznamen.
bool IsAnyName(const std::string& name)
{
    if (name.size() != 3)
        return false;

    std::string klein = name;
    for (char& c : klein)
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');

    return klein == "any";
}

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
            out += " or ";
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

bool World::mine(BlockCare mit)
{
    // Die Behandlung gilt ab sofort, auch mitten im Abbau: wer im naechsten
    // Schleifendurchgang umschwenkt, soll das auch merken.
    mineCare = mit;

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
               int anzahl, int moneyPerBlock, float market)
{
    if (anzahl <= 0)
        return 0;

    const double stueck = (double)OreOf(ores, ore).value * (double)craft.valueOf(state) *
                          (double)craft.purityFactor(purity) * (double)moneyPerBlock *
                          (double)market;

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

int World::inventoryPurity(const OrePlan& ores, const std::string& name) const
{
    const bool alles  = IsAnyName(name) && FindOre(ores, name) < 0;
    const int  nummer = alles ? -1 : FindOre(ores, name);
    if (!alles && nummer < 0)
        return 0;

    // Nach Anzahl gewichtet: zehn schmutzige und ein sauberes Stueck sind im
    // Schnitt schmutzig. Genau so rechnet auch addToBag, wenn zwei Stapel
    // aufeinandertreffen - sonst behauptete die Anzeige etwas anderes als der
    // Verkauf.
    long long summe = 0;
    long long wie   = 0;
    for (const auto& e : inventory)
    {
        if (!alles && e.first.ore != nummer)
            continue;
        summe += (long long)e.second.purity * e.second.count;
        wie += e.second.count;
    }

    return (wie > 0) ? (int)(summe / wie) : 0;
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

    // Zum Tagespreis: was ein Stapel bringt, haengt davon ab, WANN man ihn
    // ueber die Theke schiebt. Genau dafuer gibt es market.price() im
    // Spielercode - sonst waere die Schwankung eine Gemeinheit ohne Gegenmittel.
    const float markt = marketFactor(was.ore);
    const int   geld  = StackValue(ores, craft, was.ore, was.state, it->second.purity, wie,
                                   moneyPerBlock, markt);
    money += geld;

    // Fuer die Auftraege.
    stats.earned += geld;
    stats.soldPieces += wie;
    if (markt > 1.0f)
        stats.soldAbove += wie;
    if (geld > stats.biggestSale)
        stats.biggestSale = geld;

    it->second.count -= wie;
    if (it->second.count <= 0)
        inventory.erase(it);

    lastSold = geld;
    sellFx   = 1.0f;
    return geld;
}

int World::inventoryOf(const OrePlan& ores, const std::string& name) const
{
    // item.has(Any): egal was - dann zaehlt einfach alles, was da ist.
    if (IsAnyName(name) && FindOre(ores, name) < 0)
    {
        int alles = 0;
        for (const auto& e : inventory)
            alles += e.second.count;
        return alles;
    }

    const int nummer = FindOre(ores, name);
    if (nummer < 0)
        return 0;

    // Ueber alle Zustaende: item.has(Stein) fragt nach dem Erz, nicht
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

    const Ore& erz = OreOf(ores, ore);

    // power.rushMine(): der Abbau springt in diesem Durchgang gleich auf sein
    // Ende, statt normal Zeit zu brauchen.
    mineTimer += (powerActive && powerKind == PowerKind::Mine) ? erz.mineSeconds : dt;
    if (mineTimer < erz.mineSeconds)
        return;

    // Der Block wandert in die Tasche - Geld gibt es erst beim Verkaufen.
    // Frisch abgebaut heisst roh, alles andere kommt erst durch Bearbeiten.
    Item frisch;
    frisch.ore   = ore;
    frisch.state = (int)OreState::Raw;

    // Falsch behandelt kostet Reinheit - nicht Zeit. Der Block kommt genauso
    // schnell heraus, er ist nur weniger wert. Das sieht man in der Tasche, und
    // mit Reinigen laesst sich ein Teil davon wieder aufholen.
    int rein = StartPurity(ores, craft, ore) - CareLoss(ores.care, care, mineCare);
    if (rein < 0)
        rein = 0;

    lastCareLoss = StartPurity(ores, craft, ore) - rein;

    addToBag(frisch, 1, rein);

    // Ab jetzt kennt man dieses Erz - im Wiki bekommt es eine Seite.
    noteOre(ore, (int)OreState::Raw, rein);

    lastOre = ore;
    ++minedCount;

    // Fuer die Auftraege. Die saubere Serie bricht bei der kleinsten
    // Fehlbehandlung ab - das ist der ganze Reiz an ihr.
    ++stats.mined;
    ++stats.minedOre[ore];
    if (lastCareLoss > 0)
        stats.cleanStreak = 0;
    else
        ++stats.cleanStreak;

    blockAlive   = false;
    mining       = false;
    byHand       = false;
    mineTimer    = 0.0f;
    mineCare     = BlockCare::Plain;
    respawnTimer = respawnSeconds;
    fx           = 1.0f;
}

void World::cancelMining()
{
    mining    = false;
    mineTimer = 0.0f;
    byHand    = false;
    mineCare  = BlockCare::Plain;
}

int World::startCraft(const OrePlan& ores, const Limits& limits, const CraftStep& step, Item was,
                      int anzahl, bool byHandStart)
{
    // In der Vorbereitung wird nicht gearbeitet - sonst waere die Werkstatt
    // ein Weg, die Uhr zu umgehen.
    if (frozen)
        return 0;

    // Ist ein Platz frei? Frueher gab es genau einen, und ein zweiter Aufruf
    // machte nichts. Jetzt haengt die Zahl am Skilltree - der Spielercode muss
    // sich aber weiterhin selbst darum kuemmern, denn voll ist voll.
    setJobSlots(limits.maxJobs);
    Job* platz = freeJob();
    if (platz == nullptr)
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

    platz->item    = was;
    platz->count   = wie;
    platz->purity  = it->second.purity;
    platz->to      = step.to;
    platz->delta   = step.purity;
    platz->name    = step.name;
    platz->seconds = step.seconds * (float)wie;
    platz->timer   = 0.0f;
    platz->active  = true;
    platz->byHand  = byHandStart;

    // Aus der Tasche heraus: waehrend der Arbeit gehoert es der Werkstatt.
    // Gemerkt wird es trotzdem - ein Abbruch muss es zurueckgeben koennen.
    platz->taken.clear();
    platz->taken.push_back({was, wie, it->second.purity});

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

    // Ein freier Platz muss her - Legieren teilt sie sich mit dem Verarbeiten,
    // es ist ja derselbe Ofen.
    setJobSlots(limits.maxJobs);
    Job* platz = freeJob();
    if (platz == nullptr)
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

    platz->item.ore   = rezept.result;
    platz->item.state = rezept.to;
    platz->count      = wie;
    platz->purity     = rein;
    platz->to         = rezept.to;
    platz->delta      = 0;  // der Aufschlag steckt schon in rein
    platz->name       = "Alloy";
    platz->seconds    = rezept.seconds * (float)wie;
    platz->timer      = 0.0f;
    platz->active     = true;
    platz->byHand     = byHandStart;
    platz->taken      = nehmen;

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

// ---- Die Auftragsplaetze --------------------------------------------------

void World::setJobSlots(int anzahl)
{
    if (anzahl < 1)
        anzahl = 1;

    // Groesser werden ist einfach. Kleiner werden darf einen laufenden Auftrag
    // NICHT verschlucken - das waere verlorenes Material. Also bleibt die
    // Liste so lang, bis die hinteren Plaetze von selbst leer laufen.
    if ((int)jobs.size() < anzahl)
        jobs.resize((std::size_t)anzahl);

    while ((int)jobs.size() > anzahl && !jobs.back().active)
        jobs.pop_back();
}

int World::jobsRunning() const
{
    int n = 0;
    for (const Job& j : jobs)
        if (j.active)
            ++n;
    return n;
}

int World::jobsIdle() const
{
    int n = 0;
    for (const Job& j : jobs)
        if (!j.active)
            ++n;
    return n;
}

bool World::anyCrafting() const
{
    return jobsRunning() > 0;
}

bool World::anyCraftByHand() const
{
    for (const Job& j : jobs)
        if (j.active && j.byHand)
            return true;
    return false;
}

const World::Job* World::nextDone() const
{
    const Job* best = nullptr;
    for (const Job& j : jobs)
        if (j.active && (best == nullptr || j.left() < best->left()))
            best = &j;
    return best;
}

World::Job* World::freeJob()
{
    for (Job& j : jobs)
        if (!j.active)
            return &j;
    return nullptr;
}

void World::tickCraft(float dt, bool programLaeuft)
{
    if (frozen)
        return;

    for (Job& job : jobs)
    {
        if (!job.active)
            continue;

        // Vom Programm gestartet? Dann kommt er nur voran, solange das
        // Programm laeuft. Von Hand gestartet laeuft er immer weiter.
        if (!job.byHand && !programLaeuft)
            continue;

        // power.rushWork(): dieser Auftrag springt gleich auf sein Ende.
        job.timer += (powerActive && powerKind == PowerKind::Work) ? job.seconds : dt;
        if (job.timer < job.seconds)
            continue;

        // Beim Legieren ist job.item schon das Ergebnis-Erz - deshalb tut hier
        // beides dasselbe.
        Item fertig;
        fertig.ore   = job.item.ore;
        fertig.state = job.to;
        addToBag(fertig, job.count, job.purity + job.delta);

        // Fuers Wiki: das hier hat der Spieler gerade selbst herausgefunden.
        //
        // Beim Legieren lernt er den neuen Stoff ueberhaupt erst kennen -
        // deshalb steht noteOre vor der Kante. Eine Kante gibt es dabei nicht:
        // aus zwei Erzen wird ein drittes, das ist kein Schritt von einem
        // Zustand in den naechsten.
        noteOre(fertig.ore, fertig.state, job.purity + job.delta);
        if (job.taken.size() == 1 && job.taken[0].was.ore == fertig.ore &&
            job.taken[0].was.state != fertig.state)
            oreSteps.insert(OreStep{fertig.ore, job.taken[0].was.state, fertig.state});

        // Fuer die Auftraege. Legieren wird getrennt gezaehlt: es ist der
        // aufwendigere Weg und darf eine eigene Aufgabe sein.
        if (job.to == (int)OreState::Alloy)
            stats.alloyed += job.count;
        else
            stats.crafted += job.count;

        job.active = false;
        job.byHand = false;
        job.timer  = 0.0f;
        job.count  = 0;
        job.taken.clear();
    }
}

void World::cancelCraft(bool nurProgramm)
{
    for (Job& job : jobs)
    {
        if (!job.active)
            continue;
        if (nurProgramm && job.byHand)
            continue;

        // Unveraendert zurueck: ein abgebrochener Auftrag darf nichts kosten.
        for (const Taken& t : job.taken)
            addToBag(t.was, t.count, t.purity);

        job.active = false;
        job.byHand = false;
        job.timer  = 0.0f;
        job.count  = 0;
        job.taken.clear();
    }
}

// ---- Was man ueber ein Erz weiss ------------------------------------------

bool World::knowsOre(const OrePlan& ores, int ore) const
{
    if (ore < 0 || ore >= (int)ores.ores.size())
        return false;

    // Was von Hand in data/erze.json steht (und was daraus legiert wird), war
    // nie ein Raetsel: es hat eine Wiki-Seite, seit es das Spiel gibt.
    // Untersuchen muss man nur, was sich das Spiel selbst ausgedacht hat.
    if (ore < ores.handmade)
        return true;

    return assayed.count(ore) != 0;
}

int World::startAssay(const OrePlan& ores, int ore, int kosten, float dauer)
{
    if (frozen)
        return 0;
    if (assaying)
        return 0;
    if (ore < 0 || ore >= (int)ores.ores.size())
        return 0;
    if (knowsOre(ores, ore))
        return 0;
    if (money < kosten)
        return 0;

    money -= kosten;
    assaying     = true;
    assayOre     = ore;
    assayTimer   = 0.0f;
    assaySeconds = (dauer > 0.0f) ? dauer : 0.1f;
    return kosten;
}

void World::tickAssay(float dt)
{
    if (frozen || !assaying)
        return;

    // power.rushWork() beschleunigt die Untersuchung genauso wie den Ofen -
    // beides ist Warten auf eine Uhr in der Werkstatt.
    assayTimer += (powerActive && powerKind == PowerKind::Work) ? assaySeconds : dt;
    if (assayTimer < assaySeconds)
        return;

    assayed.insert(assayOre);
    ++stats.assayed;
    assaying   = false;
    assayTimer = 0.0f;
}

int World::startPower(PowerKind art, int kosten, float dauer, float marktBoost, float abklingzeit)
{
    if (frozen)
        return 0;
    if (!powerReady())
        return 0;
    if (money < kosten)
        return 0;

    money              -= kosten;
    powerActive        = true;
    powerKind          = art;
    powerTimer         = (dauer > 0.0f) ? dauer : 0.1f;
    powerMarketBoost   = (marktBoost > 0.0f) ? marktBoost : 1.0f;
    powerCooldownAfter = (abklingzeit > 0.0f) ? abklingzeit : 0.0f;
    return kosten;
}

// ---- Der Markt ------------------------------------------------------------

// Wie lange der Markt braucht, bis er ganz ausschlaegt - gezaehlt in Marktzeit,
// nicht in Sekunden.
//
// Ohne das wuerde der Preis in dem Moment springen, in dem man "market" kauft:
// die Welle steht bei ihrer Zeit 0 ja irgendwo, nur nicht bei 1.0. So faengt
// stattdessen jedes Erz bei genau seinem Grundwert an und kommt von dort aus in
// Bewegung - und die Kurve auf der Marktseite beginnt als gerade Linie.
static constexpr float kMarketWake = 2.0f;

float World::marketFactorAt(int ore, float zeit) const
{
    if (marketSwing <= 0.0f || zeit <= 0.0f)
        return 1.0f;

    // Zwei Wellen mit unterschiedlicher Laenge, dazu ein Versatz aus der
    // Erznummer. Dadurch schwingt kein Erz im Gleichtakt mit einem anderen,
    // und der Verlauf wiederholt sich nicht auf den ersten Blick.
    const float phase = (float)ore * 2.399963f;
    const float a     = std::sin(zeit + phase);
    const float b     = std::sin(zeit * 0.37f + phase * 1.7f);

    float wach = zeit / kMarketWake;
    if (wach > 1.0f)
        wach = 1.0f;

    const float f = 1.0f + marketSwing * wach * (a * 0.65f + b * 0.35f);
    return (f < 0.1f) ? 0.1f : f;
}

float World::marketFactor(int ore) const
{
    // power.rushMarket(): der Kurs ist fuer einen Moment egal, es zaehlt nur
    // der Schub. Steht an derselben Stelle wie die normale Rechnung, damit
    // Verkauf und Marktseite nie etwas Verschiedenes behaupten.
    if (powerActive && powerKind == PowerKind::Market)
        return powerMarketBoost;
    return marketFactorAt(ore, marketTime);
}

void World::update(float dt, const OrePlan& ores)
{
    // ---- Markt und Untersuchung -----------------------------------------
    //
    // Beides laeuft nur, solange die Welt laeuft. Ein Markt, der sich in der
    // Vorbereitung weiterdreht, waere ein Wartespiel: man wuerde vor dem
    // Rundenstart sitzen und auf einen guten Preis warten.
    //
    // Und sie laeuft erst, wenn es den Markt ueberhaupt gibt: solange nichts
    // schwankt, wuerde die Uhr nur ins Leere zaehlen - und beim Freischalten
    // stuende die Welle dann mitten in ihrem Ausschlag statt am Grundwert.
    if (!frozen && marketSwing > 0.0f)
        marketTime += dt * marketSpeed;

    tickAssay(dt);

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
        // power.rushGrow(): der Rest der Wartezeit faellt in diesem Durchgang
        // auf einen Schlag weg.
        respawnTimer -= (powerActive && powerKind == PowerKind::Grow) ? respawnTimer : dt;
        if (respawnTimer <= 0.0f)
        {
            respawnTimer = 0.0f;
            blockAlive   = true;

            // Der neue Block wird frisch gewuerfelt: welches Erz, und welches
            // Muster. Deshalb ist nie zweimal dasselbe da.
            if (!ores.empty())
                ore = RollOre(ores, level, rng);
            oreSeed = (unsigned)rng();

            // Und was er verlangt. Das haengt am Erz und nicht am Wuerfel:
            // ein Diamant will immer dasselbe. Deshalb kann man es sich
            // merken - und im Programm je Erz abfragen.
            care     = OreCare(ores, ore);
            mineCare = BlockCare::Plain;
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

    // Der Schub laeuft von selbst aus - genau wie fx und sellFx darueber.
    // Absichtlich ans Ende gestellt: alles, was ihn diesen Durchgang noch
    // ausnutzen sollte (Nachwachsen oben), ist da schon durchgelaufen.
    //
    // Danach beginnt die Abklingzeit - ohne die koennte man den naechsten
    // Schub in der Sekunde starten, in der der alte endet, und haette den
    // Effekt praktisch dauerhaft.
    if (powerActive)
    {
        powerTimer -= dt;
        if (powerTimer <= 0.0f)
        {
            powerActive   = false;
            powerTimer    = 0.0f;
            powerCooldown = powerCooldownAfter;
        }
    }
    else if (powerCooldown > 0.0f)
    {
        powerCooldown -= dt;
        if (powerCooldown < 0.0f)
            powerCooldown = 0.0f;
    }
}

void DrawWorld(World& world, const OrePlan& ores, const CraftPlan& craft, const AlloyPlan& alloys,
               const RoundPlan& rounds)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList*    dl = ImGui::GetBackgroundDrawList();

    // ---- Die Mine: eine Karte oben rechts ---------------------------------
    //
    // Frueher schwebte der Block frei in der Bildmitte und die Konsolen lagen
    // darueber. Jetzt hat er sein eigenes Feld: Karte, Ueberschrift, Raster,
    // Block in der Mitte, darunter der Balken fuers Nachwachsen.
    const ImVec2 ka(vp->WorkPos.x + vp->WorkSize.x - ui::kRightMargin - ui::kRightWidth,
                    vp->WorkPos.y + ui::kMineTop);
    const ImVec2 kb(ka.x + ui::kRightWidth, ka.y + ui::kMineHeight);

    ui::Card(dl, ka, kb);
    dl->AddText(ImVec2(ka.x + ui::kCardPad, ka.y + ui::kCardPad), ui::kTextDim, "MINE");

    // Das Feld, in dem der Block sitzt - mit feinem Raster, damit die Flaeche
    // nicht leer wirkt, wenn gerade nichts dasteht.
    const ImVec2 fa(ka.x + ui::kCardPad, ka.y + 46.0f);
    const ImVec2 fb(kb.x - ui::kCardPad, kb.y - 30.0f);

    dl->AddRect(fa, fb, ui::kBorder, ui::kRoundS, 0, 1.0f);
    dl->PushClipRect(fa, fb, true);
    for (float x = fa.x + 26.0f; x < fb.x; x += 26.0f)
        dl->AddLine(ImVec2(x, fa.y), ImVec2(x, fb.y), ui::kGrid, 1.0f);
    for (float y = fa.y + 26.0f; y < fb.y; y += 26.0f)
        dl->AddLine(ImVec2(fa.x, y), ImVec2(fb.x, y), ui::kGrid, 1.0f);
    dl->PopClipRect();

    const ImVec2 c((fa.x + fb.x) * 0.5f, (fa.y + fb.y) * 0.5f);
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

        // Gestrichelter Umriss: hier kommt gleich wieder einer.
        const float schritt = 7.0f;
        for (float x = a.x; x < b.x; x += schritt * 2.0f)
        {
            dl->AddLine(ImVec2(x, a.y), ImVec2(x + schritt, a.y), ui::kBorderS, 1.5f);
            dl->AddLine(ImVec2(x, b.y), ImVec2(x + schritt, b.y), ui::kBorderS, 1.5f);
        }
        for (float y = a.y; y < b.y; y += schritt * 2.0f)
        {
            dl->AddLine(ImVec2(a.x, y), ImVec2(a.x, y + schritt), ui::kBorderS, 1.5f);
            dl->AddLine(ImVec2(b.x, y), ImVec2(b.x, y + schritt), ui::kBorderS, 1.5f);
        }
    }

    // Der Balken ganz unten in der Karte. Er zeigt, was gerade laeuft: den
    // Abbau oder das Nachwachsen. Steht der Block einfach nur da, bleibt die
    // Rille leer - ein dauerhaft voller Balken saehe aus, als passiere etwas.
    {
        float t = 0.0f;
        if (!world.blockAlive && world.respawnSeconds > 0.0f)
            t = 1.0f - (world.respawnTimer / world.respawnSeconds);
        else if (world.mining && erz.mineSeconds > 0.0f)
            t = world.mineTimer / erz.mineSeconds;

        ui::Bar(dl, ImVec2(fa.x, kb.y - 20.0f), fb.x - fa.x, 5.0f, t, ui::kAccent);
    }

    // Was der Block verlangt, steht ueber ihm. Ohne das muesste man mit der
    // Maus hinfahren, um es zu erfahren - und beim Zuschauen faellt sonst gar
    // nicht auf, warum ein Block auf einmal ewig braucht.
    if (world.blockAlive && world.care != BlockCare::Plain)
    {
        const ImU32 farbe = (world.care == BlockCare::Cool) ? IM_COL32(0x2E, 0x6F, 0xA8, 255)
                                                            : IM_COL32(0xC4, 0x3D, 0x2F, 255);

        char text[64];
        std::snprintf(text, sizeof(text), "wants %s", BlockCareName(world.care));

        const ImVec2 ts = ImGui::CalcTextSize(text);
        const ImVec2 tp(c.x - ts.x * 0.5f, a.y - ts.y - 10.0f);

        dl->AddRectFilled(ImVec2(tp.x - 8.0f, tp.y - 3.0f),
                          ImVec2(tp.x + ts.x + 8.0f, tp.y + ts.y + 3.0f), farbe, ui::kRoundS);
        dl->AddText(tp, IM_COL32(255, 255, 255, 255), text);
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

        char gain[96];
        if (world.lastCareLoss > 0)
            std::snprintf(gain, sizeof(gain), "+1 %s  (-%d%% purity)", letzt.name.c_str(),
                          world.lastCareLoss);
        else
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
        std::snprintf(text, sizeof(text), "+%s money", ui::Money(world.lastSold).c_str());
        const ImVec2 ts = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y + kHalf + 24.0f - t * 40.0f),
                    IM_COL32(0xCC, 0x5B, 0x1E, alpha), text);
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
            dl->AddRect(ImVec2(a.x - 3.0f, a.y - 3.0f), ImVec2(b.x + 3.0f, b.y + 3.0f),
                        ui::kAccent, 8.0f, 0, 2.0f);

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                world.mineByHand();
        }

        const int rein = StartPurity(ores, craft, world.ore);

        ImGui::BeginTooltip();
        ImGui::TextUnformatted(erz.name.c_str());
        ImGui::TextDisabled("%s money when sold  -  %.1f s to mine",
                            ui::Money(StackValue(ores, craft, world.ore, (int)OreState::Raw, rein,
                                                 1, world.moneyPerBlock))
                                .c_str(),
                            erz.mineSeconds);
        ImGui::TextDisabled("Purity %d%%  -  processing makes it worth more", rein);

        // Was der Block verlangt, gehoert ganz oben hin - danach richtet sich
        // ja, ob das Programm ihn ueberhaupt vernuenftig aufbekommt. Der Name
        // steht dabei: es haengt am Erz, und genau das ist das Merkenswerte.
        if (world.care != BlockCare::Plain)
        {
            ImGui::TextColored(ui::V(ui::kBad), "%s always wants %s  -  block.mine(%s)",
                               erz.name.c_str(), BlockCareName(world.care),
                               BlockCareName(world.care));
            ImGui::TextDisabled("Untreated it loses %d%% purity, wrongly treated %d%%",
                                ores.care.purityNone, ores.care.purityWrong);
        }

        if (world.blockAlive && (!world.frozen || world.handMine))
            ImGui::TextDisabled("Click to mine");
        else if (world.frozen)
            ImGui::TextDisabled(world.phase == RoundPhase::Run ? "The game is paused"
                                                              : "Start the round first");
        ImGui::EndTooltip();
    }

    // Die Welt steht still. Das muss man sofort sehen - sonst wartet man
    // darauf, dass der Block nachwaechst, und versteht nicht, warum nichts
    // passiert. Es steht IN der Karte, mittig unter dem Block: die Leiste ganz
    // unten sagt es zwar auch, aber wer auf den Block starrt, schaut nicht
    // dorthin.
    if (world.frozen && world.phase != RoundPhase::Report)
    {
        const char*  hinweis =
            (world.phase == RoundPhase::Run) ? "paused - F9 continues" : "paused";
        const ImVec2 hs      = ImGui::CalcTextSize(hinweis);
        dl->AddText(ImVec2(c.x - hs.x * 0.5f, fb.y - hs.y - 8.0f), ui::kTextWk, hinweis);
    }

    // Ein Auftrag laeuft: man soll auch hier sehen, wie weit er ist - sonst
    // wartet man auf der Welt-Seite blind.
    if (const World::Job* job = world.nextDone())
    {
        const float t = job->progress();

        // Beim Legieren steht in job->item schon das Ergebnis - so liest sich
        // beides richtig: "Wash: Gold x3" und "Alloy: Electrum x2".
        //
        // Es laufen womoeglich mehrere: dann steht der naechste fertige da und
        // dahinter, wie viele noch warten. Alle nebeneinander waere hier oben
        // kein Bild mehr, sondern eine Liste.
        char text[160];
        const int weitere = world.jobsRunning() - 1;
        if (weitere > 0)
            std::snprintf(text, sizeof(text), "%s: %s x%d  (+%d)", job->name.c_str(),
                          OreOf(ores, job->item.ore).name.c_str(), job->count, weitere);
        else
            std::snprintf(text, sizeof(text), "%s: %s x%d", job->name.c_str(),
                          OreOf(ores, job->item.ore).name.c_str(), job->count);
        const ImVec2 ts = ImGui::CalcTextSize(text);

        const ImVec2 pa(c.x - 90.0f, b.y + 44.0f);
        const ImVec2 pb(c.x + 90.0f, b.y + 50.0f);
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, pa.y - ImGui::GetTextLineHeight() - 4.0f),
                    IM_COL32(180, 200, 240, 235), text);
        dl->AddRectFilled(pa, pb, ui::kSunken, 3.0f);
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

// Dieselbe Farbe, aber mit weniger Deckkraft - fuers Ein- und Ausblenden.
static ImU32 FadeColor(ImU32 farbe, float alpha)
{
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(farbe);
    v.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(v);
}

void DrawSellToast(const World& world)
{
    if (world.sellFx <= 0.0f)
        return;

    // Nur die Zahl: steigt auf, wird dabei kleiner und blasser - wie Rauch,
    // der sich aufloest. Kein Rahmen, kein Symbol, das lenkt vom eigentlichen
    // Hinweis nur ab.
    const float t     = 1.0f - world.sellFx;  // 0 = gerade erst verkauft, 1 = vorbei
    const float alpha = world.sellFx;         // faellt gleichmaessig von 1 auf 0

    char text[32];
    std::snprintf(text, sizeof(text), "+%s", ui::Money(world.lastSold).c_str());

    ImGuiViewport* vp   = ImGui::GetMainViewport();
    ImDrawList*    dl   = ImGui::GetForegroundDrawList();
    ImFont*        font = ImGui::GetFont();

    const float size = ImGui::GetFontSize() * (1.3f - 0.5f * t);  // 1.3x -> 0.8x
    const ImVec2 ts  = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);

    const float x = vp->WorkPos.x + vp->WorkSize.x - ui::kRightMargin - ts.x;
    const float y = vp->WorkPos.y + ImGui::GetFrameHeight() + 14.0f - t * 34.0f;

    dl->AddText(font, size, ImVec2(x, y), FadeColor(ui::kGood, alpha), text);
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

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::V(ui::kPage));
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

        ImGui::PushStyleColor(ImGuiCol_Text, ui::V(ui::kText));
        ImGui::TextUnformatted("Bag");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextDisabled("  %d blocks, %s money in total", world.inventoryCount(),
                            ui::Money(wert).c_str());

        if (!world.inventory.empty())
        {
            ImGui::SameLine(ImGui::GetWindowWidth() - 190.0f);
            if (ImGui::Button("Sell everything", ImVec2(160.0f, 0.0f)))
                world.sell(ores, craft);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ---- Laeuft gerade ein Auftrag? ------------------------------------
        // Was in Arbeit ist, liegt nicht in der Tasche. Ohne diese Zeile waere
        // es einfach verschwunden.
        if (world.anyCrafting())
        {
            // Eine Zeile je laufendem Auftrag. Mit mehreren Oefen ist "was ist
            // gerade in Arbeit" sonst nicht mehr zu ueberblicken - und was in
            // Arbeit ist, liegt ja nicht in der Tasche.
            for (const World::Job& job : world.jobs)
            {
                if (!job.active)
                    continue;

                ImGui::TextColored(ui::V(ui::kText), "%s: %d x %s", job.name.c_str(), job.count,
                                   OreOf(ores, job.item.ore).name.c_str());

                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ui::V(ui::kAccent));
                char rest[48];
                std::snprintf(rest, sizeof(rest), "%.1f s left", (double)job.left());
                ImGui::ProgressBar(job.progress(), ImVec2(320.0f, 0.0f), rest);
                ImGui::PopStyleColor();
            }

            // Wie viele Plaetze ueberhaupt da sind. Ohne die Zeile merkt man
            // gar nicht, dass man gerade einen zweiten Ofen gekauft hat.
            if ((int)world.jobs.size() > 1)
                ImGui::TextDisabled("%d of %d furnaces busy", world.jobsRunning(),
                                    (int)world.jobs.size());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        if (world.inventory.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Nothing mined yet.");

            // Von block.mine() steht hier nur etwas, wenn es gekauft ist. Wer
            // es noch nicht hat, soll klicken - und nicht nach einem Befehl
            // suchen, den es fuer ihn noch gar nicht gibt.
            if (limits.allowMine)
            {
                ImGui::TextDisabled("Go to the World page and click the block -");
                ImGui::TextDisabled("or let a program run block.mine().");
            }
            else
            {
                ImGui::TextDisabled("Go to the World page and click the block.");
            }
        }
        else
        {
            ImGui::TextDisabled(
                "Use the arrows to set the amount, click the number to type it.");

            // Vom Verarbeiten steht hier nur etwas, wenn es ueberhaupt schon
            // geht. Ein Hinweis auf ein Rechtsklickmenue, das leer bleibt, ist
            // schlimmer als gar keiner.
            bool kannVerarbeiten = false;
            for (const CraftStep& s : craft.steps)
                if (CraftUnlocked(s, limits))
                    kannVerarbeiten = true;

            if (kannVerarbeiten)
                ImGui::TextDisabled(
                    "Right-click a card to process it - that makes the stack worth more.");

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
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::V(ui::kCard));
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
                    ImGui::TextColored(rein >= 70 ? ui::V(ui::kText) : ui::V(ui::kTextDim), "%s",
                                       text);
                }

                char haben[48];
                std::snprintf(haben, sizeof(haben), "x %d", anzahl);
                const float hw = ImGui::CalcTextSize(haben).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (innen - hw) * 0.5f);
                ImGui::TextColored(ui::V(ui::kAccent), "%s", haben);

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
                        ImGui::SetTooltip("Click to type a number");
                }

                ImGui::SameLine();

                ImGui::PushButtonRepeat(true);
                if (ImGui::ArrowButton("mehr", ImGuiDir_Right) && wie < anzahl)
                    ++wie;
                ImGui::PopButtonRepeat();

                // ---- Verkaufen --------------------------------------------
                char knopf[64];
                std::snprintf(knopf, sizeof(knopf), "Sell  %s",
                              ui::Money(StackValue(ores, craft, stapel.ore, stapel.state, rein, wie,
                                                   world.moneyPerBlock))
                                  .c_str());

                ImGui::PushStyleColor(ImGuiCol_Button, ui::V(ui::kAccent));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::V(ui::kAccentHot));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::V(ui::kAccent));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                if (ImGui::Button(knopf, ImVec2(innen, 0.0f)))
                {
                    verkaufen    = stapel;
                    verkaufenWie = wie;
                    verkaufenJa  = true;
                }
                ImGui::PopStyleColor(4);

                // Verarbeiten. Angeboten wird genau das, was von hier aus
                // wirklich geht: der Zustand muss passen, das Erz muss das Ziel
                // erlauben, und gekauft sein muss der Schritt auch.
                //
                // Solange gar kein Schritt gekauft ist, gibt es das Menue nicht
                // einmal - sonst klappt eines auf, das nur sagen kann, dass es
                // nichts kann.
                if (kannVerarbeiten && ImGui::BeginPopupContextWindow("verarbeiten"))
                {
                    ImGui::TextDisabled("Process");
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

                        if (ImGui::MenuItem(zeile, nullptr, false,
                                            world.jobsIdle() > 0 && !world.frozen))
                        {
                            arbeitAn      = stapel;
                            arbeitSchritt = &s;
                            arbeitWie     = wie;
                        }

                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("%d pieces, %.1f s  -  purity %+d%%\nThen %s money",
                                              wie, (double)(s.seconds * (float)wie), s.purity,
                                              ui::Money(StackValue(ores, craft, stapel.ore, s.to,
                                                                   rein + s.purity, wie,
                                                                   world.moneyPerBlock))
                                                  .c_str());
                    }

                    if (moeglich == 0)
                        ImGui::TextDisabled("Nothing works from here.");
                    else if (world.frozen)
                        ImGui::TextDisabled(world.phase == RoundPhase::Run
                                                ? "The game is paused."
                                                : "Start the round first.");
                    else if (world.jobsIdle() <= 0)
                        ImGui::TextDisabled((int)world.jobs.size() > 1
                                                ? "Every furnace is busy."
                                                : "A job is already running.");

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
