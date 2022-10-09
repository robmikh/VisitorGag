#pragma once

struct SoftwareGifFrame
{
    winrt::Windows::Graphics::Imaging::SoftwareBitmap Bitmap{ nullptr };
    winrt::Windows::Foundation::TimeSpan Delay{};
    winrt::Windows::Graphics::RectInt32 Rect{};
};

struct GifImage
{
    static std::future<std::unique_ptr<GifImage>> LoadAsync(winrt::Windows::Storage::Streams::IRandomAccessStream const& gifStream);

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
    CompositionGifPlayer(
        winrt::Windows::UI::Composition::Compositor const& compositor,
        winrt::Windows::UI::Composition::CompositionGraphicsDevice const& compGraphics,
        winrt::com_ptr<ID2D1Device> const& d2dDevice,
        winrt::com_ptr<ID3D11Device> const& d3dDevice,
        bool loop);

    winrt::Windows::UI::Composition::Visual Root() const noexcept { return m_visual; }
    winrt::Windows::Graphics::SizeInt32 Size() const noexcept { return { static_cast<int32_t>(m_image->Width()), static_cast<int32_t>(m_image->Height()) }; }

    void Play();
    void Stop();
    winrt::Windows::Foundation::IAsyncAction LoadGifAsync(winrt::Windows::Storage::Streams::IRandomAccessStream const& gifStream);

private:
    winrt::Windows::Foundation::TimeSpan DrawFrameToRenderTarget(size_t index, winrt::com_ptr<ID2D1DeviceContext> const& d2dContext);

    void OnTick(winrt::Windows::System::DispatcherQueueTimer const& timer, winrt::Windows::Foundation::IInspectable const& args);
    void UpdateSurface();

private:
    wil::critical_section m_lock = {};
    winrt::com_ptr<ID2D1Device> m_d2dDevice;
    winrt::com_ptr<ID2D1DeviceContext> m_d2dContext;
    winrt::com_ptr<ID2D1Bitmap1> m_d2dRenderTarget;
    winrt::com_ptr<ID3D11Texture2D> m_renderTargetTexture;
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
    winrt::Windows::UI::Composition::CompositionGraphicsDevice m_compGraphics{ nullptr };
    std::unique_ptr<GifImage> m_image;
    std::vector<winrt::com_ptr<ID2D1Bitmap>> m_frames;
    winrt::Windows::UI::Composition::SpriteVisual m_visual{ nullptr };
    winrt::Windows::UI::Composition::CompositionSurfaceBrush m_brush{ nullptr };
    winrt::Windows::UI::Composition::CompositionDrawingSurface m_surface{ nullptr };
    winrt::Windows::System::DispatcherQueueTimer m_timer{ nullptr };
    winrt::Windows::System::DispatcherQueueTimer::Tick_revoker m_tick;
    size_t m_currentIndex = 0;
    bool m_loop = false;
};