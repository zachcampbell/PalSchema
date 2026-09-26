#pragma once
// Linux shim for PalSchema's PS::Log: libstdc++ std::format has no char16_t support, so use the
// fmt (xchar) path UE4SS itself uses. Debug (Verbose) lines are dropped unless enableDebugLogging is set in
// Mods/PalSchema/config/config.json, as upstream does, and only then is every line mirrored into the probe log file.
#include <HAL/Platform.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <fmt/format.h>
#include <fmt/xchar.h>
#include <cstdio>
#include <string>
#include "Utility/Config.h"

namespace PS {
    void LinuxMirror(const std::u16string& line); // defined in the mod (main.cpp)

    template <RC::Unreal::int32 optional_arg, typename... FmtArgs>
    auto Log(RC::File::StringViewType content, FmtArgs... fmt_args) -> void
    {
        const bool debug = PS::PSConfig::Get()->IsDebugLoggingEnabled();
        if (optional_arg == RC::LogLevel::Verbose && !debug) return;
        const char16_t* prefix = optional_arg == RC::LogLevel::Error ? u"[PalSchema] [error] "
                               : optional_arg == RC::LogLevel::Warning ? u"[PalSchema] [warning] "
                               : optional_arg == RC::LogLevel::Verbose ? u"[PalSchema] [debug] " : u"[PalSchema] ";
        std::u16string body = fmt::vformat(fmt::basic_string_view<char16_t>(content.data(), content.size()),
                                           fmt::make_format_args<fmt::buffered_context<char16_t>>(fmt_args...));
        std::u16string line = std::u16string(prefix) + body;
        if (debug) LinuxMirror(line);
        RC::Output::send<optional_arg>(line);
    }
}
