// Die Linux-Seite von proc.h. Fuer Windows siehe proc_win.cpp.
//
// Der grosse Unterschied zu Windows: es gibt kein CreateProcess, das alles auf
// einmal macht. Ein Kind entsteht in zwei Schritten - fork() macht eine Kopie
// des eigenen Prozesses, und im Kind ersetzt execv() diese Kopie durch das
// gewuenschte Programm. Zwischen den beiden Schritten haengen wir die Roehren
// ein; das ist der Grund, warum es hier ueberhaupt getrennt ist.

#include "proc.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace
{

// argv fuer execv: Zeiger auf die Texte, hinten ein nullptr.
std::vector<char*> RawArgv(const std::vector<std::string>& argv)
{
    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const std::string& a : argv)
        raw.push_back(const_cast<char*>(a.c_str()));
    raw.push_back(nullptr);
    return raw;
}

// Setzt fd auf nicht-blockierend. Damit kommt read() sofort zurueck, auch wenn
// das Kind gerade nichts zu sagen hat - genau wie PeekNamedPipe unter Windows.
void SetNonBlocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

}  // namespace

// Die Enden des laufenden Kindes. Sie stehen hier und nicht im Header, damit
// der Header auf beiden Systemen derselbe bleibt.
struct ChildImpl
{
    pid_t pid  = -1;
    int   from = -1;  // Leseende: Ausgabe des Kindes
    int   to   = -1;  // Schreibende: Freigaben an das Kind
    bool  done = false;
};

const char* Sep()
{
    return "/";
}

const char* ExeSuffix()
{
    return "";
}

bool WriteTextFile(const std::string& path, const std::string& text)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
        return false;
    if (!text.empty())
        std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    return true;
}

bool FileExists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

void MakeDir(const std::string& path)
{
    // 0755: der Besitzer darf alles, alle anderen duerfen lesen.
    mkdir(path.c_str(), 0755);
}

std::string ExeDir()
{
    // /proc/self/exe ist ein Link auf die eigene Programmdatei - das Gegenstueck
    // zu GetModuleFileName.
    char          buf[4096];
    const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
        return std::string();

    std::string       dir(buf, (std::size_t)len);
    const std::size_t cut = dir.find_last_of('/');
    if (cut != std::string::npos)
        dir.resize(cut);

    return dir;
}

std::string WorkDir()
{
    // TMPDIR, wenn gesetzt - sonst /tmp. So wie es sich unter Linux gehoert.
    const char* tmp = std::getenv("TMPDIR");
    std::string base = (tmp != nullptr && tmp[0] != '\0') ? tmp : "/tmp";
    if (base.back() != '/')
        base += '/';

    const std::string dir = base + "codeklicker/";
    MakeDir(dir);
    return dir;
}

std::string FindInPath(const std::string& program)
{
    // Ein Pfad mit / drin wird nicht gesucht, sondern direkt genommen.
    if (program.find('/') != std::string::npos)
        return FileExists(program) ? program : std::string();

    const char* path = std::getenv("PATH");
    if (path == nullptr)
        path = "/usr/local/bin:/usr/bin:/bin";

    std::string       rest = path;
    std::size_t       pos  = 0;
    while (pos <= rest.size())
    {
        std::size_t cut = rest.find(':', pos);
        if (cut == std::string::npos)
            cut = rest.size();

        std::string dir = rest.substr(pos, cut - pos);
        pos             = cut + 1;

        if (dir.empty())
            continue;
        if (dir.back() != '/')
            dir += '/';

        const std::string full = dir + program;
        if (access(full.c_str(), X_OK) == 0)
            return full;
    }

    return std::string();
}

bool RunCapture(const std::vector<std::string>& argv, const std::string& workdir, std::string& out,
                const std::vector<std::string>* env, int timeoutMs)
{
    if (argv.empty())
        return false;

    int pipefd[2];
    if (pipe(pipefd) != 0)
        return false;

    const pid_t pid = fork();
    if (pid < 0)
    {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return false;
    }

    if (pid == 0)
    {
        // Im Kind. Ab hier darf nichts mehr schiefgehen ausser _exit.
        ::close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[1]);

        if (!workdir.empty() && chdir(workdir.c_str()) != 0)
            _exit(127);

        std::vector<char*> raw = RawArgv(argv);

        if (env != nullptr && !env->empty())
        {
            std::vector<char*> rawEnv = RawArgv(*env);
            execve(argv[0].c_str(), raw.data(), rawEnv.data());
        }
        else
        {
            execv(argv[0].c_str(), raw.data());
        }

        _exit(127);  // execv kommt nur zurueck, wenn es nicht geklappt hat
    }

    ::close(pipefd[1]);

    // Lesen, bis das Kind sein Ende der Roehre zumacht - das ist gleichzeitig
    // das Zeichen, dass es fertig ist.
    char    buf[4096];
    ssize_t got = 0;
    while ((got = ::read(pipefd[0], buf, sizeof(buf))) > 0)
        out.append(buf, (std::size_t)got);
    ::close(pipefd[0]);

    (void)timeoutMs;  // die Roehre zeigt das Ende an, ein Wecker ist unnoetig

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return false;

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// ---- Das Programm des Spielers --------------------------------------------

