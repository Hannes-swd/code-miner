# Ideen — neu, nach Durchsicht des kompletten Codes

Unabhängig von der alten Ideenliste. Ausgangspunkt: der jetzige Stand des Spiels, wie er
sich aus `src/skill.h`, `quest.h`, `world.h`, `prestige.h` und den `data/*.json`-Dateien
ergibt — also inklusive Verarbeitungskette, Legierungen, Markt, Aufträgen, Erbe/Prestige
und den Power-Fähigkeiten. Alles hier ist ein Vorschlag, nichts ist entschieden.

Sortiert von "naheliegende Erweiterung des Bestehenden" zu "größerer Umbau".

---

## 1. Mehr Sprache als Paywall

Der Skilltree sperrt bisher: Kontrollfluss (`while`/`if`/`else`/`for`/`switch`/`ternary`/
`goto`/`recursion`), Variablen, Klassen, Funktionen, Container. Das lässt sich weiterziehen,
ohne das Prinzip zu ändern — `codecheck.cpp` tastet ja schon nach Schlüsselwörtern ab:

- **Lambdas** (`[](){}`) — als eigener Punkt hinter `Function`. Macht `std::sort` mit
  eigenem Vergleich oder ein `for_each` über die Tasche möglich, ohne gleich Templates zu
  brauchen.
- **`<algorithm>`-Funktionen** (`std::sort`, `std::count_if`, `std::accumulate`, ...) als
  eigene Freischaltung, unabhängig von `Container`. Damit lohnt sich ein `std::vector<Item>`
  aus der Tasche wirklich zu bauen statt nur `item.count()` zu benutzen.
- **Vererbung/`virtual`** — spät, hinter `Class`. Erlaubt z. B. eine eigene `Strategy`-Basis-
  klasse für "wie behandle ich welches Erz", die man austauscht statt überall `if`-Ketten zu
  schreiben.
- **`std::thread`** — thematisch der Knaller: bisher ist Parallelität *nur* über mehrere
  Konsolen möglich (siehe KONZEPT.md, "+1 Konsole" ist bewusst selten und teuer). Ein
  einzelner Prozess, der sich selbst einen Thread abspaltet, wäre eine zweite, ganz andere
  Art von Parallelität — und passt gut als sehr später, sehr teurer Punkt, weil er das
  bestehende Zeilenbudget-Modell (`ck::line()` wartet auf Freigabe) technisch herausfordert.
- **`try`/`catch`** — als Absicherung gegen eigene Fehler, z. B. wenn man `item.sell()` mit
  einem ungültigen Namen aufruft. Aktuell gibt es dafür nur Rückgabewerte.
- **`constexpr`** — kostet nichts an Laufzeit, aber als Freischaltung ein netter, sehr
  billiger früher Punkt für "das ist eine andere Art, eine Zahl hinzuschreiben".

## 2. Assays und Wissen als eigene Wirtschaft

`assay(erz)` kostet aktuell einfach `limits.assayCost`. Daraus ließe sich mehr machen:

- **Forschungsfortschritt pro Erz**: das erste `assay()` eines Erzes verrät wenig, jedes
  weitere (bis zu einer Grenze) verrät mehr — Seltenheit, Legierungsrezepte, Marktschwankungs-
  breite. Macht `assay()` zu einer echten Entscheidung statt einem Einmal-Haken.
- **"Unbekanntes Erz"-Ereignis**: mit steigendem Level taucht selten ein Erz auf, das noch
  gar keinen Namen hat ("Erz #17"), bis es dreimal `assay()`-t wurde. Baut auf `RollNewOres`
  auf, ohne an dessen Timing etwas zu ändern.

## 3. Räumliche Tiefe statt nur ein Block

Aktuell ist "die Mine" ein einzelnes Feld mit einem Block drin (`world.ore`, `DrawMineCard`).
Eine Erweiterung, die am Kern nichts kaputt macht, weil sie additiv ist:

- **Schächte/Tiefe**: mehrere "Ebenen", freigeschaltet über Fortschritt statt Geld — auf
  jeder Ebene ein eigenes Set an Erzen (ähnlich wie `oreGen.problems` heute schon je Level
  neue Erze auswürfelt, nur räumlich gruppiert statt nur zahlenmäßig). `block.descend()`
  oder ähnlich als neuer, später Befehl.
