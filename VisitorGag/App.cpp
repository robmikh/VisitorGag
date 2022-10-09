#include "pch.h"
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
    using namespace robmikh::common::uwp;
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

App::App()
{
    m_dispatcherQueue = winrt::DispatcherQueue::GetForCurrentThread();

    // Create our window and visual tree
    m_window = std::make_unique<MainWindow>(L"VisitorGag", 800, 600);
    m_compositor = winrt::Compositor();
    m_target = m_window->CreateWindowTarget(m_compositor);
    m_root = m_compositor.CreateSpriteVisual();
    m_root.RelativeSizeAdjustment({ 1.0f, 1.0f });
    m_target.Root(m_root);

    // Init D3D and D2D
    m_d3dDevice = util::CreateD3DDevice();
    m_d3dDevice->GetImmediateContext(m_d3dContext.put());
    m_d2dFactory = util::CreateD2DFactory();
    m_d2dDevice = util::CreateD2DDevice(m_d2dFactory, m_d3dDevice);
    m_compGraphics = util::CreateCompositionGraphicsDevice(m_compositor, m_d3dDevice.get());

    // Create the gif player
    m_gifPlayer = std::make_unique<CompositionGifPlayer>(m_compositor, m_compGraphics, m_d2dDevice, m_d3dDevice);
    auto gifVisual = m_gifPlayer->Root();
    gifVisual.AnchorPoint({ 0.5f, 0.5f });
    gifVisual.RelativeOffsetAdjustment({ 0.5f, 0.5f, 0.0f });
    m_root.Children().InsertAtTop(gifVisual);

    // Create the shade visuals
    m_leftShadeVisual = m_compositor.CreateSpriteVisual();
    m_leftShadeVisual.RelativeSizeAdjustment({ 0.5f, 1.0f });
    m_leftShadeBrush = m_compositor.CreateSurfaceBrush();
    m_leftShadeBrush.Stretch(winrt::CompositionStretch::None);
    m_leftShadeBrush.HorizontalAlignmentRatio(0.0f);
    m_leftShadeBrush.VerticalAlignmentRatio(0.0f);
    m_leftShadeVisual.Brush(m_leftShadeBrush);
    m_rightShadeVisual = m_compositor.CreateSpriteVisual();
    m_rightShadeVisual.RelativeSizeAdjustment({ 0.5f, 1.0f });
    m_rightShadeVisual.RelativeOffsetAdjustment({ 0.5f, 0.0f, 0.0f });
    m_rightShadeBrush = m_compositor.CreateSurfaceBrush();
    m_rightShadeBrush.Stretch(winrt::CompositionStretch::None);
    m_rightShadeBrush.HorizontalAlignmentRatio(0.0f);
    m_rightShadeBrush.VerticalAlignmentRatio(0.0f);
    m_rightShadeVisual.Brush(m_rightShadeBrush);
    m_root.Children().InsertAtTop(m_leftShadeVisual);
    m_root.Children().InsertAtTop(m_rightShadeVisual);

    // DEBUG: Remove later
    //m_leftShadeVisual.Brush(m_compositor.CreateColorBrush(winrt::Color{ 255, 224, 143, 22 }));
    //m_rightShadeVisual.Brush(m_compositor.CreateColorBrush(winrt::Color{ 255, 22, 210, 224 }));

    // Prep the shade surface
    m_shadeSurface = m_compGraphics.CreateDrawingSurface2({ 1, 1 }, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, winrt::DirectXAlphaMode::Premultiplied);
    m_leftShadeBrush.Surface(m_shadeSurface);
    m_rightShadeBrush.Surface(m_shadeSurface);

    // Setup callback
    m_window->OnLButtonUp(std::bind(&App::OnLButtonUp, this));
}

winrt::IAsyncOperation<bool> App::TryLoadGifFromPickerAsync()
{
    // Load a gif file
    auto file = co_await OpenGifFileAsync(m_window->m_window);
    if (file != nullptr)
    {
        auto stream = co_await file.OpenReadAsync();
        co_await LoadGifAsync(stream);
        co_return true;
    }
    else
    {
        co_return false;
    }
}

winrt::IAsyncAction App::LoadGifAsync(winrt::IRandomAccessStream stream)
{
    co_await m_dispatcherQueue;
    co_await m_gifPlayer->LoadGifAsync(stream);
    CaptureAndAnimate();
}

void App::OnLButtonUp()
{
    auto batch = m_compositor.CreateScopedBatch(winrt::CompositionBatchTypes::Animation);
    PlayHideAnimation(std::chrono::milliseconds(800));
    batch.Completed([&](auto&&, auto&&)
        {
            m_window->Hide();
            Rerun();
        });
    batch.End();
}

void App::PlayShowAnimation(winrt::Windows::Foundation::TimeSpan const& duration)
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

void App::PlayHideAnimation(winrt::Windows::Foundation::TimeSpan const& duration)
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

