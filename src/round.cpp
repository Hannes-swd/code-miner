#include "round.h"

#include "json.h"
#include "world.h"

#include "imgui.h"

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
        problems.push_back(std::string(key) + " muss true oder false sein.");
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
        plan.problems.push_back("data/runden.json nicht gefunden.");
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
        plan.problems.push_back("dauer ist kleiner als 1 Sekunde.");
        plan.seconds = 1.0f;
    }

    plan.sellAtEnd     = Flag(wurzel, "verkauf_am_ende", plan.sellAtEnd, plan.problems);
    plan.freezeWorld   = Flag(wurzel, "welt_pausiert_in_vorbereitung", plan.freezeWorld,
                              plan.problems);
    plan.handInPrepare = Flag(wurzel, "handabbau_in_vorbereitung", plan.handInPrepare,
                              plan.problems);

    if (plan.freezeWorld && !plan.handInPrepare)
        plan.problems.push_back("Achtung: ohne Handabbau in der Vorbereitung kommt ein neuer "
                                "Spielstand nie in Gang.");

    return plan;
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
    world.roundLeft     = 0.0f;
    world.phase         = RoundPhase::Report;
}

void NextRound(World& world)
{
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

bool DrawRoundBar(const World& world, const RoundPlan& plan)
{
    bool start = false;

    ImGui::Spacing();
    ImGui::TextDisabled("|");
    ImGui::Spacing();

    if (world.phase == RoundPhase::Run)
    {
        ImGui::TextDisabled("Runde %d läuft", world.roundNumber);
        ImGui::SameLine();

        // Die letzte Minute in Rot: dann lohnt es sich nicht mehr, das
        // Programm noch einmal umzubauen - es faengt ja von vorne an.
        const bool knapp = world.roundLeft <= 60.0f;
        ImGui::TextColored(knapp ? kZeitKnapp : kZeitRuhig, "%s",
                           RoundClock(world.roundLeft).c_str());

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ist die Zeit um, wird das Programm gestoppt, die Tasche "
                              "verkauft und abgerechnet.");
    }
    else if (world.phase == RoundPhase::Report)
    {
        ImGui::TextDisabled("Runde %d  -  Abrechnung", world.roundNumber);
    }
    else
    {
        // Der wichtigste Knopf im Spiel: kraeftiger als die Reiter daneben.
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.54f, 0.31f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.33f, 0.68f, 0.42f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.19f, 0.44f, 0.26f, 1.0f));
        if (ImGui::Button("Runde starten"))
            start = true;
        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Runde %d dauert %s.\nSolange du nicht startest, steht die "
                              "Welt still.",
                              world.roundNumber, RoundClock(plan.seconds).c_str());

        ImGui::SameLine();
        ImGui::TextDisabled("Runde %d  -  Vorbereitung", world.roundNumber);
    }

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

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.082f, 0.100f, 1.0f));
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

        ImGui::TextColored(gruen, "Runde %d ist vorbei", world.roundNumber);
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

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        zeile("Abgebaute Blöcke", world.roundMined);

        if (plan.sellAtEnd)
        {
            ImGui::TextDisabled("Tasche verkauft");
            ImGui::SameLine(spalte);
            ImGui::Text("%d Stück für %d Geld", world.roundSoldCount, world.roundSoldMoney);
        }
        else
        {
            ImGui::TextDisabled("Tasche");
            ImGui::SameLine(spalte);
            ImGui::Text("bleibt dir");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.54f, 0.31f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.33f, 0.68f, 0.42f, 1.0f));
        if (ImGui::Button("Weiter", ImVec2(140.0f, 0.0f)))
            weiter = true;
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        ImGui::TextDisabled("Vorbereitung für Runde %d", world.roundNumber + 1);
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    return weiter;
}
