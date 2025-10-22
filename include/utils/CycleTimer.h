#pragma once

#if defined(__APPLE__)
    #if defined(__x86_64__)
        #include <sys/sysctl.h>
    #else
        #include <mach/mach.h>
        #include <mach/mach_time.h>
    #endif
    #include <stdio.h>
    #include <stdlib.h>
#elif _WIN32
    #include <windows.h>
    #include <time.h>
#else
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <sys/time.h>
    #include <time.h>
#endif

namespace lt {

class CycleTimer {
public:
    using SysClock = unsigned long long;

    // Return current CPU time in ticks (or nanoseconds on some platforms)
    static SysClock currentTicks() {
#if defined(__APPLE__) && !defined(__x86_64__)
        return mach_absolute_time();
#elif defined(_WIN32)
        LARGE_INTEGER qwTime;
        QueryPerformanceCounter(&qwTime);
        return qwTime.QuadPart;
#elif defined(__x86_64__)
        unsigned int a, d;
        asm volatile("rdtsc" : "=a" (a), "=d" (d));
        return static_cast<unsigned long long>(a) |
                     (static_cast<unsigned long long>(d) << 32);
#elif defined(__ARM_NEON__) && 0
        unsigned int val;
        asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(val));
        return val;
#elif defined(__ARM_ARCH)
        timespec spec;
        clock_gettime(CLOCK_MONOTONIC_RAW, &spec);
        return static_cast<unsigned long long>(spec.tv_sec) * 1000000000ull +
                     static_cast<unsigned long long>(spec.tv_nsec);
#else
        timespec spec;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &spec);
        return static_cast<unsigned long long>(static_cast<double>(spec.tv_sec) * 1e9 +
                                                                                     static_cast<double>(spec.tv_nsec));
#endif
    }

    // Return current CPU time in seconds (double)
    static double currentSeconds() {
        return static_cast<double>(currentTicks()) * secondsPerTick();
    }

    static double ticksPerSecond() { return 1.0 / secondsPerTick(); }

    static const char* tickUnits() {
#if defined(__APPLE__) && !defined(__x86_64__)
        return "ns";
#elif defined(__WIN32__) || defined(__x86_64__)
        return "cycles";
#else
        return "ns";
#endif
    }

    static double secondsPerTick() {
        static bool initialized = false;
        static double secondsPerTick_val;
        if (initialized) return secondsPerTick_val;

#if defined(__APPLE__)
    #ifdef __x86_64__
        int args[] = {CTL_HW, HW_CPU_FREQ};
        unsigned int Hz;
        size_t len = sizeof(Hz);
        if (sysctl(args, 2, &Hz, &len, NULL, 0) != 0) {
             fprintf(stderr, "Failed to initialize secondsPerTick_val!\n");
             exit(-1);
        }
        secondsPerTick_val = 1.0 / static_cast<double>(Hz);
    #else
        mach_timebase_info_data_t time_info;
        mach_timebase_info(&time_info);
        secondsPerTick_val = (1e-9 * static_cast<double>(time_info.numer)) /
                                                 static_cast<double>(time_info.denom);
    #endif
#elif defined(_WIN32)
        LARGE_INTEGER qwTicksPerSec;
        QueryPerformanceFrequency(&qwTicksPerSec);
        secondsPerTick_val = 1.0 / static_cast<double>(qwTicksPerSec.QuadPart);
#else
        FILE *fp = fopen("/proc/cpuinfo","r");
        char input[1024];
        if (!fp) {
             fprintf(stderr, "CycleTimer::resetScale failed: couldn't find /proc/cpuinfo.");
             exit(-1);
        }
        secondsPerTick_val = 1e-9; // default
        while (!feof(fp) && fgets(input, 1024, fp)) {
            float GHz = 0.0f, MHz = 0.0f;
            if (strstr(input, "model name")) {
                char* at_sign = strstr(input, "@");
                if (at_sign) {
                    char* after_at = at_sign + 1;
                    char* GHz_str = strstr(after_at, "GHz");
                    char* MHz_str = strstr(after_at, "MHz");
                    if (GHz_str) {
                        *GHz_str = '\0';
                        if (1 == sscanf(after_at, "%f", &GHz)) {
                            secondsPerTick_val = 1e-9 / static_cast<double>(GHz);
                            break;
                        }
                    } else if (MHz_str) {
                        *MHz_str = '\0';
                        if (1 == sscanf(after_at, "%f", &MHz)) {
                            secondsPerTick_val = 1e-6 / static_cast<double>(MHz);
                            break;
                        }
                    }
                }
            } else if (1 == sscanf(input, "cpu MHz : %f", &MHz)) {
                secondsPerTick_val = 1e-6 / static_cast<double>(MHz);
                break;
            }
        }
        fclose(fp);
#endif

        initialized = true;
        return secondsPerTick_val;
    }

    static double msPerTick() { return secondsPerTick() * 1000.0; }

private:
    CycleTimer() = default;
};

} // namespace lt
