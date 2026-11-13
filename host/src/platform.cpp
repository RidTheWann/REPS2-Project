// SPDX-License-Identifier: GPL-3.0-or-later
#include "reps2/host/platform.hpp"

#if defined(_WIN32)

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

namespace reps2::host {
namespace {

constexpr wchar_t kWindowClass[] = L"REPS2HostWindow";

class Win32HostWindow final : public IHostWindow {
public:
    ~Win32HostWindow() override { destroy(); }

    bool create(const HostWindowDesc& desc) noexcept override {
        if (hwnd_ != nullptr) return true;
        desc_ = desc;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Win32HostWindow::window_proc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kWindowClass;
        if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;

        const auto title = to_wide(desc.title);
        RECT rect{0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height)};
        ::AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd_ = ::CreateWindowExW(0, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, wc.hInstance, this);
        return hwnd_ != nullptr;
    }

    void show() noexcept override {
        if (hwnd_ != nullptr) ::ShowWindow(hwnd_, SW_SHOW);
    }

    void hide() noexcept override {
        if (hwnd_ != nullptr) ::ShowWindow(hwnd_, SW_HIDE);
    }

    bool pump_once() noexcept override {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
            if (msg.message == WM_QUIT) {
                close_requested_ = true;
                continue;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        return !close_requested_;
    }

    bool should_close() const noexcept override { return close_requested_; }
    void* native_handle() const noexcept override { return hwnd_; }

    void destroy() noexcept override {
        if (hwnd_ != nullptr) {
            ::DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

private:
    static std::wstring to_wide(const std::string& text) {
        if (text.empty()) return {};
        const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
            static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(size), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
            wide.data(), size);
        return wide;
    }

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        auto* self = reinterpret_cast<Win32HostWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (message) {
        case WM_NCCREATE: {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        case WM_CLOSE:
            if (self != nullptr) self->close_requested_ = true;
            ::DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        default:
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    HWND hwnd_ = nullptr;
    HostWindowDesc desc_{};
    bool close_requested_ = false;
};

} // namespace

std::unique_ptr<IHostWindow> create_host_window() {
    return std::make_unique<Win32HostWindow>();
}

} // namespace reps2::host

#else

namespace reps2::host {
namespace {

class NullHostWindow final : public IHostWindow {
public:
    bool create(const HostWindowDesc& desc) noexcept override {
        desc_ = desc;
        created_ = true;
        return true;
    }
    void show() noexcept override { shown_ = true; }
    void hide() noexcept override { shown_ = false; }
    bool pump_once() noexcept override { return !close_requested_; }
    bool should_close() const noexcept override { return close_requested_; }
    void* native_handle() const noexcept override { return nullptr; }
    void destroy() noexcept override { created_ = false; }

private:
    HostWindowDesc desc_{};
    bool created_ = false;
    bool shown_ = false;
    bool close_requested_ = false;
};

} // namespace

std::unique_ptr<IHostWindow> create_host_window() {
    return std::make_unique<NullHostWindow>();
}

} // namespace reps2::host

#endif
