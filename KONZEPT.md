# Code Klicker — Konzept

Ein Programm, in dem man **C++-Code in einem Fenster schreibt und ausführt**, um damit eine
Welt zu verändern. Erster Baustein: ein Block in der Mitte, den man mit `block.mine();`
abbaut — dann ist er weg.

Der Code läuft nicht unsichtbar: die **gerade laufende Zeile wird markiert**, und der Editor
hat **Syntax-Highlighting und Formatierung**.

**Leitsatz: so einfach wie möglich.**

---

## 1. Aufbau

```
┌──────────────────────────────────────────────────────────┐
│ [Welt] Skilltree │ [+ Neue Konsole] [Reset]      ● 12    │ ←── Menüleiste + Geld
├──────────────────────────────────────────────────────────┤
│                                                          │
│  ┌── Konsole 1 ───────────────────── x ┐                 │
│  │ [▶]                                 │       +1        │
│  ├─────────────────────────────────────┤    ┌────────┐   │
│  │ 1 │ int main() {                    │    │ BLOCK  │   │
│  │→2 │     block.mine();               │    └────────┘   │
│  │ 3 │     return 0;                   │                 │
│  │ 4 │ }                               │                 │
│  ├─────────────────────────────────────┤                 │
│  │ Block abgebaut.                     │ ←── eine Zeile Ausgabe
│  └─────────────────────────────────────┘                 │
└──────────────────────────────────────────────────────────┘
```

**Zwei Seiten**, die einander ersetzen — Reiter links in der Menüleiste:

| Seite | Inhalt |
|---|---|
| **Welt** | Der Block und die Konsolen |
| **Skilltree** | Hier wird das Geld ausgegeben — siehe **[SKILLTREE.md](SKILLTREE.md)** |

Laufende Programme **laufen beim Seitenwechsel weiter**. Man kann also den Code arbeiten
lassen und währenddessen in den Skilltree schauen.

---

## 2. Die Konsole

Ein Fenster im Fenster. Enthält **genau drei Dinge**:

1. **Ein Knopf** oben links.
   - Code steht → zeigt **▶**. Klick = starten.
   - Code läuft → zeigt **⏸**. Klick = pausieren.
   - Pausiert → zeigt wieder **▶**. Klick = weiterlaufen.
   - **Bei Pause ist der Editor wieder frei.** Ändert man etwas, startet ▶ von vorne statt
     fortzusetzen. Damit kommt man auch aus einem `while (true)` wieder heraus, ohne dass es
     einen zweiten Knopf braucht.
   - Mehr nicht. Kein Stopp, kein Einzelschritt, kein Tempo-Regler.
2. **Der Code-Editor** — mit Syntax-Highlighting, Zeilennummern und Markierung der
   laufenden Zeile.
3. **Eine Ausgabezeile** unten — für `print()` und Fehlermeldungen. Kein Panel, keine Tabs,
   nur eine Zeile Text.

**Ausdrücklich nicht dabei:** Variablen-Anzeige, Aufrufstapel, Haltepunkte, Tempo-Regler,
Einzelschritt, Stopp-Knopf.

**Fenster-Verhalten:**
- frei verschiebbar (Titelleiste ziehen), frei skalierbar (Ecke ziehen)
- **beliebig viele Konsolen** — Button „+ Neue Konsole" in der Menüleiste
- einzeln schließbar über das `x`

Tastenkürzel: **Strg+Enter** = starten/pausieren, **Strg+Alt+F** = formatieren.

### Jede Konsole mit `main()` ist ein eigenes Programm

Das ist der wichtigste Punkt am ganzen Aufbau. Eine Konsole ist entweder **ein eigenes
Programm** (sie hat ein `main()`) oder **gemeinsamer Vorrat** (sie hat keins).

