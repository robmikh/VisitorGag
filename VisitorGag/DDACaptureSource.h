#pragma once
#include "ICaptureSource.h"

struct DDACaptureSource : public ICaptureSource
{
	DDACaptureSource(winrt::com_ptr<ID3D11Device> const& d3dDevice);
	~DDACaptureSource() override {}

	RECT DesktopCoordinates() override { return m_desktopCoordinates; };
	winrt::com_ptr<ID3D11Texture2D> Capture() override;
	
private:
	winrt::com_ptr<ID3D11Device> m_d3dDevice;
	winrt::com_ptr<IDXGIDevice> m_dxgiDevice;
	winrt::com_ptr<IDXGIOutput> m_dxgiOutput;
	RECT m_desktopCoordinates = {};
};

struct DDACaptureSourceFactory : public ICaptureSourceFactory
{
	DDACaptureSourceFactory() {}
	~DDACaptureSourceFactory() override {}

	std::unique_ptr<ICaptureSource> CreateCaptureSource(winrt::com_ptr<ID3D11Device> const& d3dDevice) override;
};

