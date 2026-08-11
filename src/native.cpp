#include "native.h"

#include "theme.h"

#include "alloy.h"
#include "craft.h"
#include "instrument.h"
#include "ore.h"
#include "skilltree.h"
#include "world.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Die Spiel-API, die dem Spielercode zur Verfuegung steht.
// Wird per /FI erzwungen - dadurch steht KEINE Zeile vor dem Code des
// Spielers und alle Zeilennummern bleiben unveraendert.
// ---------------------------------------------------------------------------
const char* kKlickerHeader = R"KLICKER(#pragma once
#include <string>

namespace ck {
void line(int console, int n);
void mine();
int  sell();
int  sellSome(const char* erz, int anzahl);
int  inBag(const char* erz);
int  craft(const char* schritt, const char* erz, int anzahl);
int  alloy(const char* name, int anzahl);
int  canAlloy(const char* name);
bool exists();
void out(const char* text);

int  getShared(const char* name);
void setShared(const char* name, int value);
void addShared(const char* name, int delta);
}

// Eine geteilte Variable. Sie liegt nicht in diesem Programm, sondern im Spiel -
// deshalb sehen ALLE Konsolen denselben Wert.
//
//     shared["punkte"] = 10;
//     shared["punkte"] += 1;
//     int p = shared["punkte"];
class CkSharedValue
{
public:
    explicit CkSharedValue(const std::string& name) : mName(name) {}

    operator int() const { return ck::getShared(mName.c_str()); }

    CkSharedValue& operator=(int value)
    {
        ck::setShared(mName.c_str(), value);
        return *this;
    }

    // Ohne das wuerde  shared["a"] = shared["b"];  nur den Namen kopieren.
    CkSharedValue& operator=(const CkSharedValue& other)
    {
        return *this = (int)other;
    }

    // += und -= gehen in einem Rutsch zum Spiel. Wichtig, wenn zwei Konsolen
    // gleichzeitig hochzaehlen - sonst koennte eine die andere ueberschreiben.
    CkSharedValue& operator+=(int value)
    {
        ck::addShared(mName.c_str(), value);
        return *this;
    }
    CkSharedValue& operator-=(int value)
    {
        ck::addShared(mName.c_str(), -value);
        return *this;
    }

    CkSharedValue& operator++()
    {
        ck::addShared(mName.c_str(), 1);
        return *this;
    }
    CkSharedValue& operator--()
    {
        ck::addShared(mName.c_str(), -1);
        return *this;
    }
    int operator++(int)
    {
        const int before = ck::getShared(mName.c_str());
        ck::addShared(mName.c_str(), 1);
        return before;
    }
    int operator--(int)
    {
        const int before = ck::getShared(mName.c_str());
        ck::addShared(mName.c_str(), -1);
        return before;
    }

private:
    std::string mName;
};

struct CkShared
{
    CkSharedValue operator[](const std::string& name) const { return CkSharedValue(name); }
};

static CkShared shared;

// Der Block da draussen in der Welt. Hier steht nur, was mit IHM zu tun hat:
// abbauen und nachschauen, ob er ueberhaupt da ist. Alles, was danach kommt -
// Tasche, Verarbeiten, Verkaufen - gehoert zu "item" weiter unten.
struct CkBlock
{
    void mine() const { ck::mine(); }

    // Ist der Block gerade da?
    //   true  = da, man kann ihn abbauen
    //   false = weg, er waechst gerade nach
    bool isThere() const { return ck::exists(); }
    bool exists()  const { return ck::exists(); }

    // Waechst er gerade nach? Genau das Gegenteil von isThere().
    bool isLoading() const { return !ck::exists(); }
    bool isGone()    const { return !ck::exists(); }
};

static const CkBlock block;

// Deine Tasche. Was einmal abgebaut ist, gehoert nicht mehr dem Block, sondern
// dir - deshalb steht das alles hier und nicht bei "block".
struct CkItem
{
    // Verkauft alles aus der Tasche und gibt zurueck, wie viel Geld es gab.
    int sell() const { return ck::sell(); }

    // Nur eine Sorte: item.sell("Stein") verkauft alle Steine,
    // item.sell("Stein", 3) genau drei davon.
    int sell(const std::string& erz) const { return ck::sellSome(erz.c_str(), -1); }
    int sell(const std::string& erz, int anzahl) const
    {
        return ck::sellSome(erz.c_str(), anzahl);
    }

    // In die Tasche schauen, ohne zu verkaufen:
    //     if (item.has("Stein")) ...        mindestens einer
    //     if (item.has("Stein", 10)) ...    mindestens zehn
    bool has(const std::string& erz) const { return ck::inBag(erz.c_str()) > 0; }
    bool has(const std::string& erz, int anzahl) const
    {
        return ck::inBag(erz.c_str()) >= anzahl;
    }

    // Verarbeiten. Roh verkaufen bringt wenig - verarbeitet ist ein Block
    // deutlich mehr wert.
    //
    //     item.wash("Gold");        alles, was gerade passt
    //     item.smelt("Gold", 3);    genau drei Stueck
    //
    // Rueckgabe: wie viele Stuecke wirklich in Arbeit gegeben wurden.
    // 0 heisst: ging nicht - falscher Zustand, nichts da, noch nicht
    // freigeschaltet, oder es laeuft schon ein Auftrag. Es laeuft immer nur
    // EINER, und er braucht Zeit.
    //
    // Was von wo nach wo geht, steht in data/verarbeitung.json - dort ist es
    // ein Netz und keine feste Kette.
    int wash(const std::string& erz)   const { return ck::craft("wash", erz.c_str(), -1); }
    int smelt(const std::string& erz)  const { return ck::craft("smelt", erz.c_str(), -1); }
    int cast(const std::string& erz)   const { return ck::craft("cast", erz.c_str(), -1); }
    int clean(const std::string& erz)  const { return ck::craft("clean", erz.c_str(), -1); }
    int polish(const std::string& erz) const { return ck::craft("polish", erz.c_str(), -1); }
    int harden(const std::string& erz) const { return ck::craft("harden", erz.c_str(), -1); }
    int refine(const std::string& erz) const { return ck::craft("refine", erz.c_str(), -1); }
    int press(const std::string& erz)  const { return ck::craft("press", erz.c_str(), -1); }

