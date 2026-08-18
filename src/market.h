#pragma once

// Der Markt: was ein Erz gerade wert ist, und wie es dahin gekommen ist.
//
// Der Preis steht an EINER Stelle - hier. market.price() im Spielercode und die
// Marktseite rechnen dieselbe Zahl, sonst behauptet das Bild etwas anderes als
// das Programm, und man sucht den Fehler bei sich.

struct World;
struct OrePlan;
struct CraftPlan;
struct Limits;

// Was ein Stueck dieses Erzes kostet: EIN rohes Stueck bei voller Reinheit.
// Verarbeitet ist es mehr wert, aber der Ausschlag ist derselbe - so lassen
// sich zwei Erze ueberhaupt vergleichen.
int MarketPriceAt(const OrePlan& ores, const CraftPlan& craft, const World& world, int ore,
                  float zeit);

// Derselbe Preis jetzt, und derselbe ohne jeden Ausschlag (der Grundwert).
int MarketPriceNow(const OrePlan& ores, const CraftPlan& craft, const World& world, int ore);
int MarketBase(const OrePlan& ores, const CraftPlan& craft, const World& world, int ore);

// Die Marktseite: oben der Kurs des ausgewaehlten Erzes, darunter alle, die man
// schon einmal in der Tasche hatte - mit Grundwert, Preis, Ausschlag und einer
// kleinen Kurve je Zeile.
//
// Gibt es nur mit dem Punkt "chart" im Baum; ohne ihn zeichnet main den Reiter
// gar nicht erst.
void DrawMarketPage(World& world, const OrePlan& ores, const CraftPlan& craft,
                    const Limits& limits);
