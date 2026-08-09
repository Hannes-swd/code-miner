#pragma once

#include "imgui.h"

// Die Farben des Spiels - an EINER Stelle.
//
// Vorher standen sie als IM_COL32(...) ueber fuenf Dateien verstreut, jede
// Seite mit ihrem eigenen Grau. Wer das Aussehen aendern wollte, musste sie
// alle finden. Jetzt steht hier die ganze Palette, und die Seiten nehmen sich,
// was sie brauchen.
//
// Das Bild: heller, warmer Hintergrund, weisse Karten mit weichem Rand, EINE
// kraeftige Farbe (Orange) fuer alles, was wichtig ist - Geld, der Startknopf,
// der Fortschritt. Alles andere haelt sich zurueck.
namespace ui
{

// ---- Flaechen -------------------------------------------------------------
constexpr ImU32 kPage    = IM_COL32(0xF6, 0xF4, 0xF0, 255);  // Seitengrund
constexpr ImU32 kCard    = IM_COL32(0xFF, 0xFF, 0xFF, 255);  // Karten
constexpr ImU32 kSunken  = IM_COL32(0xF2, 0xEF, 0xE9, 255);  // Eingelassenes: Chips, Rillen
constexpr ImU32 kBorder  = IM_COL32(0xE6, 0xE1, 0xD9, 255);  // Kartenrand
constexpr ImU32 kBorderS = IM_COL32(0xD8, 0xD2, 0xC8, 255);  // etwas kraeftiger
constexpr ImU32 kGrid    = IM_COL32(0xEE, 0xEB, 0xE5, 255);  // Raster in der Mine

// ---- Schrift --------------------------------------------------------------
constexpr ImU32 kText    = IM_COL32(0x24, 0x21, 0x1D, 255);  // normal
constexpr ImU32 kTextDim = IM_COL32(0x8A, 0x83, 0x78, 255);  // Nebensachen
constexpr ImU32 kTextWk  = IM_COL32(0xB3, 0xAC, 0xA1, 255);  // ganz blass

// ---- Die eine kraeftige Farbe ---------------------------------------------
constexpr ImU32 kAccent    = IM_COL32(0xCC, 0x5B, 0x1E, 255);  // Orange
constexpr ImU32 kAccentHot = IM_COL32(0xE0, 0x6A, 0x28, 255);  // heller, fuer Hover
constexpr ImU32 kAccentDim = IM_COL32(0xF3, 0xE4, 0xD8, 255);  // ganz blass hinterlegt
constexpr ImU32 kCoin      = IM_COL32(0xE0, 0x8A, 0x2E, 255);  // Muenze

// ---- Signale --------------------------------------------------------------
constexpr ImU32 kGood = IM_COL32(0x4A, 0x8A, 0x3C, 255);
constexpr ImU32 kBad  = IM_COL32(0xC4, 0x3D, 0x2F, 255);
constexpr ImU32 kDark = IM_COL32(0x23, 0x20, 0x1C, 255);  // der dunkle Balken

// ---- Masse ----------------------------------------------------------------
constexpr float kRound     = 10.0f;  // Karten
constexpr float kRoundS    = 6.0f;   // Chips, Balken
constexpr float kCardPad   = 16.0f;
constexpr float kBarHeight = 7.0f;

// Die rechte Spalte der Welt-Seite: oben die Mine, darunter die Zahlen. Beide
// zeichnen verschiedene Dateien - deshalb stehen die Masse hier und nicht
// zweimal irgendwo.
constexpr float kRightWidth  = 340.0f;
constexpr float kRightMargin = 20.0f;
constexpr float kMineTop     = 20.0f;
constexpr float kMineHeight  = 260.0f;

// Und die Rundenleiste ganz unten quer ueber die Seite.
constexpr float kFooterHeight = 62.0f;

inline ImVec4 V(ImU32 c)
{
    return ImGui::ColorConvertU32ToFloat4(c);
}

// Eine Karte: weisse Flaeche, weicher Rand, runde Ecken. Der Grundbaustein -
// alles auf jeder Seite sitzt in so einem Ding.
inline void Card(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 fill = kCard, ImU32 border = kBorder)
{
    dl->AddRectFilled(a, b, fill, kRound);
    dl->AddRect(a, b, border, kRound, 0, 1.0f);
}

// Ein Balken ohne Zahl: Rille, darin der gefuellte Teil.
inline void Bar(ImDrawList* dl, ImVec2 a, float breite, float hoehe, float anteil, ImU32 farbe)
{
    if (anteil < 0.0f)
        anteil = 0.0f;
    if (anteil > 1.0f)
        anteil = 1.0f;

    const float r = hoehe * 0.5f;
    dl->AddRectFilled(a, ImVec2(a.x + breite, a.y + hoehe), kSunken, r);
    if (anteil > 0.0f)
        dl->AddRectFilled(a, ImVec2(a.x + breite * anteil, a.y + hoehe), farbe, r);
}

}  // namespace ui
