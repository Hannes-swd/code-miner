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
//
// Der Header hat drei Teile: das Stueck vor der Erzliste, die Erzliste selbst
// (die haengt von data/erze.json und den gewuerfelten Erzen ab, wird also erst
// beim Start zusammengebaut) und das Stueck danach.
// ---------------------------------------------------------------------------
const char* kHeaderTop = R"KLICKER(#pragma once
#include <map>
#include <string>

namespace ck {
void line(int console, int n);

// Dasselbe, aber INNERHALB einer eigenen Funktion. Mit dem Punkt "inline"
// kostet so eine Zeile nur die Haelfte - deshalb muss das Spiel die beiden
// auseinanderhalten koennen. Die Instrumentierung setzt es ein, siehe
// instrument.cpp.
void linef(int console, int n);

void mine(int care);
int  oreHere();
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

// Was ueber ein Erz bekannt ist. Rueckgabe false = noch nicht untersucht,
// dann steht in den Feldern nichts Brauchbares.
bool oreInfo(int ore, int& value, int& rarity, int& level, int& care);
int  assayOre(int ore);  // was die Untersuchung gekostet hat, 0 = ging nicht

int  bagCount(const char* erz);
int  bagPurity(const char* erz);

void jobState(int& busy, int& idle, int& promille);

void status(int& money, int& leftMs, int& target);
void marketOf(const char* erz, int& jetzt, int& mittel);

void waitMs(int ms);
int  blockLoadingMs();
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

)KLICKER";

// Hierher kommt die Erzliste als enum (siehe OreEnumSource). Danach geht es
// mit diesem Stueck weiter.
const char* kHeaderBottom = R"KLICKER(
// Was ein Block beim Abbau verlangt.
//
// Je wertvoller ein Erz, desto oefter will es behandelt werden: gekuehlt oder
// erhitzt. Das haengt am ERZ und steht fest - Gold will immer dasselbe, jeder
// einzelne Goldblock. Der Block kommt immer gleich schnell heraus, aber falsch
// behandelt verliert er Reinheit und ist damit weniger wert. Die falsche
// Behandlung kostet mehr als gar keine.
//
// Es gibt also keinen Befehl, der dir sagt, was zu tun ist. Du musst wissen,
// welches Erz was will - im Wiki steht es bei jedem Erz - und danach fragen,
// welches gerade dasteht:
//
//     if (block.is(Gold))         block.mine(Cool);
//     else if (block.is(Diamond)) block.mine(Heat);
//     else                        block.mine();
enum Care
{
    Plain = 0,  // will nichts - einfach abbauen
    Cool  = 1,  // muss gekuehlt werden
    Heat  = 2   // muss erhitzt werden
};

// Der Block da draussen in der Welt. Hier steht nur, was mit IHM zu tun hat:
// abbauen und nachschauen, ob er ueberhaupt da ist. Alles, was danach kommt -
// Tasche, Verarbeiten, Verkaufen - gehoert zu "item" weiter unten.
struct CkBlock
{
    void mine() const { ck::mine((int)Plain); }
    void mine(Care womit) const { ck::mine((int)womit); }

    // Welches Erz steht gerade da? block.ore() gibt es als Ore zurueck -
    // print(block.ore()) schreibt den Namen hin.
    Ore ore() const { return (Ore)ck::oreHere(); }

    // Und dieselbe Frage als Ja/Nein - so passt sie direkt in ein if.
    // block.is(Any) ist immer wahr, solange ueberhaupt ein Block dasteht.
    bool is(Ore erz) const
    {
        const int hier = ck::oreHere();
        return (erz == Any) ? (hier >= 0) : ((Ore)hier == erz);
    }

    // Ist der Block gerade da?
    //   true  = da, man kann ihn abbauen
    //   false = weg, er waechst gerade nach
    bool isThere() const { return ck::exists(); }
    bool exists()  const { return ck::exists(); }

    // Waechst er gerade nach? Genau das Gegenteil von isThere().
    bool isLoading() const { return !ck::exists(); }
    bool isGone()    const { return !ck::exists(); }

    // Wie lange dauert es noch, bis er wieder dasteht? In Sekunden, 0 = er ist
    // schon da.
    //
    // Zusammen mit wait() ist das die Antwort auf die Warteschleife:
    //
    //     wait(block.loading());   // eine Zeile statt hundert Leerlaeufe
    //     block.mine();
    float loading() const { return (float)ck::blockLoadingMs() / 1000.0f; }
};

static const CkBlock block;

// Deine Tasche. Was einmal abgebaut ist, gehoert nicht mehr dem Block, sondern
// dir - deshalb steht das alles hier und nicht bei "block".
struct CkItem
{
    // Verkauft alles aus der Tasche und gibt zurueck, wie viel Geld es gab.
    int sell() const { return ck::sell(); }

    // Nur eine Sorte: item.sell(Stone) verkauft alle Steine,
    // item.sell(Stone, 3) genau drei davon. item.sell(Any) ist dasselbe wie
    // item.sell() - alles.
    int sell(Ore erz) const
    {
        return (erz == Any) ? ck::sell() : ck::sellSome(CkOreName(erz), -1);
    }
    int sell(Ore erz, int anzahl) const { return ck::sellSome(CkOreName(erz), anzahl); }

    // In die Tasche schauen, ohne zu verkaufen:
    //     if (item.has(Stone)) ...        mindestens einer
    //     if (item.has(Stone, 10)) ...    mindestens zehn
    //     if (item.has(Any)) ...          ueberhaupt irgendetwas
    bool has(Ore erz) const { return ck::inBag(CkOreName(erz)) > 0; }
    bool has(Ore erz, int anzahl) const { return ck::inBag(CkOreName(erz)) >= anzahl; }

    // Wie VIELE liegen da? has() hat immer nur ja oder nein gesagt - damit
    // liess sich nicht rechnen.
    //
    //     if (item.count(Gold) >= 10) item.smelt(Gold, 10);
    int count(Ore erz) const { return ck::bagCount(CkOreName(erz)); }

    // Und wie sauber sind sie? Ueber alle Zustaende gemittelt, in Prozent.
    // Die Reinheit macht bis zum Dreifachen im Preis aus - ohne diese Frage
    // sieht man das nur in der Tasche und nie im Programm.
    int purity(Ore erz) const { return ck::bagPurity(CkOreName(erz)); }

    int count(const std::string& erz) const { return ck::bagCount(erz.c_str()); }
    int purity(const std::string& erz) const { return ck::bagPurity(erz.c_str()); }

