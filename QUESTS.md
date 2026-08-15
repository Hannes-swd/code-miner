# Aufträge: Machbarkeit und 26 Vorschläge

Antwort auf die Frage „wie schwer wäre das?" — und ein Katalog, aus dem sich
das Ganze füllen lässt.

> **Gebaut.** `src/quest.h` / `src/quest.cpp`, `data/quests.json` mit 18
> Auftragsarten, Freischaltung über den Punkt `quests` ab **Schritt 14** — in
> der Praxis um den **100. bis 130. Kauf** herum, die Karte kostet dann gut
> eine halbe Million.
> also frühestens beim 30. Kauf im Baum. Was aus dem Katalog unten noch fehlt,
> steht am Ende dieser Datei.

**Das Modell steht:** ein Auftrag gilt für **genau eine Runde**, ausgewählt in
der Vorbereitung (oder in einer Pause), **freiwillig**. Wer keinen nimmt, spielt
die Runde ohne. Wer einen nimmt und ihn nicht schafft, zahlt **80 % der
Belohnung** vom Geld, das er gerade hat. Beides steht vorher da.

---

## 1. Die kurze Antwort

**Das Auftragssystem selbst ist klein. Das Messen ist die Arbeit.**

Drei zufällige Angebote, eines annehmen, Fortschritt, Belohnung, Frist — das ist
eine Struktur, eine JSON-Datei und eine Seite. Vielleicht 400 Zeilen. Das ist
nicht das Problem.

Das Problem ist: **das Spiel merkt sich fast nichts.** Es weiß, wie viele Blöcke
insgesamt abgebaut wurden (`world.minedCount`) und was gerade in der Tasche
liegt. Es weiß *nicht*, wie viele Steine du gewaschen hast, wie viel Geld diese
Runde durch Verkäufe kam, ob du je falsch behandelt hast, wie viele Zeilen dein
Programm gebraucht hat. Jede Auftragsart, die so etwas wissen will, braucht
einen eigenen Zähler an einer eigenen Stelle.

Deshalb der Aufwand in drei Stufen:

| | Umfang | was dabei entsteht |
|---|---|---|
| **Grundgerüst** | ~1 Sitzung | Struktur, `data/quests.json`, Seite mit den drei Angeboten, Annehmen, Fortschrittsbalken, Belohnung, Frist, Spielstand, Skilltree-Punkt, Wiki-Seite |
| **Die Zähler** | ~1 Sitzung | die 15–20 kleinen Haken im Code, aus denen die Aufträge ihre Zahlen ziehen |
| **Der Katalog** | laufend | jede weitere Auftragsart ist danach *eine Zeile JSON* |

Also: **zwei Sitzungen bis es steht**, danach kosten neue Aufträge fast nichts.
Das ist ungefähr so viel wie der Umbau auf mehrere Prozesse.

---

## 2. Warum 26 Aufträge kaum mehr kosten als 3

Der Trick ist, nicht 26 Sonderfälle zu bauen, sondern **drei Arten von
Fortschritt**. Jeder Auftrag ist dann nur eine Bedingung plus eine Art.

```cpp
enum class Progress
{
    Count,  // ein Ereignis zaehlt hoch: abgebaut, verkauft, gewaschen
    Hold,   // eine Bedingung muss T Sekunden am Stueck gelten
    Once    // eine Bedingung muss EINMAL erfuellt sein
};
```

- **Count** ist ein Zähler, der bei einem Ereignis hochgeht und nie zurückfällt.
  „Bau 500 Blöcke ab."
- **Hold** ist der interessante: die Bedingung wird jedes Bild geprüft. Gilt
  sie, läuft eine Uhr; gilt sie nicht mehr, **springt die Uhr auf null zurück**.
  Genau damit funktioniert „wasch einen Stein und behalt ihn 5 Minuten".
- **Once** ist eine Momentaufnahme. „Hab von jedem Zustand mindestens ein Stück
  gleichzeitig."