Child::~Child()
{
    close();
}

bool Child::start(const std::string& exe, const std::string& workdir)
{
    close();

    int toChild[2];    // Spiel -> Kind
    int fromChild[2];  // Kind  -> Spiel

    if (pipe(toChild) != 0)
        return false;
    if (pipe(fromChild) != 0)
    {
        ::close(toChild[0]);
        ::close(toChild[1]);
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0)
    {
        ::close(toChild[0]);
        ::close(toChild[1]);
        ::close(fromChild[0]);
        ::close(fromChild[1]);
        return false;
    }

    if (pid == 0)
    {
        ::close(toChild[1]);
        ::close(fromChild[0]);

        dup2(toChild[0], STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);
        dup2(fromChild[1], STDERR_FILENO);

        ::close(toChild[0]);
        ::close(fromChild[1]);

        if (!workdir.empty() && chdir(workdir.c_str()) != 0)
            _exit(127);

        char* raw[2] = {const_cast<char*>(exe.c_str()), nullptr};
        execv(exe.c_str(), raw);
        _exit(127);
    }

    ::close(toChild[0]);
    ::close(fromChild[1]);

    SetNonBlocking(fromChild[0]);

    mImpl       = new ChildImpl();
    mImpl->pid  = pid;
    mImpl->from = fromChild[0];
    mImpl->to   = toChild[1];
    mExit       = 0;
    return true;
}

std::size_t Child::read(char* buf, std::size_t size)
{
    if (mImpl == nullptr || mImpl->from < 0)
        return 0;

    const ssize_t got = ::read(mImpl->from, buf, size);
    if (got <= 0)
        return 0;  // EAGAIN heisst nur: gerade nichts da

    return (std::size_t)got;
}

void Child::write(const char* text)
{
    if (mImpl == nullptr || mImpl->to < 0)
        return;

    const std::size_t len = std::strlen(text);
    std::size_t       off = 0;
    while (off < len)
    {
        const ssize_t got = ::write(mImpl->to, text + off, len - off);
        if (got <= 0)
        {
            // Kind schon weg: EPIPE. Das ist kein Fehler, der jemanden stoert -
            // gleich darauf merkt alive() ohnehin, dass es vorbei ist.
            break;
        }
        off += (std::size_t)got;
    }
}

bool Child::alive()
{
    if (mImpl == nullptr || mImpl->pid < 0 || mImpl->done)
        return false;

    int         status = 0;
    const pid_t got    = waitpid(mImpl->pid, &status, WNOHANG);
    if (got == 0)
        return true;  // laeuft noch

    if (got < 0)
    {
        mImpl->done = true;
        return false;
    }

    if (WIFEXITED(status))
        mExit = (unsigned long)WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        mExit = 256ul + (unsigned long)WTERMSIG(status);  // siehe CrashText

    mImpl->done = true;
    return false;
}

void Child::close()
{
    if (mImpl == nullptr)
        return;

    if (mImpl->pid > 0 && !mImpl->done)
    {
        kill(mImpl->pid, SIGKILL);

        int status = 0;
        waitpid(mImpl->pid, &status, 0);
    }
    if (mImpl->from >= 0)
        ::close(mImpl->from);
    if (mImpl->to >= 0)
        ::close(mImpl->to);

    delete mImpl;
    mImpl = nullptr;
}

std::string CrashText(unsigned long code)
{
    // Ueber 256 heisst: ein Signal hat das Programm umgebracht. Das ist unter
    // Linux der uebliche Weg, auf dem ein Absturz ankommt.
    if (code > 256ul)
    {
        switch ((int)(code - 256ul))
        {
        case SIGSEGV:
        case SIGBUS:
            return "The program reached somewhere that does not exist. Usually a pointer or "
                   "an index past the end of an array - e.g. numbers[10] with only 10 slots "
                   "(0 to 9).";
        case SIGFPE:
            return "Divided by zero. Before every / ask whether the divisor really is "
                   "not zero.";
        case SIGABRT:
            return "The program aborted itself. Usually an access out of bounds that C++ "
                   "caught just in time.";
        case SIGKILL:
            return "The program was stopped from outside.";
        default:
            break;
        }

        char buf[96];
        std::snprintf(buf, sizeof(buf), "The program died halfway through (signal %d).",
                      (int)(code - 256ul));
        return buf;
    }

    char buf[96];
    std::snprintf(buf, sizeof(buf), "The program died halfway through (code %lu).", code);
    return buf;
}
