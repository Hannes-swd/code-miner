# Ideen: was noch fehlt — Midgame und Late Game

> **Stand:** Aus der ersten Ideenliste ist das meiste gebaut: `info()`/`assay()`,
> die kleine API (`count`, `purity`, `job`, `wait`, `money`, `market`), die
> Lücken in `codecheck.cpp` (`switch`, `ternary`, `goto`, `recursion`,
> `container`), `etch`/`fuse`, **mehrere Öfen**, `inline`/`optimize`, der
> bewegliche Markt, **Konsolen als echte Prozesse** und inzwischen auch die
> **Aufträge** (`data/quests.json`).
>
> Diese Liste fängt danach an. Sie sammelt, was das Spiel in der Mitte und
> spät noch tragen könnte — und nichts davon ist gebaut.

**Wie die Liste zu lesen ist.** Jede Idee muss zwei Prüfungen bestehen, sonst
steht sie hier nicht:

1. **Belohnt sie besseren Code?** Eine Idee, die man mit derselben
   `while (true) { mine(); sell(); }`-Schleife abarbeitet, ist nur eine größere
   Zahl.
2. **Ist sie eine Entscheidung?** Wenn es immer dieselbe richtige Antwort gibt,
   ist es kein Spiel, sondern eine Pflicht.

Deshalb steht bei jeder Idee **Geld** (wofür der Spieler bezahlt wird) und
**Sinn** (warum sie in *dieses* Spiel gehört und nicht in irgendeins).

---

## 0. Wo das Spiel gerade steht

| | Wert | wo |
|---|---|---|
| Tempo am Anfang | 3 Zeilen/s, `speed` +0,5 | `skillfile.h`, `data/skills.txt` |
| Die einzigen Multiplikatoren | `optimize` ×1,2, `console+`, `furnace+` | `data/skills.txt` |
| Blöcke in der Welt | **genau 1** | `world.h` |
| Tasche | **unbegrenzt, verdirbt nie** | `world.cpp` |
| Markt | ±25 % Schwingung, reagiert auf nichts | `market_swing`, `market_speed` |
| Runde | 300 s, Ziel 2,4× → 1,2× | `data/runden.json` |
| Prozesse | einer je Konsole mit `main()` | `native.cpp` |

Die harten Deckel von früher sind gefallen — der eine Ofen und der eine
Prozess. Übrig sind vier weichere, und sie sind alle vom selben Typ: **es gibt
zu wenig, worüber ein Programm entscheiden kann.**

1. **Abbau hängt am Nachwachsen, nicht am Code.** Ein Block, eine Wartezeit.
   Wer schneller ist, wartet länger.
2. **Verkaufen ist nie eine Entscheidung.** Der Preis schwingt nach einer festen
   Kurve, die man nicht beeinflusst. Es gibt keinen Grund, etwas zu *behalten* —
   und ohne diesen Grund ist die Tasche nur ein Zwischenlager.
3. **Der Durchsatz ist eine Zahl, kein Gebilde.** Man kauft `speed`, `furnace+`,
   `console+` — aber man **baut** nichts, was man später umbaut.
4. **Spät passiert nichts Neues, nur mehr davon.** Runde 20 spielt sich wie
   Runde 8, nur mit größeren Zahlen und mehr Erzen.

Die Ideen unten sind nach diesen vier Punkten sortiert.

---

# 1. Midgame

*Ungefähr Runde 5–15, Skilltree-Schritt 10–25. Hier sitzt der Spieler zum ersten
Mal vor einem Programm, das größer ist als der Bildschirm, und die Frage ist
nicht mehr "wie schreibe ich das", sondern "was mache ich zuerst".*

### M1 — Der Block wird zur Ader

*(stand schon in der alten Liste — immer noch die stärkste offene Midgame-Idee)*

```cpp
for (int i = 0; i < vein.size(); i++)
    if (vein[i].isThere())
        vein[i].mine(info(vein[i].ore())->care);
```

Statt eines Blocks ein Feld von 4–8, jeder mit eigenem Erz und eigener
Nachwachszeit. Ein Punkt `vein+` legt einen dazu.

**Geld:** Der Nachwachs-Deckel fällt, und zwar so, dass **Code** ihn hebt und
nicht ein Klick. Wer nur `vein[0]` bedient, hat nichts gewonnen.
**Sinn:** Das ist der Moment, in dem `for`, `container` und Indizes nicht mehr
erklärt werden müssen — sie sind einfach der einzige vernünftige Weg. Und
zusammen mit mehreren Konsolen wird daraus Arbeitsteilung: Konsole 1 nimmt die
vordere Hälfte, Konsole 2 die hintere.
**Aufwand:** mittel · `world.h/.cpp`, `native.cpp`, Zeichnen in `main.cpp`

### M2 — Abbau, der nicht blockiert

`block.mine()` hält das Programm an, bis der Block heraus ist. Damit ist Abbauen
eine Wand: alles andere steht so lange still. Zwei neue Aufrufe machen daraus
einen Zustand:

