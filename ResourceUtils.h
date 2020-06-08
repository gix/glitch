#pragma once
#include "Span.h"
#include <Windows.h>

namespace gt
{

cspan<std::byte> GetModuleResource(HMODULE module, wchar_t const* type,
                                   wchar_t const* name) noexcept;

inline cspan<std::byte> GetModuleResource(wchar_t const* type,
                                          wchar_t const* name) noexcept
{
    return GetModuleResource(nullptr, type, name);
}

} // namespace gt
