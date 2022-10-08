#include "pch.h"
#include "MainWindow.h"

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

float CLEARCOLOR[] = { 0.0f, 0.0f, 0.0f, 1.0f }; // RGBA

struct SoftwareGifFrame
{
    winrt::SoftwareBitmap Bitmap{ nullptr };
    winrt::TimeSpan Delay{};
    winrt::RectInt32 Rect{};
};

struct GifImage
{
    static std::future<std::unique_ptr<GifImage>> LoadAsync(winrt::IRandomAccessStream const& gifStream)
    {
        auto gifImage = std::make_unique<GifImage>();

        auto decoder = co_await winrt::BitmapDecoder::CreateAsync(gifStream);
        gifImage->m_width = decoder.PixelWidth();
        gifImage->m_height = decoder.PixelHeight();
        auto numFrames = decoder.FrameCount();
        gifImage->m_frames.reserve(numFrames);

        for (auto i = 0; i < numFrames; i++)
        {
            auto decodedFrame = co_await decoder.GetFrameAsync(i);

            // Get the bitmap
            auto bitmap = co_await decodedFrame.GetSoftwareBitmapAsync();
            auto pixelFormat = bitmap.BitmapPixelFormat();
            auto alphaMode = bitmap.BitmapAlphaMode();
            if (pixelFormat != winrt::BitmapPixelFormat::Bgra8 ||
                alphaMode != winrt::BitmapAlphaMode::Premultiplied)
            {
                auto convertedBitmap = winrt::SoftwareBitmap::Convert(
                    bitmap,
                    winrt::BitmapPixelFormat::Bgra8,
                    winrt::BitmapAlphaMode::Premultiplied);
                bitmap = convertedBitmap;
            }

            // Get the properties
            auto properties = co_await decodedFrame.BitmapProperties().GetPropertiesAsync(
                {
                    L"/grctlext/Delay",
                    L"/imgdesc/Left",
                    L"/imgdesc/Top",
                    L"/imgdesc/Width",
                    L"/imgdesc/Height",
                });
            auto gifRawDelay = winrt::unbox_value<uint16_t>(properties.Lookup(L"/grctlext/Delay").Value());
            auto left = winrt::unbox_value<uint16_t>(properties.Lookup(L"/imgdesc/Left").Value());
            auto top = winrt::unbox_value<uint16_t>(properties.Lookup(L"/imgdesc/Top").Value());
            auto frameWidth = winrt::unbox_value<uint16_t>(properties.Lookup(L"/imgdesc/Width").Value());
            auto frameHeight = winrt::unbox_value<uint16_t>(properties.Lookup(L"/imgdesc/Height").Value());

            // Originally stored in 10ms units
            auto delay = std::chrono::milliseconds(gifRawDelay * 10);
            auto rect = winrt::RectInt32
            {
                static_cast<int32_t>(left),
                static_cast<int32_t>(top),
                static_cast<int32_t>(frameWidth),
                static_cast<int32_t>(frameHeight),
            };

            gifImage->m_frames.push_back({ bitmap, delay, rect });
        }

        co_return gifImage;
    }

    GifImage() {}

    uint32_t Width() const noexcept { return m_width; }
    uint32_t Height() const noexcept { return m_height; }
    std::vector<SoftwareGifFrame> const& Frames() const noexcept { return m_frames; }

private:
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    std::vector<SoftwareGifFrame> m_frames;
};

struct CompositionGifPlayer
{
    CompositionGifPlayer(winrt::Compositor const& compositor, winrt::CompositionGraphicsDevice const& compGraphics)
    {
        m_compGraphics = compGraphics;

        m_visual = compositor.CreateSpriteVisual();
        m_brush = compositor.CreateSurfaceBrush();
        m_visual.Brush(m_brush);

        m_surface = m_compGraphics.CreateDrawingSurface2({ 1, 1 }, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, winrt::DirectXAlphaMode::Premultiplied);
        m_brush.Surface(m_surface);
    }

