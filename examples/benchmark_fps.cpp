#include <capture/factory.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#include <winrt/base.h>
#endif

struct BenchmarkStats {
    double min_ms = 1e9;
    double max_ms = 0;
    double avg_ms = 0;
    double total_ms = 0;
    uint32_t frame_count = 0;
};

int main(int argc, char* argv[]) {
#ifdef _WIN32
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
#endif

    uint32_t monitor_index = 0;
    uint32_t num_frames = 300;  // ~5 seconds at 60 FPS

    // Parse command line
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--monitor" && i + 1 < argc) {
            monitor_index = std::stoi(argv[++i]);
        } else if (arg == "--frames" && i + 1 < argc) {
            num_frames = std::stoi(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "benchmark_fps - Measure display capture FPS\n"
                      << "Usage: benchmark_fps [options]\n"
                      << "  --monitor INDEX   Capture from monitor INDEX (default: 0)\n"
                      << "  --frames COUNT    Number of frames to capture (default: 300)\n"
                      << "  --help            Show this message\n";
            return 0;
        }
    }

    // Create backend
    std::unique_ptr<capture::ICaptureBackend> backend;
    auto err = capture::CaptureFactory::create_platform_backend(backend);
    if (!err.is_success()) {
        std::cerr << "Failed to create backend: " << err.to_string() << "\n";
        return 1;
    }
    std::cout << "Backend created successfully\n";

    // List available targets
    std::vector<capture::CaptureTarget> targets;
    err = backend->list_targets(targets);
    if (!err.is_success()) {
        std::cerr << "Failed to list targets: " << err.to_string() << "\n";
        return 1;
    }
    std::cout << "Found " << targets.size() << " capture targets\n";

    if (monitor_index >= targets.size()) {
        std::cerr << "Monitor index " << monitor_index << " out of range\n";
        return 1;
    }

    // Initialize backend for target
    err = backend->init(targets[monitor_index]);
    if (!err.is_success()) {
        std::cerr << "Failed to initialize backend: " << err.to_string() << "\n";
        return 1;
    }
    std::cout << "Backend initialized for monitor " << monitor_index << "\n";

    // Warmup: capture 5 frames to stabilize
    std::cout << "\nWarmup (5 frames)...\n";
    for (int i = 0; i < 5; ++i) {
        capture::Frame frame;
        backend->grab_frame(frame, 3000);  // 3 second timeout
    }

    // Benchmark: measure frame capture timing
    std::cout << "Benchmarking (" << num_frames << " frames)...\n";
    std::vector<double> frame_times;
    frame_times.reserve(num_frames);

    auto benchmark_start = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < num_frames; ++i) {
        capture::Frame frame;
        
        auto frame_start = std::chrono::high_resolution_clock::now();
        err = backend->grab_frame(frame, 3000);
        auto frame_end = std::chrono::high_resolution_clock::now();

        if (!err.is_success()) {
            std::cerr << "Failed to grab frame " << i << ": " << err.to_string() << "\n";
            break;
        }

        double frame_time_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
        frame_times.push_back(frame_time_ms);

        if ((i + 1) % 50 == 0) {
            std::cout << "  Captured " << (i + 1) << "/" << num_frames << " frames\n";
        }
    }

    auto benchmark_end = std::chrono::high_resolution_clock::now();
    double total_time_ms = std::chrono::duration<double, std::milli>(benchmark_end - benchmark_start).count();

    // Calculate statistics
    BenchmarkStats stats;
    stats.frame_count = static_cast<uint32_t>(frame_times.size());
    
    if (stats.frame_count > 0) {
        stats.total_ms = total_time_ms;
        stats.avg_ms = stats.total_ms / stats.frame_count;

        for (double t : frame_times) {
            stats.min_ms = std::min(stats.min_ms, t);
            stats.max_ms = std::max(stats.max_ms, t);
        }

        // Calculate 1% low (percentile)
        std::sort(frame_times.begin(), frame_times.end());
        size_t low_1pct_idx = frame_times.size() / 100;
        if (low_1pct_idx == 0) low_1pct_idx = 1;
        double low_1pct = frame_times[low_1pct_idx];
    }

    // Shutdown backend
    backend->shutdown();

    // Report results
    std::cout << "\n========== BENCHMARK RESULTS ==========\n";
    std::cout << "Frames captured: " << stats.frame_count << "\n";
    std::cout << "Total time: " << std::fixed << std::setprecision(2) << (stats.total_ms / 1000.0) << " seconds\n";
    std::cout << "\nPer-Frame Timing:\n";
    std::cout << "  Average:  " << std::fixed << std::setprecision(3) << stats.avg_ms << " ms\n";
    std::cout << "  Min:      " << std::fixed << std::setprecision(3) << stats.min_ms << " ms\n";
    std::cout << "  Max:      " << std::fixed << std::setprecision(3) << stats.max_ms << " ms\n";
    
    // Calculate and report FPS
    double avg_fps = 1000.0 / stats.avg_ms;
    double max_fps = 1000.0 / stats.min_ms;
    double min_fps = 1000.0 / stats.max_ms;
    
    std::cout << "\nFrames Per Second:\n";
    std::cout << "  Avg FPS:  " << std::fixed << std::setprecision(1) << avg_fps << " fps\n";
    std::cout << "  Peak FPS: " << std::fixed << std::setprecision(1) << max_fps << " fps\n";
    std::cout << "  Low FPS:  " << std::fixed << std::setprecision(1) << min_fps << " fps\n";
    
    // Validate target
    std::cout << "\n========== VALIDATION ==========\n";
    if (avg_fps >= 60.0) {
        std::cout << "[PASS] Average FPS (" << std::fixed << std::setprecision(1) << avg_fps 
                  << ") meets 60+ FPS target\n";
        return 0;
    } else {
        std::cout << "[FAIL] Average FPS (" << std::fixed << std::setprecision(1) << avg_fps 
                  << ") is below 60 FPS target\n";
        return 1;
    }
}
