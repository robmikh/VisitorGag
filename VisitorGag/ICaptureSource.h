#pragma once

struct ICaptureSource
{
	virtual ~ICaptureSource() {};

	virtual RECT DesktopCoordinates() = 0;
	virtual winrt::com_ptr<ID3D11Texture2D> Capture() = 0;
};

struct ICaptureSourceFactory
{
	virtual ~ICaptureSourceFactory() {};

	virtual std::unique_ptr<ICaptureSource> CreateCaptureSource(winrt::com_ptr<ID3D11Device> const& d3dDevice) = 0;
};
