#include "audio_player.h"

#include <csignal>
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

// Toca os áudios com o "afplay", que vem em todo macOS.
// Assim como no Windows, um som novo interrompe o anterior.

namespace
{
    pid_t currentPid = 0;

    void ReapIfFinished()
    {
        if (currentPid > 0 && waitpid(currentPid, nullptr, WNOHANG) == currentPid)
        {
            currentPid = 0;
        }
    }

    bool Spawn(const std::string& filePath, pid_t& pid)
    {
        const char* argv[] = { "/usr/bin/afplay", filePath.c_str(), nullptr };
        return posix_spawn(&pid, argv[0], nullptr, nullptr,
                           const_cast<char* const*>(argv), environ) == 0;
    }
}

namespace AudioPlayer
{
    bool PlayWavAsync(const std::string& filePath)
    {
        Stop();

        pid_t pid = 0;
        if (!Spawn(filePath, pid))
        {
            return false;
        }

        currentPid = pid;
        return true;
    }

    bool PlayWavSync(const std::string& filePath)
    {
        Stop();

        pid_t pid = 0;
        if (!Spawn(filePath, pid))
        {
            return false;
        }

        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    void Stop()
    {
        ReapIfFinished();

        if (currentPid > 0)
        {
            kill(currentPid, SIGTERM);
            waitpid(currentPid, nullptr, 0);
            currentPid = 0;
        }
    }
}
