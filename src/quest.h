#pragma once

#include "skill.h"

#include <string>
#include <vector>

struct World;
struct OrePlan;
struct CraftPlan;
struct RoundPlan;
struct Limits;

// Auftraege. Was es sie gibt, steht in data/quests.json - nicht im Programm.
//
// EIN AUFTRAG GILT FUER GENAU EINE RUNDE.
//
// In der Vorbereitung liegen drei Angebote da. Man nimmt eines an oder keines -
// eine Runde ohne Auftrag ist voellig in Ordnung. Bei jedem steht vorher, was
// er bringt und was er kostet, wenn man ihn nicht schafft.
//
// Verrechnet wird beim Rundenende, und zwar VOR dem Verkauf der Tasche und VOR
// dem Abzug der Miete:
//
//   - vor dem Verkauf, damit "behalt 20 Steine" ueberhaupt moeglich ist. Sonst
//     raeumt der Automatikverkauf jeden Halteauftrag ab.
//   - vor der Miete, damit eine Belohnung keine Runde rettet, die sonst
//     verloren waere. Ein Auftrag ist eine Zugabe, nicht die Hauptsache.
//
// Die Strafe geht vom Geld ab, DAS MAN GERADE HAT, und sie ist happig: 80 %
// dessen, was der Auftrag gebracht haette.
//
// Das ist Absicht. Bei 10 % lohnte sich ein Auftrag rein rechnerisch immer -
// selbst wer nur jeden fuenften schafft, stand am Ende im Plus. Damit war
// "einen nehmen oder keinen" gar keine Entscheidung, sondern eine Formalie.
// Bei 80 % muss man ihn etwa VIER VON NEUN Malen schaffen, damit er sich
// lohnt: aus +1,0 gegen -0,8 wird der Umschlagpunkt bei 0,8 / 1,8 = 44 %.
//
// Dazu kommt die zweite Gefahr, die schon vorher da war: die Strafe geht vom
// Geld ab, und kurz darauf kommt die Miete. Wer knapp ueber dem Ziel steht,
// fuer den ist ein misslungener Auftrag die Runde.

// Wie der Fortschritt gemessen wird. Mehr als diese drei braucht es nicht -
// daran haengt der ganze Katalog.
enum class QuestKind
{
    Count,  // ein Zaehler geht hoch und faellt nie zurueck
    Hold,   // eine Bedingung muss T Sekunden AM STUECK gelten
    Once    // eine Bedingung muss einmal erfuellt sein
};

// Was gemessen wird. Je Eintrag ein kleiner Fall in QuestValue() bzw.
// QuestHolds() - neue Auftragsarten kosten deshalb fast nichts.
enum class QuestMetric
{
    // ---- Count ----------------------------------------------------------
    Mined,       // Bloecke abgebaut
    MinedOre,    // Bloecke eines bestimmten Erzes
    Earned,      // Geld durch Verkaeufe (der Automatikverkauf zaehlt NICHT)
    Crafted,     // Stuecke verarbeitet
    Alloyed,     // Stuecke legiert
    Assayed,     // Erze untersucht
    CleanStreak, // Bloecke am Stueck ohne eine einzige Fehlbehandlung
    SoldAbove,   // Stuecke ueber dem Durchschnittspreis verkauft

    // ---- Hold -----------------------------------------------------------
    HoldOre,     // so viele Stueck eines Erzes in der Tasche
    BagPurity,   // Schnitt der Tasche mindestens so hoch
    NoSell,      // gar nichts verkauft
    FurnacesBusy,// kein Ofen steht still
    NoIdleBlock, // nie ein Block, an dem keiner arbeitet

    // ---- Once -----------------------------------------------------------
    DistinctOres,   // so viele verschiedene Erze gleichzeitig in der Tasche
    DistinctStates, // so viele verschiedene Zustaende gleichzeitig
    SingleSale,     // ein einzelner Verkauf mindestens so gross
    Frugal          // so viel Geld mit weniger als "grenze" Zeilen
};

// Eine Zeile aus data/quests.json.
struct QuestDef
{
    std::string id;
    std::string text;  // "Mine %n% blocks" - %n%, %erz%, %t%, %grenze%

