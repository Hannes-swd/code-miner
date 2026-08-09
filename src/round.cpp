#include "round.h"

#include "json.h"
#include "theme.h"
#include "world.h"

#include "imgui.h"

// Wegen BeginViewportSideBar: die Leiste am unteren Rand zieht sich ihren Platz
// vom freien Bereich ab, genau wie die Menueleiste oben. Die Funktion steht bei
// ImGui in den Innereien - benutzt wird sie dort fuer BeginMainMenuBar selbst.
#include "imgui_internal.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace
{

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

        out.push_back(dir + "\\..\\..\\data\\runden.json");  // build/Debug -> Projekt
        out.push_back(dir + "\\..\\..\\..\\data\\runden.json");
        out.push_back(dir + "\\..\\data\\runden.json");
        out.push_back(dir + "\\data\\runden.json");
        out.push_back(dir + "\\runden.json");
    }

    out.push_back("data/runden.json");
    out.push_back("runden.json");
    return out;
}

// true/false lesen. Ein Zahlenwert waere hier ein Tippfehler und keine
// Abkuerzung - deshalb meckert es statt zu raten.
bool Flag(const JsonValue& root, const char* key, bool fallback,
          std::vector<std::string>& problems)
{
    const JsonValue* v = root.find(key);
    if (v == nullptr)
        return fallback;

    if (v->type != JsonValue::Type::Bool)
    {
        problems.push_back(std::string(key) + " must be true or false.");
        return fallback;
    }
    return v->flag;
}

// Farben der Restzeit. Die letzte Minute faellt auf - dann lohnt es sich nicht
// mehr, das Programm noch einmal umzubauen.
const ImVec4 kZeitRuhig(0.72f, 0.90f, 0.50f, 1.0f);
const ImVec4 kZeitKnapp(1.00f, 0.45f, 0.38f, 1.0f);

}  // namespace

RoundPlan LoadRoundPlan()
{
    RoundPlan plan;

    std::ifstream in;
    for (const std::string& path : Candidates())
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
        plan.problems.push_back("data/runden.json not found.");
        return plan;  // ohne Datei bleibt es bei 15 Minuten
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    JsonValue   wurzel;
    std::string fehler;
    if (!ParseJson(buffer.str(), wurzel, fehler))
    {
        plan.problems.push_back("data/runden.json - " + fehler);
        return plan;
    }

    plan.seconds = (float)wurzel.number("dauer", (double)plan.seconds);

    // Eine Runde unter einer Sekunde waere sofort wieder vorbei - das ist
    // sicher ein Tippfehler und kein Wunsch.
    if (plan.seconds < 1.0f)
    {
        plan.problems.push_back("dauer is smaller than 1 second.");
        plan.seconds = 1.0f;
    }

    plan.sellAtEnd     = Flag(wurzel, "verkauf_am_ende", plan.sellAtEnd, plan.problems);
    plan.freezeWorld   = Flag(wurzel, "welt_pausiert_in_vorbereitung", plan.freezeWorld,
                              plan.problems);
    plan.handInPrepare = Flag(wurzel, "handabbau_in_vorbereitung", plan.handInPrepare,
                              plan.problems);

    if (const JsonValue* ziel = wurzel.find("ziel"))
    {
        plan.targetStart  = (int)ziel->number("start", (double)plan.targetStart);
        plan.targetGrowth = (float)ziel->number("wachstum", (double)plan.targetGrowth);

        if (plan.targetStart < 0)
        {
            plan.problems.push_back("ziel.start is negative.");
            plan.targetStart = 0;
        }
        if (plan.targetGrowth < 1.0f)
        {
            // Unter 1 wuerde das Ziel kleiner werden - dann waere jede Runde
            // leichter als die davor.
            plan.problems.push_back("ziel.wachstum must be at least 1.");
            plan.targetGrowth = 1.0f;
        }
    }

    plan.resetOnLoss  = Flag(wurzel, "neustart_bei_niederlage", plan.resetOnLoss, plan.problems);
    plan.deductTarget = Flag(wurzel, "ziel_abziehen", plan.deductTarget, plan.problems);


    return plan;
}

int RoundTarget(const RoundPlan& plan, int round)
{
    if (round < 1)
        round = 1;

    double wert = (double)plan.targetStart;
    for (int i = 1; i < round; ++i)
        wert *= (double)plan.targetGrowth;

    // Ueber zwei Milliarden passt nicht mehr in einen int - und waere ohnehin
    // nicht mehr zu schaffen.
    if (wert > 2000000000.0)
        wert = 2000000000.0;

    return (int)wert;
}

