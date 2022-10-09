#include "pch.h"
#include "DDACaptureSource.h"

namespace util
{
    using namespace robmikh::common::uwp;
}

std::unique_ptr<ICaptureSource> DDACaptureSourceFactory::CreateCaptureSource(winrt::com_ptr<ID3D11Device> const& d3dDevice)
{
	auto source = std::make_unique<DDACaptureSource>(d3dDevice);
	return source;
}

DDACaptureSource::DDACaptureSource(winrt::com_ptr<ID3D11Device> const& d3dDevice)
{
	m_d3dDevice = d3dDevice;
    m_dxgiDevice = m_d3dDevice.as<IDXGIDevice>();
    winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
    winrt::check_hresult(m_dxgiDevice->GetAdapter(dxgiAdapter.put()));
    winrt::check_hresult(dxgiAdapter->EnumOutputs(0, m_dxgiOutput.put()));
    DXGI_OUTPUT_DESC outputDesc = {};
    winrt::check_hresult(m_dxgiOutput->GetDesc(&outputDesc));
    m_desktopCoordinates = outputDesc.DesktopCoordinates;
}

winrt::com_ptr<ID3D11Texture2D> DDACaptureSource::Capture()
{
    winrt::com_ptr<ID3D11Texture2D> captureTexture;
    {
        auto output6 = m_dxgiOutput.as<IDXGIOutput6>();
        winrt::com_ptr<IDXGIOutputDuplication> duplication;
        winrt::check_hresult(output6->DuplicateOutput(m_d3dDevice.get(), duplication.put()));
        winrt::com_ptr<IDXGIResource> ddaResource;
        DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
        winrt::check_hresult(duplication->AcquireNextFrame(INFINITE, &frameInfo, ddaResource.put()));
        // Windows 10 build 19044 seems to have an issue where subsequent calls to DDA
        // can get an empty frame. As a workaround, always get the second frame.
        ddaResource = nullptr;
        winrt::check_hresult(duplication->ReleaseFrame());
        winrt::check_hresult(duplication->AcquireNextFrame(INFINITE, &frameInfo, ddaResource.put()));
        auto ddaTexture = ddaResource.as<ID3D11Texture2D>();
        captureTexture = util::CopyD3DTexture(m_d3dDevice, ddaTexture, false);
        winrt::check_hresult(duplication->ReleaseFrame());
    }
    return captureTexture;
}
