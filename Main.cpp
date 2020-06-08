#include "ComPtr.h"
#include "ErrorHandling.h"
#include "Random.h"
#include "ResourceUtils.h"
#include "ShaderUtils.h"

#include <windows.h>
#include <windowsx.h>

#include <DirectXMath.h>
#include <Ntsecapi.h>
#include <commctrl.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <filesystem>
#include <new>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dxguid.lib")

using namespace DirectX;

//#define INTERACTIVE

namespace gt
{

namespace
{

HRESULT SetD3DDebugObjectName(_In_ ID3D11DeviceChild* object, _In_z_ char const* name)
{
#ifdef _DEBUG
    return object->SetPrivateData(WKPDID_D3DDebugObjectName,
                                  static_cast<UINT>(strnlen_s(name, 255)), name);
#else
    return S_OK;
#endif
}

HRESULT SetD3DDebugObjectName(_In_ IDXGIObject* object, _In_z_ char const* name)
{
#ifdef _DEBUG
    return object->SetPrivateData(WKPDID_D3DDebugObjectName,
                                  static_cast<UINT>(strnlen_s(name, 255)), name);
#else
    return S_OK;
#endif
}

struct HideWindowScope
{
    explicit HideWindowScope(HWND hwnd)
    {
        bool const isVisible = GetWindowLongPtr(hwnd, GWL_STYLE) & WS_VISIBLE;
        if (!isVisible)
            return;

        if (GetLayeredWindowAttributes(hwnd, &keyColor, &alpha, &flags) &&
            SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA)) {
            DwmFlush();
            this->hwnd = hwnd;
        }
    }

    ~HideWindowScope()
    {
        if (hwnd)
            SetLayeredWindowAttributes(hwnd, keyColor, alpha, flags);
    }

private:
    HWND hwnd = nullptr;
    COLORREF keyColor = 0;
    BYTE alpha = 0;
    DWORD flags = 0;
};

HINSTANCE g_hinst;

class Window
{
public:
    HWND GetHWND() { return m_hwnd; }

protected:
    virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    virtual HRESULT PaintContent(PAINTSTRUCT* pps) { return S_OK; }
    virtual wchar_t const* ClassName() = 0;
    virtual BOOL WinRegisterClass(WNDCLASS* pwc) { return RegisterClass(pwc); }
    virtual ~Window() {}
    HWND WinCreateWindow(DWORD dwExStyle, LPCTSTR pszName, DWORD dwStyle, int x, int y,
                         int cx, int cy, HWND hwndParent, HMENU hmenu)
    {
        Register();
        return CreateWindowExW(dwExStyle, ClassName(), pszName, dwStyle, x, y, cx, cy,
                               hwndParent, hmenu, g_hinst, this);
    }

private:
    void Register();
    void OnPaint();
    void OnPrintClient(HDC hdc);
    static LRESULT CALLBACK s_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

protected:
    HWND m_hwnd = nullptr;
};

void Window::Register()
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = s_WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinst;
    wc.hIcon = nullptr;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>((COLOR_WINDOW + 1));
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = ClassName();
    WinRegisterClass(&wc);
}

