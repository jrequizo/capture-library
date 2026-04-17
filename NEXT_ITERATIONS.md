# Capture Library - Next Iterations Plan

## Current Baseline (as of 2026-04-17)

- Windows backend using WGC + D3D11 is implemented and working.
- Release build succeeds for `capture_lib`, `capture_once`, `capture_loop`, `capture_stress`, and `benchmark_fps`.
- Performance benchmark reached about 105.8 FPS average on monitor capture over 300 frames.
- Public API now exposes version constants, target metadata, optional region capture, and packed BGR8 frames for OpenCV pipelines.
- Python integration is not implemented yet and is the top priority for the next iteration.
- macOS is the required next platform.
- Linux is unsupported and is not part of the required support matrix.

## Guiding Priorities

1. Preserve the narrow portable API and BGR8 frame contract.
2. Make the library directly usable from Python CV pipelines before adding deeper preprocessing features.
3. Keep capture independent from inference and preprocessing.
4. Prioritize correctness and deterministic behavior before micro-optimizations.
5. Add features only when they are testable and benchmarked.
6. Treat Windows and macOS as the supported platform matrix.

## Iteration 1 - API Hardening and Windows Feature Completeness

### Objectives

- Stabilize public API contracts.
- Add missing high-value capture features for Windows users.

### Work Items

- [x] Define explicit API versioning and compatibility notes in headers and README.
- [x] Add region/ROI capture support through optional `CaptureTarget::region`.
- [x] Add explicit target metadata for monitor bounds and names.
- [x] Improve timeout and error semantics for frame acquisition and target loss.
- [x] Add a small capture loop sample for sustained capture usage.

### Acceptance Criteria

- Public API changes are documented and reviewed for backward compatibility.
- Region capture returns correctly cropped BGR8 frames with correct stride.
- Error behavior is deterministic for invalid bounds and lost targets.

## Iteration 2 - Windows Reliability and Performance Guardrails

### Objectives

- Ensure reliability under long-running workloads.
- Prevent regressions in throughput and latency.

### Work Items

- [x] Add stress tests for long capture sessions and repeated init/shutdown cycles.
- [x] Add benchmark thresholds and baseline snapshots for single-monitor setups.
- [x] Add benchmark threshold presets for multi-monitor setups.
- Reduce overhead in hot paths:
  - Avoid redundant start/stop checks in per-frame paths.
  - Reuse staging textures and verify no unnecessary reallocations.
- Validate behavior on Windows 10 and Windows 11 across common refresh rates.

### Acceptance Criteria

- No leaks or crashes in sustained capture stress tests.
- Stable FPS over long runs with no major frame-time spikes from API misuse.
- Benchmark command outputs structured metrics suitable for CI artifact storage.

## Iteration 3 - macOS ScreenCaptureKit Backend

### Objectives

- Implement the required production macOS backend.

### Work Items

- [x] Implement display enumeration with stable target IDs and names.
- [x] Implement macOS Screen Recording permission preflight.
- [x] Implement ScreenCaptureKit stream setup flow.
- [x] Convert or normalize captured frames to packed BGR8 output.
- [x] Preserve the same optional region semantics as the Windows backend.
- [x] Handle permission denial, stream disconnect, target loss, and timeout cases.
- [x] Add macOS build docs and dependency/runtime validation notes.

### Acceptance Criteria

- macOS backend captures monitor frames successfully.
- Frames match the Windows BGR8/stride/timestamp contract.
- `capture_once --backend macos` and `benchmark_fps --backend macos` work.
- Target listing includes monitor index, name, resolution, and origin coordinates.

## Iteration 4 - Cross-Platform UX and Tooling

### Objectives

- Make the library easier to adopt and verify on Windows and macOS.

### Work Items

- [x] Keep unified CLI options for backend, monitor index, frame count, timeout, and region bounds.
- [x] Improve target listing output where platform APIs expose richer names.
- [x] Add quick-start sections for Windows and macOS with troubleshooting.
- [x] Add a basic CI build matrix for Windows and macOS build verification.
- [x] Add shared frame conformance checks to the capture validation tools.

### Acceptance Criteria

- New users can build and run first capture in under 10 minutes.
- CLI behavior is consistent across supported platforms.
- CI catches compile-time regressions across Windows and macOS.

