#pragma once

#include "engine.h"
#include "proc.h"

#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Stufe 2: echtes C++.
//
// Der Code wird instrumentiert (ck::line), mit dem C++-Compiler des Systems
// (cl.exe bzw. g++) uebersetzt und als eigener Prozess gestartet.
//
// JEDE KONSOLE MIT EINEM EIGENEN main() WIRD EIN EIGENES PROGRAMM.
//
// Frueher wurden alle Konsolen zu einer einzigen run.cpp zusammengeklebt, und
// genau eine durfte ein main() haben. Ein zweites Fenster war damit nur ein
// zweites Fenster - am Durchsatz aenderte es nichts.
//
// Jetzt laeuft jede davon als eigener Prozess mit eigenem Zeilenbudget: eine
// baut ab, die naechste verarbeitet, die dritte verkauft. Konsolen OHNE main()
// sind gemeinsamer Vorrat und werden in jedes dieser Programme mit
// hineinuebersetzt - jeder Prozess bekommt davon aber seine EIGENE Kopie.
// Etwas wirklich Geteiltes gibt es nur ueber shared[...], denn das liegt im
// Spiel und nicht im Prozess.
//
// Verstaendigt wird sich mit jedem Kind ueber zwei Pipes:
//
//   Kind  -> Spiel   L 2 12 Konsole 2, Zeile 12 (wartet dann auf Freigabe)
//                    F 2 12 dasselbe, aber in einer eigenen Funktion
//                    M      block.mine()
//                    Q      block.exists()  (wartet auf 1 oder 0)
//                    C ...  item.wash(...) und die anderen Schritte
//                           (wartet auf die Anzahl, die angenommen wurde)
//                    G ...  item.alloy(...)   - legieren
//                    P ...  item.canAlloy(...) - wie viele gingen gerade?
//                    N ...  info(erz)   S ... assay(erz)
//                    D ...  item.count()   E ... item.purity()
//                    J      job.busy()/idle()/progress()
//                    Y      money()/timeLeft()/roundTarget()
//                    R ...  market.price()
//                    Z ...  wait(...)  (wartet auf die Antwort)
//                    T      block.loading()
//                    O text print("text")
//                    X      main() ist zu Ende
//
//   Spiel -> Kind    g      Freigabe fuer die naechste Zeile
//                    1 / 0  Antwort auf Q
//
// Weil das Kind vor jeder Zeile auf die Freigabe wartet, ist Pause einfach
// "keine Freigabe schicken" - und eine Endlosschleife laesst sich jederzeit
// beenden, indem der Prozess abgeschossen wird.
class Native final : public Engine
{
public:
    // Startgeschwindigkeit in Zeilen pro Sekunde. Der Skilltree erhoeht sie.
    static constexpr float kBaseLinesPerSecond = 10.0f;

    // Ergebnis eines Uebersetzungslaufs (oeffentlich, weil er auf einem
    // eigenen Thread erzeugt wird).
    struct Build
    {
        bool        ok = false;
        int         console = 0;  // welche Konsole das main() dazu hat
        std::string exe;
        std::string dir;
        std::string error;
        int          errorConsole = 0;
        int          errorLine    = 0;
        std::string  combined;  // der zusammengesetzte Quelltext
        std::string  cacheKey;  // Quelltext UND Header - siehe Native::start
        bool         reused = false;
    };

    ~Native() override;

    void start(const std::vector<SourceFile>& files, const OrePlan& ores) override;
    void togglePause() override;
    void stop() override;
    void update(float dt, World& world, const OrePlan& ores, const CraftPlan& craft,
                const AlloyPlan& alloys, const Limits& limits) override;
    void setSpeed(float linesPerSecond) override { mLinesPerSecond = linesPerSecond; }
    void fail(const std::string& message, int console, int line) override;

    RunState           state() const override;
    int                currentConsole() const override;
    int                currentLine() const override;
    int                lineIn(int console) const override;
    int                errorConsole() const override { return mErrConsole; }
    int                errorLine() const override { return mErrLine; }
    const std::string& message() const override { return mMsg; }

private:
    enum class Phase
    {
        Idle,
        Compiling,
        Running,
        Paused,
        Done,
        Failed
    };

    // Ein laufendes Programm - also eine Konsole, die ein main() hat, samt
    // allem, was zu ihrem Takt gehoert.
    //
    // Jedes hat sein EIGENES Zeilenbudget. Zwei Konsolen arbeiten damit
    // wirklich gleichzeitig und nicht abwechselnd - das ist der ganze Sinn der
    // Sache, und deshalb ist "+1 Konsole" im Baum ein teurer und seltener
    // Punkt geworden.
    struct Proc
    {
        int   console = 0;  // die Konsole, deren main() hier laeuft
        Child child;

        std::string pending;           // angefangene Zeile aus der Pipe
        bool        awaitingGo = false;  // Kind wartet auf Freigabe
        float       budget     = 0.0f;   // aufgelaufene Zeilen-Freigaben

        // Was die Zeile kostet, auf die dieses Kind gerade wartet. Normal 1,
        // mit dem Punkt "inline" nur die Haelfte, wenn sie in einer eigenen
        // Funktion steht - dafuer schickt das Kind ein F statt eines L.
        float lineCost = 1.0f;

        // wait(...): das Kind haengt an einer Antwort, die erst kommt, wenn die
        // Zeit um ist. Gezaehlt wird hier und nicht im Kind, damit ein
        // angehaltenes Spiel auch das Warten anhaelt.
        bool  waiting  = false;
        float waitLeft = 0.0f;

        // Wo es gerade steht. atConsole muss nicht console sein: ruft das
        // Programm eine Funktion auf, die in einer Vorratskonsole steht,
        // wandert die Markierung dorthin.
        int atConsole = 0;
        int line      = 0;

        bool done = false;  // main() ist durch
    };

    // Eine fertige .exe von vorhin. Hat sich an einer Konsole nichts geaendert,
    // wird sie einfach noch einmal gestartet, statt neu zu uebersetzen.
    struct Cached
    {
        std::string key;  // Quelltext UND Header
        std::string exe;
        std::string dir;
    };

    Phase mPhase = Phase::Idle;

    // Ein Bauauftrag je Konsole mit main(). Sie laufen nebeneinander, sonst
    // wuerde das Uebersetzen bei drei Konsolen dreimal so lange dauern.
    std::vector<std::future<Build>> mBuilds;

    // Die laufenden Programme. unique_ptr, weil ein Child weder kopiert noch
    // verschoben werden kann - es haelt Handles.
    std::vector<std::unique_ptr<Proc>> mProcs;

    // Was zuletzt gebaut wurde, je Konsole.
    std::map<int, Cached> mCache;

    float mLinesPerSecond = kBaseLinesPerSecond;

    int         mErrConsole = 0;
    int         mErrLine    = 0;
    std::string mMsg;

    void pump(Proc& proc, World& world, const OrePlan& ores, const CraftPlan& craft,
              const AlloyPlan& alloys, const Limits& limits);
    void handle(Proc& proc, const std::string& msg, World& world, const OrePlan& ores,
                const CraftPlan& craft, const AlloyPlan& alloys, const Limits& limits);
    void sendTo(Proc& proc, const char* text);

    void closeAll();
    void finish(const char* reason);

    // Laeuft ueberhaupt noch eines? Sind alle durch, ist der Lauf zu Ende.
    bool anyRunning() const;
};
