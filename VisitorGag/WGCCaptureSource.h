#pragma once
#include "ICaptureSource.h"

struct WGCCaptureSource : public ICaptureSource
{
	WGCCaptureSource(winrt::com_ptr<ID3D11Device> const& d3dDevice);
	~WGCCaptureSource() override {}

	RECT DesktopCoordinates() override { return m_desktopCoordinates; };
	winrt::com_ptr<ID3D11Texture2D> Capture() override;

private:
	winrt::com_ptr<ID3D11Device> m_d3dDevice;
	HMONITOR m_monitor = {};
	RECT m_desktopCoordinates = {};
};

struct WGCCaptureSourceFactory : public ICaptureSourceFactory
{
	WGCCaptureSourceFactory() {}
	~WGCCaptureSourceFactory() override {}

	std::unique_ptr<ICaptureSource> CreateCaptureSource(winrt::com_ptr<ID3D11Device> const& d3dDevice) override;
};