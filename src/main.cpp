// Code Miner
//
// Das Spiel selbst - plattformfrei. Fenster und Grafik stehen in platform.h
// (Win32+DirectX 11 bzw. GLFW+OpenGL 3), alles, was mit Prozessen und Dateien
// zu tun hat, in proc.h. Hier drin kommt kein #ifdef mehr vor.

#include "alloy.h"
#include "codecheck.h"
#include "console.h"
#include "market.h"
#include "quest.h"
#include "craft.h"
#include "native.h"
#include "ore.h"
#include "oregen.h"
#include "platform.h"
#include "curios.h"
#include "prestige.h"
#include "round.h"
#include "save.h"
#include "skillfile.h"
#include "skills.h"
#include "skilltree.h"
#include "theme.h"
#include "wiki.h"
#include "world.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

static void ApplyStyle()
{
    ImGui::StyleColorsLight();

    ImGuiStyle& s        = ImGui::GetStyle();
    s.WindowRounding     = ui::kRound;
    s.ChildRounding      = ui::kRound;
    s.FrameRounding      = ui::kRoundS;
    s.GrabRounding       = ui::kRoundS;
    s.PopupRounding      = ui::kRound;
    s.ScrollbarRounding  = 6.0f;
    s.WindowBorderSize   = 1.0f;
    s.FrameBorderSize    = 0.0f;
    s.ChildBorderSize    = 1.0f;
    s.WindowPadding      = ImVec2(ui::kCardPad, 14.0f);
    s.FramePadding       = ImVec2(10.0f, 6.0f);
    s.ItemSpacing        = ImVec2(10.0f, 8.0f);
    s.ItemInnerSpacing   = ImVec2(8.0f, 6.0f);
    s.ScrollbarSize      = 11.0f;
    s.WindowTitleAlign   = ImVec2(0.0f, 0.5f);
    s.SeparatorTextAlign = ImVec2(0.0f, 0.5f);

    // Die Palette steht in theme.h - hier wird sie nur an ImGui gereicht.
    ImVec4* c = s.Colors;

    c[ImGuiCol_WindowBg]        = ui::V(ui::kCard);
    c[ImGuiCol_ChildBg]         = ui::V(ui::kCard);
    c[ImGuiCol_PopupBg]         = ui::V(ui::kCard);
    c[ImGuiCol_MenuBarBg]       = ui::V(ui::kCard);
    c[ImGuiCol_Border]          = ui::V(ui::kBorder);
    c[ImGuiCol_BorderShadow]    = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_Text]            = ui::V(ui::kText);
    c[ImGuiCol_TextDisabled]    = ui::V(ui::kTextDim);
    c[ImGuiCol_TextSelectedBg]  = ui::V(ui::kAccentDim);

    c[ImGuiCol_FrameBg]         = ui::V(ui::kSunken);
    c[ImGuiCol_FrameBgHovered]  = ui::V(ui::kAccentDim);
    c[ImGuiCol_FrameBgActive]   = ui::V(ui::kAccentDim);

    c[ImGuiCol_TitleBg]         = ui::V(ui::kCard);
    c[ImGuiCol_TitleBgActive]   = ui::V(ui::kCard);
    c[ImGuiCol_TitleBgCollapsed]= ui::V(ui::kCard);

    c[ImGuiCol_Button]          = ui::V(ui::kSunken);
    c[ImGuiCol_ButtonHovered]   = ui::V(ui::kAccentDim);
    c[ImGuiCol_ButtonActive]    = ui::V(ui::kBorderS);

    c[ImGuiCol_Header]          = ui::V(ui::kSunken);
    c[ImGuiCol_HeaderHovered]   = ui::V(ui::kAccentDim);
    c[ImGuiCol_HeaderActive]    = ui::V(ui::kAccentDim);

    c[ImGuiCol_Separator]       = ui::V(ui::kBorder);
    c[ImGuiCol_SeparatorHovered]= ui::V(ui::kBorderS);
    c[ImGuiCol_SeparatorActive] = ui::V(ui::kAccent);

    c[ImGuiCol_ScrollbarBg]     = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]   = ui::V(ui::kBorder);
    c[ImGuiCol_ScrollbarGrabHovered] = ui::V(ui::kBorderS);
    c[ImGuiCol_ScrollbarGrabActive]  = ui::V(ui::kTextWk);

    c[ImGuiCol_TableHeaderBg]   = ui::V(ui::kCard);
    c[ImGuiCol_TableBorderLight]= ui::V(ui::kBorder);
    c[ImGuiCol_TableBorderStrong] = ui::V(ui::kBorderS);
    c[ImGuiCol_TableRowBg]      = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]   = ui::V(IM_COL32(0xFA, 0xF8, 0xF5, 255));

    c[ImGuiCol_ResizeGrip]        = ui::V(ui::kBorder);
    c[ImGuiCol_ResizeGripHovered] = ui::V(ui::kBorderS);
    c[ImGuiCol_ResizeGripActive]  = ui::V(ui::kAccent);

    c[ImGuiCol_CheckMark]       = ui::V(ui::kAccent);
    c[ImGuiCol_SliderGrab]      = ui::V(ui::kAccent);
    c[ImGuiCol_SliderGrabActive]= ui::V(ui::kAccentHot);
}

