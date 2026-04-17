#include "backend_macos_screencapturekit.h"
#include "../common/monotonic_time.h"

#include <condition_variable>
#include <cstring>
#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <Foundation/Foundation.h>

#if __has_include(<ScreenCaptureKit/ScreenCaptureKit.h>)
#include <ScreenCaptureKit/ScreenCaptureKit.h>
#define CAPTURE_HAVE_SCREENCAPTUREKIT 1
#else
#define CAPTURE_HAVE_SCREENCAPTUREKIT 0
#endif
#endif

namespace capture {

namespace {

bool is_valid_region(const CaptureRect& region, uint32_t width, uint32_t height) {
    if (region.width == 0 || region.height == 0) {
        return false;
    }
    if (region.x < 0 || region.y < 0) {
        return false;
    }

    const uint64_t x = static_cast<uint64_t>(region.x);
    const uint64_t y = static_cast<uint64_t>(region.y);
    return x + region.width <= width && y + region.height <= height;
}

std::string region_error_message(const CaptureRect& region, uint32_t width, uint32_t height) {
    return "Region (" + std::to_string(region.x) + "," + std::to_string(region.y) +
           " " + std::to_string(region.width) + "x" + std::to_string(region.height) +
           ") is outside target size " + std::to_string(width) + "x" +
           std::to_string(height);
}

#ifdef __APPLE__

std::string display_name(CGDirectDisplayID display_id) {
    std::ostringstream out;
    if (CGDisplayIsMain(display_id)) {
        out << "Main ";
    }
    if (CGDisplayIsBuiltin(display_id)) {
        out << "Built-in ";
    }
    out << "Display " << display_id;
    return out.str();
}

CaptureRect display_bounds(CGDirectDisplayID display_id) {
    const CGRect bounds = CGDisplayBounds(display_id);

    CaptureRect rect;
    rect.x = static_cast<int32_t>(bounds.origin.x);
    rect.y = static_cast<int32_t>(bounds.origin.y);
    rect.width = CGDisplayPixelsWide(display_id);
    rect.height = CGDisplayPixelsHigh(display_id);
    return rect;
}

bool screen_capture_permission_granted() {
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 101500
    return CGPreflightScreenCaptureAccess();
#else
    return true;
#endif
}

std::string ns_error_message(NSError* error) {
    if (!error) {
        return {};
    }

    NSString* description = [error localizedDescription];
    if (!description) {
        description = [error description];
    }
    return description ? std::string([description UTF8String]) : std::string("Unknown NSError");
}

#if CAPTURE_HAVE_SCREENCAPTUREKIT

struct ShareableDisplayQuery {
    std::mutex mutex;
    std::condition_variable cv;
    SCDisplay* display = nil;
    NSError* error = nil;
    bool completed = false;

    ~ShareableDisplayQuery() {
        if (display) {
            [display release];
        }
        if (error) {
            [error release];
        }
    }
};

struct AsyncErrorResult {
    std::mutex mutex;
    std::condition_variable cv;
    NSError* error = nil;
    bool completed = false;

    ~AsyncErrorResult() {
        if (error) {
            [error release];
        }
    }
};

std::string ns_exception_message(NSException* exception) {
    if (!exception) {
        return "Unknown Objective-C exception";
    }

    NSString* reason = [exception reason];
    if (!reason) {
        reason = [exception name];
    }
    return reason ? std::string([reason UTF8String]) : std::string("Unknown Objective-C exception");
}

#endif
#endif

}  // namespace

struct MacOSScreenCaptureKitState {
    std::mutex mutex;
    std::condition_variable frame_cv;
    bool initialized = false;
    bool stream_started = false;
    bool frame_ready = false;
    bool target_lost = false;
    std::string stream_error;
    CaptureTarget current_target;

#if defined(__APPLE__) && CAPTURE_HAVE_SCREENCAPTUREKIT
    SCStream* stream = nil;
    NSObject<SCStreamOutput, SCStreamDelegate>* delegate = nil;
    dispatch_queue_t sample_queue = nullptr;
    CVPixelBufferRef latest_pixel_buffer = nullptr;