void App::CaptureAndAnimate()
{
    auto gifSize = m_gifPlayer->Size();

    // Generate random window position
    auto dxgiDevice = m_d3dDevice.as<IDXGIDevice>();
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

    std::uniform_int_distribution<int> distX(minX, maxX);
    std::uniform_int_distribution<int> distY(minY, maxY);
    auto x = distX(m_randomDevice);
    auto y = distY(m_randomDevice);

    // Capture screen with DDA
    winrt::com_ptr<ID3D11Texture2D> windowAreaTexture;
    {
        winrt::com_ptr<ID3D11Texture2D> captureTexture;
        // Starting with Windows 10 build 20348, we can use Windows.Graphics.Capture instead and disable the border.
        if (winrt::ApiInformation::IsPropertyPresent(winrt::name_of<winrt::GraphicsCaptureSession>(), L"IsBorderRequired"))
        {
            auto primaryMonitor = winrt::check_pointer(MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY));
            auto item = util::CreateCaptureItemForMonitor(primaryMonitor);
            auto itemSize = item.Size();
            auto device = CreateDirect3DDevice(m_d3dDevice.as<IDXGIDevice>().get());
            auto framePool = winrt::Direct3D11CaptureFramePool::CreateFreeThreaded(device, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, itemSize);
            auto session = framePool.CreateCaptureSession(item);
            session.IsCursorCaptureEnabled(false);
            session.IsBorderRequired(false);
            
            wil::shared_event captureEvent(wil::EventOptions::ManualReset);
            winrt::Direct3D11CaptureFrame frame{ nullptr };
            framePool.FrameArrived([&frame, captureEvent](auto&& framePool, auto&&)
                {
                    frame = framePool.TryGetNextFrame();
                    captureEvent.SetEvent();
                });

            session.StartCapture();
            captureEvent.wait();
            framePool.Close();
            session.Close();

            captureTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
        }
        else
        {
            auto output6 = dxgiOutput.as<IDXGIOutput6>();
            winrt::com_ptr<IDXGIOutputDuplication> duplication;
            winrt::check_hresult(output6->DuplicateOutput(m_d3dDevice.get(), duplication.put()));
            winrt::com_ptr<IDXGIResource> ddaResource;
            DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
            winrt::check_hresult(duplication->AcquireNextFrame(INFINITE, &frameInfo, ddaResource.put()));
            // Windows 10 build 19044 seems to have an issue where subsequent calls to DDA
            // can get an empty frame. As a workaround, always get the second frame.
            ddaResource = nullptr;
            winrt::check_hresult(duplication->ReleaseFrame());
            winrt::check_hresult(duplication->AcquireNextFrame(INFINITE, &frameInfo, ddaResource.put()));
            auto ddaTexture = ddaResource.as<ID3D11Texture2D>();
            captureTexture = util::CopyD3DTexture(m_d3dDevice, ddaTexture, false);
            winrt::check_hresult(duplication->ReleaseFrame());
        }

        // Cut out window area from screenshot
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = static_cast<uint32_t>(gifSize.Width);
        desc.Height = static_cast<uint32_t>(gifSize.Height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.SampleDesc.Count = 1;
        winrt::check_hresult(m_d3dDevice->CreateTexture2D(&desc, nullptr, windowAreaTexture.put()));
        D3D11_BOX region = {};
        region.left = x;
        region.right = x + gifSize.Width;
        region.top = y;
        region.bottom = y + gifSize.Height;
        region.back = 1;
        m_d3dContext->CopySubresourceRegion(windowAreaTexture.get(), 0, 0, 0, 0, captureTexture.get(), 0, &region);
    }

    // DEBUG: Remove later
    auto debugFilePath = std::filesystem::current_path();
    {
        std::stringstream fileNameStream;
        fileNameStream << "windowAreaTexture_" << gifSize.Width << "x" << gifSize.Height << ".bin";
        debugFilePath /= fileNameStream.str();
    }
    std::ofstream debugFile(debugFilePath, std::ios::out | std::ios::binary);
    auto debugBytes = util::CopyBytesFromTexture(windowAreaTexture);
    debugFile.write(reinterpret_cast<const char*>(debugBytes.data()), debugBytes.size());

    // Apply the window area texture
    m_shadeSurface.Resize(gifSize);
    {
        POINT point = {};
        auto dxgiSurface = util::SurfaceBeginDraw(m_shadeSurface, &point);
        auto shadeSurface = m_shadeSurface;
        auto endDraw = wil::scope_exit([shadeSurface]()
            {
                util::SurfaceEndDraw(shadeSurface);
            });
        auto destination = dxgiSurface.as<ID3D11Texture2D>();

        D3D11_BOX region = {};
        region.left = 0;
        region.right = gifSize.Width;
        region.top = 0;
        region.bottom = gifSize.Height;
        region.back = 1;
        m_d3dContext->CopySubresourceRegion(destination.get(), 0, point.x, point.y, 0, windowAreaTexture.get(), 0, &region);
    }
    m_leftShadeBrush.Offset({ 0.0f, 0.0f });
    m_rightShadeBrush.Offset({ static_cast<float>(gifSize.Width) / -2.0f, 0.0f });
    m_leftShadeVisual.RelativeOffsetAdjustment({ 0.0f, 0.0f, 0.0f });
    m_rightShadeVisual.RelativeOffsetAdjustment({ 0.5f, 0.0f, 0.0f });

    // Show window
    m_gifPlayer->Play();
    m_window->Show(x, y, gifSize);
    PlayShowAnimation(std::chrono::milliseconds(800));
}

winrt::fire_and_forget App::Rerun()
{
    std::uniform_int_distribution<int> dist(5000, 30000);
    auto delayInMilliseconds = dist(m_randomDevice);
    co_await std::chrono::milliseconds(delayInMilliseconds);
    co_await m_dispatcherQueue;
    CaptureAndAnimate();
}
