#include "quest.h"

#include "datapath.h"
#include "json.h"
#include "ore.h"
#include "round.h"
#include "skilltree.h"
#include "theme.h"
#include "world.h"

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

namespace
{

struct MetricEntry
{
    const char* key;
    QuestMetric metric;
    QuestKind   kind;
};

// Was in der Datei stehen darf. Die Art haengt an der Sache selbst und steht
// deshalb hier und nicht in der Datei: "behalte etwas" IST ein Halteauftrag,
// da gibt es nichts einzustellen.
const MetricEntry kMetrics[] = {
    {"mined", QuestMetric::Mined, QuestKind::Count},
    {"mined_ore", QuestMetric::MinedOre, QuestKind::Count},
    {"earned", QuestMetric::Earned, QuestKind::Count},
    {"crafted", QuestMetric::Crafted, QuestKind::Count},
    {"alloyed", QuestMetric::Alloyed, QuestKind::Count},
    {"assayed", QuestMetric::Assayed, QuestKind::Count},
    {"clean_streak", QuestMetric::CleanStreak, QuestKind::Count},
    {"sold_above", QuestMetric::SoldAbove, QuestKind::Count},

    {"hold_ore", QuestMetric::HoldOre, QuestKind::Hold},
    {"bag_purity", QuestMetric::BagPurity, QuestKind::Hold},
    {"no_sell", QuestMetric::NoSell, QuestKind::Hold},
    {"furnaces_busy", QuestMetric::FurnacesBusy, QuestKind::Hold},
    {"no_idle_block", QuestMetric::NoIdleBlock, QuestKind::Hold},

    {"distinct_ores", QuestMetric::DistinctOres, QuestKind::Once},
    {"distinct_states", QuestMetric::DistinctStates, QuestKind::Once},
    {"single_sale", QuestMetric::SingleSale, QuestKind::Once},
    {"frugal", QuestMetric::Frugal, QuestKind::Once},
};

bool FindMetric(const std::string& key, QuestMetric& metric, QuestKind& kind)
{
    for (const MetricEntry& e : kMetrics)
        if (key == e.key)
        {
            metric = e.metric;
            kind   = e.kind;
            return true;
        }
    return false;
}

// Braucht der Auftrag ein Erz?
bool WantsOre(QuestMetric m)
{
    return m == QuestMetric::MinedOre || m == QuestMetric::HoldOre;
}

std::string Replace(std::string text, const std::string& was, const std::string& womit)
{
    std::size_t p = 0;
    while ((p = text.find(was, p)) != std::string::npos)
    {
        text.replace(p, was.size(), womit);
        p += womit.size();
    }
    return text;
}

}  // namespace

float Quest::ratio() const
{
    if (done)
        return 1.0f;

    if (seconds > 0.0f)
        return (held >= seconds) ? 1.0f : (held / seconds);

    if (amount <= 0)
        return 0.0f;

    const float r = (float)progress / (float)amount;
    return (r > 1.0f) ? 1.0f : r;
}

// ---------------------------------------------------------------------------
// Die Datei
// ---------------------------------------------------------------------------

