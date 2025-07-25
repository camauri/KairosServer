// KairosServer/include/Utils/Timer.hpp
#pragma once

namespace Kairos {

class Timer; // Forward declaration for simple timer

} // namespace Kairos

// KairosServer/include/Utils/Platform.hpp
#pragma once

#include <string>
#include <cstdint>

namespace Kairos {

namespace Platform {
    // Platform information
    std::string getPlatformName();
    uint32_t getCpuCoreCount();
    uint64_t getTotalMemoryBytes();
    uint64_t getAvailableMemoryBytes();
    
    // Debug utilities
    bool isDebuggerPresent();
    void setThreadPriority(int priority);
    std::string getExecutablePath();
}

} // namespace Kairos