bool RoundWon(const World& world, const RoundPlan& plan)
{
    // Nach der Runde zaehlt, was festgehalten wurde: das Ziel ist da schon
    // abgezogen, ein Vergleich mit dem Geld waere jetzt falsch.
    if (world.phase == RoundPhase::Report)
        return world.roundWon;

    return world.money >= RoundTarget(plan, world.roundNumber);
}

void StartRound(World& world, const RoundPlan& plan)
{
    world.phase     = RoundPhase::Run;
    world.roundLeft = plan.seconds;

    // Der Stand von jetzt. Die Abrechnung rechnet spaeter die Differenz.
    world.roundMoneyStart = world.money;
    world.roundMinedStart = world.minedCount;

    world.roundMoneyEnd  = world.money;
    world.roundMined     = 0;
    world.roundSoldCount = 0;
    world.roundSoldMoney = 0;
}

void FinishRound(World& world, const RoundPlan& plan, const OrePlan& ores, const CraftPlan& craft)
{
    // Erst aufraeumen: was noch im Ofen liegt, gehoert in die Tasche und damit
    // in den Verkauf. Sonst waere ein Auftrag, der eine Sekunde zu spaet fertig
    // wird, ersatzlos weg.
    world.cancelMining();
    world.cancelCraft();

    if (plan.sellAtEnd)
    {
        world.roundSoldCount = world.inventoryCount();
        world.roundSoldMoney = world.sell(ores, craft);
    }
    else
    {
        world.roundSoldCount = 0;
        world.roundSoldMoney = 0;
    }

    world.roundMoneyEnd = world.money;
    world.roundMined    = world.minedCount - world.roundMinedStart;

    // Erst pruefen, dann kassieren: das Ziel ist die Miete fuer diese Runde.
    // Was darueber liegt, ist der Gewinn - und nur damit kauft man ein.
    const int ziel  = RoundTarget(plan, world.roundNumber);
    world.roundWon  = (ziel <= 0) || (world.money >= ziel);
    world.roundPaid = 0;

    if (world.roundWon && ziel > 0 && plan.deductTarget)
    {
        world.roundPaid = ziel;
        world.money -= ziel;
    }

    world.roundLeft = 0.0f;
    world.phase     = RoundPhase::Report;
}

void NextRound(World& world, const RoundPlan& plan)
{
    // Nur wer das Ziel geschafft hat, kommt eine Runde weiter. Sonst darf man
    // dieselbe noch einmal versuchen (bei "neustart_bei_niederlage" wird das
    // vom Aufrufer gar nicht erst erreicht - der faengt komplett neu an).
    if (RoundTarget(plan, world.roundNumber) <= 0 || RoundWon(world, plan))
        ++world.roundNumber;

    world.phase     = RoundPhase::Prepare;
    world.roundLeft = 0.0f;
}

std::string RoundClock(float seconds)
{
    if (seconds < 0.0f)
        seconds = 0.0f;

    const int ganz = (int)(seconds + 0.999f);  // 0:00 erst, wenn wirklich Schluss ist

    char text[16];
    std::snprintf(text, sizeof(text), "%02d:%02d", ganz / 60, ganz % 60);
    return text;
}

