# Capture Library - Next Iterations Plan

## Current Baseline (as of 2026-04-17)

- Windows backend using WGC + D3D11 is implemented and working.
- Release build succeeds for `capture_lib`, `capture_once`, and `benchmark_fps`.
- Performance benchmark reached about 105.8 FPS average on monitor capture over 300 frames.
- Public API now exposes version constants, target metadata, and optional region capture.
- macOS is the required next platform.
- Linux is unsupported and is not part of the required support matrix.

## Guiding Priorities

1. Preserve the narrow portable API and BGRA8 frame contract.
2. Keep capture independent from inference and preprocessing.
3. Prioritize correctness and deterministic behavior before micro-optimizations.
4. Add features only when they are testable and benchmarked.
5. Treat Windows and macOS as the supported platform matrix.

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
- Region capture returns correctly cropped BGRA8 frames with correct stride.
- Error behavior is deterministic for invalid bounds and lost targets.

## Iteration 2 - Windows Reliability and Performance Guardrails

### Objectives

- Ensure reliability under long-running workloads.
- Prevent regressions in throughput and latency.

### Work Items

- Add stress tests for long capture sessions and repeated init/shutdown cycles.
- [x] Add benchmark thresholds and baseline snapshots for single-monitor setups.
- Add benchmark threshold presets for multi-monitor setups.
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

- Implement display enumeration with stable target IDs and names.
- Implement ScreenCaptureKit permission and stream setup flow.
- Convert or normalize captured frames to packed BGRA8 output.
- Preserve the same optional region semantics as the Windows backend.
- Handle permission denial, stream disconnect, target loss, and timeout cases.
- Add macOS build docs and dependency/runtime validation notes.

### Acceptance Criteria

- macOS backend captures monitor frames successfully.
- Frames match the Windows BGRA8/stride/timestamp contract.
- `capture_once --backend macos` and `benchmark_fps --backend macos` work.
- Target listing includes monitor index, name, resolution, and origin coordinates.

## Iteration 4 - Cross-Platform UX and Tooling

### Objectives

- Make the library easier to adopt and verify on Windows and macOS.

### Work Items

- Keep unified CLI options for backend, monitor index, frame count, timeout, and region bounds.
- Improve target listing output where platform APIs expose richer names.
- Add quick-start sections for Windows and macOS with troubleshooting.
- Add a basic CI build matrix for Windows and macOS build verification.

### Acceptance Criteria

- New users can build and run first capture in under 10 minutes.
- CLI behavior is consistent across supported platforms.
- CI catches compile-time regressions across Windows and macOS.

## Iteration 5 - Preprocessing Module for Inference Pipelines

### Objectives

- Add optional preprocessing while preserving capture/core separation.

### Work Items

- Implement conversion pipeline modules:
  - BGRA to RGB/BGR.
  - Resize and letterbox.
  - Normalization.
  - CHW tensor packing.
- Define clear interfaces between capture output and preprocessing input.
- Add performance benchmarks for preprocessing stages.

### Acceptance Criteria

- Preprocessing outputs are validated against reference images or tensors.
- End-to-end sample demonstrates capture to preprocess path.
- Capture performance remains unaffected when preprocessing is disabled.

## Iteration 6 - Quality, Packaging, and Release Readiness

### Objectives

- Prepare for external consumption and repeatable releases.

### Work Items

- Add unit tests for utilities and frame contract invariants.
- Add integration tests for backend lifecycle and frame format checks.
- Add semantic versioning policy and changelog.
- Add CMake install/export support and a usage example project.

### Acceptance Criteria

- Tagged release process is documented and repeatable.
- Consumers can integrate via CMake without copying sources manually.
- Release notes include a platform support matrix and known limitations.

## Cross-Cutting Risks and Mitigations

- API churn risk:
  - Mitigation: freeze core signatures early and add optional extensions instead of breaking changes.
- Platform-specific behavior drift:
  - Mitigation: shared frame conformance tests for Windows and macOS.
- macOS permission/runtime UX friction:
  - Mitigation: explicit diagnostics and remediation hints.

## Suggested Immediate Next Sprint (2-5 days)

1. Start macOS ScreenCaptureKit target enumeration and permission probing.
2. Add longer stress coverage for repeated backend init/shutdown cycles.
3. Add benchmark threshold presets for multi-monitor setups.
