# Ideen: ungenutztes C++, neue Mechaniken, Late Game

> **Stand:** Die Abschnitte 1, 2 und 4.5 sind gebaut, dazu 3.2 (mehrere Öfen),
> 3.4 (Analyse), 3.6 (Markt) — und 2.2: **jede Konsole mit eigenem `main()` ist
> jetzt ein eigener Prozess.** Was noch offen ist, steht in der Tabelle in
> Abschnitt 5 — dort ist jede Zeile mit ✅ oder ⬜ markiert.
>
> Gebaut heißt: im Skilltree kaufbar, in `codecheck.cpp` gesperrt, im Wiki
> erklärt und in `data/` einstellbar.

Eine Bestandsaufnahme des Spiels und eine Liste dessen, was noch brachliegt.
Grundlage: `src/codecheck.cpp` (was gesperrt ist), `src/native.cpp` (was der
Spieler überhaupt aufrufen kann), `src/skill.h` (was man kaufen kann),
`src/world.h` (was die Welt kann) und die Dateien in `data/`.

---

## 0. Wo das Spiel gerade steht

Damit die Liste unten Sinn ergibt, vier Zahlen, an denen alles hängt:

| | Wert | wo |
|---|---|---|
| Tempo am Anfang | **3 Zeilen pro Sekunde** | `skillfile.h:54` |
| pro `speed`-Punkt | +0,25 Zeilen/s | `skillfile.h:55` |
| Aufträge gleichzeitig | **genau 1** (Ofen = Legierofen) | `world.h`, `crafting` |
| Blöcke in der Welt | **genau 1** | `world.h`, `ore` / `blockAlive` |
| Prozesse | **genau 1**, egal wie viele Konsolen | `native.cpp`, `CombineSources` |

**Daraus folgt die eigentliche Währung des Spiels: eine ausgeführte Zeile
kostet echte Zeit.** Am Anfang ein Drittel einer Sekunde. Alles, was Geld
bringt, bringt es, weil es *Zeilen spart* oder *Durchsatz erhöht*. Jede Idee
weiter unten wird an genau dieser Frage gemessen.

Und daraus folgen die drei Deckel, an die das Spiel spät stößt:

1. **Ein Auftragsplatz.** Egal wie gut das Programm ist — es läuft immer nur
   ein `wash`/`smelt`/`alloy`. Verarbeiten ist damit hart gedeckelt.
2. **Ein Block.** Abbau ist durch `respawnSeconds` gedeckelt, nicht durch Code.
3. **Das Zeilenbudget wächst linear** (+0,25 je Punkt), das Rundenziel
   exponentiell (Faktor 2,4 → 1,2). Die Schere geht auf, und es gibt nichts im
   Spiel, was den Durchsatz *multipliziert*.

---

## 1. C++, das es gibt, aber nicht freischaltbar ist oder keinen Nutzen hat

### 1a. Nicht gesperrt — und teilweise Löcher in der Bezahlschranke

`CheckLimits()` in `src/codecheck.cpp` ist eine **Whitelist über Wortmarken**.
Was nicht in der `else if`-Kette steht, ist umsonst. Der Tokenizer wirft
außerdem alle Satzzeichen außer `(` weg — Operatoren sind für die Prüfung also
grundsätzlich unsichtbar.

Das ist nicht nur "ungenutzt", das **umgeht den Skilltree**:

| was | Status | wieso das weh tut |
|---|---|---|
| `switch` / `case` / `default` | frei | Vollwertiger Ersatz für `if` + `else` + `condition+`. Die ganze Behandlungs-Mechanik lässt sich damit bauen, ohne einen einzigen dieser Punkte zu kaufen. |
| `?:` (ternär) | frei | Eine Bedingung, die der Tokenizer gar nicht sieht. |
| `&&` / `\|\|` | frei | Kurzschluss ist eine Verzweigung: `item.has(Stone,10) && item.wash(Stone);` |
| `goto` + Marke | frei | **Endlosschleife ohne `while`.** Beispiel unten. |
| Rekursion | frei | Schleife ohne `while`/`for`, kostet nur `functions`. |
| Array / `std::vector` / `std::map` | 1 Variable | `int z[1000];` ist eine Variable und tausend Zähler. `variable+` ist damit wertlos. |
| Lambda | 1 Variable | `auto f = []{...};` zählt als Variable, nicht als Funktion — also **billiger** als `functions`. |
| `struct` mit 20 Feldern | 1 Klasse | dasselbe Loch, andere Tür. |
| Templates, `constexpr`, `static`, `new`/`delete`, Zeiger, Referenzen | frei | völlig unbenutzt — es gibt nichts, worauf man zeigen könnte |
| `<algorithm>`, `<string>`, `<chrono>` | frei | kein Berührungspunkt mit dem Spiel |
| `std::this_thread::sleep_for` | frei | **kostet kein Zeilenbudget.** Warten im Schlaf ist dadurch immer besser als Warten in einer Schleife. |
| `std::thread` | frei | kompiliert, zerstört aber das Protokoll (siehe 1b) |

