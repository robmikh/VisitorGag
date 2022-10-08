#include "pch.h"
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

winrt::com_ptr<ID2D1Bitmap1> CreateBitmapFromTexture(
    winrt::com_ptr<ID3D11Texture2D> const& texture,
    winrt::com_ptr<ID2D1DeviceContext> const& d2dContext)
{
    auto dxgiSurface = texture.as<IDXGISurface>();
    winrt::com_ptr<ID2D1Bitmap1> bitmap;
    winrt::check_hresult(d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.get(), nullptr, bitmap.put()));
    return bitmap;
}

std::future<std::unique_ptr<GifImage>> GifImage::LoadAsync(winrt::IRandomAccessStream const& gifStream)
{
    auto gifImage = std::make_unique<GifImage>();

    auto decoder = co_await winrt::BitmapDecoder::CreateAsync(gifStream);
    gifImage->m_width = decoder.PixelWidth();
    gifImage->m_height = decoder.PixelHeight();
    auto numFrames = decoder.FrameCount();
    if (numFrames == 0)
    {
        throw winrt::hresult_error(E_FAIL, L"Gifs with zero frames are not supported");
    }
    gifImage->m_frames.reserve(numFrames);

    for (uint32_t i = 0; i < numFrames; i++)
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

CompositionGifPlayer::CompositionGifPlayer(winrt::Compositor const& compositor, winrt::CompositionGraphicsDevice const& compGraphics, winrt::com_ptr<ID2D1Device> const& d2dDevice, winrt::com_ptr<ID3D11Device> const& d3dDevice)
{
    m_compGraphics = compGraphics;
    m_d2dDevice = d2dDevice;
    m_d3dDevice = d3dDevice;
    winrt::check_hresult(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, m_d2dContext.put()));

    m_visual = compositor.CreateSpriteVisual();
    m_brush = compositor.CreateSurfaceBrush();
    m_visual.Brush(m_brush);

    m_surface = m_compGraphics.CreateDrawingSurface2({ 1, 1 }, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, winrt::DirectXAlphaMode::Premultiplied);
    m_brush.Surface(m_surface);
}

void CompositionGifPlayer::Play()
{
    auto lock = m_lock.lock();

    if (!m_frames.empty())
    {
        m_timer.Start();
    }
}

void CompositionGifPlayer::Stop()
{
    auto lock = m_lock.lock();
    if (m_timer != nullptr)
    {
        m_timer.Stop();
    }
}

winrt::IAsyncAction CompositionGifPlayer::LoadGifAsync(winrt::IRandomAccessStream const& gifStream)
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

        auto d3dMultithread = m_d3dDevice.as<ID3D11Multithread>();
        winrt::com_ptr<ID3D11DeviceContext> d3dContext;
        m_d3dDevice->GetImmediateContext(d3dContext.put());

        if (m_timer != nullptr)
        {
            m_timer.Stop();
            m_timer = nullptr;
        }
        m_timer = currentQueue.CreateTimer();
        m_timer.IsRepeating(false);
        m_tick = m_timer.Tick(winrt::auto_revoke, { this, &CompositionGifPlayer::OnTick });

        m_image = std::move(image);

        if (m_d2dRenderTarget != nullptr)
        {
            m_d2dContext->SetTarget(nullptr);
            m_d2dRenderTarget = nullptr;
        }
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = m_image->Width();
        desc.Height = m_image->Width();
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.SampleDesc.Count = 1;
        winrt::com_ptr<ID3D11Texture2D> renderTargetTexture;
        winrt::check_hresult(m_d3dDevice->CreateTexture2D(&desc, nullptr, renderTargetTexture.put()));
        m_d2dRenderTarget = CreateBitmapFromTexture(renderTargetTexture, m_d2dContext);
        m_d2dContext->SetTarget(m_d2dRenderTarget.get());

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
                m_d3dDevice,
                bytes,
                frameWidth,
                frameHeight,
                DXGI_FORMAT_B8G8R8A8_UNORM);
            m_frames.push_back(CreateBitmapFromTexture(texture, m_d2dContext));
        }

        m_visual.Size({ static_cast<float>(m_image->Width()), static_cast<float>(m_image->Height()) });
        m_surface.Resize({ static_cast<int32_t>(m_image->Width()), static_cast<int32_t>(m_image->Height()) });
        {
            auto d3dLock = util::D3D11DeviceLock(d3dMultithread.get());
            winrt::TimeSpan delay = {};
            {
                m_d2dContext->BeginDraw();
                auto endDraw = wil::scope_exit([&]()
                    {
                        winrt::check_hresult(m_d2dContext->EndDraw());
                    });

                m_d2dContext->Clear(D2D1_COLOR_F{ 0.0f, 0.0f, 0.0f, 1.0f });
                WINRT_ASSERT(!m_frames.empty());
                delay = DrawFrameToRenderTarget(0, m_d2dContext);
            }
            if (delay.count() == 0)
            {
                delay = std::chrono::milliseconds(100);
            }

            auto surfaceContext = util::SurfaceContext(m_surface);
            auto surfaceD2DContext = surfaceContext.GetDeviceContext();

            surfaceD2DContext->Clear(D2D1_COLOR_F{ 0.0f, 0.0f, 0.0f, 1.0f });
            WINRT_ASSERT(!m_frames.empty());
            surfaceD2DContext->DrawImage(m_d2dRenderTarget.get());
            m_timer.Interval(delay);
            m_currentIndex = 0;
        }
    }

    co_return;
}

winrt::TimeSpan CompositionGifPlayer::DrawFrameToRenderTarget(size_t index, winrt::com_ptr<ID2D1DeviceContext> const& d2dContext)
{
    auto frames = m_image->Frames();
    auto& frame = frames[index];
    auto& frameTexture = m_frames[index];

    d2dContext->DrawImage(frameTexture.get(), { static_cast<float>(frame.Rect.X), static_cast<float>(frame.Rect.Y) });
    return frame.Delay;
}

void CompositionGifPlayer::OnTick(winrt::DispatcherQueueTimer const&, winrt::IInspectable const&)
{
    auto lock = m_lock.lock();
    m_currentIndex = (m_currentIndex + 1) % m_frames.size();

    auto d3dMultithread = m_d3dDevice.as<ID3D11Multithread>();

    winrt::TimeSpan delay = {};
    {
        m_d2dContext->BeginDraw();
        auto endDraw = wil::scope_exit([&]()
            {
                winrt::check_hresult(m_d2dContext->EndDraw());
            });

        if (m_currentIndex == 0)
        {
            m_d2dContext->Clear(D2D1_COLOR_F{ 0.0f, 0.0f, 0.0f, 1.0f });
        }
        if (!m_frames.empty())
        {
            delay = DrawFrameToRenderTarget(m_currentIndex, m_d2dContext);
        }
    }
    if (delay.count() == 0)
    {
        delay = std::chrono::milliseconds(100);
    }

    {
        auto surfaceContext = util::SurfaceContext(m_surface);
        auto surfaceD2DContext = surfaceContext.GetDeviceContext();

        surfaceD2DContext->Clear(D2D1_COLOR_F{ 0.0f, 0.0f, 0.0f, 1.0f });
        surfaceD2DContext->DrawImage(m_d2dRenderTarget.get());
    }
    m_timer.Interval(delay);
    m_timer.Start();
}