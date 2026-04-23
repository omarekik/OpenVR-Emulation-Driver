#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include <openvr.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <string>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr vr::EVREye kPreviewEye = vr::Eye_Left;
    constexpr wchar_t kWindowClassName[] = L"SteamVRMirrorPreviewWindow";
    constexpr wchar_t kWindowTitle[] = L"SteamVR Mirror Preview";
    constexpr UINT kInitialClientWidth = 1280;
    constexpr UINT kInitialClientHeight = 720;

    std::wstring ToWide(const char *text)
    {
        if (text == nullptr || text[0] == '\0') {
            return L"";
        }

        int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (length <= 0) {
            return L"";
        }

        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), length);
        if (!wide.empty() && wide.back() == L'\0') {
            wide.pop_back();
        }
        return wide;
    }

    std::wstring HrToString(HRESULT hr)
    {
        wchar_t *buffer = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        DWORD length = FormatMessageW(flags, nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
        std::wstring message = length > 0 && buffer != nullptr ? std::wstring(buffer, length) : L"Unknown error";
        if (buffer != nullptr) {
            LocalFree(buffer);
        }

        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
            message.pop_back();
        }
        return message;
    }

    std::wstring CompositorErrorToString(vr::EVRCompositorError error)
    {
        switch (error) {
        case vr::VRCompositorError_None:
            return L"OK";
        case vr::VRCompositorError_RequestFailed:
            return L"RequestFailed";
        case vr::VRCompositorError_IncompatibleVersion:
            return L"IncompatibleVersion";
        case vr::VRCompositorError_DoNotHaveFocus:
            return L"DoNotHaveFocus";
        case vr::VRCompositorError_InvalidTexture:
            return L"InvalidTexture";
        case vr::VRCompositorError_IsNotSceneApplication:
            return L"IsNotSceneApplication";
        case vr::VRCompositorError_TextureIsOnWrongDevice:
            return L"TextureIsOnWrongDevice";
        case vr::VRCompositorError_TextureUsesUnsupportedFormat:
            return L"TextureUsesUnsupportedFormat";
        case vr::VRCompositorError_SharedTexturesNotSupported:
            return L"SharedTexturesNotSupported";
        case vr::VRCompositorError_IndexOutOfRange:
            return L"IndexOutOfRange";
        case vr::VRCompositorError_AlreadySubmitted:
            return L"AlreadySubmitted";
        case vr::VRCompositorError_InvalidBounds:
            return L"InvalidBounds";
        default:
            return L"Unknown compositor error";
        }
    }

    class PreviewApp
    {
    public:
        int Run(HINSTANCE instance, int show_command)
        {
            instance_ = instance;

            if (!CreateMainWindow(show_command)) {
                return 1;
            }

            if (!InitializeOpenVR()) {
                Shutdown();
                return 1;
            }

            if (!InitializeD3D()) {
                Shutdown();
                return 1;
            }

            UpdateWindowTitle(L"Waiting for a scene app to submit frames...");

            MSG message = {};
            while (running_) {
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message == WM_QUIT) {
                        running_ = false;
                        break;
                    }

                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }

                if (!running_) {
                    break;
                }

                if (is_minimized_) {
                    Sleep(16);
                    continue;
                }

                RenderFrame();
            }

            Shutdown();
            return 0;
        }

    private:
        bool CreateMainWindow(int show_command)
        {
            WNDCLASSEXW window_class = {};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = &PreviewApp::StaticWindowProc;
            window_class.hInstance = instance_;
            window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            window_class.lpszClassName = kWindowClassName;

            if (RegisterClassExW(&window_class) == 0) {
                ShowFatalError(L"Failed to register window class.");
                return false;
            }

            RECT window_rect = { 0, 0, static_cast<LONG>(kInitialClientWidth), static_cast<LONG>(kInitialClientHeight) };
            AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);

            hwnd_ = CreateWindowExW(
                0,
                kWindowClassName,
                kWindowTitle,
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                window_rect.right - window_rect.left,
                window_rect.bottom - window_rect.top,
                nullptr,
                nullptr,
                instance_,
                this);

            if (hwnd_ == nullptr) {
                ShowFatalError(L"Failed to create preview window.");
                return false;
            }

            ShowWindow(hwnd_, show_command);
            UpdateWindow(hwnd_);
            return true;
        }

        bool InitializeOpenVR()
        {
            vr::EVRInitError init_error = vr::VRInitError_None;
            vr_system_ = vr::VR_Init(&init_error, vr::VRApplication_Background);
            if (init_error != vr::VRInitError_None || vr_system_ == nullptr) {
                std::wstring message = L"OpenVR initialization failed:\n";
                message += ToWide(vr::VR_GetVRInitErrorAsEnglishDescription(init_error));
                ShowFatalError(message);
                return false;
            }

            vr_compositor_ = vr::VRCompositor();
            if (vr_compositor_ == nullptr) {
                ShowFatalError(L"SteamVR compositor is not available.");
                return false;
            }

            return true;
        }

        bool InitializeD3D()
        {
            HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory_));
            if (FAILED(hr)) {
                ShowFatalError(L"CreateDXGIFactory2 failed:\n" + HrToString(hr));
                return false;
            }

            ComPtr<IDXGIAdapter1> adapter;
            int32_t adapter_index = -1;
            vr_system_->GetDXGIOutputInfo(&adapter_index);
            if (adapter_index >= 0) {
                hr = factory_->EnumAdapters1(static_cast<UINT>(adapter_index), &adapter);
                if (FAILED(hr)) {
                    ShowFatalError(L"Failed to locate the graphics adapter SteamVR is using:\n" + HrToString(hr));
                    return false;
                }
            }

            UINT creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
            const D3D_FEATURE_LEVEL requested_levels[] = {
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
            };

            D3D_FEATURE_LEVEL created_level = D3D_FEATURE_LEVEL_11_0;
            hr = D3D11CreateDevice(
                adapter.Get(),
                adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                creation_flags,
                requested_levels,
                static_cast<UINT>(std::size(requested_levels)),
                D3D11_SDK_VERSION,
                &device_,
                &created_level,
                &context_);

            if (FAILED(hr)) {
                ShowFatalError(L"D3D11CreateDevice failed:\n" + HrToString(hr));
                return false;
            }

            DetectTearingSupport();

            if (!CreateSwapChainResources(kInitialClientWidth, kInitialClientHeight)) {
                return false;
            }

            if (!CreateShaders()) {
                return false;
            }

            return CreateSampler();
        }

        bool CreateSwapChainResources(UINT width, UINT height)
        {
            width = std::max(width, 1u);
            height = std::max(height, 1u);

            if (!swap_chain_) {
                DXGI_SWAP_CHAIN_DESC1 desc = {};
                desc.Width = width;
                desc.Height = height;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.BufferCount = 2;
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                desc.Scaling = DXGI_SCALING_STRETCH;
                desc.Flags = SwapChainFlags();

                HRESULT hr = factory_->CreateSwapChainForHwnd(device_.Get(), hwnd_, &desc, nullptr, nullptr, &swap_chain_);
                if (FAILED(hr)) {
                    ShowFatalError(L"CreateSwapChainForHwnd failed:\n" + HrToString(hr));
                    return false;
                }

                factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
                ConfigureSwapChainLatency();
            } else {
                context_->OMSetRenderTargets(0, nullptr, nullptr);
                backbuffer_rtv_.Reset();
                context_->Flush();

                HRESULT hr = swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, SwapChainFlags());
                if (FAILED(hr)) {
                    ShowFatalError(L"ResizeBuffers failed:\n" + HrToString(hr));
                    return false;
                }
            }

            ComPtr<ID3D11Texture2D> backbuffer;
            HRESULT hr = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
            if (FAILED(hr)) {
                ShowFatalError(L"Failed to get the swap chain backbuffer:\n" + HrToString(hr));
                return false;
            }

            hr = device_->CreateRenderTargetView(backbuffer.Get(), nullptr, &backbuffer_rtv_);
            if (FAILED(hr)) {
                ShowFatalError(L"CreateRenderTargetView failed:\n" + HrToString(hr));
                return false;
            }

            client_width_ = width;
            client_height_ = height;
            return true;
        }

        void DetectTearingSupport()
        {
            ComPtr<IDXGIFactory5> factory5;
            if (FAILED(factory_.As(&factory5))) {
                return;
            }

            BOOL allow_tearing = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(
                    DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                    &allow_tearing,
                    sizeof(allow_tearing)))) {
                allow_tearing_ = allow_tearing == TRUE;
            }
        }

        UINT SwapChainFlags() const
        {
            return allow_tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        }

        void ConfigureSwapChainLatency()
        {
            ComPtr<IDXGISwapChain2> swap_chain2;
            if (SUCCEEDED(swap_chain_.As(&swap_chain2))) {
                swap_chain2->SetMaximumFrameLatency(1);
            }
        }

        bool CreateShaders()
        {
            static constexpr char vertex_shader_source[] =
                "struct VSOut {\n"
                "    float4 position : SV_Position;\n"
                "    float2 uv : TEXCOORD0;\n"
                "};\n"
                "VSOut main(uint vertexId : SV_VertexID) {\n"
                "    float2 positions[4] = {\n"
                "        float2(-1.0f, -1.0f),\n"
                "        float2(-1.0f,  1.0f),\n"
                "        float2( 1.0f, -1.0f),\n"
                "        float2( 1.0f,  1.0f)\n"
                "    };\n"
                "    float2 uvs[4] = {\n"
                "        float2(0.0f, 1.0f),\n"
                "        float2(0.0f, 0.0f),\n"
                "        float2(1.0f, 1.0f),\n"
                "        float2(1.0f, 0.0f)\n"
                "    };\n"
                "    VSOut output;\n"
                "    output.position = float4(positions[vertexId], 0.0f, 1.0f);\n"
                "    output.uv = uvs[vertexId];\n"
                "    return output;\n"
                "}\n";

            static constexpr char pixel_shader_source[] =
                "Texture2D mirrorTexture : register(t0);\n"
                "SamplerState mirrorSampler : register(s0);\n"
                "struct VSOut {\n"
                "    float4 position : SV_Position;\n"
                "    float2 uv : TEXCOORD0;\n"
                "};\n"
                "float4 main(VSOut input) : SV_Target {\n"
                "    float4 color = mirrorTexture.Sample(mirrorSampler, input.uv);\n"
                "    color.rgb = pow(saturate(color.rgb), 1.0f / 2.2f);\n"
                "    color.a = 1.0f;\n"
                "    return color;\n"
                "}\n";

            ComPtr<ID3DBlob> vertex_blob;
            if (!CompileShader(vertex_shader_source, "main", "vs_5_0", &vertex_blob)) {
                return false;
            }

            HRESULT hr = device_->CreateVertexShader(
                vertex_blob->GetBufferPointer(),
                vertex_blob->GetBufferSize(),
                nullptr,
                &vertex_shader_);
            if (FAILED(hr)) {
                ShowFatalError(L"CreateVertexShader failed:\n" + HrToString(hr));
                return false;
            }

            ComPtr<ID3DBlob> pixel_blob;
            if (!CompileShader(pixel_shader_source, "main", "ps_5_0", &pixel_blob)) {
                return false;
            }

            hr = device_->CreatePixelShader(
                pixel_blob->GetBufferPointer(),
                pixel_blob->GetBufferSize(),
                nullptr,
                &pixel_shader_);
            if (FAILED(hr)) {
                ShowFatalError(L"CreatePixelShader failed:\n" + HrToString(hr));
                return false;
            }

            return true;
        }

        bool CompileShader(const char *source, const char *entry_point, const char *target, ID3DBlob **out_blob)
        {
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
            ComPtr<ID3DBlob> shader_blob;
            ComPtr<ID3DBlob> error_blob;
            HRESULT hr = D3DCompile(
                source,
                strlen(source),
                nullptr,
                nullptr,
                nullptr,
                entry_point,
                target,
                flags,
                0,
                &shader_blob,
                &error_blob);

            if (FAILED(hr)) {
                std::wstring message = L"Shader compilation failed.";
                if (error_blob) {
                    message += L"\n";
                    message += ToWide(static_cast<const char *>(error_blob->GetBufferPointer()));
                }
                ShowFatalError(message);
                return false;
            }

            *out_blob = shader_blob.Detach();
            return true;
        }

        bool CreateSampler()
        {
            D3D11_SAMPLER_DESC sampler_desc = {};
            sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

            HRESULT hr = device_->CreateSamplerState(&sampler_desc, &sampler_state_);
            if (FAILED(hr)) {
                ShowFatalError(L"CreateSamplerState failed:\n" + HrToString(hr));
                return false;
            }

            return true;
        }

        void RenderFrame()
        {
            if (!backbuffer_rtv_) {
                return;
            }

            const float clear_color[4] = { 0.02f, 0.02f, 0.025f, 1.0f };
            context_->OMSetRenderTargets(1, backbuffer_rtv_.GetAddressOf(), nullptr);
            context_->ClearRenderTargetView(backbuffer_rtv_.Get(), clear_color);

            vr::EVRCompositorError mirror_error = EnsureMirrorTexture();
            if (mirror_error == vr::VRCompositorError_None && mirror_srv_ != nullptr) {
                RenderMirrorTexture(mirror_srv_);

                if (last_status_ != L"streaming") {
                    UpdateWindowTitle(
                        std::to_wstring(mirror_width_) +
                        L"x" +
                        std::to_wstring(mirror_height_));
                    last_status_ = L"streaming";
                }
            } else {
                std::wstring status = mirror_error == vr::VRCompositorError_None
                    ? L"waiting-for-frames"
                    : L"mirror-error:" + CompositorErrorToString(mirror_error);
                if (last_status_ != status) {
                    UpdateWindowTitle(
                        mirror_error == vr::VRCompositorError_None
                            ? L"Waiting for a scene app to submit frames..."
                            : CompositorErrorToString(mirror_error));
                    last_status_ = status;
                }
            }

            swap_chain_->Present(0, allow_tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
            vr_compositor_->PostPresentHandoff();
        }

        vr::EVRCompositorError EnsureMirrorTexture()
        {
            if (mirror_srv_ != nullptr) {
                return vr::VRCompositorError_None;
            }

            return vr_compositor_->GetMirrorTextureD3D11(
                kPreviewEye,
                device_.Get(),
                reinterpret_cast<void **>(&mirror_srv_));
        }

        void ReleaseMirrorTexture()
        {
            if (mirror_srv_ != nullptr && vr_compositor_ != nullptr) {
                vr_compositor_->ReleaseMirrorTextureD3D11(mirror_srv_);
                mirror_srv_ = nullptr;
            }
        }

        void RenderMirrorTexture(ID3D11ShaderResourceView *mirror_srv)
        {
            ComPtr<ID3D11Resource> resource;
            mirror_srv->GetResource(&resource);

            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(resource.As(&texture))) {
                return;
            }

            D3D11_TEXTURE2D_DESC texture_desc = {};
            texture->GetDesc(&texture_desc);
            mirror_width_ = texture_desc.Width;
            mirror_height_ = texture_desc.Height;

            D3D11_VIEWPORT viewport = CalculateViewport(client_width_, client_height_, mirror_width_, mirror_height_);

            context_->RSSetViewports(1, &viewport);
            context_->IASetInputLayout(nullptr);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
            context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
            context_->PSSetSamplers(0, 1, sampler_state_.GetAddressOf());
            context_->PSSetShaderResources(0, 1, &mirror_srv);
            context_->Draw(4, 0);

            ID3D11ShaderResourceView *null_srv = nullptr;
            context_->PSSetShaderResources(0, 1, &null_srv);
        }

        D3D11_VIEWPORT CalculateViewport(UINT target_width, UINT target_height, UINT source_width, UINT source_height) const
        {
            D3D11_VIEWPORT viewport = {};
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;

            if (target_width == 0 || target_height == 0 || source_width == 0 || source_height == 0) {
                viewport.Width = static_cast<FLOAT>(std::max(target_width, 1u));
                viewport.Height = static_cast<FLOAT>(std::max(target_height, 1u));
                return viewport;
            }

            const float target_aspect = static_cast<float>(target_width) / static_cast<float>(target_height);
            const float source_aspect = static_cast<float>(source_width) / static_cast<float>(source_height);

            if (target_aspect > source_aspect) {
                viewport.Height = static_cast<FLOAT>(target_height);
                viewport.Width = viewport.Height * source_aspect;
                viewport.TopLeftX = (static_cast<FLOAT>(target_width) - viewport.Width) * 0.5f;
                viewport.TopLeftY = 0.0f;
            } else {
                viewport.Width = static_cast<FLOAT>(target_width);
                viewport.Height = viewport.Width / source_aspect;
                viewport.TopLeftX = 0.0f;
                viewport.TopLeftY = (static_cast<FLOAT>(target_height) - viewport.Height) * 0.5f;
            }

            return viewport;
        }

        void Resize(UINT width, UINT height)
        {
            if (!swap_chain_ || width == 0 || height == 0) {
                return;
            }

            CreateSwapChainResources(width, height);
        }

        void Shutdown()
        {
            ReleaseMirrorTexture();

            sampler_state_.Reset();
            pixel_shader_.Reset();
            vertex_shader_.Reset();
            backbuffer_rtv_.Reset();
            swap_chain_.Reset();
            context_.Reset();
            device_.Reset();
            factory_.Reset();

            if (vr_system_ != nullptr) {
                vr::VR_Shutdown();
                vr_system_ = nullptr;
                vr_compositor_ = nullptr;
            }

            if (hwnd_ != nullptr) {
                DestroyWindow(hwnd_);
                hwnd_ = nullptr;
            }

            UnregisterClassW(kWindowClassName, instance_);
        }

        void ShowFatalError(const std::wstring &message)
        {
            MessageBoxW(hwnd_, message.c_str(), kWindowTitle, MB_ICONERROR | MB_OK);
        }

        void UpdateWindowTitle(const std::wstring &suffix)
        {
            std::wstring title = suffix.rfind(kWindowTitle, 0) == 0 ? suffix : std::wstring(kWindowTitle) + L" - " + suffix;
            SetWindowTextW(hwnd_, title.c_str());
        }

        LRESULT WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
        {
            switch (message) {
            case WM_SIZE:
                is_minimized_ = (w_param == SIZE_MINIMIZED);
                if (!is_minimized_) {
                    Resize(LOWORD(l_param), HIWORD(l_param));
                }
                return 0;

            case WM_KEYDOWN:
                if (w_param == VK_ESCAPE) {
                    DestroyWindow(hwnd);
                }
                return 0;

            case WM_DESTROY:
                running_ = false;
                hwnd_ = nullptr;
                PostQuitMessage(0);
                return 0;
            }

            return DefWindowProcW(hwnd, message, w_param, l_param);
        }

        static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
        {
            PreviewApp *app = nullptr;
            if (message == WM_NCCREATE) {
                auto *create_struct = reinterpret_cast<CREATESTRUCTW *>(l_param);
                app = static_cast<PreviewApp *>(create_struct->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
                app->hwnd_ = hwnd;
            } else {
                app = reinterpret_cast<PreviewApp *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            }

            if (app != nullptr) {
                return app->WindowProc(hwnd, message, w_param, l_param);
            }

            return DefWindowProcW(hwnd, message, w_param, l_param);
        }

    private:
        HINSTANCE instance_ = nullptr;
        HWND hwnd_ = nullptr;
        bool running_ = true;
        bool is_minimized_ = false;
        UINT client_width_ = kInitialClientWidth;
        UINT client_height_ = kInitialClientHeight;
        UINT mirror_width_ = 0;
        UINT mirror_height_ = 0;
        bool allow_tearing_ = false;
        std::wstring last_status_;

        vr::IVRSystem *vr_system_ = nullptr;
        vr::IVRCompositor *vr_compositor_ = nullptr;
        ID3D11ShaderResourceView *mirror_srv_ = nullptr;

        ComPtr<IDXGIFactory2> factory_;
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<IDXGISwapChain1> swap_chain_;
        ComPtr<ID3D11RenderTargetView> backbuffer_rtv_;
        ComPtr<ID3D11VertexShader> vertex_shader_;
        ComPtr<ID3D11PixelShader> pixel_shader_;
        ComPtr<ID3D11SamplerState> sampler_state_;
    };
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command)
{
    PreviewApp app;
    return app.Run(instance, show_command);
}