Mehr braucht es nicht. `Hold` ist die einzige Art, die überhaupt neue Logik
verlangt, und das sind zehn Zeilen:

```cpp
void Quest::tick(float dt, const World& world)
{
    if (kind != Progress::Hold)
        return;

    if (erfuellt(world))
        held += dt;
    else
        held = 0.0f;   // losgelassen heisst von vorne
}
```

---

## 3. Was in einer Runde überhaupt zu schaffen ist

Damit die Zahlen nicht geraten sind — eine Runde dauert **300 Sekunden**
(`data/runden.json`).

| | früh | spät |
|---|---|---|
| Nachwachsen | 0,6 s | ~0,2 s (mit `regrow`) |
| Abbaudauer Stein / Gold / Diamant | 0,5 / 2,2 / 3,5 s | dasselbe |
| Tempo | 3 Zeilen/s | 15–40 Zeilen/s |
| **Blöcke pro Runde (Stein)** | **~250** | **~600–900** |
| **Blöcke pro Runde (Gold)** | ~110 | ~130 (Abbaudauer bremst, nicht der Code) |
| Aufträge (Öfen) pro Runde | ~150 mit 1 Ofen | ~600 mit 4 Öfen |

**Zu deiner 10k-Frage: ja, das ist zu viel für einen Auftrag** — das sind
20 bis 40 Runden. Als *Dauerauftrag über mehrere Runden* wäre es eine schöne
Sache, aber als normale Aufgabe liegt die richtige Größe bei **200–1500**.

Faustregel für die Frist: ein Auftrag sollte **1 bis 3 Runden** brauchen. Alles
darüber vergisst man, alles darunter ist kein Auftrag, sondern ein Nebenbei.

---

## 4. Der Katalog

Spalte **Hook** sagt, was im Spiel dafür mitgezählt werden muss.
✅ = gibt es schon, ➕ = ein neuer Zähler, ⚙ = etwas mehr Arbeit.

### A — Menge. Laufen nebenbei mit, während das Programm arbeitet.

| # | Auftrag | Art | Hook |
|---|---|---|---|
| 1 | Bau **N Blöcke** ab | Count | ✅ `minedCount` |
| 2 | Bau **N Blöcke von genau diesem Erz** ab | Count | ➕ Zähler je Erz |
| 3 | Verdien insgesamt **N Geld** | Count | ➕ Summe beim Verkauf |
| 4 | Verarbeite **N Stücke** (egal welcher Schritt) | Count | ➕ in `tickCraft` |
| 5 | Mach **N Stücke einer Legierung** | Count | ➕ in `tickCraft` |

Die sind der Einstieg: man nimmt sie an und spielt einfach weiter. Ein Spiel,
das *nur* solche hätte, wäre allerdings langweilig — sie verlangen nichts.

### B — Güte. Zwingen dazu, sich um Reinheit und Behandlung zu kümmern.

| # | Auftrag | Art | Hook |
|---|---|---|---|
| 6 | Liefer **N Stück X mit mindestens P % Reinheit** | Count | ➕ Reinheit beim Verkauf prüfen |
| 7 | Bau **N Blöcke ab, ohne ein einziges Mal falsch zu behandeln** | Count | ➕ Serie, bricht bei `lastCareLoss > 0` |
| 8 | Bring den **Schnitt deiner Tasche über P %** und halt ihn | Hold | ✅ `inventoryPurity` |
| 9 | Verkauf **ein einzelnes Stück für mindestens M Geld** | Once | ➕ höchster Einzelpreis |

Nr. 7 ist der schönste der Gruppe: er macht aus `info()` und `block.mine(Cool)`
zum ersten Mal eine Pflicht statt einer Optimierung. Und er ist gnadenlos —
einmal daneben und die Serie fängt von vorne an.

### C — Halten. Kämpfen gegen den Reflex, sofort zu verkaufen.

