#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <vector>

// Function to open a perf event counter
static int perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

// Function to be profiled
void someFunction() {
    volatile int sum = 0;
    for (int i = 0; i < 1000000; ++i) {
        sum += i;  // Some computation
        if (i % 1000 == 0) {
            sum *= 2;  // Introduce branch mispredictions
        }
    }
}

int main() {
    struct perf_event_attr pe{};
    memset(&pe, 0, sizeof(pe));

    std::vector<int> event_fds;
    std::vector<std::pair<std::string, int>> events = {
        {"CPU Cycles", PERF_COUNT_HW_CPU_CYCLES},
        {"Cache Misses", PERF_COUNT_HW_CACHE_MISSES},
        {"Branch Mispredictions", PERF_COUNT_HW_BRANCH_MISSES},
        {"L1 Data Cache Misses", PERF_COUNT_HW_CACHE_L1D},  // L1 cache misses
        {"L2 Cache Misses", PERF_COUNT_HW_CACHE_L2},  // L2 cache misses
        {"L3 Cache Misses", PERF_COUNT_HW_CACHE_LL},  // L3 cache misses
        {"TLB Misses", PERF_COUNT_HW_CACHE_ITLB}  // Instruction TLB misses
    };

    for (const auto& event : events) {
        memset(&pe, 0, sizeof(pe));
        pe.type = PERF_TYPE_HARDWARE;
        pe.size = sizeof(struct perf_event_attr);
        pe.config = event.second;
        pe.disabled = 1;  // Start disabled
        pe.exclude_kernel = 1;  // User-space only
        pe.exclude_hv = 1;  // Exclude hypervisor

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

    // Execute the function to be profiled
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
