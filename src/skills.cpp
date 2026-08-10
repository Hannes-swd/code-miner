#include "skills.h"

#include "skilltree.h"
#include "theme.h"
#include "world.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{

// ---- Masse ----------------------------------------------------------------
// Alles in Bildpunkten bei Zoom 1. Eine Rasterzelle ist breiter als eine Karte,
// der Rest ist der Platz fuer die Verbindungslinie.
constexpr float kCell   = 202.0f;
constexpr float kCardW  = 154.0f;
constexpr float kCardH  = 134.0f;
constexpr float kRound  = 16.0f;
constexpr float kIcon   = 54.0f;
constexpr float kMargin = 36.0f;

// ---- Farben ---------------------------------------------------------------
// Aus theme.h. Gekaufte Punkte sind orange gefuellt, alles andere ist eine
// weisse Karte - so sieht man den gegangenen Weg auf einen Blick.
const ImU32 kOwnedTop    = ui::kAccent;
const ImU32 kOwnedBottom = ui::kAccent;
const ImU32 kOwnedRing   = IM_COL32(0x8F, 0x3C, 0x10, 255);
const ImU32 kOwnedGlow   = IM_COL32(0xCC, 0x5B, 0x1E, 26);

const ImU32 kCardTop     = ui::kCard;
const ImU32 kCardBottom  = ui::kCard;
const ImU32 kCardRing    = ui::kBorder;
const ImU32 kBuyRing     = ui::kAccent;

const ImU32 kLinkOpen    = ui::kAccent;
const ImU32 kLinkClosed  = ui::kBorderS;
const ImU32 kLinkHidden  = ui::kBorder;

const ImU32 kTextBright  = IM_COL32(255, 255, 255, 255);  // auf gekauftem Orange
const ImU32 kTextCard    = ui::kText;
const ImU32 kTextGreen   = ui::kAccent;
const ImU32 kTextDim     = ui::kTextDim;

enum class State
{
    Owned,    // gekauft
    Buyable,  // erreichbar und bezahlbar
    TooDear   // erreichbar, aber zu teuer
};

// Es gibt genau eine Skilltree-Seite, also darf die Ansicht hier liegen.
struct View
{
    float  zoom     = 1.0f;
    float  cx       = 0.0f;  // Mittelpunkt, in Rasterzellen
    float  cy       = 0.0f;
    float  userZoom = 1.0f;  // per Mausrad
    ImVec2 pan      = ImVec2(0.0f, 0.0f);
    bool   started  = false;
    bool   dragging = false;
};

View g_view;

float Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

ImU32 Mix(ImU32 a, ImU32 b, float t)
{
    const ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::GetColorU32(ImVec4(ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t,
                                     ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t));
}

// Ein Farbverlauf mit runden Ecken: dieselbe runde Flaeche mehrmals zeichnen,
// jedes Mal auf einen waagerechten Streifen beschnitten.
void GradientRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float round, ImU32 top, ImU32 bottom)
{
    const int bands = 8;
    for (int i = 0; i < bands; ++i)
    {
        const float t0 = (float)i / (float)bands;
        const float t1 = (float)(i + 1) / (float)bands;

        dl->PushClipRect(ImVec2(a.x, a.y + (b.y - a.y) * t0),
                         ImVec2(b.x, a.y + (b.y - a.y) * t1 + 1.0f), true);
        dl->AddRectFilled(a, b, Mix(top, bottom, (t0 + t1) * 0.5f), round);
        dl->PopClipRect();
    }
}

// Mittig, und nie breiter als die Karte: passt es nicht, wird die Schrift
// kleiner. Abgeschnittene Namen sehen schlampig aus.
void TextCentered(ImDrawList* dl, ImFont* font, float size, float maxWidth, float cx, float top,
                  ImU32 col, const char* text)
{
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    if (ts.x > maxWidth && ts.x > 0.0f)
    {
        size *= maxWidth / ts.x;
        ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    }
    dl->AddText(font, size, ImVec2(cx - ts.x * 0.5f, top), col, text);
}