| # | Auftrag | Art | Hook |
|---|---|---|---|
| 10 | **Behalt N Stück X für T Sekunden** | Hold | ✅ Tasche |
| 11 | Hab **von jedem Zustand gleichzeitig ein Stück** (roh bis verschmolzen) | Once | ✅ Tasche |
| 12 | Beende die Runde mit **mindestens N Stück** in der Tasche | Once | ⚙ beim Rundenende, siehe Warnung unten |
| 13 | **Verkauf T Sekunden lang gar nichts**, während du weiter abbaust | Hold | ➕ Zeit seit dem letzten Verkauf |

Das ist die Gruppe, die dein „Stein waschen und 5 Minuten behalten" umsetzt —
und die interessanteste, weil sie gegen alles arbeitet, was man sonst tut.

### D — Tempo. Zwingen zu effizientem Code statt zu mehr Zeit.

| # | Auftrag | Art | Hook |
|---|---|---|---|
| 14 | Bau **N Blöcke in T Sekunden** ab | Count + Uhr | ➕ Frist je Auftrag |
| 15 | Verdien **N Geld in den letzten 60 Sekunden** einer Runde | Count | ✅ `roundLeft` |
| 16 | Halt **alle Öfen T Sekunden am Stück beschäftigt** | Hold | ✅ `jobsIdle() == 0` |
| 17 | Halt **den Block T Sekunden lang nie leer stehen** | Hold | ✅ `blockAlive` |

Nr. 16 und 17 sind Auslastungsaufgaben: nicht „mach mehr", sondern „lass nichts
stehen". Genau das lernt man an diesem Spiel.

### E — Markt. Ohne `market.price()` nicht zu schaffen.

| # | Auftrag | Art | Hook |
|---|---|---|---|
| 18 | Verkauf **N Stück X jeweils über dem Durchschnittspreis** | Count | ➕ Marktfaktor beim Verkauf prüfen |
| 19 | Mach **M Geld in einem einzigen Verkauf** | Once | ➕ höchster Einzelverkauf |

### F — Entdecken. Zwingen ins Wiki und zu `assay()`.

| # | Auftrag | Art | Hook |
|---|---|---|---|
| 20 | **Untersuche N unbekannte Erze** | Count | ✅ `assayed.size()` |
| 21 | **Finde einen neuen Verarbeitungsweg** (eine Kante, die es noch nicht gab) | Count | ✅ `oreSteps.size()` |
| 22 | Hab **N verschiedene Erze gleichzeitig** in der Tasche | Once | ✅ Tasche |
| 23 | Bring **irgendein Erz zum ersten Mal auf „verschmolzen"** | Once | ✅ `oreFirst` |

### G — Programmform. Die Gruppe, die es so nur in diesem Spiel geben kann.

| # | Auftrag | Art | Hook |
|---|---|---|---|
| 24 | Schaff **N Geld mit einem Programm unter L Zeilen** | Once | ➕ Zeilen im Quelltext zählen |
| 25 | Schaff **N Geld mit weniger als L ausgeführten Zeilen** | Once | ➕ Zähler im Motor |
| 26 | Lass **zwei Konsolen gleichzeitig T Sekunden lang laufen** | Hold | ✅ `mProcs` |

**Nr. 25 ist der beste Auftrag im ganzen Katalog.** Er misst genau das, worum es
in diesem Spiel eigentlich geht: nicht wie lange du spielst, sondern wie gut
dein Code ist. Der Motor gibt jede Zeile einzeln frei — mitzählen ist eine
Zeile Code in `Native::update`. Und es ist die einzige Aufgabe, die man nicht
durch Warten lösen kann.

---

## 5. Das Modell: ein Auftrag, eine Runde, freiwillig

### 5.1 Der Ablauf

