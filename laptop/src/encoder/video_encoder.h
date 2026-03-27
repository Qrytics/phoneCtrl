#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace phonectrl {
namespace encoder {

/// A single encoded H.264 / H.265 NAL unit or packet.
struct EncodedPacket {
    std::vector<uint8_t> data;
    int64_t pts      = 0;   ///< Presentation timestamp (microseconds)
    bool    keyframe = false;
};

/// Callback invoked for every packet produced by the encoder.
using PacketCallback = std::function<void(const EncodedPacket&)>;

/**
 * VideoEncoder
 *
 * Wraps FFmpeg (libavcodec) to provide software H.264 encoding via libx264.
 * Frames are expected as raw BGRA pixel data (as produced by ScreenCapture).
 *
 * Usage:
 *   VideoEncoder enc;
 *   enc.setPacketCallback([](const EncodedPacket& p){ server.send(p.data); });
 *   enc.init(1920, 1080, 60); // fps
 *   // inside capture callback:
 *   enc.encodeFrame(frame.data.data(), frame.width, frame.height, frame.stride);
 *   enc.flush();
 */
class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    // Non-copyable
    VideoEncoder(const VideoEncoder&)            = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    /**
     * Register a callback that receives encoded packets.
     * Must be called before init().
     */
    void setPacketCallback(PacketCallback cb);

    /**
     * Initialise the FFmpeg codec context.
     *
     * @param width   Frame width in pixels.
     * @param height  Frame height in pixels.
     * @param fps     Target frames-per-second (e.g. 30 or 60).
     * @param bitrate Target bit-rate in bits/s (e.g. 4'000'000 for 4 Mbps).
     * @param codec   Codec name understood by FFmpeg, default "libx264".
     * @returns true on success.
     */
    bool init(int width, int height, int fps = 30,
              int64_t bitrate = 4'000'000,
              const std::string& codec = "libx264");

    /**
     * Encode a single BGRA frame.
     * Internally converts BGRA → YUV420p before passing to the codec.
     *
     * @param bgra   Pointer to BGRA pixel data (top-left origin).
     * @param width  Frame width in pixels.
     * @param height Frame height in pixels.
     * @param stride Bytes per row (may be > width * 4 due to padding).
     */
    void encodeFrame(const uint8_t* bgra, int width, int height, int stride);

    /**
     * Flush remaining frames from the codec.
     * Must be called before destroying the encoder.
     */
    void flush();

    /// Returns true if init() completed successfully.
    bool isInitialised() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace encoder
} // namespace phonectrl
