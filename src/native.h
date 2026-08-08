#pragma once

#include "engine.h"
#include "proc.h"

#include <future>
#include <string>

// Stufe 2: echtes C++.
//
// Der Code wird instrumentiert (ck::line), mit cl.exe zu einer run.exe
// kompiliert und als eigener Prozess gestartet. Verstaendigung ueber zwei
// Pipes:
//
// ALLE Konsolen werden zu EINEM Programm zusammengesetzt. Deshalb sieht
// Konsole 1 auch eine Variable, die in Konsole 2 steht - genau wie mehrere
// Dateien eines normalen C++-Projekts.
//
//   Kind  -> Spiel   L 2 12 Konsole 2, Zeile 12 (wartet dann auf Freigabe)
//                    M      block.mine()
//                    S      block.place()
//                    Q      block.exists()  (wartet auf 1 oder 0)
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
        bool         ok = false;
        std::wstring exe;
        std::wstring dir;
        std::string  error;
        int          errorConsole = 0;
        int          errorLine    = 0;
        std::string  combined;  // der zusammengesetzte Quelltext
        bool         reused = false;
    };

    ~Native() override;

    void start(const std::vector<SourceFile>& files) override;
    void togglePause() override;
    void stop() override;
    void update(float dt, World& world, const OrePlan& ores) override;
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
    std::string  mLastCombined;
    std::wstring mLastExe;
    std::wstring mLastDir;

    HANDLE mProcess  = nullptr;
    HANDLE mFromChild = nullptr;  // Leseende: Ausgabe des Kindes
    HANDLE mToChild   = nullptr;  // Schreibende: Freigaben an das Kind

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
    void pump(World& world, const OrePlan& ores);
    void handle(const std::string& msg, World& world, const OrePlan& ores);
    void sendChild(const char* text);
    void closeChild();
    void finish(const char* reason);
};
