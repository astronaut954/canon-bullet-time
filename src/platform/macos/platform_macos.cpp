#include "platform/platform.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <spawn.h>
#include <termios.h>
#include <unistd.h>

#include <climits>
#include <cstdio>
#include <clocale>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

extern char** environ;

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
    bool SdkSessionsInWorkerThreads() { return true; }

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

    namespace
    {
        // Câmera no USB = aparelho com uma interface de "imagem estática" (classe 6,
        // a usada pelas câmeras para fotos). Impressoras (7), scanners e outros
        // aparelhos da Canon não têm: não são contados como câmera.
        bool IsStillImageDevice(io_service_t device)
        {
            io_iterator_t children = 0;
            if (IORegistryEntryCreateIterator(device, kIOServicePlane, kIORegistryIterateRecursively, &children) != KERN_SUCCESS)
                return false;
            bool found = false;
            for (io_object_t child = IOIteratorNext(children); child != 0; child = IOIteratorNext(children))
            {
                if (!found)
                {
                    if (CFTypeRef value = IORegistryEntryCreateCFProperty(child, CFSTR("bInterfaceClass"), kCFAllocatorDefault, 0))
                    {
                        SInt32 interfaceClass = 0;
                        if (CFGetTypeID(value) == CFNumberGetTypeID()
                            && CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt32Type, &interfaceClass))
                            found = interfaceClass == 6;
                        CFRelease(value);
                    }
                }
                IOObjectRelease(child);
            }
            IOObjectRelease(children);
            return found;
        }
    }

    std::vector<UsbDevice> CanonUsbDevices(int& totalUsb)
    {
        std::vector<UsbDevice> devices;
        totalUsb = -1;

        // Todos os aparelhos USB; depois fica só com os da Canon (fabricante 0x04A9)
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
            if (vendor != 0x04A9 || !IsStillImageDevice(service))
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

            SInt32 product = 0;
            if (CFTypeRef value = IORegistryEntryCreateCFProperty(service, CFSTR("idProduct"), kCFAllocatorDefault, 0))
            {
                if (CFGetTypeID(value) == CFNumberGetTypeID())
                    CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt32Type, &product);
                CFRelease(value);
            }

            devices.push_back(UsbDevice{ name, location, static_cast<int>(product) });
            IOObjectRelease(service);
        }
        IOObjectRelease(iterator);
        totalUsb = total;
        return devices;
    }

    bool RelaunchAfterExit()
    {
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::vector<char> path(size + 1, '\0');
        if (_NSGetExecutablePath(path.data(), &size) != 0)
            return false;

        // A nova cópia recebe o número deste processo e espera ele terminar
        setenv("CBT_WAIT_PID", std::to_string(getpid()).c_str(), 1);
        pid_t child = 0;
        char* argv[] = { path.data(), nullptr };
        const int result = posix_spawn(&child, path.data(), nullptr, nullptr, argv, environ);
        unsetenv("CBT_WAIT_PID");
        return result == 0;
    }

    void WaitForPreviousInstance()
    {
        const char* value = std::getenv("CBT_WAIT_PID");
        if (value == nullptr)
            return;
        const pid_t previous = static_cast<pid_t>(std::atoi(value));
        unsetenv("CBT_WAIT_PID");
        // No máximo 20 s: a cópia anterior fecha em poucos segundos
        for (int i = 0; i < 200 && previous > 0 && kill(previous, 0) == 0; ++i)
            usleep(100000);
    }
}
