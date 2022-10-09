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

    // No virtualized coordinates
    winrt::check_bool(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));

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
    auto compGraphics = util::CreateCompositionGraphicsDevice(compositor, d3dDevice.get());

    // Create the gif player
    auto gifPlayer = CompositionGifPlayer(compositor, compGraphics, d2dDevice, d3dDevice);
    auto gifVisual = gifPlayer.Root();
    gifVisual.AnchorPoint({ 0.5f, 0.5f });
    gifVisual.RelativeOffsetAdjustment({ 0.5f, 0.5f, 0.0f });
    root.Children().InsertAtTop(gifVisual);

    // Create the shade visuals
    auto leftShadeVisual = compositor.CreateSpriteVisual();
    leftShadeVisual.RelativeSizeAdjustment({ 0.5f, 1.0f });
    auto leftShadeBrush = compositor.CreateSurfaceBrush();
    leftShadeBrush.Stretch(winrt::CompositionStretch::None);
    leftShadeBrush.HorizontalAlignmentRatio(0.0f);
    leftShadeBrush.VerticalAlignmentRatio(0.0f);
    leftShadeVisual.Brush(leftShadeBrush);
    auto rightShadeVisual = compositor.CreateSpriteVisual();
    rightShadeVisual.RelativeSizeAdjustment({ 0.5f, 1.0f });
    rightShadeVisual.RelativeOffsetAdjustment({ 0.5f, 0.0f, 0.0f });
    auto rightShadeBrush = compositor.CreateSurfaceBrush();
    rightShadeBrush.Stretch(winrt::CompositionStretch::None);
    rightShadeBrush.HorizontalAlignmentRatio(0.0f);
    rightShadeBrush.VerticalAlignmentRatio(0.0f);
    rightShadeVisual.Brush(rightShadeBrush);
    root.Children().InsertAtTop(leftShadeVisual);
    root.Children().InsertAtTop(rightShadeVisual);
    window.SetShadeVisuals(leftShadeVisual, rightShadeVisual);

    // Prep the shade surface
    auto shadeSurface = compGraphics.CreateDrawingSurface2({ 1, 1 }, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, winrt::DirectXAlphaMode::Premultiplied);
    leftShadeBrush.Surface(shadeSurface);
    rightShadeBrush.Surface(shadeSurface);

    // Run the rest of our initialization asynchronously on the DispatcherQueue
    auto queue = controller.DispatcherQueue();
    queue.TryEnqueue([&window, &gifPlayer, d3dDevice, shadeSurface, leftShadeBrush, rightShadeBrush]() -> winrt::fire_and_forget
        {
            auto dispatcherQueue = winrt::DispatcherQueue::GetForCurrentThread();
            auto&& windowRef = window;
            auto&& player = gifPlayer;
            auto d3dDeviceRef = d3dDevice;
            winrt::com_ptr<ID3D11DeviceContext> d3dContext;
            d3dDevice->GetImmediateContext(d3dContext.put());
            auto leftBrush = leftShadeBrush;
            auto rightBrush = rightShadeBrush;
            auto shadeSurfaceRef = shadeSurface;

            // Load a gif file
            auto file = co_await OpenGifFileAsync(windowRef.m_window);
            if (file != nullptr)
            {
                auto stream = co_await file.OpenReadAsync();
                co_await dispatcherQueue;
                co_await player.LoadGifAsync(stream);
                auto gifSize = player.Size();

                // Generate random window position
                auto dxgiDevice = d3dDeviceRef.as<IDXGIDevice>();
                winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
                winrt::check_hresult(dxgiDevice->GetAdapter(dxgiAdapter.put()));
                winrt::com_ptr<IDXGIOutput> dxgiOutput;
                winrt::check_hresult(dxgiAdapter->EnumOutputs(0, dxgiOutput.put()));
                DXGI_OUTPUT_DESC outputDesc = {};
                winrt::check_hresult(dxgiOutput->GetDesc(&outputDesc));
                auto minX = outputDesc.DesktopCoordinates.left;
                auto minY = outputDesc.DesktopCoordinates.top;
                auto maxX = (outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left) - gifSize.Width;
                auto maxY = (outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top) - gifSize.Height;
                std::random_device randomDevice;
                std::uniform_int_distribution<int> distX(minX, maxX);
                std::uniform_int_distribution<int> distY(minY, maxY);
                auto x = distX(randomDevice);
                auto y = distY(randomDevice);

                // Capture screen with DDA
                winrt::com_ptr<ID3D11Texture2D> windowAreaTexture;
                {
                    auto output6 = dxgiOutput.as<IDXGIOutput6>();
                    winrt::com_ptr<IDXGIOutputDuplication> duplication;
                    winrt::check_hresult(output6->DuplicateOutput(d3dDeviceRef.get(), duplication.put()));
                    winrt::com_ptr<IDXGIResource> ddaResource;
                    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
                    winrt::check_hresult(duplication->AcquireNextFrame(INFINITE, &frameInfo, ddaResource.put()));
                    auto ddaTexture = ddaResource.as<ID3D11Texture2D>();

                    // Cut out window area from screenshot
                    D3D11_TEXTURE2D_DESC desc = {};
                    desc.Width = static_cast<uint32_t>(gifSize.Width);
                    desc.Height = static_cast<uint32_t>(gifSize.Height);
                    desc.MipLevels = 1;
                    desc.ArraySize = 1;
                    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    desc.SampleDesc.Count = 1;
                    winrt::check_hresult(d3dDeviceRef->CreateTexture2D(&desc, nullptr, windowAreaTexture.put()));
                    D3D11_BOX region = {};
                    region.left = x;
                    region.right = x + gifSize.Width;
                    region.top = y;
                    region.bottom = y + gifSize.Height;
                    region.back = 1;
                    d3dContext->CopySubresourceRegion(windowAreaTexture.get(), 0, 0, 0, 0, ddaTexture.get(), 0, &region);
                    winrt::check_hresult(duplication->ReleaseFrame());
                }

                // Apply the window area texture
                shadeSurfaceRef.Resize(gifSize);
                {
                    POINT point = {};
                    auto dxgiSurface = util::SurfaceBeginDraw(shadeSurfaceRef, &point);
                    auto endDraw = wil::scope_exit([shadeSurfaceRef]()
                        {
                            util::SurfaceEndDraw(shadeSurfaceRef);
                        });
                    auto destination = dxgiSurface.as<ID3D11Texture2D>();

                    D3D11_BOX region = {};
                    region.left = 0;
                    region.right = gifSize.Width;
                    region.top = 0;
                    region.bottom = gifSize.Height;
                    region.back = 1;
                    d3dContext->CopySubresourceRegion(destination.get(), 0, point.x, point.y, 0, windowAreaTexture.get(), 0, &region);
                }
                leftBrush.Offset({ 0.0f, 0.0f });
                rightBrush.Offset({ static_cast<float>(gifSize.Width) / -2.0f, 0.0f });

                // Show window
                player.Play();
                windowRef.Show(x, y, gifSize);
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