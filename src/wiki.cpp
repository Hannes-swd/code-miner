#include "wiki.h"

#include "json.h"
#include "skillfile.h"
#include "skilltree.h"

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
const ImU32 kPanelTop = IM_COL32(44, 48, 57, 255);
const ImU32 kPanelBot = IM_COL32(30, 33, 40, 255);
const ImU32 kRing     = IM_COL32(62, 68, 80, 255);
const ImU32 kAccent   = IM_COL32(118, 198, 255, 255);
const ImU32 kAccentBg = IM_COL32(28, 58, 82, 255);
const ImU32 kFresh    = IM_COL32(178, 226, 122, 255);
const ImU32 kTextHead = IM_COL32(238, 243, 249, 255);
const ImU32 kTextBody = IM_COL32(206, 214, 226, 255);
const ImU32 kTextDim  = IM_COL32(130, 139, 154, 255);
const ImU32 kCodeText = IM_COL32(214, 222, 232, 255);

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

        GradientRect(dl, qa, qb, 10.0f, WithAlpha(IM_COL32(38, 46, 58, 255), bp),
                     WithAlpha(IM_COL32(26, 31, 40, 255), bp));
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

    if (ImGui::Button(g_wiki.playing ? "Pause" : "Abspielen", ImVec2(96.0f, 0.0f)))
        g_wiki.playing = !g_wiki.playing;

    ImGui::SameLine();
    if (ImGui::Button("Von vorn", ImVec2(96.0f, 0.0f)))
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
    ImGui::TextDisabled("Schritt %d von %d  -  noch %.0f s", g_wiki.step + 1, n, rest);

    ImGui::SameLine();
    ImGui::TextDisabled("   [Leertaste] Pause   [<- ->] Schritt   [R] von vorn");

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
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.22f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.26f, 0.34f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.46f, 0.78f, 1.00f, 1.0f));
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
        book.problems.push_back("data/wiki.json nicht gefunden.");
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
        book.problems.push_back("Es fehlt die Liste \"kategorien\": [ ... ].");
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

        if (c.key.empty())
        {
            book.problems.push_back("Eine Kategorie hat keinen \"schluessel\".");
            continue;
        }
        book.categories.push_back(c);
    }

    // ---- Seiten -----------------------------------------------------------
    const JsonValue* seiten = wurzel.find("seiten");
    if (seiten == nullptr || seiten->type != JsonValue::Type::Array)
    {
        book.problems.push_back("Es fehlt die Liste \"seiten\": [ ... ].");
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
            book.problems.push_back("Eine Seite hat keinen \"titel\".");
            continue;
        }

        bool bekannt = false;
        for (const WikiCategory& c : book.categories)
            if (c.key == page.category)
                bekannt = true;

        if (!bekannt)
        {
            book.problems.push_back(page.title + ": die Kategorie \"" + page.category +
                                    "\" gibt es nicht.");
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
                                            "\" kenne ich nicht.");
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
                                        ": \"braucht\" muss ein Text oder eine Liste sein.");
            }
        }

        const JsonValue* schritte = e.find("schritte");
        if (schritte == nullptr || schritte->type != JsonValue::Type::Array)
        {
            book.problems.push_back(page.title + ": \"schritte\" muss eine Liste sein.");
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
                                        ": der erste Schritt braucht \"code\".");
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
                                            "\" steht so nicht im Code.");
                }
                else
                {
                    step.markStart = (int)pos;
                    step.markLen   = (int)mark.size();
                }
            }

            if (step.seconds < 1.2f)
                step.seconds = 1.2f;

            page.steps.push_back(step);
        }

        if (page.steps.empty())
        {
            book.problems.push_back(page.title + ": kein einziger Schritt.");
            continue;
        }

        book.pages.push_back(page);
    }

    if (book.pages.empty())
        book.problems.push_back("In \"seiten\" steht keine einzige Seite.");

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
            book.problems.push_back(p.title + ": \"unter\" zeigt auf \"" + p.parent +
                                    "\" - die Seite gibt es nicht.");
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
            book.problems.push_back(p.title + ": \"unter\" dreht sich im Kreis.");
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
                                        "\" - die Seite gibt es nicht.");
        }

    return book;
}