```cpp
// Konsole 1: baut ab            // Konsole 2: verkauft
int main() {                     int main() {
    while (true) {                   while (true) {
        block.mine();                    if (item.count(Stone) > 20)
    }                                        item.sell(Stone);
}                                        wait(1.0f);
                                     }
                                 }

// Konsole 3: kein main() - Vorrat für beide
int wieViel(Ore erz) { return item.count(erz); }
```

Beide laufen **gleichzeitig**, als zwei echte Prozesse mit je eigenem Zeilenbudget. Das ist
die einzige Stelle im Spiel, an der sich der Durchsatz *vervielfacht* statt zu wachsen —
deshalb ist „+1 Konsole" im Skilltree der teuerste und seltenste Punkt.

**Vier Dinge ergeben sich daraus:**

1. **Mindestens eine Konsole braucht ein `main()`.** Keine → *„No console has an int main()"*.
   Mehrere sind ausdrücklich erwünscht — das ist der ganze Sinn.
2. **Der ▶-Knopf startet alle**, egal in welcher Konsole man ihn drückt. Deshalb sieht er in
   allen gleich aus und alle zeigen dieselbe Ausgabezeile.
3. Ruft der Code eine Funktion auf, die in einer Vorratskonsole steht, **wandert die
   Zeilenmarkierung dorthin mit**. Es können also mehrere Markierungen gleichzeitig stehen —
   jede Konsole fragt über `engine.lineIn(id)` nach sich selbst.
4. **Globale Variablen aus dem Vorrat sind NICHT geteilt.** Jeder Prozess bekommt seine
   eigene Kopie, denn es sind eigene Prozesse mit eigenem Speicher. Geteilt ist nur
   `shared[...]` — das liegt im Spiel. Genau dafür gibt es den Punkt.

**Wie das gebaut ist** (`CombineSources` und `CompileToExe` in `native.cpp`):

- Für **jede** Konsole mit `main()` entsteht eine eigene `.cpp`: erst alle Vorratskonsolen,
  dann diese eine. In C++ muss eine Variable vor ihrer Benutzung stehen — so sieht das
  `main()` automatisch alles andere, ohne dass man auf die Reihenfolge achten muss.
- Übersetzt wird **nebeneinander**, ein Thread je Programm. Nacheinander würde es bei drei
  Konsolen dreimal so lange dauern.
- Gemerkt wird das Ergebnis **je Konsole**: ändert man nur eine von dreien, wird auch nur die
  eine neu übersetzt.
- Vor jedes Stück kommt ein **`#line 1 "konsoleN"`**. Dadurch meldet der Compiler Fehler als
  `konsole2(7,5): error ...` statt als Zeile in der zusammengesetzten Datei — der rote Marker
  landet also in der richtigen Konsole und der richtigen Zeile.
- Die Instrumentierung schreibt `ck::line(2, 7)` statt `ck::line(7)`, also **Konsole und
  Zeile**. Deshalb weiß die Oberfläche, wo sie markieren muss.
- Hakt es bei **einem** Programm, wird **keines** gestartet. Ein halb laufender Satz wäre
  schlimmer als keiner: man sucht den Fehler dann in der falschen Konsole.

> Nebenwirkung, die beim Umbau aufgefallen ist: `GetToolchain()` suchte den Compiler hinter
> einem schlichten `static bool done`. Das reichte, solange nur ein Übersetzer lief. Bei
> zweien sah der zweite Thread `done == true`, während der erste noch in `vcvars64.bat`
> steckte — und bekam einen leeren Compilerpfad. Jetzt ist es ein `static` mit
> Initialisierer: der erste sucht, alle anderen warten.

---

## 2b. Geld und Nachwachsen

Jeder abgebaute Block bringt Geld. Angezeigt oben rechts als Münze plus Zahl, beim Abbauen
steigt ein **+1** über dem Block auf.

**Der Block wächst von selbst nach.** Nach dem Abbauen ist er kurz weg und steigt dann
sichtbar von unten wieder auf. Damit lohnt sich eine Schleife:

```cpp
int main() {
    while (true) {
        block.mine();
    }
}
```