## Iteration 5 - Python CV Pipeline Integration

### Objectives

- Make monitor capture usable from Python with OpenCV and NumPy as the next top priority.
- Preserve the C++ capture core and BGR8 frame contract while exposing a Python-friendly API.
- Ship a minimal path that can be used in a real Python CV loop before implementing optional preprocessing.

### Work Items

- Add a Python extension module using `pybind11` and CMake.
- Add Python packaging with `pyproject.toml` and a CMake-backed build flow.
- Expose target listing, backend selection, monitor selection, optional region capture, timeout control, and explicit shutdown.
- Return captured frames as `numpy.ndarray` with shape `(height, width, 3)`, dtype `uint8`, and BGR channel order.
- Map `capture::ErrorCode` values to Python exceptions for permission denial, timeout, target loss, invalid region, unsupported backend, and generic backend failures.
- Add Python examples:
  - OpenCV display loop.
  - Single-frame capture to PNG through `cv2.imwrite`.
  - YOLO-style inference handoff with optional BGR-to-RGB conversion in Python.
- Add Python smoke tests for import, target listing, frame shape/stride/channel contract, region dimensions, and timeout error behavior where possible.
- Add Windows and macOS CI build coverage for the Python extension.
- Document install/build instructions and platform runtime notes for Python users.

### Acceptance Criteria

- `pip install .` or an equivalent documented local build produces an importable Python module on Windows and macOS.
- Python can list targets and capture a monitor frame into a NumPy `uint8` BGR array without extra copies beyond the first safe implementation copy.
- A minimal OpenCV loop works with `cv2.imshow` and a YOLO-style example can pass frames to an inference pipeline.
- Python exceptions are deterministic and preserve the underlying native error category and message.
- Existing C++ CLI tools and benchmarks continue to build and pass.

## Iteration 6 - Preprocessing Module for Inference Pipelines

### Objectives

- Add optional preprocessing while preserving capture/core separation.
- Keep preprocessing optional; Python capture usability is the prerequisite and higher priority.

### Work Items

- Implement conversion pipeline modules:
  - BGR to RGB.
  - Resize and letterbox.
  - Normalization.
  - CHW tensor packing.
- Define clear interfaces between capture output and preprocessing input.
- Add performance benchmarks for preprocessing stages.

### Acceptance Criteria

- Preprocessing outputs are validated against reference images or tensors.
- End-to-end sample demonstrates capture to preprocess path.
- Capture performance remains unaffected when preprocessing is disabled.

## Iteration 7 - Quality, Packaging, and Release Readiness

### Objectives

- Prepare for external consumption and repeatable releases.

### Work Items

- Add unit tests for utilities and frame contract invariants.
- Add integration tests for backend lifecycle and frame format checks.
- Add semantic versioning policy and changelog.
- Add CMake install/export support and a usage example project.
- Add Python wheel/release packaging once the binding API stabilizes.

### Acceptance Criteria

- Tagged release process is documented and repeatable.
- Consumers can integrate via CMake without copying sources manually.
- Release notes include a platform support matrix and known limitations.

## Cross-Cutting Risks and Mitigations

- Python buffer lifetime risk:
  - Mitigation: start with safe copied NumPy arrays, then add optional lower-copy paths only after lifetime tests exist.
- API churn risk:
  - Mitigation: freeze core signatures early and add optional extensions instead of breaking changes.
- Platform-specific behavior drift:
  - Mitigation: shared frame conformance tests for Windows and macOS.
- macOS permission/runtime UX friction:
  - Mitigation: explicit diagnostics and remediation hints.

## Suggested Immediate Next Sprint (2-5 days)

1. Add the first `pybind11` Python module with `list_targets()` and a `CaptureSession.grab()` method returning a copied NumPy BGR `uint8` array.
2. Add `pyproject.toml` and documented local install instructions for Windows and macOS.
3. Add Python OpenCV examples for target listing, single-frame save, and display-loop capture.
4. Add Python smoke tests for import, target metadata, frame shape, dtype, region dimensions, and native error-to-exception mapping.
5. Validate macOS ScreenCaptureKit capture on Apple Silicon and Intel Macs, including permission denial and display disconnect cases.
