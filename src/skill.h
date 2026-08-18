#pragma once

// Alles, was man kaufen kann. Der Witz dabei: freigeschaltet wird nicht eine
// Zahl, sondern C++ selbst.
//
// Steht in einer eigenen Datei, weil sowohl der Baum als auch die Datei
// data/skills.txt davon wissen muessen.
enum class Skill
{
    None,  // "nichts" - fuer "braucht nichts"
    Root,  // Startknoten: die erste Konsole. Gehoert einem schon.

    // Sprache freischalten. Jede dieser Faehigkeiten gibt es GENAU EINMAL im
    // Baum, und sie bringt genau eine Verwendung mit. Mehr gibt es nur ueber
    // die Erweiterungen weiter unten.
    Mine,  // block.mine() - abbauen per Programm statt per Klick
    Care,  // block.ore()/block.is(...) und block.mine(Cool) - Bloecke behandeln
    Sell,  // item.sell() - die Tasche zu Geld machen
    While,
    If,
    Else,
    For,
    Print,
    Check,  // block.isThere() / isLoading() / exists() / isGone()
    Bag,    // item.has(...) - in die Tasche schauen
    Shared,
    Variable,  // int, float, bool, auto, ...
    Class,     // struct und class
    Function,  // eigene Funktionen und Methoden

    // Verarbeiten. Ein Punkt je Befehl - welche Schritte damit moeglich sind,
    // steht in data/verarbeitung.json.
    Wash,    // item.wash()
    Smelt,   // item.smelt()
    Cast,    // item.cast()
    Clean,   // item.clean()
    Polish,  // item.polish()
    Harden,  // item.harden()
    Refine,  // item.refine()
    Press,   // item.press()

    // Legieren: zwei Erze werden ein neuer Stoff. item.alloy() und
    // item.canAlloy() - welche Rezepte es gibt, steht in
    // data/legierungen.json.
    Alloy,

    // Mehr davon. Diese Punkte tauchen im Baum erst auf, wenn das Passende
    // schon gekauft ist - eine zweite Bedingung ohne "if" waere sinnlos.
    ExtraLoop,
    ExtraIf,
    ExtraConsole,
    ExtraVariable,
    ExtraClass,
    ExtraFunction,

    // Werte verbessern (beliebig oft)
    Speed,
    MoneyPerBlock,
    FasterRespawn,

    // ---- Ab hier: alles Neue haengt HINTEN an ---------------------------
    //
    // Im Spielstand steht die Zahl hinter dem Namen (save.cpp schreibt
    // (int)n.skill). Wer hier in der Mitte etwas einfuegt, verschiebt jeden
    // alten Spielstand um einen Punkt. Also immer nur anhaengen.

    // Sprache, die es vorher umsonst gab. Ohne diese Punkte kam man an "if"
    // und "while" vorbei - ein switch ist eine Verzweigung, ein goto eine
    // Schleife. Jetzt kosten sie, und dafuer koennen sie mehr.
    Switch,     // switch/case/default - EINE Zeile statt einer je else-if
    Ternary,    // a ? b : c - eine Bedingung ohne eigene Zeile
    Goto,       // goto und Marken - die billige Schleife
    Recursion,  // eine Funktion, die sich selbst aufruft
    Container,  // vector, array, map, set - viele Werte in einer Variablen

    // Der Compiler als Ausbaustufe. Das Spiel uebersetzt wirklich mit
    // cl.exe/g++ - also kann man auch an der Uebersetzung drehen.
    Inline,    // Zeilen INNERHALB eigener Funktionen kosten nur die Haelfte
    Optimize,  // Tempo mal einem Faktor, nicht plus einer Zahl

    // Fragen an die Welt. Vorher gab es die Antworten im Spiel schon, sie
    // kamen nur nicht bis zum Spielercode durch.
    Info,     // info(erz) - Wert, Seltenheit und Behandlung als Zeiger
    Assay,    // assay(erz) - ein unbekanntes Erz untersuchen
    Count,    // item.count(erz) und item.purity(erz)
    JobQuery, // job.busy(), job.progress(), job.free()
    Wait,     // wait(sekunden) und block.loading()
    Status,   // money(), round.left(), round.target()
    Market,   // market.price(erz) - der Preis schwankt

    // Mehr Werkstatt: der eine Auftragsplatz war die haerteste Grenze im
    // ganzen Spiel.
    ExtraFurnace,

    // Das Ende der Verarbeitungskette. Frueher war bei "veredelt" Schluss,
    // und danach gab es nur noch neue Erze - die sich alle gleich anfuehlten,
    // weil sie denselben Weg gingen. Diese beiden gehen tiefer statt breiter.
    Etch,
    Fuse,

    // Auftraege. Kommt mit Absicht sehr spaet: bis dahin ist das Rundenziel
    // die einzige Struktur, und das ist etwas, das man ABWEHRT. Ein Auftrag
    // ist das erste, das man WILL - und der hat erst dann seinen Platz, wenn
    // Abbauen und Verarbeiten laengst sitzen und die Wiederholung anfaengt,
    // duenn zu werden.
    Quests,

    // Die Marktseite: der Kurs aller Erze als Bild. Bis hierher war der Preis
    // eine Zahl, die man im Programm abfragen konnte - jetzt sieht man ihn
    // laufen, und "halten oder verkaufen" wird zu einer Entscheidung, die man
    // trifft statt sie zu erraten. Braucht market.price().
    Chart,
};

const char* SkillName(Skill skill);
const char* SkillInfo(Skill skill);
const char* SkillTag(Skill skill);  // 1-3 Zeichen fuer die Anzeige im Knoten
