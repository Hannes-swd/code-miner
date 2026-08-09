#pragma once

#include <string>
#include <vector>

struct World;
struct OrePlan;
struct CraftPlan;
struct AlloyPlan;
struct Limits;

// Eine Konsole als Quelltext-Stueck. Alle Konsolen zusammen ergeben EIN
// Programm - deshalb sieht Konsole 1 auch die Variablen aus Konsole 2.
struct SourceFile
{
    int         id = 0;  // Nummer der Konsole
    std::string code;
};

enum class RunState
{
    Idle,     // noch nichts gestartet
    Running,  // laeuft
    Paused,   // angehalten
    Done,     // durchgelaufen
    Failed    // Fehler im Code
};

// Die Schnittstelle zwischen Oberflaeche und Ausfuehrung.
class Engine
{
public:
    virtual ~Engine() = default;

    virtual void start(const std::vector<SourceFile>& files) = 0;
    virtual void togglePause()                               = 0;
    virtual void stop()                                      = 0;
    // Die Erzliste braucht der Motor fuers Verkaufen: item.sell() muss
    // wissen, was ein Block wert ist. Verarbeitung, Legierungen und Limits
    // kommen dazu, weil item.wash() und item.alloy() beides brauchen: was
    // moeglich ist und was gekauft ist.
    virtual void update(float dt, World& world, const OrePlan& ores, const CraftPlan& craft,
                        const AlloyPlan& alloys, const Limits& limits) = 0;

    // Tempo in Zeilen pro Sekunde. Der Skilltree dreht daran.
    virtual void setSpeed(float linesPerSecond) = 0;

    // Ablehnen, ohne ueberhaupt zu kompilieren - z. B. weil im Code etwas
    // steht, das noch nicht freigeschaltet ist.
    virtual void fail(const std::string& message, int console, int line) = 0;

    virtual RunState state() const = 0;

    // Wo laeuft es gerade? 0 = nirgends.
    virtual int currentConsole() const = 0;
    virtual int currentLine() const    = 0;

    // Wo steckt der Fehler? 0 = keiner.
    virtual int errorConsole() const = 0;
    virtual int errorLine() const    = 0;

    virtual const std::string& message() const = 0;

    bool busy() const
    {
        return state() == RunState::Running || state() == RunState::Paused;
    }
};
