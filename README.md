# Code Miner

Ein Spiel, in dem du **echten C++-Code schreibst**, um zu spielen. In der Mitte liegt ein
Block. Du baust ihn nicht mit der Maus ab, sondern so:

```cpp
int main() {
    while (true) {
        block.mine();
        item.sell();
    }
    return 0;
}
```

Dieser Code wird wirklich übersetzt — von `cl.exe` bzw. `g++`, nicht von einem
eingebauten Spielzeug-Interpreter. Er läuft als eigenes Programm neben dem Spiel, und die
**gerade laufende Zeile ist im Editor markiert**. Du siehst deinem Programm beim Denken zu.

Je besser dein Code, desto mehr verdienst du. Vom Geld kaufst du im Skilltree die *Sprache
selbst* frei: Schleifen, Bedingungen, Variablen, eigene Funktionen. Am Anfang darfst du
noch nicht einmal `while` benutzen.

---

## Schnellstart

Im Projektordner liegen zwei fertige Pakete:

| Datei | für |
|---|---|
| `windows_CodeMiner.zip` | Windows (x64) |
| `linux_CodeMiner.zip` | Linux (x86-64) |

Auspacken und starten — `CodeMiner.exe` bzw. `./CodeMiner`. Der Ordner `data/` muss
**neben** der Programmdatei liegen, er ist in beiden Paketen schon dabei.

### Was auf dem Rechner installiert sein muss

Das Spiel übersetzt deinen Code während des Spielens. Dafür braucht es einen echten
C++-Compiler — ohne ihn startet es zwar, meldet beim Drücken auf **Play** aber, dass es
keinen Compiler findet.

