#include "codecheck.h"

#include <cctype>

namespace
{

bool IsIdentChar(char c)
{
    return std::isalnum((unsigned char)c) != 0 || c == '_';
}

struct Token
{
    std::string word;
    int         line;
};

// Zerlegt Quelltext in Bezeichner - Kommentare und Zeichenketten fallen weg.
// Dasselbe Verfahren wie bei der Instrumentierung, damit ein "while" in einem
// Kommentar nicht mitzaehlt.
//
// Die offene Klammer kommt als eigenes Zeichen mit durch: nur an ihr sieht man
// den Unterschied zwischen "int zahl" (Variable) und "int zaehle(" (Funktion).
std::vector<Token> Tokenize(const std::string& src)
{
    std::vector<Token> out;
    int                line = 1;

    for (std::size_t i = 0; i < src.size(); ++i)
    {
        const char c = src[i];

        if (c == '\n')
        {
            ++line;
            continue;
        }

        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/')
        {
            while (i < src.size() && src[i] != '\n')
                ++i;
            --i;
            continue;
        }

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
            continue;
        }

        if (c == '(')
        {
            out.push_back({"(", line});
            continue;
        }

        if (std::isalpha((unsigned char)c) != 0 || c == '_')
        {
            const std::size_t start = i;
            while (i < src.size() && IsIdentChar(src[i]))
                ++i;
            out.push_back({src.substr(start, i - start), line});
            --i;
        }
    }

    return out;
}

// Woran man einen Typ erkennt. Nicht perfekt, aber ehrlich: wer einen Typ
// hinschreibt, legt etwas an.
bool IsTypeWord(const std::string& w)
{
    return w == "int" || w == "float" || w == "double" || w == "bool" || w == "char" ||
           w == "auto" || w == "long" || w == "short" || w == "unsigned" || w == "string" ||
           w == "size_t" || w == "void";
}

bool IsName(const std::string& w)
{
    return !w.empty() && w != "(" && !IsTypeWord(w);
}

// Sammelt die Namen hinter "class" und "struct". Ohne das waere "Block b;"
// eine Gratis-Variable: der Typ steht ja in keiner Liste.
std::vector<std::string> ClassNames(const std::vector<Token>& tokens)
{
    std::vector<std::string> out;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
        if (tokens[i].word == "class" || tokens[i].word == "struct")
            if (IsName(tokens[i + 1].word))
                out.push_back(tokens[i + 1].word);
    return out;
}

bool IsIn(const std::vector<std::string>& list, const std::string& word)
{
    for (const std::string& w : list)
        if (w == word)
            return true;
    return false;
}

std::string TooMany(const char* was, int erlaubt, int jetzt)
{
    return std::string("You may only use ") + std::to_string(erlaubt) + " " + was +
           ", this is number " + std::to_string(jetzt) + ".";
}

}  // namespace

