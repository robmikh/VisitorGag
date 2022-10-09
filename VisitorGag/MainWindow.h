#pragma once
#include <robmikh.common/DesktopWindow.h>

struct MainWindow : robmikh::common::desktop::DesktopWindow<MainWindow>
{
	static const std::wstring ClassName;
	MainWindow(std::wstring const& titleString, int width, int height);
	LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam);

	void Show(int32_t x, int32_t y, winrt::Windows::Graphics::SizeInt32 const& size);
	void Hide();
	void OnLButtonUp(std::function<void()> const& callback);

private:
	static void RegisterWindowClass();

	void PlayShowAnimation(winrt::Windows::Foundation::TimeSpan const& duration);
	void PlayHideAnimation(winrt::Windows::Foundation::TimeSpan const& duration);

private:
	std::function<void()> m_lButtonUp;
	winrt::Windows::UI::Composition::Compositor m_compositor{ nullptr };
	winrt::Windows::UI::Composition::Visual m_leftShadeVisual{ nullptr };
	winrt::Windows::UI::Composition::Visual m_rightShadeVisual{ nullptr };
};