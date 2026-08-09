# Skilltree — Konzept

Gehört zu [KONZEPT.md](KONZEPT.md). Hier steht nur der Skilltree.

---

## 1. Die Grundidee: die Sprache selbst ist die Paywall

Nicht Zahlen werden freigeschaltet, sondern **C++ selbst**. Am Anfang darfst du genau das:

```cpp
int main() {
    block.mine();
}
```

Eine Konsole, ein Befehl. Kein `while`, kein `if`, kein `print`. Du drückst ▶, bekommst
**+1 Geld**, drückst wieder ▶ — das ist der Klicker-Anfang.

Der erste Kauf ist **`while`**. Ab da läuft es von selbst, und du kaufst dich Stück für Stück
in eine richtige Programmiersprache hinein.

---

## 2. Was gekauft wird

### Sprache freischalten (einmalig)

| Fähigkeit | Was sie erlaubt |
|---|---|
| `while` | Schleifen — **eine** zunächst |
| `if` | Bedingungen — **eine** zunächst |
| `else` | braucht `if` |
| `for` | Zählschleifen |
| `print()` | Ausgabe in die Zeile unten |
| `block.isThere()` | nachschauen, ob der Block da ist (auch `isLoading`) |
| `shared[...]` | Werte, die einen Neustart überleben |

### Mengen erhöhen (stapelbar)

| Fähigkeit | Wirkung |
|---|---|
| +1 Schleife | eine `while`/`for` mehr im Code erlaubt |
| +1 Bedingung | ein `if` mehr im Code erlaubt |
| +1 Konsole | eine Konsole mehr |

### Werte verbessern (stapelbar)

| Fähigkeit | Wirkung |
|---|---|
| Geschwindigkeit | +0,5 Zeilen pro Sekunde |
| Geld pro Block | +1 |
| Nachwachsen | −8 % Wartezeit |

---

## 3. Wie die Sperre durchgesetzt wird

**Vor** dem Kompilieren wird der Code abgetastet — mit demselben Verfahren wie die
Instrumentierung (Kommentare und Zeichenketten werden übersprungen). Gezählt wird, wie oft
`while`, `for`, `if`, `print(` usw. vorkommen.

Passt etwas nicht, wird **gar nicht erst kompiliert**, und die Meldung landet dort, wo auch
Compiler-Fehler landen: in der Ausgabezeile, mit rotem Marker in der richtigen Konsole und
Zeile.

```
Zeile 3: "while" ist noch nicht freigeschaltet — im Skilltree kaufen.
Zeile 7: Du darfst nur 1 Schleife benutzen, hier ist die 2.
```

Das ist bewusst dieselbe Stelle wie bei einem Tippfehler — es gibt keinen zweiten Ort, an dem
man nach Problemen suchen muss.

**Auch gesperrt:** eigene Variablen (`int`, `float`, `bool`, `auto`, …) und `class`/`struct`.
`int main()` zählt dabei nicht mit — das gehört zum Grundgerüst, sonst wäre das Spiel schon in
Zeile 1 zu Ende.

**Nicht gesperrt:** eigene Funktionen und `#include`. Die sind ohne Parser nicht zuverlässig
zu erkennen. Die Sperre sitzt dort, wo sie spürbar ist.

---

## 4. Der Baum

```
                    ┌───┐  Nachwachsen
              ┌─────┤ ○ ├──────○───────○
              │     └───┘
    ┌───┐   ┌─┴─┐   ┌───┐
    │ ● ├───┤ ● ├───┤ ○ │  if
    └───┘   └───┘   └─┬─┘
    Start   while     │     ┌───┐
                      └─────┤ ○ ├──────○───────○
                            └───┘  Tempo
```

- **Wurzel in der Mitte** = die erste Konsole + `block.mine()`. Gehört dir von Anfang an.
- **Ringe nach außen**: Tiefe 1, 2, 3, … Gezeichnet wird das auf einem Raster:
  jeder Knoten liegt direkt neben seinem Elternteil, waagerecht oder senkrecht.
  Ist rundherum kein Platz, hängt er an einem anderen Knoten desselben Rings.
