#include "wiki.h"

#include "craft.h"
#include "json.h"
#include "ore.h"
#include "skillfile.h"
#include "skilltree.h"
#include "theme.h"
#include "world.h"

#include "imgui.h"

// Ohne NOMINMAX macht windows.h aus min und max Makros - dann laesst sich
// std::max hier nicht mehr aufrufen.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace
{

// ---- Farben ---------------------------------------------------------------
// Alles aus theme.h - die Namen hier bleiben, damit der Zeichencode unveraendert
// bleibt. Ein Farbwert steht im ganzen Spiel nur noch an einer Stelle.
const ImU32 kPanelTop = ui::kCard;
const ImU32 kPanelBot = ui::kCard;
const ImU32 kRing     = ui::kBorder;
const ImU32 kAccent   = ui::kAccent;
const ImU32 kAccentBg = ui::kAccentDim;
const ImU32 kFresh    = ui::kAccent;
const ImU32 kTextHead = ui::kText;
const ImU32 kTextBody = ui::kText;
const ImU32 kTextDim  = ui::kTextDim;
const ImU32 kCodeText = ui::kText;

// ---- kleine Helfer --------------------------------------------------------

float Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// 0 vor a, 1 nach b, dazwischen weich - damit nichts ruckartig einsetzt.
float Ramp(float t, float a, float b)
{
    if (b <= a)
        return (t >= b) ? 1.0f : 0.0f;

    const float x = Clamp((t - a) / (b - a), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

ImU32 Mix(ImU32 a, ImU32 b, float t)
{
    const ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::GetColorU32(ImVec4(ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t,
                                     ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t));
}

ImU32 WithAlpha(ImU32 c, float a)
{
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= Clamp(a, 0.0f, 1.0f);
    return ImGui::GetColorU32(v);
}

// Farbverlauf mit runden Ecken - dasselbe Verfahren wie im Skilltree.
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

// Die zweite, groessere Schrift. Herunterskaliert bleibt sie scharf - deshalb
// wird hier alles daraus gezeichnet und nicht aus der kleinen.
ImFont* BigFont()
{
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    return (atlas->Fonts.Size > 1) ? atlas->Fonts[1] : ImGui::GetFont();
}

void TextAt(ImDrawList* dl, float size, ImVec2 pos, ImU32 col, const char* text,
            float wrap = 0.0f)
{
    dl->AddText(BigFont(), size, pos, col, text, nullptr, wrap);
}

ImVec2 TextSize(float size, const char* text, float wrap = 0.0f)
{
    return BigFont()->CalcTextSizeA(size, FLT_MAX, wrap, text);
}

// Wie breit ein Zeichen ist. Consolas ist eine Schreibmaschinenschrift: jedes
// Zeichen ist gleich breit, deshalb reicht eine einzige Messung.
float CharWidth(float size)
{
    return BigFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, "M").x;
}

// ---- Was der Spieler schon darf -------------------------------------------

bool Unlocked(Skill skill, const Limits& l)
{
    switch (skill)
    {
    case Skill::None: return true;
    case Skill::Mine: return l.allowMine;
    case Skill::Sell: return l.allowSell;
    case Skill::While: return l.allowWhile;
    case Skill::If: return l.allowIf;
    case Skill::Else: return l.allowElse;
    case Skill::For: return l.allowFor;
    case Skill::Print: return l.allowPrint;
    case Skill::Check: return l.allowCheck;
    case Skill::Bag: return l.allowBag;
    case Skill::Shared: return l.allowShared;
    case Skill::Variable: return l.allowVariable;
    case Skill::Class: return l.allowClass;
    case Skill::Function: return l.allowFunction;
    case Skill::Wash: return l.allowWash;
    case Skill::Smelt: return l.allowSmelt;
    case Skill::Cast: return l.allowCast;
    case Skill::Clean: return l.allowClean;
    case Skill::Polish: return l.allowPolish;
    case Skill::Harden: return l.allowHarden;
    case Skill::Refine: return l.allowRefine;
    case Skill::Press: return l.allowPress;
    case Skill::Alloy: return l.allowAlloy;
    default: return false;  // Erweiterungen und Werte haben keine eigene Seite
    }
}

// Ein Schritt ist sichtbar, wenn ALLE seine Punkte gekauft sind. Er zeigt ja
// ein Stueck Code - darin muss jeder Befehl erlaubt sein, sonst steht dort
// etwas, das der Spieler nirgends hinschreiben darf.
bool SchrittSichtbar(const WikiStep& step, const Limits& l)
{
    for (Skill s : step.needs)
        if (!Unlocked(s, l))
            return false;
    return true;
}

// Eine Seite ist sichtbar, wenn sie nichts braucht - oder wenn EINER der
// genannten Punkte gekauft ist. Die Uebersicht ueber das Verarbeiten soll
// auftauchen, sobald man irgendeinen der acht Schritte hat.
bool Sichtbar(const WikiPage& page, const Limits& l)
{
    if (page.needs.empty())
        return true;

    for (Skill s : page.needs)
        if (Unlocked(s, l))
            return true;

    return false;
}

// ---- Zustand der Seite ----------------------------------------------------
//
// Es gibt genau eine Wiki-Seite, also darf die Ansicht hier liegen. Gespeichert
// wird nichts davon: wo man im Wiki war, gehoert nicht in den Spielstand.
struct WikiView
{
    std::string category;  // leer = Startseite
    int         page    = -1;
    int         step    = 0;
    float       time    = 0.0f;  // Sekunden im aktuellen Schritt
    bool        playing = true;

    // Welches Erz in der Kategorie "Erze" offen ist. Eine eigene Zahl, weil
    // die Erze keine Seiten aus der Datei sind - page zeigt dort ins Leere.
    int ore = -1;

    // Und welche Zeile darin aufgeklappt ist: der Zustand, zu dem gerade alle
    // Wege untereinander stehen. -1 = keiner.
    int oreState = -1;
};

WikiView g_wiki;

// Wer eine Seite aufschlaegt, hat sie gesehen - die Markierung faellt weg.
void GoTo(int page, const WikiBook& book, std::set<std::string>& seen)
{
    g_wiki.page    = page;
    g_wiki.step    = 0;
    g_wiki.time    = 0.0f;
    g_wiki.playing = true;

    if (page >= 0 && page < (int)book.pages.size())
        seen.insert(book.pages[(std::size_t)page].title);
}

// ---- Die Erz-Sammlung ------------------------------------------------------
//
// Die Kategorie "Erze" steht als einzige NICHT in data/wiki.json - ihre Seiten
// entstehen beim Spielen. Ein Erz kommt dazu, sobald es einmal in der Tasche
// lag, ein Weg, sobald man ihn einmal gegangen ist. Deshalb steht hier nie ein
// Preis, den man nicht selbst herausgefunden hat.

const char* kOreCategory = "erze";

// Ein Weg zu einem Zustand: welche Schritte dorthin fuehren, welche Reinheit
// dabei herauskommt und was ein Stueck dann wert ist.
struct OreWay
{
    bool             known  = false;
    int              purity = 0;
    int              price  = 0;
    std::vector<int> steps;  // Indizes in craft.steps
};

// Notbremse. Bei den Daten, die mitgeliefert werden, kommen hoechstens 26 Wege
// je Zustand heraus - das hier greift nie. Es haelt nur eine
// data/verarbeitung.json im Zaum, in der jemand so viele Schritte kreuz und
// quer verbindet, dass die Suche die Oberflaeche stehen laesst.
//
// Gezaehlt wird ueber ALLE Zustaende zusammen, und abgeschnitten wird beim
// Suchen - nicht beim Anzeigen. Deshalb steht die Grenze so hoch: was gesammelt
// ist, wird erst danach sortiert, und ein zu frueh abgeschnittener Fund koennte
// sonst genau der beste gewesen sein.
const int kMaxWays = 4000;

// So viele Wege stehen hoechstens untereinander. Mehr liest niemand.
const std::size_t kMaxShown = 25;

// ALLE Wege durchgehen und je Zustand jeden einzelnen merken - nicht nur den
// besten. Genau darum geht es ja: dass es mehrere gibt und dass sie
// unterschiedlich viel bringen.
//
// Kein Zustand wird dabei zweimal besucht. Reinigen und Pressen fuehren
// naemlich im Kreis (gereinigt -> gepresst -> gereinigt ...) und schrauben
// dabei die Reinheit immer weiter hoch. Ein Weg mit fuenf Runden darin waere
// zwar teurer, aber keine Antwort auf die Frage "wie komme ich dorthin".
void SearchWays(const OrePlan& ores, const CraftPlan& craft, const Ore& erz, int oreIdx,
                const std::set<World::OreStep>& kanten, int moneyPerBlock, int state, int purity,
                unsigned besucht, std::vector<int>& weg, std::vector<std::vector<OreWay>>& out,
                int& gesamt)
{
    if (gesamt >= kMaxWays)
        return;
    ++gesamt;

    OreWay w;
    w.known  = true;
    w.purity = purity;
    w.price  = StackValue(ores, craft, oreIdx, state, purity, 1, moneyPerBlock);
    w.steps  = weg;
    out[(std::size_t)state].push_back(w);

    for (int i = 0; i < (int)craft.steps.size(); ++i)
    {
        const CraftStep& s = craft.steps[(std::size_t)i];

        if (!s.fits(state))
            continue;
        if (s.to < 0 || s.to >= (int)OreState::Count)
            continue;
        if ((besucht & (1u << (unsigned)s.to)) != 0)
            continue;
        if (!erz.allows((OreState)s.to))
            continue;

        // Der Kern der Sache: nur Schritte, die der Spieler bei genau diesem
        // Erz und aus genau diesem Zustand heraus schon einmal gemacht hat.
        if (kanten.find(World::OreStep{oreIdx, state, s.to}) == kanten.end())
            continue;

        int rein = purity + s.purity;
        if (rein < 0)
            rein = 0;
        if (rein > 100)
            rein = 100;

        weg.push_back(i);
        SearchWays(ores, craft, erz, oreIdx, kanten, moneyPerBlock, s.to, rein,
                   besucht | (1u << (unsigned)s.to), weg, out, gesamt);
        weg.pop_back();
    }
}

// Alle bekannten Wege zu jedem Zustand, der beste zuerst. Eine leere Liste
// heisst: dorthin hat der Spieler noch nicht gefunden.
//
// "Bester Weg" heisst: der teuerste. Bei gleichem Preis der kuerzere - wer
// dasselbe mit weniger Arbeit bekommt, hat den besseren Weg gefunden.
std::vector<std::vector<OreWay>> OreWays(const World& world, const OrePlan& ores,
                                         const CraftPlan& craft, int oreIdx)
{
    std::vector<std::vector<OreWay>> out((std::size_t)OreState::Count);

    const auto it = world.oreFirst.find(oreIdx);
    if (it == world.oreFirst.end())
        return out;

    const int anfang = it->second.state;
    if (anfang < 0 || anfang >= (int)OreState::Count)
        return out;

    std::vector<int> weg;
    int              gesamt = 0;
    SearchWays(ores, craft, OreOf(ores, oreIdx), oreIdx, world.oreSteps, world.moneyPerBlock,
               anfang, it->second.purity, 1u << (unsigned)anfang, weg, out, gesamt);

    for (std::vector<OreWay>& liste : out)
        std::sort(liste.begin(), liste.end(),
                  [](const OreWay& a, const OreWay& b)
                  {
                      if (a.price != b.price)
                          return a.price > b.price;
                      return a.steps.size() < b.steps.size();
                  });

    return out;
}

// Welche Zustaende bei diesem Erz ueberhaupt eine Zeile bekommen: der Anfang
// und alles, wo ein Schritt hinfuehrt. "Oxidiert" faellt damit heraus - dorthin
// fuehrt kein Schritt, das ist Verfall.
std::vector<int> OreRows(const OrePlan& ores, const CraftPlan& craft, const World& world,
                         int oreIdx)
{
    std::vector<int> zeilen;

    const auto it = world.oreFirst.find(oreIdx);
    if (it == world.oreFirst.end())
        return zeilen;

    const Ore& erz = OreOf(ores, oreIdx);
    zeilen.push_back(it->second.state);

    for (const CraftStep& s : craft.steps)
    {
        if (s.to < 0 || s.to >= (int)OreState::Count)
            continue;
        if (!erz.allows((OreState)s.to))
            continue;
        if (std::find(zeilen.begin(), zeilen.end(), s.to) == zeilen.end())
            zeilen.push_back(s.to);
    }

    // Nach Wert sortiert: billig oben, teuer unten. So liest sich die Tabelle
    // wie eine Leiter - und man sieht sofort, wie weit oben man schon ist.
    std::sort(zeilen.begin(), zeilen.end(),
              [&](int a, int b)
              {
                  const float va = craft.valueOf(a);
                  const float vb = craft.valueOf(b);
                  if (va != vb)
                      return va < vb;
                  return a < b;
              });
    return zeilen;
}

// Welche Erze der Spieler schon kennt, in der Reihenfolge aus data/erze.json.
std::vector<int> KnownOres(const World& world, const OrePlan& ores)
{
    std::vector<int> out;
    for (const auto& e : world.oreFirst)
        if (e.first >= 0 && e.first < (int)ores.ores.size())
            out.push_back(e.first);
    return out;
}

// ---- Datei suchen ---------------------------------------------------------
// Der Projektordner kommt zuerst: wer dort etwas aendert, will es sofort im
// Spiel sehen.
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

        out.push_back(dir + "\\..\\..\\data\\wiki.json");  // build/Debug -> Projekt
        out.push_back(dir + "\\..\\..\\..\\data\\wiki.json");
        out.push_back(dir + "\\..\\data\\wiki.json");
        out.push_back(dir + "\\data\\wiki.json");
        out.push_back(dir + "\\wiki.json");
    }

    out.push_back("data/wiki.json");
    out.push_back("wiki.json");
    return out;
}

