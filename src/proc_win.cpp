// Die Windows-Seite von proc.h. Fuer Linux siehe proc_posix.cpp.

#include "proc.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>

namespace
{

// UTF-8 -> UTF-16. Windows will ueberall wchar_t, das Spiel rechnet in UTF-8.
std::wstring Widen(const std::string& s)
{
    if (s.empty())
        return std::wstring();

    const int    len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((std::size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

std::string Narrow(const std::wstring& s)
{
    if (s.empty())
        return std::string();

    const int len =
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((std::size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len, nullptr, nullptr);
    return out;
}

// Aus einzelnen Argumenten EINE Kommandozeile bauen - so, wie Windows sie
// wieder auseinandernimmt. Alles mit Leerzeichen kommt in Anfuehrungszeichen;
// Anfuehrungszeichen und Backslashes davor werden escaped.
std::wstring BuildCommandLine(const std::vector<std::string>& argv)
{
    std::wstring cmd;
    for (const std::string& arg : argv)
    {
        if (!cmd.empty())
            cmd += L' ';

        const std::wstring w      = Widen(arg);
        const bool         quotes = w.empty() || w.find_first_of(L" \t\"") != std::wstring::npos;

        if (!quotes)
        {
            cmd += w;
            continue;
        }

        cmd += L'"';
        for (std::size_t i = 0; i < w.size(); ++i)
        {
            std::size_t backslashes = 0;
            while (i < w.size() && w[i] == L'\\')
            {
                ++backslashes;
                ++i;
            }

            if (i == w.size())
            {
                cmd.append(backslashes * 2, L'\\');
                break;
            }
            if (w[i] == L'"')
            {
                cmd.append(backslashes * 2 + 1, L'\\');
                cmd += L'"';
            }
            else
            {
                cmd.append(backslashes, L'\\');
                cmd += w[i];
            }
        }
        cmd += L'"';
    }
    return cmd;
}

// Die Umgebung fuer CreateProcess: doppelt nullterminierter UTF-16-Block.
std::vector<wchar_t> BuildEnvBlock(const std::vector<std::string>& env)
{
    std::vector<wchar_t> block;
    for (const std::string& entry : env)
    {
        const std::wstring w = Widen(entry);
        block.insert(block.end(), w.begin(), w.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

}  // namespace

// Die drei Handles des laufenden Kindes. Sie stehen hier und nicht im Header,
// damit der Rest des Spiels kein <windows.h> sieht.
struct ChildImpl
{
    HANDLE process = nullptr;
    HANDLE from    = nullptr;  // Leseende: Ausgabe des Kindes
    HANDLE to      = nullptr;  // Schreibende: Freigaben an das Kind
    bool   done    = false;
};

const char* Sep()
{
    return "\\";
}

const char* ExeSuffix()
{
    return ".exe";
}

bool WriteTextFile(const std::string& path, const std::string& text)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, Widen(path).c_str(), L"wb") != 0 || f == nullptr)
        return false;
    if (!text.empty())
        fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return true;
}

bool FileExists(const std::string& path)
{
    return GetFileAttributesW(Widen(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

void MakeDir(const std::string& path)
{
    CreateDirectoryW(Widen(path).c_str(), nullptr);
}

std::string ExeDir()
{
    wchar_t     exe[MAX_PATH] = {};
    const DWORD len           = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (len == 0)
        return std::string();

    std::wstring      dir(exe, len);
    const std::size_t cut = dir.find_last_of(L"\\/");
    if (cut != std::wstring::npos)
        dir.resize(cut);

    return Narrow(dir);
}

std::string WorkDir()
{
    wchar_t tmp[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tmp) == 0)
        return std::string();

    const std::wstring dir = std::wstring(tmp) + L"codeklicker\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    return Narrow(dir);
}

std::string FindInPath(const std::string& program)
{
    wchar_t buf[MAX_PATH] = {};
    if (SearchPathW(nullptr, Widen(program).c_str(), nullptr, MAX_PATH, buf, nullptr) == 0)
        return std::string();
    return Narrow(buf);
}

bool RunCapture(const std::vector<std::string>& argv, const std::string& workdir, std::string& out,
                const std::vector<std::string>* env, int timeoutMs)
{
    if (argv.empty())
        return false;

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE              readEnd  = nullptr;
    HANDLE              writeEnd = nullptr;

    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0))
        return false;
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd;
    si.hStdError  = writeEnd;
    si.hStdInput  = nullptr;

    std::vector<wchar_t> envBlock;
    DWORD                flags = CREATE_NO_WINDOW;
    if (env != nullptr && !env->empty())
    {
        envBlock = BuildEnvBlock(*env);
        flags |= CREATE_UNICODE_ENVIRONMENT;
    }

    std::wstring cmd = BuildCommandLine(argv);
    cmd.push_back(L'\0');

    const std::wstring dir = Widen(workdir);

    PROCESS_INFORMATION pi{};
    const BOOL          started =
        CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, flags,
                       envBlock.empty() ? nullptr : (LPVOID)envBlock.data(),
                       dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    CloseHandle(writeEnd);

    if (!started)
    {
        CloseHandle(readEnd);
        return false;
    }

    char  buf[4096];
    DWORD got = 0;
    while (ReadFile(readEnd, buf, sizeof(buf), &got, nullptr) && got > 0)
        out.append(buf, got);
    CloseHandle(readEnd);

    WaitForSingleObject(pi.hProcess, (DWORD)timeoutMs);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exitCode == 0;
}

// ---- Das Programm des Spielers --------------------------------------------

Child::~Child()
{
    close();
}

bool Child::start(const std::string& exe, const std::string& workdir)
{
    close();

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};

    HANDLE outRead = nullptr, outWrite = nullptr;
    HANDLE inRead = nullptr, inWrite = nullptr;

    if (!CreatePipe(&outRead, &outWrite, &sa, 0))
        return false;
    if (!CreatePipe(&inRead, &inWrite, &sa, 0))
    {
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return false;
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = outWrite;
    si.hStdError  = outWrite;
    si.hStdInput  = inRead;

    std::wstring cmd = BuildCommandLine({exe});
    cmd.push_back(L'\0');

    const std::wstring dir = Widen(workdir);

    PROCESS_INFORMATION pi{};
    const BOOL          ok =
        CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                       dir.empty() ? nullptr : dir.c_str(), &si, &pi);

    CloseHandle(outWrite);
    CloseHandle(inRead);

    if (!ok)
    {
        CloseHandle(outRead);
        CloseHandle(inWrite);
        return false;
    }

    CloseHandle(pi.hThread);

    mImpl          = new ChildImpl();
    mImpl->process = pi.hProcess;
    mImpl->from    = outRead;
    mImpl->to      = inWrite;
    mExit          = 0;
    return true;
}

std::size_t Child::read(char* buf, std::size_t size)
{
    if (mImpl == nullptr || mImpl->from == nullptr)
        return 0;

    // PeekNamedPipe fragt nach, ohne stehen zu bleiben. Ohne das wuerde
    // ReadFile warten, bis das Kind etwas sagt - und das Bild bliebe stehen.
    DWORD available = 0;
    if (!PeekNamedPipe(mImpl->from, nullptr, 0, nullptr, &available, nullptr) || available == 0)
        return 0;

    const DWORD want = (available < (DWORD)size) ? available : (DWORD)size;
    DWORD       got  = 0;
    if (!ReadFile(mImpl->from, buf, want, &got, nullptr))
        return 0;

    return (std::size_t)got;
}

void Child::write(const char* text)
{
    if (mImpl == nullptr || mImpl->to == nullptr)
        return;

    DWORD written = 0;
    WriteFile(mImpl->to, text, (DWORD)std::strlen(text), &written, nullptr);
}

bool Child::alive()
{
    if (mImpl == nullptr || mImpl->process == nullptr || mImpl->done)
        return false;

    if (WaitForSingleObject(mImpl->process, 0) == WAIT_OBJECT_0)
    {
        DWORD code = 0;
        GetExitCodeProcess(mImpl->process, &code);
        mExit       = code;
        mImpl->done = true;
        return false;
    }
    return true;
}

void Child::close()
{
    if (mImpl == nullptr)
        return;

    if (mImpl->process != nullptr)
    {
        if (WaitForSingleObject(mImpl->process, 0) != WAIT_OBJECT_0)
            TerminateProcess(mImpl->process, 1);
        CloseHandle(mImpl->process);
    }
    if (mImpl->from != nullptr)
        CloseHandle(mImpl->from);
    if (mImpl->to != nullptr)
        CloseHandle(mImpl->to);

    delete mImpl;
    mImpl = nullptr;
}

std::string CrashText(unsigned long code)
{
    switch (code)
    {
    case 0xC0000005ul:
        return "The program reached somewhere that does not exist. Usually a pointer or "
               "an index past the end of an array - e.g. numbers[10] with only 10 slots (0 to 9).";
    case 0xC0000094ul:
    case 0xC0000095ul:
        return "Divided by zero. Before every / ask whether the divisor really is "
               "not zero.";
    case 0xC00000FDul:
        return "The memory overflowed. Almost always a function calls itself endlessly - "
               "is it missing its stopping if?";
    case 0xC0000409ul:
    case 3ul:
        return "The program aborted itself. Usually an access out of bounds that C++ "
               "caught just in time.";
    default:
        break;
    }

    char buf[96];
    std::snprintf(buf, sizeof(buf), "The program died halfway through (code 0x%08lX).", code);
    return buf;
}
