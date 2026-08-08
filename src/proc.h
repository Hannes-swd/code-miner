#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

// Startet ein Programm ohne sichtbares Konsolenfenster, wartet auf das Ende
// und liefert dessen Ausgabe. Rueckgabe: true bei Exitcode 0.
bool RunCapture(std::wstring cmdline, const std::wstring& workdir, std::string& out,
                const wchar_t* envBlock = nullptr, DWORD timeoutMs = 60000);

// Schreibt eine Textdatei (ohne Umkodierung).
bool WriteTextFile(const std::wstring& path, const std::string& text);

// Ordner im Temp-Verzeichnis, in dem das Programm arbeitet.
std::wstring WorkDir();
