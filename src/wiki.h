#pragma once

#include "skill.h"

#include <set>
#include <string>
#include <vector>

struct Limits;
struct World;
struct OrePlan;
struct CraftPlan;

// Das Wiki. Was darin steht, kommt aus data/wiki.json - nicht aus dem
// Programm. So kann man Seiten dazuschreiben, ohne neu zu bauen.
//
// Eine Seite ist eine kleine Animation: ein Stueck Code, dazu laufen
// nacheinander Erklaerungen ein. Ein Schritt ist ein Bild dieser Animation.

struct WikiStep
{
    std::string point;  // Beschriftung auf der Zeitleiste

    // Der volle Code dieses Schrittes. In der Datei darf er fehlen - dann gilt
    // der vom Schritt davor weiter. Beim Einlesen wird er hier ausgeschrieben,
    // damit das Zeichnen nicht mehr rueckwaerts suchen muss.
    std::string code;

    std::string text;  // was in der Sprechblase steht

    // Welches Stueck vom Code eingerahmt wird, schon als Bereich. -1 = nichts
    // markieren (dann steht die Blase unter dem Code).
    int markStart = -1;
    int markLen   = 0;

    float seconds = 3.5f;
};

struct WikiPage
{
    std::string category;   // Schluessel der Kategorie
    std::string title;
    std::string shortText;  // eine Zeile darunter

    // Ohne einen dieser Punkte im Skilltree taucht die Seite gar nicht auf.
    // Leer = immer sichtbar. Mehrere heisst: EINER davon reicht - die
    // Uebersicht ueber das Verarbeiten soll ja auftauchen, sobald man
    // irgendeinen der acht Schritte gekauft hat.
    std::vector<Skill> needs;

    std::vector<WikiStep> steps;

    // Haengt diese Seite unter einer anderen? Dann steht sie in der Liste
    // links eingerueckt darunter, und der Oberpunkt laesst sich zuklappen.
    // Leer = eigener Punkt ganz oben.
    std::string parent;

    // Verwandte Seiten, die rechts als kleine Verweise stehen. In der Datei
    // sind das die Titel der anderen Seiten.
    std::vector<std::string> seeAlso;
};

struct WikiCategory
{
    std::string key;
    std::string name;
    std::string text;  // kurzer Text, solange nichts gewaehlt ist
};

struct WikiBook
{
    std::vector<WikiCategory> categories;
    std::vector<WikiPage>     pages;

    // Was an der Datei nicht gestimmt hat. Wird rot angezeigt - eine stumm
    // verschluckte Seite waere das Schlimmste.
    std::vector<std::string> problems;
    std::string              file;
};

// Sucht data/wiki.json an den ueblichen Stellen und liest sie ein.
WikiBook LoadWikiBook();

// Unter welchem Namen ein Erz in world.wikiSeen steht. Der Doppelpunkt haelt
// es von den Seitentiteln aus der Datei auseinander.
std::string WikiOreKey(const std::string& oreName);

// Welche Seiten sind neu freigeschaltet, aber noch nicht aufgeschlagen?
// Gemerkt wird das ueber den Titel - Nummern wuerden sich verschieben, sobald
// jemand eine Seite in die Datei einfuegt. Die frisch gefundenen Erze zaehlen
// mit: sie sind auch Seiten, nur eben erspielte.
int WikiUnseen(const WikiBook& book, const Limits& limits, const World& world,
               const OrePlan& ores);

// Die Wiki-Seite. Wird STATT der Welt gezeigt, nicht daneben.
//
// limits entscheidet, welche Eintraege ueberhaupt in der Liste stehen: was im
// Skilltree noch nicht gekauft ist, gibt es hier auch noch nicht.
//
// world ist nicht const, weil das Aufschlagen einer Seite in world.wikiSeen
// landet. Erz- und Verarbeitungsplan braucht die Kategorie "Erze": die steht
// nicht in der Datei, sondern entsteht aus dem, was der Spieler gefunden hat.
void DrawWikiPage(const WikiBook& book, const Limits& limits, World& world, const OrePlan& ores,
                  const CraftPlan& craft);