    // Dasselbe mit Text. Das war frueher der einzige Weg - es geht weiter,
    // damit alter Code nicht auf einmal nicht mehr uebersetzt.
    int  sell(const std::string& erz) const { return ck::sellSome(erz.c_str(), -1); }
    int  sell(const std::string& erz, int anzahl) const { return ck::sellSome(erz.c_str(), anzahl); }
    bool has(const std::string& erz) const { return ck::inBag(erz.c_str()) > 0; }
    bool has(const std::string& erz, int anzahl) const
    {
        return ck::inBag(erz.c_str()) >= anzahl;
    }

    // Verarbeiten. Roh verkaufen bringt wenig - verarbeitet ist ein Block
    // deutlich mehr wert.
    //
    //     item.wash(Gold);        alles, was gerade passt
    //     item.smelt(Gold, 3);    genau drei Stueck
    //
    // Rueckgabe: wie viele Stuecke wirklich in Arbeit gegeben wurden.
    // 0 heisst: ging nicht - falscher Zustand, nichts da, noch nicht
    // freigeschaltet, oder es laeuft schon ein Auftrag. Es laeuft immer nur
    // EINER, und er braucht Zeit.
    //
    // Was von wo nach wo geht, steht in data/verarbeitung.json - dort ist es
    // ein Netz und keine feste Kette.
    int wash(Ore erz)   const { return ck::craft("wash", CkOreName(erz), -1); }
    int smelt(Ore erz)  const { return ck::craft("smelt", CkOreName(erz), -1); }
    int cast(Ore erz)   const { return ck::craft("cast", CkOreName(erz), -1); }
    int clean(Ore erz)  const { return ck::craft("clean", CkOreName(erz), -1); }
    int polish(Ore erz) const { return ck::craft("polish", CkOreName(erz), -1); }
    int harden(Ore erz) const { return ck::craft("harden", CkOreName(erz), -1); }
    int refine(Ore erz) const { return ck::craft("refine", CkOreName(erz), -1); }
    int press(Ore erz)  const { return ck::craft("press", CkOreName(erz), -1); }
    int etch(Ore erz)   const { return ck::craft("etch", CkOreName(erz), -1); }
    int fuse(Ore erz)   const { return ck::craft("fuse", CkOreName(erz), -1); }

    int wash(Ore erz, int anzahl)   const { return ck::craft("wash", CkOreName(erz), anzahl); }
    int smelt(Ore erz, int anzahl)  const { return ck::craft("smelt", CkOreName(erz), anzahl); }
    int cast(Ore erz, int anzahl)   const { return ck::craft("cast", CkOreName(erz), anzahl); }
    int clean(Ore erz, int anzahl)  const { return ck::craft("clean", CkOreName(erz), anzahl); }
    int polish(Ore erz, int anzahl) const { return ck::craft("polish", CkOreName(erz), anzahl); }
    int harden(Ore erz, int anzahl) const { return ck::craft("harden", CkOreName(erz), anzahl); }
    int refine(Ore erz, int anzahl) const { return ck::craft("refine", CkOreName(erz), anzahl); }
    int press(Ore erz, int anzahl)  const { return ck::craft("press", CkOreName(erz), anzahl); }
    int etch(Ore erz, int anzahl)   const { return ck::craft("etch", CkOreName(erz), anzahl); }
    int fuse(Ore erz, int anzahl)   const { return ck::craft("fuse", CkOreName(erz), anzahl); }

    // ... und dasselbe mit Text, wie oben bei has() und sell().
    int wash(const std::string& erz)   const { return ck::craft("wash", erz.c_str(), -1); }
    int smelt(const std::string& erz)  const { return ck::craft("smelt", erz.c_str(), -1); }
    int cast(const std::string& erz)   const { return ck::craft("cast", erz.c_str(), -1); }
    int clean(const std::string& erz)  const { return ck::craft("clean", erz.c_str(), -1); }
    int polish(const std::string& erz) const { return ck::craft("polish", erz.c_str(), -1); }
    int harden(const std::string& erz) const { return ck::craft("harden", erz.c_str(), -1); }
    int refine(const std::string& erz) const { return ck::craft("refine", erz.c_str(), -1); }
    int press(const std::string& erz)  const { return ck::craft("press", erz.c_str(), -1); }
    int etch(const std::string& erz)   const { return ck::craft("etch", erz.c_str(), -1); }
    int fuse(const std::string& erz)   const { return ck::craft("fuse", erz.c_str(), -1); }

    int wash(const std::string& erz, int anzahl)   const { return ck::craft("wash", erz.c_str(), anzahl); }
    int smelt(const std::string& erz, int anzahl)  const { return ck::craft("smelt", erz.c_str(), anzahl); }
    int cast(const std::string& erz, int anzahl)   const { return ck::craft("cast", erz.c_str(), anzahl); }
    int clean(const std::string& erz, int anzahl)  const { return ck::craft("clean", erz.c_str(), anzahl); }
    int polish(const std::string& erz, int anzahl) const { return ck::craft("polish", erz.c_str(), anzahl); }
    int harden(const std::string& erz, int anzahl) const { return ck::craft("harden", erz.c_str(), anzahl); }
    int refine(const std::string& erz, int anzahl) const { return ck::craft("refine", erz.c_str(), anzahl); }
    int press(const std::string& erz, int anzahl)  const { return ck::craft("press", erz.c_str(), anzahl); }
    int etch(const std::string& erz, int anzahl)   const { return ck::craft("etch", erz.c_str(), anzahl); }
    int fuse(const std::string& erz, int anzahl)   const { return ck::craft("fuse", erz.c_str(), anzahl); }

    // Legieren: aus zwei verschiedenen Erzen wird EIN neuer Stoff, der mehr
    // wert ist als seine Teile - und der sich danach normal weiterverarbeiten
    // laesst.
    //
    //     item.alloy(Electrum);       ein Stueck
    //     item.alloy(Electrum, 3);    drei Stueck
    //
    // Rueckgabe: wie viele Stuecke wirklich in Arbeit gegeben wurden.
    // 0 heisst: ging nicht - Zutaten fehlen, falscher Zustand, noch nicht
    // freigeschaltet, oder es laeuft schon ein Auftrag. Legieren benutzt
    // denselben Platz wie das Verarbeiten: es laeuft immer nur EINER.
    //
    // Welche Rezepte es gibt und in welchem Zustand die Zutaten sein muessen,
    // steht in data/legierungen.json.
    int alloy(Ore stoff) const { return ck::alloy(CkOreName(stoff), 1); }
    int alloy(Ore stoff, int anzahl) const { return ck::alloy(CkOreName(stoff), anzahl); }