Zwei Beispiele, die heute laufen und beide nichts kosten:

```cpp
// Endlosschleife ohne den Punkt "while":
int main() {
start:
    block.mine();
    goto start;
}
```

```cpp
// Behandlung je Erz ohne die Punkte "if", "else" und "+1 Bedingung":
int main() {
    while (true) {
        switch (block.ore()) {
        case Gold:    block.mine(Cool); break;
        case Diamond: block.mine(Heat); break;
        default:      block.mine();     break;
        }
        item.sell();
    }
}
```

Beides ist gültiges C++, beides geht durch `CheckLimits()` hindurch. Wer das
weiß, kauft `if`, `else`, `condition+`, `variable+`, `loop+` und `function+`
nie — also genau die Punkte, aus denen der halbe Baum besteht.

### 1b. Kaufbar, aber ohne echten Gegenwert

| Punkt | was er heute wirklich bringt |
|---|---|
| **`console+`** | Nichts am Durchsatz. Konsolen sind **keine Prozesse**, sondern Übersetzungseinheiten — `CombineSources()` klebt sie zu *einer* `run.cpp` zusammen, genau eine hat `main()`. Man bekommt für Geld ein zweites Textfenster. |
| **`classes` / `class+`** | Es gibt nichts zu modellieren: keine API nimmt ein Objekt entgegen, kein `virtual`, keine Operatoren. Einziger echter Nutzen: das Variablenlimit umgehen. |
| **`functions` / `function+`** | Wirtschaftlich **negativ**. Jede Zeile im Rumpf kostet Budget, der Aufruf kostet noch eine dazu. Bei 3 Zeilen/s ist eine Funktion langsamer als derselbe Code zweimal hingeschrieben. |
| **`print`** | Kostet eine Zeile, also Geld, und bringt keins. Reines Debugging. |
| **`shared`** | Jeder *Lesezugriff* ist eine blockierende Rundreise (`V name` → warten). Der einzige Vorteil ("überlebt den Neustart des Programms") zahlt sich kaum aus: es gibt nur einen Prozess, mit dem man sich nichts teilen muss. |
| **`for`** | Ökonomisch identisch zu `while`. Ein Geschmackspunkt. |
| **`check`** | Drei Namen für dieselbe Frage (`isThere`/`exists`, `isLoading`/`isGone`). Und die einzige sinnvolle Anwendung — nicht ins Leere greifen — ist eine Warteschleife, die selbst Budget verbrennt. |
| **`block.is(Any)`** | Identisch mit `block.isThere()`. |

### 1c. Halb angeschlossene API — Sackgassen

Im Kindprozess existiert mehr, als der Spieler sehen kann, und an wichtigen
Stellen fehlt die Rückgabe:

- **`ck::inBag()` liefert `int`, `item.has()` gibt nur `bool` zurück.** Es gibt
  kein `item.count(Gold)`. Der Wert ist da, er kommt nur nicht durch. Wer
  wissen will, ob 10 Stück da sind, fragt `has(Gold,10)` — wer wissen will,
  *wie viele*, kann es gar nicht.
- **Es gibt keine Frage, ob der Ofen frei ist.** `item.wash(...)` gibt `0`
  zurück, wenn schon etwas läuft. Man erfährt es also nur, indem man es
  *versucht* — und der Versuch kostet eine Zeile.
- **Nicht abfragbar:** Geld, Restzeit der Runde, Reinheit eines Stapels,
  Zustand eines Stapels, Restzeit des Nachwachsens, Wert/Seltenheit/Behandlung
  eines Erzes.