    void on_frame(CMSampleBufferRef sample_buffer, SCStreamOutputType output_type) {
        if (output_type != SCStreamOutputTypeScreen || !CMSampleBufferDataIsReady(sample_buffer)) {
            return;
        }

        CVImageBufferRef image_buffer = CMSampleBufferGetImageBuffer(sample_buffer);
        if (!image_buffer) {
            return;
        }

        CVPixelBufferRef pixel_buffer = static_cast<CVPixelBufferRef>(image_buffer);
        CVPixelBufferRetain(pixel_buffer);

        std::lock_guard<std::mutex> lock(mutex);
        if (latest_pixel_buffer) {
            CVPixelBufferRelease(latest_pixel_buffer);
        }
        latest_pixel_buffer = pixel_buffer;
        frame_ready = true;
        frame_cv.notify_one();
    }

    void on_stream_stopped(NSError* error) {
        std::lock_guard<std::mutex> lock(mutex);
        stream_error = ns_error_message(error);
        target_lost = true;
        frame_ready = false;
        if (latest_pixel_buffer) {
            CVPixelBufferRelease(latest_pixel_buffer);
            latest_pixel_buffer = nullptr;
        }
        frame_cv.notify_all();
    }
#endif
};

#if defined(__APPLE__) && CAPTURE_HAVE_SCREENCAPTUREKIT

@interface CaptureLibrarySCStreamDelegate : NSObject <SCStreamOutput, SCStreamDelegate> {
    capture::MacOSScreenCaptureKitState* state_;
}
- (instancetype)initWithState:(capture::MacOSScreenCaptureKitState*)state;
@end

@implementation CaptureLibrarySCStreamDelegate

- (instancetype)initWithState:(capture::MacOSScreenCaptureKitState*)state {
    self = [super init];
    if (self) {
        state_ = state;
    }
    return self;
}

- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type {
    (void)stream;
    if (state_) {
        state_->on_frame(sampleBuffer, type);
    }
}

- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    (void)stream;
    if (state_) {
        state_->on_stream_stopped(error);
    }
}

@end

SCDisplay* find_shareable_display(CGDirectDisplayID display_id, std::string& out_error) {
    auto query = std::make_shared<ShareableDisplayQuery>();

    [SCShareableContent
        getShareableContentExcludingDesktopWindows:NO
                              onScreenWindowsOnly:YES
                                completionHandler:^(SCShareableContent* content, NSError* error) {
                                    {
                                        std::lock_guard<std::mutex> lock(query->mutex);
                                        if (error) {
                                            query->error = [error retain];
                                        } else {
                                            for (SCDisplay* display in [content displays]) {
                                                if ([display displayID] == display_id) {
                                                    query->display = [display retain];
                                                    break;
                                                }
                                            }
                                        }
                                        query->completed = true;
                                    }
                                    query->cv.notify_one();
                                }];

    {
        std::unique_lock<std::mutex> lock(query->mutex);
        if (!query->cv.wait_for(lock,
                                std::chrono::seconds(5),
                                [&query] { return query->completed; })) {
            out_error = "Timed out querying ScreenCaptureKit shareable content";
            return nil;
        }
    }

    if (query->error) {
        out_error = ns_error_message(query->error);
        return nil;
    }

    if (!query->display) {
        out_error = "macOS display target is not available to ScreenCaptureKit";
        return nil;
    }

    SCDisplay* result = [query->display retain];
    return result;
}

