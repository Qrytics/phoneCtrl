#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Windows headers required for DXGI Desktop Duplication API
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

namespace phonectrl {
namespace capture {

/// Raw BGRA frame data captured from the desktop.
struct Frame {
    std::vector<uint8_t> data; ///< Pixel data (BGRA, row-major)
    int width  = 0;
    int height = 0;
    int stride = 0; ///< Bytes per row (may include padding)
};

/// Callback type invoked each time a new frame is captured.
using FrameCallback = std::function<void(const Frame&)>;

/**
 * ScreenCapture
 *
 * Uses the DXGI Desktop Duplication API to capture the primary monitor at
 * up to 60 FPS with minimal CPU overhead (GPU-backed).
 *
 * Usage:
 *   ScreenCapture cap;
 *   cap.setFrameCallback([](const Frame& f){ /* encode f */ });
 *   cap.start();
 *   // ... run event loop ...
 *   cap.stop();
 */
class ScreenCapture {
public:
    ScreenCapture();
    ~ScreenCapture();

    // Non-copyable
    ScreenCapture(const ScreenCapture&)            = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    /**
     * Register a callback that will be called on every captured frame.
     * Must be called before start().
     */
    void setFrameCallback(FrameCallback cb);

    /**
     * Initialize DXGI resources and start capturing on a background thread.
     * @param monitorIndex  0-based index of the monitor to capture (default: 0).
     * @returns true on success, false if DXGI initialisation failed.
     */
    bool start(int monitorIndex = 0);

    /**
     * Stop the capture thread and release DXGI resources.
     */
    void stop();

    /// Returns true if the capture loop is running.
    bool isRunning() const;

    /// Returns the current capture width in pixels.
    int width()  const;

    /// Returns the current capture height in pixels.
    int height() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace capture
} // namespace phonectrl