// Welche Seite ist gerade offen? Die Seiten ersetzen einander, sie liegen
// nicht nebeneinander.
enum class Page
{
    Welt,
    Tasche,
    Skills,
    Wiki,

    // Der Markt. Es gibt ihn erst mit dem Punkt "chart" im Baum - vorher wird
    // der Reiter nicht gezeichnet und die Seite nie geoeffnet.
    Markt,

    // Das Erbe: Erbe-Punkte gegen dauerhafte Boni, ueberlebt "Start over".
    // Siehe prestige.h.
    Erbe,

    // Gefundene Kuriositaeten - siehe world.h. Der Reiter taucht erst auf,
    // sobald es ueberhaupt eine gibt: vorher soll man von der Seite so wenig
    // wissen wie von den Dingern selbst.
    Kurios
};

// Reiter in der Menueleiste. Die offene Seite wird hervorgehoben.
//
// breite 0 heisst "so breit wie die Aufschrift". Reiter mit einer Zahl darin
// geben eine feste Breite mit - siehe CountTab weiter unten.
static bool PageTab(const char* label, bool active, float breite = 0.0f)
{
    // Der offene Reiter ist eine helle Pille mit Rand, die anderen sind nur
    // Text. Kein Farbklecks - die kraeftige Farbe bleibt dem Wichtigen
    // vorbehalten (Geld, Startknopf).
    ImGui::PushStyleColor(ImGuiCol_Button, active ? ui::V(ui::kCard) : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          active ? ui::V(ui::kCard) : ui::V(ui::kSunken));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::V(ui::kSunken));
    ImGui::PushStyleColor(ImGuiCol_Text, active ? ui::V(ui::kText) : ui::V(ui::kTextDim));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, active ? 1.0f : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 7.0f));

    const bool clicked = ImGui::Button(label, ImVec2(breite, 0.0f));

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return clicked;
}

// ---- Reiter mit einer Zahl dahinter ---------------------------------------
//
// "Tasche (12)". Die Zahl aendert sich beim Abbauen im Sekundentakt, und wenn
// der Knopf dabei breiter wird, huepft alles dahinter mit. Deshalb:
//
//   - die Zahl steht rechtsbuendig in einem Feld fester Breite,
//   - ueber kZaehlerMax steht "999+" statt einer immer laengeren Zahl,
//   - ist nichts da, haelt ein "-" den Platz frei,
//   - und der Knopf ist immer so breit wie seine laengstmoegliche Aufschrift.
//
// Der Teil hinter ### ist die ImGui-Kennung. Sie MUSS gleich bleiben: sonst
// gehoert der Knopf beim Loslassen der Maus einer anderen Kennung als beim
// Druecken, und der Klick faellt unter den Tisch - genau dann, wenn ein
// Programm laeuft und staendig etwas abbaut.
static constexpr int kZaehlerMax = 999;

static void CountText(char* out, std::size_t size, const char* name, int count)
{
    char zahl[16];
    if (count <= 0)
        std::snprintf(zahl, sizeof(zahl), "-");
    else if (count > kZaehlerMax)
        std::snprintf(zahl, sizeof(zahl), "%d+", kZaehlerMax);
    else
        std::snprintf(zahl, sizeof(zahl), "%d", count);

    std::snprintf(out, size, "%s (%4s)###%s", name, zahl, name);
}

static float CountWidth(const char* name)
{
    char breiteste[64];
    std::snprintf(breiteste, sizeof(breiteste), "%s (%4s)", name, "999+");
    return ImGui::CalcTextSize(breiteste).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

static bool CountTab(const char* name, int count, bool active)
{
    char label[64];
    CountText(label, sizeof(label), name, count);
    return PageTab(label, active, CountWidth(name));
}

// Wie breit die Geldanzeige wird. Der Knopf daneben muss das vorher wissen,
// um sich davor zu setzen - deshalb steht die Rechnung an einer Stelle.
static float MoneyWidth(const World& world)
{
    const std::string text = ui::Money(world.money);
    return 14.0f * 2.0f + 5.0f * 2.0f + 9.0f + ImGui::CalcTextSize(text.c_str()).x;
}

// Wie breit der Menue-Knopf ganz rechts ist.
static float MenuWidth()
{
    return ImGui::CalcTextSize("...").x + 22.0f;
}

// Geldanzeige oben rechts: eine Pille mit Muenze und Zahl.
//
// "rechts" ist der Abstand vom rechten Fensterrand bis zur rechten Kante der
// Pille. Ohne den setzten sich Geld und Menue-Knopf beide ganz nach aussen und
// lagen uebereinander.
static void DrawMoney(const World& world, float rechts)
{
    const std::string text = ui::Money(world.money);

    const float punkt = 5.0f;
    const float innen = 14.0f;
    const float breit = MoneyWidth(world);
    const float hoch  = ImGui::GetTextLineHeight() + 12.0f;

    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - breit - rechts);

    const ImVec2 p  = ImGui::GetCursorScreenPos();
    const float  y0 = p.y + (ImGui::GetTextLineHeight() - hoch) * 0.5f;
    ImDrawList*  dl = ImGui::GetWindowDrawList();

    ui::Card(dl, ImVec2(p.x, y0), ImVec2(p.x + breit, y0 + hoch));

    const float cy = y0 + hoch * 0.5f;
    dl->AddCircleFilled(ImVec2(p.x + innen + punkt, cy), punkt, ui::kCoin);
    dl->AddText(ImVec2(p.x + innen + punkt * 2.0f + 9.0f, cy - ImGui::GetTextLineHeight() * 0.5f),
                ui::kText, text.c_str());

    ImGui::Dummy(ImVec2(breit, 0.0f));
}

