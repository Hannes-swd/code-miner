#include "market.h"

#include "craft.h"
#include "ore.h"
#include "skilltree.h"
#include "theme.h"
#include "world.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

// Wie weit die Kurve zurueckschaut, in Sekunden Spielzeit. Zwei Minuten sind
// ungefaehr eine Runde: man sieht also den Verlauf, an dem man gerade selbst
// mitgespielt hat, und nicht die halbe Erdgeschichte.
constexpr float kWindowSeconds = 120.0f;

// So viele Punkte hat eine Kurve. Mehr braucht es nicht - die Welle ist glatt,
// und je Punkt kostet es eine Wurzel Rechnung.
constexpr int kSamples = 128;

// Was auf der Seite gerade offen ist. Gehoert nicht in den Spielstand: welches
// Erz man zuletzt angeschaut hat, ist keine Errungenschaft.
struct MarketView
{
    int ore = -1;
};

MarketView g_markt;

// Welche Erze ueberhaupt auf die Seite kommen: die, die man schon einmal in der
// Tasche hatte (siehe world.h), sortiert nach Marktwert.
std::vector<int> KnownOresByValue(const World& world, const OrePlan& ores, const CraftPlan& craft)
{
    std::vector<int> out = KnownOres(world, ores);

    // Das Teuerste oben: da lohnt sich das Warten am ehesten, und dort schaut
    // man beim Aufschlagen der Seite zuerst hin.
    std::sort(out.begin(), out.end(),
              [&](int a, int b)
              {
                  const int va = MarketBase(ores, craft, world, a);
                  const int vb = MarketBase(ores, craft, world, b);
                  if (va != vb)
                      return va > vb;
                  return a < b;
              });
    return out;
}

// Der Ausschlag in Prozent, gerundet: +12 heisst zwoelf Prozent ueber dem
// Grundwert.
int Swing(int jetzt, int grund)
{
    if (grund <= 0)
        return 0;
    return (int)(((double)jetzt / (double)grund - 1.0) * 100.0 + ((jetzt >= grund) ? 0.5 : -0.5));
}

ImU32 SwingColor(int prozent)
{
    if (prozent > 0)
        return ui::kGood;
    if (prozent < 0)
        return ui::kBad;
    return ui::kTextDim;
}

// ---- Die Kurve ------------------------------------------------------------
//
// Aufgezeichnet wird nichts: der Preis ist eine Rechnung aus Marktzeit und
// Erznummer, also laesst sich jeder Punkt der Vergangenheit ausrechnen. Vor dem
// Freischalten ist die Marktuhr nie gelaufen - dort kommt ueberall der
// Grundwert heraus, und die Kurve beginnt als gerade Linie.

// Wie viel Marktzeit ein Fenster breit ist.
float Span(const World& world)
{
    return kWindowSeconds * world.marketSpeed;
}

// Der Ausschnitt, den die Achse zeigt. Er haengt am Grundwert und am erlaubten
// Ausschlag und NICHT an den Werten im Fenster: sonst zappelt die Achse bei
// jedem Bild, und eine ruhige Welle saehe genauso wild aus wie ein Absturz.
//
// Steht hier und nicht in DrawCurve, weil der Zeiger dieselbe Rechnung
// braucht - sonst laege der Punkt unter der Maus neben der Kurve.
void CurveRange(const World& world, int grund, float& unten, float& oben)
{
    const float schwung = (world.marketSwing > 0.0f) ? world.marketSwing : 0.1f;
    unten               = (float)grund * (1.0f - schwung) - 0.5f;
    oben                = (float)grund * (1.0f + schwung) + 0.5f;

    if (oben - unten < 1.0f)
    {
        unten -= 0.5f;
        oben += 0.5f;
    }
}

// Wo ein Wert in einem Kasten sitzt.
float CurveY(float wert, float unten, float oben, ImVec2 a, ImVec2 b)
{
    float t = (wert - unten) / (oben - unten);
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    return b.y - t * (b.y - a.y);
}