```
Vorbereitung  ->  drei Angebote liegen da
                  Belohnung und Strafe stehen bei jedem dran
                  eines annehmen  ODER  keines  ->  Runde ohne Auftrag
Lauf          ->  Fortschritt läuft mit, sichtbar in der Rundenleiste
                  F9-Pause: man darf noch wechseln, solange nichts gezählt hat
Abrechnung    ->  geschafft:      + Belohnung
                  nicht geschafft: - 10 % der Belohnung
                  dann erst das Rundenziel abziehen
```

**Die Reihenfolge am Ende ist wichtig:** erst der Auftrag verrechnen, dann die
Miete abziehen. Andersherum könnte eine Belohnung eine Runde retten, die
eigentlich verloren war — und dann wäre der Auftrag keine Zusatzaufgabe mehr,
sondern die Hauptsache.

### 5.2 Das löst die Verkaufsfalle von selbst

Ich hatte hier vorher eine Warnung stehen: `FinishRound()` verkauft die ganze
Tasche, also wäre „behalt 20 Steine" unlösbar. **Mit einem Auftrag, der genau
eine Runde gilt, ist das weg** — der Auftrag wird beim Rundenende geprüft,
*bevor* verkauft wird. Ein Tresor ist nicht nötig.

Was bleibt: eine Haltezeit muss **kürzer als die Runde** sein. Bei 300 Sekunden
Rundendauer sind 4 Minuten das Äußerste, und selbst das heißt: sofort nach dem
Start anfangen. Ich würde bei **60 bis 180 Sekunden** bleiben.

### 5.3 Was aus dem Katalog dadurch herausfällt

Alles, was über eine Runde hinausgeht:

