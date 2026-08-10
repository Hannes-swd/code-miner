#include "format.h"

#include "proc.h"

namespace
{

// clang-format suchen. Unter Windows liegt es bei Visual Studio dabei, unter
// Linux kommt es aus dem PATH (Paket clang-format). Gefunden wird es einmal,
// danach steht es fest.
std::string FindClangFormat()
{
    static std::string cached;
    static bool        searched = false;
    if (searched)
        return cached;
    searched = true;

    static const char* kCandidates[] = {
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/bin/clang-format.exe",
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/Llvm/bin/"
        "clang-format.exe",
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/Llvm/bin/"
        "clang-format.exe",
        "C:/Program Files/LLVM/bin/clang-format.exe",
    };

    for (const char* c : kCandidates)
    {
        if (FileExists(c))
        {
            cached = c;
            return cached;
        }
    }

    cached = FindInPath(std::string("clang-format") + ExeSuffix());
    return cached;
}

}  // namespace

std::string FormatCode(const std::string& code)
{
    const std::string exe = FindClangFormat();
    const std::string dir = WorkDir();
    if (exe.empty() || dir.empty())
        return code;

    // Der Stil kommt aus einer .clang-format Datei daneben - das erspart das
    // Escapen langer Optionen auf der Kommandozeile.
    static const char* kStyle =
        "BasedOnStyle: LLVM\n"
        "IndentWidth: 4\n"
        "ColumnLimit: 100\n"
        "BreakBeforeBraces: Attach\n"
        "AllowShortFunctionsOnASingleLine: Empty\n"
        "AllowShortIfStatementsOnASingleLine: false\n"
        "AllowShortLoopsOnASingleLine: false\n"
        "SpaceBeforeParens: ControlStatements\n"
        "PointerAlignment: Left\n"
        // Ergaenzt fehlende { } bei einzeiligen if/for/while-Rumpfen.
        // Das ist nicht nur Kosmetik: nur Anweisungen in einem Block
        // bekommen eine Zeilenmarkierung.
        "InsertBraces: true\n";

    const std::string stylePath = dir + ".clang-format";
    const std::string srcPath   = dir + "snippet.cpp";

    if (!WriteTextFile(stylePath, kStyle) || !WriteTextFile(srcPath, code))
        return code;

    std::string out;
    if (!RunCapture({exe, "-style=file", srcPath}, dir, out, nullptr, 10000) || out.empty())
        return code;

    return out;
}