```cpp
if (!block.mining()) block.start(info(block.ore())->care);
if (item.count(Stone) > 20) item.wash(Stone);   // laeuft NEBENHER
if (block.done()) item.sell();
```

**Geld:** Ein Programm, das den Abbau nicht abwartet, erledigt die Verarbeitung
geschenkt — in Zeit, die heute vollständig verloren ist.
**Sinn:** Das ist der Sprung vom Skript zur **Schleife mit Zustand**, und der ist
die eigentliche Lektion des Midgame. Er kostet nichts an Welt: der Block kann
das längst, nur die API wartet auf ihn.
**Aufwand:** klein–mittel · `native.cpp`, `world.cpp`

### M3 — Der Markt drückt zurück

Heute ist `market.price()` eine Schwingung, die man nur ablesen kann. Sobald
**der eigene Verkauf** den Preis bewegt, wird Verkaufen zum Problem:

```cpp
// naiv: 200 Stueck auf einmal, der Preis bricht ein
item.sell(Gold);

// besser: dosieren, bis es weh tut
while (market.price(Gold) > market.average(Gold) * 0.9f)
    item.sell(Gold, 5);
```

Jedes verkaufte Stück drückt den Preis dieser Sorte um einen kleinen Betrag, der
über die Runde wieder zurückläuft — zwei Zahlen in `data/skills.txt` neben
`market_swing`: `market_impact` und `market_recover`.

**Geld:** Wer streut — über die Zeit und über die Sorten — verdient bei gleicher
Ausbeute deutlich mehr. Das ist der erste Ertrag im Spiel, der *nur* aus einer
besseren Reihenfolge kommt.
**Sinn:** Es macht `market.average()` von einer Deko zu einer Messlatte, gibt
`shared` etwas zu merken und beantwortet endlich die Frage "warum sollte ich
jemals nicht sofort verkaufen".
**Aufwand:** klein · `world.cpp` (der Markt steht schon)

### M4 — Oxidation: die Tasche verdirbt

`oxidized` steht in `ore.h`, in `erze.json`, im Wiki und in `verarbeitung.json`
mit Faktor **0,6** — und **nichts im Spiel erzeugt diesen Zustand jemals.** Der
Zustand wartet auf seine Mechanik:

Rohes Erz oxidiert in der Tasche, je nach Sorte nach 30–120 Sekunden. `clean`
holt einen Teil zurück, kostet aber einen Ofenplatz.

```cpp
if (item.age(Copper) > 40) item.smelt(Copper);   // rette es, bevor es kippt
```

**Geld:** Wer seine Tasche im Blick hat, verliert nicht 40 % des Werts. Und
`item.age()` ist ein Grund, `count`/`purity` wirklich zu lesen statt nur `has`.
**Sinn:** Damit hat die Tasche zum ersten Mal eine **Uhr** — Aufheben wird eine
Wette und keine Selbstverständlichkeit. Es ist außerdem die billigste Art,
Verarbeiten von "irgendwann" auf "jetzt" zu stellen.
**Aufwand:** klein · `world.cpp`, ein Feld je Stapel

### M5 — Lagerplatz

Die Tasche ist unbegrenzt. Mit einer Obergrenze (`storage+` im Baum) wird
Sortieren Pflicht: was hebt man auf, was geht sofort raus, was lässt man liegen?

```cpp
if (item.full(0.9f)) item.sell(cheapest());   // Platz fuer das Gute
```

**Geld:** indirekt, aber hart — ein voller Sack heißt, dass der nächste Block ins
Leere fällt.
**Sinn:** Zusammen mit M3 und M4 entsteht die erste echte Wirtschaft: begrenzter
Platz, verderbliche Ware, ein Preis, der auf Mengen reagiert. Jede der drei
allein ist nett, zusammen sind sie ein Spiel.
**Aufwand:** klein · `world.cpp`

### M6 — Die Tasche als Liste

`container` ist kaufbar, aber nichts im Spiel gibt eine Liste zurück. Jede Frage
an die Tasche muss man **mit dem Namen des Erzes** stellen — und genau die Erze,
die der Generator erfindet, kennt man beim Schreiben nicht.

```cpp
for (Ore o : item.bag())                 // was liegt ueberhaupt drin?
    if (market.price(o) > market.average(o))
        item.sell(o);
```

**Geld:** Ein Programm, das nach `market.price` sortiert verkauft, holt bei jedem
Erz das Beste heraus — auch bei denen, die es beim Schreiben noch nicht gab.
**Sinn:** Das ist für die Tasche, was `info()` für die Behandlung war: der
Schritt von "Erz für Erz abschreiben" zu "einmal richtig schreiben". Und es ist
der Punkt, an dem `container` und `for` endlich zusammenkommen.
**Aufwand:** klein–mittel · `native.cpp` (eine Nachricht, die eine Liste
zurückgibt), `codecheck.cpp`

