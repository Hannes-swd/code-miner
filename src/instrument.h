#pragma once

#include <string>

// Baut vor jede Anweisung ein  ck::line(Konsole, Zeile);  ein, damit das
// laufende Programm meldet, wo es gerade ist.
//
//   int main() {                 int main() {
//       block.mine();       ->       ck::line(1,2); block.mine();
//       return 0;                    ck::line(1,3); return 0;
//   }                            }
//
// Der Einbau passiert IN DERSELBEN ZEILE. Dadurch verschieben sich keine
// Zeilennummern und Compiler-Fehler zeigen weiterhin auf die Zeile, die im
// Editor steht.
std::string Instrument(const std::string& code, int consoleId);

// Steht in diesem Quelltext ein  main()  ?  Kommentare und Zeichenketten
// zaehlen nicht mit.
bool ContainsMainFunction(const std::string& code);