// Die Runde bekommt eine eigene Leiste, unten quer ueber die Seite.
//
// Vorher stand das alles in der Menueleiste - Knopf, Nummer, Phase, Uhr und
// Ziel nebeneinander als Text. Das war zu viel fuer eine Zeile: das Wichtigste
// (wie lange noch, wie weit bin ich) ging zwischen den Reitern unter.
//
// Zwei Leisten, immer dieselben: oben das Geld mit dem Ziel dahinter, unten die
// Zeit. Sie stehen auf jeder Seite und in jeder Phase an derselben Stelle -
// beim Spielen schaut man nicht hin, man sieht nur, wie voll sie sind.
bool DrawRoundHud(const World& world, const RoundPlan& plan)
{
    bool start = false;

    ImGuiViewport* vp = ImGui::GetMainViewport();

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    // Eine Leiste am unteren Rand - dieselbe Sorte Fenster wie die Menueleiste
    // oben. Der Witz daran: sie zieht sich ihren Platz vom freien Bereich ab.
    // Alles andere richtet sich danach und legt sich nicht mehr darueber.
    if (ImGui::BeginViewportSideBar("##runde", vp, ImGuiDir_Down, ui::kFooterHeight, flags))
    {
        const int    ziel = RoundTarget(plan, world.roundNumber);
        ImDrawList*  dl   = ImGui::GetWindowDrawList();
        const ImVec2 wp   = ImGui::GetWindowPos();
        const ImVec2 ws   = ImGui::GetWindowSize();

        dl->AddLine(wp, ImVec2(wp.x + ws.x, wp.y), ui::kBorder, 1.0f);

        // Ganz links die Nummer, dann zwei beschriftete Balken quer ueber die
        // Breite, rechts der Wert. Zwei Zeilen statt einer Kachel: so sieht man
        // beim Spielen im Augenwinkel, wie beides gleichzeitig laeuft.
        // Vier Spalten: Nummer, Beschriftung, Balken, Wert. Die Breiten sind
        // aus der laengsten Aufschrift gerechnet, damit nichts uebereinander
        // rutscht, wenn aus "Time" mal "Vorbereitung" wird.
        // Der Knopf steht nur in der Vorbereitung da. Dann muss ihm Platz
        // gemacht werden - sonst legt er sich ueber das Ende der Balken.
        const bool  vorbereitung = (world.phase == RoundPhase::Prepare);
        const float knopfB       = 150.0f;
        const float knopfH       = 34.0f;
        const float rechtsFrei   = vorbereitung ? (knopfB + 22.0f) : 0.0f;

        const float x0     = wp.x + 26.0f;
        const float xLabel = x0 + ImGui::CalcTextSize("Round 88").x + 26.0f;
        const float xBar   = xLabel + ImGui::CalcTextSize("Time").x + 16.0f;
        const float xWert  = wp.x + ws.x - 26.0f - rechtsFrei;
        const float wertW  = ImGui::CalcTextSize("00000 / 00000").x;

        float breit = xWert - wertW - 16.0f - xBar;
        if (breit < 60.0f)
            breit = 60.0f;

        char nummer[32];
        std::snprintf(nummer, sizeof(nummer), "Round %d", world.roundNumber);
        dl->AddText(ImVec2(x0, wp.y + (ws.y - ImGui::GetTextLineHeight()) * 0.5f), ui::kText,
                    nummer);

        auto zeile = [&](float y, const char* name, float anteil, ImU32 farbe, const char* wert)
        {
            const float th = ImGui::GetTextLineHeight();
            dl->AddText(ImVec2(xLabel, y - th * 0.5f), ui::kTextDim, name);
            ui::Bar(dl, ImVec2(xBar, y - 3.0f), breit, 6.0f, anteil, farbe);

            const float w = ImGui::CalcTextSize(wert).x;
            dl->AddText(ImVec2(xWert - w, y - th * 0.5f), ui::kText, wert);
        };

        const float y1 = wp.y + ws.y * 0.34f;
        const float y2 = wp.y + ws.y * 0.68f;

        // IMMER dieselben zwei Leisten: oben das Geld, unten die Zeit. Sie
        // wandern nicht und sie verschwinden nicht - man soll im Augenwinkel
        // sehen koennen, wie beides steht, ohne erst hinsehen zu muessen, was
        // die Zeile diesmal bedeutet.
        char geld[48];
        if (ziel > 0)
            std::snprintf(geld, sizeof(geld), "%d / %d", world.money, ziel);
        else
            std::snprintf(geld, sizeof(geld), "%d", world.money);

        zeile(y1, "Money", (ziel > 0) ? (float)world.money / (float)ziel : 0.0f, ui::kAccent, geld);

        if (world.phase == RoundPhase::Run)
        {
            const bool  knapp = world.roundLeft <= 60.0f;
            const float t     = (plan.seconds > 0.0f) ? (world.roundLeft / plan.seconds) : 0.0f;
            zeile(y2, "Time", t, knapp ? ui::kBad : ui::kDark, RoundClock(world.roundLeft).c_str());
        }
        else if (world.phase == RoundPhase::Report)
        {
            zeile(y2, "Time", 0.0f, ui::kDark, "over");
        }
        else
        {
            // Noch nicht gestartet: die volle Zeit steht bereit.
            zeile(y2, "Time", 1.0f, ui::kDark, RoundClock(plan.seconds).c_str());

            // Der wichtigste Knopf im Spiel darf auch so aussehen.
            ImGui::SetCursorScreenPos(
                ImVec2(wp.x + ws.x - 26.0f - knopfB, wp.y + (ws.y - knopfH) * 0.5f));

            ImGui::PushStyleColor(ImGuiCol_Button, ui::V(ui::kAccent));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::V(ui::kAccentHot));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::V(ui::kAccent));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            if (ImGui::Button("Start round", ImVec2(knopfB, knopfH)))
                start = true;
            ImGui::PopStyleColor(4);

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The round lasts %s.\n"
                                  "Until you start, the world stands still.",
                                  RoundClock(plan.seconds).c_str());
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    return start;
}

