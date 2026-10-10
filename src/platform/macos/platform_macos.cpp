#include "platform/platform.h"

#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>

#include <climits>
#include <clocale>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace Platform
{
    void InitConsole()
    {
        // O Terminal do macOS já usa UTF-8; só alinhamos a localidade do programa.
        setlocale(LC_ALL, "");
    }

    fs::path ExecutableDirectory()
    {
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);

        std::vector<char> buffer(size + 1, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        {
            return fs::current_path();
        }

        char resolved[PATH_MAX];
        if (realpath(buffer.data(), resolved) == nullptr)
        {
            return fs::path(buffer.data()).parent_path();
        }

        return fs::path(resolved).parent_path();
    }

    fs::path DownloadsFolder()
    {
        const char* home = std::getenv("HOME");
        if (home != nullptr && *home != '\0')
        {
            return fs::path(home) / "Downloads";
        }

        return fs::current_path();
    }

    std::tm LocalTime(std::time_t t)
    {
        std::tm result{};
        localtime_r(&t, &result);
        return result;
    }

    void ProcessEvents(int milliseconds)
    {
        const CFTimeInterval seconds = milliseconds > 0 ? milliseconds / 1000.0 : 0.0;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false);
    }
}