    int wash(const std::string& erz, int anzahl)   const { return ck::craft("wash", erz.c_str(), anzahl); }
    int smelt(const std::string& erz, int anzahl)  const { return ck::craft("smelt", erz.c_str(), anzahl); }
    int cast(const std::string& erz, int anzahl)   const { return ck::craft("cast", erz.c_str(), anzahl); }
    int clean(const std::string& erz, int anzahl)  const { return ck::craft("clean", erz.c_str(), anzahl); }
    int polish(const std::string& erz, int anzahl) const { return ck::craft("polish", erz.c_str(), anzahl); }
    int harden(const std::string& erz, int anzahl) const { return ck::craft("harden", erz.c_str(), anzahl); }
    int refine(const std::string& erz, int anzahl) const { return ck::craft("refine", erz.c_str(), anzahl); }
    int press(const std::string& erz, int anzahl)  const { return ck::craft("press", erz.c_str(), anzahl); }

    // Legieren: aus zwei verschiedenen Erzen wird EIN neuer Stoff, der mehr
    // wert ist als seine Teile - und der sich danach normal weiterverarbeiten
    // laesst.
    //
    //     item.alloy("Elektrum");       ein Stueck
    //     item.alloy("Elektrum", 3);    drei Stueck
    //
    // Rueckgabe: wie viele Stuecke wirklich in Arbeit gegeben wurden.
    // 0 heisst: ging nicht - Zutaten fehlen, falscher Zustand, noch nicht
    // freigeschaltet, oder es laeuft schon ein Auftrag. Legieren benutzt
    // denselben Platz wie das Verarbeiten: es laeuft immer nur EINER.
    //
    // Welche Rezepte es gibt und in welchem Zustand die Zutaten sein muessen,
    // steht in data/legierungen.json.
    int alloy(const std::string& stoff) const { return ck::alloy(stoff.c_str(), 1); }
    int alloy(const std::string& stoff, int anzahl) const
    {
        return ck::alloy(stoff.c_str(), anzahl);
    }

    // Wie viele Stueck koenntest du gerade davon machen? 0 = keins.
    // Damit kannst du dich VORHER entscheiden:
    //
    //     if (item.canAlloy("Elektrum")) item.alloy("Elektrum");
    //     else                           item.sell("Gold");
    int canAlloy(const std::string& stoff) const { return ck::canAlloy(stoff.c_str()); }
};

static const CkItem item;

inline void print(const char* text)        { ck::out(text); }
inline void print(const std::string& text) { ck::out(text.c_str()); }
inline void print(int value)               { ck::out(std::to_string(value).c_str()); }
inline void print(double value)            { ck::out(std::to_string(value).c_str()); }
inline void print(bool value)              { ck::out(value ? "true" : "false"); }
inline void print(const CkSharedValue& v)  { ck::out(std::to_string((int)v).c_str()); }
)KLICKER";

const char* kKlickerSource = R"KLICKER(#include <cstdio>
#include <cstdlib>

namespace ck {

static void waitForGo()
{
    char buf[32];
    if (!std::fgets(buf, sizeof(buf), stdin))
        std::exit(0);
}

void line(int console, int n)
{
    std::printf("L %d %d\n", console, n);
    std::fflush(stdout);
    waitForGo();
}

void mine()
{
    std::printf("M\n");
    std::fflush(stdout);
}

// Verkaufen. Das Spiel antwortet mit dem Geld - deshalb wird hier gewartet.
int sell()
{
    std::printf("K\n");
    std::fflush(stdout);
    char buf[64];
    if (!std::fgets(buf, sizeof(buf), stdin))
        std::exit(0);
    return std::atoi(buf);
}

// Verkaufen, aber nur eine Sorte. anzahl < 0 heisst: alles davon.
int sellSome(const char* erz, int anzahl)
{
    std::printf("K %d %s\n", anzahl, erz ? erz : "");
    std::fflush(stdout);
    char buf[64];
    if (!std::fgets(buf, sizeof(buf), stdin))
        std::exit(0);
    return std::atoi(buf);
}

// Verarbeiten. Das Spiel antwortet, wie viele Stuecke es angenommen hat -
// deshalb wird hier gewartet.
int craft(const char* schritt, const char* erz, int anzahl)
{
    std::printf("C %s %d %s\n", schritt, anzahl, erz ? erz : "");
    std::fflush(stdout);
    char buf[64];
    if (!std::fgets(buf, sizeof(buf), stdin))
        std::exit(0);
    return std::atoi(buf);
}

// Legieren. Das Spiel antwortet, wie viele Stuecke es angenommen hat.
int alloy(const char* name, int anzahl)
{
    std::printf("G %d %s\n", anzahl, name ? name : "");
    std::fflush(stdout);
    char buf[64];
    if (!std::fgets(buf, sizeof(buf), stdin))
        std::exit(0);
    return std::atoi(buf);
}

// Nur nachfragen: wie viele Stuecke gingen gerade?
int canAlloy(const char* name)
{
    std::printf("P %s\n", name ? name : "");
    std::fflush(stdout);
    char buf[64];
    if (!std::fgets(buf, sizeof(buf), stdin))
        std::exit(0);
    return std::atoi(buf);
}

// Wie viele davon liegen in der Tasche?
int inBag(const char* erz)
{
    std::printf("I %s\n", erz ? erz : "");
    std::fflush(stdout);
    char buf[64];
    if (!std::fgets(buf, sizeof(buf), stdin))
        std::exit(0);
    return std::atoi(buf);
}

bool exists()
{
    std::printf("Q\n");
    std::fflush(stdout);
    char buf[32];
    if (!std::fgets(buf, sizeof(buf), stdin))
        std::exit(0);
    return buf[0] == '1';
}

void out(const char* text)
{
    std::printf("O %s\n", text ? text : "");
    std::fflush(stdout);
}

int getShared(const char* name)
{
    std::printf("V %s\n", name);
    std::fflush(stdout);
    char buf[64];
    if (!std::fgets(buf, sizeof(buf), stdin))
        std::exit(0);
    return std::atoi(buf);
}

void setShared(const char* name, int value)
{
    std::printf("W %s %d\n", name, value);
    std::fflush(stdout);
}

void addShared(const char* name, int delta)
{
    std::printf("A %s %d\n", name, delta);
    std::fflush(stdout);
}

}

