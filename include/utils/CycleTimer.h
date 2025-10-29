#pragma once

#if defined(_WIN32)
        #include <windows.h>
        #include <time.h>
#elif defined(__APPLE__)
        #include <mach/mach.h>
        #include <mach/mach_time.h>
#else
        #include <time.h>
        #include <stdio.h>
        #include <string.h>
#endif

namespace lt {

class CycleTimer {
public:
    using SysClock = unsigned long long;

    // Tick source:
    // - Linux x86_64: RDTSC cycles (fast, user requested), secondsPerTick derived from /proc/cpuinfo once
    // - Windows: QPC ticks
    // - macOS: mach_absolute_time ticks
    // - Other POSIX: CLOCK_MONOTONIC ns
    static SysClock currentTicks() {
#if defined(__linux__) && defined(__x86_64__)
        unsigned int a, d;
        asm volatile("rdtsc" : "=a" (a), "=d" (d));
        return (static_cast<SysClock>(d) << 32) | static_cast<SysClock>(a);
#elif defined(_WIN32)
        LARGE_INTEGER qwTime;
        QueryPerformanceCounter(&qwTime);
        return static_cast<SysClock>(qwTime.QuadPart);
#elif defined(__APPLE__)
        return static_cast<SysClock>(mach_absolute_time());
#else
        timespec spec;
        clock_gettime(CLOCK_MONOTONIC, &spec);
        return static_cast<SysClock>(spec.tv_sec) * 1000000000ull + static_cast<SysClock>(spec.tv_nsec);
#endif
    }

    static double currentSeconds() {
        return static_cast<double>(currentTicks()) * secondsPerTick();
    }

    static double ticksPerSecond() { return 1.0 / secondsPerTick(); }

    static const char* tickUnits() {
#if defined(__linux__) && defined(__x86_64__)
        return "cycles";
#else
        return "ns";
#endif
    }

    // Cached conversion: computed once per process
    static double secondsPerTick() {
        static bool initialized = false;
        static double spt = 0.0;
        if (initialized) return spt;

#if defined(__linux__) && defined(__x86_64__)
                // Derive seconds per TSC tick from /proc/cpuinfo once.
                // Prefer "cpu MHz"; fallback to parsing "model name" with "@ x.xxGHz".
                FILE* fp = fopen("/proc/cpuinfo", "r");
                double spt_from_mhz = 0.0;
                double spt_from_ghz = 0.0;
                if (fp) {
                        char line[1024];
                        while (fgets(line, sizeof(line), fp)) {
                                // Try cpu MHz
                                double mhz = 0.0;
                                if (sscanf(line, "cpu MHz%*[^:]: %lf", &mhz) == 1 && mhz > 0.0) {
                                        spt_from_mhz = 1e-6 / mhz; // seconds per cycle
                                        break;
                                }
                                // Try model name @ x.xxGHz
                                const char* at = strstr(line, "@");
                                if (at) {
                                        double ghz = 0.0;
                                        if (sscanf(at + 1, " %lfGHz", &ghz) == 1 && ghz > 0.0) {
                                                spt_from_ghz = 1e-9 / ghz;
                                                // keep scanning in case cpu MHz appears later
                                        }
                                }
                        }
                        fclose(fp);
                }
                if (spt_from_mhz > 0.0) spt = spt_from_mhz;
                else if (spt_from_ghz > 0.0) spt = spt_from_ghz;
                else spt = 1e-9; // very rough fallback; user explicitly requested this path
#elif defined(_WIN32)
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        spt = 1.0 / static_cast<double>(freq.QuadPart);
#elif defined(__APPLE__)
        mach_timebase_info_data_t tbi;
        mach_timebase_info(&tbi);
        // mach_absolute_time() * (numer/denom) gives ns
        spt = (1e-9 * static_cast<double>(tbi.numer)) / static_cast<double>(tbi.denom);
#else
        // POSIX: currentTicks returns nanoseconds
        spt = 1e-9;
#endif
        initialized = true;
        return spt;
    }

    static double msPerTick() { return secondsPerTick() * 1000.0; }

private:
    CycleTimer() = default;
};

} // namespace lt
