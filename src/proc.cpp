#include "proc.h"

#include <cstdio>

bool RunCapture(std::wstring cmdline, const std::wstring& workdir, std::string& out,
                const wchar_t* envBlock, DWORD timeoutMs)
{
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

    DWORD flags = CREATE_NO_WINDOW;
    if (envBlock != nullptr)
        flags |= CREATE_UNICODE_ENVIRONMENT;

    cmdline.push_back(L'\0');

    PROCESS_INFORMATION pi{};
    const BOOL          started =
        CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE, flags,
                       (LPVOID)envBlock, workdir.empty() ? nullptr : workdir.c_str(), &si, &pi);
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

    WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exitCode == 0;
}

bool WriteTextFile(const std::wstring& path, const std::string& text)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || f == nullptr)
        return false;
    if (!text.empty())
        fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return true;
}

std::wstring WorkDir()
{
    wchar_t tmp[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tmp) == 0)
        return L"";

    const std::wstring dir = std::wstring(tmp) + L"codeklicker\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