- **Adern**: mehrere Blöcke desselben Erzes hintereinander, mit einem kleinen Bonus fürs
  lückenlose Durcharbeiten (Combo-Zähler) — ein sichtbares, kleines Zusatzziel innerhalb
  einer Runde, ganz ohne neues Quest-System.

## 4. Ein Debug-Zweig als spätes Privileg

KONZEPT.md sagt ausdrücklich: kein Variablen-Panel, kein Einzelschritt, keine Haltepunkte —
**am Anfang**. Als sehr späte, sehr teure Skilltree-Äste ergäbe genau das Gegenteil einen
schönen Bogen: du fängst als Anfänger ohne Werkzeuge an, am Ende bekommst du die Werkzeuge
eines echten Entwicklers geschenkt, weil du sie dir verdient hast.

- **Haltepunkte**: ein Klick auf eine Zeilennummer hält `ck::line()` dort an, bis man
  weiterklickt — technisch nur ein weiterer Zustand im schon vorhandenen "wartet auf `g`"-
  Protokoll (`engine.h`, `Native`).
- **`watch(name)`**: ein neuer Spielbefehl, der einen `shared[...]`-Wert dauerhaft in einer
  kleinen Übersicht anzeigt statt ihn jedes Mal zu `print()`-en.
- **Ein Mini-Profiler**: zeigt nach einem Lauf, welche Zeile am häufigsten drankam — nützlich,
  sobald `Optimize`/`Inline` gekauft sind und man wirklich am Zeilenbudget optimiert.

## 5. Erbe/Prestige mit echten Entscheidungen statt nur Zahlen

`prestige.h` sagt selbst: bewusst klein und listet feste, generische Zahlen-Boni (Geld pro
Block, Rundenzeit, ...). Eine Erweiterung, die den bestehenden Rahmen (Angebote ziehen,
Punkte ausgeben) beibehält, aber mehr Charakter reingibt:

- **Sich gegenseitig ausschließende Pfade**: z. B. "Händler" (Marktschwankung kleiner, aber
  Verkaufspreis dauerhaft niedriger) gegen "Spekulant" (Schwankung größer, Höchstpreis
  höher). Eine echte Wahl statt eines Sammelsuriums an Plus-Prozenten.
- **Vermächtnis-Belohnung für einen Spielstil**: eine Stufe, die sich nur freischaltet, wenn
  die verlorene Runde ohne eine einzige Fehlbehandlung lief (`CleanStreak` gibt es als
  Quest-Metrik schon, ließe sich hier wiederverwenden) — belohnt sauberen Code auch über den
  Neuanfang hinaus, nicht nur reines Durchhalten.

## 6. Herausforderungen abseits der laufenden Runde

Aktuell gibt es genau einen Fortschrittsdruck: die laufende Runde und ihr Ziel. Eine zweite,
freiwillige Schiene, die nichts am Kernloop ändert:

- **Katas**: kleine, in sich geschlossene Programmieraufgaben im Wiki oder einer eigenen
  Seite ("schreib eine Funktion, die die Summe der Tasche zurückgibt"), mit einem
  automatischen Check über die schon vorhandene Instrumentierung/Pipe-Nachrichten statt
  einem neuen Ausführungsweg. Belohnung: ein einmaliger, kleiner Bonus, keine Dauerschleife.