LRESULT CALLBACK Window::s_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    Window* self;
    if (uMsg == WM_NCCREATE) {
        LPCREATESTRUCT lpcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
        self = reinterpret_cast<Window*>(lpcs->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(uMsg, wParam, lParam);
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
LRESULT Window::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LRESULT lres;
    switch (uMsg) {
    case WM_NCDESTROY:
        lres = DefWindowProcW(m_hwnd, uMsg, wParam, lParam);
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        delete this;
        return lres;
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_PRINTCLIENT:
        OnPrintClient(reinterpret_cast<HDC>(wParam));
        return 0;
    }
    return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
}

void Window::OnPaint()
{
    PAINTSTRUCT ps;
    BeginPaint(m_hwnd, &ps);
    PaintContent(&ps);
    EndPaint(m_hwnd, &ps);
}

void Window::OnPrintClient(HDC hdc)
{
    PAINTSTRUCT ps;
    ps.hdc = hdc;
    GetClientRect(m_hwnd, &ps.rcPaint);
    PaintContent(&ps);
}

struct RenderContext;

class IBehavior
{
public:
    virtual ~IBehavior() {};
    virtual void Update() = 0;
    virtual void OnRenderImage(RenderContext& rc, ID3D11DeviceContext* context,
                               unsigned frameCount, ID3D11ShaderResourceView* source,
                               ID3D11RenderTargetView* destination) = 0;
};

class DigitalGlitch;

struct RenderContext
{
    HRESULT Initialize(HWND hWnd);
    HRESULT CreateVertices();
    HRESULT InitPipeline();
    HRESULT Resize(unsigned newWidth, unsigned newHeight);

    HRESULT CreateRasterizerState();
    HRESULT CreateRenderTargetView();
    HRESULT CreateDepthBuffer(unsigned width, unsigned height);

    HRESULT SetupCapture(IDXGIFactory2* dxgiFactory);
    HRESULT RefreshCapture();
    HRESULT RenderFrame();
    HRESULT RenderSingleFrame(float intensity = 0.5f);
    HRESULT UpdateConstants();
    void UpdateViewport(float width, float height);

    struct Vertex
    {
        XMFLOAT2 Position;
        XMFLOAT2 TexCoords;
    };

    struct VSConstants
    {
        XMMATRIX Projection;
    };

    bool initialized = false;

    HWND window = nullptr;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11RenderTargetView> backBufferView;
    ComPtr<ID3D11InputLayout> inputLayout;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11Buffer> vertexBuffer;
    ComPtr<ID3D11Buffer> constantBuffer;

    // ComPtr<ID3D11Texture2D> depthStencilBuffer;
    // ComPtr<ID3D11DepthStencilState> depthStencilState;
    // ComPtr<ID3D11DepthStencilView> depthStencilView;
    // ComPtr<ID3D11BlendState> blendState;
    //
    ComPtr<ID3D11RasterizerState> rasterizerState;

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL(0);
    unsigned renderWidth = 0;
    unsigned renderHeight = 0;
    unsigned frameCount = 0;

    XMMATRIX orthoProjection{};

    struct CaptureItem
    {
        ComPtr<IDXGIOutput1> output;
        ComPtr<IDXGIOutputDuplication> outputDuplication;
        ComPtr<ID3D11Texture2D> snapshot;
        ComPtr<ID3D11ShaderResourceView> snapshotView;

        HRESULT Initialize(_In_ ID3D11Device* device, _In_opt_ IDXGIOutput* output);
        HRESULT SetupDuplication();
        HRESULT SetupDuplication(_In_ ID3D11Device* device);
        HRESULT Refresh();
    };

    std::vector<CaptureItem> captureItems;
    std::unique_ptr<DigitalGlitch> digitalGlitch;
};

float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

ComPtr<ID3D11DeviceContext> GetImmediateContext(ID3D11DeviceChild* deviceChild)
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    deviceChild->GetDevice(&device);
    device->GetImmediateContext(&context);
    return context;
}

