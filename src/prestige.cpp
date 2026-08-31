#include "prestige.h"
#include "datapath.h"
#include "proc.h"

#include "json.h"
#include "theme.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace
{

std::vector<std::string> Candidates()
{
    return DataPaths("erbe.json");
}

// Eigene Datei neben der exe - siehe save.cpp fuer dasselbe Muster mit
// spielstand.txt. Bewusst ein anderer Name: DeleteSave() in save.cpp loescht
// nur spielstand.txt, und "Start over" soll genau das tun.
std::string PrestigePath()
{
    const std::string dir = ExeDir();
    return dir.empty() ? std::string("erbe_stand.txt") : (dir + "/erbe_stand.txt");
}

// Wie viel eine Stufe eines Upgrades bewirkt, in "effect je Stufe". Steht an
// einer Stelle, damit ComputePrestigeEffects und die Kartenanzeige (naechste
// Stufe / Gesamtstufe) nie etwas Verschiedenes ausrechnen.
void Apply(const std::string& key, int level, float effect, PrestigeEffects& out)
{
    if (level <= 0)
        return;

    if (key == "geld_prozent")
        out.moneyMul *= 1.0f + (float)level * effect * 0.01f;
    else if (key == "rundenzeit")
        out.extraSeconds += (float)level * effect;
    else if (key == "nachwachsen")
        out.respawnMul *= std::max(0.1f, 1.0f - (float)level * effect * 0.01f);
    else if (key == "tempo")
        out.speedMul *= 1.0f + (float)level * effect * 0.01f;
    else if (key == "startgeld")
        out.startMoneyBonus += (int)((float)level * effect);
    else if (key == "ofen")
        out.extraJobs += (int)((float)level * effect);
    else if (key == "konsole")
        out.extraConsoles += (int)((float)level * effect);
    else if (key == "forschung")
        out.assayCostMul *= std::max(0.1f, 1.0f - (float)level * effect * 0.01f);
    else if (key == "ziel_rabatt")
        out.targetMul *= std::max(0.1f, 1.0f - (float)level * effect * 0.01f);
    // Ein unbekannter Schluessel (Datei geaendert, alter Spielstand) bewirkt
    // absichtlich nichts - besser eine stumme Stufe als ein Absturz.
}

}  // namespace

PrestigePlan LoadPrestigePlan()
{
    PrestigePlan plan;

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
        plan.problems.push_back("data/erbe.json not found.");
        return plan;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    JsonValue   wurzel;
    std::string fehler;
    if (!ParseJson(buffer.str(), wurzel, fehler))
    {
        plan.problems.push_back("data/erbe.json - " + fehler);
        return plan;
    }

    plan.offerCount = (int)wurzel.number("angebote", (double)plan.offerCount);
    if (plan.offerCount < 1)
    {
        plan.problems.push_back("angebote must be at least 1.");
        plan.offerCount = 1;
    }

    plan.pointsBase     = (float)wurzel.number("punkte_basis", (double)plan.pointsBase);
    plan.pointsExponent = (float)wurzel.number("punkte_exponent", (double)plan.pointsExponent);
    plan.moneyDivisor   = (float)wurzel.number("punkte_geld_teiler", (double)plan.moneyDivisor);

    if (plan.pointsBase < 0.0f)
    {
        plan.problems.push_back("punkte_basis must not be negative.");
        plan.pointsBase = 0.0f;
    }
    if (plan.moneyDivisor < 1.0f)
    {
        plan.problems.push_back("punkte_geld_teiler must be at least 1.");
        plan.moneyDivisor = 1.0f;
    }

    const JsonValue* liste = wurzel.find("upgrades");
    if (liste == nullptr || liste->type != JsonValue::Type::Array)
    {
        plan.problems.push_back("Missing the list \"upgrades\": [ ... ].");
        return plan;
    }

    for (const JsonValue& e : liste->items)
    {
        if (e.type != JsonValue::Type::Object)
            continue;

        PrestigeUpgradeDef def;
        def.key  = e.text("schluessel", "");
        def.name = e.text("name", def.key.c_str());
        def.unit = e.text("einheit", "");

        def.baseCost = (int)e.number("basis", (double)def.baseCost);
        def.growth   = (float)e.number("wachstum", (double)def.growth);
        def.effect   = (float)e.number("wirkung", (double)def.effect);
        def.maxLevel = (int)e.number("maximum", (double)def.maxLevel);

        if (def.key.empty())
        {
            plan.problems.push_back("An upgrade has no \"schluessel\".");
            continue;
        }
        if (def.baseCost < 1)
        {
            plan.problems.push_back(def.key + ": \"basis\" must be at least 1.");
            def.baseCost = 1;
        }
        if (def.growth < 1.0f)
        {
            plan.problems.push_back(def.key + ": \"wachstum\" must be at least 1.");
            def.growth = 1.0f;
        }
        if (def.maxLevel < 0)
            def.maxLevel = 0;

        plan.upgrades.push_back(def);
    }

    if (plan.upgrades.empty())
        plan.problems.push_back("In \"upgrades\" there is not a single entry.");

    return plan;
}

