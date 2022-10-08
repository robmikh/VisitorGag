#include "pch.h"
#include "MainWindow.h"
#include "CompositionGifPlayer.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::Graphics;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::Graphics::Imaging;
    using namespace Windows::Storage;
    using namespace Windows::Storage::Pickers;
    using namespace Windows::Storage::Streams;
    using namespace Windows::System;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
}

namespace util
{
    using namespace robmikh::common::desktop;
    using namespace robmikh::common::uwp;
}

winrt::IAsyncOperation<winrt::StorageFile> OpenGifFileAsync(HWND modalTo);

int __stdcall WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
    // Initialize COM
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // Create the DispatcherQueue that the compositor needs to run
    auto controller = util::CreateDispatcherQueueControllerForCurrentThread();

    // Create our window and visual tree
    auto window = MainWindow(L"VisitorGag", 800, 600);
    auto compositor = winrt::Compositor();
    auto target = window.CreateWindowTarget(compositor);
    auto root = compositor.CreateSpriteVisual();
    root.RelativeSizeAdjustment({ 1.0f, 1.0f });
    target.Root(root);

    // Init D3D and D2D
    auto d3dDevice = util::CreateD3DDevice();
    auto d2dFactory = util::CreateD2DFactory();
    auto d2dDevice = util::CreateD2DDevice(d2dFactory, d3dDevice);
    auto compGraphics = util::CreateCompositionGraphicsDevice(compositor, d2dDevice.get());

    // Create the gif player
    auto gifPlayer = CompositionGifPlayer(compositor, compGraphics, d2dDevice, d3dDevice);
    auto gifVisual = gifPlayer.Root();
    gifVisual.AnchorPoint({ 0.5f, 0.5f });
    gifVisual.RelativeOffsetAdjustment({ 0.5f, 0.5f, 0.0f });
    root.Children().InsertAtTop(gifVisual);
    // Run the rest of our initialization asynchronously on the DispatcherQueue
    auto queue = controller.DispatcherQueue();
    queue.TryEnqueue([&window, &gifPlayer]() -> winrt::fire_and_forget
        {
            auto dispatcherQueue = winrt::DispatcherQueue::GetForCurrentThread();
            auto&& windowRef = window;
            auto&& player = gifPlayer;

            // Load a gif file
            auto file = co_await OpenGifFileAsync(windowRef.m_window);
            if (file != nullptr)
            {
                auto stream = co_await file.OpenReadAsync();
                co_await dispatcherQueue;
                co_await player.LoadGifAsync(stream);
                windowRef.Resize(player.Size());
                windowRef.Show();
            }
            else
            {
                co_await dispatcherQueue;
                PostQuitMessage(0);
            }
            co_return;
        });

    // Message pump
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return util::ShutdownDispatcherQueueControllerAndWait(controller, static_cast<int>(msg.wParam));
}

winrt::IAsyncOperation<winrt::StorageFile> OpenGifFileAsync(HWND modalTo)
{
    auto picker = winrt::FileOpenPicker();
    picker.SuggestedStartLocation(winrt::PickerLocationId::PicturesLibrary);
    picker.ViewMode(winrt::PickerViewMode::Thumbnail);
    picker.FileTypeFilter().Append(L".gif");
    auto interop = picker.as<IInitializeWithWindow>();
    winrt::check_hresult(interop->Initialize(modalTo));

    auto file = co_await picker.PickSingleFileAsync();
    co_return file;
}