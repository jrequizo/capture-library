#include <capture/backend.h>
#include <capture/factory.h>
#include <capture/frame.h>
#include <capture/frame_validation.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winrt/base.h>
#endif

namespace {

struct LoopOptions {
    capture::BackendType backend_type = capture::BackendType::Auto;
    uint32_t monitor_index = 0;
    uint32_t frames = 600;
    int timeout_ms = 3000;
    uint32_t report_every = 60;
    bool json_output = false;
    bool has_region = false;
    capture::CaptureRect region;
};

struct LoopStats {
    uint32_t captured = 0;
    uint32_t timeouts = 0;
    uint32_t frame_errors = 0;
    uint32_t invariant_errors = 0;
    double total_grab_ms = 0.0;
    double min_grab_ms = 1e9;
    double max_grab_ms = 0.0;
    capture::ErrorCode last_error = capture::ErrorCode::Success;
    std::string last_error_message;
};

void print_usage(const char* program_name) {
    std::cout << "capture_loop - Sustained display capture smoke test\n"
              << "Usage: " << program_name << " [options]\n"
              << "  --backend <windows|macos|auto>   Capture backend (default: auto)\n"
              << "  --monitor <index>                Monitor index (default: 0)\n"
              << "  --frames <count>                 Number of frames to capture (default: 600)\n"
              << "  --timeout-ms <ms>                Per-frame timeout (default: 3000)\n"
              << "  --region <x,y,w,h>               Capture region relative to the monitor\n"
              << "  --report-every <count>           Progress interval (default: 60, 0 disables)\n"
              << "  --json                           Print machine-readable metrics\n"
              << "  --help                           Show this message\n";
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

bool parse_options(int argc, char* argv[], LoopOptions& options) {
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
        } else if (arg == "--frames" && i + 1 < argc) {
            options.frames = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--timeout-ms" && i + 1 < argc) {
            options.timeout_ms = std::stoi(argv[++i]);
        } else if (arg == "--region" && i + 1 < argc) {
            if (!parse_region(argv[++i], options.region)) {
                std::cerr << "Invalid region. Expected x,y,w,h\n";
                return false;
            }
            options.has_region = true;
        } else if (arg == "--report-every" && i + 1 < argc) {
            options.report_every = static_cast<uint32_t>(std::stoul(argv[++i]));
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

    LoopOptions options;
    if (!parse_options(argc, argv, options)) {
        return 1;
    }

    std::unique_ptr<capture::ICaptureBackend> backend;
    auto err = capture::CaptureFactory::create_backend(options.backend_type, backend);
    if (err.is_error()) {
        std::cerr << "Failed to create backend: " << err.to_string() << "\n";
        return 1;
    }

    std::vector<capture::CaptureTarget> targets;
    err = backend->list_targets(targets);
    if (err.is_error()) {
        std::cerr << "Failed to list targets: " << err.to_string() << "\n";
        return 1;
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

    err = backend->init(selected_target);
    if (err.is_error()) {
        std::cerr << "Failed to initialize backend: " << err.to_string() << "\n";
        return 1;
    }

    if (!options.json_output) {
        std::cout << "Capturing " << options.frames << " frames from monitor "
                  << options.monitor_index << "\n";
    }

    LoopStats stats;
    capture::FrameConformanceChecker checker(selected_target);
    const auto loop_start = std::chrono::steady_clock::now();

    for (uint32_t i = 0; i < options.frames; ++i) {
        capture::Frame frame;
        const auto grab_start = std::chrono::steady_clock::now();
        err = backend->grab_frame(frame, options.timeout_ms);
        const auto grab_end = std::chrono::steady_clock::now();
        const double grab_ms =
            std::chrono::duration<double, std::milli>(grab_end - grab_start).count();

        if (err.is_success()) {
            ++stats.captured;
            stats.total_grab_ms += grab_ms;
            stats.min_grab_ms = std::min(stats.min_grab_ms, grab_ms);
            stats.max_grab_ms = std::max(stats.max_grab_ms, grab_ms);

            const capture::Error validation = checker.validate(frame);
            if (validation.is_error()) {
                ++stats.invariant_errors;
                stats.last_error = validation.code();
                stats.last_error_message = validation.to_string();
            }
        } else if (err.code() == capture::ErrorCode::Timeout) {
            ++stats.timeouts;
            stats.last_error = err.code();
            stats.last_error_message = err.to_string();
        } else {
            ++stats.frame_errors;
            stats.last_error = err.code();
            stats.last_error_message = err.to_string();
            if (err.code() == capture::ErrorCode::TargetLost) {
                break;
            }
        }

        if (!options.json_output && options.report_every > 0 &&
            (i + 1) % options.report_every == 0) {
            std::cout << "  attempted=" << (i + 1)
                      << " captured=" << stats.captured
                      << " timeouts=" << stats.timeouts
                      << " errors=" << stats.frame_errors
                      << " invariant_errors=" << stats.invariant_errors << "\n";
        }
    }

    const auto loop_end = std::chrono::steady_clock::now();
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(loop_end - loop_start).count();

    backend->shutdown();

    const double avg_grab_ms =
        stats.captured > 0 ? stats.total_grab_ms / stats.captured : 0.0;
    const double capture_fps =
        elapsed_ms > 0.0 ? static_cast<double>(stats.captured) * 1000.0 / elapsed_ms : 0.0;
    const bool pass =
        stats.captured == options.frames && stats.timeouts == 0 &&
        stats.frame_errors == 0 && stats.invariant_errors == 0;

    if (options.json_output) {
        std::cout << std::fixed << std::setprecision(3)
                  << "{"
                  << "\"requested_frames\":" << options.frames << ","
                  << "\"captured_frames\":" << stats.captured << ","
                  << "\"timeouts\":" << stats.timeouts << ","
                  << "\"frame_errors\":" << stats.frame_errors << ","
                  << "\"invariant_errors\":" << stats.invariant_errors << ","
                  << "\"elapsed_ms\":" << elapsed_ms << ","
                  << "\"avg_grab_ms\":" << avg_grab_ms << ","
                  << "\"min_grab_ms\":"
                  << (stats.captured > 0 ? stats.min_grab_ms : 0.0) << ","
                  << "\"max_grab_ms\":" << stats.max_grab_ms << ","
                  << "\"capture_fps\":" << capture_fps << ","
                  << "\"pass\":" << (pass ? "true" : "false")
                  << "}\n";
    } else {
        std::cout << "\n========== LOOP RESULTS ==========\n"
                  << "Requested frames: " << options.frames << "\n"
                  << "Captured frames:  " << stats.captured << "\n"
                  << "Timeouts:         " << stats.timeouts << "\n"
                  << "Frame errors:     " << stats.frame_errors << "\n"
                  << "Invariant errors: " << stats.invariant_errors << "\n"
                  << "Elapsed:          " << std::fixed << std::setprecision(2)
                  << (elapsed_ms / 1000.0) << " seconds\n"
                  << "Capture FPS:      " << std::fixed << std::setprecision(1)
                  << capture_fps << "\n"
                  << "Average grab:     " << std::fixed << std::setprecision(3)
                  << avg_grab_ms << " ms\n";

        if (!stats.last_error_message.empty()) {
            std::cout << "Last error:       " << stats.last_error_message << "\n";
        }
        std::cout << (pass ? "[PASS]" : "[FAIL]") << "\n";
    }

    return pass ? 0 : 1;
}