int WikiUnseen(const WikiBook& book, const Limits& limits, const std::set<std::string>& seen)
{
    int neu = 0;
    for (const WikiPage& p : book.pages)
        if (Sichtbar(p, limits) && seen.find(p.title) == seen.end())
            ++neu;
    return neu;
}

// ===========================================================================
// Die Seite
// ===========================================================================

void DrawWikiPage(const WikiBook& book, const Limits& limits, std::set<std::string>& seen)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.047f, 0.058f, 1.0f));
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

        // ---- Brotkrume ----------------------------------------------------
        if (Crumb("Wiki", kat != nullptr))
        {
            g_wiki.category.clear();
            g_wiki.page = -1;
        }
        if (kat != nullptr)
        {
            Slash();
            if (Crumb(kat->name.c_str(), g_wiki.page >= 0))
                g_wiki.page = -1;
        }
        if (g_wiki.page >= 0)
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
            const int    n     = (int)book.categories.size();

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

                const char* hallo = "Wähle ein Thema.";
                TextAt(dl, 17.0f,
                       ImVec2(area.x + avail.x * 0.5f - TextSize(17.0f, hallo).x * 0.5f,
                              y0 - 46.0f),
                       kTextDim, hallo);

                for (int i = 0; i < n; ++i)
                {
                    const WikiCategory& c = book.categories[(std::size_t)i];

                    const ImVec2 ca(x0 + (cardW + gap) * (float)i, y0);
                    const ImVec2 cb(ca.x + cardW, ca.y + cardH);

                    ImGui::SetCursorScreenPos(ca);
                    ImGui::InvisibleButton(("##kat" + c.key).c_str(), ImVec2(cardW, cardH));

                    const bool hovered = ImGui::IsItemHovered();
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    {
                        g_wiki.category = c.key;
                        g_wiki.page     = -1;
                    }

                    GradientRect(dl, ca, cb, 16.0f,
                                 hovered ? IM_COL32(56, 63, 76, 255) : kPanelTop, kPanelBot);
                    dl->AddRect(ca, cb, hovered ? kAccent : kRing, 16.0f, 0,
                                hovered ? 2.4f : 1.6f);

                    const ImVec2 ts = TextSize(26.0f, c.name.c_str());
                    TextAt(dl, 26.0f, ImVec2((ca.x + cb.x) * 0.5f - ts.x * 0.5f, ca.y + 36.0f),
                           kTextHead, c.name.c_str());

                    TextAt(dl, 14.5f, ImVec2(ca.x + 18.0f, ca.y + 88.0f), kTextDim, c.text.c_str(),
                           cardW - 36.0f);

                    int zahl = 0;
                    int neu  = 0;
                    for (int si : sichtbar)
                        if (book.pages[(std::size_t)si].category == c.key)
                        {
                            ++zahl;
                            if (seen.find(book.pages[(std::size_t)si].title) == seen.end())
                                ++neu;
                        }

                    char label[48];
                    std::snprintf(label, sizeof(label), "%d Seite%s", zahl, (zahl == 1) ? "" : "n");
                    TextAt(dl, 13.0f, ImVec2(ca.x + 18.0f, cb.y - 27.0f), kAccent, label);

                    // Was hier neu ist, faellt sofort auf - man soll nicht erst
                    // in jede Kategorie hineinschauen muessen.
                    if (neu > 0)
                    {
                        char nl[32];
                        std::snprintf(nl, sizeof(nl), "%d neu", neu);
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
            // "item" und "Verarbeiten" beide zugeklappt sind.
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
            // beliebig viele Ebenen: "item" > "Verarbeiten" > "item.wash()".
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
                float breit = ImGui::CalcTextSize("Passt dazu").x;
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
                ImGui::TextDisabled("Links auswählen.");
            }
            else
            {
                const WikiPage& page = book.pages[(std::size_t)g_wiki.page];

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

                ImGui::TextDisabled("Passt dazu");
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