// Die groessere Schrift fuers Zeichen in der Karte. Liegt keine da, tut es
// auch die normale.
ImFont* BigFont()
{
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    return (atlas->Fonts.Size > 1) ? atlas->Fonts[1] : ImGui::GetFont();
}

State StateOf(const SkillNode& n, int money)
{
    if (n.owned)
        return State::Owned;
    return (money >= n.cost) ? State::Buyable : State::TooDear;
}

void DrawCard(ImDrawList* dl, const SkillNode& n, State st, ImVec2 c, float z, bool hovered,
              bool selected, bool frisch)
{
    const ImVec2 a(c.x - kCardW * 0.5f * z, c.y - kCardH * 0.5f * z);
    const ImVec2 b(c.x + kCardW * 0.5f * z, c.y + kCardH * 0.5f * z);
    const float  round = kRound * z;

    ImU32 top    = kCardTop;
    ImU32 bottom = kCardBottom;
    ImU32 ring   = kCardRing;
    ImU32 name   = kTextCard;
    ImU32 glyph  = ui::kText;
    ImU32 state  = kTextDim;

    char label[48];
    if (st == State::Owned)
    {
        top    = kOwnedTop;
        bottom = kOwnedBottom;
        ring   = kOwnedRing;
        name   = kTextBright;
        glyph  = IM_COL32(255, 255, 255, 255);
        state  = IM_COL32(255, 236, 224, 255);
        std::snprintf(label, sizeof(label), "Unlocked");

        // Schein nach aussen, damit man das Gekaufte sofort sieht.
        for (int i = 3; i >= 1; --i)
            dl->AddRect(ImVec2(a.x - (float)i * 3.0f * z, a.y - (float)i * 3.0f * z),
                        ImVec2(b.x + (float)i * 3.0f * z, b.y + (float)i * 3.0f * z), kOwnedGlow,
                        round + (float)i * 3.0f * z, 0, 3.0f * z);

        // Das gerade Gekaufte bekommt einen dunklen Ring aussen herum. Im
        // gewachsenen Baum sieht sonst alles Gekaufte gleich aus, und man
        // findet nicht wieder, was man eben angeklickt hat.
        if (frisch)
        {
            const float d = 5.0f * z;
            dl->AddRect(ImVec2(a.x - d, a.y - d), ImVec2(b.x + d, b.y + d), ui::kText,
                        round + d, 0, 2.5f * z);
        }
    }
    else if (st == State::Buyable)
    {
        ring  = kBuyRing;
        state = kTextGreen;
        std::snprintf(label, sizeof(label), "Buy - %s", ui::Money(n.cost).c_str());
    }
    else
    {
        name  = ui::kTextDim;
        glyph = ui::kTextWk;
        std::snprintf(label, sizeof(label), "Locked - %s", ui::Money(n.cost).c_str());
    }

    GradientRect(dl, a, b, round, top, bottom);

    float border = 1.6f;
    if (hovered)
        border = 2.4f;
    if (selected)
    {
        border = 3.0f;
        ring   = IM_COL32(255, 255, 255, 235);
    }
    dl->AddRect(a, b, ring, round, 0, border * z);

    // Zeichen in einem dunklen Kaestchen - wie ein Symbol.
    const float  box = kIcon * z;
    const ImVec2 ia(c.x - box * 0.5f, a.y + 15.0f * z);
    const ImVec2 ib(ia.x + box, ia.y + box);
    // Das Kaestchen hinter dem Zeichen: auf gekauften Karten hell durchsichtig,
    // sonst die eingelassene Flaeche.
    dl->AddRectFilled(ia, ib,
                      (st == State::Owned) ? IM_COL32(255, 255, 255, 46) : ui::kSunken, 10.0f * z);

    ImFont*     font = BigFont();
    const float gs   = 27.0f * z;
    const ImVec2 ts  = font->CalcTextSizeA(gs, FLT_MAX, 0.0f, SkillTag(n.skill));
    dl->AddText(font, gs, ImVec2((ia.x + ib.x) * 0.5f - ts.x * 0.5f, (ia.y + ib.y) * 0.5f - ts.y * 0.5f),
                glyph, SkillTag(n.skill));

    // Die Schrift schrumpft nicht beliebig mit: sonst steht auf einem grossen
    // Baum nur noch Grau auf den Karten. Was zu lang wird, wird abgeschnitten.
    const float nameSize   = std::max(10.5f, 15.0f * z);
    const float statusSize = std::max(9.5f, 12.5f * z);
    const float textW      = (kCardW - 14.0f) * z;

    TextCentered(dl, font, nameSize, textW, c.x, ib.y + 8.0f * z, name, SkillName(n.skill));
    TextCentered(dl, font, statusSize, textW, c.x, ib.y + 8.0f * z + nameSize + 4.0f * z, state,
                 label);
}

}  // namespace

