#include <capture/factory.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
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
    double p50_ms = 0;
    double p95_ms = 0;
    double p99_ms = 0;
    uint32_t frame_count = 0;
};

struct BenchmarkOptions {
    capture::BackendType backend_type = capture::BackendType::Auto;
    uint32_t monitor_index = 0;
    uint32_t num_frames = 300;
    int timeout_ms = 3000;
    bool json_output = false;
    bool has_region = false;
    capture::CaptureRect region;
    double min_avg_fps = 60.0;
    double max_avg_ms = 0.0;
    double max_frame_ms = 0.0;
    std::string artifact_path;
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

bool parse_backend(const std::string& value, capture::BackendType& out_backend) {
    if (value == "windows") {
        out_backend = capture::BackendType::Windows;
        return true;
    }
    if (value == "macos") {
        out_backend = capture::BackendType::MacOS;
        return true;
    }
    if (value == "auto") {
        out_backend = capture::BackendType::Auto;
        return true;
    }
    return false;
}

void print_usage() {
    std::cout << "benchmark_fps - Measure display capture FPS\n"
              << "Usage: benchmark_fps [options]\n"
              << "  --backend NAME          Backend: windows, macos, auto (default: auto)\n"
              << "  --monitor INDEX         Capture from monitor INDEX (default: 0)\n"
              << "  --frames COUNT          Number of frames to capture (default: 300)\n"
              << "  --timeout-ms MS         Frame timeout (default: 3000)\n"
              << "  --region X,Y,W,H        Capture region relative to the monitor\n"
              << "  --min-avg-fps FPS       Minimum average FPS threshold (default: 60)\n"
              << "  --max-avg-ms MS         Optional maximum average frame time threshold\n"
              << "  --max-frame-ms MS       Optional maximum single-frame time threshold\n"
              << "  --artifact PATH         Write JSON benchmark artifact to PATH\n"
              << "  --json                  Print machine-readable metrics\n"
              << "  --help                  Show this message\n";
}

bool parse_options(int argc, char* argv[], BenchmarkOptions& options) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--backend" && i + 1 < argc) {
            if (!parse_backend(argv[++i], options.backend_type)) {
                std::cerr << "Unknown backend: " << argv[i] << "\n";
                return false;
            }
        } else if (arg == "--monitor" && i + 1 < argc) {
            options.monitor_index = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--frames" && i + 1 < argc) {
            options.num_frames = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--timeout-ms" && i + 1 < argc) {
            options.timeout_ms = std::stoi(argv[++i]);
        } else if (arg == "--region" && i + 1 < argc) {
            if (!parse_region(argv[++i], options.region)) {
                std::cerr << "Invalid region. Expected x,y,w,h\n";
                return false;
            }
            options.has_region = true;
        } else if (arg == "--min-avg-fps" && i + 1 < argc) {
            options.min_avg_fps = std::stod(argv[++i]);
        } else if (arg == "--max-avg-ms" && i + 1 < argc) {
            options.max_avg_ms = std::stod(argv[++i]);
        } else if (arg == "--max-frame-ms" && i + 1 < argc) {
            options.max_frame_ms = std::stod(argv[++i]);
        } else if (arg == "--artifact" && i + 1 < argc) {
            options.artifact_path = argv[++i];
        } else if (arg == "--json") {
            options.json_output = true;
        } else if (arg == "--help") {
            print_usage();
            return false;
        } else {
            std::cerr << "Unknown or incomplete option: " << arg << "\n";
            return false;
        }
    }

    return true;
}

double percentile(const std::vector<double>& sorted_values, double fraction) {
    if (sorted_values.empty()) {
        return 0.0;
    }

    const double clamped = std::max(0.0, std::min(1.0, fraction));
    const size_t index = static_cast<size_t>(
        std::ceil(clamped * static_cast<double>(sorted_values.size())) - 1.0);
    return sorted_values[std::min(index, sorted_values.size() - 1)];
}

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (char c : value) {
        switch (c) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << c;
                break;
        }
    }
    return out.str();
}