- **`block.ore()` liefert ein `Ore`, aber es gibt keine Metadaten dazu.**
  Das ist die größte Sackgasse im Spiel: die Behandlung (`care`) *muss* als fest
  verdrahtete `if`-Kette geschrieben werden, Erz für Erz, aus dem Wiki
  abgeschrieben. Und `oregen.cpp` erfindet **endlos neue Erze**. Der Spieler
  muss sein Programm also für immer von Hand nachpflegen. Ein Programm, das mit
  einem unbekannten Erz umgehen kann, lässt sich heute gar nicht schreiben.

---

## 2. Nutzen für das Tote — Vorschläge

Reihenfolge: was am meisten bringt, zuerst. Jeder Punkt sagt, **wofür es Geld
gibt** und **warum es Sinn ergibt** — nicht nur, dass es hübsch wäre.

### 2.1 Zeiger und Referenzen → Metadaten und Maschinen zum Anfassen

Zwei Dinge in einem, und es löst gleichzeitig die Sackgasse aus 1c:

```cpp
// Neu: das Erz kennt sich selbst.
const OreInfo* i = info(block.ore());
block.mine(i->care);            // funktioniert für JEDES Erz, auch für die,
                                // die es beim Schreiben noch nicht gab
```

`info()` gibt einen Zeiger zurück — `nullptr`, wenn das Erz noch nicht
untersucht wurde (siehe 3.4). Damit ist ein Zeiger zum ersten Mal kein Zierrat,
sondern der einzige Weg, "ich weiß es noch nicht" auszudrücken:

```cpp
const OreInfo* i = info(block.ore());
if (i == nullptr) { assay(block.ore()); }   // erst untersuchen
else              { block.mine(i->care); }  // dann richtig abbauen
```

**Geld:** Richtige Behandlung statt falscher sind 50 Reinheitspunkte
(`verlust_falsch` in `data/erze.json`), und Reinheit wirkt linear von 0,5× bis
1,5× auf den Preis. Ein generisches Programm holt bei jedem neuen Erz sofort
bis zum Doppelten heraus, statt erst nach einer Runde Handarbeit.
**Sinn:** Das Spiel erfindet endlos Erze — dann muss es auch einen Weg geben,
generisch damit umzugehen. Sonst bestraft der Generator den Spieler.

Zweiter Teil: sobald es mehrere Öfen gibt (3.2), zeigen Zeiger auf Maschinen.

```cpp
Furnace* frei = workshop.free();     // nullptr = alle beschäftigt
if (frei) frei->smelt(Copper, 10);
```

Umsetzung: `native.cpp` braucht zwei neue Nachrichten (`N <ore>` → Metadaten,
`F` → freier Ofen). Das Muster steht schon da — `B`, `I` und `P` warten alle
auf eine Antwort.

### 2.2 Parallele Arbeit → Konsolen werden echte Prozesse

**Der größte Hebel im ganzen Dokument.** `console+` ist heute wertlos, weil
`CombineSources()` alles zu einem Programm klebt. Stattdessen:

> **Jede Konsole mit eigenem `main()` wird ein eigener Kindprozess mit eigenem
> Zeilenbudget.** Konsolen ohne `main()` gehören weiterhin zu der Konsole, die
> zuerst kommt — gemeinsame Funktionen und Variablen bleiben also möglich.

Damit ist `console+` nicht mehr ein zweites Textfenster, sondern **ein zweiter
Arbeiter**: einer bedient den Block, einer den Ofen, einer verkauft. Und
schlagartig bekommen drei tote Punkte einen Sinn:

- **`shared[...]`** ist plötzlich die *einzige* Möglichkeit, sich zwischen den
  Prozessen zu verständigen. Genau dafür war es gedacht, und genau dafür gibt
  es heute keinen Anlass.
- **Wettläufe werden ein Spielproblem.** Zwei Prozesse, die denselben Ofen
  wollen, sind ein echter, lehrreicher Fehler. Ein Punkt `lock`/`atomic` wird
  damit kaufenswert.
- **`std::thread`** kann man dann entweder sauber unterstützen (Mutex um
  `stdout`, Freigabe je Thread) oder ehrlich sperren, statt es wie heute
  stillschweigend kaputtgehen zu lassen.

