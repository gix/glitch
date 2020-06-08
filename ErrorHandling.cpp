#include "ErrorHandling.h"

#include <cstdio>
#include <iterator>

namespace gt
{

void TraceHResult(HRESULT hresult, wchar_t const* file, int line, wchar_t const* function)
{
    wchar_t buffer[512];
    swprintf(buffer, std::size(buffer), L"%ls(%d): hr=0x%08lX (%ls)\n", file, line,
             hresult, function);
    fwprintf(stderr, buffer);
    OutputDebugStringW(buffer);
}

} // namespace gt
