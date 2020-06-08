#pragma once
#include <Windows.h>
#include <comdef.h>

#define HR(expr)                                                                         \
    do {                                                                                 \
        HRESULT const hr_ = (expr);                                                      \
        if (FAILED(hr_)) {                                                               \
            ::gt::TraceHResult(hr_, __FILEW__, __LINE__, __FUNCTIONW__);                 \
            return hr_;                                                                  \
        }                                                                                \
    } while (false)

#define HRT(expr)                                                                        \
    do {                                                                                 \
        HRESULT const hr_ = (expr);                                                      \
        if (FAILED(hr_)) {                                                               \
            ::gt::TraceHResult(hr_, __FILEW__, __LINE__, __FUNCTIONW__);                 \
            throw _com_error(hr_, nullptr, false);                                       \
        }                                                                                \
    } while (false)

namespace gt
{

void TraceHResult(HRESULT hresult, wchar_t const* file, int line,
                  wchar_t const* function);

} // namespace gt