**Geld:** Durchsatz mal Anzahl der Konsolen — das erste Mal, dass irgendetwas
im Spiel *multipliziert* statt addiert. Genau das fehlt spät (siehe 0).
**Sinn:** Es erklärt endlich, warum es überhaupt mehrere Fenster gibt.

Umsetzung: mittelgroß, aber das Protokoll kann es schon — jede Nachricht trägt
bereits die Konsolennummer (`L 2 12`). `Native` hält statt einem `Child` eine
Liste, verteilt die Freigaben reihum und mischt die Antworten. Das Budget je
Prozess muss ein Bruchteil sein (z. B. `linesPerSecond / n` plus ein
`speed`-Punkt je Konsole), sonst ist es kostenloses Tempo.

### 2.3 `functions` → Übersetzung wird zur Ressource

Funktionen sind heute teurer als Copy-Paste. Das dreht man mit Punkten um, die
im Spiel selbst thematisch sitzen:

| Punkt | Wirkung |
|---|---|
| `inline` | Zeilen **innerhalb eigener Funktionen** kosten nur die Hälfte |
| `optimize` (`-O2`) | Der Aufruf selbst kostet keine Zeile mehr |
| `cache` | Ein zweiter Aufruf mit denselben Argumenten kostet gar nichts |

**Geld:** direkt — mehr ausgeführte Arbeit pro Sekunde. **Sinn:** Das Spiel
kompiliert wirklich mit `cl.exe`/`g++`; Optimierungsstufen als Kaufware sind
die naheliegendste Idee, die dieses Projekt haben kann. Und sie belohnt genau
das, was es lehren will: Code strukturieren statt wiederholen.

### 2.4 `classes` → der Autopilot

Gib der API eine Basisklasse, von der der Spieler ableitet:

```cpp
struct MyRoutine : Routine {
    void step() override {
        block.mine(info(block.ore())->care);
        if (item.has(Any, 20)) item.sell();
    }
};

int main() { automation.install(new MyRoutine()); }
```

Eine installierte `Routine` läuft **weiter, während man im Skilltree ist, im
Wiki liest oder in der Vorbereitungsphase steht** — mit deutlich gedrosseltem
Tempo, etwa 20 %. Sie ist das Idle-Spiel im Programmierspiel.

**Geld:** Einnahmen in Zeit, die heute komplett tot ist — die Vorbereitung
steht still (`frozen = true`). **Sinn:** `classes` kauft man dann, weil man
eine Maschine bauen will, die sich selbst besitzt, nicht um das Variablenlimit
auszutricksen. Und `new` und Zeiger sind an der Stelle ganz von selbst da.

### 2.5 `print` → Messen statt Nachschlagen

`print()` kostet Geld und bringt keins. Verbinde es mit der Analyse aus 3.4:
Was das Programm ausgibt, landet in einem **Logbuch**, und das Logbuch füllt
das Wiki. Wer `print(block.ore())` schreibt, während er unbekannte Erze abbaut,
sammelt Beobachtungen — und ab genug Beobachtungen kennt das Wiki (und damit
`info()`) die Behandlung des Erzes.

**Geld:** indirekt, aber groß — die Behandlung ist bis zu 50 Reinheitspunkte
wert. **Sinn:** Das Wiki behauptet ohnehin schon, eine *Sammlung* zu sein und
kein Nachschlagewerk (`world.h`, `oreFirst`/`oreSteps`). Das zieht die Idee
konsequent durch: man findet die Welt heraus, indem man sie vermisst.

### 2.6 `shared` → das Gedächtnis über Runden hinweg

Kombiniert mit 2.1 und 2.5 entsteht die schönste Schleife, die das Spiel haben
kann: ein Programm, das die Behandlung **selbst ausprobiert, das Ergebnis misst
und sich merkt**.

```cpp
int gemerkt = shared[name(block.ore())];   // 0 = unbekannt
if (gemerkt == 0) {
    block.mine(Cool);
    shared[name(block.ore())] = probe();   // war die Reinheit gut?
} else {
    block.mine((Care)gemerkt);
}
```

Dafür braucht es nur `item.purity()` (siehe 2.8) und `shared` mit
Zeichenketten-Schlüsseln — beides ist schon da.

