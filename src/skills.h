#pragma once

struct World;
struct SkillTree;
struct Limits;

// Kleine Anzeige auf der Welt-Seite: welche Runde laeuft, und was du kannst.
//
// Bewusst nur das, was du HAST - was noch fehlt, steht nirgends. Das soll die
// Ueberraschung im Baum bleiben.
void DrawStatus(const Limits& limits, const World& world);

// Die Skilltree-Seite. Wird STATT der Welt gezeigt, nicht daneben.
//
// Nur der Baum, sonst nichts: was ein Punkt macht und kostet, steht im
// Tooltip. Gekauft wird per Doppelklick.
//
// Das Zuruecksetzen sitzt nicht mehr hier, sondern oben in der Leiste - es
// gehoert zum Spiel und nicht zu einer einzelnen Seite.
void DrawSkillPage(World& world, SkillTree& tree);