template<typename T>
class ConstantBufferImpl : public T
{
public:
    HRESULT Create(ID3D11Device* device)
    {
        CD3D11_BUFFER_DESC const bufferDesc(sizeof(T), D3D11_BIND_CONSTANT_BUFFER,
                                            D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
        D3D11_SUBRESOURCE_DATA const data = {
            .pSysMem = static_cast<T const*>(this),
        };

        HR(device->CreateBuffer(&bufferDesc, &data, &buffer));
        return S_OK;
    }

    HRESULT Update()
    {
        ComPtr<ID3D11DeviceContext> context = GetImmediateContext(buffer);

        D3D11_MAPPED_SUBRESOURCE mapped;
        HR(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
        std::memcpy(mapped.pData, static_cast<T const*>(this), sizeof(T));
        context->Unmap(buffer, 0);
        return S_OK;
    }

    operator ID3D11Buffer*() const { return buffer; }

private:
    ComPtr<ID3D11Buffer> buffer;
};

class DigitalGlitch : public IBehavior
{
public:
    struct alignas(16) Constants
    {
        float intensity = 0.5f;
    };

    ConstantBufferImpl<Constants> constants;

    ComPtr<ID3D11PixelShader> pixelShader;

    ComPtr<ID3D11SamplerState> mainSamplerState;

    ComPtr<ID3D11Texture2D> noiseTexture;
    ComPtr<ID3D11ShaderResourceView> noiseTextureView;
    ComPtr<ID3D11SamplerState> noiseSamplerState;

    ComPtr<ID3D11Texture2D> trashFrame1Tex;
    ComPtr<ID3D11ShaderResourceView> trashFrame1View;
    ComPtr<ID3D11RenderTargetView> trashFrame1;
    ComPtr<ID3D11Texture2D> trashFrame2Tex;
    ComPtr<ID3D11ShaderResourceView> trashFrame2View;
    ComPtr<ID3D11RenderTargetView> trashFrame2;
    ComPtr<ID3D11SamplerState> trashSamplerState;

    HRESULT SetupResources(ID3D11Device* device, unsigned renderWidth,
                           unsigned renderHeight)
    {
        HR(constants.Create(device));

        CD3D11_TEXTURE2D_DESC noiseTextureDesc(DXGI_FORMAT_B8G8R8A8_UNORM, 64, 32);
        noiseTextureDesc.MipLevels = 1;
        noiseTextureDesc.Usage = D3D11_USAGE_DYNAMIC;
        noiseTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        HR(device->CreateTexture2D(&noiseTextureDesc, nullptr, &noiseTexture));
        HR(device->CreateShaderResourceView(noiseTexture, nullptr, &noiseTextureView));

        CD3D11_SAMPLER_DESC noiseSamplerDesc(D3D11_DEFAULT);
        noiseSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        noiseSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        noiseSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        noiseSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        HR(device->CreateSamplerState(&noiseSamplerDesc, &noiseSamplerState));

        CD3D11_TEXTURE2D_DESC trashFrameTextureDesc(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                    renderWidth, renderHeight);
        trashFrameTextureDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;

        HR(device->CreateTexture2D(&trashFrameTextureDesc, nullptr, &trashFrame1Tex));
        HR(device->CreateRenderTargetView(trashFrame1Tex, nullptr, &trashFrame1));
        HR(device->CreateShaderResourceView(trashFrame1Tex, nullptr, &trashFrame1View));

        HR(device->CreateTexture2D(&trashFrameTextureDesc, nullptr, &trashFrame2Tex));
        HR(device->CreateRenderTargetView(trashFrame2Tex, nullptr, &trashFrame2));
        HR(device->CreateShaderResourceView(trashFrame2Tex, nullptr, &trashFrame2View));

        CD3D11_SAMPLER_DESC trashSamplerDesc(D3D11_DEFAULT);
        HR(device->CreateSamplerState(&trashSamplerDesc, &trashSamplerState));

        CD3D11_SAMPLER_DESC mainSamplerDesc(D3D11_DEFAULT);
        HR(device->CreateSamplerState(&trashSamplerDesc, &mainSamplerState));

        UpdateNoiseTexture();

        auto const psBytecode =
            GetModuleResource(nullptr, L"SHADER", MAKEINTRESOURCEW(200));
        HR(device->CreatePixelShader(psBytecode.data(), psBytecode.size(), nullptr,
                                     &pixelShader));

        return S_OK;
    }

    void UpdateNoiseTexture()
    {
        ComPtr<ID3D11DeviceContext> context = GetImmediateContext(noiseTexture);

        D3D11_TEXTURE2D_DESC texDesc;
        noiseTexture->GetDesc(&texDesc);

        D3D11_MAPPED_SUBRESOURCE mapped;
        HRT(context->Map(noiseTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));

        uint32_t color = RandomColorBGRA();

        for (unsigned y = 0; y < texDesc.Height; ++y) {
            auto row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(mapped.pData) +
                                                   (y * mapped.RowPitch));

            for (unsigned x = 0; x < texDesc.Width; ++x) {
                if (RandomFloat() > 0.89f)
                    color = RandomColorBGRA();
                row[x] = color;
            }
        }

        context->Unmap(noiseTexture, 0);
    }

    void Update() override
    {
        if (RandomFloat() > Lerp(0.9f, 0.5f, constants.intensity)) {
            UpdateNoiseTexture();
        }

        constants.Update();
    }

    void OnRenderImage(RenderContext& rc, ID3D11DeviceContext* context,
                       unsigned frameCount, ID3D11ShaderResourceView* source,
                       ID3D11RenderTargetView* destination) override
    {
        // Update trash frames on a constant interval.
        // if (frameCount % 13 == 0)
        //    rc.Blit(source, _trashFrame1);
        // if (frameCount % 73 == 0)
        //    rc.Blit(source, _trashFrame2);

        ID3D11ShaderResourceView* trashFrame =
            RandomFloat() > 0.5f ? trashFrame1View : trashFrame2View;

        ID3D11Buffer* const constantBuffers[] = {
            constants,
        };
        ID3D11ShaderResourceView* const resources[] = {
            source,
            noiseTextureView,
            trashFrame,
        };
        ID3D11SamplerState* const samplers[] = {
            mainSamplerState,
            noiseSamplerState,
            trashSamplerState,
        };
        context->PSSetConstantBuffers(0, std::size(constantBuffers), constantBuffers);
        context->PSSetShaderResources(0, std::size(resources), resources);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->PSSetSamplers(0, std::size(samplers), samplers);
        context->OMSetRenderTargets(1, &destination, nullptr);

        context->Draw(3, 0);
    }
};

HRESULT RenderContext::CaptureItem::Initialize(_In_ ID3D11Device* device,
                                               _In_opt_ IDXGIOutput* newOutput)
{
    if (newOutput) {
        HR(output.QueryFrom(newOutput));
    }

    HR(SetupDuplication(device));

    return S_OK;
}

HRESULT RenderContext::CaptureItem::SetupDuplication()
{
    if (!snapshot)
        return E_UNEXPECTED;

    ComPtr<ID3D11Device> device;
    snapshot->GetDevice(&device);

    HR(SetupDuplication(device));
    return S_OK;
}

HRESULT RenderContext::CaptureItem::SetupDuplication(_In_ ID3D11Device* device)
{
    outputDuplication.Reset();
    snapshot.Reset();
    snapshotView.Reset();

    HR(output->DuplicateOutput(device, &outputDuplication));

    DXGI_OUTDUPL_DESC outduplDesc;
    outputDuplication->GetDesc(&outduplDesc);

    CD3D11_TEXTURE2D_DESC const snapshotTextureDesc(
        outduplDesc.ModeDesc.Format, outduplDesc.ModeDesc.Width,
        outduplDesc.ModeDesc.Height, 1, 1, D3D11_BIND_SHADER_RESOURCE,
        D3D11_USAGE_DEFAULT);

    HR(device->CreateTexture2D(&snapshotTextureDesc, nullptr, &snapshot));
    HR(device->CreateShaderResourceView(snapshot, nullptr, &snapshotView));

    return S_OK;
}

HRESULT RenderContext::CaptureItem::Refresh()
{
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> desktopResource;
    while (true) {
        HRESULT const hr =
            outputDuplication->AcquireNextFrame(1000, &frameInfo, &desktopResource);
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            HR(SetupDuplication());
            continue;
        }
        HR(hr);

        if (frameInfo.LastPresentTime.QuadPart != 0)
            break;

        outputDuplication->ReleaseFrame();
    }

