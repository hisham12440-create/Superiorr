#include "capture.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <atomic>

#include "core/log.h"

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wdx = winrt::Windows::Graphics::DirectX;
namespace wd3d = winrt::Windows::Graphics::DirectX::Direct3D11;


namespace superior {

struct Capture::Impl {
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    wd3d::IDirect3DDevice rtDevice{nullptr};
    winrt::event_token frameToken{};
    winrt::event_token closedToken{};
    winrt::Windows::Graphics::SizeInt32 lastSize{0, 0};
    std::atomic<bool> closed{false};
    bool active = false;
};

Capture::Capture() : p_(new Impl) {}
Capture::~Capture() { Stop(); }

bool Capture::Active() const { return p_->active; }
bool Capture::Closed() const { return p_->closed.load(); }

bool Capture::Start(HWND target, ID3D11Device* dev, HANDLE frameEvent, std::string& error) {
    Stop();
    try {
        if (!wgc::GraphicsCaptureSession::IsSupported()) {
            error = "Windows.Graphics.Capture is not supported on this Windows version";
            return false;
        }
        winrt::com_ptr<IDXGIDevice> dxgi;
        winrt::check_hresult(dev->QueryInterface(IID_PPV_ARGS(dxgi.put())));
        winrt::com_ptr<::IInspectable> insp;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), insp.put()));
        p_->rtDevice = insp.as<wd3d::IDirect3DDevice>();

        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForWindow(target, winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                                      winrt::put_abi(p_->item)));

        p_->lastSize = p_->item.Size();
        p_->closed = false;
        p_->pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            p_->rtDevice, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, p_->lastSize);
        p_->frameToken = p_->pool.FrameArrived([frameEvent](auto&&, auto&&) { SetEvent(frameEvent); });
        Impl* impl = p_.get();
        p_->closedToken = p_->item.Closed([impl, frameEvent](auto&&, auto&&) {
            impl->closed = true;
            SetEvent(frameEvent);
        });
        p_->session = p_->pool.CreateCaptureSession(p_->item);
        try {
            p_->session.IsCursorCaptureEnabled(false);
        } catch (...) {
        }
        p_->session.StartCapture();
        p_->active = true;
        SLOG("Capture started (%dx%d)", p_->lastSize.Width, p_->lastSize.Height);
        return true;
    } catch (const winrt::hresult_error& e) {
        error = winrt::to_string(e.message());
        SERR("Capture start failed: %s", error.c_str());
    } catch (const std::exception& e) {
        error = e.what();
        SERR("Capture start failed: %s", e.what());
    }
    Stop();
    return false;
}

void Capture::Stop() {
    if (!p_) return;
    try {
        if (p_->pool) p_->pool.FrameArrived(p_->frameToken);
        if (p_->item) p_->item.Closed(p_->closedToken);
        if (p_->session) p_->session.Close();
        if (p_->pool) p_->pool.Close();
    } catch (...) {
    }
    p_->session = nullptr;
    p_->pool = nullptr;
    p_->item = nullptr;
    p_->rtDevice = nullptr;
    p_->active = false;
}

bool Capture::Poll(const std::function<void(ID3D11Texture2D*, UINT, UINT)>& onFrame) {
    if (!p_->active) return false;
    try {
        wgc::Direct3D11CaptureFrame latest{nullptr};
        for (;;) {
            auto f = p_->pool.TryGetNextFrame();
            if (!f) break;
            if (latest) latest.Close();
            latest = f;
        }
        if (!latest) return false;

        const auto cs = latest.ContentSize();
        if (cs.Width <= 0 || cs.Height <= 0) {
            latest.Close();
            return false;
        }
        if (cs.Width != p_->lastSize.Width || cs.Height != p_->lastSize.Height) {
            latest.Close();
            p_->pool.Recreate(p_->rtDevice, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, cs);
            p_->lastSize = cs;
            return false;  // Wait for a surface from the resized pool.
        }

        auto access = latest.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> tex;
        winrt::check_hresult(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), tex.put_void()));
        onFrame(tex.get(), static_cast<UINT>(cs.Width), static_cast<UINT>(cs.Height));
        latest.Close();
        return true;
    } catch (const winrt::hresult_error& e) {
        SERR("Capture poll failed: %s", winrt::to_string(e.message()).c_str());
        p_->closed = true;  // treat a dead capture session like a closed window
        return false;
    }
}

}  // namespace superior