    QuestKind   kind   = QuestKind::Count;
    QuestMetric metric = QuestMetric::Mined;

    // Wie gross die Aufgabe ist. Entweder eine feste Zahl, die je Runde
    // waechst, ODER ein Anteil am Rundenziel - fuer alles, was in Geld
    // gerechnet wird, ist Letzteres das Einzige, was mitwaechst.
    int   amount     = 0;
    float amountGrow = 1.0f;
    float amountOfTarget = 0.0f;  // > 0 = Anteil am Rundenziel statt amount

    float seconds = 0.0f;  // fuer Hold
    int   limit   = 0;     // fuer Frugal: hoechstens so viele Zeilen

    // Welches Erz. Leer oder "*" = eines auswuerfeln, aber nur aus denen, die
    // der Spieler schon kennt - sonst waere der Auftrag von vornherein nicht
    // zu schaffen.
    std::string ore;

    float reward  = 0.6f;   // Anteil am Rundenziel
    float penalty = 0.80f;  // Anteil an der Belohnung

    Skill needs    = Skill::None;  // ohne diesen Punkt wird er nie angeboten
    int   minRound = 1;
};

// Ein wirklich angebotener oder angenommener Auftrag: die Zahlen stehen fest.
struct Quest
{
    int         def = -1;  // Nummer in QuestPlan::defs, -1 = keiner
    std::string text;      // fertig ausformuliert, so wie es dasteht

    int   ore     = -1;
    int   amount  = 0;
    float seconds = 0.0f;
    int   limit   = 0;

    int reward  = 0;
    int penalty = 0;

    // Fortschritt
    int   progress = 0;      // Count: der Zaehler. Sonst nur fuer die Anzeige.
    float held     = 0.0f;   // Hold: wie lange die Bedingung schon gilt
    bool  done     = false;  // einmal geschafft bleibt geschafft

    bool valid() const { return def >= 0; }

    // 0 bis 1, fuer den Balken.
    float ratio() const;
};

struct QuestPlan
{
    std::vector<QuestDef> defs;

    // Wie viele Angebote gleichzeitig dastehen.
    int offers = 3;

    std::vector<std::string> problems;
    std::string              file;
};

// Sucht data/quests.json an den ueblichen Stellen und liest sie ein.
QuestPlan LoadQuestPlan();

// Wuerfelt neue Angebote. Genommen wird nur, was der Spieler auch kann:
// freigeschaltet, weit genug in den Runden, und bei einem Erz-Auftrag muss er
// das Erz schon einmal gesehen haben.
//
// alles = die beiden ersten Bedingungen ueberspringen. NUR zum Ausprobieren
// gedacht (siehe den Testschalter in main.cpp): so sieht man den ganzen
// Katalog, ohne erst den halben Baum kaufen zu muessen. Das Erz muss trotzdem
// bekannt sein - ein Auftrag auf ein Erz, das es in dieser Welt noch gar nicht
// gibt, waere auch zum Ausprobieren keine Hilfe.
void QuestRollOffers(World& world, const QuestPlan& plan, const OrePlan& ores,
                     const RoundPlan& rounds, const Limits& limits, bool alles = false);

// Angebot annehmen. Es laeuft ab sofort und gilt bis zum Rundenende.
void QuestAccept(World& world, int welches);

// Jedes Bild: Hold-Uhren weiterlaufen lassen und pruefen, ob es geschafft ist.
void QuestTick(World& world, const QuestPlan& plan, const OrePlan& ores, float dt);

// Beim Rundenende abrechnen. Rueckgabe: was es gebracht (positiv) oder
// gekostet (negativ) hat. Muss VOR dem Verkauf und VOR dem Abzug der Miete
// laufen - siehe oben.
int QuestSettle(World& world, const QuestPlan& plan, const OrePlan& ores);

// Die Tafel mit den drei Angeboten (Vorbereitung) bzw. der laufende Auftrag
// (Lauf). Zeichnet nichts, solange der Skilltree-Punkt fehlt.
void DrawQuestBoard(World& world, const QuestPlan& plan, const OrePlan& ores,
                    const Limits& limits);
