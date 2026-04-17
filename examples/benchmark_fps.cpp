#include <capture/factory.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
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

bool parse_region(const std::string& value, capture::CaptureRect& out_region) {
    char comma1 = '\0';
    char comma2 = '\0';
    char comma3 = '\0';
    std::istringstream stream(value);
    stream >> out_region.x >> comma1 >> out_region.y >> comma2
           >> out_region.width >> comma3 >> out_region.height;

    return stream && comma1 == ',' && comma2 == ',' && comma3 == ',';
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
#endif

    capture::BackendType backend_type = capture::BackendType::Auto;
    uint32_t monitor_index = 0;
    uint32_t num_frames = 300;  // ~5 seconds at 60 FPS
    int timeout_ms = 3000;
    bool json_output = false;
    capture::CaptureRect region;
    bool has_region = false;

    // Parse command line
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--backend" && i + 1 < argc) {
            std::string backend_str = argv[++i];
            if (backend_str == "windows") {
                backend_type = capture::BackendType::Windows;
            } else if (backend_str == "macos") {
                backend_type = capture::BackendType::MacOS;
            } else if (backend_str == "auto") {
                backend_type = capture::BackendType::Auto;
            } else {
                std::cerr << "Unknown backend: " << backend_str << "\n";
                return 1;
            }
        } else if (arg == "--monitor" && i + 1 < argc) {
            monitor_index = std::stoi(argv[++i]);
        } else if (arg == "--frames" && i + 1 < argc) {
            num_frames = std::stoi(argv[++i]);
        } else if (arg == "--timeout-ms" && i + 1 < argc) {
            timeout_ms = std::stoi(argv[++i]);
        } else if (arg == "--region" && i + 1 < argc) {
            if (!parse_region(argv[++i], region)) {
                std::cerr << "Invalid region. Expected x,y,w,h\n";
                return 1;
            }
            has_region = true;
        } else if (arg == "--json") {
            json_output = true;
        } else if (arg == "--help") {
            std::cout << "benchmark_fps - Measure display capture FPS\n"
                      << "Usage: benchmark_fps [options]\n"
                      << "  --backend NAME    Backend: windows, macos, auto (default: auto)\n"
                      << "  --monitor INDEX   Capture from monitor INDEX (default: 0)\n"
                      << "  --frames COUNT    Number of frames to capture (default: 300)\n"
                      << "  --timeout-ms MS   Frame timeout (default: 3000)\n"
                      << "  --region X,Y,W,H  Capture region relative to the monitor\n"
                      << "  --json            Print machine-readable metrics\n"
                      << "  --help            Show this message\n";
            return 0;
        } else {
            std::cerr << "Unknown or incomplete option: " << arg << "\n";
            return 1;
        }
    }

    // Create backend
    std::unique_ptr<capture::ICaptureBackend> backend;
    auto err = capture::CaptureFactory::create_backend(backend_type, backend);
    if (!err.is_success()) {
        std::cerr << "Failed to create backend: " << err.to_string() << "\n";
        return 1;
    }
    if (!json_output) {
        std::cout << "Backend created successfully\n";
    }

    // List available targets
    std::vector<capture::CaptureTarget> targets;
    err = backend->list_targets(targets);
    if (!err.is_success()) {
        std::cerr << "Failed to list targets: " << err.to_string() << "\n";
        return 1;
    }
    if (!json_output) {
        std::cout << "Found " << targets.size() << " capture targets\n";
    }

    if (monitor_index >= targets.size()) {
        std::cerr << "Monitor index " << monitor_index << " out of range\n";
        return 1;
    }

    capture::CaptureTarget selected_target = targets[monitor_index];
    if (has_region) {
        selected_target.region = region;
        selected_target.has_region = true;
    }

    // Initialize backend for target
    err = backend->init(selected_target);
    if (!err.is_success()) {
        std::cerr << "Failed to initialize backend: " << err.to_string() << "\n";
        return 1;
    }
    if (!json_output) {
        std::cout << "Backend initialized for monitor " << monitor_index << "\n";
    }

    // Warmup: capture 5 frames to stabilize
    if (!json_output) {
        std::cout << "\nWarmup (5 frames)...\n";
    }
    for (int i = 0; i < 5; ++i) {
        capture::Frame frame;
        backend->grab_frame(frame, timeout_ms);
    }

    // Benchmark: measure frame capture timing
    if (!json_output) {
        std::cout << "Benchmarking (" << num_frames << " frames)...\n";
    }
    std::vector<double> frame_times;
    frame_times.reserve(num_frames);

    auto benchmark_start = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < num_frames; ++i) {
        capture::Frame frame;
        
        auto frame_start = std::chrono::high_resolution_clock::now();
        err = backend->grab_frame(frame, timeout_ms);
        auto frame_end = std::chrono::high_resolution_clock::now();

        if (!err.is_success()) {
            std::cerr << "Failed to grab frame " << i << ": " << err.to_string() << "\n";
            break;
        }

        double frame_time_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
        frame_times.push_back(frame_time_ms);

        if (!json_output && (i + 1) % 50 == 0) {
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

        std::sort(frame_times.begin(), frame_times.end());
    }

    // Shutdown backend
    backend->shutdown();

    double avg_fps = stats.avg_ms > 0.0 ? 1000.0 / stats.avg_ms : 0.0;
    double max_fps = stats.min_ms > 0.0 ? 1000.0 / stats.min_ms : 0.0;
    double min_fps = stats.max_ms > 0.0 ? 1000.0 / stats.max_ms : 0.0;

    if (json_output) {
        std::cout << std::fixed << std::setprecision(3)
                  << "{"
                  << "\"frames\":" << stats.frame_count << ","
                  << "\"total_ms\":" << stats.total_ms << ","
                  << "\"avg_ms\":" << stats.avg_ms << ","
                  << "\"min_ms\":" << stats.min_ms << ","
                  << "\"max_ms\":" << stats.max_ms << ","
                  << "\"avg_fps\":" << avg_fps << ","
                  << "\"peak_fps\":" << max_fps << ","
                  << "\"low_fps\":" << min_fps
                  << "}\n";

        return avg_fps >= 60.0 ? 0 : 1;
    }

    // Report results
    std::cout << "\n========== BENCHMARK RESULTS ==========\n";
    std::cout << "Frames captured: " << stats.frame_count << "\n";
    std::cout << "Total time: " << std::fixed << std::setprecision(2) << (stats.total_ms / 1000.0) << " seconds\n";
    std::cout << "\nPer-Frame Timing:\n";
    std::cout << "  Average:  " << std::fixed << std::setprecision(3) << stats.avg_ms << " ms\n";
    std::cout << "  Min:      " << std::fixed << std::setprecision(3) << stats.min_ms << " ms\n";
    std::cout << "  Max:      " << std::fixed << std::setprecision(3) << stats.max_ms << " ms\n";
    
    // Calculate and report FPS
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
