#include <capture/backend.h>
#include <capture/factory.h>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winrt/base.h>
#endif

namespace py = pybind11;

namespace {

capture::BackendType parse_backend_type(const std::string& backend) {
    if (backend == "auto") {
        return capture::BackendType::Auto;
    }
    if (backend == "windows") {
        return capture::BackendType::Windows;
    }
    if (backend == "macos") {
        return capture::BackendType::MacOS;
    }

    throw py::value_error("Invalid backend. Expected one of: auto, windows, macos");
}

void throw_if_error(const capture::Error& err, const std::string& context) {
    if (err.is_error()) {
        throw std::runtime_error(context + ": " + err.to_string());
    }
}

py::dict target_to_dict(const capture::CaptureTarget& target) {
    py::dict out;
    out["kind"] = (target.kind == capture::CaptureTarget::Kind::Monitor) ? "monitor" : "window";
    out["id"] = target.id;
    out["name"] = target.name;

    if (target.has_bounds) {
        out["bounds"] = py::make_tuple(
            target.bounds.x,
            target.bounds.y,
            target.bounds.width,
            target.bounds.height);
    } else {
        out["bounds"] = py::none();
    }

    return out;
}

capture::CaptureRect parse_region(const py::object& region_obj) {
    if (region_obj.is_none()) {
        throw py::value_error("Region is None");
    }

    py::sequence seq = region_obj.cast<py::sequence>();
    if (seq.size() != 4) {
        throw py::value_error("Region must have exactly 4 values: (x, y, width, height)");
    }

    capture::CaptureRect region;
    region.x = seq[0].cast<int32_t>();
    region.y = seq[1].cast<int32_t>();

    const int width = seq[2].cast<int>();
    const int height = seq[3].cast<int>();
    if (width <= 0 || height <= 0) {
        throw py::value_error("Region width and height must be positive");
    }

    region.width = static_cast<uint32_t>(width);
    region.height = static_cast<uint32_t>(height);
    return region;
}

py::array_t<uint8_t> frame_to_numpy(const capture::Frame& frame) {
    if (frame.format != capture::PixelFormat::Bgr8Unorm) {
        throw std::runtime_error("Unsupported frame pixel format. Expected Bgr8Unorm.");
    }

    const py::ssize_t height = static_cast<py::ssize_t>(frame.height);
    const py::ssize_t width = static_cast<py::ssize_t>(frame.width);

    py::array_t<uint8_t> out({height, width, static_cast<py::ssize_t>(3)});
    uint8_t* dst = out.mutable_data();

    const size_t row_bytes = static_cast<size_t>(frame.width) * 3;
    const uint8_t* src = frame.bytes.data();

    for (uint32_t y = 0; y < frame.height; ++y) {
        std::memcpy(
            dst + (static_cast<size_t>(y) * row_bytes),
            src + (static_cast<size_t>(y) * frame.stride_bytes),
            row_bytes);
    }

    return out;
}

class PyCaptureSession {
public:
    PyCaptureSession(
        const std::string& backend,
        uint32_t monitor,
        py::object region,
        int timeout_ms)
        : default_timeout_ms_(timeout_ms) {
#ifdef _WIN32
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
#endif

        throw_if_error(
            capture::CaptureFactory::create_backend(parse_backend_type(backend), backend_),
            "Failed to create backend");

        std::vector<capture::CaptureTarget> targets;
        throw_if_error(backend_->list_targets(targets), "Failed to list targets");

        if (monitor >= targets.size()) {
            throw py::index_error("Monitor index out of range");
        }

        target_ = targets[monitor];
        if (!region.is_none()) {
            target_.region = parse_region(region);
            target_.has_region = true;
        }

        throw_if_error(backend_->init(target_), "Failed to initialize backend");
        initialized_ = true;
    }

    ~PyCaptureSession() {
        close();
    }

    py::array_t<uint8_t> grab(py::object timeout_ms_obj = py::none()) {
        const int timeout_ms = timeout_ms_obj.is_none()
            ? default_timeout_ms_
            : timeout_ms_obj.cast<int>();

        capture::Frame frame;
        capture::Error err;
        {
            py::gil_scoped_release release;
            err = backend_->grab_frame(frame, timeout_ms);
        }
        if (err.is_error()) {
            throw std::runtime_error("Failed to grab frame: " + err.to_string());
        }

        return frame_to_numpy(frame);
    }

    py::tuple grab_with_timestamp(py::object timeout_ms_obj = py::none()) {
        const int timeout_ms = timeout_ms_obj.is_none()
            ? default_timeout_ms_
            : timeout_ms_obj.cast<int>();

        capture::Frame frame;
        capture::Error err;
        {
            py::gil_scoped_release release;
            err = backend_->grab_frame(frame, timeout_ms);
        }
        if (err.is_error()) {
            throw std::runtime_error("Failed to grab frame: " + err.to_string());
        }

        return py::make_tuple(frame_to_numpy(frame), frame.timestamp_ns);
    }

    void close() {
        if (initialized_ && backend_) {
            backend_->shutdown();
            initialized_ = false;
        }
    }

private:
    std::unique_ptr<capture::ICaptureBackend> backend_;
    capture::CaptureTarget target_;
    int default_timeout_ms_ = 5000;
    bool initialized_ = false;
};

std::vector<py::dict> list_targets(const std::string& backend) {
#ifdef _WIN32
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
#endif

    std::unique_ptr<capture::ICaptureBackend> capture_backend;
    throw_if_error(
        capture::CaptureFactory::create_backend(parse_backend_type(backend), capture_backend),
        "Failed to create backend");

    std::vector<capture::CaptureTarget> targets;
    throw_if_error(capture_backend->list_targets(targets), "Failed to list targets");

    std::vector<py::dict> out;
    out.reserve(targets.size());
    for (const capture::CaptureTarget& target : targets) {
        out.push_back(target_to_dict(target));
    }
    return out;
}

}  // namespace

PYBIND11_MODULE(capture, m) {
    m.doc() = "Python bindings for capture-library. Frames are returned as NumPy uint8 arrays in BGR order.";

    m.def(
        "list_targets",
        &list_targets,
        py::arg("backend") = "auto",
        "List available capture targets.");

    py::class_<PyCaptureSession>(m, "CaptureSession")
        .def(
            py::init<const std::string&, uint32_t, py::object, int>(),
            py::arg("backend") = "auto",
            py::arg("monitor") = 0,
            py::arg("region") = py::none(),
            py::arg("timeout_ms") = 5000)
        .def("grab", &PyCaptureSession::grab, py::arg("timeout_ms") = py::none())
        .def(
            "grab_with_timestamp",
            &PyCaptureSession::grab_with_timestamp,
            py::arg("timeout_ms") = py::none())
        .def("close", &PyCaptureSession::close)
        .def("__enter__", [](PyCaptureSession& self) -> PyCaptureSession& { return self; })
        .def("__exit__", [](PyCaptureSession& self, py::object, py::object, py::object) {
            self.close();
        });
}
