#include "world.h"

#include "ore.h"

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

const Ore& OreOf(const OrePlan& ores, int index)
{
    static const Ore ersatz;  // falls die Datei fehlt: schlichter grauer Stein
    if (index < 0 || index >= (int)ores.ores.size())
        return ersatz;
    return ores.ores[(std::size_t)index];
}

ImU32 Mix(const Color& a, const Color& b, float t, int alpha)
{
    const int r = (int)((float)a.r + ((float)b.r - (float)a.r) * t);
    const int g = (int)((float)a.g + ((float)b.g - (float)a.g) * t);
    const int bl = (int)((float)a.b + ((float)b.b - (float)a.b) * t);
    return IM_COL32(r, g, bl, alpha);
}

}  // namespace

bool World::mine()
{
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

int World::inventoryCount() const
{
    int summe = 0;
    for (const auto& e : inventory)
        summe += e.second;
    return summe;
}

int World::sell(const OrePlan& ores, int oreIndex)
{
    const auto it = inventory.find(oreIndex);
    if (it == inventory.end() || it->second <= 0)
        return 0;

    const int geld = it->second * OreOf(ores, oreIndex).value * moneyPerBlock;
    money += geld;
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

    const auto it = inventory.find(nummer);
    return (it != inventory.end()) ? it->second : 0;
}

int World::sell(const OrePlan& ores, const std::string& name, int anzahl)
{
    const int nummer = FindOre(ores, name);
    if (nummer < 0)
        return 0;  // Erz gibt es nicht - dann eben kein Geld

    const auto it = inventory.find(nummer);
    if (it == inventory.end() || it->second <= 0)
        return 0;

    // Weniger als gewuenscht ist in Ordnung: es wird verkauft, was da ist.
    int wie = (anzahl < 0) ? it->second : anzahl;
    if (wie > it->second)
        wie = it->second;
    if (wie <= 0)
        return 0;

    const int geld = wie * OreOf(ores, nummer).value * moneyPerBlock;
    money += geld;

    it->second -= wie;
    if (it->second <= 0)
        inventory.erase(it);

    lastSold = geld;
    sellFx   = 1.0f;
    return geld;
}

int World::sell(const OrePlan& ores)
{
    int geld = 0;
    for (const auto& e : inventory)
        geld += e.second * OreOf(ores, e.first).value * moneyPerBlock;

    inventory.clear();
    money += geld;

    if (geld > 0)
    {
        lastSold = geld;
        sellFx   = 1.0f;
    }
    return geld;
}

bool World::place()
{
    if (blockAlive)
        return false;

    // Von Hand hinsetzen geht weiterhin - dann eben sofort.
    blockAlive   = true;
    respawnTimer = 0.0f;
    mining       = false;
    mineTimer    = 0.0f;
    return true;
}

void World::tickMining(float dt, const OrePlan& ores)
{
    if (!blockAlive || !mining)
        return;

    mineTimer += dt;

    const Ore& erz = OreOf(ores, ore);
    if (mineTimer < erz.mineSeconds)
        return;

    // Der Block wandert in die Tasche - Geld gibt es erst beim Verkaufen.
    ++inventory[ore];
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

void World::update(float dt, const OrePlan& ores)
{
    // ---- Nachwachsen ----------------------------------------------------
    if (!blockAlive && respawnTimer > 0.0f)
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

void DrawWorld(World& world, const OrePlan& ores)
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

        ImGui::BeginTooltip();
        ImGui::TextUnformatted(erz.name.c_str());
        ImGui::TextDisabled("%d Geld beim Verkaufen  -  %.1f s Abbau",
                            erz.value * world.moneyPerBlock, erz.mineSeconds);
        if (world.blockAlive)
            ImGui::TextDisabled("Klick zum Abbauen");
        ImGui::EndTooltip();
    }

    // Stimmt an data/erze.json etwas nicht, muss man das sehen - sonst sucht
    // man den Fehler im Spiel statt in der Datei.
    if (!ores.problems.empty())
    {
        float y = vp->WorkPos.y + 12.0f;
        dl->AddText(ImVec2(vp->WorkPos.x + 16.0f, y), IM_COL32(250, 140, 108, 255),
                    "data/erze.json:");
        for (const std::string& p : ores.problems)
        {
            y += ImGui::GetTextLineHeight() + 2.0f;
            dl->AddText(ImVec2(vp->WorkPos.x + 16.0f, y), IM_COL32(250, 140, 108, 255), p.c_str());
        }
    }
}

void DrawInventory(World& world, const OrePlan& ores)
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
        // ---- Kopfzeile ----------------------------------------------------
        int wert = 0;
        for (const auto& e : world.inventory)
            wert += e.second * OreOf(ores, e.first).value * world.moneyPerBlock;

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.97f, 0.99f, 1.0f));
        ImGui::TextUnformatted("Tasche");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextDisabled("  %d Blöcke, zusammen %d Geld", world.inventoryCount(), wert);

        if (!world.inventory.empty())
        {
            ImGui::SameLine(ImGui::GetWindowWidth() - 190.0f);
            if (ImGui::Button("Alles verkaufen", ImVec2(160.0f, 0.0f)))
                world.sell(ores);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (world.inventory.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Noch nichts abgebaut.");
            ImGui::TextDisabled("Geh auf die Welt-Seite und klick auf den Block -");
            ImGui::TextDisabled("oder lass ein Programm block.mine() machen.");
        }
        else
        {
            ImGui::TextDisabled("Rechtsklick auf eine Karte zum Verkaufen.");
            ImGui::Spacing();
            ImGui::Spacing();

            // ---- Karten, nebeneinander mit Umbruch -------------------------
            const float kartenBreite = 170.0f;
            const float kartenHoehe  = 200.0f;
            const float bild         = 104.0f;
            const float luft         = 16.0f;

            const float platz = ImGui::GetContentRegionAvail().x;
            int         proZeile = (int)((platz + luft) / (kartenBreite + luft));
            if (proZeile < 1)
                proZeile = 1;

            int verkaufenAlles = -1;  // erst nach der Schleife, sonst wackelt sie
            int verkaufenEins  = -1;
            int spalte         = 0;

            for (const auto& e : world.inventory)
            {
                const Ore& erz = OreOf(ores, e.first);

                if (spalte > 0)
                    ImGui::SameLine(0.0f, luft);
                ++spalte;
                if (spalte > proZeile)
                {
                    ImGui::NewLine();
                    spalte = 1;
                }

                ImGui::PushID(e.first);

                const ImVec2 p  = ImGui::GetCursorScreenPos();
                ImDrawList*  dl = ImGui::GetWindowDrawList();

                ImGui::InvisibleButton("karte", ImVec2(kartenBreite, kartenHoehe));
                const bool drauf = ImGui::IsItemHovered();

                // Karte
                const ImVec2 a = p;
                const ImVec2 b(p.x + kartenBreite, p.y + kartenHoehe);
                dl->AddRectFilled(a, b, IM_COL32(36, 40, 48, 255), 12.0f);
                dl->AddRect(a, b, drauf ? IM_COL32(150, 214, 92, 255) : IM_COL32(62, 68, 80, 255),
                            12.0f, 0, drauf ? 2.4f : 1.6f);

                // Bild vom Erz - dieselbe Musterlogik wie beim Block draussen
                const ImVec2 ba(p.x + (kartenBreite - bild) * 0.5f, p.y + 16.0f);
                const int    zellen = 18;
                const float  st     = bild / (float)zellen;
                for (int gy = 0; gy < zellen; ++gy)
                    for (int gx = 0; gx < zellen; ++gx)
                    {
                        const float t = OrePixel((float)gx / (float)zellen,
                                                 (float)gy / (float)zellen, erz.pattern,
                                                 777u + (unsigned)e.first * 31u);
                        const ImVec2 q(ba.x + (float)gx * st, ba.y + (float)gy * st);
                        dl->AddRectFilled(q, ImVec2(q.x + st + 0.6f, q.y + st + 0.6f),
                                          Mix(erz.color2, erz.color1, t, 255));
                    }
                dl->AddRect(ba, ImVec2(ba.x + bild, ba.y + bild), IM_COL32(16, 18, 22, 255), 5.0f,
                            0, 2.0f);

                // Name, Anzahl, Wert
                const ImVec2 ns = ImGui::CalcTextSize(erz.name.c_str());
                dl->AddText(ImVec2(p.x + (kartenBreite - ns.x) * 0.5f, ba.y + bild + 12.0f),
                            IM_COL32(240, 244, 250, 255), erz.name.c_str());

                char anzahl[48];
                std::snprintf(anzahl, sizeof(anzahl), "x %d", e.second);
                const ImVec2 as = ImGui::CalcTextSize(anzahl);
                dl->AddText(ImVec2(p.x + (kartenBreite - as.x) * 0.5f, ba.y + bild + 34.0f),
                            IM_COL32(178, 226, 122, 255), anzahl);

                char geld[48];
                std::snprintf(geld, sizeof(geld), "%d Geld",
                              e.second * erz.value * world.moneyPerBlock);
                const ImVec2 gs = ImGui::CalcTextSize(geld);
                dl->AddText(ImVec2(p.x + (kartenBreite - gs.x) * 0.5f, ba.y + bild + 54.0f),
                            IM_COL32(255, 214, 120, 255), geld);

                if (ImGui::BeginPopupContextItem("menue"))
                {
                    ImGui::TextDisabled("%s x%d", erz.name.c_str(), e.second);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Alle verkaufen"))
                        verkaufenAlles = e.first;
                    if (e.second > 1 && ImGui::MenuItem("Einen verkaufen"))
                        verkaufenEins = e.first;
                    ImGui::EndPopup();
                }

                if (drauf)
                    ImGui::SetTooltip("%s - %d Geld pro Stück", erz.name.c_str(),
                                      erz.value * world.moneyPerBlock);

                ImGui::PopID();
            }

            if (verkaufenAlles >= 0)
                world.sell(ores, verkaufenAlles);
            if (verkaufenEins >= 0)
                world.sell(ores, OreOf(ores, verkaufenEins).name, 1);
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}
