#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winrt/base.h>
#endif

#include <capture/factory.h>
#include <capture/backend.h>
#include <capture/frame.h>
#include <capture/frame_validation.h>
#include "png_writer.h"

using namespace capture;

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  --backend <windows|macos|auto>   Capture backend (default: auto)\n"
              << "  --monitor <index>                Monitor index (default: 0)\n"
              << "  --region <x,y,w,h>               Capture region relative to the monitor\n"
              << "  --timeout-ms <ms>                Frame timeout (default: 5000)\n"
              << "  --list-targets                   Print target metadata and exit\n"
              << "  --output <path>                  Output file path (default: capture.png)\n"
              << "  --help                           Show this help message\n";
}

bool parse_region(const std::string& value, CaptureRect& out_region) {
    char comma1 = '\0';
    char comma2 = '\0';
    char comma3 = '\0';
    std::istringstream stream(value);
    stream >> out_region.x >> comma1 >> out_region.y >> comma2
           >> out_region.width >> comma3 >> out_region.height;

    return stream && comma1 == ',' && comma2 == ',' && comma3 == ',';
}

void print_targets(const std::vector<CaptureTarget>& targets) {
    for (size_t i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        std::cout << "  [" << i << "] " << target.name;
        if (target.has_bounds) {
            std::cout << " bounds=(" << target.bounds.x << "," << target.bounds.y
                      << " " << target.bounds.width << "x" << target.bounds.height << ")";
        }
        std::cout << "\n";
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
#endif

    BackendType backend_type = BackendType::Auto;
    uint32_t monitor_index = 0;
    std::string output_path = "capture.png";
    CaptureRect region;
    bool has_region = false;
    bool list_targets_only = false;
    int timeout_ms = 5000;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--backend" && i + 1 < argc) {
            std::string backend_str = argv[++i];
            if (backend_str == "windows") {
                backend_type = BackendType::Windows;
            } else if (backend_str == "macos") {
                backend_type = BackendType::MacOS;
            } else if (backend_str == "auto") {
                backend_type = BackendType::Auto;
            } else {
                std::cerr << "Unknown backend: " << backend_str << "\n";
                return 1;
            }
        } else if (arg == "--monitor" && i + 1 < argc) {
            monitor_index = std::stoul(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--region" && i + 1 < argc) {
            if (!parse_region(argv[++i], region)) {
                std::cerr << "Invalid region. Expected x,y,w,h\n";
                return 1;
            }
            has_region = true;
        } else if (arg == "--timeout-ms" && i + 1 < argc) {
            timeout_ms = std::stoi(argv[++i]);
        } else if (arg == "--list-targets") {
            list_targets_only = true;
        } else {
            std::cerr << "Unknown or incomplete option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Create backend
    std::unique_ptr<ICaptureBackend> backend;
    Error err = CaptureFactory::create_backend(backend_type, backend);
    
    if (err.is_error()) {
        std::cerr << "Failed to create backend: " << err.to_string() << "\n";
        return 1;
    }
    
    std::cout << "Backend created successfully\n";
    
    // List available targets
    std::vector<CaptureTarget> targets;
    err = backend->list_targets(targets);
    
    if (err.is_error()) {
        std::cerr << "Failed to list targets: " << err.to_string() << "\n";
        return 1;
    }
    
    std::cout << "Found " << targets.size() << " capture targets\n";
    print_targets(targets);

    if (list_targets_only) {
        return 0;
    }
    
    if (monitor_index >= targets.size()) {
        std::cerr << "Monitor index " << monitor_index << " is out of range\n";
        return 1;
    }

    CaptureTarget selected_target = targets[monitor_index];
    if (has_region) {
        selected_target.region = region;
        selected_target.has_region = true;
    }
    
    // Initialize backend for selected monitor
    err = backend->init(selected_target);
    
    if (err.is_error()) {
        std::cerr << "Failed to initialize backend: " << err.to_string() << "\n";
        return 1;
    }
    
    std::cout << "Backend initialized, capturing from monitor " << monitor_index << "\n";
    std::cout.flush();
    
    // Grab frame
    Frame frame;
    try {
        err = backend->grab_frame(frame, timeout_ms);
    } catch (std::exception const& e) {
        std::cout << "Exception during grab_frame: " << e.what() << "\n";
        backend->shutdown();
        return 1;
    } catch (...) {
        std::cout << "Unknown exception during grab_frame\n";
        backend->shutdown();
        return 1;
    }
    
    if (err.is_error()) {
        std::cout << "Failed to grab frame: " << err.to_string() << "\n";
        backend->shutdown();
        return 1;
    }

    FrameConformanceChecker checker(selected_target);
    err = checker.validate(frame);
    if (err.is_error()) {
        std::cerr << "Captured frame failed conformance checks: "
                  << err.to_string() << "\n";
        backend->shutdown();
        return 1;
    }
    
    std::cout << "Frame captured: " << frame.width << "x" << frame.height
              << " (" << frame.stride_bytes << " bytes per row)\n";
    
    // Write to file
    err = write_frame_to_png(frame, output_path);
    
    if (err.is_error()) {
        std::cerr << "Failed to write file: " << err.to_string() << "\n";
        backend->shutdown();
        return 1;
    }
    
    std::cout << "Frame saved to: " << output_path << "\n";
    
    // Shutdown
    backend->shutdown();
    
    return 0;
}