    ComPtr<ID3D11Texture2D> desktopTexture;
    HR(desktopResource.As(&desktopTexture));

    ComPtr<ID3D11DeviceContext> context = GetImmediateContext(snapshot);
    context->CopyResource(snapshot, desktopTexture);

    HR(outputDuplication->ReleaseFrame());

    return S_OK;
}

HRESULT RenderContext::Initialize(HWND hWnd)
{
    if (initialized)
        return S_OK;

    window = hWnd;

    RECT clientArea;
    GetClientRect(window, &clientArea);
    renderWidth = clientArea.right - clientArea.left;
    renderHeight = clientArea.bottom - clientArea.top;

    UINT flags = D3D11_CREATE_DEVICE_DEBUG /*| D3D11_CREATE_DEVICE_DEBUGGABLE*/;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    HR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, featureLevels,
                         std::size(featureLevels), D3D11_SDK_VERSION, &device,
                         &featureLevel, &context));

    ComPtr<IDXGIDevice2> dxgiDevice;
    ComPtr<IDXGIAdapter> dxgiAdapter;
    ComPtr<IDXGIFactory2> dxgiFactory;
    HR(device.As(&dxgiDevice));
    HR(dxgiDevice->GetParent(COMPTR_PPV_ARGS(&dxgiAdapter)));
    HR(dxgiAdapter->GetParent(COMPTR_PPV_ARGS(&dxgiFactory)));

    HR(SetupCapture(dxgiFactory));
    HR(RefreshCapture());

    DXGI_SWAP_CHAIN_DESC1 const swapChainDesc = {
        .Width = renderWidth,
        .Height = renderHeight,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
        .Stereo = FALSE,
        .SampleDesc =
            {
                .Count = 1,
                .Quality = 0, // No multisampling
            },
        .BufferUsage = DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 2,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
        .Flags = 0,
    };

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC const swapChainFullscreenDesc = {
        .RefreshRate =
            {
                .Numerator = 0,
                .Denominator = 1,
            },
        .ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
        .Scaling = DXGI_MODE_SCALING_UNSPECIFIED,
        .Windowed = TRUE,
    };

    HR(dxgiFactory->CreateSwapChainForHwnd(
        device, window, &swapChainDesc, &swapChainFullscreenDesc, nullptr, &swapChain));

    HR(InitPipeline());

    HR(CreateVertices());
    HR(CreateRasterizerState());
    HR(CreateRenderTargetView());
    // HR(CreateDepthBuffer(renderWidth, renderHeight));

    UpdateViewport(renderWidth, renderHeight);

    auto digitalGlitch = std::make_unique<DigitalGlitch>();
    HR(digitalGlitch->SetupResources(device, renderWidth, renderHeight));
    this->digitalGlitch = std::move(digitalGlitch);

    initialized = true;
    return S_OK;
}