void DrawSkillPage(World& world, SkillTree& tree)
{
    ImGuiViewport* vp          = ImGui::GetMainViewport();
    ImGuiIO&       io          = ImGui::GetIO();

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::V(ui::kPage));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##skilltree", nullptr, flags))
    {
        // Die ganze Seite ist der Baum. Was ein Punkt macht und kostet, steht
        // im Tooltip - dafuer braucht es keine Leiste daneben.
        const float treeW = vp->WorkSize.x;

        // ---- Was ueberhaupt zu sehen ist ---------------------------------
        // Gekauftes und genau der naechste Schritt. Der Rest bleibt verdeckt,
        // sonst steht von Anfang an alles auf dem Schirm.
        std::vector<int> shown;
        for (const SkillNode& n : tree.nodes)
            if (tree.visible(n.id))
                shown.push_back(n.id);

        if (tree.selected >= 0 && !tree.visible(tree.selected))
            tree.selected = -1;

        // ---- Ansicht ------------------------------------------------------
        int minX = 0, maxX = 0, minY = 0, maxY = 0;
        for (std::size_t i = 0; i < shown.size(); ++i)
        {
            const SkillNode& n = tree.nodes[(std::size_t)shown[i]];
            if (i == 0)
            {
                minX = maxX = n.gx;
                minY = maxY = n.gy;
            }
            minX = std::min(minX, n.gx);
            maxX = std::max(maxX, n.gx);
            minY = std::min(minY, n.gy);
            maxY = std::max(maxY, n.gy);
        }

        const float needW = (float)(maxX - minX) * kCell + kCardW + 2.0f * kMargin;
        const float needH = (float)(maxY - minY) * kCell + kCardH + 2.0f * kMargin;

        // Nach oben begrenzt, damit bei zwei Karten nicht alles riesig wird -
        // nach unten, damit spaeter noch etwas zu lesen ist.
        const float fit = Clamp(std::min(treeW / needW, vp->WorkSize.y / needH), 0.46f, 1.3f);
        const float targetZoom = fit * g_view.userZoom;
        const float targetCx   = (float)(minX + maxX) * 0.5f;
        const float targetCy   = (float)(minY + maxY) * 0.5f;

        // Sanft nachziehen: nach einem Kauf rutscht die Ansicht mit, statt zu
        // springen.
        if (!g_view.started)
        {
            g_view.zoom    = targetZoom;
            g_view.cx      = targetCx;
            g_view.cy      = targetCy;
            g_view.started = true;
        }
        else
        {
            const float k = 1.0f - std::exp(-io.DeltaTime * 9.0f);
            g_view.zoom += (targetZoom - g_view.zoom) * k;
            g_view.cx += (targetCx - g_view.cx) * k;
            g_view.cy += (targetCy - g_view.cy) * k;
        }

        const float  z = g_view.zoom;
        const ImVec2 origin(vp->WorkPos.x + treeW * 0.5f + g_view.pan.x,
                            vp->WorkPos.y + vp->WorkSize.y * 0.5f + g_view.pan.y);

        auto pos = [&](const SkillNode& n)
        {
            return ImVec2(origin.x + ((float)n.gx - g_view.cx) * kCell * z,
                          origin.y + ((float)n.gy - g_view.cy) * kCell * z);
        };

        // ---- Maus ---------------------------------------------------------
        const ImVec2 mouse  = io.MousePos;
        const bool   inTree = ImGui::IsWindowHovered() && mouse.x < vp->WorkPos.x + treeW;

        int hovered = -1;
        for (int id : shown)
        {
            const ImVec2 c = pos(tree.nodes[(std::size_t)id]);
            if (std::fabs(mouse.x - c.x) <= kCardW * 0.5f * z &&
                std::fabs(mouse.y - c.y) <= kCardH * 0.5f * z)
                hovered = id;
        }
        if (!inTree)
            hovered = -1;

        if (inTree && io.MouseWheel != 0.0f)
            g_view.userZoom = Clamp(g_view.userZoom * (1.0f + io.MouseWheel * 0.12f), 0.6f, 1.8f);

        if (inTree && hovered < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            g_view.dragging = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            g_view.dragging = false;
        if (g_view.dragging)
        {
            g_view.pan.x += io.MouseDelta.x;
            g_view.pan.y += io.MouseDelta.y;
        }

        // Nicht ins Leere schieben - der Baum bleibt immer erreichbar.
        const float limitX = std::max(0.0f, needW * z - treeW) * 0.5f + 160.0f;
        const float limitY = std::max(0.0f, needH * z - vp->WorkSize.y) * 0.5f + 160.0f;
        g_view.pan.x = Clamp(g_view.pan.x, -limitX, limitX);
        g_view.pan.y = Clamp(g_view.pan.y, -limitY, limitY);

        if (hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            tree.selected = hovered;
        if (hovered >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            tree.buy(hovered, world);

        // ---- Zeichnen -----------------------------------------------------
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(vp->WorkPos, ImVec2(vp->WorkPos.x + treeW, vp->WorkPos.y + vp->WorkSize.y),
                         true);

        // Erst die Verbindungen, danach die Karten - so liegen die Linien
        // sauber unter den Kaesten.
        for (int id : shown)
        {
            const SkillNode& n = tree.nodes[(std::size_t)id];
            if (n.parent < 0)
                continue;

            const SkillNode& p = tree.nodes[(std::size_t)n.parent];
            const ImVec2     a = pos(p);
            const ImVec2     b = pos(n);
            const ImU32      col = n.owned ? kLinkOpen : kLinkClosed;

            // Normalerweise liegen Eltern und Kind auf einer Achse. Nur im
            // Ausnahmefall wird ein Winkel daraus.
            if (a.x == b.x || a.y == b.y)
            {
                dl->AddLine(a, b, col, (n.owned ? 5.0f : 3.5f) * z);
            }
            else
            {
                const ImVec2 knee(a.x, b.y);
                dl->AddLine(a, knee, col, (n.owned ? 5.0f : 3.5f) * z);
                dl->AddLine(knee, b, col, (n.owned ? 5.0f : 3.5f) * z);
            }
        }

        // Hinweis, dass hinter einer gesperrten Karte noch etwas kommt. Was
        // genau, steht noch nicht fest - das wird erst beim Kauf gewuerfelt.
        for (int id : shown)
        {
            const SkillNode& n = tree.nodes[(std::size_t)id];
            if (n.owned || n.parent < 0 ||
                (tree.plan.steps > 0 && n.depth >= tree.plan.steps))
                continue;

            const SkillNode& p  = tree.nodes[(std::size_t)n.parent];
            const float      dx = (float)(n.gx - p.gx);
            const float      dy = (float)(n.gy - p.gy);
            const float      len = std::sqrt(dx * dx + dy * dy);
            if (len <= 0.0f)
                continue;

            const ImVec2 c = pos(n);
            const ImVec2 e(c.x + dx / len * (kCardW * 0.5f) * z,
                           c.y + dy / len * (kCardH * 0.5f) * z);

            for (int i = 1; i <= 3; ++i)
                dl->AddCircleFilled(ImVec2(e.x + dx / len * (float)i * 10.0f * z,
                                           e.y + dy / len * (float)i * 10.0f * z),
                                    2.4f * z, kLinkHidden);
        }

        for (int id : shown)
        {
            const SkillNode& n = tree.nodes[(std::size_t)id];
            DrawCard(dl, n, StateOf(n, world.money), pos(n), z, id == hovered,
                     id == tree.selected, id == tree.lastBought);
        }

        dl->PopClipRect();

        // Alles, was man ueber einen Punkt wissen muss, steht hier - deshalb
        // braucht die Seite keine Leiste daneben.
        if (hovered >= 0)
        {
            const SkillNode& n = tree.nodes[(std::size_t)hovered];
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(SkillName(n.skill));
            ImGui::PushTextWrapPos(340.0f);
            ImGui::TextDisabled("%s", SkillInfo(n.skill));
            ImGui::PopTextWrapPos();

            if (n.owned)
            {
                ImGui::TextColored(ImVec4(0.70f, 0.89f, 0.48f, 1.0f), "Unlocked");
            }
            else if (world.money < n.cost)
            {
                ImGui::TextDisabled("Costs %s - you are %s short", ui::Money(n.cost).c_str(),
                                    ui::Money(n.cost - world.money).c_str());
            }
            else
            {
                ImGui::TextColored(ImVec4(0.70f, 0.89f, 0.48f, 1.0f),
                                   "Double click to buy - %s money", ui::Money(n.cost).c_str());
            }
            ImGui::EndTooltip();
        }

        // ---- Meldungen zur Datei -----------------------------------------
        // Stimmt an data/skills.txt etwas nicht, muss man das sehen - sonst
        // sucht man den Fehler im Baum statt in der Datei.
        if (!tree.problems.empty())
        {
            ImGui::SetCursorPos(ImVec2(16.0f, 12.0f));
            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.55f, 0.42f, 1.0f));
            ImGui::TextUnformatted("data/skills.txt:");
            for (const std::string& p : tree.problems)
                ImGui::TextUnformatted(p.c_str());
            ImGui::PopStyleColor();
            ImGui::EndGroup();
        }
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void DrawStatus(const Limits& limits, const World& world)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // Zweite Karte der rechten Spalte, direkt unter der Mine. Die Zahlen, auf
    // die es ankommt, stehen gross oben; die Feinheiten als Liste darunter.
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - ui::kRightMargin,
                                   vp->WorkPos.y + ui::kMineTop + ui::kMineHeight + 16.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(ui::kRightWidth, 0.0f), ImGuiCond_Always);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::kCardPad, ui::kCardPad));

    if (ImGui::Begin("##status", nullptr, flags))
    {
        // Nur was da ist. Eine Zeile "Klassen: 0" wuerde verraten, dass es
        // Klassen ueberhaupt gibt - das soll der Baum machen, nicht diese Ecke.

        // ---- Die zwei grossen Zahlen -------------------------------------
        const float halb = ImGui::GetContentRegionAvail().x * 0.5f;

        ImGui::BeginGroup();
        ImGui::TextColored(ui::V(ui::kTextDim), "LEVEL");
        ImGui::PushFont(BigFont());
        ImGui::SetWindowFontScale(0.62f);
        ImGui::TextColored(ui::V(ui::kText), "%d", world.level);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();
        ImGui::EndGroup();

        ImGui::SameLine(halb + ui::kCardPad * 0.5f);

        ImGui::BeginGroup();
        ImGui::TextColored(ui::V(ui::kTextDim), "MONEY");
        ImGui::PushFont(BigFont());
        ImGui::SetWindowFontScale(0.62f);
        ImGui::TextColored(ui::V(ui::kAccent), "%s", ui::Money(world.money).c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();
        ImGui::EndGroup();

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextColored(ui::V(ui::kTextDim), "EFFICIENCY");
        ImGui::Spacing();

        // Name links, Wert rechtsbuendig - wie eine Rechnung.
        auto zeile = [&](const char* name, const char* wert)
        {
            ImGui::TextColored(ui::V(ui::kText), "%s", name);
            const float w = ImGui::CalcTextSize(wert).x;
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - w + ImGui::GetCursorPosX() -
                            ImGui::GetStyle().ItemSpacing.x);
            ImGui::TextColored(ui::V(ui::kText), "%s", wert);
        };

        auto zahl = [&](const char* name, int wert)
        {
            if (wert <= 0)
                return;
            char t[24];
            std::snprintf(t, sizeof(t), "%d", wert);
            zeile(name, t);
        };

        auto wort = [&](const char* name, bool on)
        {
            if (!on)
                return;
            ImGui::TextColored(ui::V(ui::kTextDim), "%s", name);
        };

        char t[32];
        std::snprintf(t, sizeof(t), "%.1f lines/s", limits.linesPerSecond);
        zeile("Speed", t);
        std::snprintf(t, sizeof(t), "%d", limits.moneyPerBlock);
        zeile("Money/block", t);
        std::snprintf(t, sizeof(t), "%.2f s", limits.respawnSeconds);
        zeile("Regrow", t);

        zahl("Loops", limits.maxLoops);
        zahl("Conditions", limits.maxIfs);
        zahl("Variables", limits.maxVariables);
        zahl("Functions", limits.maxFunctions);
        zahl("Classes", limits.maxClasses);
        zahl("Consoles", limits.maxConsoles);

        if (limits.allowWhile || limits.allowFor || limits.allowIf || limits.allowElse ||
            limits.allowPrint || limits.allowCheck || limits.allowShared ||
            limits.allowVariable || limits.allowClass || limits.allowFunction ||
            limits.allowSell || limits.allowBag || limits.allowMine || limits.allowWash ||
            limits.allowSmelt || limits.allowCast || limits.allowClean || limits.allowPolish ||
            limits.allowHarden || limits.allowRefine || limits.allowPress || limits.allowAlloy)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(ui::V(ui::kTextDim), "UNLOCKED");
            ImGui::Spacing();

            wort("while, do", limits.allowWhile);
            wort("for", limits.allowFor);
            wort("if", limits.allowIf);
            wort("else", limits.allowElse);
            wort("print()", limits.allowPrint);
            wort("block.isThere()", limits.allowCheck);
            wort("block.mine()", limits.allowMine);
            wort("item.sell()", limits.allowSell);
            wort("item.has(...)", limits.allowBag);
            wort("item.wash(...)", limits.allowWash);
            wort("item.clean(...)", limits.allowClean);
            wort("item.press(...)", limits.allowPress);
            wort("item.smelt(...)", limits.allowSmelt);
            wort("item.cast(...)", limits.allowCast);
            wort("item.harden(...)", limits.allowHarden);
            wort("item.polish(...)", limits.allowPolish);
            wort("item.refine(...)", limits.allowRefine);
            wort("item.alloy(...)", limits.allowAlloy);
            wort("shared[...]", limits.allowShared);
            wort("int, float, ...", limits.allowVariable);
            wort("class, struct", limits.allowClass);
            wort("your own functions", limits.allowFunction);
        }
    }
    ImGui::End();

    ImGui::PopStyleVar();
}