// Das wievielte Vorkommen? hit zaehlt ab 1.
std::size_t FindHit(const std::string& hay, const std::string& needle, int hit)
{
    if (needle.empty())
        return std::string::npos;

    std::size_t pos = 0;
    for (int i = 0; i < hit; ++i)
    {
        pos = (i == 0) ? hay.find(needle) : hay.find(needle, pos + 1);
        if (pos == std::string::npos)
            return pos;
    }
    return pos;
}

// ---- Zeilen im Code -------------------------------------------------------
struct CodeLine
{
    int off = 0;
    int len = 0;
};

std::vector<CodeLine> SplitLines(const std::string& code)
{
    std::vector<CodeLine> out;

    int start = 0;
    for (int i = 0; i <= (int)code.size(); ++i)
    {
        if (i == (int)code.size() || code[(std::size_t)i] == '\n')
        {
            CodeLine l;
            l.off = start;
            l.len = i - start;
            out.push_back(l);
            start = i + 1;
        }
    }
    return out;
}

// Kaesten um einen Bereich im Code. Geht der Bereich ueber mehrere Zeilen,
// bekommt jede Zeile ihren eigenen Kasten.
std::vector<ImVec4> RangeRects(const std::vector<CodeLine>& lines, int start, int len, ImVec2 org,
                               float adv, float lineH, float size)
{
    std::vector<ImVec4> out;
    if (start < 0 || len <= 0)
        return out;

    const int end = start + len;
    for (int i = 0; i < (int)lines.size(); ++i)
    {
        const int ls = lines[(std::size_t)i].off;
        const int le = ls + lines[(std::size_t)i].len;
        const int s  = std::max(start, ls);
        const int e  = std::min(end, le);
        if (s >= e)
            continue;

        out.push_back(ImVec4(org.x + (float)(s - ls) * adv, org.y + (float)i * lineH,
                             org.x + (float)(e - ls) * adv, org.y + (float)i * lineH + size));
    }
    return out;
}

// Was ist am neuen Code neu? Gleicher Anfang und gleiches Ende fallen weg, der
// Rest dazwischen ist dazugekommen - genau der laeuft dann sichtbar ein.
void DiffRange(const std::string& older, const std::string& newer, int& start, int& len)
{
    std::size_t p = 0;
    while (p < older.size() && p < newer.size() && older[p] == newer[p])
        ++p;

    std::size_t s = 0;
    while (s < older.size() - p && s < newer.size() - p &&
           older[older.size() - 1 - s] == newer[newer.size() - 1 - s])
        ++s;

    start = (int)p;
    len   = (int)(newer.size() - p - s);
}