Error copy_pixel_buffer_to_frame(
    CVPixelBufferRef pixel_buffer,
    const CaptureTarget& target,
    Frame& out_frame) {

    const OSType pixel_format = CVPixelBufferGetPixelFormatType(pixel_buffer);
    if (pixel_format != kCVPixelFormatType_32BGRA) {
        return Error(ErrorCode::UnsupportedPixelFormat,
                     "Captured macOS frame is not BGRA8 source format");
    }

    const uint32_t source_width = static_cast<uint32_t>(CVPixelBufferGetWidth(pixel_buffer));
    const uint32_t source_height = static_cast<uint32_t>(CVPixelBufferGetHeight(pixel_buffer));

    CaptureRect copy_region;
    copy_region.x = 0;
    copy_region.y = 0;
    copy_region.width = source_width;
    copy_region.height = source_height;

    if (target.has_region) {
        if (!is_valid_region(target.region, source_width, source_height)) {
            return Error(ErrorCode::InvalidRegion,
                         region_error_message(target.region, source_width, source_height));
        }
        copy_region = target.region;
    }

    CVReturn lock_result = CVPixelBufferLockBaseAddress(pixel_buffer, kCVPixelBufferLock_ReadOnly);
    if (lock_result != kCVReturnSuccess) {
        return Error(ErrorCode::BackendFailure, "Failed to lock macOS frame pixel buffer");
    }

    const uint8_t* src = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pixel_buffer));
    const size_t src_stride = CVPixelBufferGetBytesPerRow(pixel_buffer);
    if (!src) {
        CVPixelBufferUnlockBaseAddress(pixel_buffer, kCVPixelBufferLock_ReadOnly);
        return Error(ErrorCode::BackendFailure, "macOS frame pixel buffer has no base address");
    }

    const uint32_t stride = copy_region.width * 3;
    out_frame.width = copy_region.width;
    out_frame.height = copy_region.height;
    out_frame.stride_bytes = stride;
    out_frame.format = PixelFormat::Bgr8Unorm;
    out_frame.timestamp_ns = monotonic_now_ns();
    out_frame.bytes.resize(static_cast<size_t>(stride) * copy_region.height);

    const uint32_t src_x = static_cast<uint32_t>(copy_region.x);
    const uint32_t src_y = static_cast<uint32_t>(copy_region.y);
    for (uint32_t y = 0; y < copy_region.height; ++y) {
        const uint8_t* src_row = src + (src_y + y) * src_stride + src_x * 4;
        uint8_t* dst_row = out_frame.bytes.data() + static_cast<size_t>(y) * stride;
        for (uint32_t x = 0; x < copy_region.width; ++x) {
            dst_row[x * 3 + 0] = src_row[x * 4 + 0];
            dst_row[x * 3 + 1] = src_row[x * 4 + 1];
            dst_row[x * 3 + 2] = src_row[x * 4 + 2];
        }
    }

    CVPixelBufferUnlockBaseAddress(pixel_buffer, kCVPixelBufferLock_ReadOnly);
    return Error(ErrorCode::Success);
}

#endif

MacOSScreenCaptureKitBackend::MacOSScreenCaptureKitBackend()
    : state_(std::make_unique<MacOSScreenCaptureKitState>()) {}

MacOSScreenCaptureKitBackend::~MacOSScreenCaptureKitBackend() {
    shutdown();
}

Error MacOSScreenCaptureKitBackend::list_targets(std::vector<CaptureTarget>& out_targets) {
    out_targets.clear();

#ifdef __APPLE__
    uint32_t display_count = 0;
    CGError err = CGGetActiveDisplayList(0, nullptr, &display_count);
    if (err != kCGErrorSuccess) {
        return Error(ErrorCode::BackendFailure,
                     "Failed to query active macOS display count");
    }

    if (display_count == 0) {
        return Error(ErrorCode::TargetLost, "No active macOS displays found");
    }

    std::vector<CGDirectDisplayID> displays(display_count);
    err = CGGetActiveDisplayList(display_count, displays.data(), &display_count);
    if (err != kCGErrorSuccess) {
        return Error(ErrorCode::BackendFailure,
                     "Failed to enumerate active macOS displays");
    }

    out_targets.reserve(display_count);
    for (uint32_t i = 0; i < display_count; ++i) {
        const CGDirectDisplayID display_id = displays[i];
        CaptureTarget target;
        target.kind = CaptureTarget::Kind::Monitor;
        target.id = static_cast<uint64_t>(display_id);
        target.name = display_name(display_id);
        target.bounds = display_bounds(display_id);
        target.has_bounds = true;
        out_targets.push_back(target);
    }

    return Error(ErrorCode::Success);
#else
    return Error(ErrorCode::NotSupported,
                 "macOS ScreenCaptureKit backend is only available on macOS");
#endif
}

