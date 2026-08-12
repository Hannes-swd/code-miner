#pragma once

// Wegen BlockCare: was ein Block beim Abbau verlangt, gehoert zum Block und
// damit in die Welt.
#include "ore.h"

#include "round.h"

// Wegen ImVec2 in DrawOreTile - die Tasche und das Wiki zeichnen dasselbe
// Kaestchen, deshalb steht es hier und nicht in world.cpp.
#include "imgui.h"

#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

struct Ore;
struct OrePlan;
struct CraftPlan;
struct CraftStep;
struct AlloyPlan;
struct AlloyRecipe;
struct Limits;

// Die Welt. Aktuell: genau ein Block in der Mitte - aber jedes Mal ein anderer.
struct World
{
    // Geteilte Variablen. Jede Konsole laeuft in einem eigenen Prozess und hat
    // deshalb ihren eigenen Speicher - hier ist der gemeinsame Ablageort, ueber
    // den sich mehrere Konsolen etwas sagen koennen.
    std::map<std::string, int> shared;

    // Welche Wiki-Seiten man schon aufgeschlagen hat. Alles, was freigeschaltet
    // ist und hier NICHT drinsteht, wird als neu markiert.
    std::set<std::string> wikiSeen;

    // ---- Was das Wiki ueber die Erze weiss -------------------------------
    //
    // Beides waechst nur beim Spielen: ein Erz kommt dazu, sobald es zum
    // ersten Mal in der Tasche liegt, ein Schritt, sobald er einmal fertig
    // geworden ist. Im Wiki steht deshalb ausschliesslich das, was man selbst
    // herausgefunden hat - eine Sammlung und kein Nachschlagewerk.

    // Wo ein Erz angefangen hat. Abgebautes faengt roh an, eine Legierung
    // dagegen im Zustand "legiert" - und mit der Reinheit, die dabei
    // herausgekommen ist. Von hier aus rechnet das Wiki alle Wege.
    struct OreFirst
    {
        int state  = 0;  // OreState
        int purity = 0;  // Prozent
    };
    std::map<int, OreFirst> oreFirst;

    // Ein entdeckter Schritt: bei DIESEM Erz von DIESEM Zustand in jenen.
    // Die Wege im Wiki setzen sich aus lauter solchen Kanten zusammen.
    struct OreStep
    {
        int ore  = 0;
        int from = 0;
        int to   = 0;

        bool operator<(const OreStep& o) const
        {
            if (ore != o.ore)
                return ore < o.ore;
            if (from != o.from)
                return from < o.from;
            return to < o.to;
        }
    };
    std::set<OreStep> oreSteps;

    // Merkt sich, dass dieses Erz so zum ersten Mal dalag. Spaetere Funde
    // aendern nichts - der Anfang ist der Anfang.
    void noteOre(int ore, int state, int purity);

    // ---- Runden ----------------------------------------------------------
    // Die Phase gehoert hierher, weil sie in den Spielstand muss: wer mitten
    // im Lauf beendet, macht dort weiter.
    RoundPhase phase       = RoundPhase::Prepare;
    int        roundNumber = 1;
    float      roundLeft   = 0.0f;  // Restzeit in Sekunden

    // Stand beim Start der Runde - daraus wird spaeter die Differenz.
    int roundMoneyStart = 0;
    int roundMinedStart = 0;

    // Was die Abrechnung anzeigt. Steht fest, sobald die Zeit um ist.
    int roundMoneyEnd  = 0;
    int roundMined     = 0;
    int roundSoldCount = 0;
    int roundSoldMoney = 0;

    // Wurde das Ziel geschafft? Steht fest, BEVOR das Ziel abgezogen wird -
    // danach liesse es sich nicht mehr ausrechnen.
    bool roundWon  = true;
    int  roundPaid = 0;  // wie viel das Ziel gekostet hat

    // Steht die Welt gerade still? Setzt main jedes Bild neu aus Phase und
    // data/runden.json - so muss die Welt selbst nichts von Runden wissen.
    // Gehoert deshalb auch nicht in den Spielstand.
    bool frozen   = false;  // kein Abbau, kein Nachwachsen, kein Auftrag
    bool handMine = true;   // ... ausser dem Klick auf den Block, wenn erlaubt