bool DrawRoundReport(const World& world, const RoundPlan& plan)
{
    bool weiter = false;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    // Feste Breite, Hoehe passt sich an (0 heisst bei ImGui "so hoch wie
    // noetig"). Sonst huepft die Breite, je nachdem wie gross die Zahlen sind.
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Always);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::V(ui::kCard));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.26f, 0.34f, 0.28f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 18.0f));

    // Ganz nach vorne: die Abrechnung ist der einzige Weg zur naechsten Runde.
    ImGui::SetNextWindowFocus();

    if (ImGui::Begin("##abrechnung", nullptr, flags))
    {
        const ImVec4 gruen(0.72f, 0.90f, 0.50f, 1.0f);
        const ImVec4 rot(1.00f, 0.45f, 0.38f, 1.0f);
        const float  spalte = 240.0f;

        const int  ziel     = RoundTarget(plan, world.roundNumber);
        const bool geschafft = RoundWon(world, plan);

        if (ziel <= 0)
            ImGui::TextColored(gruen, "Round %d is over", world.roundNumber);
        else if (geschafft)
            ImGui::TextColored(gruen, "Round %d cleared", world.roundNumber);
        else
            ImGui::TextColored(rot, "Round %d not cleared", world.roundNumber);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        auto zeile = [&](const char* name, int wert)
        {
            ImGui::TextDisabled("%s", name);
            ImGui::SameLine(spalte);
            ImGui::Text("%d", wert);
        };

        zeile("Geld am Anfang", world.roundMoneyStart);
        zeile("Geld am Ende", world.roundMoneyEnd);

        const int verdienst = world.roundMoneyEnd - world.roundMoneyStart;
        ImGui::TextDisabled("Verdienst");
        ImGui::SameLine(spalte);
        ImGui::TextColored(verdienst < 0 ? rot : gruen, "%+d", verdienst);

        if (ziel > 0)
        {
            ImGui::TextDisabled("Goal");
            ImGui::SameLine(spalte);
            ImGui::TextColored(geschafft ? gruen : rot, "%d  (%+d)", ziel,
                               world.roundMoneyEnd - ziel);

            if (world.roundPaid > 0)
            {
                ImGui::TextDisabled("Goal paid");
                ImGui::SameLine(spalte);
                ImGui::TextColored(rot, "-%d", world.roundPaid);

                ImGui::TextDisabled("You keep");
                ImGui::SameLine(spalte);
                ImGui::TextColored(gruen, "%d", world.roundMoneyEnd - world.roundPaid);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        zeile("Blocks mined", world.roundMined);

        if (plan.sellAtEnd)
        {
            ImGui::TextDisabled("Bag sold");
            ImGui::SameLine(spalte);
            ImGui::Text("%d pieces for %d money", world.roundSoldCount, world.roundSoldMoney);
        }
        else
        {
            ImGui::TextDisabled("Bag");
            ImGui::SameLine(spalte);
            ImGui::Text("bleibt dir");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Verloren und "alles auf Anfang": dann ist der Knopf kein Weiter,
        // sondern ein Neuanfang - und er sieht auch so aus.
        const bool vorbei = (ziel > 0) && !geschafft && plan.resetOnLoss;

        if (vorbei)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.52f, 0.19f, 0.19f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.66f, 0.24f, 0.24f, 1.0f));
            if (ImGui::Button("Neu anfangen", ImVec2(160.0f, 0.0f)))
                weiter = true;
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            ImGui::TextColored(rot, "Everything starts over.");
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.54f, 0.31f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.33f, 0.68f, 0.42f, 1.0f));
            if (ImGui::Button("Continue", ImVec2(140.0f, 0.0f)))
                weiter = true;
            ImGui::PopStyleColor(2);

            ImGui::SameLine();

            // Nicht geschafft, aber es geht weiter: dann kommt dieselbe Runde
            // noch einmal, nicht die naechste.
            if ((ziel > 0) && !geschafft)
                ImGui::TextDisabled("Round %d once more", world.roundNumber);
            else
                ImGui::TextDisabled("Preparation for round %d", world.roundNumber + 1);
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    return weiter;
}