// Consolas liegt auf jedem Windows - damit braucht das Programm keine
// mitgelieferte Schriftdatei.
int main(int, char**)
{
    if (!plat::Init("Code Miner", 1280, 800))
        return 1;

    // Erst das Fenster, dann das Aussehen: ApplyStyle und die Schrift brauchen
    // den ImGui-Kontext, den plat::Init aufmacht.
    ApplyStyle();
    plat::LoadFont();

    // ---- Testschalter ----------------------------------------------------
    //
    // Beide aus: normales Spiel. kUnendlichGeld fuellt das Geld in jedem Bild
    // wieder auf - nur zum Ausprobieren gedacht.
    //
    // Achtung: Geld ist ein int, mehr als rund 2 Milliarden passen da nicht
    // rein. Eine groessere Zahl laeuft ueber und wird negativ.
    const bool kUnendlichGeld = false;
    const int  kVielGeld      = 2000000000;

    // Auftraege sofort, ohne sie im Baum zu kaufen. Nur zum Anschauen: im
    // fertigen Spiel haengen sie an "quests" ab Schritt 30, und das ist mit
    // Absicht so weit hinten (siehe data/skills.txt).
    //
    // kAlleAuftraege zeigt dazu den GANZEN Katalog - auch die Auftraege, deren
    // Punkt noch fehlt oder deren Runde noch nicht erreicht ist. Sonst saehe
    // man in Runde 1 nur die drei, die ohne alles auskommen.
    //
    // Beide vor dem Ausliefern wieder auf false.
    const bool kAuftraegeSofort = false;
    const bool kAlleAuftraege   = false;

    // Der Markt sofort: der Reiter "Market" ist da, und die Preise schwanken,
    // ohne dass "market" und "chart" im Baum gekauft sind. Zum Anschauen der
    // Seite - im fertigen Spiel haengt sie an den beiden Punkten.
    //
    // Vor dem Ausliefern wieder auf false.
    const bool kMarktSofort = false;

    // Startgeld. Beim Zuruecksetzen kommt genau das wieder.
    const int kStartGeld = kUnendlichGeld ? kVielGeld : 0;

    // Welche Erze es gibt, steht in data/erze.json.
    OrePlan ores = LoadOrePlan();

    // Und was man daraus machen kann, in data/verarbeitung.json.
    const CraftPlan crafts = LoadCraftPlan();

    // Legierungen haengen ihre Ergebnisse hinten an die Erzliste an - deshalb
    // erst hier und deshalb ist die Erzliste oben nicht const.
    AlloyPlan alloys = LoadAlloyPlan(ores);

    // Ab hier ist alles Handgeschriebene beisammen. Was danach noch dazukommt,
    // hat sich das Spiel selbst ausgedacht und steht im Spielstand.
    ores.handmade = (int)ores.ores.size();

    // Die Regeln fuers Wuerfeln. Gewuerfelt wird erst spaeter und dann immer
    // nur eines: wenn das Level so weit ist.
    OreGenPlan oreGen = LoadOreGenPlan();
    for (const std::string& p : oreGen.problems)
        ores.problems.push_back(p);
    oreGen.problems.clear();

    // Was im Wiki steht, kommt aus data/wiki.json. Es wird einmal gelesen und
    // danach nur noch angezeigt - erklaeren aendert nichts am Spiel.
    const WikiBook wiki = LoadWikiBook();

    // Wie lange eine Runde dauert und was in der Vorbereitung still steht,
    // steht in data/runden.json. Fuer eine kurze Testrunde einfach dort die
    // Zahl kleiner machen - hier steht sie mit Absicht nicht.
    //
    // Nicht mehr const: das Erbe (siehe unten) darf Rundenzeit und Rundenziel
    // dauerhaft ein Stueck verschieben. Die Grundwerte aus der Datei bleiben
    // dafuer separat gemerkt.
    RoundPlan   rounds           = LoadRoundPlan();
    const float baseRoundSeconds = rounds.seconds;
    const int   baseTargetStart  = rounds.targetStart;

    // Das Erbe: Erbe-Punkte gegen dauerhafte Boni, ueberlebt "Start over".
    // Anders als der normale Spielstand liegt es in einer eigenen Datei und
    // wird von resetAll()/DeleteSave() nicht angeruehrt.
    const PrestigePlan prestigePlan = LoadPrestigePlan();
    Prestige           prestige;
    if (!LoadPrestige(prestige))
        RerollOffers(prestige, prestigePlan);  // allererster Start: erste Angebote wuerfeln

    // Kuriositaeten: aus demselben Grund in einer eigenen Datei - siehe
    // curios.h.
    Curios curios;
    LoadCurios(curios);

    World world;
    world.money = kStartGeld + ComputePrestigeEffects(prestige, prestigePlan).startMoneyBonus;

    // EIN Motor fuer alle Konsolen: sie sind zusammen ein Programm.
    Native engine;

    // Was wann freigeschaltet werden KANN, steht in data/skills.txt - nicht
    // hier. Der Baum waechst dann beim Spielen: erst beim Kauf wird gewuerfelt,
    // was dahinter kommt.
    const SkillPlan plan = LoadSkillPlan();

    // Die Auftraege. Fehlt die Datei, gibt es eben keine - das Spiel laeuft
    // trotzdem, eine Zusatzaufgabe darf nichts kaputtmachen.
    const QuestPlan quests = LoadQuestPlan();

    SkillTree tree;
    tree.start(plan, 20260808u);

    Page page = Page::Welt;

    std::vector<std::unique_ptr<Console>> consoles;
    int                                   nextId = 1;

    auto addConsole = [&]()
    {
        const float offset = (float)((nextId - 1) % 6) * 26.0f;
        consoles.push_back(std::make_unique<Console>(nextId, ImVec2(40.0f + offset, 60.0f + offset),
                                                     nextId == 1));
        ++nextId;
    };
    addConsole();

    // Alles auf Anfang - und der alte Spielstand weg.
    auto resetAll = [&]()
    {
        engine.stop();
        DeleteSave();

        world = World();
        world.money = kStartGeld + ComputePrestigeEffects(prestige, prestigePlan).startMoneyBonus;

        tree.start(plan, 20260808u);

        // Die gewuerfelten Erze gehen mit weg: ein neues Spiel faengt wieder
        // mit Stein und Kohle an und wuerfelt sich seine eigenen zusammen.
        ores.ores.resize((std::size_t)ores.handmade);
        ores.rolled = 0;
        while (!alloys.recipes.empty() && alloys.recipes.back().result >= ores.handmade)
            alloys.recipes.pop_back();

        consoles.clear();
        nextId = 1;
        addConsole();

        page = Page::Welt;
    };

    // Gab es schon einen Spielstand? Dann ersetzt er den frischen Anfang -
    // samt der Erze, die sich das Spiel damals ausgedacht hat.
    LoadGame(world, tree, consoles, nextId, ores, alloys);

    // Gespeichert wird nicht bei jeder Kleinigkeit, sondern alle paar Sekunden
    // und beim Beenden. Geld aendert sich staendig - jedes Mal auf die Platte
    // zu schreiben waere Unsinn.
    float saveTimer = 0.0f;

    // Das ganze Spiel anhalten. Nicht dasselbe wie die Pause an der Konsole:
    // die haelt nur das Programm an, waehrend die Runde weiterlaeuft. Hier
    // steht ALLES - Uhr, Block, Auftrag und Programm.
    //
    // Steht bewusst nicht im Spielstand: eine gespeicherte Pause waere beim
    // naechsten Start nur ein Spiel, das sich nicht ruehrt.
    bool paused = false;

    // Sammelt den Inhalt aller Konsolen ein - daraus wird ein Programm.
    auto collectSources = [&consoles]()
    {
        std::vector<SourceFile> files;
        for (const auto& c : consoles)
            files.push_back({c->id, c->editor.GetText()});
        return files;
    };

    // ---- Ein Bild simulieren: Uhr, Abbau, Verarbeitung, Auftrag ----------
    //
    // Alles, was voranschreitet, egal welche Seite gerade offen ist. Liefert
    // dt zurueck, weil der Motor es nach dem Zeichnen der Menueleiste noch
    // einmal braucht (siehe unten, engine.update()).
    auto simulateFrame = [&]() -> float
    {
        // Pausieren geht nur im Lauf - in der Vorbereitung steht die Welt
        // ohnehin still, und in der Abrechnung gibt es nichts mehr anzuhalten.
        if (world.phase != RoundPhase::Run)
            paused = false;
        else if (ImGui::IsKeyPressed(ImGuiKey_F9, false))
            paused = !paused;

        // Die Pause ist einfach "es vergeht keine Zeit". Dadurch stehen Uhr,
        // Abbau, Auftrag und Nachwachsen von selbst still - ohne dass jede
        // einzelne Stelle die Pause kennen muesste.
        const float dt = paused ? 0.0f : ImGui::GetIO().DeltaTime;

        // Das Level entscheidet, welche Erze ueberhaupt vorkommen. Woraus es
        // sich ergibt, steht in data/erze.json.
        if (ores.levelFrom == "skills")
        {
            int gekauft = 0;
            for (const SkillNode& n : tree.nodes)
                if (n.owned)
                    ++gekauft;
            world.level = gekauft;  // die Wurzel gehoert einem schon: Level 1
        }
        else
        {
            world.level = 1 + world.minedCount / ores.perLevel;
        }

        // Ist das Level weit genug, kommt ein neues, noch nie dagewesenes Erz
        // dazu. Meistens passiert hier gar nichts - und wenn doch, steht es ab
        // sofort im Spielstand und bleibt fuer immer.
        RollNewOres(ores, alloys, oreGen, world.level);

        // ---- Runden ------------------------------------------------------
        //
        // In der Vorbereitung steht die Welt still: nichts wird abgebaut,
        // nichts waechst nach, kein Auftrag laeuft. Die Welt selbst kennt
        // keine Runden - sie bekommt nur diese zwei Schalter.
        world.frozen = (rounds.freezeWorld && world.phase != RoundPhase::Run) || paused;

        // Waehrend der Abrechnung nicht: was danach noch in die Tasche faellt,
        // steht in keiner Rechnung mehr. In der Pause auch nicht - sonst waere
        // sie die beste Gelegenheit, in aller Ruhe von Hand abzubauen.
        world.handMine =
            rounds.handInPrepare && world.phase != RoundPhase::Report && !paused;

        if (world.phase == RoundPhase::Run)
        {
            world.roundLeft -= dt;
            if (world.roundLeft <= 0.0f)
            {
                // Zeit um: erst das Programm anhalten, dann abrechnen. Sonst
                // koennte das Kind noch eine Zeile mitten in den Verkauf
                // hineinfunken.
                engine.stop();
                FinishRound(world, rounds, ores, crafts, quests);

                // Die Abrechnung soll einen Absturz ueberleben - sie ist der
                // einzige Ort, an dem man das Ergebnis je zu sehen bekommt.
                SaveGame(world, tree, consoles, ores, alloys);
                saveTimer = 0.0f;
            }
        }

        // Abgebaut wird nur, solange das Programm laeuft.
        //
        // Pause haelt den Abbau an der Stelle an, Stopp bricht ihn ab. Sonst
        // wuerde der Block auch dann noch fertig abgebaut, wenn man laengst
        // gestoppt hat - es sieht dann so aus, als holte das Spiel etwas nach.
        if (world.byHand)
        {
            // Von Hand angeklickt: das laeuft immer, auch ohne Programm.
            world.tickMining(dt, ores, crafts, curios);
        }
        else
        {
            switch (engine.state())
            {
            case RunState::Paused: break;  // eingefroren
            case RunState::Idle: world.cancelMining(); break;
            default:
                world.tickMining(dt, ores, crafts, curios);
                break;  // laeuft oder gerade fertig
            }
        }

        // Verarbeiten haengt genauso am Programm - ausser der Auftrag wurde in
        // der Tasche angeklickt. Bricht er ab, kommt das Material zurueck.
        //
        // Entschieden wird das je Auftrag, nicht fuer alle zusammen: mit
        // mehreren Oefen laeuft der eine von Hand und der andere vom Programm.
        {
            switch (engine.state())
            {
            case RunState::Paused: world.tickCraft(dt, false); break;
            case RunState::Idle:
                world.cancelCraft(true);
                world.tickCraft(dt, false);
                break;
            default: world.tickCraft(dt, true); break;
            }
        }

        // Der Auftrag sieht sich an, was dieses Bild passiert ist. Muss NACH
        // dem Abbauen und Verarbeiten kommen - sonst hinkt der Fortschritt
        // immer ein Bild hinterher.
        QuestTick(world, quests, ores, dt);

        // Testschalter: Geld laeuft nie aus.
        if (kUnendlichGeld)
            world.money = kVielGeld;

        // Zeit vergeht auf jeder Seite: der Block waechst auch nach, waehrend
        // man im Skilltree ist - aber nur waehrend der Runde. In der
        // Vorbereitung steht er (siehe world.frozen).
        world.update(dt, ores);

        saveTimer += dt;
        if (saveTimer >= 10.0f)
        {
            SaveGame(world, tree, consoles, ores, alloys);
            SavePrestige(prestige);  // Kaeufe auf der Legacy-Seite sollen auch ohne Verlieren bleiben
            SaveCurios(curios);      // ueberlebt aus demselben Grund wie das Erbe
            saveTimer = 0.0f;
        }

        return dt;
    };

    // ---- Was der Spieler gerade darf - Baum plus Erbe ---------------------
    auto computeLimits = [&]() -> Limits
    {
        // Die Werte kommen aus dem Baum - jedes Bild neu ausgerechnet. Dadurch
        // stimmen sie immer, egal in welcher Reihenfolge gekauft wurde.
        Limits limits = tree.limits();

        // Testschalter: die Tafel gibt es sofort.
        if (kAuftraegeSofort)
            limits.allowQuests = true;

        // Testschalter: die Marktseite und die Funktionen dazu sofort. Im
        // fertigen Spiel kommt erst die Seite ("chart") und ein gutes Stueck
        // spaeter market.price() ("market") - siehe data/skills.txt.
        if (kMarktSofort)
        {
            limits.allowChart  = true;
            limits.allowMarket = true;
        }

        // Das Erbe legt sich ueber die Werte aus dem Baum - genauso jedes Bild
        // neu, damit ein Kauf auf der Erbe-Seite sofort wirkt, auch ohne
        // Neustart.
        const PrestigeEffects peff = ComputePrestigeEffects(prestige, prestigePlan);

        rounds.seconds     = baseRoundSeconds + peff.extraSeconds;
        rounds.targetStart = std::max(0, (int)std::llround((double)baseTargetStart * peff.targetMul));

        limits.maxJobs     += peff.extraJobs;
        limits.maxConsoles += peff.extraConsoles;
        limits.assayCost = std::max(1, (int)std::llround((double)limits.assayCost * peff.assayCostMul));

        world.moneyPerBlock  = std::max(1, (int)std::llround((double)limits.moneyPerBlock * peff.moneyMul));
        world.respawnSeconds = std::max(0.05f, limits.respawnSeconds * peff.respawnMul);
        engine.setSpeed(limits.linesPerSecond * peff.speedMul);

        // Was die Runde verlangt. Die Welt rechnet es nicht selbst aus, sie
        // kennt data/runden.json nicht - sie bekommt nur das Ergebnis, damit
        // round.target() im Spielercode danach fragen kann.
        world.roundTargetNow = RoundTarget(rounds, world.roundNumber);

        // Wie viele Oefen es gibt, sagt der Baum - genau wie beim Tempo. Die
        // Welt merkt sich das nicht selbst, sonst muesste es in den Spielstand.
        world.setJobSlots(limits.maxJobs);

        // Wie stark der Markt schwankt, ist Balance und steht deshalb in
        // data/skills.txt und nicht im Programm.
        //
        // Und er schwankt erst, wenn man ihn auch SIEHT - also mit der
        // Marktseite, nicht mit market.price(). Ein Preis, der sich bewegt,
        // waehrend niemand hinschaut, ist ein Wuerfel: man haette denselben
        // Stapel mal fuer 80 und mal fuer 120 verkauft und nie erfahren,
        // warum. Bis dahin hat alles seinen festen Grundwert.
        //
        // Erst danach kommt market.price() - da weiss man dann schon, WARUM
        // man die Zahl haben will.
        world.marketSwing = limits.allowChart ? plan.marketSwing : 0.0f;
        world.marketSpeed = plan.marketSpeed;

        // Der Baum kann einem die Seite wieder wegnehmen (Spielstand geloescht,
        // Baum zurueckgesetzt). Dann waere man auf einer Seite, zu der es
        // keinen Reiter mehr gibt.
        if (page == Page::Markt && !limits.allowChart)
            page = Page::Welt;

        return limits;
    };

    // ---- Die Menueleiste: Reiter, Konsolen-Knopf, Geld, "..."-Menue -------
    auto drawTopBar = [&](const Limits& limits)
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (PageTab("World", page == Page::Welt))
                page = Page::Welt;

            // Die Zahl im Reiter: dann sieht man auch von der Welt-Seite aus,
            // dass sich in der Tasche etwas angesammelt hat.
            if (CountTab("Bag", world.inventoryCount(), page == Page::Tasche))
                page = Page::Tasche;

            if (PageTab("Skills", page == Page::Skills))
                page = Page::Skills;

            // Der Markt kommt erst mit dem Punkt "chart" dazu. Ein Reiter,
            // hinter dem nichts steht, waere ein Versprechen ohne Deckung.
            if (limits.allowChart && PageTab("Market", page == Page::Markt))
                page = Page::Markt;

            // Das Wiki ist immer erreichbar - auch in der Vorbereitung. Wer
            // nachschlaegt, soll dafuer keine Runde verbrauchen.
            // Neu freigeschaltete Seiten stehen als Zahl im Reiter - sonst
            // merkt man erst beim Hineinschauen, dass es etwas Neues gibt.
            if (CountTab("Wiki", WikiUnseen(wiki, limits, world, ores), page == Page::Wiki))
                page = Page::Wiki;

            // Das Erbe ist immer erreichbar, auch wenn gerade nichts zu holen
            // ist - Erbe-Punkte sammeln sich ja nur beim Verlieren an, und man
            // soll trotzdem jederzeit nachschauen koennen, was schon da ist.
            if (CountTab("Legacy", prestige.points, page == Page::Erbe))
                page = Page::Erbe;

            // Taucht erst auf, wenn es die erste ueberhaupt gibt - kein Reiter
            // fuer etwas, das man noch nie gesehen hat.
            if (!curios.found.empty() &&
                PageTab("Finds", page == Page::Kurios))
                page = Page::Kurios;

            // Wo die Reiter aufgehoert haben - falls es gerade so viele sind,
            // dass der rechts angepinnte Knopf unten sonst mitten hineinragen
            // wuerde (siehe dort).
            const float tabsEndX = ImGui::GetCursorPosX();

            // Rechts stehen die Sachen, die zur Seite gehoeren - Geld ganz
            // aussen, davor der Knopf fuer eine weitere Konsole. Links die
            // Reiter, rechts das Handwerkszeug: so faellt beides auseinander.
            if (page == Page::Welt)
            {
                // Wie viele Konsolen erlaubt sind, steht im Skilltree.
                const bool  moreAllowed = (int)consoles.size() < limits.maxConsoles;
                const char* label       = "+ New Console";
                const float breit       = ImGui::CalcTextSize(label).x + 28.0f;

                // Normalerweise rechtsbuendig - aber steht gerade eine lange
                // Reihe Reiter da (z. B. weil "Finds" dazugekommen ist), darf
                // der Knopf sie nicht ueberdecken. Dann eben direkt dahinter,
                // mit demselben Abstand wie zwischen zwei Reitern.
                const float rechtsbuendig = ImGui::GetWindowWidth() - MenuWidth() - 8.0f - 12.0f -
                                            MoneyWidth(world) - 12.0f - breit;
                ImGui::SetCursorPosX(std::max(tabsEndX + 12.0f, rechtsbuendig));

                ImGui::BeginDisabled(!moreAllowed);
                ImGui::PushStyleColor(ImGuiCol_Button, ui::V(ui::kCard));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::V(ui::kAccentDim));
                ImGui::PushStyleColor(ImGuiCol_Text, ui::V(ui::kAccent));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                if (ImGui::Button(label, ImVec2(breit, 0.0f)))
                    addConsole();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
                ImGui::EndDisabled();

                if (!moreAllowed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("More consoles are in the skill tree.");
            }

            DrawMoney(world, MenuWidth() + 8.0f + 12.0f);

            // ---- Das Menue ganz rechts ------------------------------------
            //
            // Hier sitzt alles, was zum Spiel gehoert und nicht zu einer
            // einzelnen Seite. Bisher ist das genau eine Sache: von vorne
            // anfangen. Sie steht mit Absicht hinter zwei Klicks und einer
            // Rueckfrage - versehentlich loescht sich sonst ein Nachmittag.
            {
                const float breit = MenuWidth();
                ImGui::SetCursorPosX(ImGui::GetWindowWidth() - breit - 8.0f);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ui::V(ui::kTextDim));
                if (ImGui::Button("...", ImVec2(breit, 0.0f)))
                    ImGui::OpenPopup("##menue");
                ImGui::PopStyleColor(2);

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Menu");

                // Der Nachfrage-Dialog darf NICHT im Menue liegen: ein
                // Selectable schliesst sein Popup, und der verschachtelte
                // Dialog ginge sofort mit zu. Deshalb merkt sich der Klick nur
                // einen Wunsch, und der Dialog wird eine Ebene hoeher geoeffnet.
                bool willNeuAnfangen = false;

                if (ImGui::BeginPopup("##menue"))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::V(ui::kBad));
                    if (ImGui::Selectable("Start over"))
                        willNeuAnfangen = true;
                    ImGui::PopStyleColor();
                    ImGui::EndPopup();
                }

                if (willNeuAnfangen)
                    ImGui::OpenPopup("##sicher");

                ImGuiViewport* vp = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(
                    ImVec2(vp->GetCenter().x, vp->GetCenter().y), ImGuiCond_Always,
                    ImVec2(0.5f, 0.5f));

                if (ImGui::BeginPopupModal("##sicher", nullptr,
                                           ImGuiWindowFlags_AlwaysAutoResize |
                                               ImGuiWindowFlags_NoTitleBar |
                                               ImGuiWindowFlags_NoSavedSettings))
                {
                    ImGui::TextUnformatted("Start over?");
                    ImGui::Spacing();
                    ImGui::TextColored(ui::V(ui::kTextDim),
                                       "Money, skill tree, bag and your code are gone.");
                    ImGui::TextColored(ui::V(ui::kTextDim),
                                       "The ores the game rolled up are gone too.");
                    ImGui::Spacing();
                    ImGui::Spacing();

                    ImGui::PushStyleColor(ImGuiCol_Button, ui::V(ui::kBad));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::V(ui::kBad));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                    // Breite aus der Aufschrift: fest waere geraten, und
                    // abgeschnitten sieht ein Warnknopf nicht vertrauenswuerdig aus.
                    const char* ja = "Yes, delete everything";
                    if (ImGui::Button(ja, ImVec2(ImGui::CalcTextSize(ja).x + 28.0f, 32.0f)))
                    {
                        resetAll();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleColor(3);

                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(110.0f, 32.0f)))
                        ImGui::CloseCurrentPopup();

                    ImGui::EndPopup();
                }
            }

            ImGui::EndMainMenuBar();
        }
    };

    // ---- Die offene Seite -------------------------------------------------
    auto drawActivePage = [&](const Limits& limits, float dt)
    {
        // Das Programm laeuft weiter, egal welche Seite offen ist - aber nur
        // waehrend der Runde. In der Vorbereitung ruehrt sich der Motor nicht.
        if (world.phase == RoundPhase::Run)
            // In der Pause gar nicht erst hinein: mit dt = 0 bekaeme das
            // Programm zwar keine Zeile mehr frei, aber ein gerade fertig
            // uebersetztes Programm wuerde noch starten und seine erste Zeile
            // ausfuehren. Was wartet, wartet eben bis nach der Pause.
            if (!paused)
                engine.update(dt, world, ores, crafts, alloys, limits);

        if (page == Page::Welt)
        {
            DrawWorld(world, ores, crafts, alloys, rounds);
            DrawStatus(limits, world);

            bool trigger = false;
            for (auto& c : consoles)
                if (DrawConsole(*c, engine))
                    trigger = true;

            if (trigger && world.phase != RoundPhase::Run)
            {
                // Ohne laufende Runde gibt es nichts zu tun. Das gehoert
                // gesagt - sonst haelt man den Knopf fuer kaputt.
                engine.fail("Start the round first - in the bar at the bottom.", 0, 0);
            }
            else if (trigger)
            {
                const bool anyEdited =
                    std::any_of(consoles.begin(), consoles.end(),
                                [](const std::unique_ptr<Console>& c) { return c->edited; });

                if (engine.state() == RunState::Paused && !anyEdited)
                {
                    engine.togglePause();
                }
                else if (engine.state() == RunState::Running)
                {
                    engine.togglePause();
                }
                else
                {
                    // Von vorne: alle Konsolen zusammen sind das Programm.
                    for (auto& c : consoles)
                        c->edited = false;

                    const std::vector<SourceFile> sources = collectSources();

                    // Erst der Skilltree, dann der Compiler. Wer etwas benutzt,
                    // das noch nicht gekauft ist, soll das sofort erfahren -
                    // und nicht erst nach einer halben Sekunde Kompilieren.
                    int         errConsole = 0;
                    int         errLine    = 0;
                    std::string problem    = CheckLimits(sources, limits, errConsole, errLine);

                    if (!problem.empty())
                        engine.fail(problem, errConsole, errLine);
                    else
                        engine.start(sources, ores);
                }
            }
        }
        else if (page == Page::Tasche)
        {
            DrawInventory(world, ores, crafts, limits);
        }
        else if (page == Page::Wiki)
        {
            DrawWikiPage(wiki, limits, world, ores, crafts);
        }
        else if (page == Page::Markt)
        {
            DrawMarketPage(world, ores, crafts, limits);
        }
        else if (page == Page::Erbe)
        {
            DrawPrestigePage(prestige, prestigePlan);
        }
        else if (page == Page::Kurios)
        {
            DrawCuriosityPage(curios);
        }
        else
        {
            DrawSkillPage(world, tree);

            // Dieselbe Ecke wie auf der Welt-Seite: was man schon hat. Beim
            // Kaufen will man ja sehen, was man damit ueberhaupt schon kann.
            DrawStatus(limits, world);
        }
    };

    while (plat::BeginFrame())
    {
        const float  dt     = simulateFrame();
        const Limits limits = computeLimits();

        drawTopBar(limits);

        // Verkauft: der Hinweis oben rechts soll man auf jeder Seite sehen,
        // nicht nur auf der Welt-Seite - deshalb hier und nicht in DrawWorld().
        DrawSellToast(world);

        drawActivePage(limits, dt);

        // Die Abrechnung liegt ueber allem - sie ist der Weg zur naechsten
        // Runde, egal auf welcher Seite man gerade ist.
        // Die Runde schwebt ueber jeder Seite - sie gehoert zu allen.
        bool togglePause = false;
        // Die Tafel. In der Vorbereitung liegen die Angebote da, im Lauf steht
        // der angenommene Auftrag oben mit seinem Balken.
        //
        // Gewuerfelt wird, sobald nichts dasteht: nach dem Rundenwechsel, nach
        // dem Zurueckgeben und beim allerersten Freischalten.
        if (limits.allowQuests && world.phase == RoundPhase::Prepare &&
            world.questOffers.empty() && !world.quest.valid() && !world.questDeclined)
            QuestRollOffers(world, quests, ores, rounds, limits, kAlleAuftraege);

        DrawQuestBoard(world, quests, ores, limits);

        if (world.phase != RoundPhase::Report && DrawRoundHud(world, rounds, paused, togglePause))
            StartRound(world, rounds);

        if (togglePause)
            paused = !paused;

        // Ziel verfehlt und die Datei sagt "alles auf Anfang": dann ist das
        // Spiel wirklich vorbei, nicht nur die Runde - und genau dann gibt es
        // Erbe-Punkte, die den Neuanfang ueberleben. Schon HIER ausgerechnet,
        // nicht erst nach dem Klick: die Abrechnung soll die Zahl vorher
        // zeigen koennen (siehe DrawRoundReport).
        const bool verloren = !RoundWon(world, rounds) && rounds.resetOnLoss &&
                             RoundTarget(rounds, world.roundNumber) > 0;
        const int  legacyPreview =
            verloren ? PrestigePointsEarned(prestigePlan, world.roundNumber, world.money) : 0;

        if (world.phase == RoundPhase::Report &&
            DrawRoundReport(world, rounds, legacyPreview))
        {
            if (verloren)
            {
                prestige.points += legacyPreview;
                prestige.totalEarned += legacyPreview;
                RerollOffers(prestige, prestigePlan);
                SavePrestige(prestige);
                SaveCurios(curios);  // unangetastet von resetAll(), aber sicher ist sicher
                resetAll();
            }
            else
                NextRound(world, rounds);
        }

        // Geschlossene Konsolen entfernen
        for (std::size_t i = consoles.size(); i-- > 0;)
            if (!consoles[i]->open)
                consoles.erase(consoles.begin() + (long long)i);

        // Der Seitengrund. Alles andere sind Karten darauf.
        plat::EndFrame(ui::V(ui::kPage));
    }

    SaveGame(world, tree, consoles, ores, alloys);
    SavePrestige(prestige);
    SaveCurios(curios);

    plat::Shutdown();
    return 0;
}

