#include <capture/backend.h>
#include <capture/factory.h>
#include <capture/frame.h>
#include <capture/frame_validation.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winrt/base.h>
#endif

namespace {

struct StressOptions {
    capture::BackendType backend_type = capture::BackendType::Auto;
    uint32_t monitor_index = 0;
    uint32_t cycles = 50;
    uint32_t frames_per_cycle = 3;
    int timeout_ms = 3000;
    bool json_output = false;
    bool has_region = false;
    capture::CaptureRect region;
};

struct StressStats {
    uint32_t completed_cycles = 0;
    uint32_t captured_frames = 0;
    uint32_t init_failures = 0;
    uint32_t list_failures = 0;
    uint32_t frame_failures = 0;
    uint32_t invariant_failures = 0;
    uint32_t shutdown_failures = 0;
    double elapsed_ms = 0.0;
    capture::ErrorCode last_error = capture::ErrorCode::Success;
    std::string last_error_message;
};

void print_usage(const char* program_name) {
    std::cout << "capture_stress - Repeated backend lifecycle stress test\n"
              << "Usage: " << program_name << " [options]\n"
              << "  --backend <windows|macos|auto>   Capture backend (default: auto)\n"
              << "  --monitor <index>                Monitor index (default: 0)\n"
              << "  --cycles <count>                 Init/shutdown cycles (default: 50)\n"
              << "  --frames-per-cycle <count>       Frames captured per cycle (default: 3)\n"
              << "  --timeout-ms <ms>                Per-frame timeout (default: 3000)\n"
              << "  --region <x,y,w,h>               Capture region relative to the monitor\n"
              << "  --json                           Print machine-readable metrics\n"
              << "  --help                           Show this message\n";
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

bool parse_region(const std::string& value, capture::CaptureRect& out_region) {
    char comma1 = '\0';
    char comma2 = '\0';
    char comma3 = '\0';
    std::istringstream stream(value);
    stream >> out_region.x >> comma1 >> out_region.y >> comma2
           >> out_region.width >> comma3 >> out_region.height;

    return stream && comma1 == ',' && comma2 == ',' && comma3 == ',';
}

bool parse_options(int argc, char* argv[], StressOptions& options) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            print_usage(argv[0]);
            return false;
        }
        if (arg == "--backend" && i + 1 < argc) {
            if (!parse_backend(argv[++i], options.backend_type)) {
                std::cerr << "Unknown backend: " << argv[i] << "\n";
                return false;
            }
        } else if (arg == "--monitor" && i + 1 < argc) {
            options.monitor_index = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--cycles" && i + 1 < argc) {
            options.cycles = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--frames-per-cycle" && i + 1 < argc) {
            options.frames_per_cycle = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--timeout-ms" && i + 1 < argc) {
            options.timeout_ms = std::stoi(argv[++i]);
        } else if (arg == "--region" && i + 1 < argc) {
            if (!parse_region(argv[++i], options.region)) {
                std::cerr << "Invalid region. Expected x,y,w,h\n";
                return false;
            }
            options.has_region = true;
        } else if (arg == "--json") {
            options.json_output = true;
        } else {
            std::cerr << "Unknown or incomplete option: " << arg << "\n";
            print_usage(argv[0]);
            return false;
        }
    }

    return true;
}