    bool blockAlive = true;
    int  minedCount = 0;

    // Welches Erz gerade dasteht und mit welchem Muster. Der Startwert wird bei
    // jedem Nachwachsen neu gewuerfelt - deshalb sieht kein Block aus wie der
    // davor, obwohl die Farben zum Erz gehoeren.
    int      ore     = 0;
    unsigned oreSeed = 1;

    // Was DIESER Block beim Abbau verlangt. Wird beim Nachwachsen gewuerfelt,
    // je wertvoller das Erz desto oefter - siehe RollCare in ore.h. Steht im
    // Spielstand: sonst haette derselbe Block nach dem Laden auf einmal eine
    // andere Laune.
    BlockCare care = BlockCare::Plain;

    // Womit gerade abgebaut wird. Das setzt block.mine(Cool) bei jedem Aufruf
    // neu - man kann also mitten im Abbau umschwenken.
    BlockCare mineCare = BlockCare::Plain;

    // Abbau braucht Zeit. Solange der Zaehler laeuft, steht der Block noch da.
    bool  mining    = false;
    float mineTimer = 0.0f;

    // Von Hand angeklickt statt vom Programm abgebaut. Das laeuft auch dann
    // weiter, wenn kein Programm laeuft - am Anfang hat man ja noch keins.
    bool byHand = false;

    // Ein Stapel in der Tasche: dasselbe Erz im selben Zustand.
    //
    // Roher Stein und geschmolzener Stein sind zwei verschiedene Stapel -
    // deshalb gehoert der Zustand mit in den Schluessel.
    struct Item
    {
        int ore   = 0;
        int state = 0;  // OreState

        bool operator<(const Item& o) const
        {
            if (ore != o.ore)
                return ore < o.ore;
            return state < o.state;
        }
    };

    // Was in einem Stapel drin ist.
    //
    // Die Reinheit gehoert zum Inhalt und nicht zum Schluessel: sonst haette
    // man fuer jede Nachkommastelle einen eigenen Stapel. Treffen zwei Stapel
    // aufeinander, wird sie nach Anzahl gewichtet gemittelt.
    struct Stack
    {
        int count  = 0;
        int purity = 0;  // Prozent, 0..100
    };

    // Was man abgebaut, aber noch nicht verkauft hat.
    // Abbauen bringt kein Geld, erst das Verkaufen tut das.
    std::map<Item, Stack> inventory;

    // Was fuer den laufenden Auftrag aus der Tasche genommen wurde. Beim
    // Legieren sind das mehrere Sorten - und beim Abbruch muss alles zurueck,
    // genau so, wie es hineinging.
    struct Taken
    {
        Item was;
        int  count  = 0;
        int  purity = 0;
    };

    // Stuecke in die Tasche legen. Der einzige Weg dorthin - so kann die
    // Reinheit gar nicht vergessen werden.
    void addToBag(Item was, int anzahl, int reinheit);

    // Steigt mit dem Spielstand. Welche Erze es ueberhaupt geben kann, haengt
    // daran. Ausgerechnet wird es woanders (siehe main), hier steht nur das
    // Ergebnis.
    int level = 1;

    // Geld gibt es beim Verkaufen: anzahl * wert * moneyPerBlock.
    int money         = 0;
    int moneyPerBlock = 1;

    int   lastOre  = 0;     // was zuletzt in die Tasche ging (fuer die Anzeige)

    // Wie viele Punkte Reinheit der letzte Block durch eine falsche oder
    // fehlende Behandlung verloren hat. Nur fuer die Anzeige: sonst merkt man
    // gar nicht, dass gerade etwas schiefgeht - der Block kommt ja normal.
    int   lastCareLoss = 0;
    int   lastSold = 0;     // was der letzte Verkauf gebracht hat
    float sellFx   = 0.0f;  // laeuft nach einem Verkauf von 1 auf 0

    // Nachwachsen: nach dem Abbauen kommt der Block von selbst zurueck.
    float respawnSeconds = 0.6f;
    float respawnTimer   = 0.0f;  // laeuft runter, solange der Block weg ist

    float fx = 0.0f;  // Abbau-Effekt, laeuft von 1 auf 0