### M7 — Eine Warteschlange, die man selbst füllt

Mehrere Öfen gibt es, aber die Zuteilung ist "hau drauf und schau, ob es klappt".
Mit einer Warteschlange wird daraus Scheduling:

```cpp
job.queue(Copper, Smelt, 20);      // stellt sich an
job.queue(Gold,   Refine, 5);
job.priority(Gold, 2);             // Gold zuerst, wenn ein Ofen frei wird
if (job.waiting() > 10) job.clear(Copper);
```

**Geld:** Kein Ofen steht mehr leer, weil das Programm gerade woanders war — und
der Quest-Typ `furnaces_busy` wird planbar statt zufällig.
**Sinn:** Scheduling ist genau die Sorte Aufgabe, für die dieses Spiel gebaut
ist: kein neuer Befehl, sondern eine bessere Reihenfolge. Und es ist die
natürliche Fortsetzung von `furnace+`, statt einfach noch einen Ofen zu kaufen.
**Aufwand:** mittel · `craft.cpp`, `world.cpp`, `native.cpp`

### M8 — Werkzeug und Verschleiß

Spitzhacken mit Stufen und Haltbarkeit. `tool.wear()` liest den Zustand,
`tool.repair()` kostet Geld und Zeit, eine stumpfe Hacke kostet Reinheit.

```cpp
if (tool.wear() > 0.8f && !job.busy()) tool.repair();
```

**Geld:** Ein Geldabfluss, der nicht die Miete ist — und der erste Grund für
**Wartungslogik**: etwas tun, *bevor* es kaputt ist.
**Sinn:** Zusammen mit M9 wird aus der Anlage etwas, das man **pflegt**. Das ist
der Unterschied zwischen "läuft" und "lebt".
**Aufwand:** klein–mittel · `world.cpp`, `native.cpp`

### M9 — Strom und Kohle

Öfen brauchen Energie, ein Generator verbrennt Kohle. Kohle hat mit Wert 3 heute
keine Aufgabe und wird nach Runde 2 nie wieder angesehen.

```cpp
if (power.left() < 20.0f) power.feed(Coal, 10);
if (power.load() > 0.9f)  job.pause(Smelt);   // sonst faellt alles aus
```

**Geld:** Ein Gleichgewicht, das man ausrechnen muss: mehr Öfen brauchen mehr
Kohle, und Kohle abzubauen kostet Blockzeit. Die erste Aufgabe im Spiel mit einer
**optimalen Zahl** statt "mehr ist besser".
**Sinn:** Es gibt dem billigsten Erz eine zweite Karriere und macht `furnace+` zu
einer Investition statt zu einem Geschenk.
**Aufwand:** mittel · `world.cpp`, neue Datei `data/energie.json`

### M10 — Einen Block ablehnen

Nicht jeder Block ist es wert. `block.drop()` wirft ihn weg und startet das
Nachwachsen sofort — die Frage ist, ob sich das rechnet:

```cpp
const OreInfo* i = info(block.ore());
if (i && market.price(block.ore()) * i->value < 20) block.drop();
else                                                block.mine(i->care);
```

**Geld:** Bei langsamen, wertlosen Erzen spart das die halbe Abbauzeit. Der
Gewinn steckt vollständig in der Rechnung, die das Programm anstellt.
**Sinn:** Die kleinste denkbare Optimierungsaufgabe mit echter Rechnung —
Erwartungswert gegen Zeit — und sie braucht nichts als eine neue Nachricht.
Perfekter Einstieg in "das Programm entscheidet".
**Aufwand:** sehr klein · `world.cpp`, `native.cpp`

### M11 — Analyse in Stufen, und Gutachten verkaufen

`assay()` kostet 40 und liefert alles auf einmal. Zwei Stufen daraus machen:

| Aufruf | kostet | liefert |
|---|---|---|
| `assay(o, Quick)` | 10, 1 s | nur die Behandlung |
| `assay(o, Full)` | 40, 3 s | Wert, Seltenheit, Stufe, Legierbarkeit |
| `report(o)` | — | **verkauft** das Gutachten weiter, einmal je Erz |

**Geld:** Wer früh untersucht, verkauft die Erkenntnis — die erste Einnahme im
Spiel, die nicht aus Erz kommt. Den Quest-Typ `assayed` gibt es schon.
**Sinn:** Der Erzgenerator produziert endlos Neues; damit wird das
**Herausfinden selbst** ein Geschäftsmodell statt einer Gebühr.
**Aufwand:** klein · `world.cpp`, `data/skills.txt`

### M12 — Stammkunden und Ruf

Die Aufträge sind da, aber sie sind austauschbar: drei Karten, eine Runde, weg.
Ein **Ruf** je Kunde macht daraus eine Beziehung:

- Jeder Kunde hat einen Ruf-Wert 0–100. Erfüllte Aufträge heben ihn, verpatzte
  reißen ihn ein.
