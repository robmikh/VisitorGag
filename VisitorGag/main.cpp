#include "pch.h"
#include "MainWindow.h"
#include "CompositionGifPlayer.h"
#include "App.h"

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



int __stdcall WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
    // Initialize COM
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // No virtualized coordinates
    winrt::check_bool(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));

    // Create the DispatcherQueue that the compositor needs to run
    auto controller = util::CreateDispatcherQueueControllerForCurrentThread();

    // Create our app
    auto app = App();

    // Run the rest of our initialization asynchronously on the DispatcherQueue
    auto queue = controller.DispatcherQueue();
    queue.TryEnqueue([&app]() -> winrt::fire_and_forget
        {
            auto dispatcherQueue = winrt::DispatcherQueue::GetForCurrentThread();
            if (!co_await app.TryLoadGifFromPickerAsync())
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