// ---- Ein Schritt der Animation --------------------------------------------
//
// Der Ablauf steht in Sekunden ab dem Anfang des Schrittes. Bei einem kurzen
// Schritt wird alles zusammengeschoben (k), sonst waere die Blase nie zu sehen.
void DrawAnimation(const WikiPage& page, int stepIndex, float t, ImVec2 a, ImVec2 b)
{
    ImDrawList*     dl   = ImGui::GetWindowDrawList();
    const WikiStep& step = page.steps[(std::size_t)stepIndex];

    const float dur = std::max(1.2f, step.seconds);
    const float k   = std::min(1.0f, dur / 2.2f);
    const float fo  = 1.0f - Ramp(t, dur - 0.45f, dur);  // am Ende ausblenden

    const std::string& code = step.code;
    const std::string  prev =
        (stepIndex > 0) ? page.steps[(std::size_t)stepIndex - 1].code : std::string();

    int newStart = 0;
    int newLen   = 0;
    DiffRange(prev, code, newStart, newLen);

    const std::vector<CodeLine> lines = SplitLines(code);

    int cols = 0;
    for (const CodeLine& l : lines)
        cols = std::max(cols, l.len);

    // Die Schrift schrumpft nur so weit, wie sie muss: der Code soll neben die
    // Sprechblase passen und trotzdem lesbar bleiben.
    const float areaW = b.x - a.x;
    const float areaH = b.y - a.y;

    float size = 21.0f;
    if (cols > 0)
        size = std::min(size, areaW * 0.52f / ((float)cols * CharWidth(1.0f)));
    if (!lines.empty())
        size = std::min(size, areaH * 0.55f / ((float)lines.size() * 1.45f));
    size = Clamp(size, 11.0f, 21.0f);

    const float adv    = CharWidth(size);
    const float lineH  = size * 1.45f;
    const float blockW = (float)cols * adv;
    const float blockH = (float)lines.size() * lineH;

    // Etwas links der Mitte: rechts daneben soll die Blase Platz haben.
    float cx = a.x + areaW * 0.40f;
    if (blockW > areaW * 0.66f)
        cx = a.x + areaW * 0.5f;

    ImVec2 org(cx - blockW * 0.5f, a.y + (areaH - blockH) * 0.40f);
    org.x = Clamp(org.x, a.x + 26.0f, std::max(a.x + 26.0f, b.x - blockW - 26.0f));
    org.y = Clamp(org.y, a.y + 22.0f, std::max(a.y + 22.0f, b.y - blockH - 22.0f));

    // ---- Der Kasten unter dem Code ----------------------------------------
    const ImVec2 pa(org.x - 18.0f, org.y - 14.0f);
    const ImVec2 pb(org.x + blockW + 18.0f, org.y + blockH + 12.0f);
    GradientRect(dl, pa, pb, 10.0f, kPanelTop, kPanelBot);
    dl->AddRect(pa, pb, kRing, 10.0f, 0, 1.4f);

    // ---- Der Code selbst --------------------------------------------------
    const float in     = Ramp(t, 0.0f, 0.42f * k);  // wie weit das Neue schon da ist
    const float glow   = 1.0f - Ramp(t, 0.10f * k, 0.95f * k);
    const char* base   = code.c_str();
    const int   newEnd = newStart + newLen;

    if (newLen > 0)
    {
        // Kurz hinterlegt, dann geht die Hinterlegung wieder weg - so sieht
        // man, was gerade dazugekommen ist.
        const std::vector<ImVec4> fresh =
            RangeRects(lines, newStart, newLen, org, adv, lineH, size);
        for (const ImVec4& r : fresh)
            dl->AddRectFilled(ImVec2(r.x - 2.0f, r.y - 2.0f), ImVec2(r.z + 2.0f, r.w + 3.0f),
                              WithAlpha(kFresh, glow * 0.22f), 3.0f);
    }

    for (int i = 0; i < (int)lines.size(); ++i)
    {
        const int   ls = lines[(std::size_t)i].off;
        const int   le = ls + lines[(std::size_t)i].len;
        const float y  = org.y + (float)i * lineH;

        const int ns = std::min(std::max(newStart, ls), le);
        const int ne = std::min(std::max(newEnd, ls), le);

        if (ns > ls)
            dl->AddText(BigFont(), size, ImVec2(org.x, y), kCodeText, base + ls, base + ns);

        if (ne > ns)
        {
            // Das Neue waechst herein: es rutscht von unten hoch und blendet
            // dabei auf.
            const ImVec2 p(org.x + (float)(ns - ls) * adv, y + (1.0f - in) * 6.0f);
            dl->AddText(BigFont(), size, p, WithAlpha(Mix(kFresh, kCodeText, in), in), base + ns,
                        base + ne);
        }

        if (le > ne)
            dl->AddText(BigFont(), size, ImVec2(org.x + (float)(ne - ls) * adv, y), kCodeText,
                        base + ne, base + le);
    }

    // ---- Die Markierung ---------------------------------------------------
    const float               mk = Ramp(t, 0.35f * k, 0.66f * k) * fo;
    const std::vector<ImVec4> marks =
        RangeRects(lines, step.markStart, step.markLen, org, adv, lineH, size);

    ImVec2 from(pb.x, (pa.y + pb.y) * 0.5f);  // Ansatz fuer den Strich
    bool   haveMark = false;

    if (!marks.empty() && mk > 0.01f)
    {
        const float grow = (1.0f - Ramp(t, 0.35f * k, 0.66f * k)) * 9.0f;
        for (std::size_t i = 0; i < marks.size(); ++i)
        {
            const ImVec4& r = marks[i];
            const ImVec2  ra(r.x - 4.0f - grow, r.y - 3.0f - grow);
            const ImVec2  rb(r.z + 4.0f + grow, r.w + 4.0f + grow);

            dl->AddRectFilled(ra, rb, WithAlpha(kAccentBg, mk * 0.55f), 4.0f);
            dl->AddRect(ra, rb, WithAlpha(kAccent, mk), 4.0f, 0, 2.0f);

            if (i == 0)
                from = ImVec2(rb.x, (ra.y + rb.y) * 0.5f);
        }
        haveMark = true;
    }

    // ---- Die Sprechblase --------------------------------------------------
    const float bubbleW = std::min(370.0f, std::max(180.0f, areaW * 0.42f));
    const float pad     = 14.0f;
    const float headSz  = 14.0f;
    const float bodySz  = 15.5f;

    const ImVec2 headTs  = TextSize(headSz, step.point.c_str(), bubbleW - 2.0f * pad);
    const ImVec2 bodyTs  = TextSize(bodySz, step.text.c_str(), bubbleW - 2.0f * pad);
    const float  bubbleH = pad + headTs.y + 7.0f + bodyTs.y + pad;

    const bool rightSide = (pb.x + 46.0f + bubbleW) <= (b.x - 12.0f);

    ImVec2 ba;
    if (rightSide)
    {
        ba = ImVec2(pb.x + 46.0f, from.y - bubbleH * 0.5f);
    }
    else
    {
        // Kein Platz daneben: dann wandert die Blase nach unten, und der Strich
        // geht von unten aus der Markierung heraus.
        ba = ImVec2(cx - bubbleW * 0.5f, pb.y + 46.0f);
        if (haveMark)
            from = ImVec2((marks[0].x + marks[0].z) * 0.5f, marks[0].w + 6.0f);
        else
            from = ImVec2((pa.x + pb.x) * 0.5f, pb.y);
    }

    ba.x = Clamp(ba.x, a.x + 10.0f, std::max(a.x + 10.0f, b.x - bubbleW - 10.0f));
    ba.y = Clamp(ba.y, a.y + 8.0f, std::max(a.y + 8.0f, b.y - bubbleH - 8.0f));

    const ImVec2 bb(ba.x + bubbleW, ba.y + bubbleH);
    const ImVec2 to = rightSide ? ImVec2(ba.x, (ba.y + bb.y) * 0.5f)
                                : ImVec2((ba.x + bb.x) * 0.5f, ba.y);

    // Der Strich zeichnet sich sichtbar auf - er ist nicht einfach da.
    const float lp = Ramp(t, 0.60f * k, 1.15f * k) * fo;
    if (lp > 0.01f)
    {
        const ImVec2 end(from.x + (to.x - from.x) * lp, from.y + (to.y - from.y) * lp);
        dl->AddLine(from, end, WithAlpha(kAccent, fo), 2.2f);
        dl->AddCircleFilled(from, 3.4f, WithAlpha(kAccent, fo));
    }

    const float bp = Ramp(t, 1.02f * k, 1.42f * k) * fo;
    if (bp > 0.01f)
    {
        const float  lift = (1.0f - Ramp(t, 1.02f * k, 1.42f * k)) * 10.0f;
        const ImVec2 qa(ba.x, ba.y + lift);
        const ImVec2 qb(bb.x, bb.y + lift);

        GradientRect(dl, qa, qb, 10.0f, WithAlpha(ui::kSunken, bp),
                     WithAlpha(ui::kSunken, bp));
        dl->AddRect(qa, qb, WithAlpha(kAccent, bp * 0.75f), 10.0f, 0, 1.6f);

        TextAt(dl, headSz, ImVec2(qa.x + pad, qa.y + pad), WithAlpha(kAccent, bp),
               step.point.c_str(), bubbleW - 2.0f * pad);
        TextAt(dl, bodySz, ImVec2(qa.x + pad, qa.y + pad + headTs.y + 7.0f),
               WithAlpha(kTextBody, bp), step.text.c_str(), bubbleW - 2.0f * pad);
    }
}