QuestPlan LoadQuestPlan()
{
    QuestPlan plan;

    std::ifstream in;
    for (const std::string& path : DataPaths("quests.json"))
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
        // Ohne Datei gibt es eben keine Auftraege. Das Spiel laeuft weiter -
        // eine fehlende Zusatzaufgabe darf nichts kaputtmachen.
        plan.problems.push_back("data/quests.json not found - no contracts available.");
        return plan;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    JsonValue   wurzel;
    std::string fehler;
    if (!ParseJson(buffer.str(), wurzel, fehler))
    {
        plan.problems.push_back("data/quests.json - " + fehler);
        return plan;
    }

    plan.offers = (int)wurzel.number("angebote", plan.offers);
    if (plan.offers < 1)
        plan.offers = 1;
    if (plan.offers > 5)
        plan.offers = 5;

    const JsonValue* liste = wurzel.find("auftraege");
    if (liste == nullptr || liste->type != JsonValue::Type::Array)
    {
        plan.problems.push_back("data/quests.json: \"auftraege\" is missing or not a list.");
        return plan;
    }

    for (const JsonValue& e : liste->items)
    {
        QuestDef def;
        def.id   = e.text("id", "");
        def.text = e.text("text", "");

        const std::string art = e.text("art", "");
        if (!FindMetric(art, def.metric, def.kind))
        {
            plan.problems.push_back("quests.json: \"" + art + "\" is not a known kind.");
            continue;
        }

        if (def.text.empty())
        {
            plan.problems.push_back("quests.json: " + def.id + " has no text.");
            continue;
        }

        def.amount         = (int)e.number("menge", 0);
        def.amountGrow     = (float)e.number("menge_wachstum", 1.0);
        def.amountOfTarget = (float)e.number("menge_vom_ziel", 0.0);
        def.seconds        = (float)e.number("dauer", 0.0);
        def.limit          = (int)e.number("grenze", 0);
        def.ore            = e.text("erz", "");
        def.reward         = (float)e.number("lohn", 0.6);
        def.penalty        = (float)e.number("strafe", 0.10);
        def.minRound       = (int)e.number("ab_runde", 1);

        const std::string braucht = e.text("braucht", "");
        if (!braucht.empty() && !SkillFromKey(braucht, def.needs))
        {
            plan.problems.push_back("quests.json: " + def.id + " needs \"" + braucht +
                                    "\", which is not a skill.");
            continue;
        }

        if (def.kind == QuestKind::Hold && def.seconds <= 0.0f)
        {
            plan.problems.push_back("quests.json: " + def.id + " is a holding job and needs \"dauer\".");
            continue;
        }

        if (def.amount <= 0 && def.amountOfTarget <= 0.0f)
        {
            plan.problems.push_back("quests.json: " + def.id + " needs \"menge\" or \"menge_vom_ziel\".");
            continue;
        }

        plan.defs.push_back(def);
    }

    if (plan.defs.empty())
        plan.problems.push_back("data/quests.json: not a single usable contract.");

    return plan;
}

// ---------------------------------------------------------------------------
// Angebote wuerfeln
// ---------------------------------------------------------------------------

namespace
{

bool Unlocked(const QuestDef& def, const Limits& limits)
{
    if (def.needs == Skill::None)
        return true;

    switch (def.needs)
    {
    case Skill::Wash: return limits.allowWash;
    case Skill::Smelt: return limits.allowSmelt;
    case Skill::Cast: return limits.allowCast;
    case Skill::Clean: return limits.allowClean;
    case Skill::Polish: return limits.allowPolish;
    case Skill::Harden: return limits.allowHarden;
    case Skill::Refine: return limits.allowRefine;
    case Skill::Press: return limits.allowPress;
    case Skill::Etch: return limits.allowEtch;
    case Skill::Fuse: return limits.allowFuse;
    case Skill::Alloy: return limits.allowAlloy;
    case Skill::Care: return limits.allowCare;
    case Skill::Assay: return limits.allowAssay;
    case Skill::Market: return limits.allowMarket;
    case Skill::Sell: return limits.allowSell;
    case Skill::ExtraFurnace: return limits.maxJobs > 1;
    default: return true;
    }
}

// Aus welchen Erzen ein Auftrag waehlen darf: nur aus denen, die der Spieler
// schon einmal in der Tasche hatte. Sonst stuende da "bau 40 Mondsplitter ab"
// fuer ein Erz, das es in seiner Welt noch gar nicht gibt.
std::vector<int> KnownOres(const World& world, const OrePlan& ores)
{
    std::vector<int> out;
    for (const auto& e : world.oreFirst)
        if (e.first >= 0 && e.first < (int)ores.ores.size() && ores.ores[(std::size_t)e.first].minable)
            out.push_back(e.first);
    return out;
}

}  // namespace

