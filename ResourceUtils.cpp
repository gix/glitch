#include "ResourceUtils.h"

namespace gt
{

cspan<std::byte> GetModuleResource(HMODULE module, wchar_t const* type,
                                   wchar_t const* name) noexcept
{
    auto const resInfo = FindResourceW(module, name, type);
    if (!resInfo)
        return {};

    HGLOBAL const resData = LoadResource(module, resInfo);
    size_t const size = SizeofResource(module, resInfo);
    if (!resData || size == 0)
        return {};

    auto const* ptr = reinterpret_cast<std::byte const*>(LockResource(resData));
    return {ptr, size};
}

} // namespace gt
