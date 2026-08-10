#include "datapath.h"

#include "proc.h"

std::vector<std::string> DataPaths(const std::string& name)
{
    std::vector<std::string> out;

    // Schraegstriche gehen auf beiden Systemen: Windows nimmt sie genauso an
    // wie Backslashes. Damit bleibt die Liste hier lesbar.
    const std::string dir = ExeDir();
    if (!dir.empty())
    {
        out.push_back(dir + "/../../data/" + name);  // build/Debug -> Projekt
        out.push_back(dir + "/../../../data/" + name);
        out.push_back(dir + "/../data/" + name);
        out.push_back(dir + "/data/" + name);  // neben dem Programm
        out.push_back(dir + "/" + name);
    }

    // Zuletzt der Arbeitsordner - falls das Spiel von woanders gestartet wurde.
    out.push_back("data/" + name);
    out.push_back(name);
    return out;
}
