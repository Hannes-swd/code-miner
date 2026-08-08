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
    Sell,  // block.sell() - die Tasche zu Geld machen
    While,
    If,
    Else,
    For,
    Print,
    Check,  // block.isThere() / isLoading() / exists() / isGone()
    Bag,    // block.has(...) - in die Tasche schauen
    Place,
    Shared,
    Variable,  // int, float, bool, auto, ...
    Class,     // struct und class
    Function,  // eigene Funktionen und Methoden

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
};

const char* SkillName(Skill skill);
const char* SkillInfo(Skill skill);
const char* SkillTag(Skill skill);  // 1-3 Zeichen fuer die Anzeige im Knoten