HRESULT RenderContext::CreateRenderTargetView()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    HR(swapChain->GetBuffer(0, COMPTR_PPV_ARGS(&backBuffer)));
    HR(device->CreateRenderTargetView(backBuffer, nullptr, &backBufferView));
    HR(SetD3DDebugObjectName(backBuffer, "Backbuffer"));
    HR(SetD3DDebugObjectName(backBufferView, "Backbuffer RTV"));
    return S_OK;
}

HRESULT RenderContext::Resize(unsigned newWidth, unsigned newHeight)
{
    if (!swapChain)
        return S_OK;

    newWidth = std::max(newWidth, 1u);
    newHeight = std::max(newHeight, 1u);

    backBufferView.Reset();
    // depthStencilBuffer.Reset();
    // depthStencilState.Reset();
    // depthStencilView.Reset();

    HR(swapChain->ResizeBuffers(0, newWidth, newHeight, DXGI_FORMAT_UNKNOWN,
                                DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));
    HR(CreateRenderTargetView());
    // HR(CreateDepthBuffer(newWidth, newHeight));

    UpdateViewport(static_cast<float>(newWidth), static_cast<float>(newHeight));

    renderWidth = newWidth;
    renderHeight = newHeight;

    CreateVertices();

    return S_OK;
}

void RenderContext::UpdateViewport(float width, float height)
{
    CD3D11_VIEWPORT viewport(0.0f, 0.0f, width, height);
    context->RSSetViewports(1, &viewport);

    orthoProjection =
        XMMatrixOrthographicOffCenterLH(0.0f, width, height, 0.0f, -1.0f, 1.0f);
}

HRESULT RenderContext::SetupCapture(IDXGIFactory2* dxgiFactory)
{
    ComPtr<IDXGIAdapter> dxgiAdapter;
    HR(dxgiFactory->EnumAdapters(0, &dxgiAdapter));

    ComPtr<IDXGIOutput> dxgiOutput;
    HR(dxgiAdapter->EnumOutputs(0, &dxgiOutput));

    CaptureItem& capture = captureItems.emplace_back();
    capture.Initialize(device, dxgiOutput);

    return S_OK;
}