std::string benchmark_json(
    const BenchmarkOptions& options,
    const capture::CaptureTarget& target,
    const BenchmarkStats& stats,
    double avg_fps,
    double peak_fps,
    double low_fps,
    bool passed) {

    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << "{"
        << "\"frames\":" << stats.frame_count << ","
        << "\"requested_frames\":" << options.num_frames << ","
        << "\"monitor_index\":" << options.monitor_index << ","
        << "\"target_name\":\"" << json_escape(target.name) << "\",";

    if (target.has_bounds) {
        out << "\"target_bounds\":{"
            << "\"x\":" << target.bounds.x << ","
            << "\"y\":" << target.bounds.y << ","
            << "\"width\":" << target.bounds.width << ","
            << "\"height\":" << target.bounds.height
            << "},";
    }

    if (options.has_region) {
        out << "\"region\":{"
            << "\"x\":" << options.region.x << ","
            << "\"y\":" << options.region.y << ","
            << "\"width\":" << options.region.width << ","
            << "\"height\":" << options.region.height
            << "},";
    }

    out << "\"total_ms\":" << stats.total_ms << ","
        << "\"avg_ms\":" << stats.avg_ms << ","
        << "\"min_ms\":" << stats.min_ms << ","
        << "\"max_ms\":" << stats.max_ms << ","
        << "\"p50_ms\":" << stats.p50_ms << ","
        << "\"p95_ms\":" << stats.p95_ms << ","
        << "\"p99_ms\":" << stats.p99_ms << ","
        << "\"avg_fps\":" << avg_fps << ","
        << "\"peak_fps\":" << peak_fps << ","
        << "\"low_fps\":" << low_fps << ","
        << "\"thresholds\":{"
        << "\"min_avg_fps\":" << options.min_avg_fps << ","
        << "\"max_avg_ms\":" << options.max_avg_ms << ","
        << "\"max_frame_ms\":" << options.max_frame_ms
        << "},"
        << "\"pass\":" << (passed ? "true" : "false")
        << "}";

    return out.str();
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
#endif

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--help") {
            print_usage();
            return 0;
        }
    }

    BenchmarkOptions options;
    if (!parse_options(argc, argv, options)) {
        return 1;
    }

    // Create backend
    std::unique_ptr<capture::ICaptureBackend> backend;
    auto err = capture::CaptureFactory::create_backend(options.backend_type, backend);
    if (!err.is_success()) {
        std::cerr << "Failed to create backend: " << err.to_string() << "\n";
        return 1;
    }
    if (!options.json_output) {
        std::cout << "Backend created successfully\n";
    }

    // List available targets
    std::vector<capture::CaptureTarget> targets;
    err = backend->list_targets(targets);
    if (!err.is_success()) {
        std::cerr << "Failed to list targets: " << err.to_string() << "\n";
        return 1;
    }
    if (!options.json_output) {
        std::cout << "Found " << targets.size() << " capture targets\n";
    }

    if (options.monitor_index >= targets.size()) {
        std::cerr << "Monitor index " << options.monitor_index << " out of range\n";
        return 1;
    }

    capture::CaptureTarget selected_target = targets[options.monitor_index];
    if (options.has_region) {
        selected_target.region = options.region;
        selected_target.has_region = true;
    }

    // Initialize backend for target
    err = backend->init(selected_target);
    if (!err.is_success()) {
        std::cerr << "Failed to initialize backend: " << err.to_string() << "\n";
        return 1;
    }
    if (!options.json_output) {
        std::cout << "Backend initialized for monitor " << options.monitor_index << "\n";
    }

    // Warmup: capture 5 frames to stabilize
    if (!options.json_output) {
        std::cout << "\nWarmup (5 frames)...\n";
    }
    for (int i = 0; i < 5; ++i) {
        capture::Frame frame;
        backend->grab_frame(frame, options.timeout_ms);
    }

    // Benchmark: measure frame capture timing
    if (!options.json_output) {
        std::cout << "Benchmarking (" << options.num_frames << " frames)...\n";
    }
    std::vector<double> frame_times;
    frame_times.reserve(options.num_frames);

    auto benchmark_start = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < options.num_frames; ++i) {
        capture::Frame frame;
        
        auto frame_start = std::chrono::high_resolution_clock::now();
        err = backend->grab_frame(frame, options.timeout_ms);
        auto frame_end = std::chrono::high_resolution_clock::now();

        if (!err.is_success()) {
            std::cerr << "Failed to grab frame " << i << ": " << err.to_string() << "\n";
            break;
        }

        double frame_time_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
        frame_times.push_back(frame_time_ms);

        if (!options.json_output && (i + 1) % 50 == 0) {
            std::cout << "  Captured " << (i + 1) << "/" << options.num_frames << " frames\n";
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
        stats.p50_ms = percentile(frame_times, 0.50);
        stats.p95_ms = percentile(frame_times, 0.95);
        stats.p99_ms = percentile(frame_times, 0.99);
    }

    // Shutdown backend
    backend->shutdown();

    double avg_fps = stats.avg_ms > 0.0 ? 1000.0 / stats.avg_ms : 0.0;
    double max_fps = stats.min_ms > 0.0 ? 1000.0 / stats.min_ms : 0.0;
    double min_fps = stats.max_ms > 0.0 ? 1000.0 / stats.max_ms : 0.0;
    bool passed = stats.frame_count == options.num_frames && avg_fps >= options.min_avg_fps;
    if (options.max_avg_ms > 0.0 && stats.avg_ms > options.max_avg_ms) {
        passed = false;
    }
    if (options.max_frame_ms > 0.0 && stats.max_ms > options.max_frame_ms) {
        passed = false;
    }

    std::string artifact = benchmark_json(
        options, selected_target, stats, avg_fps, max_fps, min_fps, passed);

    if (!options.artifact_path.empty()) {
        std::ofstream file(options.artifact_path, std::ios::out | std::ios::trunc);
        if (!file) {
            std::cerr << "Failed to open artifact path: " << options.artifact_path << "\n";
            return 1;
        }
        file << artifact << "\n";
    }

    if (options.json_output) {
        std::cout << artifact << "\n";
        return passed ? 0 : 1;
    }

    // Report results
    std::cout << "\n========== BENCHMARK RESULTS ==========\n";
    std::cout << "Frames captured: " << stats.frame_count << "\n";
    std::cout << "Total time: " << std::fixed << std::setprecision(2) << (stats.total_ms / 1000.0) << " seconds\n";
    std::cout << "\nPer-Frame Timing:\n";
    std::cout << "  Average:  " << std::fixed << std::setprecision(3) << stats.avg_ms << " ms\n";
    std::cout << "  Min:      " << std::fixed << std::setprecision(3) << stats.min_ms << " ms\n";
    std::cout << "  Max:      " << std::fixed << std::setprecision(3) << stats.max_ms << " ms\n";
    std::cout << "  P50:      " << std::fixed << std::setprecision(3) << stats.p50_ms << " ms\n";
    std::cout << "  P95:      " << std::fixed << std::setprecision(3) << stats.p95_ms << " ms\n";
    std::cout << "  P99:      " << std::fixed << std::setprecision(3) << stats.p99_ms << " ms\n";
    
    // Calculate and report FPS
    std::cout << "\nFrames Per Second:\n";
    std::cout << "  Avg FPS:  " << std::fixed << std::setprecision(1) << avg_fps << " fps\n";
    std::cout << "  Peak FPS: " << std::fixed << std::setprecision(1) << max_fps << " fps\n";
    std::cout << "  Low FPS:  " << std::fixed << std::setprecision(1) << min_fps << " fps\n";
    
    // Validate target
    std::cout << "\n========== VALIDATION ==========\n";
    std::cout << "Frames target: " << stats.frame_count << "/" << options.num_frames << "\n";
    std::cout << "Min avg FPS:   " << std::fixed << std::setprecision(1)
              << avg_fps << " / " << options.min_avg_fps << "\n";
    if (options.max_avg_ms > 0.0) {
        std::cout << "Max avg ms:    " << std::fixed << std::setprecision(3)
                  << stats.avg_ms << " / " << options.max_avg_ms << "\n";
    }
    if (options.max_frame_ms > 0.0) {
        std::cout << "Max frame ms:  " << std::fixed << std::setprecision(3)
                  << stats.max_ms << " / " << options.max_frame_ms << "\n";
    }
    if (!options.artifact_path.empty()) {
        std::cout << "Artifact:      " << options.artifact_path << "\n";
    }

    if (passed) {
        std::cout << "[PASS]\n";
        return 0;
    } else {
        std::cout << "[FAIL]\n";
        return 1;
    }
}
