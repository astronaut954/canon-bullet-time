#include "platform/platform.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach-o/dyld.h>
#include <termios.h>
#include <unistd.h>

#include <climits>
#include <cstdio>
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

    void BeginSdkThread() {}
    void EndSdkThread() {}

    void DiscardPendingInput()
    {
        tcflush(STDIN_FILENO, TCIFLUSH);
    }

    fs::path AppDataFolder()
    {
        const char* home = std::getenv("HOME");
        if (home != nullptr && *home != '\0')
            return fs::path(home) / "Library" / "Application Support" / "Canon Bullet Time";

        return fs::current_path() / "Canon Bullet Time";
    }

    std::vector<std::string> CanonUsbDevices()
    {
        std::vector<std::string> devices;

        // Todos os aparelhos USB; depois fica só com os da Canon (fabricante 0x04A9).
        // O último item é o total de aparelhos USB vistos (diagnóstico).
        CFMutableDictionaryRef matching = IOServiceMatching("IOUSBHostDevice");
        if (matching == nullptr)
            return devices;

        io_iterator_t iterator = 0;
        // IOServiceGetMatchingServices fica com "matching" (não liberar aqui)
        if (IOServiceGetMatchingServices(MACH_PORT_NULL, matching, &iterator) != KERN_SUCCESS)
            return devices;

        int total = 0;
        for (io_service_t service = IOIteratorNext(iterator); service != 0; service = IOIteratorNext(iterator))
        {
            ++total;
            SInt32 vendor = 0;
            if (CFTypeRef value = IORegistryEntryCreateCFProperty(service, CFSTR("idVendor"), kCFAllocatorDefault, 0))
            {
                if (CFGetTypeID(value) == CFNumberGetTypeID())
                    CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt32Type, &vendor);
                CFRelease(value);
            }
            if (vendor != 0x04A9)
            {
                IOObjectRelease(service);
                continue;
            }

            std::string name = "Canon USB device";
            if (CFTypeRef value = IORegistryEntryCreateCFProperty(service, CFSTR("USB Product Name"), kCFAllocatorDefault, 0))
            {
                if (CFGetTypeID(value) == CFStringGetTypeID())
                {
                    char buffer[256] = {};
                    if (CFStringGetCString(static_cast<CFStringRef>(value), buffer, sizeof(buffer), kCFStringEncodingUTF8))
                        name = buffer;
                }
                CFRelease(value);
            }

            std::string location = "?";
            if (CFTypeRef value = IORegistryEntryCreateCFProperty(service, CFSTR("locationID"), kCFAllocatorDefault, 0))
            {
                SInt64 id = 0;
                if (CFGetTypeID(value) == CFNumberGetTypeID() && CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &id))
                {
                    char buffer[32] = {};
                    std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(id));
                    location = buffer;
                }
                CFRelease(value);
            }

            devices.push_back(name + " @ " + location);
            IOObjectRelease(service);
        }
        IOObjectRelease(iterator);
        devices.push_back("(" + std::to_string(total) + " USB devices in total)");
        return devices;
    }
}
