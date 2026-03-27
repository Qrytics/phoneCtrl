#include "video_encoder.h"

#include <stdexcept>
#include <cstring>
#include <array>
#include <algorithm>

// FFmpeg C headers
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace phonectrl {
namespace encoder {

// ─── Private implementation ───────────────────────────────────────────────────

struct VideoEncoder::Impl {
    PacketCallback  callback;
    bool            initialised = false;
    int64_t         frameIndex  = 0;
    std::string     activeCodecName;
    std::string     lastError;

    // FFmpeg objects
    const AVCodec*  codec       = nullptr;
    AVCodecContext* codecCtx    = nullptr;
    AVFrame*        yuvFrame    = nullptr;
    AVPacket*       avPacket    = nullptr;
    SwsContext*     swsCtx      = nullptr;

    // Source frame dimensions (may differ from codec dimensions during resize)
    int srcWidth  = 0;
    int srcHeight = 0;

    void cleanup();
    void drainPackets();
};

static std::string ffErr(int err)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buf{};
    av_strerror(err, buf.data(), buf.size());
    return std::string(buf.data());
}

// ─── Cleanup ─────────────────────────────────────────────────────────────────

void VideoEncoder::Impl::cleanup()
{
    if (swsCtx)    { sws_freeContext(swsCtx);    swsCtx    = nullptr; }
    if (avPacket)  { av_packet_free(&avPacket);  avPacket  = nullptr; }
    if (yuvFrame)  { av_frame_free(&yuvFrame);   yuvFrame  = nullptr; }
    if (codecCtx)  { avcodec_free_context(&codecCtx); codecCtx = nullptr; }
    initialised = false;
    activeCodecName.clear();
}

// ─── Drain encoded packets from the codec ────────────────────────────────────

void VideoEncoder::Impl::drainPackets()
{
    while (true) {
        int ret = avcodec_receive_packet(codecCtx, avPacket);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        if (callback) {
            EncodedPacket pkt;
            pkt.data.assign(avPacket->data, avPacket->data + avPacket->size);
            pkt.pts      = avPacket->pts;
            pkt.keyframe = (avPacket->flags & AV_PKT_FLAG_KEY) != 0;
            callback(pkt);
        }
        av_packet_unref(avPacket);
    }
}

// ─── Public VideoEncoder API ──────────────────────────────────────────────────

VideoEncoder::VideoEncoder()
    : impl_(std::make_unique<Impl>())
{}

VideoEncoder::~VideoEncoder()
{
    impl_->cleanup();
}

void VideoEncoder::setPacketCallback(PacketCallback cb)
{
    impl_->callback = std::move(cb);
}

bool VideoEncoder::init(int width, int height, int fps,
                        int64_t bitrate, const std::string& codecName)
{
    impl_->cleanup();
    impl_->lastError.clear();

    std::vector<std::string> codecCandidates;
    codecCandidates.push_back(codecName);
    for (const std::string fallback : {"libx264", "libopenh264", "h264"}) {
        if (std::find(codecCandidates.begin(), codecCandidates.end(), fallback) == codecCandidates.end()) {
            codecCandidates.emplace_back(fallback);
        }
    }

    for (const auto& candidate : codecCandidates) {
        impl_->codec = avcodec_find_encoder_by_name(candidate.c_str());
        if (!impl_->codec) {
            impl_->lastError = "FFmpeg encoder not found: " + candidate;
            continue;
        }

        impl_->codecCtx = avcodec_alloc_context3(impl_->codec);
        if (!impl_->codecCtx) {
            impl_->lastError = "Failed to allocate codec context for: " + candidate;
            continue;
        }

        // Codec parameters
        impl_->codecCtx->width        = width;
        impl_->codecCtx->height       = height;
        impl_->codecCtx->time_base    = { 1, fps };
        impl_->codecCtx->framerate    = { fps, 1 };
        impl_->codecCtx->bit_rate     = bitrate;
        impl_->codecCtx->gop_size     = fps; // keyframe every ~1 second
        impl_->codecCtx->max_b_frames = 0;   // low-latency: no B-frames
        impl_->codecCtx->pix_fmt      = AV_PIX_FMT_YUV420P;

        AVDictionary* codecOptions = nullptr;
        if (candidate == "libx264") {
            av_dict_set(&codecOptions, "preset", "ultrafast", 0);
            av_dict_set(&codecOptions, "tune", "zerolatency", 0);
            av_dict_set(&codecOptions, "profile", "baseline", 0);
        }

        const int openRet = avcodec_open2(impl_->codecCtx, impl_->codec, &codecOptions);
        av_dict_free(&codecOptions);
        if (openRet < 0) {
            impl_->lastError = "avcodec_open2(" + candidate + ") failed: " + ffErr(openRet);
            impl_->cleanup();
            continue;
        }

        // Allocate YUV frame
        impl_->yuvFrame = av_frame_alloc();
        if (!impl_->yuvFrame) {
            impl_->lastError = "Failed to allocate AVFrame for: " + candidate;
            impl_->cleanup();
            continue;
        }

        impl_->yuvFrame->format = AV_PIX_FMT_YUV420P;
        impl_->yuvFrame->width  = width;
        impl_->yuvFrame->height = height;

        const int frameBufRet = av_frame_get_buffer(impl_->yuvFrame, 32);
        if (frameBufRet < 0) {
            impl_->lastError = "av_frame_get_buffer(" + candidate + ") failed: " + ffErr(frameBufRet);
            impl_->cleanup();
            continue;
        }

        // Allocate packet
        impl_->avPacket = av_packet_alloc();
        if (!impl_->avPacket) {
            impl_->lastError = "Failed to allocate AVPacket for: " + candidate;
            impl_->cleanup();
            continue;
        }

        impl_->srcWidth       = width;
        impl_->srcHeight      = height;
        impl_->initialised    = true;
        impl_->activeCodecName = candidate;
        impl_->lastError.clear();
        return true;
    }

    if (impl_->lastError.empty()) {
        impl_->lastError = "No suitable H.264 encoder found.";
    }
    return false;
}

void VideoEncoder::encodeFrame(const uint8_t* bgra,
                               int width, int height, int stride)
{
    if (!impl_->initialised || !bgra) return;

    // (Re-)create the SwsContext if dimensions changed or first call
    if (!impl_->swsCtx ||
        impl_->srcWidth  != width ||
        impl_->srcHeight != height)
    {
        if (impl_->swsCtx) sws_freeContext(impl_->swsCtx);

        impl_->swsCtx = sws_getContext(
            width, height, AV_PIX_FMT_BGRA,
            impl_->codecCtx->width, impl_->codecCtx->height,
            AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        impl_->srcWidth  = width;
        impl_->srcHeight = height;
    }
    if (!impl_->swsCtx) return;

    // Convert BGRA → YUV420p
    const uint8_t* srcPlanes[1] = { bgra };
    const int      srcStrides[1] = { stride };

    sws_scale(impl_->swsCtx,
              srcPlanes, srcStrides,
              0, height,
              impl_->yuvFrame->data, impl_->yuvFrame->linesize);

    impl_->yuvFrame->pts = impl_->frameIndex++;

    // Send frame to codec
    if (avcodec_send_frame(impl_->codecCtx, impl_->yuvFrame) < 0) return;

    impl_->drainPackets();
}

void VideoEncoder::flush()
{
    if (!impl_->initialised) return;
    avcodec_send_frame(impl_->codecCtx, nullptr); // flush signal
    impl_->drainPackets();
}

bool VideoEncoder::isInitialised() const { return impl_->initialised; }
std::string VideoEncoder::activeCodec() const { return impl_->activeCodecName; }
std::string VideoEncoder::lastError() const { return impl_->lastError; }

} // namespace encoder
} // namespace phonectrl