- Ab Ruf 50 gibt es **Daueraufträge**: jede Runde dieselbe Lieferung, keine
  Annahme nötig, planbares Grundeinkommen.
- Ab Ruf 80 kommt der Kunde mit Sonderwünschen, die richtig zahlen.

```cpp
if (customer(Guild).wants(Copper) > 0)
    item.deliver(Guild, Copper, 20);
```

**Geld:** Grundeinkommen dämpft die Ziel-Kurve genau dort, wo sie am steilsten
ist (Runde 8–12), ohne dass man am `wachstum` schrauben muss.
**Sinn:** Aufträge sind heute Einzelereignisse. Ruf macht daraus einen Verlauf —
man baut etwas auf, das über die Runde hinausreicht. Und die 80-%-Strafe wird
verständlicher, wenn sie einen *Namen* hat, den man enttäuscht.
**Aufwand:** mittel · `quest.cpp`, `data/quests.json`

### M13 — Termingeschäfte

Ein Vertrag über Ware, die man noch nicht hat:

> *"In 2 Runden: 100× Silber, geschmolzen, zu 1,3× dem heutigen Preis."*

Verkaufen, bevor abgebaut ist. Wer den Markt richtig einschätzt, verdient an der
Differenz; wer sich verrechnet, kauft am Ende teuer zu.

**Geld:** Der erste Ertrag, der aus einer **Vorhersage** kommt — und Vorhersagen
kann man programmieren, `market.average()` gibt es schon.
**Sinn:** Es verbindet Markt (M3), Aufträge (M12) und `shared` zu einer Sache und
gibt dem Spieler etwas, das über die Runde hinaus geplant wird. Runden haben
heute kein Gedächtnis.
**Aufwand:** mittel · `quest.cpp`, `world.cpp`

### M14 — `print()` füllt das Wiki

*(aus der alten Liste, immer noch offen)*

`print()` kostet eine Zeile und bringt nichts ein. Wenn die Ausgabe in ein
**Logbuch** läuft und das Logbuch das Wiki füllt, wird Messen zur Methode: Wer
`print(item.purity(o))` nach verschiedenen Behandlungen schreibt, findet die
richtige selbst heraus — ohne `assay()` zu bezahlen.

**Geld:** die billigere Alternative zu `assay`, bezahlt in Zeilen statt in Geld.
**Sinn:** Das Wiki behauptet ohnehin, eine Sammlung zu sein und kein
Nachschlagewerk (`oreFirst`/`oreSteps` in `world.h`). Das zieht es zu Ende.
**Aufwand:** mittel · `wiki.cpp`, `console.cpp`

---

# 2. Late Game

*Ab etwa Runde 15. Alles sitzt, das Programm läuft, und die Frage ist nur noch:
warum spiele ich weiter? Jede Idee hier muss etwas ändern, das nicht "mal zwei"
heißt.*

### L1 — Kerne: Nebenläufigkeit als Endspiel

*(aus der alten Liste — mit echten Prozessen ist jetzt der Boden dafür da)*

Man kauft **Kerne**, und die Zahl der Kerne begrenzt, wie viele Konsolen
wirklich gleichzeitig laufen. Darauf sitzen die Punkte, die ein C++-Spiel haben
*muss*:

| Punkt | wofür |
|---|---|
| `core+` | ein weiterer Kern |
| `atomic` | `shared` hochzählen, ohne Werte zu verlieren |
| `mutex` | einen Ofen für sich reservieren |
| `queue` | eine Auftragsliste zwischen den Konsolen |

Und der Fehler, den man dabei macht — zwei Konsolen greifen nach demselben Ofen —
ist ein **echter Wettlauf in einem echten Programm**. Kein anderes Spiel kann das
anbieten.

**Geld:** Ohne Absprache verlieren zwei Konsolen Arbeit aneinander; mit Absprache
multiplizieren sie sich.
**Aufwand:** groß · `native.cpp`, `codecheck.cpp`, `data/skills.txt`

### L2 — Ereignisse und Handler statt Warteschleifen

