#include "format.h"

#include "proc.h"

namespace
{

bool FileExists(const wchar_t* path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// clang-format.exe suchen: erst die ueblichen Visual-Studio-Pfade, dann PATH.
std::wstring FindClangFormat()
{
    static std::wstring cached;
    static bool         searched = false;
    if (searched)
        return cached;
    searched = true;

    static const wchar_t* kCandidates[] = {
        L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\Llvm\\bin\\clang-format.exe",
        L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Tools\\Llvm\\bin\\clang-format.exe",
        L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Tools\\Llvm\\bin\\clang-format.exe",
        L"C:\\Program Files\\LLVM\\bin\\clang-format.exe",
    };

    for (const wchar_t* c : kCandidates)
    {
        if (FileExists(c))
        {
            cached = c;
            return cached;
        }
    }

    wchar_t buf[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"clang-format.exe", nullptr, MAX_PATH, buf, nullptr) != 0)
        cached = buf;

    return cached;
}

}  // namespace

std::string FormatCode(const std::string& code)
{
    const std::wstring exe = FindClangFormat();
    const std::wstring dir = WorkDir();
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

    const std::wstring stylePath = dir + L".clang-format";
    const std::wstring srcPath   = dir + L"snippet.cpp";

    if (!WriteTextFile(stylePath, kStyle) || !WriteTextFile(srcPath, code))
        return code;

    std::string out;
    if (!RunCapture(L"\"" + exe + L"\" -style=file \"" + srcPath + L"\"", dir, out, nullptr,
                    10000) ||
        out.empty())
        return code;

    return out;
}