Error MacOSScreenCaptureKitBackend::init(const CaptureTarget& target) {
    shutdown();

    if (target.kind != CaptureTarget::Kind::Monitor) {
        return Error(ErrorCode::NotSupported,
                     "Only monitor capture is supported");
    }

#ifdef __APPLE__
#if !CAPTURE_HAVE_SCREENCAPTUREKIT
    return Error(ErrorCode::NotSupported,
                 "ScreenCaptureKit headers are not available in this macOS SDK");
#else
    if (@available(macOS 12.3, *)) {
    } else {
        return Error(ErrorCode::NotSupported,
                     "ScreenCaptureKit requires macOS 12.3 or newer");
    }

    const auto display_id = static_cast<CGDirectDisplayID>(target.id);
    if (CGDisplayPixelsWide(display_id) == 0 || CGDisplayPixelsHigh(display_id) == 0) {
        return Error(ErrorCode::InvalidMonitorIndex,
                     "macOS display target is no longer active");
    }

    const CaptureRect bounds = display_bounds(display_id);
    if (target.has_region &&
        !is_valid_region(target.region, bounds.width, bounds.height)) {
        return Error(ErrorCode::InvalidRegion,
                     region_error_message(target.region, bounds.width, bounds.height));
    }

    if (!screen_capture_permission_granted()) {
        return Error(ErrorCode::PermissionDenied,
                     "Screen Recording permission is required in System Settings > Privacy & Security > Screen Recording");
    }

    @autoreleasepool {
        std::string display_error;
        SCDisplay* display = find_shareable_display(display_id, display_error);
        if (!display) {
            return Error(ErrorCode::TargetLost, display_error);
        }

        SCContentFilter* filter =
            [[SCContentFilter alloc] initWithDisplay:display
                               excludingApplications:@[]
                                    exceptingWindows:@[]];
        [display release];

        if (!filter) {
            return Error(ErrorCode::InitializationFailed,
                         "Failed to create ScreenCaptureKit content filter");
        }

        SCStreamConfiguration* configuration = [[SCStreamConfiguration alloc] init];
        [configuration setWidth:bounds.width];
        [configuration setHeight:bounds.height];
        [configuration setPixelFormat:kCVPixelFormatType_32BGRA];
        [configuration setMinimumFrameInterval:CMTimeMake(1, 60)];
        [configuration setQueueDepth:3];
        if ([configuration respondsToSelector:@selector(setShowsCursor:)]) {
            [configuration setShowsCursor:NO];
        }
        if ([configuration respondsToSelector:@selector(setCapturesAudio:)]) {
            [configuration setCapturesAudio:NO];
        }
        if ([configuration respondsToSelector:@selector(setExcludesCurrentProcessAudio:)]) {
            [configuration setExcludesCurrentProcessAudio:YES];
        }

        CaptureLibrarySCStreamDelegate* delegate =
            [[CaptureLibrarySCStreamDelegate alloc] initWithState:state_.get()];
        SCStream* stream = [[SCStream alloc] initWithFilter:filter
                                              configuration:configuration
                                                   delegate:delegate];
        [filter release];
        [configuration release];

        if (!stream) {
            [delegate release];
            return Error(ErrorCode::InitializationFailed,
                         "Failed to create ScreenCaptureKit stream");
        }

        dispatch_queue_t queue =
            dispatch_queue_create("capture_library.screencapturekit.frames", DISPATCH_QUEUE_SERIAL);
        if (!queue) {
            [stream release];
            [delegate release];
            return Error(ErrorCode::InitializationFailed,
                         "Failed to create ScreenCaptureKit sample queue");
        }
        NSError* output_error = nil;
        BOOL added = [stream addStreamOutput:delegate
                                        type:SCStreamOutputTypeScreen
                          sampleHandlerQueue:queue
                                       error:&output_error];
        if (!added) {
            std::string message = ns_error_message(output_error);
            [stream release];
            [delegate release];
            if (queue) {
                dispatch_release(queue);
            }
            return Error(ErrorCode::InitializationFailed,
                         "Failed to add ScreenCaptureKit stream output: " + message);
        }

        state_->current_target = target;
        if (!state_->current_target.has_bounds) {
            state_->current_target.bounds = bounds;
            state_->current_target.has_bounds = true;
        }
        state_->stream = stream;
        state_->delegate = delegate;
        state_->sample_queue = queue;
        state_->initialized = true;
    }

    return Error(ErrorCode::Success);
#endif
#else
    return Error(ErrorCode::NotSupported,
                 "macOS ScreenCaptureKit backend is only available on macOS");
#endif
}