void SamplePrices(const World& world, const OrePlan& ores, const CraftPlan& craft, int ore,
                  int punkte, std::vector<float>& out)
{
    out.clear();
    out.reserve((std::size_t)punkte);

    const float spanne = Span(world);  // in Marktzeit

    for (int i = 0; i < punkte; ++i)
    {
        const float anteil = (punkte > 1) ? (float)i / (float)(punkte - 1) : 1.0f;
        const float zeit   = world.marketTime - spanne * (1.0f - anteil);
        out.push_back((float)MarketPriceAt(ores, craft, world, ore, zeit));
    }
}

// Kurve in einen Kasten zeichnen. "gross" schaltet Beschriftung, Raster und den
// Punkt am rechten Rand dazu - klein ist dieselbe Kurve als Vorschau in einer
// Zeile.
void DrawCurve(ImDrawList* dl, ImVec2 a, ImVec2 b, const World& world, const OrePlan& ores,
               const CraftPlan& craft, int ore, bool gross)
{
    const int grund = MarketBase(ores, craft, world, ore);

    float unten = 0.0f;
    float oben  = 0.0f;
    CurveRange(world, grund, unten, oben);

    std::vector<float> werte;
    SamplePrices(world, ores, craft, ore, gross ? kSamples : 48, werte);

    const float breite = b.x - a.x;
    const float hoehe  = b.y - a.y;

    auto ypos = [&](float wert) { return CurveY(wert, unten, oben, a, b); };

    // ---- Raster und Grundwert --------------------------------------------
    const float yGrund = ypos((float)grund);

    if (gross)
    {
        for (int i = 1; i < 4; ++i)
        {
            const float y = a.y + hoehe * (float)i / 4.0f;
            dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), ui::kGrid, 1.0f);
        }
    }

    // Der Grundwert als gestrichelte Linie: alles darueber ist ein guter
    // Moment, alles darunter ein schlechter. Das ist die einzige Zahl, gegen
    // die man den Preis ueberhaupt vergleichen kann (market.average()).
    for (float x = a.x; x < b.x; x += 8.0f)
        dl->AddLine(ImVec2(x, yGrund), ImVec2(std::min(x + 4.0f, b.x), yGrund), ui::kBorderS,
                    1.0f);

    // ---- die Kurve selbst -------------------------------------------------
    const std::size_t n = werte.size();
    if (n < 2)
        return;

    std::vector<ImVec2> punkte;
    punkte.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const float x = a.x + breite * (float)i / (float)(n - 1);
        punkte.push_back(ImVec2(x, ypos(werte[i])));
    }

    // Die Flaeche darunter, ganz blass. Je Abschnitt ein Viereck: das ist
    // immer konvex, eine Kurve als Ganzes waere es nicht.
    const ImU32 fuellung = IM_COL32(0xCC, 0x5B, 0x1E, gross ? 28 : 20);
    for (std::size_t i = 0; i + 1 < n; ++i)
        dl->AddQuadFilled(punkte[i], punkte[i + 1], ImVec2(punkte[i + 1].x, b.y),
                          ImVec2(punkte[i].x, b.y), fuellung);

    dl->AddPolyline(punkte.data(), (int)n, ui::kAccent, 0, gross ? 2.0f : 1.5f);

    if (!gross)
        return;

    // ---- der Punkt von jetzt ----------------------------------------------
    const ImVec2 letzt = punkte[n - 1];
    dl->AddCircleFilled(letzt, 4.5f, ui::kAccent);
    dl->AddCircle(letzt, 7.0f, IM_COL32(0xCC, 0x5B, 0x1E, 90), 0, 2.0f);

    // ---- Beschriftung -----------------------------------------------------
    char text[48];

    std::snprintf(text, sizeof(text), "%s", ui::Money((long long)oben).c_str());
    dl->AddText(ImVec2(a.x + 6.0f, a.y + 4.0f), ui::kTextWk, text);

    std::snprintf(text, sizeof(text), "%s", ui::Money((long long)unten).c_str());
    dl->AddText(ImVec2(a.x + 6.0f, b.y - ImGui::GetTextLineHeight() - 4.0f), ui::kTextWk, text);

    std::snprintf(text, sizeof(text), "base %s", ui::Money(grund).c_str());
    dl->AddText(ImVec2(a.x + 6.0f, yGrund - ImGui::GetTextLineHeight() - 2.0f), ui::kTextDim,
                text);

    std::snprintf(text, sizeof(text), "-%d s", (int)kWindowSeconds);
    dl->AddText(ImVec2(a.x + 6.0f, b.y + 4.0f), ui::kTextWk, text);

    const char* jetzt = "now";
    dl->AddText(ImVec2(b.x - ImGui::CalcTextSize(jetzt).x - 2.0f, b.y + 4.0f), ui::kTextWk,
                jetzt);
}