    winrt::Visual Root() const noexcept { return m_visual; }

    winrt::IAsyncAction LoadGifAsync(winrt::IRandomAccessStream const& gifStream)
    {
        auto currentQueue = winrt::DispatcherQueue::GetForCurrentThread();
        if (currentQueue == nullptr)
        {
            throw winrt::hresult_error(E_FAIL, L"Must be called from a thread with a Windows.System.DispatcherQueue");
        }

        auto image = co_await GifImage::LoadAsync(gifStream);
        co_await currentQueue;

        {
            auto lock = m_lock.lock();

            auto graphicsInterop = m_compGraphics.as<ABI::Windows::UI::Composition::ICompositionGraphicsDeviceInterop>();
            winrt::com_ptr<::IUnknown> renderingDevice;
            winrt::check_hresult(graphicsInterop->GetRenderingDevice(renderingDevice.put()));

            auto d3dDevice = renderingDevice.as<ID3D11Device>();
            auto d3dMultithread = renderingDevice.as<ID3D11Multithread>();
            winrt::com_ptr<ID3D11DeviceContext> d3dContext;
            d3dDevice->GetImmediateContext(d3dContext.put());

            if (m_timer != nullptr)
            {
                m_timer.Stop();
                m_timer = nullptr;
            }
            m_timer = currentQueue.CreateTimer();
            m_tick = m_timer.Tick(winrt::auto_revoke, { this, &CompositionGifPlayer::OnTick });

            m_image = std::move(image);
            auto frames = m_image->Frames();
            m_frames.clear();
            m_frames.reserve(frames.size());
            for (auto&& frame : frames)
            {
                auto frameWidth = static_cast<uint32_t>(frame.Rect.Width);
                auto frameHeight = static_cast<uint32_t>(frame.Rect.Height);

                auto bitmapBuffer = frame.Bitmap.LockBuffer(winrt::Windows::Graphics::Imaging::BitmapBufferAccessMode::Read);
                auto reference = bitmapBuffer.CreateReference();
                auto byteAccess = reference.as<::Windows::Foundation::IMemoryBufferByteAccess>();
                uint8_t* bytes = nullptr;
                uint32_t size = 0;
                winrt::check_hresult(byteAccess->GetBuffer(&bytes, &size));

                auto texture = robmikh::common::uwp::CreateTextureFromRawBytes(
                    d3dDevice,
                    bytes,
                    frameWidth,
                    frameHeight,
                    DXGI_FORMAT_B8G8R8A8_UNORM);
                m_frames.push_back(texture);
            }

            m_visual.Size({ static_cast<float>(m_image->Width()), static_cast<float>(m_image->Height()) });
            m_surface.Resize({ static_cast<int32_t>(m_image->Width()), static_cast<int32_t>(m_image->Height()) });
            {
                POINT point = {};
                auto dxgiSurface = util::SurfaceBeginDraw(m_surface, &point);
                auto destination = dxgiSurface.as<ID3D11Texture2D>();

                winrt::com_ptr<ID3D11RenderTargetView> renderTargetView;
                winrt::check_hresult(d3dDevice->CreateRenderTargetView(destination.get(), nullptr, renderTargetView.put()));

                auto d3dLock = util::D3D11DeviceLock(d3dMultithread.get());

                d3dContext->ClearRenderTargetView(renderTargetView.get(), CLEARCOLOR);
                if (!m_frames.empty())
                {
                    auto delay = DrawFrame(0, destination, d3dContext);
                    m_timer.Interval(delay);
                    m_timer.Start();
                }
                m_currentIndex = 0;
            }
        }

        co_return;
    }

private:
    winrt::TimeSpan DrawFrame(size_t index, winrt::com_ptr<ID3D11Texture2D> const& destination, winrt::com_ptr<ID3D11DeviceContext> const& d3dContext)
    {
        auto frames = m_image->Frames();
        auto& frame = frames[index];
        auto& frameTexture = m_frames[index];

        D3D11_BOX region = {};
        region.left = 0;
        region.right = frame.Rect.Width;
        region.top = 0;
        region.bottom = frame.Rect.Height;
        region.back = 1;

        d3dContext->CopySubresourceRegion(destination.get(), 0, frame.Rect.X, frame.Rect.Y, 0, frameTexture.get(), 0, &region);
        return frame.Delay;
    }

