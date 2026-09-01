#include "instrument.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace
{

bool IsIdentChar(char c)
{
    return std::isalnum((unsigned char)c) != 0 || c == '_';
}

// Steht an Position p das Wort w (und nicht nur dessen Anfang)?
bool WordAt(const std::string& s, std::size_t p, const char* w)
{
    const std::size_t n = std::strlen(w);
    if (s.compare(p, n, w) != 0)
        return false;
    const char after = (p + n < s.size()) ? s[p + n] : '\0';
    return !IsIdentChar(after);
}

// Endet an Position p (einschliesslich) das Wort w?
bool WordEndsAt(const std::string& s, std::size_t p, const char* w)
{
    const std::size_t n = std::strlen(w);
    if (p + 1 < n)
        return false;
    const std::size_t start = p + 1 - n;
    if (s.compare(start, n, w) != 0)
        return false;
    return start == 0 || !IsIdentChar(s[start - 1]);
}

// Oeffnet die geschweifte Klammer an lastSig einen Anweisungsblock (Funktion,
// if, while, for, ...) oder etwas anderes (Klasse, struct, enum, namespace,
// Initialisierungsliste)? Entscheidend ist das Zeichen davor.
bool OpensCodeBlock(const std::string& s, std::size_t lastSig)
{
    if (lastSig == std::string::npos)
        return false;

    const char c = s[lastSig];

    // "void f() {", "if (x) {", "while (x) {"  ->  Anweisungsblock
    if (c == ')')
        return true;

    // "else {", "do {", "try {"  ->  ebenfalls
    return WordEndsAt(s, lastSig, "else") || WordEndsAt(s, lastSig, "do") ||
           WordEndsAt(s, lastSig, "try");
}

// Darf vor diesem Zeichen ein ck::line() stehen?
bool MayInsertBefore(const std::string& s, std::size_t p)
{
    const char c = s[p];

    // } beendet nur einen Block, ; allein ist eine leere Anweisung.
    if (c == '}' || c == '#' || c == ';')
        return false;

    // Diese leiten keine eigenstaendige Anweisung ein.
    return !WordAt(s, p, "else") && !WordAt(s, p, "case") && !WordAt(s, p, "default") &&
           !WordAt(s, p, "public") && !WordAt(s, p, "private") && !WordAt(s, p, "protected");
}

}  // namespace