std::string CheckLimits(const std::vector<SourceFile>& files, const Limits& limits,
                        int& errorConsole, int& errorLine)
{
    errorConsole = 0;
    errorLine    = 0;

    if ((int)files.size() > limits.maxConsoles)
    {
        errorConsole = files[(std::size_t)limits.maxConsoles].id;
        return "You may only use " + std::to_string(limits.maxConsoles) +
               " console(s). More are available in the skill tree.";
    }

    // Ein Programm - also wird ueber alle Konsolen zusammen gezaehlt.
    int loops     = 0;
    int ifs       = 0;
    int variables = 0;
    int classes   = 0;
    int functions = 0;

    // Eigene Klassen sind auch Typen - erst alle einsammeln, dann zaehlen.
    std::vector<std::string> classNames;
    for (const SourceFile& file : files)
        for (const std::string& name : ClassNames(Tokenize(file.code)))
            classNames.push_back(name);

    for (const SourceFile& file : files)
    {
        const std::vector<Token> tokens = Tokenize(file.code);

        for (std::size_t ti = 0; ti < tokens.size(); ++ti)
        {
            const Token& t = tokens[ti];

            errorConsole = file.id;
            errorLine    = t.line;

            const auto locked = [&](const char* what) -> std::string
            { return std::string("\"") + what + "\" is not unlocked yet - buy it in the skill tree."; };

            if (t.word == "while" || t.word == "do")
            {
                if (!limits.allowWhile)
                    return locked(t.word.c_str());
                if (++loops > limits.maxLoops)
                    return TooMany("loop(s)", limits.maxLoops, loops);
            }
            else if (t.word == "for")
            {
                if (!limits.allowFor)
                    return locked("for");
                if (++loops > limits.maxLoops)
                    return TooMany("loop(s)", limits.maxLoops, loops);
            }
            else if (t.word == "if")
            {
                if (!limits.allowIf)
                    return locked("if");
                if (++ifs > limits.maxIfs)
                    return TooMany("condition(s)", limits.maxIfs, ifs);
            }
            else if (t.word == "else")
            {
                if (!limits.allowElse)
                    return locked("else");
            }
            else if (t.word == "print")
            {
                if (!limits.allowPrint)
                    return locked("print()");
            }
            else if (t.word == "isThere" || t.word == "isLoading" || t.word == "exists" ||
                     t.word == "isGone")
            {
                if (!limits.allowCheck)
                    return locked("block.isThere()");
            }
            else if (t.word == "mine")
            {
                if (!limits.allowMine)
                    return locked("block.mine()");
            }
            // Behandeln. "Cool" und "Heat" haengen mit dran: ohne den Punkt
            // gibt es die beiden Woerter gar nicht, und ein block.mine(Cool)
            // waere sonst erst beim Compiler aufgefallen - mit einer Meldung,
            // die niemandem sagt, dass ein Skill fehlt. block.ore() und
            // block.is(...) gehoeren dazu: ohne sie weiss man nicht, welches
            // Erz gerade dasteht, und damit auch nicht, was es will.
            else if (t.word == "ore" || t.word == "is" || t.word == "Cool" || t.word == "Heat")
            {
                if (!limits.allowCare)
                    return locked("block.ore(...)");
            }
            else if (t.word == "has")
            {
                if (!limits.allowBag)
                    return locked("item.has(...)");
            }
            else if (t.word == "sell")
            {
                if (!limits.allowSell)
                    return locked("item.sell()");
            }
            else if (t.word == "shared")
            {
                if (!limits.allowShared)
                    return locked("shared[...]");
            }
            // Verarbeiten: je Befehl ein Punkt im Baum.
            else if (t.word == "wash")
            {
                if (!limits.allowWash)
                    return locked("item.wash(...)");
            }
            else if (t.word == "smelt")
            {
                if (!limits.allowSmelt)
                    return locked("item.smelt(...)");
            }
            else if (t.word == "cast")
            {
                if (!limits.allowCast)
                    return locked("item.cast(...)");
            }
            else if (t.word == "clean")
            {
                if (!limits.allowClean)
                    return locked("item.clean(...)");
            }
            else if (t.word == "polish")
            {
                if (!limits.allowPolish)
                    return locked("item.polish(...)");
            }
            else if (t.word == "harden")
            {
                if (!limits.allowHarden)
                    return locked("item.harden(...)");
            }
            else if (t.word == "refine")
            {
                if (!limits.allowRefine)
                    return locked("item.refine(...)");
            }
            else if (t.word == "press")
            {
                if (!limits.allowPress)
                    return locked("item.press(...)");
            }
            // Legieren. Nachfragen und Machen haengen am selben Punkt: ohne
            // legieren zu duerfen braucht man auch die Frage nicht.
            else if (t.word == "alloy" || t.word == "canAlloy")
            {
                if (!limits.allowAlloy)
                    return locked("item.alloy(...)");
            }
            else if (t.word == "class" || t.word == "struct")
            {
                if (!limits.allowClass)
                    return locked(t.word.c_str());
                if (++classes > limits.maxClasses)
                    return TooMany("class(es)", limits.maxClasses, classes);
            }
            else if (IsTypeWord(t.word) || IsIn(classNames, t.word))
            {
                const std::string next  = (ti + 1 < tokens.size()) ? tokens[ti + 1].word : "";
                const std::string after = (ti + 2 < tokens.size()) ? tokens[ti + 2].word : "";

                // "unsigned int zahl": erst beim letzten Typwort zaehlen.
                if (IsTypeWord(next))
                    continue;
                if (!IsName(next) || IsIn(classNames, next))
                    continue;

                // "Punkt p(1, 2)" ist keine Funktion, sondern eine Variable mit
                // Klammern. Bei eigenen Klassen ist das der haeufigere Fall.
                if (after == "(" && !IsIn(classNames, t.word))
                {
                    // "int main()" gehoert zum Grundgeruest und zaehlt nicht -
                    // sonst waere das Spiel schon in Zeile 1 zu Ende.
                    if (next == "main")
                        continue;

                    if (!limits.allowFunction)
                        return locked("your own functions");
                    if (++functions > limits.maxFunctions)
                        return TooMany("function(s)", limits.maxFunctions, functions);
                }
                else
                {
                    if (!limits.allowVariable)
                        return locked("your own variables");
                    if (++variables > limits.maxVariables)
                        return TooMany("variable(s)", limits.maxVariables, variables);
                }
            }
        }
    }

    errorConsole = 0;
    errorLine    = 0;
    return "";
}