// ---- Die Zeitleiste -------------------------------------------------------
//
// Sie ist das Inhaltsverzeichnis der Animation: ein Punkt je Schritt, jeder
// beschriftet und anklickbar.
void DrawTimeline(const WikiPage& page, float width)
{
    const int   n = (int)page.steps.size();
    const float w = std::max(width, 120.0f);

    if (ImGui::Button(g_wiki.playing ? "Pause" : "Play", ImVec2(96.0f, 0.0f)))
        g_wiki.playing = !g_wiki.playing;

    ImGui::SameLine();
    if (ImGui::Button("Restart", ImVec2(96.0f, 0.0f)))
    {
        g_wiki.step = 0;
        g_wiki.time = 0.0f;
    }

    // Die Leiste laeuft nach ZEIT, nicht nach Schritten: ein langer Schritt
    // bekommt ein breites Stueck. Nur so sieht man auch beim letzten Schritt
    // noch, wie lange es dauert - bei gleichmaessigen Abstaenden waere der
    // Balken dort schon am Anschlag.
    auto dauerVon = [&](int i)
    { return std::max(1.2f, page.steps[(std::size_t)i].seconds); };

    float gesamt = 0.0f;
    for (int i = 0; i < n; ++i)
        gesamt += dauerVon(i);

    float davor = 0.0f;  // Zeit vor dem laufenden Schritt
    for (int i = 0; i < g_wiki.step; ++i)
        davor += dauerVon(i);

    const float dur     = dauerVon(g_wiki.step);
    const float gelaufen = davor + std::min(g_wiki.time, dur);
    const float rest     = std::max(0.0f, gesamt - gelaufen);

    ImGui::SameLine();
    ImGui::TextDisabled("Step %d of %d  -  %.0f s left", g_wiki.step + 1, n, rest);

    ImGui::SameLine();
    ImGui::TextDisabled("   [Space] pause   [<- ->] step   [R] restart");

    ImGui::Spacing();

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##zeitleiste", ImVec2(w, 52.0f));

    const float x0 = p0.x + 26.0f;
    const float x1 = p0.x + w - 26.0f;
    const float y  = p0.y + 13.0f;

    const float prog = (gesamt > 0.0f) ? Clamp(gelaufen / gesamt, 0.0f, 1.0f) : 0.0f;

    // Ein Wegpunkt sitzt dort, wo sein Schritt anfaengt.
    auto pointX = [&](int i)
    {
        if (gesamt <= 0.0f)
            return (x0 + x1) * 0.5f;
        float vor = 0.0f;
        for (int k = 0; k < i; ++k)
            vor += dauerVon(k);
        return x0 + (x1 - x0) * (vor / gesamt);
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), IM_COL32(58, 63, 74, 255), 3.0f);
    dl->AddLine(ImVec2(x0, y), ImVec2(x0 + (x1 - x0) * prog, y), kAccent, 3.0f);

    int hovered = -1;
    if (ImGui::IsItemHovered())
    {
        const float mx   = ImGui::GetIO().MousePos.x;
        float       best = 1e9f;
        for (int i = 0; i < n; ++i)
        {
            const float d = std::fabs(mx - pointX(i));
            if (d < best && d < 46.0f)
            {
                best    = d;
                hovered = i;
            }
        }
    }

    if (hovered >= 0 && ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        g_wiki.step = hovered;
        g_wiki.time = 0.0f;
    }

    for (int i = 0; i < n; ++i)
    {
        const float px = pointX(i);
        const float r  = (i == hovered) ? 8.5f : 6.5f;

        dl->AddCircleFilled(ImVec2(px, y), r,
                            (i <= g_wiki.step) ? kAccent : IM_COL32(70, 76, 88, 255));
        if (i == g_wiki.step)
            dl->AddCircle(ImVec2(px, y), r + 3.5f, IM_COL32(255, 255, 255, 220), 0, 2.0f);

        // Beschriftung: die erste linksbuendig, die letzte rechtsbuendig - so
        // laeuft nichts ueber den Rand hinaus.
        const char*  label = page.steps[(std::size_t)i].point.c_str();
        const float  ls    = 13.0f;
        const ImVec2 ts    = TextSize(ls, label);

        // Am Rand einklappen, damit keine Beschriftung hinauslaeuft.
        float tx = px - ts.x * 0.5f;
        tx = std::max(tx, p0.x + 2.0f);
        tx = std::min(tx, p0.x + w - 2.0f - ts.x);

        TextAt(dl, ls, ImVec2(tx, y + 14.0f),
               (i == g_wiki.step) ? kTextHead : ((i == hovered) ? kTextBody : kTextDim), label);
    }
}

// ---- Brotkrume ------------------------------------------------------------
// Jedes Stueck davor ist anklickbar und springt dorthin zurueck.
bool Crumb(const char* label, bool clickable)
{
    if (!clickable)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.96f, 0.99f, 1.0f));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        return false;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::V(ui::kAccentDim));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::V(ui::kAccentDim));
    ImGui::PushStyleColor(ImGuiCol_Text, ui::V(ui::kAccent));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));

    const bool clicked = ImGui::Button(label);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return clicked;
}

void Slash()
{
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextDisabled(">");
    ImGui::SameLine(0.0f, 4.0f);
}

