#pragma once

#include "skill.h"

#include <string>
#include <vector>

// Der Skilltree steht nicht im Programm, sondern in data/skills.txt. Diese
// Datei liest sie ein.
//
// Warum: wann was auftauchen darf, ist eine Frage des Spielgefuehls und nicht
// des Programms. Das will man aendern und sofort ausprobieren koennen, ohne
// neu zu bauen.

// Eine Zeile aus der Datei.
struct SkillRule
{
    Skill skill   = Skill::Speed;
    int   minStep = 1;  // Schritt = der wievielte Kauf das fruehestens ist
    int   maxStep = 1;
    int   price   = 10;  // Grundpreis, waechst mit dem Schritt

    // einmal = darf nur ein einziges Mal im ganzen Baum auftauchen.
    // Sonst darf der Punkt beliebig oft kommen.
    bool once   = false;
    int  weight = 1;  // wie oft er im Vergleich zu anderen gezogen wird

    // Wie sich das Gewicht pro Schritt nach aussen aendert. Ueber 1 heisst:
    // weiter draussen kommt der Punkt oefter. Damit werden die "+1"-Punkte
    // spaeter zum Normalfall, waehrend neue Befehle seltener werden.
    float weightGrowth = 1.0f;

    // Taucht erst auf, wenn das hier schon GEKAUFT ist. Skill::None = immer.
    Skill needs = Skill::None;
};

struct SkillPlan
{
    int steps   = 12;  // so viele Schritte hat der Baum
    int minKids = 1;   // so viele neue Punkte macht ein Kauf auf
    int maxKids = 3;

    // So viele Punkte duerfen hoechstens gleichzeitig offenstehen. Danach
    // macht ein Kauf nur noch einen neuen Punkt auf - sonst waere die Seite
    // irgendwann mit 50 Karten zugestellt.
    int maxOpen = 10;

    // Je weiter weg, desto teurer: jeder Schritt kostet das growth-fache.
    // Dadurch macht man erst die nahen Punkte und dann die weiter draussen.
    float growth = 1.5f;

    // Was die stapelbaren Punkte bringen. Steht hier, weil das reine Balance
    // ist - im Programm hat so eine Zahl nichts verloren.
    float speedStart   = 3.0f;   // Zeilen pro Sekunde ganz am Anfang
    float speedPlus    = 0.25f;  // je "tempo" dazu
    int   moneyPlus    = 1;      // je "geld" dazu
    float respawnStart = 0.6f;   // Sekunden, bis der Block nachwaechst
    float respawnMul   = 0.94f;  // je "nachwachsen" mal so viel

    // So viele Schritte Abstand halten zwei einmalige Punkte mindestens.
    // Ohne das schaltet man drei Sprachsachen direkt hintereinander frei.
    int spacing = 2;

    std::vector<SkillRule> rules;

    // Was an der Datei nicht gestimmt hat. Wird im Spiel angezeigt - eine
    // stumm verschluckte Zeile waere das Schlimmste.
    std::vector<std::string> problems;

    std::string file;  // welche Datei genommen wurde, leer = keine gefunden
};

// Sucht data/skills.txt an den ueblichen Stellen und liest sie ein.
SkillPlan LoadSkillPlan();

// Schluessel aus der Datei ("pruefen", "while", ...) zu einem Skill. false =
// kenne ich nicht. Steht hier, weil auch data/wiki.json dieselben Schluessel
// benutzt - zwei Tabellen wuerden frueher oder spaeter auseinanderlaufen.
bool SkillFromKey(const std::string& key, Skill& out);