namespace {
struct AtEnd
{
    ~AtEnd()
    {
        std::printf("X\n");
        std::fflush(stdout);
    }
};
AtEnd g_atEnd;
}
)KLICKER";

// ---------------------------------------------------------------------------
// Den C++-Compiler finden
// ---------------------------------------------------------------------------
//
// Das Spiel uebersetzt den Code des Spielers wirklich - es braucht also einen
// echten Compiler. Unter Windows ist das cl.exe aus Visual Studio, unter Linux
// g++. Gesucht wird einmal, das Ergebnis bleibt stehen.

std::string Trim(const std::string& s)
{
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

struct Toolchain
{
    std::string              compiler;  // voller Pfad zum Compiler
    std::vector<std::string> env;       // "NAME=WERT", leer = eigene erben
    std::string              problem;   // gefuellt, wenn nichts gefunden wurde
};

#ifdef _WIN32

// vcvars64.bat setzt INCLUDE, LIB und PATH so, dass cl.exe arbeiten kann.
// Ohne das findet der Compiler nicht einmal <string>.
std::string FindVcVars()
{
    const char* pf = std::getenv("ProgramFiles(x86)");
    if (pf != nullptr)
    {
        const std::string vswhere =
            std::string(pf) + "/Microsoft Visual Studio/Installer/vswhere.exe";
        if (FileExists(vswhere))
        {
            std::string out;
            if (RunCapture({vswhere, "-latest", "-products", "*", "-property", "installationPath"},
                           "", out, nullptr, 15000))
            {
                const std::string root = Trim(out);
                if (!root.empty())
                {
                    const std::string bat = root + "/VC/Auxiliary/Build/vcvars64.bat";
                    if (FileExists(bat))
                        return bat;
                }
            }
        }
    }

    static const char* kFallback[] = {
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat",
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Auxiliary/Build/"
        "vcvars64.bat",
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Auxiliary/Build/"
        "vcvars64.bat",
    };
    for (const char* p : kFallback)
        if (FileExists(p))
            return p;

    return std::string();
}

const Toolchain& GetToolchain()
{
    static Toolchain tc;
    static bool      done = false;
    if (done)
        return tc;
    done = true;

    const std::string vcvars = FindVcVars();
    if (vcvars.empty())
    {
        tc.problem = "Visual Studio 2022 was not found (vcvars64.bat is missing).";
        return tc;
    }

    // vcvars64.bat einmal aufrufen und die Umgebung abschreiben, die es setzt.
    // "set" listet sie danach Zeile fuer Zeile auf.
    //
    // Der Umweg ueber eine eigene .bat-Datei ist noetig, weil cmd.exe seine
    // Kommandozeile nach eigenen Regeln zerlegt: Anfuehrungszeichen INNERHALB
    // eines Arguments (und der Pfad zu vcvars braucht welche, er hat
    // Leerzeichen) kommen bei ihm nicht heil an. In einer Datei steht der
    // Aufruf dagegen genau so da, wie er gemeint ist.
    const std::string arbeit = WorkDir();
    if (arbeit.empty())
    {
        tc.problem = "No temp folder available.";
        return tc;
    }

    const std::string helfer = arbeit + "vcenv.bat";
    const std::string inhalt = "@echo off\r\n"
                               "call \"" +
                               vcvars +
                               "\" >nul 2>&1\r\n"
                               "set\r\n";

    if (!WriteTextFile(helfer, inhalt))
    {
        tc.problem = "vcenv.bat could not be written.";
        return tc;
    }

    std::string out;
    if (!RunCapture({"cmd.exe", "/c", helfer}, arbeit, out, nullptr, 60000))
    {
        tc.problem = "vcvars64.bat could not be run.";
        return tc;
    }

    std::string pathValue;
    std::size_t pos = 0;
    while (pos < out.size())
    {
        std::size_t nl = out.find('\n', pos);
        if (nl == std::string::npos)
            nl = out.size();

        std::string entry = out.substr(pos, nl - pos);
        pos               = nl + 1;
        while (!entry.empty() && (entry.back() == '\r' || entry.back() == '\n'))
            entry.pop_back();

        const std::size_t eq = entry.find('=');
        if (eq == std::string::npos || eq == 0)
            continue;

        tc.env.push_back(entry);

        if (_strnicmp(entry.c_str(), "PATH=", 5) == 0)
            pathValue = entry.substr(5);
    }

    // CreateProcess sucht Programme in der Umgebung des AUFRUFERS, nicht in der
    // uebergebenen. Deshalb brauchen wir den vollen Pfad zu cl.exe - und der
    // steht in dem PATH, den vcvars gerade gesetzt hat.
    std::size_t suche = 0;
    while (suche <= pathValue.size() && tc.compiler.empty())
    {
        std::size_t cut = pathValue.find(';', suche);
        if (cut == std::string::npos)
            cut = pathValue.size();

        std::string dir = pathValue.substr(suche, cut - suche);
        suche           = cut + 1;

        if (dir.empty())
            continue;
        if (dir.back() != '/' && dir.back() != '\\')
            dir += '\\';

        const std::string full = dir + "cl.exe";
        if (FileExists(full))
            tc.compiler = full;
    }

    if (tc.compiler.empty())
        tc.problem = "cl.exe was not found.";

    return tc;
}

// Die Kommandozeile zum Uebersetzen. /utf-8: damit Umlaute in print("...")
// richtig ankommen - der Editor liefert UTF-8, und ImGui erwartet ebenfalls
// UTF-8. /FI haengt den Header davor, ohne eine Zeile im Code des Spielers zu
// verbrauchen: die Zeilennummern bleiben also stehen.
std::vector<std::string> CompileCommand(const Toolchain& tc, const std::string& header,
                                        const std::string& exeName)
{
    return {tc.compiler,    "/nologo",       "/EHsc",   "/std:c++17",  "/W1", "/utf-8",
            "/FI" + header, "/Fe" + exeName, "run.cpp", "klicker.cpp"};
}

#else  // Linux

const Toolchain& GetToolchain()
{
    static Toolchain tc;
    static bool      done = false;
    if (done)
        return tc;
    done = true;

    // g++ zuerst, clang++ als Ersatz. Beide koennen alles, was das Spiel
    // braucht - und eines von beiden ist auf einem Rechner mit Entwicklerkram
    // fast immer da.
    tc.compiler = FindInPath("g++");
    if (tc.compiler.empty())
        tc.compiler = FindInPath("clang++");

    if (tc.compiler.empty())
        tc.problem = "No C++ compiler found. Install g++ (package build-essential).";

    return tc;
}

// -include ist das Gegenstueck zu /FI: der Header kommt davor, ohne dass eine
// Zeile im Code des Spielers dazukommt.
std::vector<std::string> CompileCommand(const Toolchain& tc, const std::string& header,
                                        const std::string& exeName)
{
    return {tc.compiler, "-std=c++17", "-O1",     "-include", header,
            "-o",        exeName,      "run.cpp", "klicker.cpp"};
}

#endif

// Zerlegt "W name wert" bzw. "A name differenz".
// Getrennt wird am LETZTEN Leerzeichen, damit Namen mit Leerzeichen gehen.
bool SplitNameValue(const std::string& msg, std::string& name, int& value)
{
    if (msg.size() < 3)
        return false;

    const std::string rest = msg.substr(2);
    const std::size_t sep  = rest.rfind(' ');
    if (sep == std::string::npos || sep == 0)
        return false;

    name  = rest.substr(0, sep);
    value = std::atoi(rest.c_str() + sep + 1);
    return true;
}

// ---- Fehlermeldungen uebersetzen ------------------------------------------
//
// cl.exe sagt Sachen wie  error C3861: "verdoppeln": Bezeichner wurde nicht
// gefunden.  Das stimmt, hilft aber niemandem weiter, der gerade erst anfaengt.
// Hier steht deshalb zu jeder haeufigen Meldung ein Satz, der SAGT WAS ZU TUN
// IST - die Nummer bleibt hinten dran, damit man sie notfalls nachschlagen
// kann.
//
// Uebersetzt wird ueber die Fehlernummer und nicht ueber den Text: cl.exe
// spricht die Sprache von Windows, die Nummer ist ueberall dieselbe.

// Das erste Wort in Anfuehrungszeichen. cl.exe benutzt je nach Sprache " oder
// ', g++ die typographischen Zeichen U+2018/U+2019. Alle drei Sorten also.
std::string FirstQuoted(const std::string& text)
{
    struct Paar
    {
        const char* auf;
        const char* zu;
    };
    static const Paar kPaare[] = {{"\"", "\""}, {"'", "'"}, {"\xe2\x80\x98", "\xe2\x80\x99"}};

    for (const Paar& p : kPaare)
    {
        const std::size_t len = std::strlen(p.auf);

        const std::size_t a = text.find(p.auf);
        if (a == std::string::npos)
            continue;
        const std::size_t b = text.find(p.zu, a + len);
        if (b != std::string::npos && b > a + len)
            return text.substr(a + len, b - a - len);
    }
    return std::string();
}

// Den Namen aufraeumen: der Compiler nennt die Innereien beim Namen
// ("CkItem::wash"), der Spieler kennt nur wash.
std::string KurzerName(const std::string& roh)
{
    std::string       wort = FirstQuoted(roh);
    const std::size_t cut  = wort.rfind("::");
    if (cut != std::string::npos)
        wort = wort.substr(cut + 2);

    // g++ haengt gern die ganze Signatur an: "wash(std::string)". Klammer weg.
    const std::size_t klammer = wort.find('(');
    if (klammer != std::string::npos)
        wort = wort.substr(0, klammer);

    return wort;
}

// Die Saetze selbst. Sie sind auf beiden Systemen dieselben - nur der Weg
// dorthin ist ein anderer. Deshalb stehen sie hier einmal, und die beiden
// Uebersetzer unten springen sie nur noch an.
enum class Fehlerart
{
    Unbekannt,
    NichtBekannt,      // Name gibt es (hier) nicht
    KeinMitglied,      // das Ding kann das nicht
    KeinPunkt,         // vor dem Punkt steht etwas Falsches
    Schreibweise,      // Semikolon und Verwandtes
    TextOhneEnde,      // Anfuehrungszeichen fehlt
    KlammerFehlt,      // { ohne }
    ElseOhneIf,
    FunktionInFunktion,
    FalscheAnzahl,     // zu viele/zu wenige Werte in den Klammern
    FalscheArt,        // Zahl statt Text
    GibtEsSchon,       // Name doppelt
    IncludeFehlt,
    KeinRumpf          // angekuendigt, aber nie geschrieben
};

std::string Satz(Fehlerart art, const std::string& was)
{
    switch (art)
    {
    case Fehlerart::NichtBekannt:
        return was +
               " is not known here yet. In C++ everything must ALREADY BE THERE before you "
               "use it: your own functions belong above main(), not below it. "
               "Otherwise: a typo?";
    case Fehlerart::KeinMitglied:
        return was +
               " does not exist there. block can mine() and isThere(); everything about the bag "
               "belongs to item - selling, washing, alloying.";
    case Fehlerart::KeinPunkt:
        return "What is in front of the dot cannot take a dot. You mean block or item.";
    case Fehlerart::Schreibweise:
        return "Something is written wrong. Most often: the semicolon at the end of the "
               "line BEFORE is missing.";
    case Fehlerart::TextOhneEnde:
        return "A text without an end: somewhere a quotation mark is missing. Text always "
               "belongs between two of them, item.wash(\"Stone\").";
    case Fehlerart::KlammerFehlt:
        return "A curly brace is missing. Every { needs its }.";
    case Fehlerart::ElseOhneIf:
        return "An else without an if. else belongs right after the } of the if.";
    case Fehlerart::FunktionInFunktion:
        return "A function inside a function does not work. Put it next to it, above main().";
    case Fehlerart::FalscheAnzahl:
        return was + " gets the wrong number of values in the brackets.";
    case Fehlerart::FalscheArt:
        return "At " + was +
               " the brackets hold the wrong kind of value - a number where text belongs, "
               "or the other way round. Text belongs in quotes: item.wash(\"Stone\").";
    case Fehlerart::GibtEsSchon:
        return was + " already exists. Every name may be used only once.";
    case Fehlerart::IncludeFehlt:
        return "An #include that does not exist. Check the spelling.";
    case Fehlerart::KeinRumpf:
        return was +
               " was announced, but nowhere did you write what it should do. Missing "
               "is the body with { }.";
    default:
        return std::string();
    }
}

#ifdef _WIN32

// Aus  "konsole1(7,5): error C3861: ..."  wird  C3861.
std::string ErrorCode(const std::string& text)
{
    const std::size_t pos = text.find("error ");
    if (pos == std::string::npos)
        return std::string();

    std::size_t i = pos + 6;
    std::string code;
    while (i < text.size() && text[i] != ':' && text[i] != ' ')
        code += text[i++];

    return code;
}

// Uebersetzt wird ueber die Fehlernummer und nicht ueber den Text: cl.exe
// spricht die Sprache von Windows, die Nummer ist ueberall dieselbe.
std::string Erklaere(const std::string& roh)
{
    const std::string code = ErrorCode(roh);
    if (code.empty())
        return roh;

    const std::string wort = KurzerName(roh);
    const std::string was  = wort.empty() ? std::string("This") : ("\"" + wort + "\"");

    Fehlerart art = Fehlerart::Unbekannt;

    if (code == "C3861" || code == "C2065" || code == "C2064" || code == "C2062")
        art = Fehlerart::NichtBekannt;
    else if (code == "C2039")
        art = Fehlerart::KeinMitglied;
    else if (code == "C2228" || code == "C2227")
        art = Fehlerart::KeinPunkt;
    else if (code == "C2143" || code == "C2144" || code == "C2146" || code == "C2059" ||
             code == "C2238")
        art = Fehlerart::Schreibweise;
    else if (code == "C2001" || code == "C2015")
        art = Fehlerart::TextOhneEnde;
    else if (code == "C1004" || code == "C1075")
        art = Fehlerart::KlammerFehlt;
    else if (code == "C2181")
        art = Fehlerart::ElseOhneIf;
    else if (code == "C2601" || code == "C2447")
        art = Fehlerart::FunktionInFunktion;
    else if (code == "C2660" || code == "C2661" || code == "C2198" || code == "C2668")
        art = Fehlerart::FalscheAnzahl;
    else if (code == "C2664" || code == "C2440" || code == "C2446" || code == "C2665" ||
             code == "C2666")
        art = Fehlerart::FalscheArt;
    else if (code == "C4430" || code == "C2371" || code == "C2086")
        art = Fehlerart::GibtEsSchon;
    else if (code == "C1083")
        art = Fehlerart::IncludeFehlt;
    else if (code == "LNK2019" || code == "LNK2001")
        art = Fehlerart::KeinRumpf;

    const std::string text = Satz(art, was);
    if (text.empty())
        return roh;  // unbekannt: dann lieber das Original als gar nichts

    return text + "  (" + code + ")";
}

#else  // Linux

// g++ hat keine Fehlernummern, es schreibt ganze Saetze. Uebersetzt wird
// deshalb ueber Textstuecke, die sich zwischen den Versionen nicht aendern:
// "was not declared in this scope" sagt gcc seit jeher so.
std::string Erklaere(const std::string& roh)
{
    const std::string wort = KurzerName(roh);
    const std::string was  = wort.empty() ? std::string("This") : ("\"" + wort + "\"");

    auto hat = [&roh](const char* teil) { return roh.find(teil) != std::string::npos; };

    Fehlerart art = Fehlerart::Unbekannt;

    if (hat("was not declared in this scope") || hat("has not been declared") ||
        hat("there are no arguments to") || hat("is not a member of"))
        art = Fehlerart::NichtBekannt;
    else if (hat("has no member named"))
        art = Fehlerart::KeinMitglied;
    else if (hat("request for member") || hat("which is of non-class type"))
        art = Fehlerart::KeinPunkt;
    else if (hat("missing terminating"))
        art = Fehlerart::TextOhneEnde;
    else if (hat("at end of input"))
        art = Fehlerart::KlammerFehlt;
    else if (hat("without a previous"))
        art = Fehlerart::ElseOhneIf;
    else if (hat("too few arguments") || hat("too many arguments"))
        art = Fehlerart::FalscheAnzahl;
    else if (hat("cannot convert") || hat("invalid conversion") || hat("no matching function"))
        art = Fehlerart::FalscheArt;
    else if (hat("redefinition of") || hat("redeclared"))
        art = Fehlerart::GibtEsSchon;
    else if (hat("No such file or directory"))
        art = Fehlerart::IncludeFehlt;
    else if (hat("undefined reference to"))
        art = Fehlerart::KeinRumpf;
    // Die Schreibweise ganz zuletzt: "expected" steht auch in Meldungen, die
    // oben schon genauer erklaert sind.
    else if (hat("expected"))
        art = Fehlerart::Schreibweise;

    const std::string text = Satz(art, was);
    return text.empty() ? roh : text;
}

#endif

// Aus der Ausgabe des Compilers die erste Fehlermeldung holen - samt der
// Konsole und der Zeile, in der sie steht.
//
// Moeglich ist das durch die #line-Anweisungen: der Compiler meldet Fehler
// dadurch als  konsole2(7,5): error ...  (cl.exe) bzw.  konsole2:7:5: error:
// ...  (g++), statt als Zeile in der zusammengesetzten Datei.
void ParseCompilerError(const std::string& out, std::string& message, int& console, int& line)
{
    std::size_t pos = 0;
    while (pos < out.size())
    {
        std::size_t nl = out.find('\n', pos);
        if (nl == std::string::npos)
            nl = out.size();

        const std::string entry = Trim(out.substr(pos, nl - pos));
        pos                     = nl + 1;

        if (entry.find(": error") == std::string::npos &&
            entry.find(": fatal error") == std::string::npos)
            continue;

        const std::size_t open = entry.find("konsole");
        if (open == std::string::npos)
        {
            message = Erklaere(entry);
            return;
        }

        console = std::atoi(entry.c_str() + open + 7);

        // Hinter dem Konsolennamen steht die Stelle: cl.exe schreibt sie als
        // (zeile,spalte), g++ als :zeile:spalte. Gesucht wird also die Zahl
        // direkt hinter dem Namen - egal, welches Zeichen davor steht.
        std::size_t i = open + 7;
        while (i < entry.size() && entry[i] >= '0' && entry[i] <= '9')
            ++i;  // die Nummer der Konsole selbst ueberspringen

        if (i < entry.size() && (entry[i] == '(' || entry[i] == ':'))
            line = std::atoi(entry.c_str() + i + 1);

        // Der Text dahinter. Beide Compiler leiten ihn mit ": error" ein.
        const std::size_t fehler = entry.find(": error", open);
        const std::size_t fatal  = entry.find(": fatal error", open);
        const std::size_t start  = (fehler != std::string::npos) ? fehler : fatal;

        message = Erklaere((start != std::string::npos) ? Trim(entry.substr(start + 2)) : entry);
        return;
    }

    message = out.empty() ? "Compiling failed." : Trim(out);
    if (message.size() > 300)
        message = message.substr(0, 300) + " ...";
}

// Setzt alle Konsolen zu einer einzigen .cpp zusammen.
//
// Reihenfolge: erst die Konsolen OHNE main(), dann die mit. In C++ muss eine
// Variable vor ihrer Benutzung stehen - so sieht die Konsole mit main()
// automatisch alles aus den anderen.
//
// Vor jedes Stueck kommt ein  #line 1 "konsoleN"  - dadurch meldet der Compiler
// Fehler mit der richtigen Konsole und der richtigen Zeilennummer.
std::string CombineSources(const std::vector<SourceFile>& files)
{
    std::string out;

    auto append = [&out](const SourceFile& f)
    {
        out += "#line 1 \"konsole";
        out += std::to_string(f.id);
        out += "\"\n";
        out += Instrument(f.code, f.id);
        if (!out.empty() && out.back() != '\n')
            out += '\n';
    };

    for (const SourceFile& f : files)
        if (!ContainsMainFunction(f.code))
            append(f);

    for (const SourceFile& f : files)
        if (ContainsMainFunction(f.code))
            append(f);

    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Uebersetzen (laeuft auf einem eigenen Thread, damit das Fenster fluessig bleibt)
// ---------------------------------------------------------------------------

static Native::Build CompileToExe(const std::vector<SourceFile>& files, int runId,
                                  const std::string& lastCombined, const std::string& lastExe,
                                  const std::string& lastDir);

Native::~Native()
{
    stop();
    if (mBuild.valid())
        mBuild.wait();
}

void Native::start(const std::vector<SourceFile>& files)
{
    stop();

    mConsole    = 0;
    mLine       = 0;
    mErrConsole = 0;
    mErrLine    = 0;
    mBudget     = 0.0f;
    mAwaitingGo = false;
    mPending.clear();
    mMsg   = "compiling ...";
    mPhase = Phase::Compiling;

    static int counter = 0;
    const int  id      = ++counter;

    const std::string  lastCombined = mLastCombined;
    const std::string lastExe      = mLastExe;
    const std::string lastDir      = mLastDir;

    mBuild = std::async(std::launch::async, [files, id, lastCombined, lastExe, lastDir]
                        { return CompileToExe(files, id, lastCombined, lastExe, lastDir); });
}

void Native::fail(const std::string& message, int console, int line)
{
    stop();
    mPhase      = Phase::Failed;
    mMsg        = message;
    mErrConsole = console;
    mErrLine    = line;
    mConsole    = 0;
    mLine       = 0;
}

void Native::togglePause()
{
    if (mPhase == Phase::Running)
        mPhase = Phase::Paused;
    else if (mPhase == Phase::Paused)
        mPhase = Phase::Running;
}

void Native::stop()
{
    closeChild();
    if (mPhase == Phase::Running || mPhase == Phase::Paused || mPhase == Phase::Compiling)
        mPhase = Phase::Idle;
    mConsole    = 0;
    mLine       = 0;
    mAwaitingGo = false;
    mPending.clear();
}

RunState Native::state() const
{
    switch (mPhase)
    {
    case Phase::Compiling:
    case Phase::Running: return RunState::Running;
    case Phase::Paused: return RunState::Paused;
    case Phase::Done: return RunState::Done;
    case Phase::Failed: return RunState::Failed;
    default: return RunState::Idle;
    }
}

void Native::closeChild()
{
    mChild.close();
}

void Native::finish(const char* reason)
{
    mPhase      = Phase::Done;
    mConsole    = 0;
    mLine       = 0;
    mAwaitingGo = false;
    if (mMsg.empty() || mMsg == "running" || mMsg == "compiling ...")
        mMsg = reason;
    closeChild();
}

void Native::sendChild(const char* text)
{
    mChild.write(text);
}

bool Native::launch(const Build& build)
{
    return mChild.start(build.exe, build.dir);
}

void Native::pump(World& world, const OrePlan& ores, const CraftPlan& craft,
                  const AlloyPlan& alloys, const Limits& limits)
{
    if (!mChild.started())
        return;

    // Holen, was da ist - read() bleibt nicht stehen, wenn gerade nichts kommt.
    char        buf[2048];
    std::size_t got = 0;
    while ((got = mChild.read(buf, sizeof(buf))) > 0)
        mPending.append(buf, got);

    std::size_t nl;
    while ((nl = mPending.find('\n')) != std::string::npos)
    {
        std::string entry = mPending.substr(0, nl);
        mPending.erase(0, nl + 1);
        while (!entry.empty() && entry.back() == '\r')
            entry.pop_back();
        handle(entry, world, ores, craft, alloys, limits);
    }
}

void Native::handle(const std::string& msg, World& world, const OrePlan& ores,
                    const CraftPlan& craft, const AlloyPlan& alloys, const Limits& limits)
{
    if (msg.empty())
        return;

    switch (msg[0])
    {
    case 'L':  // "L konsole zeile"
    {
        const char* p = msg.c_str() + 1;
        mConsole      = std::atoi(p);
        const std::size_t sep = msg.rfind(' ');
        mLine         = (sep != std::string::npos) ? std::atoi(msg.c_str() + sep + 1) : 0;
        mAwaitingGo   = true;
        break;
    }

    case 'M':
        mMsg = world.mine() ? "Block mined." : "The block is already mined.";
        break;

    case 'Q':
        sendChild(world.blockAlive ? "1\n" : "0\n");
        break;

    case 'K':  // verkaufen. Ohne Zusatz alles, sonst "K <anzahl> <erz>"
    {
        int geld = 0;

        if (msg.size() > 2)
        {
            const std::string rest = msg.substr(2);
            const std::size_t cut  = rest.find(' ');
            if (cut != std::string::npos)
                geld = world.sell(ores, craft, rest.substr(cut + 1),
                                  std::atoi(rest.substr(0, cut).c_str()));
        }
        else
        {
            geld = world.sell(ores, craft);
        }

        sendChild((std::to_string(geld) + "\n").c_str());
        mMsg = (geld > 0) ? ("Sold: " + ui::Money(geld) + " money.")
                          : std::string("Nothing to sell.");
        break;
    }

    case 'C':  // verarbeiten:  C <schritt> <anzahl> <erz>
    {
        int wie = 0;

        // Getrennt wird von vorne: Schritt und Anzahl sind ein Wort, der Rest
        // ist der Name des Erzes - der darf Leerzeichen haben.
        const std::string rest = (msg.size() > 2) ? msg.substr(2) : std::string();
        const std::size_t a    = rest.find(' ');
        const std::size_t b    = (a != std::string::npos) ? rest.find(' ', a + 1) : a;

        if (b != std::string::npos)
            wie = world.startCraft(ores, craft, limits, rest.substr(0, a), rest.substr(b + 1),
                                   std::atoi(rest.c_str() + a + 1));

        sendChild((std::to_string(wie) + "\n").c_str());
        mMsg = (wie > 0) ? (world.craftName + ": " + std::to_string(wie) + " pieces.")
                         : std::string("Processing did not work.");
        break;
    }

    case 'G':  // legieren:  G <anzahl> <name>
    {
        int wie = 0;

        // Wie beim Verarbeiten: die Anzahl ist ein Wort, der Rest ist der Name -
        // der darf Leerzeichen haben.
        const std::string rest = (msg.size() > 2) ? msg.substr(2) : std::string();
        const std::size_t cut  = rest.find(' ');

        if (cut != std::string::npos)
            wie = world.startAlloy(ores, alloys, limits, rest.substr(cut + 1),
                                   std::atoi(rest.c_str()), false);

        sendChild((std::to_string(wie) + "\n").c_str());
        mMsg = (wie > 0) ? ("Alloying: " + std::to_string(wie) + " pieces.")
                         : std::string("Alloying did not work.");
        break;
    }

    case 'P':  // item.canAlloy(...) - wie viele gingen gerade?
    {
        const std::string name = (msg.size() > 2) ? msg.substr(2) : std::string();
        sendChild((std::to_string(world.canAlloy(ores, alloys, limits, name)) + "\n").c_str());
        break;
    }

    case 'I':  // item.has(...) - in die Tasche schauen
    {
        const std::string erz = (msg.size() > 2) ? msg.substr(2) : std::string();
        sendChild((std::to_string(world.inventoryOf(ores, erz)) + "\n").c_str());
        break;
    }

    case 'O':
        mMsg = (msg.size() > 2) ? msg.substr(2) : std::string();
        break;

    // Geteilte Variablen. Sie liegen im Spiel, deshalb sehen alle Konsolen
    // denselben Wert.
    case 'V':  // lesen  -> Wert zurueckschicken
    {
        const std::string name = msg.substr(2);
        const auto        it   = world.shared.find(name);
        const int         value = (it != world.shared.end()) ? it->second : 0;
        sendChild((std::to_string(value) + "\n").c_str());
        break;
    }

    case 'W':  // schreiben:  W name wert
    {
        std::string name;
        int         value = 0;
        if (SplitNameValue(msg, name, value))
            world.shared[name] = value;
        break;
    }

    case 'A':  // dazuzaehlen:  A name differenz
    {
        std::string name;
        int         delta = 0;
        if (SplitNameValue(msg, name, delta))
            world.shared[name] += delta;
        break;
    }

    case 'X':
        finish("Done.");
        break;

    default:
        break;
    }
}

void Native::update(float dt, World& world, const OrePlan& ores, const CraftPlan& craft,
                    const AlloyPlan& alloys, const Limits& limits)
{
    if (mPhase == Phase::Compiling)
    {
        if (!mBuild.valid())
        {
            mPhase = Phase::Idle;
            return;
        }
        if (mBuild.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        const Build build = mBuild.get();
        if (!build.ok)
        {
            mPhase      = Phase::Failed;
            mErrConsole = build.errorConsole;
            mErrLine    = build.errorLine;
            mMsg        = build.error;
            mConsole    = 0;
            mLine       = 0;
            return;
        }
        if (!launch(build))
        {
            mPhase = Phase::Failed;
            mMsg   = "The program could not be started.";
            return;
        }
        mLastCombined = build.combined;
        mLastExe      = build.exe;
        mLastDir      = build.dir;

        mPhase  = Phase::Running;
        mMsg    = "running";
        mBudget = 1.0f;  // die erste Zeile sofort freigeben
        return;
    }

    if (mPhase != Phase::Running && mPhase != Phase::Paused)
        return;

    pump(world, ores, craft, alloys, limits);

    if (mPhase == Phase::Running)
    {
        mBudget += dt * mLinesPerSecond;

        // Hoechstens eine Zeile auf Vorrat. Sonst spart das Spiel waehrend
        // einer Pause oder beim Kompilieren Zeilen an und feuert sie danach
        // auf einen Schlag ab - das sieht aus, als holte es etwas nach.
        if (mBudget > 1.0f)
            mBudget = 1.0f;

        if (mAwaitingGo && mBudget >= 1.0f)
        {
            mBudget -= 1.0f;
            mAwaitingGo = false;
            sendChild("g\n");
        }
    }

    if (mChild.started() && !mChild.alive())
    {
        pump(world, ores, craft, alloys, limits);
        if (mPhase == Phase::Running || mPhase == Phase::Paused)
        {
            const unsigned long code = mChild.exitCode();
            if (code == 0)
            {
                finish("Done.");
            }
            else
            {
                // Die Zeile, an der es passiert ist, kennen wir noch: das Kind
                // meldet ja jede Zeile, BEVOR es sie ausfuehrt. Deshalb wird
                // sie hier gerettet, bevor die laufende Anzeige geloescht wird -
                // sonst stuende der Spieler vor einem Absturz ohne jeden
                // Anhaltspunkt.
                mErrConsole = mConsole;
                mErrLine    = mLine;

                mPhase   = Phase::Failed;
                mConsole = 0;
                mLine    = 0;
                mMsg     = CrashText(code);
                closeChild();
            }
        }
    }
}

// ---------------------------------------------------------------------------

static Native::Build CompileToExe(const std::vector<SourceFile>& files, int runId,
                                  const std::string& lastCombined, const std::string& lastExe,
                                  const std::string& lastDir)
{
    Native::Build build;

    // Erst pruefen, ob es ueberhaupt ein Programm gibt. Ohne diese Pruefung
    // kaeme nur eine kryptische Linker-Meldung.
    std::vector<int> withMain;
    for (const SourceFile& f : files)
        if (ContainsMainFunction(f.code))
            withMain.push_back(f.id);

    if (withMain.empty())
    {
        build.error = "No console has an  int main() { ... }  - that is where the program starts.";
        return build;
    }
    if (withMain.size() > 1)
    {
        build.error = "Console " + std::to_string(withMain[0]) + " and console " +
                      std::to_string(withMain[1]) +
                      " both have a main(). There may only be one.";
        build.errorConsole = withMain[1];
        return build;
    }

    // Hat sich seit dem letzten Mal nichts geaendert? Dann das fertige
    // Programm einfach noch einmal starten. Beim Klicken am Spielanfang ist
    // genau das der Normalfall.
    build.combined = CombineSources(files);

    if (!lastExe.empty() && build.combined == lastCombined && FileExists(lastExe))
    {
        build.ok     = true;
        build.reused = true;
        build.exe    = lastExe;
        build.dir    = lastDir;
        return build;
    }

    const std::string base = WorkDir();
    if (base.empty())
    {
        build.error = "No temp folder available.";
        return build;
    }

    const std::string dir = base + "r" + std::to_string(runId) + Sep();
    MakeDir(dir);
    build.dir = dir;

    const std::string headerPath = dir + "klicker.h";

    if (!WriteTextFile(headerPath, kKlickerHeader) ||
        !WriteTextFile(dir + "klicker.cpp", kKlickerSource) ||
        !WriteTextFile(dir + "run.cpp", build.combined))
    {
        build.error = "Files could not be written.";
        return build;
    }

    const Toolchain& tc = GetToolchain();
    if (tc.compiler.empty())
    {
        build.error = tc.problem.empty() ? "The C++ compiler was not found." : tc.problem;
        return build;
    }

    // Der Name ist ohne Pfad, damit er auf beiden Systemen gleich aussieht -
    // gebaut wird ohnehin im Arbeitsordner dir.
    const std::string exeName = std::string("run") + ExeSuffix();

    std::string out;
    const bool  ok = RunCapture(CompileCommand(tc, headerPath, exeName), dir, out,
                                tc.env.empty() ? nullptr : &tc.env, 60000);

    if (!ok)
    {
        ParseCompilerError(out, build.error, build.errorConsole, build.errorLine);
        return build;
    }

    build.ok  = true;
    build.exe = dir + exeName;
    return build;
}