HRESULT RenderContext::RefreshCapture()
{
    HideWindowScope hws(window);

    HRESULT hr = S_OK;
    for (auto& item : captureItems) {
        HRESULT hrItem = item.Refresh();
        if (hr == S_OK && FAILED(hrItem))
            hr = hrItem;
    }

    return hr;
}

float TriangleSeries(int index, int steps, float min, float max)
{
    return max - std::abs((max - min) * (2.0f * index / steps - 1.0f));
}

HRESULT RenderContext::RenderFrame()
{
    if (!initialized)
        return S_OK;

    int const frames = static_cast<int>(15 + RandomFloat() * 40) & ~1;
    for (int i = 0; i < frames; ++i) {
        if ((i % 10) == 9) {
            RefreshCapture();
        }

        float const intensity = TriangleSeries(i, frames, 0.0f, 0.75f);
        HR(RenderSingleFrame(intensity));
    }

    HR(RenderSingleFrame(0.0f));

    return S_OK;
}

HRESULT RenderContext::RenderSingleFrame(float intensity)
{
    float clearColor[4] = {};
    context->ClearRenderTargetView(backBufferView, clearColor);

    UpdateConstants();

    digitalGlitch->constants.intensity = intensity;
    digitalGlitch->Update();
    digitalGlitch->OnRenderImage(*this, context, frameCount, captureItems[0].snapshotView,
                                 backBufferView);
    // context->Draw(4, 0);

    HR(swapChain->Present(1, 0));
    ++frameCount;

    return S_OK;
}

HRESULT RenderContext::CreateDepthBuffer(unsigned width, unsigned height)
{
    /*
    D3D11_TEXTURE2D_DESC depthBufferDesc = {};
    depthBufferDesc.Width = width;
    depthBufferDesc.Height = height;
    depthBufferDesc.MipLevels = 1;
    depthBufferDesc.ArraySize = 1;
    depthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthBufferDesc.SampleDesc.Count = 1;
    depthBufferDesc.SampleDesc.Quality = 0;
    depthBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    depthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthBufferDesc.CPUAccessFlags = 0;
    depthBufferDesc.MiscFlags = 0;

    HR(device->CreateTexture2D(&depthBufferDesc, nullptr, &depthStencilBuffer));
    HR(SetD3DDebugObjectName(depthStencilBuffer, "Depth buffer"));

    D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
    depthStencilDesc.StencilEnable = FALSE;
    depthStencilDesc.StencilReadMask = 0xFF;
    depthStencilDesc.StencilWriteMask = 0xFF;
    depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
    depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
    depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

    HR(device->CreateDepthStencilState(&depthStencilDesc, &depthStencilState));
    HR(SetD3DDebugObjectName(depthStencilState, "Default DepthStencilState"));

    D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc = {};
    depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    depthStencilViewDesc.Texture2D.MipSlice = 0;

    HR(device->CreateDepthStencilView(depthStencilBuffer, &depthStencilViewDesc,
                                      &depthStencilView));
    HR(SetD3DDebugObjectName(depthStencilView, "Default DepthStencilView"));

    CD3D11_BLEND_DESC blendStateDesc(D3D11_DEFAULT);
    blendStateDesc.AlphaToCoverageEnable = FALSE;
    blendStateDesc.IndependentBlendEnable = FALSE;
    blendStateDesc.RenderTarget[0].BlendEnable = TRUE;
    blendStateDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blendStateDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendStateDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendStateDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendStateDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendStateDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendStateDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    for (UINT i = 1; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        blendStateDesc.RenderTarget[i] = blendStateDesc.RenderTarget[0];

    HR(device->CreateBlendState(&blendStateDesc, &blendState));
    HR(SetD3DDebugObjectName(blendState, "Premultiplied alpha"));

    context->OMSetDepthStencilState(depthStencilState, 1);
    context->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);
    context->OMSetRenderTargets(1, &backBufferView, nullptr);

    return S_OK;
    */
    return E_NOTIMPL;
}