void QuestRollOffers(World& world, const QuestPlan& plan, const OrePlan& ores,
                     const RoundPlan& rounds, const Limits& limits, bool alles)
{
    world.questOffers.clear();

    if (plan.defs.empty())
        return;

    const int ziel = RoundTarget(rounds, world.roundNumber);
    if (ziel <= 0)
        return;

    const std::vector<int> bekannt = KnownOres(world, ores);

    // Was ueberhaupt in Frage kommt.
    std::vector<int> moeglich;
    for (std::size_t i = 0; i < plan.defs.size(); ++i)
    {
        const QuestDef& def = plan.defs[i];
        if (!alles && world.roundNumber < def.minRound)
            continue;
        if (!alles && !Unlocked(def, limits))
            continue;
        if (WantsOre(def.metric) && def.ore.empty() && bekannt.empty())
            continue;
        moeglich.push_back((int)i);
    }

    // Nicht zweimal dieselbe Art nebeneinander: drei Mengenauftraege waeren
    // keine Auswahl, sondern dreimal dasselbe mit anderen Zahlen.
    std::set<int> genommen;
    std::set<int> arten;

    for (int n = 0; n < plan.offers && !moeglich.empty(); ++n)
    {
        int gewaehlt = -1;

        for (int versuch = 0; versuch < 24 && gewaehlt < 0; ++versuch)
        {
            const int kandidat =
                moeglich[(std::size_t)(world.rng() % (unsigned)moeglich.size())];

            if (genommen.count(kandidat) != 0)
                continue;

            // Beim letzten Angebot nicht mehr waehlerisch sein - lieber zweimal
            // dieselbe Art als ein leerer Platz.
            const int art = (int)plan.defs[(std::size_t)kandidat].metric;
            if (versuch < 16 && arten.count(art) != 0)
                continue;

            gewaehlt = kandidat;
        }

        if (gewaehlt < 0)
            break;

        genommen.insert(gewaehlt);
        arten.insert((int)plan.defs[(std::size_t)gewaehlt].metric);

        const QuestDef& def = plan.defs[(std::size_t)gewaehlt];

        Quest q;
        q.def     = gewaehlt;
        q.seconds = def.seconds;
        q.limit   = def.limit;

        // Wie gross die Aufgabe ist. Alles, was in Geld gerechnet wird, haengt
        // am Rundenziel - nur so waechst es zuverlaessig mit.
        if (def.amountOfTarget > 0.0f)
        {
            q.amount = (int)((float)ziel * def.amountOfTarget + 0.5f);
        }
        else
        {
            const double f =
                std::pow((double)def.amountGrow, (double)(world.roundNumber - 1));
            q.amount = (int)((double)def.amount * f + 0.5);
        }
        if (q.amount < 1)
            q.amount = 1;

        // Das Erz.
        std::string erzName;
        if (WantsOre(def.metric))
        {
            if (!def.ore.empty() && def.ore != "*")
            {
                q.ore = FindOre(ores, def.ore);
            }
            else if (!bekannt.empty())
            {
                q.ore = bekannt[(std::size_t)(world.rng() % (unsigned)bekannt.size())];
            }

            if (q.ore < 0)
                continue;  // das Erz gibt es nicht - Angebot faellt aus

            erzName = OreOf(ores, q.ore).name;
        }

        q.reward = (int)((float)ziel * def.reward + 0.5f);
        if (q.reward < 1)
            q.reward = 1;

        q.penalty = (int)((float)q.reward * def.penalty + 0.5f);
        if (q.penalty < 0)
            q.penalty = 0;

        // Den Text ausformulieren, damit spaeter niemand mehr rechnen muss.
        char zahl[32];
        std::snprintf(zahl, sizeof(zahl), "%d", q.amount);
        char dauer[32];
        std::snprintf(dauer, sizeof(dauer), "%d", (int)(q.seconds + 0.5f));
        char grenze[32];
        std::snprintf(grenze, sizeof(grenze), "%d", q.limit);

        q.text = def.text;
        q.text = Replace(q.text, "%n%", zahl);
        q.text = Replace(q.text, "%t%", dauer);
        q.text = Replace(q.text, "%grenze%", grenze);
        q.text = Replace(q.text, "%erz%", erzName);

        world.questOffers.push_back(q);
    }
}

void QuestAccept(World& world, int welches)
{
    if (welches < 0 || welches >= (int)world.questOffers.size())
        return;

    world.quest = world.questOffers[(std::size_t)welches];
    world.questOffers.clear();
}

// ---------------------------------------------------------------------------
// Fortschritt
// ---------------------------------------------------------------------------

