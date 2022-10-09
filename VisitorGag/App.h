#pragma once
#include "MainWindow.h"
#include "CompositionGifPlayer.h"
#include "ICaptureSource.h"

enum class CaptureMode
{
	Default,
	WGC,
	DDA
};

struct App
{
	App(bool dxDebug, std::optional<std::filesystem::path> path, CaptureMode captureMode, bool demoMode, bool noLoop);

	winrt::Windows::Foundation::IAsyncOperation<bool> TryLoadGifFromPickerAsync();
	winrt::Windows::Foundation::IAsyncAction LoadGifAsync(winrt::Windows::Storage::Streams::IRandomAccessStream stream);

private:
	void OnLButtonUp();

	void PlayShowAnimation(winrt::Windows::Foundation::TimeSpan const& duration);
	void PlayHideAnimation(winrt::Windows::Foundation::TimeSpan const& duration);
	void CaptureAndAnimate();
	winrt::fire_and_forget Rerun();

private:
	std::unique_ptr<MainWindow> m_window;
	winrt::Windows::UI::Composition::Compositor m_compositor{ nullptr };
	winrt::Windows::UI::Composition::CompositionTarget m_target{ nullptr };
	winrt::Windows::UI::Composition::SpriteVisual m_root{ nullptr };

	winrt::com_ptr<ID3D11Device> m_d3dDevice;
	winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
	winrt::com_ptr<ID2D1Factory1> m_d2dFactory;
	winrt::com_ptr<ID2D1Device> m_d2dDevice;
	winrt::Windows::UI::Composition::CompositionGraphicsDevice m_compGraphics{ nullptr };

	winrt::Windows::UI::Composition::SpriteVisual m_leftShadeVisual{ nullptr };
	winrt::Windows::UI::Composition::CompositionSurfaceBrush m_leftShadeBrush{ nullptr };
	winrt::Windows::UI::Composition::SpriteVisual m_rightShadeVisual{ nullptr };
	winrt::Windows::UI::Composition::CompositionSurfaceBrush m_rightShadeBrush{ nullptr };
	winrt::Windows::UI::Composition::CompositionDrawingSurface m_shadeSurface{ nullptr };

	winrt::Windows::System::DispatcherQueue m_dispatcherQueue{ nullptr };
	std::random_device m_randomDevice;
	std::unique_ptr<CompositionGifPlayer> m_gifPlayer;
	std::shared_ptr<ICaptureSourceFactory> m_captureSourceFactory;

	std::optional<std::filesystem::path> m_gifPath = std::nullopt;
	bool m_demoMode = false;
};
