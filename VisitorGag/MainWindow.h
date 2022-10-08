#pragma once
#include <robmikh.common/DesktopWindow.h>

struct MainWindow : robmikh::common::desktop::DesktopWindow<MainWindow>
{
	static const std::wstring ClassName;
	MainWindow(std::wstring const& titleString, int width, int height);
	LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam);

	void Show(int32_t x, int32_t y, winrt::Windows::Graphics::SizeInt32 const& size);
	void Hide();

private:
	static void RegisterWindowClass();
};