// ---- Der Zeiger auf der Kurve ---------------------------------------------
//
// Die Kurve allein sagt "es war mal hoeher". Die Frage beim Zuschauen ist aber
// "wie hoch, und wie lange ist das her" - und genau das steht unter der Maus.
void DrawHover(ImDrawList* dl, ImVec2 a, ImVec2 b, const World& world, const OrePlan& ores,
               const CraftPlan& craft, int ore)
{
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        return;

    const ImVec2 maus = ImGui::GetIO().MousePos;
    if (maus.x < a.x || maus.x > b.x || maus.y < a.y || maus.y > b.y)
        return;

    const float anteil = (b.x > a.x) ? (maus.x - a.x) / (b.x - a.x) : 1.0f;
    const float zeit   = world.marketTime - Span(world) * (1.0f - anteil);

    const int grund = MarketBase(ores, craft, world, ore);
    const int preis = MarketPriceAt(ores, craft, world, ore, zeit);

    float unten = 0.0f;
    float oben  = 0.0f;
    CurveRange(world, grund, unten, oben);

    const float x = maus.x;
    const float y = CurveY((float)preis, unten, oben, a, b);

    dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), ui::kBorderS, 1.0f);
    dl->AddCircleFilled(ImVec2(x, y), 4.5f, ui::kAccent);
    dl->AddCircleFilled(ImVec2(x, y), 2.0f, ui::kCard);

    // Zwei Zeilen: was es kostet, und wie lange das her ist. Die Sekunden
    // rechnen sich aus der Marktzeit zurueck - dieselbe Uhr, die auch steht,
    // wenn das Spiel steht.
    char eins[48];
    std::snprintf(eins, sizeof(eins), "%s", ui::Money(preis).c_str());

    const float her = (world.marketTime - zeit) / ((world.marketSpeed > 0.0f) ? world.marketSpeed
                                                                              : 1.0f);
    char zwei[64];
    if (her < 1.0f)
        std::snprintf(zwei, sizeof(zwei), "%+d %%   now", Swing(preis, grund));
    else
        std::snprintf(zwei, sizeof(zwei), "%+d %%   -%.0f s", Swing(preis, grund), (double)her);

    const ImVec2 s1 = ImGui::CalcTextSize(eins);
    const ImVec2 s2 = ImGui::CalcTextSize(zwei);

    const float bw = std::max(s1.x, s2.x) + 18.0f;
    const float bh = s1.y + s2.y + 14.0f;

    // Neben den Punkt, und immer im Kasten bleiben: sonst haengt das
    // Kaestchen am rechten Rand halb in der Luft.
    float bx = x + 14.0f;
    if (bx + bw > b.x)
        bx = x - 14.0f - bw;
    if (bx < a.x)
        bx = a.x;

    float by = y - bh - 12.0f;
    if (by < a.y)
        by = y + 12.0f;
    if (by + bh > b.y)
        by = b.y - bh;

    ui::Card(dl, ImVec2(bx, by), ImVec2(bx + bw, by + bh), ui::kCard, ui::kBorderS);
    dl->AddText(ImVec2(bx + 9.0f, by + 5.0f), ui::kText, eins);
    dl->AddText(ImVec2(bx + 9.0f, by + 5.0f + s1.y), SwingColor(Swing(preis, grund)), zwei);
}

}  // namespace

