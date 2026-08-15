#pragma once

#include "skill.h"
#include "skillfile.h"

#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

struct World;

struct SkillNode
{
    int   id     = 0;
    Skill skill  = Skill::Speed;
    int   depth  = 0;  // Schritt: der wievielte Kauf das fruehestens ist
    int   parent = -1;
    int   cost   = 0;
    bool  owned  = false;

    // Lage auf einem Raster, in ganzen Zellen. Die Wurzel liegt auf (0,0).
    // Ein Knoten sitzt immer waagerecht oder senkrecht neben seinem Elternteil,
    // dadurch laufen alle Verbindungen als gerade Linien - nichts kreuzt sich.
    int gx = 0;
    int gy = 0;
};

// Was der Baum an Rechten hergibt. Wird bei jedem Bild neu ausgerechnet -
// dadurch stimmt es immer, egal in welcher Reihenfolge gekauft wurde.
//
// Jede Freischaltung bringt GENAU EINE Verwendung mit: wer "if" kauft, darf
// eine Bedingung schreiben. Mehr gibt es nur ueber "+1 Bedingung".
struct Limits
{
    bool allowWhile    = false;
    bool allowFor      = false;
    bool allowIf       = false;
    bool allowElse     = false;
    bool allowPrint    = false;
    bool allowCheck    = false;
    bool allowShared   = false;
    bool allowMine     = false;

    // block.ore()/block.is(...) und block.mine(Cool): teure Erze wollen behandelt
    // werden, und ohne diesen Punkt kann man sie nur langsam abbauen.
    bool allowCare     = false;
    bool allowSell     = false;
    bool allowBag      = false;
    bool allowVariable = false;
    bool allowClass    = false;
    bool allowFunction = false;

    // Verarbeiten: ein Schalter je Befehl.
    bool allowWash   = false;
    bool allowSmelt  = false;
    bool allowCast   = false;
    bool allowClean  = false;
    bool allowPolish = false;
    bool allowHarden = false;
    bool allowRefine = false;
    bool allowPress  = false;

    // Das Ende der Kette - siehe data/verarbeitung.json.
    bool allowEtch = false;
    bool allowFuse = false;

    // Legieren: item.alloy(...) und item.canAlloy(...).
    bool allowAlloy = false;

    // Sprache, die es frueher umsonst gab. Ohne diese Schalter kommt man
    // durch codecheck.cpp nicht mehr durch - vorher war ein switch eine
    // Gratis-Verzweigung und ein goto eine Gratis-Schleife.
    bool allowSwitch    = false;
    bool allowTernary   = false;
    bool allowGoto      = false;
    bool allowRecursion = false;
    bool allowContainer = false;

    // Der Compiler. inline macht Zeilen in eigenen Funktionen billiger,
    // optimize dreht am Tempo - und zwar MAL statt PLUS.
    bool allowInline = false;

    // Fragen an die Welt.
    bool allowInfo   = false;
    bool allowAssay  = false;
    bool allowCount  = false;
    bool allowJob    = false;
    bool allowWait   = false;
    bool allowStatus = false;
    bool allowMarket = false;

    int maxLoops     = 0;
    int maxIfs       = 0;
    int maxConsoles  = 1;
    int maxVariables = 0;
    int maxClasses   = 0;
    int maxFunctions = 0;

    float linesPerSecond = 10.0f;
    int   moneyPerBlock  = 1;
    float respawnSeconds = 0.6f;

    // Was assay() kostet und wie lange es dauert. Kommt aus data/skills.txt
    // und reist hier mit, weil der Motor den Skillplan nicht kennt - er
    // bekommt nur die Limits.
    int   assayCost    = 40;
    float assaySeconds = 3.0f;

    // Wie viele Auftraege gleichzeitig laufen duerfen. Einer gehoert einem
    // von Anfang an - jeder "furnace+" legt einen dazu.
    int maxJobs = 1;

    // Was eine Zeile innerhalb einer eigenen Funktion kostet. Mit "inline"
    // die Haelfte - dadurch rechnet sich eine Funktion ueberhaupt erst.
    float insideFunctionCost() const { return allowInline ? 0.5f : 1.0f; }
};

// Der Baum waechst mit.
//
// Am Anfang gibt es nur die Wurzel und das, was direkt dahinter liegt. Erst
// beim Kaufen wird der naechste Schritt gewuerfelt - und zwar aus dem, was
// data/skills.txt fuer diesen Schritt erlaubt UND was man schon besitzt.
// Deshalb sieht man immer genau einen Schritt weiter und nie mehr.
struct SkillTree
{
    std::vector<SkillNode> nodes;
    int                    selected = -1;

    // Was zuletzt gekauft wurde. Nur fuer die Anzeige: die Karte bekommt einen
    // deutlichen Rand, damit man nach dem Kauf sieht, WAS man da gerade
    // freigeschaltet hat - im gewachsenen Baum geht das sonst unter.
    // Gehoert nicht in den Spielstand: nach einem Neustart ist nichts mehr neu.
    int lastBought = -1;

    // Was an data/skills.txt nicht gestimmt hat. Wird auf der Seite angezeigt.
    std::vector<std::string> problems;

    // Legt die Wurzel an und wuerfelt den ersten Schritt.
    void start(const SkillPlan& plan, unsigned seed);

    bool reachable(int id) const;  // Elternknoten gekauft?

    // Was auf der Seite ueberhaupt zu sehen ist: das Gekaufte und genau der
    // naechste Schritt dahinter. Weiter gibt es noch gar nichts.
    bool visible(int id) const;

    bool canBuy(int id, int money) const;
    bool buy(int id, World& world);  // kauft und laesst den Baum weiterwachsen

    bool   owns(Skill skill) const;
    Limits limits() const;

    // ---- Innereien -------------------------------------------------------
    SkillPlan    plan;
    std::mt19937 rng;

    std::set<std::pair<int, int>> taken;     // belegte Rasterzellen
    std::vector<int>              branches;  // wie viele Kinder ein Knoten hat
    std::vector<bool>             usedOnce;  // welche "einmal"-Zeile schon liegt

    // Wie viele Punkte seit dem letzten einmaligen entstanden sind. Der
    // Abstand wird in Punkten gezaehlt und nicht in Schritten: der Baum
    // waechst ja nicht der Reihe nach, sondern dort, wo man gerade kauft.
    int sinceOnce = 1000;

    void grow(int id);  // legt die Kinder eines gekauften Knotens an

    // Nach dem Laden eines Spielstands: belegte Zellen und Kinderzahlen aus den
    // Knoten zurueckrechnen. Beides steht nicht in der Datei, weil es sich
    // vollstaendig aus der Lage der Knoten ergibt.
    void rebuildCells();
};
