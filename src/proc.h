#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Alles, was das Spiel vom Betriebssystem braucht: Dateien schreiben, ein
// Hilfsprogramm aufrufen, das Programm des Spielers starten.
//
// Hier steht nur, WAS geht - nicht wie. Das Wie steht in proc_win.cpp bzw.
// proc_posix.cpp. Dadurch ist der Rest des Spiels frei von <windows.h>: der
// Unterschied zwischen den Systemen sitzt an dieser einen Stelle.
//
// Pfade und Texte sind ueberall UTF-8 in einem std::string. Unter Windows wird
// erst ganz unten nach UTF-16 umgesetzt.

// ---- Dateien und Ordner ---------------------------------------------------

bool WriteTextFile(const std::string& path, const std::string& text);
bool FileExists(const std::string& path);
void MakeDir(const std::string& path);

// Ordner, in dem die eigene Programmdatei liegt - ohne Trenner am Ende.
// Die Spieldaten (data/...) werden von dort aus gesucht.
std::string ExeDir();

// Arbeitsordner im Temp-Verzeichnis, MIT Trenner am Ende.
std::string WorkDir();

// Der Trenner des Systems: "\\" oder "/".
const char* Sep();

// Wie eine ausfuehrbare Datei hier heisst: ".exe" oder "" (leer).
const char* ExeSuffix();

// ---- Ein Hilfsprogramm aufrufen und die Ausgabe einsammeln ----------------

// Sucht ein Programm im PATH. Leer, wenn es nicht da ist.
std::string FindInPath(const std::string& program);

// Startet argv[0] mit den restlichen Argumenten, wartet auf das Ende und
// sammelt stdout+stderr ein. Rueckgabe: true bei Exitcode 0.
//
// Die Argumente stehen einzeln in der Liste und werden NICHT von einer Shell
// angefasst - Leerzeichen in Pfaden sind damit von selbst richtig.
//
// env: die komplette Umgebung als "NAME=WERT". Leer = die eigene erben.
bool RunCapture(const std::vector<std::string>& argv, const std::string& workdir, std::string& out,
                const std::vector<std::string>* env = nullptr, int timeoutMs = 60000);

// ---- Das Programm des Spielers --------------------------------------------
//
// Es laeuft als eigener Prozess und redet ueber zwei Roehren mit dem Spiel:
// es schreibt, was es tun will, und wartet auf die Freigabe fuer die naechste
// Zeile. Deshalb muss read() zurueckkommen, auch wenn gerade nichts da ist -
// sonst stuende das Bild still.

struct ChildImpl;  // steht in proc_win.cpp bzw. proc_posix.cpp

class Child
{
public:
    Child() = default;
    ~Child();

    Child(const Child&)            = delete;
    Child& operator=(const Child&) = delete;

    bool start(const std::string& exe, const std::string& workdir);

    // Holt, was da ist, und gibt die Anzahl Zeichen zurueck. 0 heisst nur
    // "gerade nichts" - nicht "zu Ende".
    std::size_t read(char* buf, std::size_t size);

    void write(const char* text);

    // Laeuft es noch? Ist es fertig, sagt exitCode(), wie es ausging.
    bool alive();

    bool started() const { return mImpl != nullptr; }

    // Abschiessen und aufraeumen. Darf immer gerufen werden.
    void close();

    // Was beim Ende herauskam. Windows: der Rueckgabewert bzw. der
    // Ausnahmecode. Linux: der Rueckgabewert, oder 256 + Signalnummer, wenn
    // ein Signal das Programm umgebracht hat.
    unsigned long exitCode() const { return mExit; }

private:
    ChildImpl*    mImpl = nullptr;
    unsigned long mExit = 0;
};

// Warum ein laufendes Programm mittendrin gestorben ist, in einem Satz.
std::string CrashText(unsigned long code);