const PrestigeUpgradeDef* FindPrestigeUpgrade(const PrestigePlan& plan, const std::string& key)
{
    for (const PrestigeUpgradeDef& def : plan.upgrades)
        if (def.key == key)
            return &def;
    return nullptr;
}

int PrestigeUpgradeCost(const PrestigeUpgradeDef& def, int level)
{
    if (level < 0)
        level = 0;
    const double kosten = (double)def.baseCost * std::pow((double)def.growth, (double)level);
    return (int)std::llround(kosten);
}

int PrestigePointsEarned(const PrestigePlan& plan, int roundNumber, int moneyAtLoss)
{
    if (roundNumber < 1)
        roundNumber = 1;
    if (moneyAtLoss < 0)
        moneyAtLoss = 0;

    const double basis = (double)plan.pointsBase * std::pow((double)roundNumber,
                                                             (double)plan.pointsExponent);
    const double bonus = (double)moneyAtLoss / (double)plan.moneyDivisor;

    return (int)std::llround(basis + bonus);
}

int PrestigeLevel(const Prestige& prestige, const std::string& key)
{
    const auto it = prestige.levels.find(key);
    return (it == prestige.levels.end()) ? 0 : it->second;
}

bool PrestigeMaxed(const Prestige& prestige, const PrestigeUpgradeDef& def)
{
    return def.maxLevel > 0 && PrestigeLevel(prestige, def.key) >= def.maxLevel;
}

void RerollOffers(Prestige& prestige, const PrestigePlan& plan)
{
    prestige.offers.clear();
    if (plan.upgrades.empty())
        return;

    // Erst die, die noch Luft nach oben haben, dann der Rest - so stehen nie
    // weniger als moeglich da, nur weil der Zufall dreimal auf ein
    // ausgereiztes Upgrade fiel.
    std::vector<int> frei;
    std::vector<int> voll;
    for (int i = 0; i < (int)plan.upgrades.size(); ++i)
    {
        if (PrestigeMaxed(prestige, plan.upgrades[(std::size_t)i]))
            voll.push_back(i);
        else
            frei.push_back(i);
    }

    std::shuffle(frei.begin(), frei.end(), prestige.rng);
    std::shuffle(voll.begin(), voll.end(), prestige.rng);

    const int ziel = std::min(plan.offerCount, (int)plan.upgrades.size());
    for (int i : frei)
    {
        if ((int)prestige.offers.size() >= ziel)
            break;
        prestige.offers.push_back(plan.upgrades[(std::size_t)i].key);
    }
    for (int i : voll)
    {
        if ((int)prestige.offers.size() >= ziel)
            break;
        prestige.offers.push_back(plan.upgrades[(std::size_t)i].key);
    }
}

bool BuyPrestigeOffer(Prestige& prestige, const PrestigePlan& plan, const std::string& key)
{
    const PrestigeUpgradeDef* def = FindPrestigeUpgrade(plan, key);
    if (def == nullptr)
        return false;

    if (PrestigeMaxed(prestige, *def))
        return false;

    const int level = PrestigeLevel(prestige, key);
    const int preis = PrestigeUpgradeCost(*def, level);
    if (prestige.points < preis)
        return false;

    prestige.points -= preis;
    prestige.levels[key] = level + 1;
    return true;
}

PrestigeEffects ComputePrestigeEffects(const Prestige& prestige, const PrestigePlan& plan)
{
    PrestigeEffects out;
    for (const auto& e : prestige.levels)
    {
        const PrestigeUpgradeDef* def = FindPrestigeUpgrade(plan, e.first);
        if (def != nullptr)
            Apply(e.first, e.second, def->effect, out);
    }
    return out;
}