```cpp
struct World {
    int   money          = 0;     // was man hat
    int   moneyPerBlock  = 1;     // was ein Block bringt
    float respawnSeconds = 0.6f;  // wie lange er weg ist
};
```

`moneyPerBlock` und `respawnSeconds` sind bewusst schon jetzt eigene Variablen, obwohl beide
fest stehen: **genau an diesen beiden dreht der Skilltree später** — mehr Geld pro Block und
schnelleres Nachwachsen.

**Was sich dadurch rechnet:** Bei 10 Zeilen pro Sekunde dauert eine Runde der Schleife oben
100 ms, das Nachwachsen 600 ms. Also läuft die Schleife ungefähr sechsmal ins Leere, bevor
wieder etwas da ist — etwa **1,4 Blöcke pro Sekunde**. Wer das nicht will, fragt vorher:

```cpp
while (true) {
    if (block.isThere()) {
        block.mine();
    }
}
```

Damit bekommt `block.isThere()` einen echten Sinn. **Genau das ist auch der Startinhalt jeder
neuen Konsole.**

„Block zurücksetzen" setzt **nur den Block** zurück — Geld und Zähler bleiben, sonst wäre der
Knopf ein Zurücksetzen des ganzen Spielstands.

---

## 3. Editor

### Syntax-Highlighting
Schlüsselwörter blau, Typen türkis, Funktionen gelb, Zahlen grün, Texte orange, Kommentare
grau. Kommt bei **ImGuiColorTextEdit** fertig mit — die Library hat eine eingebaute
C++-Definition. Nichts selbst zu bauen.

### Formatierung
- **Beim Tippen:** automatisch einrücken nach `{`, Klammern `{ } ( ) " "` automatisch
  schließen, ausrücken beim Tippen von `}`.
- **Auf Knopfdruck (Strg+Alt+F):** Text durch **clang-format** schicken — denselben
  Formatierer, den Visual Studio benutzt. `clang-format.exe` liegt schon auf dem Rechner
  (kommt mit VS 2022), muss also nicht gebaut werden.

### Laufende Zeile
Farbiger Hintergrund auf der aktuellen Zeile + Pfeil `→` bei der Zeilennummer, der Editor
scrollt mit. ImGuiColorTextEdit kann das bereits.

Da es keinen Tempo-Regler gibt: **feste Geschwindigkeit im Code** — eine Zahl an einer Stelle,
`Native::kLinesPerSecond` in `src/native.h`, Startwert 10.

---

## 4. Der Code

Es ist ein **ganz normales, vollständiges C++-Programm mit `main()`** — kein Dialekt, keine
Teilmenge. Es wird wirklich mit dem MSVC-Compiler übersetzt. Also geht alles:

```cpp
#include <vector>

int doppelt(int a) {
    return a * 2;
}

int main() {
    std::vector<int> zahlen = {1, 2, 3};

    for (int i = 0; i < doppelt(2); i++) {
        block.mine();
        if (block.exists()) {
            print("noch da");
        }
    }

    print(zahlen.size());
    return 0;
}
```

Schleifen, `if`, eigene Funktionen, Klassen, Templates, Zeiger, `std::` — alles, was C++ kann.

### Die Spiel-API

Steht ohne `#include` bereit:

```cpp
struct Block {
    void mine();   // abbauen -> +1 Geld, Block ist kurz weg

    // Ist der Block gerade da?
    bool isThere() const;    // true = da
    bool exists()  const;    // dasselbe, anderer Name

    // Waechst er gerade nach? Genau das Gegenteil.
    bool isLoading() const;
    bool isGone()    const;  // dasselbe, anderer Name
};

const Block block;

// Alles, was die Tasche betrifft. Was einmal abgebaut ist, gehoert nicht mehr
// dem Block - deshalb steht es hier und nicht bei Block.
// Welche Erze es gibt, steht nicht im Programm, sondern in data/erze.json -
// und spaeter wuerfelt das Spiel weitere aus. Deshalb wird dieses enum bei
// jedem Start neu erzeugt (native.cpp, OreEnumSource). Aus "White Gold" wird
// WhiteGold: was in einem C++-Namen nichts zu suchen hat, faellt weg.
enum Ore {
    Any = -1,   // egal was: has(Any) zaehlt alles, sell(Any) verkauft alles
    Stone = 0, Coal = 1, /* ... */
};

struct Item {
    int  sell();                            // alles verkaufen -> Geld
    int  sell(Ore erz);                     // nur eine Sorte
    int  sell(Ore erz, int anzahl);

    bool has(Ore erz);                      // liegt etwas davon in der Tasche?
    bool has(Ore erz, int anzahl);

    // Verarbeiten. Ohne Zahl: alles, was gerade passt.
    int wash(Ore erz);                      // dazu smelt, cast, clean,
    int wash(Ore erz, int anzahl);          // polish, harden, refine, press

    int alloy(Ore stoff);                   // legieren
    int alloy(Ore stoff, int anzahl);
    int canAlloy(Ore stoff);                // wie viele gingen gerade?

    // Dasselbe noch einmal mit const std::string& statt Ore. Das war frueher
    // der einzige Weg - es bleibt, damit alter Code weiter uebersetzt.
};

const Item item;

void print(const char* text);   // schreibt in die Ausgabezeile
void print(const std::string&); // auch fuer std::string,
void print(int);                // Zahlen
void print(double);
void print(bool);
```

### `shared[...]` — Werte, die einen Neustart überleben

Um Werte zwischen Konsolen zu teilen, braucht man **nichts Besonderes** — normale Variablen
reichen, weil alle Konsolen ein Programm sind (siehe oben).

`shared` macht etwas anderes: es liegt **im Spiel**, nicht im Programm. Deshalb ist der Wert
noch da, wenn man ▶ erneut drückt. Normale Variablen fangen bei jedem Start wieder von vorne
an.

```cpp
shared["gesamt"] += 1;   // zaehlt ueber alle Programmlaeufe hinweg weiter
int wieOft = shared["gesamt"];
print(shared["gesamt"]);
```

**Nur ganze Zahlen** (`int`). Unbekannte Namen sind 0.

`+=` und `++` gehen als **eine einzige** Nachricht zum Spiel (`A name differenz`), nicht als
Lesen-dann-Schreiben — sonst wäre der Wert bei parallelen Zugriffen falsch.

---

**Die ganze Liste steht auch in jeder neuen Konsole als Kommentar drin** — sonst weiß niemand,
was es überhaupt gibt. Das ist die einzige Dokumentation im Programm, und sie steht genau
dort, wo man sie braucht.

Der Spielercode wird mit **`/utf-8`** übersetzt, damit Umlaute in `print("Größe")` richtig
ankommen.

**Kein `#include` dafür nötig.** Der Header wird beim Kompilieren mit dem Schalter
`/FI klicker.h` erzwungen — dadurch sind `block` und `item` verfügbar, **ohne dass eine Zeile vor dem Code
des Spielers steht**. Wichtig, weil sonst alle Zeilennummern verrutschen und
Compiler-Fehlermeldungen auf die falsche Zeile zeigen würden.

Eigene Includes wie `#include <vector>` schreibt man ganz normal selbst.

Später: `block.setColor(...)`, Raster mit mehreren Blöcken, Zähler.

---

## 5. Wie wird der Code ausgeführt?

Echtes kompiliertes C++ läuft mit ~1 Milliarde Operationen pro Sekunde — da ist nichts zu
sehen. Damit man die laufende Zeile sieht, muss das Programm **vor jeder Zeile Bescheid
sagen**, wo es ist.

**Lösung: Instrumentierung.** Vor dem Kompilieren baut das Programm Meldungen ein:

```cpp
// geschrieben:                  daraus gemacht:
int main() {                     int main() {
    block.mine();                    ck::line(2); block.mine();
    return 0;                        ck::line(3); return 0;
}                                }
```