HRESULT RenderContext::CreateRasterizerState()
{
    D3D11_RASTERIZER_DESC const rasterizerDesc = {
        .FillMode = D3D11_FILL_SOLID,
        .CullMode = D3D11_CULL_BACK,
        .FrontCounterClockwise = FALSE,
        .DepthBias = 0,
        .DepthBiasClamp = 0.0f,
        .SlopeScaledDepthBias = 0.0f,
        .DepthClipEnable = TRUE,
        .ScissorEnable = FALSE,
        .MultisampleEnable = FALSE,
        .AntialiasedLineEnable = FALSE,
    };

    HR(device->CreateRasterizerState(&rasterizerDesc, &rasterizerState));
    HR(SetD3DDebugObjectName(rasterizerState, "Solid/backculling Rasterizer"));

    context->RSSetState(rasterizerState);

    return S_OK;
}

HRESULT RenderContext::UpdateConstants()
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    HR(context->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));

    VSConstants* ptr = static_cast<VSConstants*>(mapped.pData);
    ptr->Projection = orthoProjection;

    context->Unmap(constantBuffer, 0);
    return S_OK;
}

//   <-----w----->
// ^ 2-----------0
// | | \         |
// | |   \       |
// h |     \     |
// | |       \   |
// | |         \ |
// v 3-----------1
void CreateFullscreenQuad(RenderContext::Vertex (&vertices)[4], float w, float h)
{
    vertices[0] = {XMFLOAT2(w, 0), XMFLOAT2(1, 0)};
    vertices[1] = {XMFLOAT2(w, h), XMFLOAT2(1, 1)};
    vertices[2] = {XMFLOAT2(0, 0), XMFLOAT2(0, 0)};
    vertices[3] = {XMFLOAT2(0, h), XMFLOAT2(0, 1)};
}

//   <--w-->
// ^ 0-----+-----1
// h |     |   /
// | |     | /
// v +-----/
//   |   /
//   | /
//   2
void CreateFullscreenTriangle(RenderContext::Vertex (&vertices)[3], float w, float h)
{
    vertices[0] = {XMFLOAT2(0, 0), XMFLOAT2(0, 0)};
    vertices[1] = {XMFLOAT2(w * 2, 0), XMFLOAT2(2, 0)};
    vertices[2] = {XMFLOAT2(0, h * 2), XMFLOAT2(0, 2)};
}

HRESULT RenderContext::CreateVertices()
{
    Vertex vertices[3];
    // CreateFullscreenQuad(vertices, renderWidth, renderHeight);
    CreateFullscreenTriangle(vertices, renderWidth, renderHeight);

    D3D11_BUFFER_DESC const vertexBufferDesc = {
        .ByteWidth = sizeof(Vertex) * std::size(vertices),
        .Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = D3D11_BIND_VERTEX_BUFFER,
        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
    };

    D3D11_SUBRESOURCE_DATA const vertexData = {
        .pSysMem = vertices,
    };

    HR(device->CreateBuffer(&vertexBufferDesc, &vertexData, &vertexBuffer));

    UINT const stride = sizeof(Vertex);
    UINT const offset = 0;
    context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    return S_OK;
}

HRESULT RenderContext::InitPipeline()
{
    auto const vsBytecode = GetModuleResource(nullptr, L"SHADER", MAKEINTRESOURCEW(100));

    D3D11_INPUT_ELEMENT_DESC const inputElementDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    HR(device->CreateInputLayout(inputElementDesc, 2, vsBytecode.data(),
                                 vsBytecode.size(), &inputLayout));
    HR(device->CreateVertexShader(vsBytecode.data(), vsBytecode.size(), nullptr,
                                  &vertexShader));

    context->IASetInputLayout(inputLayout);
    context->VSSetShader(vertexShader, nullptr, 0);

    {
        CD3D11_BUFFER_DESC const bufferDesc(sizeof(VSConstants),
                                            D3D11_BIND_CONSTANT_BUFFER,
                                            D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
        HR(device->CreateBuffer(&bufferDesc, nullptr, &constantBuffer));
        HR(SetD3DDebugObjectName(constantBuffer, "VSConstants"));
        context->VSSetConstantBuffers(0, 1, &constantBuffer);
    }

    return S_OK;
}

class RootWindow : public Window
{
    using base = Window;

public:
    static RootWindow* Create();
    wchar_t const* ClassName() override { return L"Scratch"; }

    HRESULT PaintContent(PAINTSTRUCT* pps) override;

protected:
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    LRESULT OnCreate();
    LRESULT OnSize(int x, int y);

    static constexpr unsigned GlitchTimerId = 1;
    void ScheduleGlitch();
    void OnTimer();
    void DoGlitch();

private:
    HWND m_hwndChild = nullptr;

    RenderContext rc;
};

LRESULT RootWindow::OnCreate()
{
    BOOL forceDisabled = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &forceDisabled,
                          sizeof(forceDisabled));

#ifndef INTERACTIVE
    MONITORINFO mi = {.cbSize = sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);

    SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOACTIVATE);