**Geld:** Ein selbstlernendes Programm ist gegen den Erzgenerator immun.
**Sinn:** `shared` überlebt den Neustart — das ist genau dann etwas wert, wenn
es etwas zu *lernen* gibt.

### 2.7 Die Lücken schließen — und als Punkte verkaufen

Für `switch`, `?:`, `goto`, Rekursion und Container gibt es zwei Wege.
Empfehlung: **beides** — erst in `codecheck.cpp` sperren, dann als eigene
Punkte verkaufen, und zwar mit echtem Vorteil, sonst kauft sie niemand.

| Punkt | Sperren über | Was man dafür bekommt |
|---|---|---|
| `switch` | Wort `switch` | Die ganze Verzweigung kostet **eine** Zeile statt einer je `else if`. Bei 20 Erzen der stärkste Tempo-Kauf im Spiel. |
| `ternary` | `?` im Tokenizer mit aufnehmen | Bedingung ohne eigene Zeile |
| `goto` | Wort `goto` | Sehr billige Schleife, aber mit Nachteil (z. B. keine `speed`-Boni darin) — der Fluch-Punkt |
| `recursion` | Funktion ruft sich selbst auf | Schleife ohne Schleifenpunkt |
| `container` | `vector`/`map`/`array` und `[` nach Bezeichner | Feste Obergrenze an Elementen, die an `variable+` hängt |

**Geld:** `switch` allein macht aus einer 20-Erz-Kette von 20 Zeilen eine — bei
3 Zeilen/s über sechs Sekunden je Block.
**Sinn:** Was man umsonst haben kann, kauft man nicht. Heute ist der halbe Baum
für jeden, der C++ kennt, ein Placebo.

### 2.8 Kleinkram, der sofort etwas bringt

Alles davon ist eine Nachricht im Protokoll und ein paar Zeilen in
`native.cpp` — das Muster steht schon da:

```cpp
int   item.count(Gold);       // ck::inBag() gibt es schon, nur nicht durchgereicht
int   item.purity(Gold);      // Reinheit des Stapels
bool  job.busy();             // läuft gerade ein Auftrag?
float job.progress();         // wie weit
int   money();                // wie viel habe ich
float round.left();           // wie viel Zeit noch
float block.loading();        // wie lange, bis er wieder da ist
void  wait(float sekunden);   // offizielles Warten, kostet EINE Zeile
```

`job.busy()` und `wait()` sind die beiden wichtigsten:

- **`job.busy()`** macht aus dem einen Auftragsplatz erst ein Planungsproblem.
  Heute kann man nur blind draufhauen.
- **`wait()`** ersetzt die Warteschleife. Und es stopft nebenbei das Loch mit
  `sleep_for` aus 1a: Warten im Schlaf ist heute schon kostenlos, es weiß nur
  niemand. Besser ehrlich anbieten und über die Wartezeit balancieren.

---

## 3. Neue Mechaniken, die das Spiel lebendiger machen

### 3.1 Mehrere Blöcke — eine Ader statt eines Steins

```cpp
for (int i = 0; i < 6; i++)
    if (vein[i].isThere())
        vein[i].mine(info(vein[i].ore())->care);
```

Der Block wird zu einem **Feld**. Das ist der Moment, in dem Indizes, `for`,
Arrays und Zeiger nicht mehr erklärt werden müssen — sie sind einfach der Weg.
Zusammen mit 2.2 wird daraus Arbeitsteilung: eine Konsole je Abschnitt der Ader.

**Geld:** Abbau ist heute durch `respawnSeconds` gedeckelt, nicht durch Code.
Mehr Blöcke heben genau diesen Deckel — und zwar so, dass besserer Code mehr
bringt statt nur mehr Klicks.

### 3.2 Mehrere Öfen — der härteste Deckel fällt

Ein Auftragsplatz ist die schärfste Grenze im Spiel: das ganze
Verarbeitungsnetz aus `data/verarbeitung.json` (acht Schritte, ein Netz, keine
Kette) schnurrt in der Praxis auf "der Ofen ist eh belegt" zusammen. Kaufbare
Maschinen ändern das:

- Öfen, Waschtröge und Pressen als eigene Gegenstände, jeder mit eigenem Platz
- verschiedene Maschinen können verschiedene Schritte (Ofen: `smelt`/`cast`/
  `alloy`; Trog: `wash`/`clean`)