`ck::line(n)` meldet die Zeile an den Editor und wartet kurz. Dadurch geht **volles C++ mit
`#include`, Templates und Zeigern** — *und* die Live-Markierung.

`ck::line()` kommt dabei in **dieselbe** Zeile, nie in eine neue. So zeigen Compiler-Fehler
weiterhin auf die richtige Zeilennummer.

### Warum ein echtes Programm (.exe) statt einer DLL

Weil der Code ein `main()` hat, ist er ein **vollständiges Programm**. Also wird auch genau
das daraus gebaut: `cl.exe` erzeugt eine `run.exe`, die das Spiel als **eigenen Prozess**
startet und über zwei **Pipes** steuert.

Das ist nicht nur naheliegender, sondern auch **einfacher und sicherer** als eine DLL:

| | .exe + Pipes | DLL |
|---|---|---|
| `main()` bleibt `main()` | ✅ | ❌ müsste umbenannt werden |
| Absturz im Spielercode | ✅ nur der Kindprozess stirbt, Meldung „abgestürzt" | ❌ reißt das Spiel mit, braucht `__try/__except` |
| Endlosschleife | ✅ Prozess einfach beenden | ❌ Thread lässt sich nicht sauber abbrechen |
| Thread im Spiel nötig | ✅ nein | ❌ ja |
| Nächster Start | ✅ neuer Ordner, fertig | ❌ geladene DLLs sind gesperrt |

**So läuft die Verständigung.** Der Kindprozess schreibt kurze Zeilen nach `stdout`, das Spiel
liest sie einmal pro Bild und antwortet über `stdin`:

| Kind → Spiel | Bedeutung |
|---|---|
| `L 2 12` | Konsole 2, Zeile 12 beginnt gleich — **wartet dann auf Freigabe** |
| `M` | `block.mine()` |
| `Q` | `block.exists()` — wartet auf `1` oder `0` |
| `O text` | `print("text")` |
| `V name` | geteilte Variable lesen — wartet auf die Zahl |
| `W name wert` | geteilte Variable setzen |
| `A name differenz` | zu einer geteilten Variable dazuzählen |
| `X` | `main()` ist zu Ende |

| Spiel → Kind | Bedeutung |
|---|---|
| `g` | Freigabe für die nächste Zeile |
| `1` / `0` | Antwort auf `Q` |

**Der Trick dabei:** Weil das Kind *vor jeder Zeile* auf die Freigabe wartet, steuert das
Spiel das Tempo einfach dadurch, wie oft es `g` schickt.

- **Laufen** → alle 100 ms ein `g` (= 10 Zeilen pro Sekunde)
- **Pause** → einfach kein `g` mehr schicken. Das Kind bleibt stehen. Nichts weiter zu bauen.
- **Endlosschleife** → Prozess abschießen, fertig.

### Vorgehen in zwei Stufen

| Stufe | Ausführung | Warum |
|---|---|---|
| **1** ✅ erledigt | Winziger eigener Interpreter — kannte nur `block.mine()` und `print()` | Konsolen, Editor, Highlighting, Formatierung und Zeilenmarkierung standen damit sofort |
| **2** ✅ erledigt | Echtes C++: instrumentieren → `cl.exe` → `run.exe` starten → Pipes | Volles C++ hinter derselben Oberfläche |

Dazwischen sitzt die Schnittstelle `Engine` (`src/engine.h`) mit `start()`, `togglePause()`,
`stop()`, `update()`, `currentLine()`. Der Austausch von Stufe 1 gegen Stufe 2 hat deshalb
nur die Zeile `Interp engine;` → `Native engine;` in `console.h` gekostet.

### Gemessene Zahlen

| | |
|---|---|
| Wartezeit pro ▶-Klick | **≈ 380 ms** (kein vorkompilierter Header nötig) |
| Einmalig beim ersten Start | ≈ 1 s für `vcvars64.bat` (danach gemerkt) |