void SavePrestige(const Prestige& prestige)
{
    std::ofstream out(PrestigePath().c_str(), std::ios::binary);
    if (!out.is_open())
        return;

    out << "erbe 1\n";
    out << "punkte " << prestige.points << " " << prestige.totalEarned << "\n";

    out << "stufen " << prestige.levels.size() << "\n";
    for (const auto& e : prestige.levels)
        out << e.second << " " << e.first << "\n";  // Zahl zuerst, Name ist der Rest

    out << "angebote " << prestige.offers.size() << "\n";
    for (const std::string& key : prestige.offers)
        out << key << "\n";

    std::ostringstream rng;
    rng << prestige.rng;
    out << "wuerfel " << rng.str() << "\n";
}

bool LoadPrestige(Prestige& prestige)
{
    std::ifstream in(PrestigePath().c_str(), std::ios::binary);
    if (!in.is_open())
        return false;

    std::string kopf;
    int         version = 0;
    if (!(in >> kopf >> version) || kopf != "erbe" || version != 1)
        return false;

    Prestige neu;

    std::string wort;
    while (in >> wort)
    {
        if (wort == "punkte")
        {
            in >> neu.points >> neu.totalEarned;
        }
        else if (wort == "stufen")
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
                if (!name.empty() && wert > 0)
                    neu.levels[name] = wert;
            }
        }
        else if (wort == "angebote")
        {
            std::size_t n = 0;
            in >> n;
            std::string rest;
            std::getline(in, rest);  // Rest der Kopfzeile wegwerfen

            for (std::size_t i = 0; i < n; ++i)
            {
                std::string key;
                if (!std::getline(in, key))
                    break;
                if (!key.empty() && key.back() == '\r')
                    key.pop_back();
                if (!key.empty())
                    neu.offers.push_back(key);
            }
        }
        else if (wort == "wuerfel")
        {
            in >> neu.rng;
        }
        else
        {
            std::string rest;
            std::getline(in, rest);
        }
    }

    prestige = neu;
    return true;
}

namespace
{

// Preis oder "Max" - fuer die Karte.
std::string PreisText(const PrestigeUpgradeDef& def, int level, bool maxed)
{
    if (maxed)
        return "Maximum reached";
    char t[64];
    std::snprintf(t, sizeof(t), "Buy - %s points", ui::Money(PrestigeUpgradeCost(def, level)).c_str());
    return t;
}

// "+7% total" bzw. "+56s total" - was eine Stufe gerade insgesamt bringt.
std::string GesamtText(const PrestigeUpgradeDef& def, int level)
{
    char t[64];
    if (def.unit == "%")
        std::snprintf(t, sizeof(t), "+%.0f%% total", (double)level * def.effect);
    else if (def.unit == "s")
        std::snprintf(t, sizeof(t), "+%.0fs total", (double)level * def.effect);
    else
        std::snprintf(t, sizeof(t), "+%d total", (int)((double)level * def.effect));
    return t;
}

void DrawOfferCard(Prestige& prestige, const PrestigePlan& plan, const std::string& key,
                   float width)
{
    const PrestigeUpgradeDef* def = FindPrestigeUpgrade(plan, key);

    ImGui::BeginGroup();
    ImGui::PushID(key.c_str());

    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const ImVec2 a  = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + width, a.y + 150.0f);
    ui::Card(dl, a, b);

    ImGui::Dummy(ImVec2(width, 150.0f));

    if (def == nullptr)
    {
        // Der Schluessel steht nicht mehr in der Datei - lieber eine leere
        // Karte als ein Absturz. Verschwindet mit der naechsten Niederlage.
        dl->AddText(ImVec2(a.x + 14.0f, a.y + 14.0f), ui::kTextDim, "(unknown upgrade)");
        ImGui::PopID();
        ImGui::EndGroup();
        return;
    }

    const int  level = PrestigeLevel(prestige, key);
    const bool maxed = PrestigeMaxed(prestige, *def);
    const int  preis = PrestigeUpgradeCost(*def, level);
    const bool kaufbar = !maxed && prestige.points >= preis;

    float y = a.y + 14.0f;
    dl->AddText(ImVec2(a.x + 14.0f, y), ui::kText, def->name.c_str());
    y += ImGui::GetTextLineHeight() + 4.0f;

