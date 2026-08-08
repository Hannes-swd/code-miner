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

    if (const JsonValue* ziel = wurzel.find("ziel"))
    {
        plan.targetStart  = (int)ziel->number("start", (double)plan.targetStart);
        plan.targetGrowth = (float)ziel->number("wachstum", (double)plan.targetGrowth);

        if (plan.targetStart < 0)
        {
            plan.problems.push_back("ziel.start ist negativ.");
            plan.targetStart = 0;
        }
        if (plan.targetGrowth < 1.0f)
        {
            // Unter 1 wuerde das Ziel kleiner werden - dann waere jede Runde
            // leichter als die davor.
            plan.problems.push_back("ziel.wachstum muss mindestens 1 sein.");
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

// Die Runde bekommt eine eigene Anzeige, oben in der Mitte.
//
// Vorher stand das alles in der Menueleiste - Knopf, Nummer, Phase, Uhr und
// Ziel nebeneinander als Text. Das war zu viel fuer eine Zeile: das Wichtigste
// (wie lange noch, wie weit bin ich) ging zwischen den Reitern unter. Hier hat
// es Platz, und die beiden Balken sagen mehr als jede Zahl.
bool DrawRoundHud(const World& world, const RoundPlan& plan)
{
    bool start = false;

    ImGuiViewport* vp    = ImGui::GetMainViewport();
    const float    breit = 380.0f;

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 12.0f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(breit, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.082f, 0.100f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.23f, 0.28f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));

    if (ImGui::Begin("##runde", nullptr, flags))
    {
        const int   ziel   = RoundTarget(plan, world.roundNumber);
        const float innen  = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl     = ImGui::GetWindowDrawList();

        // Ein Balken, der ohne Zahl schon alles sagt.
        auto balken = [&](float anteil, ImU32 farbe, const char* text)
        {
            if (anteil < 0.0f)
                anteil = 0.0f;
            if (anteil > 1.0f)
                anteil = 1.0f;

            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float  h = 8.0f;

            dl->AddRectFilled(p, ImVec2(p.x + innen, p.y + h), IM_COL32(38, 42, 50, 255), 4.0f);
            if (anteil > 0.0f)
                dl->AddRectFilled(p, ImVec2(p.x + innen * anteil, p.y + h), farbe, 4.0f);

            ImGui::Dummy(ImVec2(innen, h));

            if (text != nullptr)
                ImGui::TextDisabled("%s", text);
        };

        if (world.phase == RoundPhase::Run)
        {
            const bool  knapp = world.roundLeft <= 60.0f;
            const ImU32 zeitF = knapp ? IM_COL32(255, 115, 97, 255) : IM_COL32(184, 230, 128, 255);

            // Kopfzeile: Nummer links, Uhr gross rechts.
            ImGui::TextDisabled("Runde %d", world.roundNumber);

            const std::string uhr = RoundClock(world.roundLeft);
            ImFont*           gf  = (ImGui::GetIO().Fonts->Fonts.Size > 1)
                                        ? ImGui::GetIO().Fonts->Fonts[1]
                                        : ImGui::GetFont();
            const float  us = 30.0f;
            const ImVec2 um = gf->CalcTextSizeA(us, FLT_MAX, 0.0f, uhr.c_str());
            const ImVec2 up = ImGui::GetCursorScreenPos();

            dl->AddText(gf, us, ImVec2(up.x + innen - um.x, up.y - ImGui::GetTextLineHeight() - 6.0f),
                        zeitF, uhr.c_str());

            const float anteil =
                (plan.seconds > 0.0f) ? (world.roundLeft / plan.seconds) : 0.0f;
            balken(anteil, zeitF, nullptr);

            if (ziel > 0)
            {
                ImGui::Spacing();

                const bool  reicht = world.money >= ziel;
                const ImU32 zf     = reicht ? IM_COL32(150, 214, 92, 255)
                                            : IM_COL32(226, 158, 70, 255);

                char text[96];
                std::snprintf(text, sizeof(text), "Ziel %d  -  du hast %d", ziel, world.money);
                balken((float)world.money / (float)ziel, zf, text);
            }
        }
        else if (world.phase == RoundPhase::Report)
        {
            ImGui::TextDisabled("Runde %d", world.roundNumber);
            ImGui::TextUnformatted("Abrechnung");
        }
        else
        {
            ImGui::TextDisabled("Runde %d  -  Vorbereitung", world.roundNumber);
            ImGui::Spacing();

            // Der wichtigste Knopf im Spiel darf auch so aussehen.
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.54f, 0.31f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.33f, 0.68f, 0.42f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.19f, 0.44f, 0.26f, 1.0f));
            if (ImGui::Button("Runde starten", ImVec2(innen, ImGui::GetFrameHeight() * 1.5f)))
                start = true;
            ImGui::PopStyleColor(3);

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Die Runde dauert %s.\n"
                                  "Solange du nicht startest, steht die Welt still.",
                                  RoundClock(plan.seconds).c_str());

            if (ziel > 0)
            {
                ImGui::Spacing();

                char text[96];
                std::snprintf(text, sizeof(text), "Ziel %d  -  du hast %d", ziel, world.money);
                balken((float)world.money / (float)ziel,
                       (world.money >= ziel) ? IM_COL32(150, 214, 92, 255)
                                             : IM_COL32(226, 158, 70, 255),
                       text);
            }
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
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

        const int  ziel     = RoundTarget(plan, world.roundNumber);
        const bool geschafft = RoundWon(world, plan);

        if (ziel <= 0)
            ImGui::TextColored(gruen, "Runde %d ist vorbei", world.roundNumber);
        else if (geschafft)
            ImGui::TextColored(gruen, "Runde %d geschafft", world.roundNumber);
        else
            ImGui::TextColored(rot, "Runde %d nicht geschafft", world.roundNumber);

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
            ImGui::TextDisabled("Ziel");
            ImGui::SameLine(spalte);
            ImGui::TextColored(geschafft ? gruen : rot, "%d  (%+d)", ziel,
                               world.roundMoneyEnd - ziel);

            if (world.roundPaid > 0)
            {
                ImGui::TextDisabled("Ziel bezahlt");
                ImGui::SameLine(spalte);
                ImGui::TextColored(rot, "-%d", world.roundPaid);

                ImGui::TextDisabled("Bleibt dir");
                ImGui::SameLine(spalte);
                ImGui::TextColored(gruen, "%d", world.roundMoneyEnd - world.roundPaid);
            }
        }

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
            ImGui::TextColored(rot, "Alles beginnt von vorn.");
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.54f, 0.31f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.33f, 0.68f, 0.42f, 1.0f));
            if (ImGui::Button("Weiter", ImVec2(140.0f, 0.0f)))
                weiter = true;
            ImGui::PopStyleColor(2);

            ImGui::SameLine();

            // Nicht geschafft, aber es geht weiter: dann kommt dieselbe Runde
            // noch einmal, nicht die naechste.
            if ((ziel > 0) && !geschafft)
                ImGui::TextDisabled("Runde %d noch einmal", world.roundNumber);
            else
                ImGui::TextDisabled("Vorbereitung für Runde %d", world.roundNumber + 1);
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    return weiter;
}