- **Tiefe 1 hat genau einen Knoten: `while`.** Vorher gibt es nichts zu entscheiden.
- Ab Tiefe 2 **teilt sich der Baum auf**. Jeder Knoten hat 1–2 Kinder.
- Kaufbar ist ein Knoten nur, wenn **sein Elternknoten gekauft** ist.
- **Tiefe = Schritt**: ein Knoten auf Tiefe 5 hängt hinter vier anderen. Früher als beim
  5. Kauf kann man ihn also nie haben.

### Was wann kommt, steht in `data/skills.txt`

Der Baum ist **nicht im Programm festgeschrieben**. Beim Start liest das Spiel
`data/skills.txt`. Ändern, Spiel neu starten — fertig, ohne neu zu bauen.

```
#  schlüssel     schritt   preis   anzahl

breite   1 3 4 5 6 6 7 7 8 8 8 8      # wie breit der Baum je Schritt ist

while          1        8    1        # genau Schritt 1
if             2-3     20    1        # irgendwo zwischen Schritt 2 und 3
if             5-10    20    1        # zweite Chance
variablen      2-4     14    1
klassen        4-8     34    1
tempo          2-12    10    füllen 3 # füllt den Rest auf, Gewicht 3
```

- **schritt** ist der Bereich, in dem der Punkt liegen darf — `4` oder `2-6`.
- **anzahl** ist entweder eine Zahl (so oft liegt der Punkt fest im Baum) oder `füllen N`
  (damit wird der Rest des Baums aufgefüllt, `N` ist das Gewicht).
- Ein Schlüssel darf **mehrmals** vorkommen. Deshalb gibt es `if` zweimal: wer sich bei
  Schritt 2 dagegen entscheidet, kommt zwischen 5 und 10 noch einmal daran.
  **Man kann sich verplanen, aber nicht aussperren.**
- Der Preis wächst mit dem Schritt: `preis × (1 + schritt)^1,55`.
- Alle Schlüssel und was sie bedeuten stehen oben in der Datei selbst.

**„+1 Konsole" steht bewusst nicht im Füll-Topf** — eine weitere Konsole ist ein großer
Schritt, den soll es nur an den zwei festen Plätzen geben. Sonst liegt er überall herum.
(Beim ersten Versuch lag er 9× im Baum, das war deutlich zu viel.)

Stimmt an der Datei etwas nicht — Tippfehler im Schlüssel, kein Platz mehr für einen festen
Punkt —, steht das **rot in der Seitenleiste** des Skilltrees, mit Zeilennummer. Nichts wird
stumm verschluckt.

**Der Zufall wird einmal pro Spielstand gewürfelt** (fester Startwert). Der Baum sieht also
bei jedem Start gleich aus — sonst könnte man so lange neu starten, bis er günstig liegt.

### So sieht der erzeugte Baum aus

72 Knoten, alle von der Wurzel erreichbar:

```
Schritt  1: while(23)
Schritt  2: print()(66), Geld(77), Nachwachsen(66)
Schritt  3: Variablen(120), if(171), block.isThere()(137), +1 Schleife(223)
Schritt  4: Tasche(194), Tempo(121), Geld(170), Tempo, Tempo
Schritt  5: else(386), Geld, +1 Konsole(643), Geld, Geld, +1 Schleife
Schritt  6: +1 Bedingung, for(612), Geld, Tempo, Nachwachsen, Nachwachsen
Schritt  7: Klassen(854), +1 Schleife, Geld, Tempo, +1 Bedingung, Geld, Tempo
Schritt  8: Tempo, if(603), Geld, Geld, +1 Bedingung, Tempo, Tempo
...
Schritt 10: ..., shared[...](1851)
Schritt 11: ..., +1 Konsole(1883), ...
```

Preise zum Vergleich: `while` kostet **23**, ein Punkt auf Schritt 3 rund **150**, auf
Schritt 8 rund **500**.