namespace
{

// Der Zaehlerstand fuer einen Count-Auftrag.
int CountValue(const World& world, const Quest& q, QuestMetric m)
{
    switch (m)
    {
    case QuestMetric::Mined: return world.stats.mined;
    case QuestMetric::Earned: return world.stats.earned;
    case QuestMetric::Crafted: return world.stats.crafted;
    case QuestMetric::Alloyed: return world.stats.alloyed;
    case QuestMetric::Assayed: return world.stats.assayed;
    case QuestMetric::CleanStreak: return world.stats.cleanStreak;
    case QuestMetric::SoldAbove: return world.stats.soldAbove;

    case QuestMetric::MinedOre:
    {
        const auto it = world.stats.minedOre.find(q.ore);
        return (it != world.stats.minedOre.end()) ? it->second : 0;
    }

    default: return 0;
    }
}

// Gilt die Bedingung eines Halteauftrags gerade?
bool Holds(const World& world, const OrePlan& ores, const Quest& q, QuestMetric m)
{
    switch (m)
    {
    case QuestMetric::HoldOre:
        return world.bagCount(q.ore, 0xFFFFFFFFu) >= q.amount;

    case QuestMetric::BagPurity:
        return world.inventoryCount() > 0 &&
               world.inventoryPurity(ores, "any") >= q.amount;

    case QuestMetric::NoSell:
        return true;  // bricht ueber stats.soldSince, siehe QuestTick

    case QuestMetric::FurnacesBusy:
        return !world.jobs.empty() && world.jobsIdle() == 0;

    // Nie ein Block, an dem niemand arbeitet. Waehrend er nachwaechst ist das
    // in Ordnung - dagegen kann man nichts tun. Nur ein Block, der DASTEHT und
    // an dem nichts passiert, bricht die Uhr.
    case QuestMetric::NoIdleBlock:
        return !world.blockAlive || world.mining;

    default: return false;
    }
}

// Ist ein Once-Auftrag gerade erfuellt?
bool OnceDone(const World& world, const Quest& q, QuestMetric m)
{
    switch (m)
    {
    case QuestMetric::DistinctOres:
    {
        std::set<int> erze;
        for (const auto& e : world.inventory)
            if (e.second.count > 0)
                erze.insert(e.first.ore);
        return (int)erze.size() >= q.amount;
    }

    case QuestMetric::DistinctStates:
    {
        std::set<int> zustaende;
        for (const auto& e : world.inventory)
            if (e.second.count > 0)
                zustaende.insert(e.first.state);
        return (int)zustaende.size() >= q.amount;
    }

    case QuestMetric::SingleSale:
        return world.stats.biggestSale >= q.amount;

    // Genug Geld, und dabei unter der Zeilengrenze geblieben. Der einzige
    // Auftrag im Spiel, den man nicht durch Warten loesen kann.
    case QuestMetric::Frugal:
        return world.stats.earned >= q.amount && world.stats.linesRun < q.limit;

    default: return false;
    }
}

// Wie weit ein Once-Auftrag ist - nur fuer den Balken.
int OnceProgress(const World& world, const Quest& q, QuestMetric m)
{
    switch (m)
    {
    case QuestMetric::DistinctOres:
    {
        std::set<int> erze;
        for (const auto& e : world.inventory)
            if (e.second.count > 0)
                erze.insert(e.first.ore);
        return (int)erze.size();
    }
    case QuestMetric::DistinctStates:
    {
        std::set<int> zustaende;
        for (const auto& e : world.inventory)
            if (e.second.count > 0)
                zustaende.insert(e.first.state);
        return (int)zustaende.size();
    }
    case QuestMetric::SingleSale: return world.stats.biggestSale;
    case QuestMetric::Frugal: return world.stats.earned;
    default: return 0;
    }
}

}  // namespace

