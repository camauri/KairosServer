// KairosServer/src/Utils/Platform.cpp
#include <Utils/Platform.hpp>

#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/utsname.h>
    #if defined(__linux__)
        #include <sys/sysinfo.h>
    #elif defined(__APPLE__)
        #include <sys/types.h>
        #include <sys/sysctl.h>
    #endif
#endif

namespace Kairos {

// Platform-specific utility functions
namespace Platform {

std::string getPlatformName() {
#ifdef _WIN32
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Unknown";
#endif
}

uint32_t getCpuCoreCount() {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#else
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    return (cores > 0) ? static_cast<uint32_t>(cores) : 1;
#endif
}

uint64_t getTotalMemoryBytes() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    return memInfo.ullTotalPhys;
#elif defined(__linux__)
    struct sysinfo memInfo;
    sysinfo(&memInfo);
    return memInfo.totalram * memInfo.mem_unit;
#elif defined(__APPLE__)
    int mib[2];
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    int64_t size = 0;
    size_t length = sizeof(size);
    sysctl(mib, 2, &size, &length, NULL, 0);
    return size;
#else
    return 0;
#endif
}

uint64_t getAvailableMemoryBytes() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    return memInfo.ullAvailPhys;
#elif defined(__linux__)
    struct sysinfo memInfo;
    sysinfo(&memInfo);
    return memInfo.freeram * memInfo.mem_unit;
#else
    return getTotalMemoryBytes() / 2; // Rough estimate
#endif
}

bool isDebuggerPresent() {
#ifdef _WIN32
    return IsDebuggerPresent();
#else
    return false; // Simplified for non-Windows platforms
#endif
}

void setThreadPriority(int priority) {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), priority);
#else
    // Unix thread priority setting would go here
    (void)priority; // Suppress unused parameter warning
#endif
}

std::string getExecutablePath() {
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    return std::string(path);
#elif defined(__linux__)
    char path[1024];
    ssize_t count = readlink("/proc/self/exe", path, sizeof(path));
    if (count != -1) {
        path[count] = '\0';
        return std::string(path);
    }
    return "";
#elif defined(__APPLE__)
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        return std::string(path);
    }
    return "";
#else
    return "";
#endif
}

} // namespace Platform

} // namespace Kairos