---

## 5. Bedienung

- Seite **Skilltree** in der Menüleiste
- **Zu sehen ist nur das Gekaufte und genau der nächste Schritt dahinter.** Alles
  weiter draußen bleibt verdeckt — sonst steht von Anfang an der ganze Baum auf
  dem Schirm und man sieht den Weg nicht mehr.
- Drei Punkte am Rand einer gesperrten Karte heißen: dahinter geht es weiter.
- Jede Fähigkeit ist eine **Karte** mit Zeichen, Namen und Zustand:
  - **grün gefüllt, mit Schein** = freigeschaltet
  - **grüner Rand, „Kaufen - Preis"** = Geld reicht
  - **grauer Rand, „Gesperrt - Preis"** = Geld reicht nicht
- Karten liegen auf einem **Raster**, jede Verbindung ist eine gerade Linie zum
  Elternteil - grün, wenn der Weg schon gekauft ist. Nichts kreuzt sich.
- Klick wählt aus (rechts: Wirkung, Preis, Kauf-Knopf), **Doppelklick kauft**.
- Die Ansicht zieht sich von selbst auf das zurecht, was zu sehen ist. Ziehen
  verschiebt, Mausrad zoomt.
- **Gekauftes bleibt gekauft.** Es gibt keinen Knopf, der Punkte zurückgibt — sonst
  wäre jede Entscheidung im Baum belanglos.
- Auch **„Block zurücksetzen" gibt es nicht mehr**: von Hand hätte man damit schneller
  abgebaut als mit jedem Programm. Der Block wächst von selbst nach.

---

## 6. Was das im Code bedeutet

Neue Dateien:

```
data/
└── skills.txt         WAS WANN freigeschaltet wird - hier wird geschraubt

src/
├── skillfile.h/.cpp   liest data/skills.txt ein, sammelt Fehlermeldungen
├── skilltree.h/.cpp   Knoten, Erzeugung, Kaufen, daraus abgeleitete Grenzen
├── codecheck.h/.cpp   Code abtasten und gegen die Grenzen prüfen
└── skills.cpp         die Baum-Seite
```

Gesucht wird `data/skills.txt` zuerst im **Projektordner** (von `build/Debug/` aus zwei
Ordner höher), dann neben der exe. So wirkt eine Änderung an der Datei sofort, ohne bauen.

Was sich ändert:

- `Native::kLinesPerSecond` wird aus einer festen Zahl eine **Variable** — der Skilltree
  dreht daran.
- `World::moneyPerBlock` und `World::respawnSeconds` werden **aus dem Baum berechnet**, nicht
  beim Kauf gesetzt. Dadurch stimmen sie immer, egal in welcher Reihenfolge gekauft wurde.
- Vor `engine.start(...)` wird **zuerst geprüft**, dann erst kompiliert.
- „+ Neue Konsole" ist nur so oft erlaubt, wie Konsolen freigeschaltet sind (Knopf ausgegraut).

### Gleicher Code → kein neues Kompilieren

Am Spielanfang drückt man immer wieder ▶ auf **denselben** Code — das ist ja das Klicken.
Jedes Mal 400 ms zu kompilieren wäre unerträglich.

Deshalb merkt sich der Motor den zuletzt zusammengesetzten Quelltext. Ist er unverändert und
die `run.exe` liegt noch da, wird sie **einfach noch einmal gestartet**. Aus 400 ms wird ein
Sofort. Erst wenn man wirklich etwas tippt, wird neu übersetzt.

---

## 7. Bewusst noch nicht drin

- **Kein Speichern.** Beim Neustart des Programms ist alles wieder weg. Das ist der nächste
  logische Schritt, aber ein eigenes Thema.
- **Kein Zurücksetzen / Umskillen.** „Man kann später einen anderen Weg gehen" heißt: man
  kauft den anderen Ast zusätzlich, nicht statt.
- Kein Verschieben/Zoomen im Baum — er wird so gelegt, dass er aufs Bild passt.