#endif

    HRESULT hr = rc.Initialize(m_hwnd);
    if (FAILED(hr))
        return -1;

#ifndef INTERACTIVE
    ScheduleGlitch();
#endif
    return 0;
}

LRESULT RootWindow::OnSize(int x, int y)
{
    if (m_hwndChild) {
        SetWindowPos(m_hwndChild, nullptr, 0, 0, x, y, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    rc.Resize(x, y);
    rc.RenderFrame();

    return 0;
}

void RootWindow::ScheduleGlitch()
{
    SetTimer(m_hwnd, GlitchTimerId, RandomInt(1000, 30000), nullptr);
}

void RootWindow::OnTimer()
{
    KillTimer(m_hwnd, GlitchTimerId);
    DoGlitch();
    ScheduleGlitch();
}

void RootWindow::DoGlitch()
{
    rc.RefreshCapture();

    SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    rc.RenderFrame();
    ShowWindow(m_hwnd, SW_HIDE);
}

LRESULT RootWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_CREATE:
        return OnCreate();
#ifndef INTERACTIVE
    case WM_NCCALCSIZE:
        return 0;
#endif
    case WM_NCDESTROY:
        // Death of the root window ends the thread
        PostQuitMessage(0);
        break;
    case WM_SIZE:
        return OnSize(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    case WM_SETFOCUS:
        if (m_hwndChild) {
            SetFocus(m_hwndChild);
        }
        return 0;
    case WM_TIMER:
        OnTimer();
        return 0;

#ifdef INTERACTIVE
    case WM_KEYDOWN:
        if (wParam == VK_F5) {
            // InvalidateRect(m_hwnd, nullptr, FALSE);
            rc.RenderFrame();
            return 0;
        }
        if (wParam == VK_F6) {
            // InvalidateRect(m_hwnd, nullptr, FALSE);
            rc.RenderSingleFrame();
            return 0;
        }
        if (wParam == VK_F7) {
            rc.RefreshCapture();
            rc.RenderSingleFrame();
            return 0;
        }
        break;
#endif
    }

    return base::HandleMessage(uMsg, wParam, lParam);
}

HRESULT RootWindow::PaintContent(PAINTSTRUCT* pps)
{
#ifdef INTERACTIVE
    rc.RenderFrame();
#endif
    return S_OK;
}

RootWindow* RootWindow::Create()
{
    auto self = new (std::nothrow) RootWindow();
    if (!self)
        return nullptr;

    DWORD exStyle = WS_EX_LAYERED;
#ifndef INTERACTIVE
    exStyle |= WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
#endif

    if (!self->WinCreateWindow(exStyle, L"", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr,
                               nullptr)) {
        DWORD const ec = GetLastError();
#ifdef INTERACTIVE
        MessageBoxW(nullptr, std::to_wstring(ec).c_str(), L"Failed to create window",
                    MB_OK);
#endif
        delete self;
        return nullptr;
    }

    SetLayeredWindowAttributes(self->m_hwnd, RGB(0xFF, 0, 0xFF), 0xFF, 0);
    return self;
}

} // namespace
} // namespace gt

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int nShowCmd)
{
    using namespace gt;

    g_hinst = hinst;
    if (FAILED(CoInitialize(nullptr)))
        return 1;

    InitCommonControls();
    RootWindow* prw = RootWindow::Create();
    if (prw) {
#ifdef INTERACTIVE
        ShowWindow(prw->GetHWND(), nShowCmd);
#endif
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    CoUninitialize();

    return 0;
}