    int alloy(const std::string& stoff) const { return ck::alloy(stoff.c_str(), 1); }
    int alloy(const std::string& stoff, int anzahl) const
    {
        return ck::alloy(stoff.c_str(), anzahl);
    }

    // Wie viele Stueck koenntest du gerade davon machen? 0 = keins.
    // Damit kannst du dich VORHER entscheiden:
    //
    //     if (item.canAlloy(Electrum)) item.alloy(Electrum);
    //     else                         item.sell(Gold);
    int canAlloy(Ore stoff) const { return ck::canAlloy(CkOreName(stoff)); }
    int canAlloy(const std::string& stoff) const { return ck::canAlloy(stoff.c_str()); }
};

static const CkItem item;
)KLICKER";

// Zweites Stueck. Aufgeteilt ist es nur, weil MSVC eine Zeichenfolge nicht
// laenger als 16380 Bytes annimmt (C2026) - inhaltlich gehoert es direkt an
// das Stueck darueber.
const char* kHeaderBottom2 = R"KLICKER(
// ---------------------------------------------------------------------------
// Was ein Erz IST
// ---------------------------------------------------------------------------
//
// Frueher gab es diese Frage nicht, und das war das groesste Loch im Spiel:
// welche Behandlung ein Erz will, stand nur im Wiki. Also musste man sie als
// feste Kette abschreiben -
//
//     if (block.is(Gold))         block.mine(Cool);
//     else if (block.is(Diamond)) block.mine(Heat);
//
// - und weil sich das Spiel endlos neue Erze ausdenkt, war diese Kette nie
// fertig. Jeder neue Fund hiess: Wiki aufschlagen, Zeile nachtragen.
//
// info() dreht das um. Es gibt einen ZEIGER auf das, was man ueber ein Erz
// weiss - und nullptr, wenn man es noch nicht untersucht hat. Damit passt ein
// einziges Programm auf jedes Erz, auch auf die, die es beim Schreiben noch
// gar nicht gab:
//
//     const OreInfo* i = info(block.ore());
//     if (i == nullptr) assay(block.ore());   // erst kennenlernen
//     else              block.mine(i->care);  // dann richtig abbauen
struct OreInfo
{
    Ore         ore   = Any;
    const char* name  = "";
    int         value = 0;   // Grundwert, wie in data/erze.json
    int         rarity = 0;  // je groesser, desto seltener
    int         level = 0;   // ab welchem Level es ueberhaupt vorkommt
    Care        care  = Plain;  // DAS ist der Grund, warum es info() gibt
};

// Der Name eines Erzes als Text. Praktisch als Schluessel fuer shared[...]:
//
//     shared[nameOf(block.ore())] = (int)Cool;
inline const char* nameOf(Ore erz) { return CkOreName(erz); }

// Was das Spiel schon verraten hat. Ein Erz, das einmal drinsteht, bleibt
// drin - deshalb kostet ein zweites info() auf dasselbe Erz nichts mehr.
//
// Die Zeiger bleiben gueltig: eine std::map verschiebt ihre Eintraege nie.
inline std::map<int, OreInfo>& CkOreCache()
{
    static std::map<int, OreInfo> cache;
    return cache;
}

inline const OreInfo* info(Ore erz)
{
    const int nummer = (int)erz;
    if (nummer < 0)
        return nullptr;  // Any ist kein Erz, sondern eine Frage

    std::map<int, OreInfo>& cache = CkOreCache();

    const std::map<int, OreInfo>::iterator it = cache.find(nummer);
    if (it != cache.end())
        return &it->second;

    int value = 0, rarity = 0, level = 0, care = 0;
    if (!ck::oreInfo(nummer, value, rarity, level, care))
        return nullptr;  // noch nicht untersucht - assay() hilft

    OreInfo& e = cache[nummer];
    e.ore      = erz;
    e.name     = CkOreName(erz);
    e.value    = value;
    e.rarity   = rarity;
    e.level    = level;
    e.care     = (Care)care;
    return &e;
}

// Ein unbekanntes Erz untersuchen. Kostet Geld und einen Moment Zeit; danach
// weiss info() Bescheid.
//
// Rueckgabe: was es gekostet hat. 0 heisst: ging nicht - kein Geld, schon
// bekannt, oder es laeuft bereits eine Untersuchung.
inline int assay(Ore erz)
{
    const int bezahlt = ck::assayOre((int)erz);
    if (bezahlt > 0)
        CkOreCache().erase((int)erz);  // beim naechsten info() frisch fragen
    return bezahlt;
}

// ---------------------------------------------------------------------------
// Die Werkstatt
// ---------------------------------------------------------------------------
//
// Ein Auftrag braucht Zeit, und es laufen nur so viele gleichzeitig, wie du
// Oefen hast. Ohne diese Fragen erfaehrst du das nur, indem du es VERSUCHST -
// und der Versuch kostet eine Zeile.
struct CkJob
{
    // Laeuft gerade mindestens einer?
    bool busy() const
    {
        int b = 0, i = 0, p = 0;
        ck::jobState(b, i, p);
        return b > 0;
    }

    // Wie viele Plaetze sind frei? 0 heisst: ein item.wash() ginge jetzt ins
    // Leere.
    int idle() const
    {
        int b = 0, i = 0, p = 0;
        ck::jobState(b, i, p);
        return i;
    }

    // Wie weit ist der, der als naechstes fertig wird? 0 bis 1.
    float progress() const
    {
        int b = 0, i = 0, p = 0;
        ck::jobState(b, i, p);
        return (float)p / 1000.0f;
    }
};

static const CkJob job;

// ---------------------------------------------------------------------------
// Warten, ohne Zeilen zu verbrennen
// ---------------------------------------------------------------------------
//
// Eine Warteschleife kostet dich in JEDEM Durchgang eine Zeile:
//
//     while (!block.isThere()) { }        // teuer
//     wait(block.loading());              // eine Zeile, fertig
//
// Waehrend des Wartens laeuft die Welt weiter: der Block waechst nach, die
// Oefen arbeiten. Steht das Spiel still (F9 oder Vorbereitung), steht auch
// das Warten still.
inline void wait(float sekunden)
{
    if (sekunden < 0.0f)
        sekunden = 0.0f;
    ck::waitMs((int)(sekunden * 1000.0f + 0.5f));
}

// ---------------------------------------------------------------------------
// Wie steht es gerade?
// ---------------------------------------------------------------------------
inline int money()
{
    int g = 0, l = 0, z = 0;
    ck::status(g, l, z);
    return g;
}

