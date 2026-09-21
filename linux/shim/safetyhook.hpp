#pragma once
// palhook Linux shim for the safetyhook API surface PalSchema uses (create_inline, call<>, disable),
// backed by funchook, the inline-hook engine the Linux UE4SS port already builds (libfunchook.a +
// libdistorm.a, position independent). funchook relocates the prologue into a trampoline, which is
// what `call` uses to reach the original.
#include <funchook.h>
#include <expected>
#include <cstdint>
#include <string>
#include <utility>

namespace safetyhook {
    struct Error {
        enum : int { FUNCHOOK_CREATE_FAILED = 1, FUNCHOOK_PREPARE_FAILED, FUNCHOOK_INSTALL_FAILED, FUNCHOOK_UNINSTALL_FAILED, NOT_INSTALLED } type{};
        std::string message;
    };

    class InlineHook {
    public:
        InlineHook() = default;
        InlineHook(const InlineHook&) = delete;
        InlineHook& operator=(const InlineHook&) = delete;
        InlineHook(InlineHook&& o) noexcept { *this = std::move(o); }
        InlineHook& operator=(InlineHook&& o) noexcept {
            if (this != &o) { reset(); m_handle = o.m_handle; m_target = o.m_target; m_trampoline = o.m_trampoline; m_installed = o.m_installed;
                              o.m_handle = nullptr; o.m_target = nullptr; o.m_trampoline = nullptr; o.m_installed = false; }
            return *this;
        }
        ~InlineHook() { reset(); }

        static std::expected<InlineHook, Error> create(void* target, void* destination) {
            InlineHook h;
            h.m_handle = funchook_create();
            if (!h.m_handle) return std::unexpected(Error{Error::FUNCHOOK_CREATE_FAILED, "funchook_create failed"});
            h.m_target = target;
            h.m_trampoline = target;
            int rc = funchook_prepare(h.m_handle, &h.m_trampoline, destination);
            if (rc != 0) { Error e{Error::FUNCHOOK_PREPARE_FAILED, funchook_error_message(h.m_handle)}; funchook_destroy(h.m_handle); h.m_handle = nullptr; return std::unexpected(e); }
            rc = funchook_install(h.m_handle, 0);
            if (rc != 0) { Error e{Error::FUNCHOOK_INSTALL_FAILED, funchook_error_message(h.m_handle)}; funchook_destroy(h.m_handle); h.m_handle = nullptr; return std::unexpected(e); }
            h.m_installed = true;
            return h;
        }

        std::expected<void, Error> enable() { return {}; }
        std::expected<void, Error> disable() {
            if (!m_handle || !m_installed) return std::unexpected(Error{Error::NOT_INSTALLED, "not installed"});
            if (funchook_uninstall(m_handle, 0) != 0) return std::unexpected(Error{Error::FUNCHOOK_UNINSTALL_FAILED, funchook_error_message(m_handle)});
            m_installed = false;
            return {};
        }
        void reset() {
            if (m_handle) { if (m_installed) funchook_uninstall(m_handle, 0); funchook_destroy(m_handle); }
            m_handle = nullptr; m_target = nullptr; m_trampoline = nullptr; m_installed = false;
        }
        [[nodiscard]] bool enabled() const { return m_installed; }
        [[nodiscard]] explicit operator bool() const { return m_handle != nullptr; }
        [[nodiscard]] void* target() const { return m_target; }
        [[nodiscard]] uintptr_t target_address() const { return reinterpret_cast<uintptr_t>(m_target); }
        [[nodiscard]] void* original() const { return m_trampoline; }
        [[nodiscard]] uintptr_t trampoline() const { return reinterpret_cast<uintptr_t>(m_trampoline); }

        template <typename RetT = void, typename... Args> RetT call(Args... args) {
            return reinterpret_cast<RetT (*)(Args...)>(m_trampoline)(args...);
        }
        template <typename RetT = void, typename... Args> RetT unsafe_call(Args... args) {
            return reinterpret_cast<RetT (*)(Args...)>(m_trampoline)(args...);
        }
        template <typename RetT = void, typename... Args> RetT fastcall(Args... args) { return call<RetT>(args...); }
        template <typename RetT = void, typename... Args> RetT thiscall(Args... args) { return call<RetT>(args...); }
        template <typename RetT = void, typename... Args> RetT stdcall(Args... args) { return call<RetT>(args...); }
        template <typename RetT = void, typename... Args> RetT ccall(Args... args) { return call<RetT>(args...); }

    private:
        funchook_t* m_handle{};
        void* m_target{};
        void* m_trampoline{};
        bool m_installed{};
    };

    [[nodiscard]] inline InlineHook create_inline(void* target, void* destination) {
        auto h = InlineHook::create(target, destination);
        return h ? std::move(*h) : InlineHook{};
    }
    template <typename T, typename U> [[nodiscard]] InlineHook create_inline(T target, U destination) {
        return create_inline(reinterpret_cast<void*>(target), reinterpret_cast<void*>(destination));
    }
}
using SafetyHookInline = safetyhook::InlineHook;