// ---- Die Kategorie "Erze" zeichnen ----------------------------------------
//
// Links die Erze, die man kennt, rechts eine Tabelle: jede Zeile ein Zustand,
// dazu der beste Weg dorthin, den man selbst schon gegangen ist. Was noch
// keiner ist, steht als "?" da - das ist die Sammlung.
void DrawOreCollection(World& world, const OrePlan& ores, const CraftPlan& craft)
{
    const std::vector<int> erze = KnownOres(world, ores);

    if (erze.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Nothing here yet. Mine a block and its ore gets a page here -");
        ImGui::TextDisabled("and every step you take with it is added.");
        return;
    }

    // Nach einem Zuruecksetzen kann das gewaehlte Erz weg sein.
    if (std::find(erze.begin(), erze.end(), g_wiki.ore) == erze.end())
        g_wiki.ore = -1;

    // ---- links: was man kennt ---------------------------------------------
    ImGui::BeginChild("##erzliste", ImVec2(250.0f, 0.0f), ImGuiChildFlags_Borders);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        for (int i : erze)
        {
            const Ore&        erz    = OreOf(ores, i);
            const std::string schluessel = WikiOreKey(erz.name);
            const bool        neu    = world.wikiSeen.find(schluessel) == world.wikiSeen.end();
            const ImVec2      p0     = ImGui::GetCursorScreenPos();

            char id[32];
            std::snprintf(id, sizeof(id), "##erz%d", i);
            if (ImGui::Selectable(id, i == g_wiki.ore, 0, ImVec2(0.0f, 30.0f)))
            {
                g_wiki.ore      = i;
                g_wiki.oreState = -1;  // beim Erzwechsel klappt die Zeile zu
                world.wikiSeen.insert(schluessel);
            }

            // Erkennen soll man das Erz an Farbe und Muster - also steht das
            // Kaestchen davor, genau wie in der Tasche.
            DrawOreTile(dl, ImVec2(p0.x + 4.0f, p0.y + 4.0f), 22.0f, erz, i, 8);
            dl->AddText(ImVec2(p0.x + 34.0f, p0.y + 7.0f), neu ? kFresh : kTextBody,
                        erz.name.c_str());

            if (neu)
                dl->AddCircleFilled(ImVec2(ImGui::GetItemRectMax().x - 12.0f, p0.y + 15.0f), 4.5f,
                                    kAccent);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##erzseite", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);

    if (g_wiki.ore < 0)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Pick an ore on the left.");
        ImGui::EndChild();
        return;
    }

    const Ore&            erz    = OreOf(ores, g_wiki.ore);
    const World::OreFirst anfang = world.oreFirst.find(g_wiki.ore)->second;

    // ---- Kopf: Bild, Name, Grundwert --------------------------------------
    {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        DrawOreTile(ImGui::GetWindowDrawList(), ImVec2(p0.x + 4.0f, p0.y), 72.0f, erz, g_wiki.ore,
                    16);
        ImGui::Dummy(ImVec2(80.0f, 72.0f));
        ImGui::SameLine(0.0f, 14.0f);

        ImGui::BeginGroup();
        ImGui::TextUnformatted(erz.name.c_str());
        ImGui::TextDisabled("Base value %d   -   from level %d", erz.value, erz.minLevel);
        ImGui::TextDisabled("found as %s at %d %% purity",
                            OreStateName((OreState)anfang.state), anfang.purity);
        ImGui::EndGroup();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- die Tabelle ------------------------------------------------------
    const std::vector<std::vector<OreWay>> wege   = OreWays(world, ores, craft, g_wiki.ore);
    const std::vector<int>                 zeilen = OreRows(ores, craft, world, g_wiki.ore);

    // Der Weg als Text: "Waschen > Reinigen".
    auto wegText = [&](const OreWay& w)
    {
        if (w.steps.empty())
            return std::string("how you found it");

        std::string text;
        for (std::size_t k = 0; k < w.steps.size(); ++k)
        {
            if (k > 0)
                text += " > ";
            text += craft.steps[(std::size_t)w.steps[k]].name;
        }
        return text;
    };

    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                               ImGuiTableFlags_SizingStretchProp;

    int gefunden = 0;

    if (ImGui::BeginTable("##wege", 5, tf))
    {
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("best way there");
        ImGui::TableSetupColumn("Ways", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Purity", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();

        for (int s : zeilen)
        {
            const std::vector<OreWay>& liste = wege[(std::size_t)s];
            const bool                 known = !liste.empty();
            if (known)
                ++gefunden;

            ImGui::TableNextRow();

            // Die ganze Zeile ist anklickbar: unten stehen dann ALLE Wege
            // dorthin. Ohne das saehe man immer nur den Sieger - und dass es
            // ueberhaupt mehrere gibt, waere das Geheimnis des Spiels.
            ImGui::TableNextColumn();
            char id[32];
            std::snprintf(id, sizeof(id), "##zeile%d", s);
            if (ImGui::Selectable(id, g_wiki.oreState == s, ImGuiSelectableFlags_SpanAllColumns) &&
                known)
                g_wiki.oreState = (g_wiki.oreState == s) ? -1 : s;

            ImGui::SameLine(0.0f, 0.0f);
            if (known)
                ImGui::TextUnformatted(OreStateName((OreState)s));
            else
                ImGui::TextDisabled("%s", OreStateName((OreState)s));

            ImGui::TableNextColumn();
            if (known)
                ImGui::TextUnformatted(wegText(liste[0]).c_str());
            else
                ImGui::TextDisabled("?");

            ImGui::TableNextColumn();
            if (!known)
                ImGui::TextDisabled("?");
            else if (liste.size() > 1)
                ImGui::Text("%d", (int)liste.size());
            else
                ImGui::TextDisabled("1");

            ImGui::TableNextColumn();
            if (known)
                ImGui::Text("%d %%", liste[0].purity);
            else
                ImGui::TextDisabled("?");

            ImGui::TableNextColumn();
            if (known)
                ImGui::Text("%d", liste[0].price);
            else
                ImGui::TextDisabled("?");
        }
        ImGui::EndTable();
    }

    // ---- alle Wege zu dem, was gerade angeklickt ist ----------------------
    if (g_wiki.oreState >= 0 && g_wiki.oreState < (int)wege.size() &&
        !wege[(std::size_t)g_wiki.oreState].empty())
    {
        const std::vector<OreWay>& liste = wege[(std::size_t)g_wiki.oreState];

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Every way to %s that you know:",
                    OreStateName((OreState)g_wiki.oreState));
        ImGui::Spacing();

        if (ImGui::BeginTable("##allewege", 4, tf))
        {
            ImGui::TableSetupColumn("Way");
            ImGui::TableSetupColumn("Steps", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Purity", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            const std::size_t zeige = (liste.size() < kMaxShown) ? liste.size() : kMaxShown;
            for (std::size_t k = 0; k < zeige; ++k)
            {
                const OreWay& w = liste[k];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(wegText(w).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d", (int)w.steps.size());
                ImGui::TableNextColumn();
                ImGui::Text("%d %%", w.purity);
                ImGui::TableNextColumn();
                ImGui::Text("%d", w.price);
            }
            ImGui::EndTable();

            if (liste.size() > zeige)
                ImGui::TextDisabled("... and %d more that pay less.",
                                    (int)(liste.size() - zeige));
        }

        ImGui::Spacing();
        if (liste.size() > 1)
        {
            const int  unten     = liste.back().price;
            const int  oben      = liste.front().price;
            const bool lohntSich = oben > unten;

            if (lohntSich)
                ImGui::TextDisabled(
                    "Same state, %d instead of %d money - purity along the way is the only difference.", oben,
                    unten);
            else
                ImGui::TextDisabled(
                    "All worth the same: here the shortest way is the best one.");
        }
        else
        {
            ImGui::TextDisabled("So far you only know this one. Try a detour!");
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%d of %d states found.  ? means: never made it yourself.",
                        gefunden, (int)zeilen.size());
    ImGui::TextDisabled("Click a row: every way to get there is listed below.");
    ImGui::TextDisabled("Price for ONE piece, at your current %d money per block.",
                        world.moneyPerBlock);
    ImGui::TextDisabled("The way counts: a state is always worth the same, purity is not.");
    ImGui::TextDisabled("Smelting costs purity, cleaning adds some.");

    ImGui::EndChild();
}

}  // namespace

// ===========================================================================
// Datei einlesen
// ===========================================================================

WikiBook LoadWikiBook()
{
    WikiBook book;

    std::ifstream in;
    for (const std::string& path : Candidates())
    {
        in.open(path.c_str(), std::ios::binary);
        if (in.is_open())
        {
            book.file = path;
            break;
        }
        in.clear();
    }

    if (!in.is_open())
    {
        book.problems.push_back("data/wiki.json not found.");
        return book;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    JsonValue   wurzel;
    std::string fehler;
    if (!ParseJson(buffer.str(), wurzel, fehler))
    {
        book.problems.push_back("data/wiki.json - " + fehler);
        return book;
    }

    // ---- Kategorien -------------------------------------------------------
    const JsonValue* kats = wurzel.find("kategorien");
    if (kats == nullptr || kats->type != JsonValue::Type::Array)
    {
        book.problems.push_back("Missing the list \"kategorien\": [ ... ].");
        return book;
    }

    for (const JsonValue& e : kats->items)
    {
        if (e.type != JsonValue::Type::Object)
            continue;

        WikiCategory c;
        c.key  = e.text("schluessel", "");
        c.name = e.text("name", c.key.c_str());
        c.text = e.text("text", "");
        c.icon = e.text("zeichen", "");

        if (c.key.empty())
        {
            book.problems.push_back("A category has no \"schluessel\".");
            continue;
        }
        book.categories.push_back(c);
    }

    // ---- Seiten -----------------------------------------------------------
    const JsonValue* seiten = wurzel.find("seiten");
    if (seiten == nullptr || seiten->type != JsonValue::Type::Array)
    {
        book.problems.push_back("Missing the list \"seiten\": [ ... ].");
        return book;
    }

    for (const JsonValue& e : seiten->items)
    {
        if (e.type != JsonValue::Type::Object)
            continue;

        WikiPage page;
        page.category  = e.text("kategorie", "");
        page.title     = e.text("titel", "");
        page.shortText = e.text("kurz", "");

        page.parent = e.text("unter", "");

        // Verwandte Seiten. Ob es sie wirklich gibt, wird erst geprueft, wenn
        // alle Seiten gelesen sind - sonst wuerde die Reihenfolge in der Datei
        // ueber Erfolg oder Meldung entscheiden.
        if (const JsonValue* v = e.find("siehe_auch"))
        {
            if (v->type != JsonValue::Type::Array)
                book.problems.push_back(page.title + ": \"siehe_auch\" muss eine Liste sein.");
            else
                for (const JsonValue& t : v->items)
                    if (t.type == JsonValue::Type::String)
                        page.seeAlso.push_back(t.str);
        }

        if (page.title.empty())
        {
            book.problems.push_back("A page has no \"titel\".");
            continue;
        }

        bool bekannt = false;
        for (const WikiCategory& c : book.categories)
            if (c.key == page.category)
                bekannt = true;

        if (!bekannt)
        {
            book.problems.push_back(page.title + ": the category \"" + page.category +
                                    "\" does not exist.");
            continue;
        }

        // "braucht": derselbe Schluessel wie in data/skills.txt - eine zweite
        // Tabelle wuerde frueher oder spaeter auseinanderlaufen.
        // Ein einzelner Schluessel oder eine Liste davon.
        if (const JsonValue* b = e.find("braucht"))
        {
            auto nimm = [&](const std::string& schluessel)
            {
                Skill was = Skill::None;
                if (SkillFromKey(schluessel, was))
                    page.needs.push_back(was);
                else
                    book.problems.push_back(page.title + ": \"braucht\": \"" + schluessel +
                                            "\" is not something I know.");
            };

            if (b->type == JsonValue::Type::String)
            {
                if (!b->str.empty())
                    nimm(b->str);
            }
            else if (b->type == JsonValue::Type::Array)
            {
                for (const JsonValue& k : b->items)
                    if (k.type == JsonValue::Type::String)
                        nimm(k.str);
            }
            else
            {
                book.problems.push_back(page.title +
                                        ": \"braucht\" must be a text or a list.");
            }
        }

        const JsonValue* schritte = e.find("schritte");
        if (schritte == nullptr || schritte->type != JsonValue::Type::Array)
        {
            book.problems.push_back(page.title + ": \"schritte\" must be a list.");
            continue;
        }

        std::string laufender;  // der zuletzt gesetzte Code gilt weiter
        for (const JsonValue& s : schritte->items)
        {
            if (s.type != JsonValue::Type::Object)
                continue;

            WikiStep step;
            step.point   = s.text("punkt", "");
            step.text    = s.text("text", "");
            step.seconds = (float)s.number("dauer", 3.5);

            if (const JsonValue* c = s.find("code"))
                if (c->type == JsonValue::Type::String)
                {
                    laufender = c->str;

                    // Ein Zeilenumbruch am Ende waere eine leere Zeile im Bild.
                    while (!laufender.empty() && laufender[laufender.size() - 1] == '\n')
                        laufender.resize(laufender.size() - 1);
                }

            if (step.point.empty())
                step.point = "Schritt " + std::to_string((int)page.steps.size() + 1);

            if (laufender.empty())
            {
                book.problems.push_back(page.title + " / " + step.point +
                                        ": the first step needs \"code\".");
                continue;
            }
            step.code = laufender;

            const std::string mark = s.text("markiere", "");
            if (!mark.empty())
            {
                const int         treffer = (int)s.number("treffer", 1.0);
                const std::size_t pos     = FindHit(step.code, mark, (treffer < 1) ? 1 : treffer);

                if (pos == std::string::npos)
                {
                    // Still nichts zu tun waere das Schlimmste: dann sucht man
                    // den Fehler in der Animation statt in der Datei.
                    book.problems.push_back(page.title + " / " + step.point + ": \"" + mark +
                                            "\" does not appear in the code like that.");
                }
                else
                {
                    step.markStart = (int)pos;
                    step.markLen   = (int)mark.size();
                }
            }

            if (step.seconds < 1.2f)
                step.seconds = 1.2f;

            // "braucht" an einem Schritt: derselbe Schluessel wie bei der Seite,
            // nur muessen hier ALLE genannten Punkte gekauft sein.
            if (const JsonValue* b = s.find("braucht"))
            {
                auto nimm = [&](const std::string& schluessel)
                {
                    Skill was = Skill::None;
                    if (SkillFromKey(schluessel, was))
                        step.needs.push_back(was);
                    else
                        book.problems.push_back(page.title + " / " + step.point +
                                                ": \"braucht\": \"" + schluessel +
                                                "\" is not something I know.");
                };

                if (b->type == JsonValue::Type::String)
                {
                    if (!b->str.empty())
                        nimm(b->str);
                }
                else if (b->type == JsonValue::Type::Array)
                {
                    for (const JsonValue& k : b->items)
                        if (k.type == JsonValue::Type::String)
                            nimm(k.str);
                }
                else
                {
                    book.problems.push_back(page.title + " / " + step.point +
                                            ": \"braucht\" must be a text or a list.");
                }
            }

            page.steps.push_back(step);
        }

        if (page.steps.empty())
        {
            book.problems.push_back(page.title + ": not a single step.");
            continue;
        }

        book.pages.push_back(page);
    }

    if (book.pages.empty())
        book.problems.push_back("In \"seiten\" there is not a single page.");

    // Zeigt "unter" auf eine Seite, die es gibt?
    for (WikiPage& p : book.pages)
    {
        if (p.parent.empty())
            continue;

        bool gibtEs = false;
        for (const WikiPage& q : book.pages)
            if (q.title == p.parent)
                gibtEs = true;

        if (!gibtEs)
        {
            book.problems.push_back(p.title + ": \"unter\" points to \"" + p.parent +
                                    "\" - that page does not exist.");
            continue;
        }

        // Und laeuft die Kette nach oben auch irgendwann aus? Ein Ring ("a"
        // unter "b", "b" unter "a") wuerde die Liste endlos zeichnen lassen -
        // lieber eine Meldung und die Seite ganz oben.
        std::string       oben     = p.parent;
        const std::size_t grenze   = book.pages.size() + 1;
        std::size_t       schritte = 0;
        while (!oben.empty() && schritte++ < grenze)
        {
            std::string weiter;
            for (const WikiPage& q : book.pages)
                if (q.title == oben)
                {
                    weiter = q.parent;
                    break;
                }
            oben = weiter;  // nichts gefunden: Kette endet hier
        }

        if (!oben.empty())
        {
            book.problems.push_back(p.title + ": \"unter\" goes in a circle.");
            p.parent.clear();
        }
    }

    // Jetzt, wo alle Seiten da sind: zeigen die Verweise auch irgendwohin?
    for (const WikiPage& p : book.pages)
        for (const std::string& ziel : p.seeAlso)
        {
            bool gibtEs = false;
            for (const WikiPage& q : book.pages)
                if (q.title == ziel)
                    gibtEs = true;

            if (!gibtEs)
                book.problems.push_back(p.title + ": siehe_auch zeigt auf \"" + ziel +
                                        "\" - that page does not exist.");
        }

    return book;
}

std::string WikiOreKey(const std::string& oreName)
{
    return "erz:" + oreName;
}

int WikiUnseen(const WikiBook& book, const Limits& limits, const World& world, const OrePlan& ores)
{
    int neu = 0;
    for (const WikiPage& p : book.pages)
        if (Sichtbar(p, limits) && world.wikiSeen.find(p.title) == world.wikiSeen.end())
            ++neu;

    // Ein frisch gefundenes Erz ist auch etwas Neues im Wiki - sonst merkt man
    // erst beim Hineinschauen, dass eine Seite dazugekommen ist.
    for (int i : KnownOres(world, ores))
        if (world.wikiSeen.find(WikiOreKey(OreOf(ores, i).name)) == world.wikiSeen.end())
            ++neu;

    return neu;
}

// ===========================================================================
// Die Seite
// ===========================================================================

void DrawWikiPage(const WikiBook& book, const Limits& limits, World& world, const OrePlan& ores,
                  const CraftPlan& craft)
{
    std::set<std::string>& seen = world.wikiSeen;

    ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::V(ui::kPage));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 14.0f));

    if (ImGui::Begin("##wiki", nullptr, flags))
    {
        // ---- Was der Spieler ueberhaupt sehen darf ------------------------
        // Was im Skilltree noch nicht gekauft ist, taucht hier gar nicht auf.
        std::vector<int> sichtbar;
        for (int i = 0; i < (int)book.pages.size(); ++i)
            if (Sichtbar(book.pages[(std::size_t)i], limits))
                sichtbar.push_back(i);

        // Nach dem Zuruecksetzen kann eine Seite wieder verschwinden.
        bool nochDa = false;
        for (int i : sichtbar)
            if (i == g_wiki.page)
                nochDa = true;
        if (!nochDa)
            g_wiki.page = -1;

        const WikiCategory* kat = nullptr;
        for (const WikiCategory& c : book.categories)
            if (c.key == g_wiki.category)
                kat = &c;

        if (kat == nullptr)
        {
            g_wiki.category.clear();
            g_wiki.page = -1;
        }

        // Die Erze sind die einzige Kategorie, deren Seiten nicht in der Datei
        // stehen - sie fuehrt deshalb einen eigenen Weg durch das Zeichnen.
        const bool erzKat = (kat != nullptr && kat->key == kOreCategory);

        // ---- Brotkrume ----------------------------------------------------
        if (Crumb("Wiki", kat != nullptr))
        {
            g_wiki.category.clear();
            g_wiki.page = -1;
            g_wiki.ore  = -1;
        }
        if (kat != nullptr)
        {
            Slash();
            if (Crumb(kat->name.c_str(), erzKat ? (g_wiki.ore >= 0) : (g_wiki.page >= 0)))
            {
                g_wiki.page = -1;
                g_wiki.ore  = -1;
            }
        }
        if (erzKat)
        {
            if (g_wiki.ore >= 0)
            {
                Slash();
                Crumb(OreOf(ores, g_wiki.ore).name.c_str(), false);
            }
        }
        else if (g_wiki.page >= 0)
        {
            Slash();
            Crumb(book.pages[(std::size_t)g_wiki.page].title.c_str(), false);
        }

        ImGui::Separator();
        ImGui::Spacing();

        // ---- Meldungen zur Datei ------------------------------------------
        // Stimmt an data/wiki.json etwas nicht, muss man das sehen - sonst
        // sucht man den Fehler in der Animation statt in der Datei.
        if (!book.problems.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.55f, 0.42f, 1.0f));
            ImGui::TextUnformatted("data/wiki.json:");
            for (const std::string& p : book.problems)
                ImGui::TextUnformatted(p.c_str());
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        if (kat == nullptr)
        {
            // ---- Startseite: nur die Oberkategorien, gross und mittig -----
            ImDrawList*  dl    = ImGui::GetWindowDrawList();
            const ImVec2 area  = ImGui::GetCursorScreenPos();
            const ImVec2 avail = ImGui::GetContentRegionAvail();

            // Wie viele Seiten eine Kategorie gerade hat und wie viele davon
            // noch keiner gelesen hat. Die Erze zaehlen anders: dort ist jedes
            // gefundene Erz eine Seite.
            auto zaehle = [&](const std::string& key, int& zahl, int& neu)
            {
                zahl = 0;
                neu  = 0;

                if (key == kOreCategory)
                {
                    for (int oi : KnownOres(world, ores))
                    {
                        ++zahl;
                        if (seen.find(WikiOreKey(OreOf(ores, oi).name)) == seen.end())
                            ++neu;
                    }
                    return;
                }

                for (int si : sichtbar)
                    if (book.pages[(std::size_t)si].category == key)
                    {
                        ++zahl;
                        if (seen.find(book.pages[(std::size_t)si].title) == seen.end())
                            ++neu;
                    }
            };

            // Eine leere Kategorie gibt es gar nicht erst. "Functions" taucht
            // also auf, sobald der erste Befehl gekauft ist, die Erze mit dem
            // ersten abgebauten Brocken - eine Karte mit "0 Seiten" waere nur
            // ein Versprechen, das man noch nicht einloesen kann.
            std::vector<int> karten;
            for (int i = 0; i < (int)book.categories.size(); ++i)
            {
                int zahl = 0, neu = 0;
                zaehle(book.categories[(std::size_t)i].key, zahl, neu);
                if (zahl > 0)
                    karten.push_back(i);
            }

            const int n = (int)karten.size();

            if (n == 0)
            {
                const char* leer = "Nothing here yet. Mine a block or buy something "
                                   "in the skill tree.";
                TextAt(dl, 15.0f,
                       ImVec2(area.x + avail.x * 0.5f - TextSize(15.0f, leer).x * 0.5f,
                              area.y + avail.y * 0.35f),
                       kTextDim, leer);
            }

            if (n > 0)
            {
                const float gap   = 26.0f;
                const float cardW =
                    std::max(120.0f, std::min(300.0f, (avail.x - gap * (float)(n - 1)) /
                                                          (float)n));
                const float cardH = 200.0f;
                const float total = cardW * (float)n + gap * (float)(n - 1);
                const float x0    = area.x + (avail.x - total) * 0.5f;
                const float y0    = area.y + std::max(34.0f, (avail.y - cardH) * 0.33f);

                const char* hallo = "Pick a topic.";
                TextAt(dl, 17.0f,
                       ImVec2(area.x + avail.x * 0.5f - TextSize(17.0f, hallo).x * 0.5f,
                              y0 - 46.0f),
                       kTextDim, hallo);

                for (int slot = 0; slot < n; ++slot)
                {
                    const int           i = karten[(std::size_t)slot];
                    const WikiCategory& c = book.categories[(std::size_t)i];

                    const ImVec2 ca(x0 + (cardW + gap) * (float)slot, y0);
                    const ImVec2 cb(ca.x + cardW, ca.y + cardH);

                    ImGui::SetCursorScreenPos(ca);
                    ImGui::InvisibleButton(("##kat" + c.key).c_str(), ImVec2(cardW, cardH));

                    const bool hovered = ImGui::IsItemHovered();
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    {
                        g_wiki.category = c.key;
                        g_wiki.page     = -1;
                    }

                    // Zeichen und Name, sonst nichts. Der erklaerende Satz aus
                    // der Datei stand frueher hier mit drauf - vier Zeilen
                    // Kleingedrucktes auf jeder Karte, die man ohnehin nur
                    // anklickt, um weiterzukommen.
                    dl->AddRectFilled(ca, cb, hovered ? ui::kAccentDim : ui::kCard, 16.0f);
                    dl->AddRect(ca, cb, hovered ? ui::kAccent : ui::kBorder, 16.0f, 0,
                                hovered ? 2.0f : 1.0f);

                    const std::string zeichen =
                        c.icon.empty() ? c.name.substr(0, 1) : c.icon;

                    // Das Zeichen sitzt in einem eingelassenen Kaestchen - wie
                    // die Knoten im Skilltree, damit beides zusammengehoert.
                    const float  box = 66.0f;
                    const ImVec2 ia((ca.x + cb.x) * 0.5f - box * 0.5f, ca.y + 34.0f);
                    const ImVec2 ib(ia.x + box, ia.y + box);
                    dl->AddRectFilled(ia, ib, hovered ? ui::kCard : ui::kSunken, 14.0f);

                    const float  zs = 30.0f;
                    const ImVec2 zt = TextSize(zs, zeichen.c_str());
                    TextAt(dl, zs,
                           ImVec2((ia.x + ib.x) * 0.5f - zt.x * 0.5f,
                                  (ia.y + ib.y) * 0.5f - zt.y * 0.5f),
                           ui::kAccent, zeichen.c_str());

                    const ImVec2 ts = TextSize(24.0f, c.name.c_str());
                    TextAt(dl, 24.0f, ImVec2((ca.x + cb.x) * 0.5f - ts.x * 0.5f, ib.y + 20.0f),
                           kTextHead, c.name.c_str());

                    int zahl = 0;
                    int neu  = 0;
                    zaehle(c.key, zahl, neu);

                    char label[48];
                    std::snprintf(label, sizeof(label), "%d page%s", zahl, (zahl == 1) ? "" : "s");
                    TextAt(dl, 13.0f, ImVec2(ca.x + 18.0f, cb.y - 27.0f), kAccent, label);

                    // Was hier neu ist, faellt sofort auf - man soll nicht erst
                    // in jede Kategorie hineinschauen muessen.
                    if (neu > 0)
                    {
                        char nl[32];
                        std::snprintf(nl, sizeof(nl), "%d new", neu);
                        const ImVec2 ns = TextSize(13.0f, nl);
                        const ImVec2 np(cb.x - 18.0f - ns.x, cb.y - 27.0f);

                        dl->AddRectFilled(ImVec2(np.x - 8.0f, np.y - 3.0f),
                                          ImVec2(np.x + ns.x + 8.0f, np.y + ns.y + 3.0f),
                                          IM_COL32(196, 96, 40, 255), 6.0f);
                        TextAt(dl, 13.0f, np, IM_COL32(255, 240, 226, 255), nl);
                    }
                }
            }
        }
        else if (erzKat)
        {
            // ---- Die Erze: keine Seiten aus der Datei, sondern Erspieltes --
            DrawOreCollection(world, ores, craft);
        }
        else
        {
            // ---- In einer Kategorie: Liste links, Animation rechts --------
            ImGui::BeginChild("##liste", ImVec2(250.0f, 0.0f), ImGuiChildFlags_Borders);

            // Ein Eintrag in der Liste. Der Punkt dahinter heisst: noch nicht
            // gelesen.
            auto eintrag = [&](int i, bool eingerueckt)
            {
                const WikiPage& p   = book.pages[(std::size_t)i];
                const bool      neu = seen.find(p.title) == seen.end();

                if (eingerueckt)
                    ImGui::Indent(14.0f);

                if (ImGui::Selectable(p.title.c_str(), i == g_wiki.page))
                    GoTo(i, book, seen);

                if (neu)
                {
                    const ImVec2 mitte(ImGui::GetItemRectMax().x - 12.0f,
                                       (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) *
                                           0.5f);
                    ImGui::GetWindowDrawList()->AddCircleFilled(mitte, 4.5f, kAccent);
                }

                if (ImGui::IsItemHovered() && !p.shortText.empty())
                    ImGui::SetTooltip("%s", p.shortText.c_str());

                if (eingerueckt)
                    ImGui::Unindent(14.0f);
            };

            // Die Unterseiten einer Seite, in der Reihenfolge der Datei.
            auto kinderVon = [&](const std::string& titel)
            {
                std::vector<int> out;
                for (int k : sichtbar)
                    if (book.pages[(std::size_t)k].parent == titel)
                        out.push_back(k);
                return out;
            };

            // Liegt IRGENDWO darunter etwas Ungelesenes? Muss ueber alle Ebenen
            // gehen: sonst bliebe ein neues item.wash() unbemerkt, solange
            // "item" und "Process" beide zugeklappt sind.
            auto neuDarunter = [&](auto&& self, int i) -> bool
            {
                if (seen.find(book.pages[(std::size_t)i].title) == seen.end())
                    return true;
                for (int k : kinderVon(book.pages[(std::size_t)i].title))
                    if (self(self, k))
                        return true;
                return false;
            };

            // Ein Punkt der Liste. Wer Unterseiten hat, wird zum Oberpunkt zum
            // Auf- und Zuklappen - acht Verarbeitungs-Befehle einzeln
            // untereinander waeren sonst die halbe Liste. Das geht ueber
            // beliebig viele Ebenen: "item" > "Process" > "item.wash()".
            auto zeichne = [&](auto&& self, int i, bool eingerueckt) -> void
            {
                const WikiPage&        p      = book.pages[(std::size_t)i];
                const std::vector<int> kinder = kinderVon(p.title);

                if (kinder.empty())
                {
                    eintrag(i, eingerueckt);
                    return;
                }

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                           ImGuiTreeNodeFlags_OpenOnArrow |
                                           ImGuiTreeNodeFlags_OpenOnDoubleClick;
                if (i == g_wiki.page)
                    flags |= ImGuiTreeNodeFlags_Selected;

                const bool offen = ImGui::TreeNodeEx(p.title.c_str(), flags);

                // Klick auf den Namen schlaegt die Uebersicht auf, Klick auf
                // das Dreieck klappt nur zu - deshalb OpenOnArrow.
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                    GoTo(i, book, seen);

                // Zugeklappt zeigt der Punkt, dass DARUNTER etwas Neues liegt.
                bool neuDrin = seen.find(p.title) == seen.end();
                if (!offen)
                    for (int k : kinder)
                        if (neuDarunter(neuDarunter, k))
                            neuDrin = true;

                if (neuDrin)
                {
                    const ImVec2 mitte(ImGui::GetItemRectMax().x - 12.0f,
                                       (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) *
                                           0.5f);
                    ImGui::GetWindowDrawList()->AddCircleFilled(mitte, 4.5f, kAccent);
                }

                if (offen)
                {
                    // TreeNodeEx rueckt schon selbst ein - die 14 Punkte
                    // obendrauf gibt es nur fuer die Blaetter.
                    for (int k : kinder)
                        self(self, k, true);
                    ImGui::TreePop();
                }
            };

            for (int i : sichtbar)
            {
                const WikiPage& p = book.pages[(std::size_t)i];
                if (p.category != g_wiki.category || !p.parent.empty())
                    continue;

                zeichne(zeichne, i, false);
            }
            ImGui::EndChild();

            // Rechts eine schmale Spalte mit Verweisen - aber nur, wenn die
            // offene Seite ueberhaupt welche hat, die man auch schon sehen darf.
            std::vector<int> verwandt;
            if (g_wiki.page >= 0)
                for (const std::string& ziel : book.pages[(std::size_t)g_wiki.page].seeAlso)
                    for (int i : sichtbar)
                        if (book.pages[(std::size_t)i].title == ziel && i != g_wiki.page)
                            verwandt.push_back(i);

            // Die Spalte ist so breit wie ihr laengster Titel. Fest waere
            // falsch: "Zustaende und Reinheit" wuerde abgeschnitten, und ein
            // abgeschnittener Verweis sagt einem nicht, wohin er fuehrt.
            float spalte = 0.0f;
            if (!verwandt.empty())
            {
                float breit = ImGui::CalcTextSize("Related").x;
                for (int i : verwandt)
                    breit = std::max(breit,
                                     ImGui::CalcTextSize(book.pages[(std::size_t)i].title.c_str()).x);

                // Rand, Innenabstand und Platz fuer einen Rollbalken.
                spalte = Clamp(breit + 46.0f, 170.0f, 340.0f);
            }

            ImGui::SameLine();
            ImGui::BeginChild("##haupt", ImVec2(-spalte, 0.0f), ImGuiChildFlags_None);

            ImDrawList* dl = ImGui::GetWindowDrawList();

            if (g_wiki.page < 0)
            {
                // Noch nichts gewaehlt: ein kurzer Text zur Kategorie.
                ImGui::Dummy(ImVec2(0.0f, 22.0f));

                const ImVec2 p    = ImGui::GetCursorScreenPos();
                const float  wrap = std::max(240.0f, ImGui::GetContentRegionAvail().x - 40.0f);

                TextAt(dl, 26.0f, p, kTextHead, kat->name.c_str());
                TextAt(dl, 16.0f, ImVec2(p.x, p.y + 42.0f), kTextDim, kat->text.c_str(), wrap);

                ImGui::Dummy(ImVec2(0.0f, 130.0f));
                ImGui::TextDisabled("Pick one on the left.");
            }
            else
            {
                // Nur die Schritte, die der Spieler auch benutzen darf. Ein
                // Schritt, der einen noch nicht gekauften Befehl zeigt, faellt
                // heraus - samt seiner Marke auf der Zeitleiste. Die Kopie ist
                // billig (eine Handvoll kurzer Texte) und spart es, jede Stelle
                // weiter unten auf eine Umrechnungstabelle umzubauen.
                WikiPage page = book.pages[(std::size_t)g_wiki.page];
                {
                    const WikiPage& roh = book.pages[(std::size_t)g_wiki.page];

                    page.steps.clear();
                    for (const WikiStep& s : roh.steps)
                        if (SchrittSichtbar(s, limits))
                            page.steps.push_back(s);

                    // Etwas muss dastehen: eine Seite ohne einen einzigen
                    // erlaubten Schritt waere ein leerer Rahmen.
                    if (page.steps.empty())
                        page.steps.push_back(roh.steps[0]);
                }

                if (g_wiki.step >= (int)page.steps.size())
                    g_wiki.step = 0;

                // ---- Kopf ------------------------------------------------
                TextAt(dl, 24.0f, ImGui::GetCursorScreenPos(), kTextHead, page.title.c_str());
                ImGui::Dummy(ImVec2(0.0f, 30.0f));

                if (!page.shortText.empty())
                {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextDisabled("%s", page.shortText.c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::Spacing();

                // ---- Tasten -----------------------------------------------
                // Nur, wenn gerade niemand tippt - sonst haette man in einer
                // Konsole kein Leerzeichen mehr.
                if (!ImGui::GetIO().WantTextInput)
                {
                    const int letzter = (int)page.steps.size() - 1;

                    if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
                        g_wiki.playing = !g_wiki.playing;

                    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
                    {
                        g_wiki.step = (g_wiki.step < letzter) ? g_wiki.step + 1 : 0;
                        g_wiki.time = 0.0f;
                    }

                    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
                    {
                        // Erst an den Anfang des Schrittes, beim zweiten Druck
                        // eins zurueck - so wie bei einem Musikspieler.
                        if (g_wiki.time > 0.35f)
                            g_wiki.time = 0.0f;
                        else
                        {
                            g_wiki.step = (g_wiki.step > 0) ? g_wiki.step - 1 : letzter;
                            g_wiki.time = 0.0f;
                        }
                    }

                    if (ImGui::IsKeyPressed(ImGuiKey_R, false))
                    {
                        g_wiki.step = 0;
                        g_wiki.time = 0.0f;
                    }
                }

                // ---- Zeit laeuft ------------------------------------------
                const float dur = std::max(1.2f, page.steps[(std::size_t)g_wiki.step].seconds);

                if (g_wiki.playing)
                    g_wiki.time += ImGui::GetIO().DeltaTime;

                if (g_wiki.time >= dur)
                {
                    // Am Ende laeuft die Animation wieder von vorn.
                    g_wiki.time = 0.0f;
                    g_wiki.step = (g_wiki.step + 1) % (int)page.steps.size();
                }

                // ---- Buehne -----------------------------------------------
                const ImVec2 c0    = ImGui::GetCursorScreenPos();
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                const float  canW  = std::max(avail.x, 120.0f);
                const float  canH  = std::max(200.0f, avail.y - 104.0f);

                ImGui::InvisibleButton("##buehne", ImVec2(canW, canH));

                const ImVec2 c1(c0.x + canW, c0.y + canH);
                dl->PushClipRect(c0, c1, true);
                DrawAnimation(page, g_wiki.step, g_wiki.time, c0, c1);
                dl->PopClipRect();

                ImGui::Spacing();
                DrawTimeline(page, canW);
            }

            ImGui::EndChild();

            if (!verwandt.empty())
            {
                ImGui::SameLine();
                ImGui::BeginChild("##verwandt", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

                ImGui::TextDisabled("Related");
                ImGui::Spacing();

                int springe = -1;
                for (int i : verwandt)
                {
                    const WikiPage& z = book.pages[(std::size_t)i];

                    // Klein und unaufdringlich: das ist ein Verweis, keine
                    // zweite Hauptsache.
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.78f, 0.98f, 1.0f));
                    if (ImGui::Selectable(z.title.c_str()))
                        springe = i;
                    ImGui::PopStyleColor();

                    if (ImGui::IsItemHovered() && !z.shortText.empty())
                        ImGui::SetTooltip("%s", z.shortText.c_str());
                }

                // Der Sprung darf auch in eine andere Kategorie gehen.
                if (springe >= 0)
                {
                    g_wiki.category = book.pages[(std::size_t)springe].category;
                    GoTo(springe, book, seen);
                }

                ImGui::EndChild();
            }
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}