Bis hierher fragt das Programm ständig nach ("läuft der Ofen? ist der Block
da?"). Jede Frage kostet eine Zeile. Der späte Gegenentwurf:

```cpp
on(BlockAppeared, []{ block.mine(info(block.ore())->care); });
on(JobDone,       []{ item.sell(); });
on(PowerLow,      []{ power.feed(Coal, 5); });

int main() { idle(); }      // das war das ganze Programm
```

Ein Handler kostet beim Anmelden eine Zeile und läuft danach **umsonst**, wenn
das Ereignis eintritt.

**Geld:** Das ganze Zeilenbudget, das heute in Warteschleifen verbrennt, wird
frei. Für ein spätes Programm ist das der größte einzelne Sprung.
**Sinn:** Es ist genau der Schritt, den echte Programme auch machen — von Polling
zu Callbacks —, und er sitzt an der richtigen Stelle im Lernbogen: erst muss man
gemerkt haben, dass Nachfragen teuer ist. Nebenbei bekommen Lambdas endlich einen
Grund zu existieren, statt nur das Variablenlimit auszutricksen.
**Aufwand:** mittel–groß · `native.cpp`, `instrument.cpp`

### L3 — Firmware: Maschinen, die man einmal programmiert

Die Endstufe von "Zeilen sind die Währung": Man kauft Maschinen, die **eigenen
Code** ausführen — geschrieben in einer eigenen Konsole, einmal übersetzt, und
danach laufen sie ohne das Zeilenbudget des Spielers.

```cpp
// Konsole "Sortierer" - wird zur Firmware, kein main()
firmware void sort(Ore o) {
    if (market.price(o) > market.average(o)) item.sell(o);
    else                                     item.wash(o);
}
```

Der Preis: eine Firmware hat ein **hartes Zeilenlimit** (z. B. 12), sie darf
nicht in `shared` schreiben, und sie wird nur zwischen den Runden neu geladen.
Was drinsteht, muss also *gut* sein und nicht lang.

**Geld:** Ein Sortierer, der immer läuft, verdient in der Zeit, in der der
Spieler etwas anderes tut.
**Sinn:** Das ist die einzige Antwort auf den Deckel "eine Zeile kostet Zeit", die
nicht bloß die Zeile billiger macht: **Code, der einmal richtig geschrieben
wurde, kostet nichts mehr.** Und ein hartes Limit von 12 Zeilen ist die beste
Übung im ganzen Spiel.
**Aufwand:** groß · `native.cpp`, `console.cpp`, `codecheck.cpp`

### L4 — Der Compiler als Ausbaustufe

`inline` und `optimize` gibt es. Was fehlt, ist die Spitze:

| Punkt | Wirkung |
|---|---|
| `cache` | zweiter Aufruf mit denselben Argumenten kostet nichts |
| `pgo` | Zeilen in der **heißesten** Schleife kosten die Hälfte |
| `fast { … }` | ein markierter Block läuft in **einem** Tick |

```cpp
fast {                                  // eine Zeile, egal was drinsteht
    for (int i = 0; i < 100; i++) job.queue(Copper, Smelt);
}
```

`fast` braucht eine Bremse, sonst ist es das Ende des Spiels — z. B. einmal je
Runde, oder eine Abklingzeit proportional zur Länge des Blocks.

**Sinn:** Hier wird wirklich mit `cl.exe`/`g++` übersetzt. Optimierungsstufen als
Kaufware sind die naheliegendste Idee, die dieses Projekt haben kann, und `pgo`
belohnt genau das, was das Spiel lehrt: wissen, wo die Zeit hingeht.
**Aufwand:** mittel · `instrument.cpp`, `native.cpp`

### L5 — Die Fabrik: das Programm wird Steuerung

Maschinen auf einem Raster, verbunden durch Bänder. Das Programm bedient nicht
mehr jeden Block einzeln, sondern **leitet**: was geht wohin, welche Maschine
bekommt welchen Auftrag, wann wird umgestellt.

```cpp
belt(Mine, Washer).route(Any);
belt(Washer, Furnace).route([](Ore o){ return info(o)->value > 50; });
```

**Geld:** Der Durchsatz hängt an der Anlage statt am Zeilenbudget — der letzte
Deckel aus Abschnitt 0 fällt.
**Sinn:** "Die Anlage umbauen, weil sich der Markt gedreht hat" ist der Grund,
warum Fabrikspiele spät noch tragen. Und mit M3 gibt es diesen Markt.
**Aufwand:** groß · neu

### L6 — Schichten mit eigenen Regeln

Nicht mehr Erze, sondern **andere Regeln**. Jede Schicht ändert die Schleife,
nicht die Zahlen:

| Schicht | was neu ist | was das Programm können muss |
|---|---|---|
| 2 — Hitze | Maschinen überhitzen | Pausen einplanen, Temperatur lesen |
| 3 — Wasser | die Ader läuft voll | Pumpen takten, sonst Totalausfall |
| 4 — Gas | Warnung, dann 20 s Zeit | alles anhalten und rausgehen — sauber |
| 5 — Druck | Blöcke zerfallen beim Abbau | Reihenfolge nach Haltbarkeit |

**Sinn:** Der Erzgenerator macht Breite. Was fehlt, ist **Tiefe**: ein Grund, das
Programm noch einmal ganz anders zu schreiben. Schicht 4 zwingt zum ersten Mal
zum sauberen Aufräumen — und dafür gibt es in einem Programmierspiel keinen
besseren Anlass.
**Aufwand:** mittel je Schicht · `world.cpp`, `data/`

### L7 — Das Legierungslabor: eigene Rezepte und Patente

Heute stehen alle Rezepte in `legierungen.json`. Spät sollte man **selbst welche
finden**: zwei Erze in den Ofen, schauen, was herauskommt.

- Ein Versuch kostet Material und einen Ofenplatz und schlägt meistens fehl.
- Was klappt, landet im Wiki, bekommt einen Namen — und ein **Patent**.
- Auf jedes Patent zahlt der Markt eine kleine Gebühr, jede Runde, dauerhaft.

```cpp
if (!known(Gold, Titan)) experiment(Gold, Titan);
```

**Geld:** Passives Einkommen, das man sich erarbeitet hat — der erste Ertrag, der
auch dann läuft, wenn man gerade nichts tut.
**Sinn:** `legierbar_mit` steht beim Ergebnis schon in den Daten und wird nicht
benutzt; der Erzgenerator kann längst Neues erfinden. Beides zusammen ergibt ein
Spätspiel, das der Spieler *selbst füllt* statt es abzuarbeiten.
**Aufwand:** mittel · `alloy.cpp`, `wiki.cpp`

### L8 — Konkurrenz

Ein zweiter Betrieb arbeitet an derselben Ader und verkauft auf demselben Markt.
Er ist keine KI-Show, sondern drei Zahlen: Abbaugeschwindigkeit, Lieblingssorte,
Reaktionszeit.

- Er nimmt Blöcke weg, die man liegen lässt (M1 wird dadurch scharf).
- Er drückt die Preise der Sorte, die er gerade verkauft (M3).
- Er bietet auf dieselben Aufträge — wer schneller zusagt, bekommt sie.

**Sinn:** Es gibt dem Spiel spät einen **Gegner** statt nur einer Miete. Und alles,
was er tut, ist über die API sichtbar — also programmierbar beantwortbar
(`rival.mining()`, `rival.selling()`).
**Aufwand:** mittel · `world.cpp`

### L9 — Zertifizierung: der Code selbst wird geprüft

Der Quest-Typ `frugal` misst schon ausgeführte Zeilen. Der nächste Schritt sind
Aufträge, die an den **Quelltext** Bedingungen stellen — `codecheck.cpp` sieht
das bereits alles:

> *"Zertifiziert: höchstens 40 Zeilen, kein `goto`, mindestens zwei eigene
> Funktionen, jede unter 10 Zeilen. Zahlt das Vierfache."*

**Geld:** hoch, weil es keinen Umweg gibt: man muss den Code wirklich aufräumen.
**Sinn:** Das ist der einzige Auftrag, den nur *dieses* Spiel stellen kann. Und er
belohnt zum ersten Mal **Struktur** statt Durchsatz — bis hierher war
Copy-Paste ökonomisch fast immer richtig.
**Aufwand:** klein–mittel · `codecheck.cpp`, `data/quests.json`

### L10 — Technische Schulden: Übersetzen kostet Rundenzeit

Das Spiel übersetzt wirklich — und ein langes Programm braucht dafür spürbar
länger. Heute ist das nur eine Wartezeit; als **Rundenzeit** wird es eine
Mechanik:

> Beim Rundenstart gehen die Sekunden der Übersetzung von den 300 ab.

**Geld:** Ein 800-Zeilen-Programm kostet vielleicht 6 Sekunden Runde — 2 % vom
Ertrag, jede Runde.
**Sinn:** Es gibt dem Aufräumen einen Preis in der Währung des Spiels, ohne
irgendetwas zu erfinden: die Kosten sind schon da, sie werden nur noch nicht
berechnet. Zusammen mit L9 entsteht der einzige Druck im Spiel, der Richtung
**weniger Code** zeigt.
**Aufwand:** sehr klein · `round.cpp`, `native.cpp`

### L11 — Der Autopilot

*(aus der alten Liste)*

Eine abgeleitete Klasse, die weiterläuft, während man im Skilltree ist, im Wiki
liest oder in der Vorbereitung steht — gedrosselt auf etwa 20 %.

```cpp
struct MyRoutine : Routine {
    void step() override {
        block.mine(info(block.ore())->care);
        if (item.has(Any, 20)) item.sell();
    }
};
int main() { automation.install(new MyRoutine()); }
```

**Geld:** Einnahmen in Zeit, die heute tot ist (`welt_pausiert_in_vorbereitung`).
**Sinn:** `classes` kauft man dann, um eine Maschine zu bauen, die sich selbst
besitzt — nicht, um das Variablenlimit auszutricksen.
**Aufwand:** mittel · `native.cpp`, `main.cpp`

### L12 — Prestige: eine Schicht tiefer

Geld und Skilltree gehen auf Anfang, ein dauerhafter Multiplikator bleibt —
**und der Code bleibt.** Das ist die Pointe, die nur dieses Spiel haben kann:

> Der Spielstand ist nicht dein Geld. Der Spielstand ist dein Programm.

Beim zweiten Durchgang steht man mit einer fertigen Bibliothek da und schafft in
zehn Minuten, wofür man vorher zehn Runden gebraucht hat. Man sieht dem eigenen
Fortschritt zu, nicht dem einer Zahl.

Dazu gehört eine **Programm-Bibliothek**: benannte, gespeicherte Konsolen, die
über Läufe hinweg existieren (siehe W4).
**Aufwand:** mittel · `save.cpp`, `skilltree.cpp`

### L13 — Das Artefakt: ein Ziel jenseits der Miete

Eine Maschine, die man über viele Runden zusammenbaut. Jedes Teil verlangt eine
bestimmte Pipeline:

| Teil | verlangt |
|---|---|
| Rahmen | 200× gehärtete Bronze über 80 % |
| Linse | 50× geätzter Diamant über 95 % |
| Kern | 10× verschmolzenes Elektrum, in **einer** Runde hergestellt |

**Sinn:** Das Rundenziel ist etwas, das man **abwehrt**. Ein Artefakt ist etwas,
auf das man **zugeht** — und es zwingt genau die tiefen Zustände (`etched`,
`fused`) in den Vordergrund, die es in den Daten schon gibt und die sonst kaum
jemand anfasst.
**Aufwand:** mittel · neue Datei `data/artefakt.json`, `round.cpp`

### L14 — Speicher und Bandbreite als zweite Währung

Spät sind Zeilen billig. Dann wird die andere Ressource knapp: **`shared` ist eine
Rundreise zum Spiel.** Jeder Zugriff kostet Zeit, und mit vielen Konsolen addiert
sich das.

Daraus eine sichtbare Größe machen (`shared.load()`), dazu Punkte, die sie
senken: lokaler Zwischenspeicher, Stapelzugriffe (`shared.batch()`), eigene Kanäle
zwischen zwei Konsolen statt einer gemeinsamen Tafel.

**Sinn:** Die ehrlichste Fortsetzung von L1 — wer nebenläufig arbeitet, stößt in
Wirklichkeit auf Kommunikationskosten und nicht auf Rechenzeit.
**Aufwand:** mittel · `native.cpp`

### L15 — Geisterläufe

Beim Rundenstart legt sich die Geldkurve des **eigenen besten Laufs** als blasse
Linie über die Anzeige. Kein Multiplayer, keine Server: eine Zahlenreihe im
Spielstand.

**Sinn:** Spät fehlt ein Gegner (L8 ist einer, das hier ist der billigere). Und
weil das Spiel wirklich Code ausführt, sieht man dabei zu, wie das eigene
Programm sein Vorgängerprogramm überholt — das ist die Belohnung, die genau zu
diesem Spiel gehört.
**Aufwand:** klein · `round.cpp`, `save.cpp`

---

# 3. Reine Datenarbeit

*Das Billigste im Dokument: kein C++, nur `data/`. Wenn wenig Zeit ist, fängt man
hier an.*

| | Idee | Datei |
|---|---|---|
| **D1** | **Legierungen aus Legierungen.** `legierbar_mit` steht beim Ergebnis schon in den Daten und wird nicht genutzt. Zwei Legierungen zu einer dritten macht aus einer Liste einen Baum. | `legierungen.json` |
| **D2** | **Rezepte mit drei oder vier Zutaten** aus verschiedenen Zweigen des Verarbeitungsnetzes — das erzwingt eine echte Pipeline statt zweimal `smelt`. | `legierungen.json` |
| **D3** | **Zustände hinter `fused`.** `zustandswert` ist einfach eine Liste; ein Zustand mit Faktor 20 und 30 s Dauer ist ein Endspiel-Ziel für den Preis von drei Zeilen JSON. | `verarbeitung.json` |
| **D4** | **Erzfamilien im Generator.** Verwandte Erze teilen die Behandlung. Dann lohnt es sich, von einem `assay()` auf die Verwandten zu schließen — und ein Programm kann *raten*. | `erzgenerator.json` |
| **D5** | **Fehlende Quest-Arten:** eine ganze Pipeline in einer Runde (`raw → fused`), nur über dem Durchschnittspreis verkaufen, ein selbst entdecktes Rezept liefern, eine Runde ohne einen einzigen Fehlgriff am Ofen, unter N Zeilen **und** über X Geld. | `quests.json` |
| **D6** | **Strategieseiten im Wiki.** Es erklärt jeden Befehl, aber keine Entscheidung. Eine Seite "wann verkaufen", eine "wann verarbeiten statt abbauen" — genau die Fragen, die das Midgame stellt. | `wiki.json` |

---

# 4. Werkzeug für den Spieler

*Kein Inhalt, aber sie entscheiden, ob der Inhalt oben überhaupt spielbar ist.*

### W1 — Profiler

Zwei Zahlen in der Rundenleiste: **Geld pro ausgeführter Zeile** und **Zeilen pro
Runde**. Dazu, teuer im Baum, eine Heatmap im Editor: welche Zeile hat wie viel
Zeit gefressen.

Ohne das optimiert man im Dunkeln. Mit ihm hat man zum ersten Mal eine Zahl, die
man schlagen will — und `pgo` aus L4 wird verständlich.
**Aufwand:** klein · `console.cpp`, `round.cpp`

### W2 — Der Probelauf

Eine Runde ohne Geld und ohne Ziel, dafür in dreifachem Tempo. Zum Ausprobieren
eines neuen Programms, bevor man eine echte Runde damit verliert.

Ab Runde 10 ist ein Fehler teuer genug, dass man sich nicht mehr traut, etwas
umzubauen — und ein Spiel, in dem man sich nicht mehr traut, den Code anzufassen,
hat sein Thema verloren.
**Aufwand:** klein · `round.cpp`

### W3 — Haltepunkte

`F9` hält schon alles an. Was fehlt: Klick auf eine Zeile setzt einen Haltepunkt,
ein Fenster zeigt `shared` und die Tasche, ein Knopf geht eine Zeile weiter.

Das Spiel markiert die laufende Zeile bereits (`instrument.cpp`) — der Debugger
ist zur Hälfte gebaut, ohne dass es jemand so nennt.
**Aufwand:** mittel · `instrument.cpp`, `console.cpp`

### W4 — Programm-Bibliothek

Benannte Programme, die **neben** dem Spielstand liegen und über Läufe hinweg
existieren; beim Anlegen einer Konsole wählbar. Voraussetzung für L12, aber auch
ohne Prestige sofort nützlich.
**Aufwand:** klein · `save.cpp`, `console.cpp`

---

# 5. Was zuerst?

Sortiert nach Wirkung geteilt durch Aufwand.

| # | Idee | Aufwand | Wirkung | Dateien |
|---|---|---|---|---|
| 1 | **M10** Block ablehnen | sehr klein | mittel — die erste echte Rechnung | `world.cpp`, `native.cpp` |
| 2 | **M3** Markt drückt zurück | klein | **hoch** — Verkaufen wird eine Entscheidung | `world.cpp` |
| 3 | **M4** Oxidation | klein | hoch — die Tasche bekommt eine Uhr | `world.cpp` |
| 4 | **D1–D3** Legierungstiefe | sehr klein | mittel — reines JSON | `data/` |
| 5 | **W1** Profiler | klein | hoch — macht das Spiel überhaupt messbar | `console.cpp` |
| 6 | **M2** Abbau ohne Blockade | klein–mittel | **hoch** — Schleife mit Zustand | `native.cpp` |
| 7 | **M6** Tasche als Liste | klein–mittel | hoch — `container` bekommt Sinn | `native.cpp` |
| 8 | **L10** Kompilierzeit zählt | sehr klein | mittel — Druck Richtung weniger Code | `round.cpp` |
| 9 | **M1** Die Ader | mittel | **sehr hoch** — hebt den Abbau-Deckel | `world.h/.cpp` |
| 10 | **W2** Probelauf | klein | mittel — man traut sich wieder | `round.cpp` |
| 11 | **M5** Lagerplatz | klein | mittel — mit M3/M4 zusammen eine Wirtschaft | `world.cpp` |
| 12 | **L9** Zertifizierung | klein–mittel | hoch — belohnt Struktur | `codecheck.cpp`, `quests.json` |
| 13 | **M7** Ofen-Warteschlange | mittel | hoch — Scheduling | `craft.cpp` |
| 14 | **M12** Ruf und Stammkunden | mittel | hoch — Runden bekommen Gedächtnis | `quest.cpp` |
| 15 | **L2** Ereignisse und Handler | mittel–groß | **sehr hoch** — spätes Spiel, neues Programm | `native.cpp` |
| 16 | **M8/M9** Werkzeug, Strom | mittel | mittel — eine Anlage, die man pflegt | `world.cpp` |
| 17 | **L1** Kerne, `mutex`, `atomic` | groß | Endgame | `native.cpp` |
| 18 | **L7** Legierungslabor, Patente | mittel | Endgame | `alloy.cpp` |
| 19 | **L3** Firmware | groß | Endgame — die Antwort auf den Zeilendeckel | `native.cpp` |
| 20 | **L12/L13** Prestige, Artefakt | mittel | Endgame — ein Ziel jenseits der Miete | `save.cpp` |
| 21 | **L5** Fabrik-Raster | groß | Endgame | neu |

**Der kleinste Anfang mit der größten Wirkung** sind die Zeilen 1–4: zusammen
vielleicht ein Nachmittag, betroffen sind nur `world.cpp` und `data/` — und
danach hat das Spiel zum ersten Mal drei Entscheidungen, die vorher keine waren:
was baue ich ab, wann verkaufe ich, was hebe ich auf.

**Der größte Sprung fürs Späte** ist **L2** (Ereignisse). Es macht das gesamte
Zeilenbudget frei, das heute in Warteschleifen verbrennt, und zwingt den Spieler
ein letztes Mal, sein Programm von Grund auf anders zu schreiben. Genau das
fehlt spät.