std::string Instrument(const std::string& src, int consoleId)
{
    std::vector<std::size_t> positions;
    std::vector<int>         lines;

    // Steht diese Anweisung im Rumpf einer EIGENEN Funktion? Dann bekommt sie
    // ein ck::linef() statt eines ck::line(). Der Unterschied ist nur der
    // Buchstabe in der Meldung - das Spiel rechnet sie mit dem Punkt "inline"
    // dann nur halb an. Ohne diese Unterscheidung waere eine Funktion
    // wirtschaftlich immer schlechter als derselbe Code zweimal hingeschrieben,
    // und der Punkt "functions" ein Verlustgeschaeft.
    std::vector<bool> inFunction;

    std::vector<bool> blockIsCode;  // Stapel: ist der Block ein Anweisungsblock?
    int               line    = 1;
    int               paren   = 0;
    int               bracket = 0;
    bool              pending = false;  // stehen wir direkt hinter ; { } ?
    std::size_t       lastSig = std::string::npos;

    // Der letzte Bezeichner, an dem wir vorbeigekommen sind, und der, der
    // direkt vor der letzten oeffnenden Klammer ganz aussen stand. Aus dem
    // zweiten wird der Name der Funktion, deren Rumpf gleich anfaengt.
    std::string lastIdent;
    std::string headName;

    // Bei welcher Stapeltiefe der Rumpf einer eigenen Funktion angefangen hat.
    // -1 = wir stehen gerade in keiner.
    int funcDepth = -1;

    for (std::size_t i = 0; i < src.size(); ++i)
    {
        const char c = src[i];

        if (c == '\n')
        {
            ++line;
            continue;
        }
        if (std::isspace((unsigned char)c))
            continue;

        // Zeilenkommentar
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/')
        {
            while (i < src.size() && src[i] != '\n')
                ++i;
            --i;
            continue;
        }

        // Blockkommentar
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/'))
            {
                if (src[i] == '\n')
                    ++line;
                ++i;
            }
            ++i;
            continue;
        }

        // Praeprozessor-Zeile ueberspringen (auch mit Fortsetzungszeilen)
        if (c == '#')
        {
            while (i < src.size() && src[i] != '\n')
            {
                if (src[i] == '\\' && i + 1 < src.size() && src[i + 1] == '\n')
                {
                    ++line;
                    ++i;
                }
                ++i;
            }
            --i;
            pending = false;
            continue;
        }

        // Wir stehen hinter ; { } - hier koennte eine Anweisung beginnen.
        if (pending)
        {
            pending = false;
            const bool inCodeBlock = !blockIsCode.empty() && blockIsCode.back();
            if (inCodeBlock && paren == 0 && bracket == 0 && MayInsertBefore(src, i))
            {
                positions.push_back(i);
                lines.push_back(line);
                inFunction.push_back(funcDepth >= 0);
            }
        }

        // Einen Bezeichner am Stueck lesen. Frueher lief die Schleife auch hier
        // Zeichen fuer Zeichen - jetzt brauchen wir den ganzen Namen, um
        // "void schritt(" von "int main(" zu unterscheiden.
        if (std::isalpha((unsigned char)c) != 0 || c == '_')
        {
            const std::size_t start = i;
            while (i + 1 < src.size() && IsIdentChar(src[i + 1]))
                ++i;
            lastIdent = src.substr(start, i + 1 - start);
            lastSig   = i;
            continue;
        }

        // Zeichenkette oder Zeichen ueberspringen
        if (c == '"' || c == '\'')
        {
            const char quote = c;
            for (++i; i < src.size(); ++i)
            {
                if (src[i] == '\\')
                {
                    ++i;
                    continue;
                }
                if (src[i] == '\n')
                    ++line;
                if (src[i] == quote)
                    break;
            }
            lastSig = i;
            continue;
        }

        switch (c)
        {
        case '{':
        {
            const bool parentIsCode = !blockIsCode.empty() && blockIsCode.back();

            // Nach ) else do try  ->  Funktionsrumpf oder if/while/for-Rumpf.
            bool isCode = OpensCodeBlock(src, lastSig);

            // Ein nackter Block mitten im Code:  { int x; ... }
            // Erkennbar daran, dass davor ; { oder } stand.
            if (!isCode && parentIsCode && lastSig != std::string::npos)
            {
                const char prev = src[lastSig];
                isCode          = (prev == ';' || prev == '{' || prev == '}');
            }

            // Alles andere ist KEIN Anweisungsblock: struct/class/enum/namespace
            // und vor allem Initialisierungslisten wie  = {1, 2, 3}.
            blockIsCode.push_back(isCode);

            // Faengt hier der Rumpf einer eigenen Funktion an? Das ist der
            // aeusserste Anweisungsblock, und davor stand ein Name mit
            // Klammern. main() zaehlt nicht dazu: es ist das Grundgeruest und
            // kein selbstgeschriebenes Unterprogramm.
            if (isCode && funcDepth < 0 && blockIsCode.size() == 1 && !headName.empty() &&
                headName != "main")
                funcDepth = 1;

            if (paren == 0 && bracket == 0)
                pending = true;
            break;
        }

        case '}':
            if (!blockIsCode.empty())
                blockIsCode.pop_back();

            // Der Rumpf ist zu - ab hier zaehlen die Zeilen wieder voll.
            if (funcDepth >= 0 && (int)blockIsCode.size() < funcDepth)
                funcDepth = -1;

            if (paren == 0 && bracket == 0)
                pending = true;
            break;

        case ';':
            if (paren == 0 && bracket == 0)
                pending = true;
            break;

        case '(':
            // Der Name direkt davor ist der Kopf einer Funktion - aber nur
            // ganz aussen. Innerhalb eines Rumpfes ist jede Klammer ein
            // Aufruf und kein neuer Kopf.
            if (paren == 0 && blockIsCode.empty())
                headName = lastIdent;
            ++paren;
            break;
        case ')': if (paren > 0) --paren; break;
        case '[': ++bracket; break;
        case ']': if (bracket > 0) --bracket; break;
        default: break;
        }

        lastSig = i;
    }

    // Von hinten nach vorne einfuegen, damit die Positionen gueltig bleiben.
    const std::string aussen = "ck::line(" + std::to_string(consoleId) + ",";
    const std::string innen  = "ck::linef(" + std::to_string(consoleId) + ",";

    std::string out = src;
    for (std::size_t k = positions.size(); k-- > 0;)
    {
        const std::string& prefix = inFunction[k] ? innen : aussen;
        out.insert(positions[k], prefix + std::to_string(lines[k]) + "); ");
    }

    return out;
}

bool ContainsMainFunction(const std::string& src)
{
    for (std::size_t i = 0; i < src.size(); ++i)
    {
        const char c = src[i];

        // Kommentare und Zeichenketten ueberspringen - dort steht kein Code.
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/')
        {
            while (i < src.size() && src[i] != '\n')
                ++i;
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/'))
                ++i;
            ++i;
            continue;
        }
        if (c == '"' || c == '\'')
        {
            const char quote = c;
            for (++i; i < src.size(); ++i)
            {
                if (src[i] == '\\')
                {
                    ++i;
                    continue;
                }
                if (src[i] == quote)
                    break;
            }
            continue;
        }

        if (c != 'm' || !WordAt(src, i, "main"))
            continue;
        if (i > 0 && IsIdentChar(src[i - 1]))
            continue;

        // Nach "main" darf nur Leerraum und dann eine ( kommen.
        std::size_t j = i + 4;
        while (j < src.size() && std::isspace((unsigned char)src[j]))
            ++j;
        if (j < src.size() && src[j] == '(')
            return true;
    }
    return false;
}
