#include "pch.h"
#include "WGCCaptureSource.h"

namespace winrt
{
    using namespace Windows::Foundation::Metadata;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Graphics::DirectX;
}

namespace util
{
    using namespace robmikh::common::desktop;
}

std::unique_ptr<ICaptureSource> WGCCaptureSourceFactory::CreateCaptureSource(winrt::com_ptr<ID3D11Device> const& d3dDevice)
{
	auto source = std::make_unique<WGCCaptureSource>(d3dDevice);
	return source;
}

WGCCaptureSource::WGCCaptureSource(winrt::com_ptr<ID3D11Device> const& d3dDevice)
{
    m_d3dDevice = d3dDevice;
    m_monitor = winrt::check_pointer(MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY));
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    winrt::check_bool(GetMonitorInfoW(m_monitor, &monitorInfo));
    m_desktopCoordinates = monitorInfo.rcMonitor;
}

winrt::com_ptr<ID3D11Texture2D> WGCCaptureSource::Capture()
{
    auto item = util::CreateCaptureItemForMonitor(m_monitor);
    auto itemSize = item.Size();
    auto device = CreateDirect3DDevice(m_d3dDevice.as<IDXGIDevice>().get());
    auto framePool = winrt::Direct3D11CaptureFramePool::CreateFreeThreaded(device, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, itemSize);
    auto session = framePool.CreateCaptureSession(item);
    session.IsCursorCaptureEnabled(false);
    if (winrt::ApiInformation::IsPropertyPresent(winrt::name_of<winrt::GraphicsCaptureSession>(), L"IsBorderRequired"))
    {
        session.IsBorderRequired(false);
    }

    wil::shared_event captureEvent(wil::EventOptions::ManualReset);
    winrt::Direct3D11CaptureFrame frame{ nullptr };
    framePool.FrameArrived([&frame, captureEvent](auto&& framePool, auto&&)
        {
            frame = framePool.TryGetNextFrame();
            captureEvent.SetEvent();
        });

    session.StartCapture();
    captureEvent.wait();
    framePool.Close();
    session.Close();

    auto captureTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
    return captureTexture;
}