Error MacOSScreenCaptureKitBackend::grab_frame(Frame& out_frame, int timeout_ms) {
    if (!state_->initialized) {
        return Error(ErrorCode::BackendFailure, "Backend not initialized");
    }
    if (timeout_ms < 0) {
        return Error(ErrorCode::Timeout, "Frame timeout must be non-negative");
    }

#if defined(__APPLE__) && CAPTURE_HAVE_SCREENCAPTUREKIT
    if (@available(macOS 12.3, *)) {
    } else {
        return Error(ErrorCode::NotSupported,
                     "ScreenCaptureKit requires macOS 12.3 or newer");
    }

    if (!state_->stream_started) {
        auto start_result = std::make_shared<AsyncErrorResult>();

        @try {
            [state_->stream startCaptureWithCompletionHandler:^(NSError* error) {
                {
                    std::lock_guard<std::mutex> lock(start_result->mutex);
                    if (error) {
                        start_result->error = [error retain];
                    }
                    start_result->completed = true;
                }
                start_result->cv.notify_one();
            }];
        } @catch (NSException* exception) {
            return Error(ErrorCode::BackendFailure,
                         "Failed to start ScreenCaptureKit stream: " +
                             ns_exception_message(exception));
        }

        {
            std::unique_lock<std::mutex> lock(start_result->mutex);
            if (!start_result->cv.wait_for(lock,
                                           std::chrono::milliseconds(timeout_ms),
                                           [&start_result] { return start_result->completed; })) {
                return Error(ErrorCode::Timeout,
                             "Timed out starting ScreenCaptureKit stream after " +
                                 std::to_string(timeout_ms) + " ms");
            }
        }

        if (start_result->error) {
            std::string message = ns_error_message(start_result->error);
            return Error(ErrorCode::BackendFailure,
                         "Failed to start ScreenCaptureKit stream: " + message);
        }

        state_->stream_started = true;
    }

    CVPixelBufferRef pixel_buffer = nullptr;
    {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->frame_ready = false;

        const bool got_frame = state_->frame_cv.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [this] { return state_->frame_ready || state_->target_lost; });

        if (state_->target_lost) {
            std::string message = state_->stream_error.empty()
                                      ? "ScreenCaptureKit stream stopped"
                                      : state_->stream_error;
            return Error(ErrorCode::TargetLost, message);
        }

        if (!got_frame) {
            return Error(ErrorCode::Timeout,
                         "Timed out waiting for frame after " +
                             std::to_string(timeout_ms) + " ms");
        }

        pixel_buffer = state_->latest_pixel_buffer;
        state_->latest_pixel_buffer = nullptr;
        state_->frame_ready = false;
    }

    if (!pixel_buffer) {
        return Error(ErrorCode::BackendFailure, "macOS frame pixel buffer is null");
    }

    Error copy_result = copy_pixel_buffer_to_frame(
        pixel_buffer, state_->current_target, out_frame);
    CVPixelBufferRelease(pixel_buffer);
    return copy_result;
#else
    return Error(ErrorCode::NotSupported,
                 "macOS ScreenCaptureKit backend is only available on macOS");
#endif
}

Error MacOSScreenCaptureKitBackend::shutdown() {
#if defined(__APPLE__) && CAPTURE_HAVE_SCREENCAPTUREKIT
    if (state_->stream) {
        if (state_->stream_started) {
            dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
            [state_->stream stopCaptureWithCompletionHandler:^(NSError* error) {
                (void)error;
                dispatch_semaphore_signal(semaphore);
            }];
            dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC));
            dispatch_release(semaphore);
            state_->stream_started = false;
        }

        NSError* remove_error = nil;
        [state_->stream removeStreamOutput:state_->delegate
                                      type:SCStreamOutputTypeScreen
                                     error:&remove_error];
        [state_->stream release];
        state_->stream = nil;
    }

    if (state_->delegate) {
        [state_->delegate release];
        state_->delegate = nil;
    }

    if (state_->sample_queue) {
        dispatch_release(state_->sample_queue);
        state_->sample_queue = nullptr;
    }

    if (state_->latest_pixel_buffer) {
        CVPixelBufferRelease(state_->latest_pixel_buffer);
        state_->latest_pixel_buffer = nullptr;
    }
#endif

    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->frame_ready = false;
        state_->target_lost = false;
        state_->stream_error.clear();
    }
    state_->initialized = false;
    return Error(ErrorCode::Success);
}

}  // namespace capture