    std::mt19937 rng{20260808u};  // wuerfelt das naechste Erz

    // Verarbeiten braucht Zeit, und es laeuft immer nur EIN Auftrag. Was drin
    // ist, liegt solange nicht in der Tasche - verkaufen kann man es also
    // nicht, waehrend es in Arbeit ist.
    // Legieren benutzt denselben Platz: es ist derselbe Ofen. Deshalb steht
    // hier kein zweiter Satz Felder - beim Legieren ist craftItem eben schon
    // das Ergebnis.
    bool  crafting    = false;
    bool  craftByHand = false;  // aus der Tasche gestartet: laeuft ohne Programm
    Item  craftItem;            // was herauskommt (Erz und Zielzustand)
    int   craftCount  = 0;
    int   craftPurity = 0;      // Reinheit, mit der es hineinging
    int   craftTo     = 0;      // OreState, was herauskommt
    int   craftDelta  = 0;      // was der Schritt an der Reinheit macht
    float craftTimer  = 0.0f;
    float craftSeconds = 0.0f;  // Gesamtdauer: dauer mal Anzahl
    std::string craftName;      // "Waschen" - fuer die Anzeige

    // Was dafuer aus der Tasche genommen wurde.
    std::vector<Taken> craftTaken;

    // Faengt an abzubauen; false = da ist nichts (mehr). Mit welcher Behandlung
    // gearbeitet wird, sagt der Aufrufer bei JEDEM Aufruf mit - block.mine()
    // ohne Angabe ist einfach Plain.
    bool mine(BlockCare mit = BlockCare::Plain);
    bool mineByHand();  // dasselbe, aber per Mausklick - laeuft ohne Programm

    // Verkaufen. Ohne Angabe geht alles ueber die Theke.
    // Rueckgabe: wie viel Geld es gab.
    int sell(const OrePlan& ores, const CraftPlan& craft);
    int sell(const OrePlan& ores, const CraftPlan& craft, Item was, int anzahl);  // ein Stapel

    // Fuer item.sell(Stein, 3): diese Sorte in jedem Zustand, hoechstens so
    // viele. anzahl < 0 heisst alles davon.
    int sell(const OrePlan& ores, const CraftPlan& craft, const std::string& name, int anzahl);

    // Einen Auftrag starten. Rueckgabe: wie viele Stuecke wirklich in Arbeit
    // gegeben wurden, 0 = ging nicht.
    //
    // Aus der Tasche heraus ist der Stapel bekannt, aus dem Spielercode nur der
    // Name des Erzes - dann wird der erste Stapel genommen, der zum Schritt
    // passt (die Zustaende stehen der Reihe nach, roh kommt also zuerst).
    int startCraft(const OrePlan& ores, const Limits& limits, const CraftStep& step, Item was,
                   int anzahl, bool byHand);
    int startCraft(const OrePlan& ores, const CraftPlan& craft, const Limits& limits,
                   const std::string& befehl, const std::string& erz, int anzahl);

    // Legieren. Es benutzt denselben einen Auftrags-Platz wie das Verarbeiten -
    // es laeuft immer nur EINER.
    //
    // Rueckgabe: wie viele Stuecke wirklich in Arbeit gegeben wurden, 0 = ging
    // nicht (Zutaten fehlen, falscher Zustand, nicht freigeschaltet, oder es
    // laeuft schon ein Auftrag).
    int startAlloy(const OrePlan& ores, const AlloyRecipe& rezept, const Limits& limits,
                   int anzahl, bool byHand);
    int startAlloy(const OrePlan& ores, const AlloyPlan& alloys, const Limits& limits,
                   const std::string& name, int anzahl, bool byHand);

    // Wie viele Stueck koennte man daraus gerade machen? 0 = gar keins.
    // Ein laufender Auftrag zaehlt hier NICHT hinein: die Frage ist, ob das
    // Material reicht.
    int canAlloy(const OrePlan& ores, const AlloyRecipe& rezept, const Limits& limits) const;
    int canAlloy(const OrePlan& ores, const AlloyPlan& alloys, const Limits& limits,
                 const std::string& name) const;