void QuestTick(World& world, const QuestPlan& plan, const OrePlan& ores, float dt)
{
    Quest& q = world.quest;
    if (!q.valid() || q.done)
        return;
    if (q.def >= (int)plan.defs.size())
        return;
    if (world.frozen)
        return;

    const QuestDef& def = plan.defs[(std::size_t)q.def];

    switch (def.kind)
    {
    case QuestKind::Count:
        q.progress = CountValue(world, q, def.metric);
        if (q.progress >= q.amount)
            q.done = true;
        break;

    case QuestKind::Hold:
    {
        // "nichts verkauft" ist der eine Fall, der nicht an einem Zustand
        // haengt, sondern an einem Ereignis - deshalb hier und nicht in Holds().
        bool gilt = Holds(world, ores, q, def.metric);
        if (def.metric == QuestMetric::NoSell && world.stats.soldPieces > 0)
            gilt = false;

        if (gilt)
            q.held += dt;
        else
            q.held = 0.0f;  // losgelassen heisst von vorne

        q.progress = (int)q.held;
        if (q.held >= q.seconds)
            q.done = true;
        break;
    }

    case QuestKind::Once:
        q.progress = OnceProgress(world, q, def.metric);
        if (OnceDone(world, q, def.metric))
            q.done = true;
        break;
    }
}

int QuestSettle(World& world, const QuestPlan& plan, const OrePlan& ores)
{
    Quest& q = world.quest;
    if (!q.valid())
        return 0;

    // Ein letztes Mal nachsehen: der letzte Verkauf der Runde soll noch zaehlen.
    QuestTick(world, plan, ores, 0.0f);

    int veraenderung = 0;

    if (q.done)
    {
        veraenderung = q.reward;
        world.money += q.reward;
    }
    else
    {
        // Nie ins Minus. Ein negativer Kontostand ist im Spiel nirgends
        // vorgesehen, und eine Strafe, die groesser ist als alles, was man hat,
        // waere ohnehin nicht mehr als "alles weg".
        int abzug = q.penalty;
        if (abzug > world.money)
            abzug = world.money;

        veraenderung = -abzug;
        world.money -= abzug;
    }

    world.questLastText   = q.text;
    world.questLastWon    = q.done;
    world.questLastChange = veraenderung;

    q = Quest();  // der Auftrag gilt fuer EINE Runde
    return veraenderung;
}

// ---------------------------------------------------------------------------
// Die Tafel
// ---------------------------------------------------------------------------

namespace
{

// Wie hoch eine Karte sein muss, damit ihr Text hineinpasst. Der Auftragstext
// wird umgebrochen, und je nach Laenge sind das eine bis drei Zeilen - eine
// feste Hoehe schnitt der laengsten Karte die Geldzeile ab.
float CardHeight(const Quest& q, float breite)
{
    const float innen = breite - 20.0f;
    const float text  = ImGui::CalcTextSize(q.text.c_str(), nullptr, false, innen).y;

    // Text, Abstand, die Zeile mit Lohn und Strafe, Rand.
    return text + ImGui::GetTextLineHeightWithSpacing() + 26.0f;
}

// Eine Karte mit einem Angebot. Rueckgabe: angenommen.
//
// Die Hoehe kommt von aussen und ist fuer alle Karten dieselbe - sonst stuenden
// drei verschieden hohe Kaesten nebeneinander.
bool OfferCard(const Quest& q, float breite, float hoehe, bool machbar)
{
    bool genommen = false;

    ImGui::PushID(&q);
    ImGui::BeginGroup();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::V(ui::kCard));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::BeginChild("##karte", ImVec2(breite, hoehe), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::PushTextWrapPos(breite - 20.0f);
    ImGui::TextColored(ui::V(ui::kText), "%s", q.text.c_str());
    ImGui::PopTextWrapPos();

    // Die Geldzeile sitzt unten am Rand, nicht direkt unter dem Text: so steht
    // sie auf allen drei Karten auf derselben Linie, auch wenn der Text
    // darueber verschieden lang ist.
    ImGui::SetCursorPosY(hoehe - ImGui::GetTextLineHeightWithSpacing() - 8.0f);

    // Beides nebeneinander: was es bringt und was es kostet. Genau diese zwei
    // Zahlen sind die ganze Entscheidung.
    ImGui::TextColored(ImVec4(0.35f, 0.72f, 0.42f, 1.0f), "+%s", ui::Money(q.reward).c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.85f, 0.36f, 0.32f, 1.0f), "  -%s if you fail",
                       ui::Money(q.penalty).c_str());

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (machbar && ImGui::Button("Accept", ImVec2(breite, 0.0f)))
        genommen = true;

