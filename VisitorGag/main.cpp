#include "pch.h"
#include "MainWindow.h"
#include "CompositionGifPlayer.h"
#include "App.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::Foundation::Metadata;
    using namespace Windows::Graphics;
    using namespace Windows::Graphics::Capture;
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
}

struct Options
{
    bool DxDebug = false;
    std::optional<std::filesystem::path> FilePath = std::nullopt;
    CaptureMode CaptureMode = CaptureMode::Default;
    bool DemoMode = false;
};

std::optional<Options> ParseOptions(int argc, wchar_t* argv[]);

int __stdcall WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
    // Route the console to our parent
    // Taken from https://social.msdn.microsoft.com/Forums/en-US/653d3703-feae-4537-b1c6-bd548057d0c3/how-to-get-output-on-command-prompt-if-i-run-mfc-mdi-application-through-cmd-prompt?forum=vcgeneral
    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        FILE* fpstdin = stdin;
        FILE* fpstdout = stdout;
        FILE* fpstderr = stderr;

        freopen_s(&fpstdin, "CONIN$", "r", stdin);
        freopen_s(&fpstdout, "CONOUT$", "w", stdout);
        freopen_s(&fpstderr, "CONOUT$", "w", stderr);

        wprintf(L"\n");
    }

    // Parse ags
    int argc = 0;
    auto argv = winrt::check_pointer(CommandLineToArgvW(GetCommandLineW(), &argc));
    auto optionsOpt = ParseOptions(argc, argv);
    if (!optionsOpt.has_value())
    {
        return 0;
    }
    auto options = optionsOpt.value();

    // Initialize COM
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // No virtualized coordinates
    winrt::check_bool(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));

    // Create the DispatcherQueue that the compositor needs to run
    auto controller = util::CreateDispatcherQueueControllerForCurrentThread();

    // Create our app
    auto app = App(options.DxDebug, options.FilePath, options.CaptureMode, options.DemoMode);

    // Run the rest of our initialization asynchronously on the DispatcherQueue
    auto queue = controller.DispatcherQueue();
    queue.TryEnqueue([&app]() -> winrt::fire_and_forget
        {
            auto dispatcherQueue = winrt::DispatcherQueue::GetForCurrentThread();
            auto&& appRef = app;

            // Request access to the capture border
            if (winrt::ApiInformation::IsPropertyPresent(winrt::name_of<winrt::GraphicsCaptureSession>(), L"IsBorderRequired"))
            {
                co_await winrt::GraphicsCaptureAccess::RequestAccessAsync(winrt::GraphicsCaptureAccessKind::Borderless);
            }

            if (!co_await appRef.TryLoadGifFromPickerAsync())
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

std::optional<Options> ParseOptions(int argc, wchar_t* argv[])
{
    using namespace robmikh::common::wcli::impl;

    // Much of this method uses helpers from the robmikh.common package.
    // I wouldn't recommend using this part, but if you're curious it can 
    // be found here: https://github.com/robmikh/robmikh.common/blob/master/robmikh.common/include/robmikh.common/wcliparse.h
    std::vector<std::wstring> args(argv + 1, argv + argc);
    if (GetFlag(args, L"-help") || GetFlag(args, L"/?"))
    {
        wprintf(L"VisitorGag.exe\n");
        wprintf(L"A joke application that brings you a visitor in the form of a gif.\n");
        wprintf(L"\n");
        wprintf(L"Flags:\n");
        wprintf(L"  -dxDebug                  (optional) Use the D3D and D2D debug layers.\n");
        wprintf(L"  -forceWGC                 (optional) Force the use of Windows.Graphics.Capture.\n");
        wprintf(L"  -forceDDA                 (optional) Force the use of the Desktop Duplication API.\n");
        wprintf(L"  -demoMode                 (optional) Always show the visitor in the same spot for demoing.\n");
        wprintf(L"\n");
        wprintf(L"Options:\n");
        wprintf(L"  -gif <path to gif file>   (optional) Path to a gif file. A picker will be shown if none is provided.\n");
        wprintf(L"\n");
        return std::nullopt;
    }
    bool dxDebug = GetFlag(args, L"-dxDebug") || GetFlag(args, L"/dxDebug");
    bool forceWGC = GetFlag(args, L"-forceWGC") || GetFlag(args, L"/forceWGC");
    bool forceDDA = GetFlag(args, L"-forceDDA") || GetFlag(args, L"/forceDDA");
    auto captureMode = CaptureMode::Default;
    bool demoMode = GetFlag(args, L"-demoMode") || GetFlag(args, L"/demoMode");
    if (forceWGC && forceDDA)
    {
        wprintf(L"Both \"-forceWGC\" and \"-forceDDA\" cannot be set!\n");
        return std::nullopt;
    }
    else if (forceWGC)
    {
        captureMode = CaptureMode::WGC;
    }
    else if (forceDDA)
    {
        captureMode = CaptureMode::DDA;
    }
    std::optional<std::filesystem::path> filePath = std::nullopt;
    {
        auto pathString = GetFlagValue(args, L"-gif", L"/gif");
        if (!pathString.empty())
        {
            filePath = std::optional(std::filesystem::path(pathString));
        }
    }

    if (dxDebug)
    {
        wprintf(L"Using D3D and D2D debug layers...\n");
    }
    if (forceWGC)
    {
        wprintf(L"Forcing the use of Windows.Graphics.Capture...\n");
    }
    if (forceDDA)
    {
        wprintf(L"Forcing the use of the Desktop Duplication API...\n");
    }
    if (auto filePathValue = filePath)
    {
        wprintf(L"Using file \"%s\"...\n", filePathValue->wstring().c_str());
    }
    if (demoMode)
    {
        wprintf(L"Using demo mode...\n");
    }
    
    return std::optional(Options{ dxDebug, filePath, captureMode, demoMode });
}