    if (level > 0)
    {
        char lvl[32];
        std::snprintf(lvl, sizeof(lvl), "Level %d - %s", level, GesamtText(*def, level).c_str());
        dl->AddText(ImVec2(a.x + 14.0f, y), ui::kTextDim, lvl);
    }
    else
    {
        dl->AddText(ImVec2(a.x + 14.0f, y), ui::kTextDim, "Not bought yet");
    }
    y += ImGui::GetTextLineHeight() + 4.0f;

    char naechste[64];
    if (def->unit == "%")
        std::snprintf(naechste, sizeof(naechste), "Next level: +%.0f%%", (double)def->effect);
    else if (def->unit == "s")
        std::snprintf(naechste, sizeof(naechste), "Next level: +%.0fs", (double)def->effect);
    else
        std::snprintf(naechste, sizeof(naechste), "Next level: +%d", (int)def->effect);

    if (!maxed)
        dl->AddText(ImVec2(a.x + 14.0f, y), ui::kText, naechste);

    // Der Knopf ganz unten in der Karte.
    ImGui::SetCursorScreenPos(ImVec2(a.x + 14.0f, b.y - 42.0f));

    ImGui::BeginDisabled(maxed || !kaufbar);
    ImGui::PushStyleColor(ImGuiCol_Button, kaufbar ? ui::V(ui::kAccent) : ui::V(ui::kSunken));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          kaufbar ? ui::V(ui::kAccentHot) : ui::V(ui::kSunken));
    ImGui::PushStyleColor(ImGuiCol_Text, kaufbar ? ImVec4(1, 1, 1, 1) : ui::V(ui::kTextDim));
    if (ImGui::Button(PreisText(*def, level, maxed).c_str(), ImVec2(width - 28.0f, 30.0f)))
        BuyPrestigeOffer(prestige, plan, key);
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    ImGui::PopID();
    ImGui::EndGroup();
}

}  // namespace

void DrawPrestigePage(Prestige& prestige, const PrestigePlan& plan)
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));

    if (ImGui::Begin("##erbe", nullptr, flags))
    {
        ImGui::TextColored(ui::V(ui::kTextDim), "LEGACY POINTS");
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextColored(ui::V(ui::kAccent), "%s", ui::Money(prestige.points).c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14.0f);
        ImGui::TextColored(ui::V(ui::kTextDim), "   earned so far: %s",
                           ui::Money(prestige.totalEarned).c_str());

        ImGui::Spacing();
        ImGui::TextColored(ui::V(ui::kTextDim),
                           "Survives \"Start over\". Losing a round earns more of these the "
                           "further you got - spend them here on permanent upgrades.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Spacing();

        // Drei (oder plan.offerCount) Karten nebeneinander. Sie stehen fest,
        // bis die naechste Niederlage sie neu wuerfelt.
        const float gap    = 20.0f;
        const int   anzahl = (int)prestige.offers.size();
        const float breit  = anzahl > 0
                                ? (ImGui::GetContentRegionAvail().x - gap * (float)(anzahl - 1)) /
                                     (float)anzahl
                                : 0.0f;

        for (int i = 0; i < anzahl; ++i)
        {
            if (i > 0)
                ImGui::SameLine(0.0f, gap);
            DrawOfferCard(prestige, plan, prestige.offers[(std::size_t)i], breit);
        }

        if (anzahl == 0)
            ImGui::TextDisabled("No offers yet - lose a round to get some.");

        // Rueckblick: alles, was man sich schon dauerhaft erspielt hat -
        // nicht nur, was gerade im Angebot steht.
        bool irgendwas = false;
        for (const auto& e : prestige.levels)
            if (e.second > 0)
                irgendwas = true;

        if (irgendwas)
        {
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(ui::V(ui::kTextDim), "PERMANENT SO FAR");
            ImGui::Spacing();

            for (const auto& e : prestige.levels)
            {
                if (e.second <= 0)
                    continue;
                const PrestigeUpgradeDef* def = FindPrestigeUpgrade(plan, e.first);
                if (def == nullptr)
                    continue;

                ImGui::TextColored(ui::V(ui::kText), "%s", def->name.c_str());
                ImGui::SameLine(240.0f);
                ImGui::TextColored(ui::V(ui::kTextDim), "Level %d - %s", e.second,
                                   GesamtText(*def, e.second).c_str());
            }
        }

        if (!plan.problems.empty())
        {
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.55f, 0.42f, 1.0f));
            ImGui::TextUnformatted("data/erbe.json:");
            for (const std::string& p : plan.problems)
                ImGui::TextUnformatted(p.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}