    void OnTick(winrt::DispatcherQueueTimer const& timer, winrt::IInspectable const&)
    {
        auto lock = m_lock.lock();

        auto graphicsInterop = m_compGraphics.as<ABI::Windows::UI::Composition::ICompositionGraphicsDeviceInterop>();
        winrt::com_ptr<::IUnknown> renderingDevice;
        winrt::check_hresult(graphicsInterop->GetRenderingDevice(renderingDevice.put()));

        auto d3dDevice = renderingDevice.as<ID3D11Device>();
        auto d3dMultithread = renderingDevice.as<ID3D11Multithread>();
        winrt::com_ptr<ID3D11DeviceContext> d3dContext;
        d3dDevice->GetImmediateContext(d3dContext.put());

        POINT point = {};
        auto dxgiSurface = util::SurfaceBeginDraw(m_surface, &point);
        auto endDraw = wil::scope_exit([&]()
            {
                util::SurfaceEndDraw(m_surface);
            });
        auto destination = dxgiSurface.as<ID3D11Texture2D>();

        winrt::com_ptr<ID3D11RenderTargetView> renderTargetView;
        winrt::check_hresult(d3dDevice->CreateRenderTargetView(destination.get(), nullptr, renderTargetView.put()));

        auto d3dLock = util::D3D11DeviceLock(d3dMultithread.get());
        m_currentIndex = (m_currentIndex + 1) % m_frames.size();
        if (m_currentIndex == 0)
        {
            d3dContext->ClearRenderTargetView(renderTargetView.get(), CLEARCOLOR);
        }
        auto delay = DrawFrame(m_currentIndex, destination, d3dContext);
        m_timer.Interval(delay);
        m_timer.Start();
    }

private:
    wil::critical_section m_lock = {};
    winrt::CompositionGraphicsDevice m_compGraphics{ nullptr };
    std::unique_ptr<GifImage> m_image;
    std::vector<winrt::com_ptr<ID3D11Texture2D>> m_frames;
    winrt::SpriteVisual m_visual{ nullptr };
    winrt::CompositionSurfaceBrush m_brush{ nullptr };
    winrt::CompositionDrawingSurface m_surface{ nullptr };
    winrt::DispatcherQueueTimer m_timer{ nullptr };
    winrt::DispatcherQueueTimer::Tick_revoker m_tick;
    size_t m_currentIndex = 0;
};

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
    root.Brush(compositor.CreateColorBrush(winrt::Colors::White()));
    target.Root(root);

    // Init D3D
    auto d3dDevice = util::CreateD3DDevice();
    auto compGraphics = util::CreateCompositionGraphicsDevice(compositor, d3dDevice.get());

    // Create the gif player
    auto gifPlayer = CompositionGifPlayer(compositor, compGraphics);
    auto gifVisual = gifPlayer.Root();
    gifVisual.AnchorPoint({ 0.5f, 0.5f });
    gifVisual.RelativeOffsetAdjustment({ 0.5f, 0.5f, 0.0f });
    root.Children().InsertAtTop(gifVisual);
    // Run the rest of our initialization asynchronously on the DispatcherQueue
    auto queue = controller.DispatcherQueue();
    auto windowHandle = window.m_window;
    queue.TryEnqueue([windowHandle, &gifPlayer]() -> winrt::fire_and_forget
        {
            auto&& player = gifPlayer;

            // Load a gif file
            auto file = co_await OpenGifFileAsync(windowHandle);
            auto stream = co_await file.OpenReadAsync();
            co_await player.LoadGifAsync(stream);
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