- eine **Warteschlange, die man selbst programmiert** — das ist Scheduling, und
  Scheduling ist genau die Sorte Aufgabe, für die dieses Spiel gebaut ist

**Geld:** Durchsatz multipliziert sich. **Sinn:** Das Verarbeitungsnetz ist
schon jetzt zu reich für einen einzigen Platz — die Daten warten auf die
Maschinen.

### 3.3 Aufträge und Verträge

Während der Runde trudeln Aufträge ein:

> *"20× Kupfer, poliert, mindestens 80 % Reinheit, in 3 Minuten — 5× Preis."*

Das gibt der Runde einen Puls, macht Reinheit zum ersten Mal zu einem Ziel
statt zu einer Nebenwirkung, und belohnt eine gebaute **Pipeline** statt eines
`while (true) { mine(); sell(); }`.

**Sinn:** Das Rundenziel ist heute die einzige Struktur, und es ist eine Miete —
etwas, das man abwehrt. Aufträge sind etwas, das man *will*.

### 3.4 Analyse: neue Erze im Spiel herausfinden

`assay(Ore)` — dauert ein paar Sekunden, kostet Geld, und danach liefert
`info(ore)` Wert, Seltenheit und **Behandlung**. Das ist der Gegenspieler zum
Erzgenerator: heute erfindet er endlos Erze, und der Spieler muss endlos ins
Wiki schauen und `if`-Ketten nachpflegen. Mit `assay` wird daraus eine Aufgabe,
die *im Programm* gelöst wird.

Zusammen mit 2.1, 2.5 und 2.6 ist das der stärkste Verbund im Dokument.

### 3.5 Strom und Kohle

Maschinen brauchen Energie, ein Generator verbrennt Kohle. Damit:

- **Kohle bekommt endlich eine Aufgabe.** Mit Wert 3 ist sie heute fast wertlos
  und wird nach Runde 2 nie wieder angesehen.
- Es entsteht ein **Gleichgewicht**, das man ausrechnen muss: mehr Öfen
  brauchen mehr Kohle, und die abzubauen kostet Blockzeit.
- Ein Grund, die eigene Anlage **umzubauen**, statt sie nur zu vergrößern.

### 3.6 Markt mit Preisbewegung

`market.price(Gold)` schwankt über die Runde. Verkaufen wird ein
Timing-Problem — also eine Programmieraufgabe:

```cpp
if (market.price(Gold) > shared["gold_schnitt"]) item.sell(Gold);
```

**Geld:** Wer wartet, verdient mehr. **Sinn:** `variables`, `if` und `shared`
bekommen in einem Zug etwas zu tun, und das Logbuch aus 2.5 wird nützlich.

### 3.7 Werkzeug und Verschleiß

Spitzhacken mit Stufen und Haltbarkeit, `tool.repair()`. Ein Geldabfluss, der
nicht der Miete gehört, und ein Grund für Wartungslogik im Programm. Mit 3.5
zusammen entsteht eine Anlage, die man **pflegen** muss — das ist der
Unterschied zwischen "läuft" und "lebt".

### 3.8 Lagerplatz

Die Tasche ist heute unbegrenzt. Mit einer Obergrenze wird Sortieren zur
Pflicht: was hebt man auf, was verkauft man sofort? `item.count()` aus 2.8 wird
damit von "nett" zu "notwendig".

### 3.9 Sichtbarkeit: die Effizienz zeigen

Eine kleine Anzeige mit zwei Zahlen: **Geld pro ausgeführter Zeile** und
**Zeilen pro Runde**. Billig zu bauen, und sie macht die eigentliche Fähigkeit
des Spiels sichtbar. Ohne sie optimiert man im Dunkeln — mit ihr hat man zum
ersten Mal eine Zahl, die man schlagen will.

---

## 4. Late Game

Was spät fehlt, steht in Abschnitt 0: Der Durchsatz addiert sich, das Ziel
verdoppelt sich, und es gibt nichts zu **bauen** — nur ein weiteres Erz, das
sich anfühlt wie das vorige. Sieben Vorschläge, der Wichtigkeit nach.

### 4.1 Kerne: Nebenläufigkeit als Endgame-Fähigkeit