// Restzeit der Runde in Sekunden.
//
// Beides sind freie Funktionen und keine Felder eines "round"-Objekts: round
// gibt es in <cmath> schon (std::round), und ein zweites daneben laesst sich
// nicht uebersetzen. Ein Name, der mit der Standardbibliothek streitet, ist
// keiner - auch wenn er sich schoener liest.
inline float timeLeft()
{
    int g = 0, l = 0, z = 0;
    ck::status(g, l, z);
    return (float)l / 1000.0f;
}

// Wie viel Geld am Ende der Runde dastehen muss.
inline int roundTarget()
{
    int g = 0, l = 0, z = 0;
    ck::status(g, l, z);
    return z;
}

// ---------------------------------------------------------------------------
// Der Markt
// ---------------------------------------------------------------------------
//
// Preise schwanken ueber die Runde. Wer zum richtigen Moment verkauft,
// verdient mehr - und der richtige Moment laesst sich programmieren:
//
//     if (market.price(Gold) > market.average(Gold)) item.sell(Gold);
//
// Beide Zahlen gelten fuer EIN rohes Stueck bei voller Reinheit. Verarbeitet
// ist es entsprechend mehr, aber der Ausschlag ist derselbe.
struct CkMarket
{
    int price(Ore erz) const
    {
        int jetzt = 0, mittel = 0;
        ck::marketOf(CkOreName(erz), jetzt, mittel);
        return jetzt;
    }

    int average(Ore erz) const
    {
        int jetzt = 0, mittel = 0;
        ck::marketOf(CkOreName(erz), jetzt, mittel);
        return mittel;
    }

    int price(const std::string& erz) const
    {
        int jetzt = 0, mittel = 0;
        ck::marketOf(erz.c_str(), jetzt, mittel);
        return jetzt;
    }

    int average(const std::string& erz) const
    {
        int jetzt = 0, mittel = 0;
        ck::marketOf(erz.c_str(), jetzt, mittel);
        return mittel;
    }
};

static const CkMarket market;

inline void print(const char* text)        { ck::out(text); }
inline void print(const std::string& text) { ck::out(text.c_str()); }
inline void print(int value)               { ck::out(std::to_string(value).c_str()); }
inline void print(double value)            { ck::out(std::to_string(value).c_str()); }
inline void print(bool value)              { ck::out(value ? "true" : "false"); }
inline void print(const CkSharedValue& v)  { ck::out(std::to_string((int)v).c_str()); }

// print(Stone) soll "Stone" schreiben und nicht die Nummer dahinter.
inline void print(Ore erz)                 { ck::out(CkOreName(erz)); }

// print(info(Gold)) schreibt hin, was man ueber das Erz weiss. nullptr wird zu
// "unknown" - so laesst sich auch das Nichtwissen anschauen.
inline void print(const OreInfo* i)
{
    if (i == nullptr)
    {
        ck::out("unknown");
        return;
    }
    ck::out((std::string(i->name) + " value " + std::to_string(i->value) + ", wants " +
             ((i->care == Cool) ? "Cool" : ((i->care == Heat) ? "Heat" : "nothing")))
                .c_str());
}
inline void print(Care c)
{
    ck::out((c == Cool) ? "Cool" : ((c == Heat) ? "Heat" : "Plain"));
}
)KLICKER";

// ---------------------------------------------------------------------------
// Die Erzliste als enum.
//
// Erze stehen in data/erze.json, kommen aus data/legierungen.json dazu oder
// wuerfelt sich das Spiel selbst aus - im Programm steht davon nichts. Deshalb
// wird dieses Stueck Header bei jedem Start neu gebaut.
// ---------------------------------------------------------------------------

