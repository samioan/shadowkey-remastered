#include "platform/win32/window.h"

#include <utility>

#include <windows.h>

namespace sk {

namespace {

// BITMAPINFO with room for the 3 BI_BITFIELDS color masks (RGB565) right
// after the header, as GDI expects.
struct Rgb565BitmapInfo {
    BITMAPINFOHEADER header;
    DWORD masks[3];
};

}  // namespace

struct Window::Impl {
    HWND hwnd = nullptr;
    int clientWidth = 0;
    int clientHeight = 0;
    bool closed = false;
    Rgb565BitmapInfo bmi{};
    Window::KeyCallback keyCallback;
    Window::CharCallback charCallback;
};

namespace {

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* impl = reinterpret_cast<Window::Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CLOSE:
            if (impl) impl->closed = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
        case WM_KEYUP:
            if (impl && impl->keyCallback) {
                impl->keyCallback(static_cast<int>(wParam), msg == WM_KEYDOWN);
            }
            return 0;
        case WM_CHAR:
            if (impl && impl->charCallback) {
                impl->charCallback(static_cast<wchar_t>(wParam));
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace

Window::Window(int clientWidth, int clientHeight, const std::wstring& title) {
    impl_ = new Impl();
    impl_->clientWidth = clientWidth;
    impl_->clientHeight = clientHeight;

    impl_->bmi.header.biSize = sizeof(BITMAPINFOHEADER);
    impl_->bmi.header.biWidth = Backbuffer::kWidth;
    // Negative height: top-down DIB, so row 0 in our buffer is the top row
    // on screen (matches the source data's row order, no manual flip).
    impl_->bmi.header.biHeight = -Backbuffer::kHeight;
    impl_->bmi.header.biPlanes = 1;
    impl_->bmi.header.biBitCount = 16;
    impl_->bmi.header.biCompression = BI_BITFIELDS;
    impl_->bmi.masks[0] = 0xF800;  // R
    impl_->bmi.masks[1] = 0x07E0;  // G
    impl_->bmi.masks[2] = 0x001F;  // B

    static const wchar_t* kClassName = L"ShadowkeyPortWindowClass";
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    RECT rect{0, 0, clientWidth, clientHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    impl_->hwnd = CreateWindowExW(
        0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr);

    SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl_));
    ShowWindow(impl_->hwnd, SW_SHOWNORMAL);
    UpdateWindow(impl_->hwnd);
}

Window::~Window() {
    if (impl_) {
        if (impl_->hwnd) DestroyWindow(impl_->hwnd);
        delete impl_;
    }
}

void Window::SetKeyCallback(KeyCallback callback) {
    impl_->keyCallback = std::move(callback);
}

void Window::SetCharCallback(CharCallback callback) {
    impl_->charCallback = std::move(callback);
}

void Window::Close() {
    if (impl_->hwnd) PostMessageW(impl_->hwnd, WM_CLOSE, 0, 0);
}

void Window::RunMessageLoop(const IdleCallback& onIdle) {
    MSG msg{};
    while (!impl_->closed) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                impl_->closed = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (impl_->closed) break;
        if (onIdle) onIdle();
    }
    shouldClose_ = true;
}

void Window::Present(const Backbuffer& backbuffer) {
    if (!impl_->hwnd) return;
    HDC hdc = GetDC(impl_->hwnd);
    RECT client{};
    GetClientRect(impl_->hwnd, &client);
    int destW = client.right - client.left;
    int destH = client.bottom - client.top;

    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(
        hdc, 0, 0, destW, destH, 0, 0, Backbuffer::kWidth, Backbuffer::kHeight,
        backbuffer.Data(), reinterpret_cast<const BITMAPINFO*>(&impl_->bmi),
        DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(impl_->hwnd, hdc);
}

}  // namespace sk
