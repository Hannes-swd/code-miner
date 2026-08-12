#pragma once

#include "engine.h"
#include "proc.h"

#include <future>
#include <string>

// Stufe 2: echtes C++.
//
// Der Code wird instrumentiert (ck::line), mit dem C++-Compiler des Systems
// (cl.exe bzw. g++) uebersetzt und als eigener Prozess gestartet. Verstaendigung ueber zwei
// Pipes:
//
// ALLE Konsolen werden zu EINEM Programm zusammengesetzt. Deshalb sieht
// Konsole 1 auch eine Variable, die in Konsole 2 steht - genau wie mehrere
// Dateien eines normalen C++-Projekts.
//
//   Kind  -> Spiel   L 2 12 Konsole 2, Zeile 12 (wartet dann auf Freigabe)
//                    M      block.mine()
//                    Q      block.exists()  (wartet auf 1 oder 0)
//                    C ...  item.wash(...) und die anderen Schritte
//                           (wartet auf die Anzahl, die angenommen wurde)
//                    G ...  item.alloy(...)   - legieren
//                    P ...  item.canAlloy(...) - wie viele gingen gerade?
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
    int                currentConsole() const override { return mConsole; }
    int                currentLine() const override { return mLine; }
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

    Phase              mPhase = Phase::Idle;
    std::future<Build> mBuild;

    // Beim Klicker drueckt man am Anfang immer wieder Play, ohne etwas zu
    // aendern. Dann wird nicht neu uebersetzt, sondern die fertige run.exe
    // noch einmal gestartet - das macht aus 400 ms ein sofort.
    std::string mLastCombined;
    std::string mLastExe;
    std::string mLastDir;

    // Das laufende Programm des Spielers samt seinen beiden Roehren. Wie das
    // auf dem jeweiligen System gemacht wird, steht in proc_win.cpp bzw.
    // proc_posix.cpp - hier ist es einfach ein Kind.
    Child mChild;

    std::string mPending;                             // angefangene Zeile aus der Pipe
    bool        mAwaitingGo     = false;              // Kind wartet auf Freigabe
    float       mBudget         = 0.0f;               // aufgelaufene Zeilen-Freigaben
    float       mLinesPerSecond = kBaseLinesPerSecond;

    int         mConsole    = 0;  // in welcher Konsole laeuft es gerade
    int         mLine       = 0;
    int         mErrConsole = 0;
    int         mErrLine    = 0;
    std::string mMsg;

    bool launch(const Build& build);
    void pump(World& world, const OrePlan& ores, const CraftPlan& craft, const AlloyPlan& alloys,
              const Limits& limits);
    void handle(const std::string& msg, World& world, const OrePlan& ores, const CraftPlan& craft,
                const AlloyPlan& alloys, const Limits& limits);
    void sendChild(const char* text);
    void closeChild();
    void finish(const char* reason);
};