std::string CEscape(const std::string& s)
{
    std::string out;
    for (const char c : s)
    {
        if (c == '\\' || c == '"')
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::string OreEnumSource(const OrePlan& ores)
{
    // Die Namen kommen aus ore.cpp - das Wiki zeigt dieselben an, und beide
    // muessen sich einig sein. Die Nummer im enum ist die Nummer in der
    // Erzliste, deshalb wird nie eines uebersprungen.
    const std::vector<std::string> idents = OreCodeNames(ores);

    std::string out =
        "\n"
        "// Alle Erze, die es gibt. Das ist ein enum - deshalb ohne\n"
        "// Anfuehrungszeichen:\n"
        "//\n"
        "//     item.has(Stone)\n"
        "//     item.sell(Gold, 3)\n"
        "//\n"
        "// Any heisst \"egal was\": item.has(Any) fragt, ob ueberhaupt etwas in\n"
        "// der Tasche liegt, item.sell(Any) verkauft alles.\n"
        "enum Ore\n"
        "{\n"
        "    Any = -1,\n";

    for (std::size_t i = 0; i < idents.size(); ++i)
        out += "    " + idents[i] + " = " + std::to_string(i) + ",\n";

    out +=
        "};\n"
        "\n"
        "// Der Name zum enum. Das Spiel draussen kennt nur Namen, keine Nummern.\n"
        "inline const char* CkOreName(Ore erz)\n"
        "{\n"
        "    switch (erz)\n"
        "    {\n";

    for (std::size_t i = 0; i < idents.size(); ++i)
        out += "    case " + idents[i] + ": return \"" + CEscape(ores.ores[i].name) + "\";\n";

    out +=
        "    default: return \"any\";\n"
        "    }\n"
        "}\n";

    return out;
}

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

// Eine Zeile innerhalb einer eigenen Funktion. Der einzige Unterschied ist der
// Buchstabe: das Spiel rechnet sie mit dem Punkt "inline" nur halb an.
void linef(int console, int n)
{
    std::printf("F %d %d\n", console, n);
    std::fflush(stdout);
    waitForGo();
}

// Eine Antwort abholen, die aus einer ganzen Zahl besteht.
static int readInt()
{
    char buf[128];
    if (!std::fgets(buf, sizeof(buf), stdin))
        std::exit(0);
    return std::atoi(buf);
}

// Eine Antwort mit mehreren Zahlen. Fehlende bleiben, wie sie waren.
static void readInts(int* out, int wie)
{
    char buf[256];
    if (!std::fgets(buf, sizeof(buf), stdin))
        std::exit(0);

    const char* p = buf;
    for (int i = 0; i < wie; ++i)
    {
        while (*p == ' ')
            ++p;
        if (*p == 0 || *p == '\n')
            return;
        out[i] = std::atoi(p);
        while (*p != 0 && *p != ' ' && *p != '\n')
            ++p;
    }
}

void mine(int care)
{
    std::printf("M %d\n", care);
    std::fflush(stdout);
}

// Welches Erz gerade dasteht. Das Spiel antwortet mit einer Zahl, deshalb wird
// hier gewartet - wie bei exists().
int oreHere()
{
    std::printf("B\n");
    std::fflush(stdout);
    char buf[32];
    if (!std::fgets(buf, sizeof(buf), stdin))
        std::exit(0);
    return std::atoi(buf);
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

// ---- Was ueber ein Erz bekannt ist ---------------------------------------

bool oreInfo(int ore, int& value, int& rarity, int& level, int& care)
{
    std::printf("N %d\n", ore);
    std::fflush(stdout);

    int felder[5] = {0, 0, 0, 0, 0};
    readInts(felder, 5);

    if (felder[0] == 0)
        return false;  // noch nicht untersucht

    value  = felder[1];
    rarity = felder[2];
    level  = felder[3];
    care   = felder[4];
    return true;
}

int assayOre(int ore)
{
    std::printf("S %d\n", ore);
    std::fflush(stdout);
    return readInt();
}

// ---- Die Tasche ----------------------------------------------------------

int bagCount(const char* erz)
{
    std::printf("D %s\n", erz ? erz : "");
    std::fflush(stdout);
    return readInt();
}

int bagPurity(const char* erz)
{
    std::printf("E %s\n", erz ? erz : "");
    std::fflush(stdout);
    return readInt();
}

// ---- Die Werkstatt -------------------------------------------------------

void jobState(int& busy, int& idle, int& promille)
{
    std::printf("J\n");
    std::fflush(stdout);

    int felder[3] = {0, 0, 0};
    readInts(felder, 3);

    busy     = felder[0];
    idle     = felder[1];
    promille = felder[2];
}

// ---- Geld, Uhr, Markt ----------------------------------------------------

void status(int& money, int& leftMs, int& target)
{
    std::printf("Y\n");
    std::fflush(stdout);

    int felder[3] = {0, 0, 0};
    readInts(felder, 3);

    money  = felder[0];
    leftMs = felder[1];
    target = felder[2];
}

void marketOf(const char* erz, int& jetzt, int& mittel)
{
    std::printf("R %s\n", erz ? erz : "");
    std::fflush(stdout);

    int felder[2] = {0, 0};
    readInts(felder, 2);

    jetzt  = felder[0];
    mittel = felder[1];
}

// ---- Warten --------------------------------------------------------------
//
// Gewartet wird NICHT hier im Kind, sondern draussen im Spiel: es antwortet
// erst, wenn die Zeit vorbei ist. Nur so steht das Warten auch still, wenn
// jemand das Spiel anhaelt - ein sleep() hier drin liefe stur weiter.
void waitMs(int ms)
{
    if (ms < 0)
        ms = 0;
    std::printf("Z %d\n", ms);
    std::fflush(stdout);
    readInt();
}

int blockLoadingMs()
{
    std::printf("T\n");
    std::fflush(stdout);
    return readInt();
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

// Sucht den Compiler. Steht je System einmal da, siehe unten.
Toolchain BuildToolchain();

// Einmal suchen, fuer alle. Ein static mit Initialisierer ist threadsicher:
// der erste, der hier durchkommt, sucht - alle anderen WARTEN so lange und
// bekommen danach dasselbe Ergebnis.
//
// Frueher stand hier ein "static bool done", und das reichte auch: es gab nur
// einen Uebersetzer. Seit jede Konsole ihr eigenes Programm ist, laufen
// mehrere gleichzeitig - und dann sah der zweite Thread ein done == true,
// waehrend der erste noch mitten in vcvars64.bat steckte. Ergebnis: "Der
// C++-Compiler wurde nicht gefunden", obwohl er da war.
const Toolchain& GetToolchain()
{
    static const Toolchain tc = BuildToolchain();
    return tc;
}

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

// Einmal suchen, dann steht es. Das Ergebnis wird zurueckgegeben und nicht in
// einem Feld abgelegt - siehe GetToolchain() weiter unten: seit jede Konsole
// ihr eigenes Programm ist, laufen mehrere Uebersetzer gleichzeitig, und die
// duerfen sich hier nicht in die Quere kommen.
Toolchain BuildToolchain()
{
    Toolchain tc;

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

Toolchain BuildToolchain()
{
    Toolchain tc;

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
// Setzt die Stuecke EINES Programms zusammen: erst der gemeinsame Vorrat
// (alle Konsolen ohne main), dann die eine mit main. In C++ muss eine Variable
// vor ihrer Benutzung stehen - so sieht das main automatisch alles andere.
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

static Native::Build CompileToExe(const std::vector<SourceFile>& teile, int console,
                                  const std::string& header, int runId,
                                  const std::string& lastKey, const std::string& lastExe,
                                  const std::string& lastDir);

Native::~Native()
{
    stop();

    // Die Uebersetzer laufen auf eigenen Threads und schreiben in Ordner, die
    // gleich verschwinden - abwarten, bevor hier alles wegfaellt.
    for (std::future<Build>& f : mBuilds)
        if (f.valid())
            f.wait();
}

void Native::start(const std::vector<SourceFile>& files, const OrePlan& ores)
{
    stop();

    mErrConsole = 0;
    mErrLine    = 0;

    // Aufteilen: was ein main() hat, wird ein eigenes Programm. Der Rest ist
    // gemeinsamer Vorrat und wandert in jedes davon.
    std::vector<SourceFile> vorrat;
    std::vector<SourceFile> starter;

    for (const SourceFile& f : files)
    {
        if (ContainsMainFunction(f.code))
            starter.push_back(f);
        else
            vorrat.push_back(f);
    }

    if (starter.empty())
    {
        // Frueher kam das als Uebersetzungsfehler zurueck. Es ist aber keiner -
        // es fehlt schlicht ein Anfang, und das kann man sofort sagen.
        mPhase = Phase::Failed;
        mMsg = "No console has an  int main() { ... }  - that is where a program starts.";
        return;
    }

    mMsg   = (starter.size() > 1) ? ("compiling " + std::to_string(starter.size()) + " programs ...")
                                  : std::string("compiling ...");
    mPhase = Phase::Compiling;

    static int counter = 0;
    const int  id      = ++counter;

    // Die Erzliste kann sich zwischen zwei Laeufen aendern - das Spiel wuerfelt
    // ja neue Erze aus. Deshalb wird der Header hier gebaut und wandert mit in
    // den Vergleich: sonst startete das Spiel eine .exe mit einer alten Liste
    // noch einmal.
    const std::string header =
        std::string(kHeaderTop) + OreEnumSource(ores) + kHeaderBottom + kHeaderBottom2;

    // Je Programm ein Bauauftrag, und alle nebeneinander: nacheinander wuerde
    // das Uebersetzen bei drei Konsolen dreimal so lange dauern, und so lange
    // steht der Spieler vor einem Knopf, der nichts tut.
    for (const SourceFile& haupt : starter)
    {
        std::vector<SourceFile> teile = vorrat;
        teile.push_back(haupt);

        const int   konsole = haupt.id;
        const auto  it      = mCache.find(konsole);
        std::string lastKey, lastExe, lastDir;
        if (it != mCache.end())
        {
            lastKey = it->second.key;
            lastExe = it->second.exe;
            lastDir = it->second.dir;
        }

        mBuilds.push_back(std::async(std::launch::async,
                                     [teile, konsole, header, id, lastKey, lastExe, lastDir] {
                                         return CompileToExe(teile, konsole, header, id, lastKey,
                                                             lastExe, lastDir);
                                     }));
    }
}

void Native::fail(const std::string& message, int console, int line)
{
    stop();
    mPhase      = Phase::Failed;
    mMsg        = message;
    mErrConsole = console;
    mErrLine    = line;
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
    closeAll();
    if (mPhase == Phase::Running || mPhase == Phase::Paused || mPhase == Phase::Compiling)
        mPhase = Phase::Idle;
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

// Irgendeine laufende Stelle - fuer den Absturzbericht. Fuer die Markierung im
// Editor ist lineIn() das Richtige: dort laufen ja mehrere gleichzeitig.
int Native::currentConsole() const
{
    for (const std::unique_ptr<Proc>& proc : mProcs)
        if (!proc->done && proc->line > 0)
            return proc->atConsole;
    return 0;
}

int Native::currentLine() const
{
    for (const std::unique_ptr<Proc>& proc : mProcs)
        if (!proc->done && proc->line > 0)
            return proc->line;
    return 0;
}

int Native::lineIn(int console) const
{
    for (const std::unique_ptr<Proc>& proc : mProcs)
        if (!proc->done && proc->atConsole == console)
            return proc->line;
    return 0;
}

bool Native::anyRunning() const
{
    for (const std::unique_ptr<Proc>& proc : mProcs)
        if (!proc->done)
            return true;
    return false;
}

void Native::closeAll()
{
    for (std::unique_ptr<Proc>& proc : mProcs)
        proc->child.close();
    mProcs.clear();
}

void Native::finish(const char* reason)
{
    mPhase = Phase::Done;
    if (mMsg.empty() || mMsg == "running" || mMsg.rfind("compiling", 0) == 0)
        mMsg = reason;
    closeAll();
}

void Native::sendTo(Proc& proc, const char* text)
{
    proc.child.write(text);
}

void Native::pump(Proc& proc, World& world, const OrePlan& ores, const CraftPlan& craft,
                  const AlloyPlan& alloys, const Limits& limits)
{
    if (!proc.child.started())
        return;

    // Holen, was da ist - read() bleibt nicht stehen, wenn gerade nichts kommt.
    char        buf[2048];
    std::size_t got = 0;
    while ((got = proc.child.read(buf, sizeof(buf))) > 0)
        proc.pending.append(buf, got);

    std::size_t nl;
    while ((nl = proc.pending.find('\n')) != std::string::npos)
    {
        std::string entry = proc.pending.substr(0, nl);
        proc.pending.erase(0, nl + 1);
        while (!entry.empty() && entry.back() == '\r')
            entry.pop_back();
        handle(proc, entry, world, ores, craft, alloys, limits);
    }
}

void Native::handle(Proc& proc, const std::string& msg, World& world, const OrePlan& ores,
                    const CraftPlan& craft, const AlloyPlan& alloys, const Limits& limits)
{
    if (msg.empty())
        return;

    switch (msg[0])
    {
    case 'L':  // "L konsole zeile"
    case 'F':  // dasselbe, aber in einer eigenen Funktion - siehe proc.lineCost
    {
        const char* p   = msg.c_str() + 1;
        proc.atConsole  = std::atoi(p);
        const std::size_t sep = msg.rfind(' ');
        proc.line       = (sep != std::string::npos) ? std::atoi(msg.c_str() + sep + 1) : 0;
        proc.awaitingGo = true;

        // Der Punkt "inline" macht Zeilen in eigenen Funktionen billiger.
        // Ohne ihn kostet drinnen wie draussen dasselbe - und dann ist eine
        // Funktion langsamer als derselbe Code zweimal hingeschrieben.
        proc.lineCost = (msg[0] == 'F') ? limits.insideFunctionCost() : 1.0f;
        break;
    }

    case 'M':  // abbauen:  M <behandlung>
    {
        int womit = (msg.size() > 2) ? std::atoi(msg.c_str() + 2) : 0;
        if (womit < 0 || womit >= (int)BlockCare::Count)
            womit = 0;

        if (!world.mine((BlockCare)womit))
        {
            mMsg = "The block is already mined.";
        }
        else if ((BlockCare)womit != world.care)
        {
            // Falsch behandelt sieht man dem Block nicht an - er kommt genauso
            // schnell heraus, nur schmutziger. Deshalb steht es in der Zeile
            // unter der Konsole. Der Name des Erzes gehoert dazu: was es will,
            // haengt am Erz, und genau das soll man sich merken.
            const std::string erz = (world.ore >= 0 && world.ore < (int)ores.ores.size())
                                        ? ores.ores[(std::size_t)world.ore].name
                                        : std::string("This block");

            mMsg = std::string("Mining with ") + BlockCareName((BlockCare)womit) + ", but " + erz +
                   " wants " + BlockCareName(world.care) + " - purity -" +
                   std::to_string(CareLoss(ores.care, world.care, (BlockCare)womit)) + "%.";
        }
        else
        {
            mMsg = "Block mined.";
        }
        break;
    }

    case 'Q':
        sendTo(proc, world.blockAlive ? "1\n" : "0\n");
        break;

    case 'B':  // block.ore() - welches Erz steht gerade da? -1 = keins
        sendTo(proc, (std::to_string(world.blockAlive ? world.ore : -1) + "\n").c_str());
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

        sendTo(proc, (std::to_string(geld) + "\n").c_str());
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

        sendTo(proc, (std::to_string(wie) + "\n").c_str());

        // Wie der Schritt heisst, weiss der Plan. Frueher stand es in der Welt -
        // aber jetzt laufen mehrere Auftraege, und keiner davon ist "der" Auftrag.
        const CraftStep*  schritt   = (a != std::string::npos)
                                          ? craft.find(rest.substr(0, a))
                                          : nullptr;
        const std::string wieHeisst = (schritt != nullptr) ? schritt->name
                                                           : std::string("Job");

        mMsg = (wie > 0) ? (wieHeisst + ": " + std::to_string(wie) + " pieces.")
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

        sendTo(proc, (std::to_string(wie) + "\n").c_str());
        mMsg = (wie > 0) ? ("Alloying: " + std::to_string(wie) + " pieces.")
                         : std::string("Alloying did not work.");
        break;
    }

    case 'P':  // item.canAlloy(...) - wie viele gingen gerade?
    {
        const std::string name = (msg.size() > 2) ? msg.substr(2) : std::string();
        sendTo(proc, (std::to_string(world.canAlloy(ores, alloys, limits, name)) + "\n").c_str());
        break;
    }

    case 'I':  // item.has(...) - in die Tasche schauen
    {
        const std::string erz = (msg.size() > 2) ? msg.substr(2) : std::string();
        sendTo(proc, (std::to_string(world.inventoryOf(ores, erz)) + "\n").c_str());
        break;
    }

    case 'N':  // info(erz):  N <nummer>  ->  "bekannt wert seltenheit level behandlung"
    {
        const int nummer = (msg.size() > 2) ? std::atoi(msg.c_str() + 2) : -1;

        if (!world.knowsOre(ores, nummer))
        {
            sendTo(proc, "0 0 0 0 0\n");
            break;
        }

        const Ore& erz = ores.ores[(std::size_t)nummer];
        char       antwort[96];
        std::snprintf(antwort, sizeof(antwort), "1 %d %d %d %d\n", erz.value,
                      (int)(erz.rarity + 0.5f), erz.minLevel, (int)OreCare(ores, nummer));
        sendTo(proc, antwort);
        break;
    }

    case 'S':  // assay(erz):  S <nummer>  ->  was es gekostet hat
    {
        const int nummer  = (msg.size() > 2) ? std::atoi(msg.c_str() + 2) : -1;
        const int bezahlt = world.startAssay(ores, nummer, limits.assayCost, limits.assaySeconds);

        sendTo(proc, (std::to_string(bezahlt) + "\n").c_str());

        if (bezahlt > 0)
            mMsg = "Examining " + OreOf(ores, nummer).name + " ...";
        else if (world.knowsOre(ores, nummer))
            mMsg = OreOf(ores, nummer).name + " is already known.";
        else if (world.assaying)
            mMsg = "An examination is already running.";
        else
            mMsg = "Not enough money to examine that.";
        break;
    }

    case 'D':  // item.count(erz)
    {
        const std::string erz = (msg.size() > 2) ? msg.substr(2) : std::string();
        sendTo(proc, (std::to_string(world.inventoryOf(ores, erz)) + "\n").c_str());
        break;
    }

    case 'E':  // item.purity(erz)
    {
        const std::string erz = (msg.size() > 2) ? msg.substr(2) : std::string();
        sendTo(proc, (std::to_string(world.inventoryPurity(ores, erz)) + "\n").c_str());
        break;
    }

    case 'J':  // job.busy() / idle() / progress()
    {
        const World::Job* naechster = world.nextDone();
        const int         promille =
            (naechster != nullptr) ? (int)(naechster->progress() * 1000.0f) : 0;

        char antwort[64];
        std::snprintf(antwort, sizeof(antwort), "%d %d %d\n", world.jobsRunning(),
                      world.jobsIdle(), promille);
        sendTo(proc, antwort);
        break;
    }

    case 'Y':  // money() / round.left() / round.target()
    {
        char antwort[96];
        std::snprintf(antwort, sizeof(antwort), "%d %d %d\n", world.money,
                      (int)(world.roundLeft * 1000.0f), world.roundTargetNow);
        sendTo(proc, antwort);
        break;
    }

    case 'R':  // market.price(erz) und market.average(erz)
    {
        const std::string name   = (msg.size() > 2) ? msg.substr(2) : std::string();
        const int         nummer = FindOre(ores, name);

        if (nummer < 0)
        {
            sendTo(proc, "0 0\n");
            break;
        }

        // Ein rohes Stueck bei voller Reinheit. Verarbeitet ist es mehr wert,
        // aber der Ausschlag ist derselbe - und so ist die Zahl zwischen zwei
        // Erzen vergleichbar.
        const int jetzt  = StackValue(ores, craft, nummer, (int)OreState::Raw, 100, 1,
                                      world.moneyPerBlock, world.marketFactor(nummer));
        const int mittel = StackValue(ores, craft, nummer, (int)OreState::Raw, 100, 1,
                                      world.moneyPerBlock, 1.0f);

        char antwort[64];
        std::snprintf(antwort, sizeof(antwort), "%d %d\n", jetzt, mittel);
        sendTo(proc, antwort);
        break;
    }

    case 'Z':  // wait(sekunden):  Z <millisekunden>
    {
        const int ms = (msg.size() > 2) ? std::atoi(msg.c_str() + 2) : 0;

        // Nicht hier antworten: das macht update(), wenn die Zeit um ist.
        // Solange haengt das Kind an seinem fgets - und weil die Zeit im Spiel
        // gezaehlt wird, steht auch das Warten, wenn das Spiel steht.
        proc.waiting  = true;
        proc.waitLeft = (float)ms / 1000.0f;

        if (proc.waitLeft <= 0.0f)
        {
            proc.waiting = false;
            sendTo(proc, "1\n");
        }
        break;
    }

    case 'T':  // block.loading() - wie lange noch, bis er wieder dasteht
    {
        const float rest = world.blockAlive ? 0.0f : world.respawnTimer;
        sendTo(proc, (std::to_string((int)(rest * 1000.0f)) + "\n").c_str());
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
        sendTo(proc, (std::to_string(value) + "\n").c_str());
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
        // Nur DIESES Programm ist durch. Die anderen laufen weiter - erst wenn
        // das letzte fertig ist, ist der Lauf zu Ende (siehe update).
        proc.done       = true;
        proc.line       = 0;
        proc.awaitingGo = false;
        proc.waiting    = false;
        proc.child.close();
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
        if (mBuilds.empty())
        {
            mPhase = Phase::Idle;
            return;
        }

        // Erst weiter, wenn ALLE fertig sind. Die Programme sollen gemeinsam
        // losgehen - eines, das schon eine Sekunde vor den anderen abbaut,
        // waere ein Vorsprung, den niemand gewollt hat.
        for (std::future<Build>& f : mBuilds)
        {
            if (!f.valid())
            {
                mBuilds.clear();
                mPhase = Phase::Idle;
                return;
            }
            if (f.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
        }

        std::vector<Build> builds;
        for (std::future<Build>& f : mBuilds)
            builds.push_back(f.get());
        mBuilds.clear();

        // Hakt es an einem, wird gar nichts gestartet. Ein halb laufendes
        // Programm waere schlimmer als keines: der Spieler sucht dann den
        // Fehler in der falschen Konsole.
        for (const Build& build : builds)
        {
            if (!build.ok)
            {
                mPhase      = Phase::Failed;
                mErrConsole = build.errorConsole;
                mErrLine    = build.errorLine;
                mMsg        = build.error;
                return;
            }
        }

        for (const Build& build : builds)
        {
            std::unique_ptr<Proc> proc(new Proc());
            proc->console = build.console;

            if (!proc->child.start(build.exe, build.dir))
            {
                closeAll();
                mPhase = Phase::Failed;
                mMsg   = "Console " + std::to_string(build.console) +
                       ": the program could not be started.";
                return;
            }

            // Die erste Zeile sofort freigeben, sonst haengt jedes Programm
            // erst einmal einen Takt lang in der Luft.
            proc->budget = 1.0f;
            mProcs.push_back(std::move(proc));

            Cached& c = mCache[build.console];
            c.key     = build.cacheKey;
            c.exe     = build.exe;
            c.dir     = build.dir;
        }

        mPhase = Phase::Running;
        mMsg   = "running";
        return;
    }

    if (mPhase != Phase::Running && mPhase != Phase::Paused)
        return;

    for (std::unique_ptr<Proc>& up : mProcs)
    {
        Proc& proc = *up;
        if (proc.done)
            continue;

        pump(proc, world, ores, craft, alloys, limits);

        if (mPhase == Phase::Running)
        {
            // wait(...) laeuft ab, bevor irgendeine Zeile drankommt. Steht die
            // Welt still, laeuft auch das Warten nicht weiter - deshalb wird
            // hier gezaehlt und nicht im Kind.
            if (proc.waiting)
            {
                if (!world.frozen)
                    proc.waitLeft -= dt;

                if (proc.waitLeft <= 0.0f)
                {
                    proc.waiting  = false;
                    proc.waitLeft = 0.0f;
                    sendTo(proc, "1\n");
                }
            }

            // Jedes Programm hat sein EIGENES Budget. Zwei Konsolen arbeiten
            // damit wirklich gleichzeitig und nicht abwechselnd - dafuer ist
            // "+1 Konsole" im Baum teuer und selten.
            proc.budget += dt * mLinesPerSecond;

            // Hoechstens eine Zeile auf Vorrat. Sonst spart das Spiel waehrend
            // einer Pause oder beim Kompilieren Zeilen an und feuert sie danach
            // auf einen Schlag ab - das sieht aus, als holte es etwas nach.
            if (proc.budget > 1.0f)
                proc.budget = 1.0f;

            if (proc.awaitingGo && proc.budget >= proc.lineCost)
            {
                proc.budget -= proc.lineCost;
                proc.awaitingGo = false;
                sendTo(proc, "g\n");
            }
        }

        // Ist das Kind weg? Dann noch einmal nachsehen, ob in der Roehre etwas
        // liegt - das letzte print() soll nicht verlorengehen.
        if (proc.child.started() && !proc.child.alive())
        {
            pump(proc, world, ores, craft, alloys, limits);

            const unsigned long code = proc.child.exitCode();
            if (code == 0)
            {
                proc.done       = true;
                proc.line       = 0;
                proc.awaitingGo = false;
                proc.child.close();
            }
            else
            {
                // Die Zeile, an der es passiert ist, kennen wir noch: das Kind
                // meldet ja jede Zeile, BEVOR es sie ausfuehrt. Deshalb wird
                // sie hier gerettet, bevor die laufende Anzeige geloescht wird -
                // sonst stuende der Spieler vor einem Absturz ohne jeden
                // Anhaltspunkt.
                mErrConsole = proc.atConsole;
                mErrLine    = proc.line;

                // Ein Absturz beendet den ganzen Lauf, nicht nur dieses eine
                // Programm. Die anderen arbeiten an derselben Welt weiter, und
                // dabei zuzusehen, waehrend eines abgestuerzt ist, hilft
                // niemandem beim Suchen.
                mMsg   = "Console " + std::to_string(proc.console) + ": " + CrashText(code);
                mPhase = Phase::Failed;
                closeAll();
                return;
            }
        }
    }

    // Erst wenn das letzte main() durch ist, ist der Lauf zu Ende.
    if ((mPhase == Phase::Running || mPhase == Phase::Paused) && !mProcs.empty() && !anyRunning())
        finish("Done.");
}

// ---------------------------------------------------------------------------

// Uebersetzt EIN Programm: den gemeinsamen Vorrat plus die eine Konsole, die
// das main() mitbringt. Wie viele Programme es gibt, entscheidet Native::start -
// hier geht es nur noch um dieses eine.
static Native::Build CompileToExe(const std::vector<SourceFile>& teile, int console,
                                  const std::string& header, int runId,
                                  const std::string& lastKey, const std::string& lastExe,
                                  const std::string& lastDir)
{
    Native::Build build;
    build.console = console;

    // Hat sich seit dem letzten Mal nichts geaendert? Dann das fertige
    // Programm einfach noch einmal starten. Beim Klicken am Spielanfang ist
    // genau das der Normalfall.
    //
    // Gemerkt wird das je Konsole: aendert man nur eine von dreien, wird auch
    // nur die eine neu uebersetzt.
    //
    // Der Header steht mit im Vergleich, sonst zaehlte eine neue Erzliste als
    // "hat sich nichts geaendert".
    build.combined = CombineSources(teile);

    const std::string key = header + build.combined;

    build.cacheKey = key;

    if (!lastExe.empty() && key == lastKey && FileExists(lastExe))
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

    // Jedes Programm braucht seinen eigenen Ordner - sonst schreiben zwei
    // Uebersetzer gleichzeitig dieselbe run.cpp, und das Ergebnis ist Zufall.
    const std::string dir =
        base + "r" + std::to_string(runId) + "_" + std::to_string(console) + Sep();
    MakeDir(dir);
    build.dir = dir;

    const std::string headerPath = dir + "klicker.h";

    if (!WriteTextFile(headerPath, header) ||
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
