#include "pch.h"
#include "MainWindow.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Graphics;
    using namespace Windows::UI::Composition;
}

const std::wstring MainWindow::ClassName = L"VisitorGag.MainWindow";
std::once_flag MainWindowClassRegistration;

void MainWindow::RegisterWindowClass()
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = instance;
    wcex.hIcon = LoadIconW(instance, IDI_APPLICATION);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.lpszClassName = ClassName.c_str();
    wcex.hIconSm = LoadIconW(instance, IDI_APPLICATION);
    winrt::check_bool(RegisterClassExW(&wcex));
}

MainWindow::MainWindow(std::wstring const& titleString, int width, int height)
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));

    std::call_once(MainWindowClassRegistration, []() { RegisterWindowClass(); });

    auto exStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST;
    auto style = WS_POPUP;

    RECT rect = { 0, 0, width, height};
    winrt::check_bool(AdjustWindowRectEx(&rect, style, false, exStyle));
    auto adjustedWidth = rect.right - rect.left;
    auto adjustedHeight = rect.bottom - rect.top;

    winrt::check_bool(CreateWindowExW(exStyle, ClassName.c_str(), titleString.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, adjustedWidth, adjustedHeight, nullptr, nullptr, instance, this));
    WINRT_ASSERT(m_window);
}

LRESULT MainWindow::MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam)
{
    switch (message)
    {
    case WM_LBUTTONUP:
        Hide();
        break;
    default:
        return base_type::MessageHandler(message, wparam, lparam);
    }
    return 0;
}

void MainWindow::SetShadeVisuals(winrt::Visual const& leftShadeVisual, winrt::Visual const& rightShadeVisual)
{
    m_leftShadeVisual = leftShadeVisual;
    m_rightShadeVisual = rightShadeVisual;
    m_compositor = m_leftShadeVisual.Compositor();
}

void MainWindow::Show(int32_t x, int32_t y, winrt::SizeInt32 const& size)
{
    SetWindowPos(m_window, HWND_TOPMOST, x, y, size.Width, size.Height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateWindow(m_window);

    if (m_compositor != nullptr)
    {
        PlayShowAnimation(std::chrono::milliseconds(800));
    }
}

void MainWindow::Hide()
{
    if (m_compositor != nullptr)
    {
        auto batch = m_compositor.CreateScopedBatch(winrt::CompositionBatchTypes::Animation);
        PlayHideAnimation(std::chrono::milliseconds(800));
        batch.Completed([&](auto&&, auto&&)
            {
                ShowWindow(m_window, SW_HIDE);
            });
        batch.End();
    }
    else
    {
        ShowWindow(m_window, SW_HIDE);
    }
}

void MainWindow::PlayShowAnimation(winrt::TimeSpan const& duration)
{
    auto leftAnimation = m_compositor.CreateScalarKeyFrameAnimation();
    leftAnimation.InsertKeyFrame(0.0f, 0.0f);
    leftAnimation.InsertKeyFrame(1.0f, -0.5f);
    leftAnimation.IterationBehavior(winrt::AnimationIterationBehavior::Count);
    leftAnimation.IterationCount(1);
    leftAnimation.Duration(duration);

    auto rightAnimation = m_compositor.CreateScalarKeyFrameAnimation();
    rightAnimation.InsertKeyFrame(0.0f, 0.5f);
    rightAnimation.InsertKeyFrame(1.0f, 1.0f);
    rightAnimation.IterationBehavior(winrt::AnimationIterationBehavior::Count);
    rightAnimation.IterationCount(1);
    rightAnimation.Duration(duration);

    m_leftShadeVisual.StartAnimation(L"RelativeOffsetAdjustment.X", leftAnimation);
    m_rightShadeVisual.StartAnimation(L"RelativeOffsetAdjustment.X", rightAnimation);
}

void MainWindow::PlayHideAnimation(winrt::TimeSpan const& duration)
{
    auto leftAnimation = m_compositor.CreateScalarKeyFrameAnimation();
    leftAnimation.InsertKeyFrame(0.0f, -0.5f);
    leftAnimation.InsertKeyFrame(1.0f, 0.0f);
    leftAnimation.IterationBehavior(winrt::AnimationIterationBehavior::Count);
    leftAnimation.IterationCount(1);
    leftAnimation.Duration(duration);

    auto rightAnimation = m_compositor.CreateScalarKeyFrameAnimation();
    rightAnimation.InsertKeyFrame(0.0f, 1.0f);
    rightAnimation.InsertKeyFrame(1.0f, 0.5f);
    rightAnimation.IterationBehavior(winrt::AnimationIterationBehavior::Count);
    rightAnimation.IterationCount(1);
    rightAnimation.Duration(duration);

    m_leftShadeVisual.StartAnimation(L"RelativeOffsetAdjustment.X", leftAnimation);
    m_rightShadeVisual.StartAnimation(L"RelativeOffsetAdjustment.X", rightAnimation);
}