    ImGui::EndGroup();
    ImGui::PopID();

    return genommen;
}

}  // namespace

void DrawQuestBoard(World& world, const QuestPlan& plan, const OrePlan& ores,
                    const Limits& limits)
{
    if (!limits.allowQuests)
        return;

    ImGuiViewport* vp = ImGui::GetMainViewport();

    // ---- Waehrend der Runde: eine Zeile, mehr braucht es nicht ------------
    if (world.phase == RoundPhase::Run)
    {
        if (!world.quest.valid())
            return;

        const Quest& q = world.quest;

        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 8.0f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.92f);

        if (ImGui::Begin("##auftrag", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
        {
            if (q.done)
                ImGui::TextColored(ImVec4(0.35f, 0.72f, 0.42f, 1.0f), "Contract done:  %s",
                                   q.text.c_str());
            else
                ImGui::TextColored(ui::V(ui::kText), "Contract:  %s", q.text.c_str());

            char rest[64];
            if (q.seconds > 0.0f)
                std::snprintf(rest, sizeof(rest), "%d / %d s", (int)q.held, (int)q.seconds);
            else
                std::snprintf(rest, sizeof(rest), "%d / %d", q.progress, q.amount);

            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                  q.done ? ImVec4(0.35f, 0.72f, 0.42f, 1.0f)
                                         : ui::V(ui::kAccent));
            ImGui::ProgressBar(q.ratio(), ImVec2(340.0f, 0.0f), rest);
            ImGui::PopStyleColor();
        }
        ImGui::End();
        return;
    }

    // ---- Vorbereitung: die drei Angebote ---------------------------------
    if (world.phase != RoundPhase::Prepare)
        return;

    if (world.quest.valid())
    {
        // Schon eines angenommen - dann steht hier nur noch, welches.
        ImGui::SetNextWindowPos(
            ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 8.0f), ImGuiCond_Always,
            ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.92f);

        if (ImGui::Begin("##auftragfest", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
        {
            ImGui::TextColored(ui::V(ui::kText), "Accepted:  %s", world.quest.text.c_str());
            ImGui::TextDisabled("+%s   /   -%s if you fail",
                                ui::Money(world.quest.reward).c_str(),
                                ui::Money(world.quest.penalty).c_str());

            // Zurueckgeben heisst: diese Runde ohne. Wuerden hier sofort drei
            // neue Angebote nachwachsen, waere der Knopf ein Wuerfelbecher -
            // man drueckte so lange, bis etwas Bequemes dabei ist.
            if (ImGui::SmallButton("Hand it back"))
            {
                world.quest         = Quest();
                world.questDeclined = true;
            }
        }
        ImGui::End();
        return;
    }

    if (world.questOffers.empty())
        return;

    const float breite = 260.0f;
    const float ganz   = breite * (float)world.questOffers.size() +
                       18.0f * (float)(world.questOffers.size() - 1);

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 8.0f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.94f);

    if (ImGui::Begin("##angebote", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
    {
        ImGui::TextColored(ui::V(ui::kText), "Contracts for this round");
        ImGui::TextDisabled(
            "Take one, or none - a round without a contract is fine. It runs for this round only.");
        ImGui::Spacing();

        // Alle Karten so hoch wie die hoechste.
        float hoehe = 0.0f;
        for (const Quest& q : world.questOffers)
        {
            const float h = CardHeight(q, breite);
            if (h > hoehe)
                hoehe = h;
        }

        int nehmen = -1;
        for (std::size_t i = 0; i < world.questOffers.size(); ++i)
        {
            if (i > 0)
                ImGui::SameLine(0.0f, 18.0f);
            if (OfferCard(world.questOffers[i], breite, hoehe, true))
                nehmen = (int)i;
        }

        (void)ores;

        if (nehmen >= 0)
            QuestAccept(world, nehmen);

        // Alle drei ablehnen. Kostet nichts und bringt nichts - aber es muss
        // gehen, sonst waere "einen nehmen oder keinen" eine leere Zusage.
        ImGui::Spacing();
        if (ImGui::Button("No contract this round", ImVec2(ganz, 0.0f)))
        {
            world.questOffers.clear();
            world.questDeclined = true;
        }
    }
    ImGui::End();
}