Der Ausbau von 2.2 zu Ende gedacht. Man kauft **Kerne**; jeder Kern lässt eine
Konsole echt nebenläufig laufen. Dann kommen die Punkte, die es in einem
C++-Spiel geben *muss*:

| Punkt | wofür |
|---|---|
| `core+` | ein weiterer Kern |
| `atomic` | `shared` ohne Wettlauf hochzählen |
| `mutex` | den Ofen für sich reservieren |
| `queue` | eine Auftragsliste zwischen den Konsolen |

Und der Fehler, den man dabei macht — zwei Konsolen greifen nach demselben Ofen —
ist ein *echter* Nebenläufigkeitsfehler in einem *echten* Programm. Kein anderes
Spiel kann das anbieten. Das ist die naheliegendste Endgame-Idee, die dieses
Projekt hat.

### 4.2 Die Fabrik: das Programm wird Steuerung statt Arbeiter

Maschinen auf einem Raster, verbunden durch Bänder. Das Programm bedient nicht
mehr jeden Block einzeln, sondern **steuert die Anlage**: was wird wohin
geleitet, welche Maschine bekommt welchen Auftrag, wann wird umgestellt.

Der Durchsatz hängt dann an der Anlage, nicht mehr am Zeilenbudget — genau der
Deckel aus Abschnitt 0 fällt. Und "eine Anlage umbauen, weil sich der Markt
gedreht hat" ist der Grund, warum Fabrikspiele spät noch tragen.

### 4.3 Der Compiler als Ausbaustufe

`inline`, `-O2`, `cache`, `PGO` als späte, teure Punkte (siehe 2.3), am Ende ein
**JIT**: ein markierter Abschnitt läuft einen Tick lang mit voller
Geschwindigkeit statt zeilenweise.

```cpp
fast {                       // läuft in EINEM Tick, kostet eine Zeile
    for (int i = 0; i < 100; i++) job.queue(Copper, Smelt);
}
```

Thematisch sitzt das im Kern des Spiels: hier wird wirklich kompiliert. Und es
gibt der Zeile als Währung eine letzte Ausbaustufe, statt sie mit +0,25 endlos
weiterzuschieben.

### 4.4 Prestige: eine Schicht tiefer

Man steigt ab. Geld und Skilltree gehen auf Anfang, ein dauerhafter
Multiplikator bleibt — **und der Code bleibt.** Das ist die Pointe, die nur
dieses Spiel haben kann:

> Der Spielstand ist nicht dein Geld. Der Spielstand ist dein Programm.

Beim zweiten Durchgang beginnt man mit einer fertigen Bibliothek und schafft in
zehn Minuten, wofür man vorher zehn Runden gebraucht hat. Man sieht seinem
eigenen Fortschritt zu, nicht dem einer Zahl.

Dazu passt eine **Code-Bibliothek**: gespeicherte Programme, benannt, über
Läufe hinweg. Die gibt es heute nicht — Konsoleninhalte hängen am Spielstand.

### 4.5 Tiefe statt Breite bei den Erzen

`oregen.cpp` macht endlos *neue* Erze — die sich alle gleich anfühlen, weil sie
denselben Weg gehen. Stattdessen (oder zusätzlich) mehr **Tiefe** in dem, was
schon da ist. Die Daten können es bereits:

- weitere Zustände hinter `refined` mit großen Faktoren — `zustandswert` in
  `data/verarbeitung.json` ist einfach eine Liste
- **Legierungen aus Legierungen**: `legierbar_mit` gibt es beim Ergebnis schon
  (`data/legierungen.json`), es wird nur nicht genutzt
- Rezepte mit drei oder vier Zutaten aus verschiedenen Zweigen, die damit eine
  echte Pipeline erzwingen

Das ist die billigste Late-Game-Erweiterung im ganzen Dokument: **reine
Datenarbeit, kein C++.**

### 4.6 Ereignisse

Adernrausch, Ofenschaden, Markteinbruch, Stromausfall. Mitten in der Runde,
angesagt und sichtbar. Sie zwingen zu defensivem Code — ein `if` auf einen
Zustand, den man nicht selbst herbeigeführt hat — und das ist genau die
Fähigkeit, die das Spiel lehren will. Außerdem hört eine Runde damit auf, wie
die vorige zu sein.

### 4.7 Ein Ziel jenseits der Miete