**Windows**
[Visual Studio 2022](https://visualstudio.microsoft.com/de/downloads/) mit der Arbeitslast
*Desktopentwicklung mit C++*. Die kostenlose Community-Fassung reicht. Das Spiel findet die
Installation von allein (über `vswhere`, sonst an den üblichen Pfaden).

**Linux**

```bash
sudo apt install g++ libglfw3 libgl1
```

`g++` übersetzt deinen Code, die anderen beiden sind Fenster und Grafik. Auf anderen
Distributionen heißen die Pakete ähnlich (`gcc-c++`, `glfw`, `libglvnd`).

> Die mitgelieferte Linux-Datei ist auf Ubuntu 26.04 gebaut. Auf deutlich älteren
> Distributionen kann die C-Bibliothek zu alt sein — dann baust du sie besser selbst, siehe
> unten. Das dauert keine zwei Minuten.

---

## Wie es gespielt wird

Das Spiel läuft in **Runden**, und die haben zwei Phasen:

| Phase | was gilt |
|---|---|
| **Vorbereitung** | Die Welt steht still. Zeit kostet nichts. Du schreibst Code, kaufst im Skilltree ein, schaust in die Tasche. |
| **Lauf** | Die Uhr läuft (voreingestellt 5 Minuten). Jetzt arbeitet dein Programm, Blöcke wachsen nach, Aufträge laufen. |

Im Lauf kannst du das **ganze Spiel jederzeit anhalten** — mit **F9** oder dem Knopf unten
rechts in der Rundenleiste. Dann steht alles: die Uhr, der Abbau, ein laufender Auftrag und
dein Programm. Weiter geht es genau an der Stelle, an der es stehengeblieben ist. Das ist
etwas anderes als die Pause an der Konsole: die hält nur dein Programm an, während die Uhr
weiterläuft.

Am Ende jeder Runde musst du ein **Geldziel** erreicht haben, sonst ist das Spiel vorbei.
Das Ziel wird abgezogen — es ist die Miete für die Runde, nicht nur eine Hürde. Nur was
darüber liegt, bleibt dir zum Einkaufen.

Runde 1 verlangt 500. Danach steigt es erst steil und wird dann immer flacher:

| Runde | 1 | 3 | 5 | 10 | 15 | 20 |
|---|---|---|---|---|---|---|
| Ziel | 500 | 2.7k | 10.7k | 135k | 740k | 2.7M |

Das ist Absicht: am Anfang ist jede neue Technik ein Sprung (der erste Ofen, das erste
Legieren), später fährst du dieselben Techniken nur noch feiner. Ein Ziel, das stur weiter
verdoppelt, wäre irgendwann nicht mehr einzuholen. Alle Zahlen stehen in
`data/runden.json` und sind änderbar — die Datei erklärt sich selbst.

**Der Skilltree** ist kein Bonus-Menü, sondern die eigentliche Sperre: `while`, `if`,
Variablen und eigene Funktionen musst du dir kaufen. Benutzt du etwas, das dir noch nicht
gehört, läuft das Programm nicht. Mehr dazu in **[SKILLTREE.md](SKILLTREE.md)**.

---

## Die Befehle

Steht alles ohne `#include` bereit:

```cpp
// ---- Der Block ----------------------------------------------------------
block.mine();          // abbauen (dauert, je nach Erz)
block.isThere();       // liegt gerade einer da?
block.isLoading();     // wächst er gerade nach?

block.is(Gold);        // ist es gerade Gold, das da liegt?
block.ore();           // -> das Erz selbst, z. B. für print(block.ore())
block.mine(Cool);      // abbauen und dabei kühlen

// ---- Die Tasche ---------------------------------------------------------
item.sell();                    // alles verkaufen -> Geld
item.sell(Stone);               // nur eine Sorte
item.sell(Stone, 10);           // höchstens zehn Stück

item.has(Stone);                // liegt etwas davon in der Tasche?
item.has(Stone, 10);            // mindestens zehn?
item.has(Any);                  // überhaupt irgendetwas?

item.wash(Stone);               // verarbeiten - ebenso smelt, clean, press,
item.wash(Stone, 5);            // cast, polish, harden, refine

item.alloy(Electrum);           // legieren
item.canAlloy(Electrum);        // wie viele gingen gerade?

// ---- Ausgabe und geteilte Werte -----------------------------------------
print("Text");                  // eine Zeile unter der Konsole
shared["zaehler"] += 1;         // von ALLEN Konsolen aus sichtbar
```

Ein vollständiges Programm, das wäscht, bevor es verkauft:

```cpp
int main() {
    while (true) {
        block.mine();

        if (item.has(Stone, 10)) {
            item.wash(Stone);
        }
        item.sell();
    }
    return 0;
}
```

### Behandlung: teure Erze wollen mehr als `mine()`

`while (true) { block.mine(); }` trägt dich durch die ersten Runden — danach nicht mehr. Je
wertvoller ein Erz, desto **wahrscheinlicher** verlangt es eine **Behandlung**: gekühlt oder
erhitzt werden beim Abbau. Gewürfelt wird das aber nur **einmal**, wenn es das Erz zum ersten
Mal gibt — danach steht es fest. Gold will immer dasselbe, jeder einzelne Goldblock.

Deshalb sagt dir im Programm niemand, was zu tun ist. Du musst wissen, welches Erz was will —
die Wikiseite jedes Erzes sagt es, und über dem Block steht es auch — und danach fragen,
welches gerade dasteht. Also ein `if` je Erz:

```cpp
while (true) {
    if (block.is(Gold))         block.mine(Cool);
    else if (block.is(Diamond)) block.mine(Heat);
    else                        block.mine();

    item.sell();
}
```

Der Block kommt **immer gleich schnell** heraus — die Strafe ist die **Reinheit**. Gar nicht
behandelt kostet 25 Punkte, falsch behandelt 50. Reinheit wirkt linear von 0,5× bis 1,5× auf
den Preis, ein falsch abgebauter Block bringt also fast die Hälfte weniger. Deshalb lohnt
sich blindes `block.mine(Cool)` nie: Erze, die nichts wollen, verlieren daran genauso.

Verloren ist es nicht ganz: **Reinigen** holt einen Teil zurück — kostet aber einen Auftrag,
und es läuft immer nur einer.

Freigeschaltet wird das über den Punkt **care** im Skilltree, und er braucht `check`.
Die Kurve steht in `data/erze.json` unter `"behandlung"`: unter Grundwert 8 verlangt nie ein
Erz etwas, nach oben läuft die Wahrscheinlichkeit gegen 80 %. Dort stehen auch die beiden
Verluste (`verlust_ohne`, `verlust_falsch`) — und bei jedem Erz darf `"behandlung": "cool"`
stehen, wenn du es selbst festlegen willst statt es würfeln zu lassen. Gewürfelte Erze aus
`data/erzgenerator.json` bekommen ihre Behandlung bei der Entstehung und behalten sie.

### Erznamen: ein `enum`, keine Zeichenkette

Erze sind kein Text, sondern Werte eines `enum Ore` — deshalb stehen sie **ohne
Anführungszeichen** da. Das Spiel baut dieses `enum` bei jedem Start aus der Erzliste,
also stehen auch die Erze darin, die es sich selbst ausgewürfelt hat.

Namen aus mehreren Wörtern (`Glacier Sheen`, `Night Ash Stone Vein`) werden dabei
zusammengezogen; alles, was in einem C++-Namen nichts zu suchen hat, fällt weg:

```cpp
item.has(GlacierSheen)      // so heißt das Erz "Glacier Sheen" im Code
item.has(WhiteGold)         // "White Gold"
item.has(Any)               // egal was - liegt überhaupt etwas in der Tasche?
```

Der Vorteil gegenüber früher: ein Tippfehler ist jetzt ein **Übersetzungsfehler** und
keine stille `0` mehr. Ein Erz, das es noch nicht gibt, kennt der Compiler auch noch
nicht — es taucht auf, sobald das Spiel es kennt.

`Any` gibt es zusätzlich zu jedem Erz: `item.has(Any)` zählt alles zusammen,
`item.sell(Any)` verkauft alles.

Die Schreibweise mit Anführungszeichen (`item.has("Glacier Sheen")`) funktioniert weiter,
damit alter Code nicht auf einmal stehenbleibt — dort gilt weiter: vollständig schreiben,
Groß- und Kleinschreibung egal, kein Teiltreffer, und ein unbekannter Name gibt `0`.

Mehrere Konsolen sind zusammen **ein** Programm: genau eine davon hat `main()`, die anderen
steuern Funktionen und Variablen bei. Details in **[KONZEPT.md](KONZEPT.md)**.

---

## Selbst bauen

Gebraucht werden CMake ab 3.20 und ein C++17-Compiler. Dear ImGui und der Code-Editor
werden von CMake selbst heruntergeladen — du musst nichts vorher besorgen.

**Windows**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Ergebnis: `build\Release\CodeMiner.exe`

**Linux**

```bash
sudo apt install build-essential cmake libglfw3-dev libgl1-mesa-dev
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j4
```

Ergebnis: `build-linux/CodeMiner`

Zum Spielen muss `data/` neben der Programmdatei liegen. Aus dem Build-Ordner heraus
gestartet findet das Spiel den Ordner auch zwei Ebenen höher — beim Entwickeln musst du
also nichts kopieren.

---

## Aufbau des Projekts

```
src/
  main.cpp            das Spiel selbst - plattformfrei, kein einziges #ifdef
  world.cpp           Block, Tasche, Abbau, Verkauf
  round.cpp           Runden, Ziel, Abrechnung
  skilltree.cpp       der Baum, skillfile.cpp liest data/skills.txt
  native.cpp          übersetzt und startet den Code des Spielers
  instrument.cpp      hängt die Zeilenmarkierung in den Code
  codecheck.cpp       prüft vorab, ob nur Erlaubtes benutzt wird
  wiki.cpp            die Erz-Seiten
  ore/oregen/craft/alloy.cpp    Erze, gewürfelte Erze, Verarbeiten, Legieren

  platform.h          Fenster und Grafik - was, nicht wie
    platform_win32.cpp    Win32 + DirectX 11
    platform_glfw.cpp     GLFW + OpenGL 3
  proc.h              Dateien, Pfade, Prozesse - was, nicht wie
    proc_win.cpp          CreateProcess, Pipes
    proc_posix.cpp        fork/exec, nicht-blockierende Pipes

data/                 alle Zahlen des Spiels, siehe unten
```

Der Unterschied zwischen Windows und Linux steckt vollständig in `platform_*.cpp` und
`proc_*.cpp`. Alles andere ist auf beiden Systemen dieselbe Datei.

### Die Dateien in `data/`

Alle sind Text und dürfen geändert werden. Jede erklärt sich in einem `hilfe`-Abschnitt
gleich selbst.

| Datei | was drinsteht |
|---|---|
| `erze.json` | die handgemachten Erze — Wert, Seltenheit, Abbauzeit |
| `erzgenerator.json` | die Regeln, nach denen sich das Spiel neue Erze ausdenkt |
| `verarbeitung.json` | Waschen, Schmelzen, Gießen … was daraus wird |
| `legierungen.json` | welche Erze sich zu was verbinden |
| `runden.json` | Rundendauer, Geldziel, Wachstum |
| `skills.txt` | was der Skilltree anbietet und was es kostet |
| `wiki.json` | die Texte im Wiki |

Der Spielstand liegt als `spielstand.txt` **neben der Programmdatei** und entsteht beim
ersten Beenden. Zum Neuanfangen einfach löschen.

---

## Weiterlesen

| Datei | Inhalt |
|---|---|
| **[KONZEPT.md](KONZEPT.md)** | Wie das Ganze gedacht ist, und wie der Code des Spielers wirklich ausgeführt wird |
| **[SKILLTREE.md](SKILLTREE.md)** | Warum die Sprache selbst das ist, was gekauft wird |