- **Lokale Bestenliste "kürzestes Programm"**: für ein festes Zwischenziel (z. B. "erste 100
  Geld") die wenigsten geschriebenen Zeichen/Zeilen — rein lokal gespeichert, kein Server
  nötig, baut auf dem ohnehin gespeicherten Konsolentext auf (`save.cpp`).

## 7. Sichtbarkeit von Parallelität

Mehrere Konsolen = mehrere echte Prozesse (siehe KONZEPT.md) — aber im Bild sieht man davon
nichts außer mehreren Editor-Fenstern. Eine rein visuelle Ergänzung, keine Regeländerung:

- Jede Konsole mit eigenem `main()` bekommt eine kleine, eigene Markierung am Block/an der
  Mine-Karte, während ihre Zeile gerade läuft — macht sichtbar, dass da wirklich zwei
  Programme gegeneinander/miteinander arbeiten, nicht nur zwei Texteditoren nebeneinander.

## 8. Handel zwischen Ständen statt nur Markt

Der Markt ist aktuell ein einzelner schwankender Kurs pro Erz. Zusätzlich, additiv:

- **Befristete Nachfrage-Ereignisse**: "Der Markt sucht gerade Kupfer, +40% für die nächsten
  90 Sekunden" — sichtbar auf der Marktseite, abfragbar über einen neuen, späten Befehl
  (`market.demand(erz)`), baut direkt auf `world.marketSwing`/`marketFactor()` auf, nur mit
  einer zusätzlichen, befristeten Erz-spezifischen Komponente statt der globalen Sinuskurve.

## 9. Kuriositäten — sehr seltene Funde ohne echten Nutzen (umgesetzt)

Idee vom Spieler: Dinge, die man beim Abbau extrem selten findet und die (fast) nichts
bringen — der Reiz ist allein, dass es sie gibt. Kein zweites Wirtschaftssystem, kein
Grind-Ziel, nur ein stiller Bonus fürs Dabeibleiben.

Umgesetzt in `world.h`/`world.cpp`: 20 Plätze (`World::kCuriosityCount`), 1-zu-6000-Chance
je abgebautem Block (`kCuriosityChance` in `world.cpp`), Aussehen rein aus der Nummer
generiert (`CuriosityColors`, goldener Winkel fürs Farbrad — keine Datei, kein Name nötig).
Anzeige über `DrawCuriosityShelf`: kleine Kacheln unten links, immer im Vordergrund, leer
und unsichtbar bis zum ersten Fund, die neueste fliegt kurz ein. Speicherung als eigene
Zeile `kuriositaet` im Spielstand (`save.cpp`), alte Spielstände bleiben kompatibel.

- **Fundmechanik**: bei jedem `block.mine()` eine feste, sehr kleine Chance (z. B. 1 zu
  einigen Zehntausend) — reiner Zufall, nicht an Zeit, Level, Erz oder Skill gekoppelt.
  Wichtig: nichts erhöht die Chance und nichts schaltet sie frei — man bekommt sie nicht,
  *weil* man lange gespielt hat, sondern es ist einfach immer gleich unwahrscheinlich, und
  wer lange spielt, hat rein rechnerisch mehr Versuche gehabt. Sobald ein Treffer da war,
  ist genau diese eine Kuriosität dauerhaft freigeschaltet (im Sinne von: eingesammelt,
  bleibt in der Sammlung).
- **Aussehen erst nach Fund**: jede Kuriosität sieht anders aus (eigenes Icon/Farbe/Muster,
  ähnlich `farbe1`/`farbe2`/`muster` bei den Erzen), aber man sieht das Aussehen nicht
  vorher — kein Platzhaltersymbol, kein Umriss, keine Zähl-Anzeige ("3/20"), die verraten
  würde, wie viele es gibt oder wie weit man ist. Vor dem Fund existiert der Slot einfach
  nicht sichtbar; nach dem Fund taucht er auf, fertig gestaltet.
- **Kein Name nötig**: keine Textzeile, kein Flavor-Text — nur das Bild selbst. Passt auch
  besser dazu, dass man nicht erklärt bekommt, was man da eigentlich gefunden hat.
- **Wirkung**: die meisten Kuriositäten tun schlicht nichts; ein paar könnten einen kaum
  messbaren Dauereffekt haben. Keine darf eine Bau- oder Verkaufsentscheidung wirklich
  beeinflussen, sonst wird aus der Sammlung eine Pflichtliste.
- **Anzeige**: eigene Wiki-/Sammlungs-Seite nach demselben Prinzip wie `erze` (siehe
  `data/wiki.json`, "Kategorie ist anders") — ein Eintrag entsteht erst, wenn das Ding
  tatsächlich gefunden wurde.
- **Inhalt aktuell nur Platzhalter**: erstmal ~20 visuell unterschiedliche, aber inhaltlich
  leere Slots (Platzhalter-Icons). Später passend zum Thema "man gräbt nach Erzen" befüllbar
  mit Fossilien, Knochen, alten Bildern/Gemälden o. Ä. — eher archäologischer Fund als
  Schmuckstück, das passt besser zur Mine als Ring/Amulett-Fantasy-Loot.
- **Datenhaltung**: eigene `data/kuriositaeten.json`, schlanker als `erze.json` — im
  Wesentlichen nur Icon-Daten (Farben/Muster oder später ein Bildpfad) pro Slot, kein
  `name`, kein `wert`, kein `seltenheit`-Verkaufsfeld, optional ein `effekt`-Feld, das in
  den meisten Fällen fehlt.

---

Keine dieser Ideen ist mit den bestehenden Dateien abgeglichen (`data/skills.txt`,
`data/erbe.json`, ...) — das wäre der nächste Schritt, falls eine davon weiterverfolgt wird.
