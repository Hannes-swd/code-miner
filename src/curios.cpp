#include "curios.h"
#include "proc.h"
#include "theme.h"

#include "imgui.h"

#include <algorithm>
#include <fstream>

namespace
{

// Eigene Datei neben der exe - dasselbe Muster wie erbe_stand.txt
// (siehe prestige.cpp): ein eigener Name, damit DeleteSave() in save.cpp sie
// nicht mit anfasst.
std::string CuriosPath()
{
    const std::string dir = ExeDir();
    return dir.empty() ? std::string("kuriositaeten_stand.txt")
                       : (dir + "/kuriositaeten_stand.txt");
}

// Eine Kuriositaet als Kaestchen. Bewusst KEIN Muster wie bei einem Erz -
// siehe curios.h fuer den Grund. "index" ist trotzdem schon Parameter: so
// bleibt der Aufrufer unveraendert, wenn hier spaeter das passende Bild
// geladen wird.
void DrawCuriosityTile(ImDrawList* dl, ImVec2 pos, float size, int /*index*/)
{
    dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(0, 0, 0, 255), 4.0f);
}

}  // namespace

void SaveCurios(const Curios& curios)
{
    std::ofstream out(CuriosPath().c_str(), std::ios::binary);
    if (!out.is_open())
        return;

    out << "kuriositaeten 1\n";
    out << "gefunden " << curios.found.size() << "\n";
    for (const int k : curios.found)
        out << k << "\n";
}

bool LoadCurios(Curios& curios)
{
    std::ifstream in(CuriosPath().c_str(), std::ios::binary);
    if (!in.is_open())
        return false;

    std::string kopf;
    int         version = 0;
    if (!(in >> kopf >> version) || kopf != "kuriositaeten" || version != 1)
        return false;

    Curios neu;

    std::string wort;
    while (in >> wort)
    {
        if (wort == "gefunden")
        {
            std::size_t n = 0;
            in >> n;
            for (std::size_t i = 0; i < n; ++i)
            {
                int k = -1;
                in >> k;
                if (k >= 0 && k < Curios::kCount)
                    neu.found.insert(k);
            }
        }
        else
        {
            std::string rest;  // unbekannte Zeile ueberspringen
            std::getline(in, rest);
        }
    }

    curios = neu;
    return true;
}

void DrawCuriosityPage(const Curios& curios)
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

    if (ImGui::Begin("##kuriositaeten", nullptr, flags))
    {
        ImGui::TextColored(ui::V(ui::kTextDim), "FOUND WHILE MINING");
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextColored(ui::V(ui::kAccent), "%d", (int)curios.found.size());
        ImGui::SetWindowFontScale(1.0f);

        ImGui::Spacing();
        ImGui::TextColored(
            ui::V(ui::kTextDim),
            "Extremely rare, and nobody knows what they are for. Pure luck - nothing makes it "
            "more likely, no matter how long you play.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Spacing();

        // Ein einfaches Raster, so breit wie reinpasst - keine gesperrten
        // Plaetze dazwischen, nur was wirklich schon da ist.
        constexpr float kSize = 64.0f;
        constexpr float kGap  = 16.0f;

        const float verfuegbar = ImGui::GetContentRegionAvail().x;
        const int   proReihe   = std::max(1, (int)((verfuegbar + kGap) / (kSize + kGap)));

        ImDrawList*  dl    = ImGui::GetWindowDrawList();
        const ImVec2 start = ImGui::GetCursorScreenPos();

        int i = 0;
        for (int index : curios.found)
        {
            const int    spalte = i % proReihe;
            const int    reihe  = i / proReihe;
            const ImVec2 pos(start.x + (float)spalte * (kSize + kGap),
                             start.y + (float)reihe * (kSize + kGap));

            ui::Card(dl, ImVec2(pos.x - 6.0f, pos.y - 6.0f),
                    ImVec2(pos.x + kSize + 6.0f, pos.y + kSize + 6.0f));
            DrawCuriosityTile(dl, pos, kSize, index);

            ++i;
        }

        const int reihen = 1 + ((int)curios.found.size() - 1) / proReihe;
        ImGui::Dummy(ImVec2(verfuegbar, (float)reihen * (kSize + kGap)));
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}