// ---- Was ein Stueck kostet -------------------------------------------------

int MarketPriceAt(const OrePlan& ores, const CraftPlan& craft, const World& world, int ore,
                  float zeit)
{
    return StackValue(ores, craft, ore, (int)OreState::Raw, 100, 1, world.moneyPerBlock,
                      world.marketFactorAt(ore, zeit));
}

int MarketPriceNow(const OrePlan& ores, const CraftPlan& craft, const World& world, int ore)
{
    return MarketPriceAt(ores, craft, world, ore, world.marketTime);
}

int MarketBase(const OrePlan& ores, const CraftPlan& craft, const World& world, int ore)
{
    return StackValue(ores, craft, ore, (int)OreState::Raw, 100, 1, world.moneyPerBlock, 1.0f);
}

// ---- Die Seite -------------------------------------------------------------

void DrawMarketPage(World& world, const OrePlan& ores, const CraftPlan& craft,
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

    if (ImGui::Begin("##markt", nullptr, flags))
    {
        const std::vector<int> erze = KnownOresByValue(world, ores, craft);

        // Beim ersten Aufschlagen - und nachdem ein Erz aus der Liste
        // verschwunden ist (neuer Spielstand) - das oberste nehmen.
        if (std::find(erze.begin(), erze.end(), g_markt.ore) == erze.end())
            g_markt.ore = erze.empty() ? -1 : erze.front();

        // ---- Kopfzeile ----------------------------------------------------
        ImGui::PushStyleColor(ImGuiCol_Text, ui::V(ui::kText));
        ImGui::TextUnformatted("Market");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (limits.allowMarket)
            ImGui::TextDisabled("  prices drift around the base value - one raw piece at full"
                                " purity, exactly what market.price(...) reports");
        else
            ImGui::TextDisabled("  prices drift around the base value - one raw piece at full"
                                " purity. Pick the moment yourself and sell.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (erze.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Nothing in your bag yet - mine a block first.");
            ImGui::TextDisabled("Only ores you have held yourself are traded here.");
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
            return;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // ---- Der grosse Kurs ----------------------------------------------
        {
            const int   ore   = g_markt.ore;
            const Ore&  erz   = OreOf(ores, ore);
            const int   grund = MarketBase(ores, craft, world, ore);
            const int   jetzt = MarketPriceNow(ores, craft, world, ore);
            const int   proz  = Swing(jetzt, grund);

            const float breite = ImGui::GetContentRegionAvail().x;
            const float hoehe  = 250.0f;
            const ImVec2 ka    = ImGui::GetCursorScreenPos();
            const ImVec2 kb(ka.x + breite, ka.y + hoehe);

            ui::Card(dl, ka, kb);

            // Kopf der Karte: Kaestchen, Name, Preis, Ausschlag.
            const float pad = ui::kCardPad;
            DrawOreTile(dl, ImVec2(ka.x + pad, ka.y + pad), 40.0f, erz, ore, 5);

            dl->AddText(ImVec2(ka.x + pad + 54.0f, ka.y + pad + 1.0f), ui::kText,
                        erz.name.c_str());

            char zeile[96];
            std::snprintf(zeile, sizeof(zeile), "base %s", ui::Money(grund).c_str());
            dl->AddText(ImVec2(ka.x + pad + 54.0f, ka.y + pad + 20.0f), ui::kTextDim, zeile);

            // Der Preis gross nach rechts: das ist die Zahl, wegen der man
            // hier ist.
            const std::string preis = ui::Money(jetzt);
            const ImVec2      ps    = ImGui::CalcTextSize(preis.c_str());
            dl->AddText(ImVec2(kb.x - pad - ps.x, ka.y + pad + 1.0f), ui::kText, preis.c_str());

            std::snprintf(zeile, sizeof(zeile), "%+d %%", proz);
            const ImVec2 ss = ImGui::CalcTextSize(zeile);
            dl->AddText(ImVec2(kb.x - pad - ss.x, ka.y + pad + 20.0f), SwingColor(proz), zeile);

            // Und darunter die Kurve. Unten bleibt Platz fuer die Zeitachse.
            const ImVec2 ga(ka.x + pad + 4.0f, ka.y + pad + 52.0f);
            const ImVec2 gb(kb.x - pad - 4.0f, kb.y - pad - ImGui::GetTextLineHeight() - 6.0f);
            DrawCurve(dl, ga, gb, world, ores, craft, ore, true);

            // Steht die Welt, steht auch der Markt: in der Vorbereitung und auf
            // Pause laeuft die Marktuhr nicht weiter. Ohne diesen Hinweis
            // stuende man davor und fragte sich, ob das Bild eingefroren ist
            // oder der Preis gerade zufaellig ruhig liegt.
            if (world.frozen && world.marketSwing > 0.0f)
            {
                const char*  halt = "paused - the market moves only while the round runs";
                const ImVec2 hs   = ImGui::CalcTextSize(halt);
                const ImVec2 hp((ga.x + gb.x) * 0.5f - hs.x * 0.5f, ga.y + 6.0f);

                dl->AddRectFilled(ImVec2(hp.x - 10.0f, hp.y - 4.0f),
                                  ImVec2(hp.x + hs.x + 10.0f, hp.y + hs.y + 4.0f), ui::kSunken,
                                  ui::kRoundS);
                dl->AddText(hp, ui::kTextDim, halt);
            }

            // Zum Schluss, damit der Zeiger ueber allem liegt.
            DrawHover(dl, ga, gb, world, ores, craft, ore);

            ImGui::Dummy(ImVec2(breite, hoehe));
        }

        ImGui::Spacing();

        // Der Satz, um den es hier eigentlich geht.
        //
        // Solange market.price() noch nicht gekauft ist, ist der Kurs etwas,
        // das man ansieht und von Hand ausnutzt - und genau das soll auf die
        // Dauer anstrengend werden. Deshalb steht hier dann kein Code,
        // sondern der Hinweis, worauf es hinauslaeuft.
        if (limits.allowMarket)
            ImGui::TextDisabled("if (market.price(%s) > market.average(%s)) item.sell(%s);",
                                OreCodeName(ores, g_markt.ore).c_str(),
                                OreCodeName(ores, g_markt.ore).c_str(),
                                OreCodeName(ores, g_markt.ore).c_str());
        else
            ImGui::TextDisabled("Sell by hand while the price is up. Later market.price(...)"
                                " lets your program watch for you.");

        ImGui::Spacing();

        // ---- Alle Erze untereinander --------------------------------------
        if (ImGui::BeginChild("##kurse", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
        {
            // Der eigene Zeichenspeicher des Kindfensters. Mit dem der Seite
            // wuerden die Zeilen beim Scrollen ueber den Rand hinausmalen -
            // abgeschnitten wird nur, was im richtigen Fenster liegt.
            ImDrawList* kdl = ImGui::GetWindowDrawList();

            const float zeileH = 56.0f;

            // Erst nach der Schleife verkaufen: sonst wackelt einem die Tasche
            // unter den Fingern weg, waehrend man noch ueber sie zeichnet.
            int verkaufen = -1;

            for (const int ore : erze)
            {
                const Ore& erz   = OreOf(ores, ore);
                const int  grund = MarketBase(ores, craft, world, ore);
                const int  jetzt = MarketPriceNow(ores, craft, world, ore);
                const int  proz  = Swing(jetzt, grund);
                const int  tasche = world.bagCount(ore, 0xFFFFFFFFu);

                const float  breite = ImGui::GetContentRegionAvail().x;
                const ImVec2 ra     = ImGui::GetCursorScreenPos();
                const ImVec2 rb(ra.x + breite, ra.y + zeileH - 6.0f);

                // Der Knopf gibt es nur, wenn davon auch etwas daliegt. Solange
                // market.price() fehlt, ist er der einzige Weg, einen guten
                // Preis wirklich mitzunehmen - ohne ihn muesste man die Seite
                // wechseln, und bis dahin ist der Moment vorbei.
                const bool  mitKnopf = (limits.allowSell && tasche > 0);
                const float knopfB   = mitKnopf ? 74.0f : 0.0f;

                char kennung[32];
                std::snprintf(kennung, sizeof(kennung), "##kurs%d", ore);
                ImGui::InvisibleButton(kennung,
                                       ImVec2(std::max(40.0f, breite - knopfB - 26.0f),
                                              zeileH - 6.0f));

                // Wo die naechste Zeile anfangen soll. Der Knopf wird gleich
                // von Hand gesetzt und darf das nicht verschieben.
                const ImVec2 weiter = ImGui::GetCursorScreenPos();

                const bool hovered = ImGui::IsItemHovered();
                const bool gewaehlt = (ore == g_markt.ore);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    g_markt.ore = ore;

                ui::Card(kdl, ra, rb, gewaehlt ? ui::kAccentDim : (hovered ? ui::kSunken : ui::kCard),
                         gewaehlt ? ui::kAccent : ui::kBorder);

                const float mitte = (ra.y + rb.y) * 0.5f;
                const float zeile = ImGui::GetTextLineHeight();

                DrawOreTile(kdl, ImVec2(ra.x + 12.0f, mitte - 16.0f), 32.0f, erz, ore, 4);

                kdl->AddText(ImVec2(ra.x + 56.0f, mitte - zeile - 2.0f), ui::kText,
                            erz.name.c_str());

                char unten[64];
                if (tasche > 0)
                    std::snprintf(unten, sizeof(unten), "base %s  -  %d in bag",
                                  ui::Money(grund).c_str(), tasche);
                else
                    std::snprintf(unten, sizeof(unten), "base %s", ui::Money(grund).c_str());
                kdl->AddText(ImVec2(ra.x + 56.0f, mitte + 2.0f), ui::kTextDim, unten);

                // Rechts: die kleine Kurve, davor Ausschlag und Preis. Von
                // rechts nach links gerechnet, damit alles buendig steht, egal
                // wie breit das Fenster gerade ist.
                const float  rechts = rb.x - 14.0f - (mitKnopf ? (knopfB + 12.0f) : 0.0f);
                const float  kurveB = std::min(170.0f, std::max(60.0f, breite * 0.20f));
                const ImVec2 ka(rechts - kurveB, ra.y + 12.0f);
                const ImVec2 kb(rechts, rb.y - 12.0f);
                if (kb.x > ka.x + 20.0f)
                    DrawCurve(kdl, ka, kb, world, ores, craft, ore, false);

                char proztext[24];
                std::snprintf(proztext, sizeof(proztext), "%+d %%", proz);
                const ImVec2 ps = ImGui::CalcTextSize(proztext);
                kdl->AddText(ImVec2(ka.x - 18.0f - ps.x, mitte - zeile * 0.5f), SwingColor(proz),
                            proztext);

                const std::string preis = ui::Money(jetzt);
                const ImVec2      qs    = ImGui::CalcTextSize(preis.c_str());
                kdl->AddText(ImVec2(ka.x - 18.0f - ps.x - 22.0f - qs.x, mitte - zeile * 0.5f),
                            ui::kText, preis.c_str());

                if (mitKnopf)
                {
                    char knopf[48];
                    std::snprintf(knopf, sizeof(knopf), "Sell##v%d", ore);

                    ImGui::SetCursorScreenPos(
                        ImVec2(rb.x - 14.0f - knopfB, mitte - ImGui::GetFrameHeight() * 0.5f));

                    if (ImGui::Button(knopf, ImVec2(knopfB, 0.0f)))
                        verkaufen = ore;

                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Sell all %d - every state, at today's price",
                                          tasche);

                    ImGui::SetCursorScreenPos(weiter);
                }
            }

            if (verkaufen >= 0)
                world.sell(ores, craft, OreOf(ores, verkaufen).name, -1);
        }
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}