void remember_error(StressStats& stats, const capture::Error& err) {
    stats.last_error = err.code();
    stats.last_error_message = err.to_string();
}

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
#endif

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    StressOptions options;
    if (!parse_options(argc, argv, options)) {
        return 1;
    }

    StressStats stats;
    const auto start = std::chrono::steady_clock::now();

    for (uint32_t cycle = 0; cycle < options.cycles; ++cycle) {
        std::unique_ptr<capture::ICaptureBackend> backend;
        capture::Error err =
            capture::CaptureFactory::create_backend(options.backend_type, backend);
        if (err.is_error()) {
            ++stats.init_failures;
            remember_error(stats, err);
            break;
        }

        std::vector<capture::CaptureTarget> targets;
        err = backend->list_targets(targets);
        if (err.is_error()) {
            ++stats.list_failures;
            remember_error(stats, err);
            break;
        }
        if (options.monitor_index >= targets.size()) {
            ++stats.list_failures;
            stats.last_error = capture::ErrorCode::InvalidMonitorIndex;
            stats.last_error_message = "Monitor index out of range";
            break;
        }

        capture::CaptureTarget target = targets[options.monitor_index];
        if (options.has_region) {
            target.region = options.region;
            target.has_region = true;
        }

        err = backend->init(target);
        if (err.is_error()) {
            ++stats.init_failures;
            remember_error(stats, err);
            break;
        }

        bool cycle_ok = true;
        capture::FrameConformanceChecker checker(target);
        for (uint32_t frame_index = 0; frame_index < options.frames_per_cycle; ++frame_index) {
            capture::Frame frame;
            err = backend->grab_frame(frame, options.timeout_ms);
            if (err.is_error()) {
                ++stats.frame_failures;
                remember_error(stats, err);
                cycle_ok = false;
                break;
            }

            ++stats.captured_frames;
            const capture::Error validation = checker.validate(frame);
            if (validation.is_error()) {
                ++stats.invariant_failures;
                stats.last_error = validation.code();
                stats.last_error_message = validation.to_string();
                cycle_ok = false;
                break;
            }
        }

        err = backend->shutdown();
        if (err.is_error()) {
            ++stats.shutdown_failures;
            remember_error(stats, err);
            break;
        }

        if (!cycle_ok) {
            break;
        }

        ++stats.completed_cycles;
        if (!options.json_output && (stats.completed_cycles % 10 == 0 ||
                                     stats.completed_cycles == options.cycles)) {
            std::cout << "  completed=" << stats.completed_cycles << "/"
                      << options.cycles << " cycles"
                      << " captured=" << stats.captured_frames << "\n";
        }
    }

    const auto end = std::chrono::steady_clock::now();
    stats.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

    const bool pass =
        stats.completed_cycles == options.cycles &&
        stats.captured_frames == options.cycles * options.frames_per_cycle &&
        stats.init_failures == 0 && stats.list_failures == 0 &&
        stats.frame_failures == 0 && stats.invariant_failures == 0 &&
        stats.shutdown_failures == 0;

    if (options.json_output) {
        std::cout << std::fixed << std::setprecision(3)
                  << "{"
                  << "\"requested_cycles\":" << options.cycles << ","
                  << "\"completed_cycles\":" << stats.completed_cycles << ","
                  << "\"frames_per_cycle\":" << options.frames_per_cycle << ","
                  << "\"captured_frames\":" << stats.captured_frames << ","
                  << "\"init_failures\":" << stats.init_failures << ","
                  << "\"list_failures\":" << stats.list_failures << ","
                  << "\"frame_failures\":" << stats.frame_failures << ","
                  << "\"invariant_failures\":" << stats.invariant_failures << ","
                  << "\"shutdown_failures\":" << stats.shutdown_failures << ","
                  << "\"elapsed_ms\":" << stats.elapsed_ms << ","
                  << "\"pass\":" << (pass ? "true" : "false")
                  << "}\n";
    } else {
        std::cout << "\n========== STRESS RESULTS ==========\n"
                  << "Requested cycles:  " << options.cycles << "\n"
                  << "Completed cycles:  " << stats.completed_cycles << "\n"
                  << "Frames per cycle:  " << options.frames_per_cycle << "\n"
                  << "Captured frames:   " << stats.captured_frames << "\n"
                  << "Init failures:     " << stats.init_failures << "\n"
                  << "List failures:     " << stats.list_failures << "\n"
                  << "Frame failures:    " << stats.frame_failures << "\n"
                  << "Invariant failures:" << stats.invariant_failures << "\n"
                  << "Shutdown failures: " << stats.shutdown_failures << "\n"
                  << "Elapsed:           " << std::fixed << std::setprecision(2)
                  << (stats.elapsed_ms / 1000.0) << " seconds\n";

        if (!stats.last_error_message.empty()) {
            std::cout << "Last error:        " << stats.last_error_message << "\n";
        }
        std::cout << (pass ? "[PASS]" : "[FAIL]") << "\n";
    }

    return pass ? 0 : 1;
}