### Grenzen der Instrumentierung

Der Einbau von `ck::line()` benutzt einen eigenen kleinen Abtaster statt eines vollen
C++-Parsers (`src/instrument.cpp`, ~150 Zeilen). Er unterscheidet Anweisungsblöcke von
Klassen, `struct`, `enum`, `namespace` und Initialisierungslisten und überspringt
Kommentare, Zeichenketten und Präprozessor-Zeilen.

Eine bekannte Lücke: **klammerlose Rümpfe** bekommen keine eigene Markierung.

```cpp
if (block.exists())
    print("da");        // wird ausgeführt, aber nicht markiert
```

Gelöst über die Formatierung: **Strg+Alt+F ergänzt die fehlenden `{ }`** (clang-format mit
`InsertBraces: true`), danach wird auch diese Zeile markiert. Deshalb ist
**tree-sitter nicht mehr nötig** — es war ursprünglich genau dafür vorgesehen.

---

## 6. Libraries

Alles kostenlos, alles per CMake automatisch beim ersten Build geladen. Nichts von Hand
installieren.

Am Ende sind es **genau zwei Downloads**:

| Library | Zweck | Lizenz |
|---|---|---|
| **Dear ImGui** v1.91.5 | Die Oberfläche: verschiebbare Fenster im Fenster, Knöpfe. Der ▶/⏸-Knopf braucht **kein** Icon-Font — ImGui zeichnet Dreieck und Pausenbalken selbst. | MIT |
| **ImGuiColorTextEdit** | Der Code-Editor: C++-Highlighting, Zeilennummern, Zeilenmarkierung, Fehlermarker — alles fertig. Hängt nur von `imgui.h` und der Standardbibliothek ab. | MIT |

**Schon auf dem Rechner, also keine Abhängigkeit:**
- `cl.exe` (MSVC 14.44) — der Compiler, der den Spielercode übersetzt
- `clang-format.exe` (v19.1.5) — Formatierung, kommt mit VS 2022
- **Win32 + DirectX 11** — Fenster und Grafik, Teil von Windows
- **Consolas** — die Editor-Schrift, liegt auf jedem Windows

**Nicht nötig:** SDL/GLFW (Win32 reicht unter Windows), Qt, vcpkg/Conan, LLVM/libclang,
Icon-Fonts, {fmt}, JSON-Library, eine mitgelieferte Schriftdatei.

