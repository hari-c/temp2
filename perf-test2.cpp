#define _GNU_SOURCE
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sched.h>   // For CPU pinning
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <immintrin.h>  // For SIMD (AVX/AVX2/SSE)

// Function to open a perf event counter
static int perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

// Pin process to a single CPU core to reduce cache thrashing
void pinToCore(int core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) < 0) {
        std::cerr << "Failed to set CPU affinity" << std::endl;
    }
}

// Set process to real-time priority
void setRealTimePriority() {
    struct sched_param param;
    param.sched_priority = 99; // Highest RT priority
    if (sched_setscheduler(0, SCHED_FIFO, &param) < 0) {
        std::cerr << "Failed to set real-time priority" << std::endl;
    }
}

// Optimize Function using Branchless Programming & SIMD
void someFunction() {
    constexpr size_t ARRAY_SIZE = 1024 * 1024;  // 1M elements
    alignas(64) int arr[ARRAY_SIZE];  // Cache-aligned

    // Prefetch next data in advance
    for (size_t i = 0; i < ARRAY_SIZE; i += 16) {
        _mm_prefetch(reinterpret_cast<const char*>(&arr[i + 16]), _MM_HINT_T0);
    }

    __m256i sum = _mm256_setzero_si256();  // SIMD register

    // Vectorized processing (AVX2)
    for (size_t i = 0; i < ARRAY_SIZE; i += 8) {
        __m256i data = _mm256_load_si256(reinterpret_cast<const __m256i*>(&arr[i]));
        sum = _mm256_add_epi32(sum, data);
    }

    int finalSum[8];
    _mm256_store_si256(reinterpret_cast<__m256i*>(finalSum), sum);
}

// Main function
int main() {
    // Pin to a single CPU core for low latency
    pinToCore(0);

    // Set real-time priority
    setRealTimePriority();

    struct perf_event_attr pe{};
    memset(&pe, 0, sizeof(pe));

    std::vector<int> event_fds;
    std::vector<std::pair<std::string, int>> events = {
        {"CPU Cycles", PERF_COUNT_HW_CPU_CYCLES},
        {"Cache Misses", PERF_COUNT_HW_CACHE_MISSES},
        {"Branch Mispredictions", PERF_COUNT_HW_BRANCH_MISSES},
        {"L1 Data Cache Misses", PERF_COUNT_HW_CACHE_L1D},
        {"L2 Cache Misses", PERF_COUNT_HW_CACHE_L2},
        {"L3 Cache Misses", PERF_COUNT_HW_CACHE_LL},
        {"TLB Misses", PERF_COUNT_HW_CACHE_ITLB}
    };

    for (const auto& event : events) {
        memset(&pe, 0, sizeof(pe));
        pe.type = PERF_TYPE_HARDWARE;
        pe.size = sizeof(struct perf_event_attr);
        pe.config = event.second;
        pe.disabled = 1;
        pe.exclude_kernel = 1;
        pe.exclude_hv = 1;

        int fd = perf_event_open(&pe, 0, -1, -1, 0);
        if (fd == -1) {
            std::cerr << "Error opening perf event for " << event.first << ": " << strerror(errno) << std::endl;
            return 1;
        }
        event_fds.push_back(fd);
    }

    // Start profiling
    for (int fd : event_fds) {
        ioctl(fd, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    }

    // Execute the optimized function
    someFunction();

    // Stop profiling
    for (int fd : event_fds) {
        ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    }

    // Read and display results
    for (size_t i = 0; i < events.size(); ++i) {
        long long count;
        read(event_fds[i], &count, sizeof(count));
        std::cout << events[i].first << ": " << count << std::endl;
        close(event_fds[i]);  // Close file descriptor
    }

    return 0;
}