    // Welche Stapel fuer so viele Stuecke genommen wuerden und welche Reinheit
    // dabei herauskaeme. false = das Material reicht nicht.
    //
    // Steht extra hier, damit die Tasche vorher zeigen kann, was herauskommt -
    // und zwar mit derselben Rechnung, die es danach wirklich tut.
    bool alloyPick(const AlloyRecipe& rezept, int anzahl, std::vector<Taken>& out,
                   int& reinheit) const;

    int inventoryCount() const;  // wie viele Bloecke insgesamt in der Tasche

    // Fuer item.has(Stein): wie viele davon liegen in der Tasche.
    int inventoryOf(const OrePlan& ores, const std::string& name) const;

    // Wie viele Stuecke dieses Erzes in einem der Zustaende liegen (ein Bit je
    // OreState).
    int bagCount(int ore, unsigned states) const;

    // Einen Knopf "Block zuruecksetzen" gibt es mit Absicht nicht: damit
    // koennte man von Hand schneller abbauen als jedes Programm.

    // Der Abbau gehoert zum laufenden Programm: er kommt nur voran, solange das
    // Programm laeuft. Sonst wuerde ein angehaltenes Programm weiter abbauen -
    // man druckt auf Pause und der Block geht trotzdem kaputt.
    void tickMining(float dt, const OrePlan& ores, const CraftPlan& craft);

    // Abbau abbrechen, der Block bleibt ganz. Beim Stoppen des Programms.
    void cancelMining();

    // Dasselbe fuers Verarbeiten: es laeuft nur, solange das Programm laeuft -
    // ausser der Auftrag kam aus der Tasche.
    void tickCraft(float dt);

    // Auftrag abbrechen. Was drin war, kommt unveraendert zurueck.
    void cancelCraft();

    // Nachwachsen und Effekte. Laeuft immer, auch im Skilltree - der Block
    // waechst ja auch nach, waehrend man dort etwas kauft. Beim Nachwachsen
    // wird das naechste Erz gewuerfelt, deshalb die Liste.
    void update(float dt, const OrePlan& ores);
};

// Was ein Stapel wert ist: Grundwert des Erzes * Zustandsfaktor *
// Reinheitsfaktor * Geld pro Block.
//
// Steht an EINER Stelle, damit Verkauf, Tasche und Tooltip nie etwas
// Verschiedenes behaupten.
int StackValue(const OrePlan& ores, const CraftPlan& craft, int ore, int state, int purity,
               int anzahl, int moneyPerBlock);

// Mit welcher Reinheit ein Block dieses Erzes aus dem Boden kommt.
int StartPurity(const OrePlan& ores, const CraftPlan& craft, int ore);

// Ein Erz als Kaestchen: Farben und Muster kommen aus data/erze.json, der
// Startwert des Musters aus der Erz-Nummer. Derselbe Stoff sieht damit ueberall
// gleich aus - in der Tasche wie im Wiki.
void DrawOreTile(ImDrawList* dl, ImVec2 pos, float size, const Ore& erz, int ore, int zellen);

// Das Erz zu einer Nummer. Eine Nummer, die es nicht gibt, liefert einen
// schlichten grauen Stein - so muss niemand vorher pruefen.
const Ore& OreOf(const OrePlan& ores, int index);

// Zeichnet die Welt in den Hintergrund (hinter alle Konsolen).
// Ein Klick auf den Block baut ihn ab - dafuer braucht man kein Programm.
//
// Der Rundenplan ist dabei, weil auch seine Fehler rot im Bild stehen sollen:
// sonst sucht man den Fehler im Spiel statt in der Datei.
void DrawWorld(World& world, const OrePlan& ores, const CraftPlan& craft, const AlloyPlan& alloys,
               const RoundPlan& rounds);

// Die Tasche: was man abgebaut hat. Das Rechtsklickmenue auf einer Karte
// bietet genau die Schritte an, die von hier aus moeglich sind - deshalb
// braucht die Seite die Limits.
//
// Legieren gibt es hier mit Absicht NICHT: das soll der Spieler programmieren
// ("wenn ich von beidem genug habe, misch es"), nicht von Hand klicken. Das
// Ergebnis landet dann von selbst in der Tasche.
void DrawInventory(World& world, const OrePlan& ores, const CraftPlan& craft,
                   const Limits& limits);