**tree-sitter wurde geprüft und ist nicht nötig.** Es war für das Finden der
Anweisungsanfänge vorgesehen; der eigene Abtaster in `instrument.cpp` erledigt das in ~150
Zeilen ohne zusätzliche Abhängigkeit (siehe „Grenzen der Instrumentierung" oben).

**Ebenfalls verworfen:** *Cling / clang-repl* (echter C++-Interpreter, aber mehrere GB LLVM —
und bietet trotzdem keine Zeilen-Rückmeldung), *TinyCC* (nur C, kein C++), *AngelScript*
(sieht aus wie C++, ist es aber nicht), *Windows-Debugger-API* (technisch sauber, aber ein
Vielfaches an Aufwand gegenüber der Instrumentierung).

**Der Fork `santaclose/ImGuiColorTextEdit` wurde bewusst nicht genommen** — er zieht
boost::regex als Abhängigkeit nach. Das Original braucht nur `<regex>` aus der
Standardbibliothek.

---

## 7. Stand

- [x] Fenster öffnet sich, Block ist in der Mitte sichtbar
- [x] Konsolen frei verschiebbar und skalierbar
- [x] Mehrere Konsolen möglich, einzeln schließbar
- [x] Editor mit C++-Syntax-Highlighting und Zeilennummern
- [x] Formatieren auf Knopfdruck (Strg+Alt+F)
- [x] Automatisches Einrücken beim Tippen
- [x] **Ein** Knopf, der zwischen ▶ und ⏸ wechselt
- [x] Laufende Zeile wird markiert, Editor scrollt mit
- [x] `block.mine();` lässt den Block verschwinden
- [x] Fehler erscheinen in der Ausgabezeile — **kein Absturz**
- [x] **Volles C++**: `#include`, Schleifen, `if`, eigene Funktionen, Klassen, Templates
- [x] Compiler-Fehler landen als roter Marker in der richtigen Editor-Zeile
- [x] Absturz im Spielercode killt nur den Kindprozess
- [x] Aus einer Endlosschleife kommt man wieder heraus (⏸, ändern, ▶ startet neu)
- [x] Geld: +1 pro abgebautem Block, Anzeige oben rechts, „+1"-Effekt am Block
- [x] Block wächst nach dem Abbauen von selbst nach (sichtbar von unten aufsteigend)
- [x] Zwei Seiten (Welt / Skilltree), die einander ersetzen
- [x] Programme laufen beim Seitenwechsel weiter, der Block wächst auch dort nach
- [x] **Alle Konsolen zusammen sind ein Programm** — eine Variable aus Konsole 2 lässt sich
      in Konsole 1 ganz normal benutzen
- [x] Zeilenmarkierung wandert in die richtige Konsole
- [x] Compiler-Fehler landen in der richtigen Konsole und Zeile (über `#line`)
- [x] Klare Meldung, wenn kein oder mehr als ein `main()` da ist
- [x] `shared["name"]` — Werte, die einen Neustart des Programms überleben
- [x] **Skilltree**: C++ selbst liegt hinter der Paywall — siehe [SKILLTREE.md](SKILLTREE.md)
- [x] Gleicher Code → kein neues Kompilieren, die `run.exe` wird wiederverwendet

**Bewusst nicht dabei:** Block wächst nicht nach, kein Inventar, keine Punkte, kein Speichern,
nur ein Block statt einer Welt. Kein Variablen-Panel, kein Aufrufstapel, keine Haltepunkte,
kein Tempo-Regler, kein Einzelschritt.

---

## 8. Später

**Als Nächstes:**
- **Speichern** — Geld, gekaufte Punkte und der Code der Konsolen. Ohne das ist beim
  Neustart alles weg, und der Skilltree ist auf ein längeres Spiel ausgelegt.

**Danach:**
- Mehrere Blöcke in einem Raster
- Umskillen / Zurücksetzen
- Aufräumen der `run.exe`-Ordner im Temp-Verzeichnis beim Beenden

---

## 9. Projektstruktur

```
code klicker/
├── CMakeLists.txt        Build, lädt die zwei Libraries automatisch
├── .clang-format         Stil für den Quelltext des Spiels selbst
├── KONZEPT.md            diese Datei
└── src/
    ├── main.cpp          Fenster, DirectX, Hauptschleife, Menüleiste, Seitenwechsel
    ├── console.h/.cpp    Eine Konsole: Knopf + Editor + Ausgabezeile
    ├── world.h/.cpp      Der Block und das Geld: Zustand + Zeichnen
    ├── skills.h/.cpp     Die Skilltree-Seite (noch Platzhalter)
    ├── engine.h          Schnittstelle: start / togglePause / stop / update / currentLine
    ├── native.h/.cpp     Kompilieren, Prozess starten, Pipes lesen
    │                     — enthält auch klicker.h/.cpp als Text (die Spiel-API
    │                       im Kindprozess). Dadurch braucht das fertige
    │                       Programm keine mitgelieferten Dateien.
    ├── instrument.h/.cpp  ck::line() einbauen
    ├── format.h/.cpp     clang-format aufrufen
    └── proc.h/.cpp       Programme starten und ihre Ausgabe einsammeln
```

Beim Bauen entsteht zusätzlich `%TEMP%\codeklicker\rN\` mit `run.cpp`, `klicker.h`,
`klicker.cpp` und `run.exe` — dort kann man nachschauen, was aus dem eigenen Code
tatsächlich gemacht wurde.