Zum Schluss braucht das Geld ein Ziel, das nicht "die nächste Miete" heißt. Zum
Beispiel **eine Maschine, die man über viele Runden zusammenbaut** — jedes Teil
verlangt eine bestimmte Pipeline: dieses 50× veredeltes Elektrum über 90 %
Reinheit, jenes 200× gehärtete Bronze. Man arbeitet nicht mehr gegen den
Countdown, sondern auf etwas zu.

---

## 5. Was zuerst?

| | # | Idee | Aufwand | Wirkung | betroffene Dateien |
|---|---|---|---|---|---|
| ✅ | 1 | `info()` / Erz-Metadaten (2.1) | klein | **sehr hoch** — nimmt dem Generator den Stachel | `native.cpp`, `world.cpp` |
| ✅ | 2 | Kleinkram-API: `count`, `purity`, `job`, `wait`, `money`, `market` (2.8) | klein | hoch | `native.cpp` |
| ✅ | 3 | Lücken in `codecheck` schließen (2.7) | klein | hoch — der halbe Baum wird erst dadurch echt | `codecheck.cpp` |
| ✅ | 4 | Erz-Tiefe: `etch` und `fuse` (4.5) | klein | mittel | `ore.h`, `data/` |
| ✅ | 5 | Mehrere Öfen (3.2) | mittel | **sehr hoch** — hebt den härtesten Deckel | `world.h/.cpp` |
| ✅ | — | `inline` und `-O2`: Funktionen rechnen sich (2.3) | klein | hoch | `instrument.cpp`, `native.cpp` |
| ✅ | 7 | Analyse `assay()` (3.4) | mittel | hoch | `world.cpp`, `native.cpp` |
| ✅ | — | Markt mit Preisbewegung (3.6) | klein | mittel | `world.cpp` |
| ✅ | 6 | Konsolen als echte Prozesse (2.2) | mittel–groß | **sehr hoch** — das Erste, was multipliziert | `native.cpp`, `engine.h` |
| ⬜ | 8 | Mehrere Blöcke (3.1) | mittel | hoch | `world.h/.cpp` |
| ⬜ | 9 | Aufträge (3.3) | mittel | mittel | neu: `contract.cpp` |
| ⬜ | — | Autopilot über `classes` (2.4) | mittel | mittel | `native.cpp`, `main.cpp` |
| ⬜ | — | Logbuch: `print()` füllt das Wiki (2.5) | mittel | mittel | `wiki.cpp` |
| ⬜ | — | Strom und Kohle (3.5), Werkzeug (3.7), Lager (3.8) | mittel | mittel | `world.cpp` |
| ⬜ | 10 | Kerne / Nebenläufigkeit (4.1) | groß | Endgame | `native.cpp`, `skilltree.cpp` |
| ⬜ | 11 | Fabrik-Raster (4.2) | groß | Endgame | neu |
| ⬜ | 12 | Prestige + Code-Bibliothek (4.4) | mittel | Endgame | `save.cpp` |

Der Flaschenhals ist weg: es laufen jetzt so viele Prozesse, wie es Konsolen
mit `main()` gibt, jeder mit eigenem Zeilenbudget. Damit ist der Weg frei für
das, was daran hing — **mehrere Blöcke** (#8) haben jetzt einen Sinn, weil zwei
Konsolen sie sich teilen können, und **Kerne mit `atomic`/`mutex`** (4.1) sind
nur noch die nächste Stufe derselben Idee: heute darf jede Konsole voll laufen,
später könnte die Zahl der Kerne das begrenzen und Wettläufe um den Ofen zum
eigentlichen Spätspiel machen.

Zwei Dinge, die dabei bewusst so entschieden sind:

- **Jede Konsole bekommt das volle Zeilenbudget**, nicht einen Anteil. Zwei
  Konsolen sind also wirklich doppelte Arbeit. Dafür ist `console+` in
  `data/skills.txt` jetzt der teuerste und seltenste Punkt im Baum (Preis 45 ab
  Schritt 10, `rise 1.05`). Wenn sich das zu stark anfühlt: eine Zahl in der
  Datei, kein Neubau nötig.
- **Ein Absturz beendet alle.** Drei Konsolen weiterarbeiten zu lassen, während
  die vierte kaputt ist, hilft beim Suchen nicht.