- **Nr. 1 mit großen Zahlen** („bau 10 000 Blöcke ab") — geht nur noch bis etwa
  1500, siehe Abschnitt 3.
- **Nr. 12** („beende die Runde mit N Stück in der Tasche") wird zum Normalfall
  statt zur Aufgabe — der Auftrag endet ja ohnehin dort.
- **Nr. 21** („finde einen neuen Verarbeitungsweg") ist Glückssache innerhalb
  einer Runde. Besser als „finde **N** neue Wege in dieser Runde".

Dafür wird alles aus Gruppe D (Tempo) und Gruppe G (Programmform) besser: die
brauchen ohnehin genau eine Runde als Rahmen.

### 5.4 Die Zahlen: was die 10 % wirklich bedeuten

Belohnung ≈ **50–80 % eines Rundenziels**, mitwachsend über `RoundTarget()`.
Daraus ergibt sich:

| Runde | Rundenziel | Belohnung (~60 %) | Strafe (80 %) |
|---|---|---|---|
| 3 | 2 700 | 1 600 | 1 280 |
| 5 | 10 700 | 6 400 | 5 120 |
| 10 | 135 000 | 81 000 | 64 800 |
| 15 | 740 000 | 444 000 | 355 200 |

**Die Strafe steht jetzt auf 80 %, nicht auf 10 %** — genau deshalb. Mit
+100 % gegen −10 % hätte sich ein Auftrag *immer* gelohnt: selbst wer nur jeden
fünften schafft, stünde am Ende im Plus. Damit wäre „nehmen oder nicht" keine
Wahl gewesen, sondern eine Formalie.

Bei 80 % liegt der Umschlagpunkt bei **0,8 / 1,8 = 44 %** — man muss einen
Auftrag also etwa **vier von neun Malen** schaffen, damit sich das Annehmen
überhaupt rechnet. Ein Auftrag, bei dem man sich nicht ziemlich sicher ist, ist
ab jetzt ein schlechtes Geschäft und keine Gratiswette.

**Trotzdem funktioniert dein Entwurf** — nur aus einem anderen Grund, als es
zunächst aussieht. Die Gefahr ist nicht der Erwartungswert, sondern der
**Ruin**: die Strafe geht vom Geld ab, *das du gerade hast*, und kurz darauf
kommt die Miete. Wer komfortabel dasteht, nimmt jeden Auftrag mit. Wer knapp
über dem Ziel liegt, für den sind 640 die Runde. Die Entscheidung ist also nicht
„lohnt sich das?", sondern **„kann ich mir das jetzt leisten?"** — und die ist
jede Runde anders. Das ist die bessere Frage.

Zwei Stellschrauben, damit es auch spät noch trägt:

- **Die Strafe bleibt je Auftrag einstellbar** (`strafe` in
  `data/quests.json`), auch wenn gerade überall 0.80 steht. Falls sich die
  leichten Aufträge damit zu hart anfühlen, lassen sie sich einzeln
  zurücknehmen, ohne die fetten anzufassen.
- **Strafe nie über das aktuelle Geld hinaus.** Sonst geht man ins Minus, und
  ein negativer Kontostand ist im Spiel nirgends vorgesehen.

### 5.5 Welche drei angeboten werden

Gewürfelt aus dem Katalog, aber **nur aus dem, was man auch kann** — dasselbe
`needs`-Verfahren wie im Skilltree. Ein „mach 20 Stück Elektrum" darf nicht
angeboten werden, solange `alloy` nicht gekauft ist, und „verkauf über dem
Durchschnitt" nicht ohne `market`.

Dazu zwei kleine Regeln, damit sich die Angebote nicht anfühlen wie ein
Zufallsgenerator:

- **Nie zweimal dieselbe Art hintereinander.** Drei Mengenaufträge
  nebeneinander wären keine Auswahl.
- **Gemischte Größe**: einer leicht, einer mittel, einer fett. Dann ist die
  Auswahl eine Entscheidung über Risiko und nicht über Geschmack.

---

## 6. Wo es hingehört

```
neu:      src/quest.h / quest.cpp     die Struktur, das Würfeln, das Prüfen
          data/quests.json            der Katalog, mit hilfe-Abschnitt wie überall
anfassen: world.h                     die Zähler und der angenommene Auftrag
          round.cpp                   Angebote würfeln beim Rundenstart,
                                      verrechnen in FinishRound - VOR dem
                                      Abzug der Miete und vor dem Verkauf
          save.cpp                    Auftrag und Zähler in den Spielstand
          main.cpp                    ticken, Auswahl in der Vorbereitung,
                                      Fortschritt in der Rundenleiste
          skill.h / skilltree.cpp     der Punkt "quests"
          data/skills.txt             ab welchem Schritt
          data/wiki.json              die Erklärseite
          native.cpp                  optional: quest.name(), quest.progress()
```

`round.cpp` ist damit die zweitwichtigste Datei — nicht `main.cpp`. Die
Angebote gehören an den Rundenwechsel, und die Abrechnung gehört in
`FinishRound()`, wo auch das Ziel abgezogen wird.

Das letzte ist eine eigene Überlegung wert: wenn der Spielercode den laufenden
Auftrag **abfragen** kann, lässt sich ein Programm schreiben, das selbst
entscheidet, woran es gerade arbeitet. Das wäre sehr in Richtung dieses Spiels —
aber es ist ein zweiter Schritt, nicht der erste.

### Freischalten

Als eigener Punkt `quests`, etwa ab Schritt 12–18 — also dann, wenn Abbauen und
Verarbeiten sitzen und die reine Wiederholung anfängt, dünn zu werden. Genau da
haben Aufträge ihren Platz: sie geben der Runde etwas, das man *will*, statt nur
etwas, das man abwehrt.

---

## 7. Reihenfolge beim Bauen

1. **Das Gerüst mit genau zwei Auftragsarten**: `Count` und `Hold`, und darin
   Nr. 1 („bau N ab") und Nr. 10 („behalt N Stück X für T Sekunden"). Damit
   stehen deine beiden Beispiele, das Angebot in der Vorbereitung, die
   Verrechnung in `FinishRound` und die Anzeige. Ab hier ist es spielbar.
2. **Die Zähler nachziehen** — Gruppe A und B, das sind die meisten neuen
   Haken auf einmal.
3. **Nr. 7** (N Blöcke ohne eine einzige Fehlbehandlung) — der erste Auftrag,
   der aus `info()` eine Pflicht macht statt einer Optimierung.
4. **Nr. 25** (N Geld unter L ausgeführten Zeilen) — der Auftrag, den es so nur
   in diesem Spiel geben kann. Kostet eine Zeile in `Native::update`.

Der Rest des Katalogs ist danach je eine Zeile in `data/quests.json`.

---

## 8. Was davon jetzt drinsteckt

Gebaut sind **18 der 26** Auftragsarten — alle, die mit den Zählern auskommen,
die es jetzt gibt:

`mined`, `mined_ore`, `earned`, `crafted`, `alloyed`, `assayed`,
`clean_streak`, `sold_above`, `hold_ore`, `bag_purity`, `no_sell`,
`furnaces_busy`, `no_idle_block`, `distinct_ores`, `distinct_states`,
`single_sale`, `frugal`

Noch nicht dabei, weil sie je einen weiteren Haken bräuchten:

- **Nr. 6** „liefer N Stück mit mindestens P % Reinheit" — braucht die Reinheit
  je Verkauf, nicht nur die Summe.
- **Nr. 14** „N Blöcke in T Sekunden" — braucht eine zweite Uhr neben der
  Rundenuhr.
- **Nr. 15** „N Geld in den letzten 60 Sekunden" — braucht einen Zähler, der
  erst spät in der Runde anfängt.
- **Nr. 21** „finde einen neuen Verarbeitungsweg" — als Rundenaufgabe zu sehr
  Glückssache, siehe 5.3.
- **Nr. 24** „Programm unter L Zeilen im Quelltext" — `frugal` misst stattdessen
  die *ausgeführten* Zeilen, was die bessere Frage ist.
- **Nr. 26** „zwei Konsolen T Sekunden gleichzeitig" — die Welt weiß nichts vom
  Motor; bräuchte eine Zahl, die `Native` hineinschreibt.

Jede davon ist ein Zähler plus eine Zeile in `data/quests.json`.

### Zwei Zahlen, die noch nicht erspielt sind

- **`lohn`** liegt jetzt zwischen 0,40 und 0,85 eines Rundenziels. Beim ersten
  Durchlauf zahlte `frugal` mehr als eine ganze Miete — das ist gedeckelt, aber
  ob 0,85 richtig ist, zeigt erst das Spielen.
- **`quests` ab Schritt 14.** Erst stand dort 30 — das war nicht spät, sondern
  unerreichbar: jeder Schritt nach außen kostet das 1,8-fache, bei Schritt 30
  steht ein Punkt am Preisdeckel von einer Milliarde. Nachgemessen (40
  Durchläufe, immer das Günstigste gekauft) liegt der Baum nach 150 Käufen erst
  bei Schritt 18 — er wächst vor allem in die Breite, nicht in die Tiefe.

  Mit 14 taucht der Punkt in etwa der Hälfte der Durchläufe auf, im Mittel beim
  119. Kauf, Kartenpreis rund 607k. Das ist dieselbe Gegend wie `shared` und
  damit wirklich spät — aber erreichbar.

### Dieselbe Frage steht noch bei `etch` und `fuse`

Beide sind in derselben Sitzung dazugekommen und haben dasselbe Problem, nur
schwächer. Aus `Preis × 1,8^(Schritt−1)` ergibt sich:

| Punkt | Schritt | Kartenpreis | entspricht etwa |
|---|---|---|---|
| `etch` | 24 | ~45M | Runde 34 |
| `fuse` | 28 | ~630M | Runde 50 |

Für Endspiel-Inhalt in einem Spiel ohne Ende ist das vertretbar, `fuse` ist
aber grenzwertig. Beides sind zwei Zahlen in `data/skills.txt`.
