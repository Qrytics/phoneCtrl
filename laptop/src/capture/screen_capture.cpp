#include "screen_capture.h"

#include <atomic>
#include <cassert>
#include <stdexcept>
#include <thread>

// ─── platform guard ──────────────────────────────────────────────────────────
#ifndef _WIN32
#  error "ScreenCapture requires Windows (DXGI Desktop Duplication API)"
#endif

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace phonectrl {
namespace capture {

// ─── Private implementation ───────────────────────────────────────────────────

struct ScreenCapture::Impl {
    FrameCallback  callback;
    std::thread    captureThread;
    std::atomic_bool running{false};
    int monitorIndex = 0;

    // Resolved once DXGI is initialised
    int captureWidth  = 0;
    int captureHeight = 0;

    // DXGI / D3D11 objects
    ComPtr<ID3D11Device>           d3dDevice;
    ComPtr<ID3D11DeviceContext>    d3dCtx;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D>        stagingTexture;

    bool initDXGI();
    void captureLoop();
    bool acquireFrame(Frame& outFrame);
    void cleanup();
};

// ─── DXGI initialisation ─────────────────────────────────────────────────────

bool ScreenCapture::Impl::initDXGI()
{
    // Create D3D11 device (Feature Level 11.0 or higher required for DXGI 1.2)
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr, 0,
        D3D11_SDK_VERSION,
        &d3dDevice,
        &featureLevel,
        &d3dCtx
    );
    if (FAILED(hr)) return false;

    // Enumerate the requested output (monitor)
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(static_cast<UINT>(monitorIndex), &output);
    if (FAILED(hr)) return false;

    // Query the desktop dimensions from the output descriptor
    DXGI_OUTPUT_DESC outDesc{};
    output->GetDesc(&outDesc);
    captureWidth  = outDesc.DesktopCoordinates.right  - outDesc.DesktopCoordinates.left;
    captureHeight = outDesc.DesktopCoordinates.bottom - outDesc.DesktopCoordinates.top;

    // Upgrade to IDXGIOutput1 for Desktop Duplication
    ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) return false;

    hr = output1->DuplicateOutput(d3dDevice.Get(), &duplication);
    if (FAILED(hr)) return false;

    // Create a CPU-accessible staging texture (same dimensions as the desktop)
    D3D11_TEXTURE2D_DESC stDesc{};
    stDesc.Width              = static_cast<UINT>(captureWidth);
    stDesc.Height             = static_cast<UINT>(captureHeight);
    stDesc.MipLevels          = 1;
    stDesc.ArraySize          = 1;
    stDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM; // BGRA
    stDesc.SampleDesc.Count   = 1;
    stDesc.Usage              = D3D11_USAGE_STAGING;
    stDesc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;

    hr = d3dDevice->CreateTexture2D(&stDesc, nullptr, &stagingTexture);
    return SUCCEEDED(hr);
}

// ─── Capture loop (runs on background thread) ────────────────────────────────

void ScreenCapture::Impl::captureLoop()
{
    while (running.load()) {
        Frame frame;
        if (acquireFrame(frame) && callback) {
            callback(frame);
        }
    }
}

bool ScreenCapture::Impl::acquireFrame(Frame& outFrame)
{
    ComPtr<IDXGIResource> desktopResource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};

    // Wait up to 50 ms for a new frame
    HRESULT hr = duplication->AcquireNextFrame(50, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false; // no new frame yet
    }
    if (FAILED(hr)) {
        // Output change (e.g. resolution change) — reinitialise
        cleanup();
        initDXGI();
        return false;
    }

    // Get the captured texture
    ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (FAILED(hr)) {
        duplication->ReleaseFrame();
        return false;
    }

    // Copy GPU texture → CPU staging texture
    d3dCtx->CopyResource(stagingTexture.Get(), desktopTexture.Get());

    // Map the staging texture to read pixels
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = d3dCtx->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(hr)) {
        outFrame.width  = captureWidth;
        outFrame.height = captureHeight;
        outFrame.stride = static_cast<int>(mapped.RowPitch);

        const size_t dataSize = static_cast<size_t>(mapped.RowPitch) *
                                static_cast<size_t>(captureHeight);
        outFrame.data.resize(dataSize);
        std::memcpy(outFrame.data.data(), mapped.pData, dataSize);

        d3dCtx->Unmap(stagingTexture.Get(), 0);
    }

    duplication->ReleaseFrame();
    return SUCCEEDED(hr);
}

void ScreenCapture::Impl::cleanup()
{
    duplication.Reset();
    stagingTexture.Reset();
    d3dCtx.Reset();
    d3dDevice.Reset();
}

// ─── Public ScreenCapture API ─────────────────────────────────────────────────

ScreenCapture::ScreenCapture()
    : impl_(std::make_unique<Impl>())
{}

ScreenCapture::~ScreenCapture()
{
    stop();
}

void ScreenCapture::setFrameCallback(FrameCallback cb)
{
    impl_->callback = std::move(cb);
}

bool ScreenCapture::start(int monitorIndex)
{
    if (impl_->running.load()) return true;

    impl_->monitorIndex = monitorIndex;
    if (!impl_->initDXGI()) return false;

    impl_->running = true;
    impl_->captureThread = std::thread([this]{ impl_->captureLoop(); });
    return true;
}

void ScreenCapture::stop()
{
    impl_->running = false;
    if (impl_->captureThread.joinable()) {
        impl_->captureThread.join();
    }
    impl_->cleanup();
}

bool ScreenCapture::isRunning() const { return impl_->running.load(); }
int  ScreenCapture::width()     const { return impl_->captureWidth;   }
int  ScreenCapture::height()    const { return impl_->captureHeight;  }

} // namespace capture
} // namespace phonectrl
