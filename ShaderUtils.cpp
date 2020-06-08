#include "ShaderUtils.h"

#include "ComPtr.h"
#include "ErrorHandling.h"
#include "ResourceUtils.h"

#include <Windows.h>

namespace gt
{

HRESULT LoadPixelShader(_In_ ID3D11Device* d3dDevice, cspan<std::byte> bytecode,
                        _COM_Outptr_ ID3D11PixelShader** ppPixelShader)
{
    return d3dDevice->CreatePixelShader(
        bytecode.data(), static_cast<SIZE_T>(bytecode.size()), nullptr, ppPixelShader);
}

HRESULT LoadVertexShader(_In_ ID3D11Device* d3dDevice, cspan<std::byte> bytecode,
                         cspan<D3D11_INPUT_ELEMENT_DESC> inputElementDesc,
                         _COM_Outptr_ ID3D11VertexShader** ppVertexShader,
                         _COM_Outptr_opt_ ID3D11InputLayout** ppInputLayout)
{
    // Use a local variable for the shader so that we only return it if
    // the input layout creation is successful (or skipped).
    ComPtr<ID3D11VertexShader> vertexShader;
    HR(d3dDevice->CreateVertexShader(
        bytecode.data(), static_cast<SIZE_T>(bytecode.size()), nullptr, &vertexShader));

    if (ppInputLayout != nullptr) {
        HR(d3dDevice->CreateInputLayout(
            inputElementDesc.data(), inputElementDesc.size(), bytecode.data(),
            static_cast<SIZE_T>(bytecode.size()), ppInputLayout));
    }

    vertexShader.MoveTo(ppVertexShader);

    return S_OK;
}

HRESULT LoadPixelShaderResource(_In_ ID3D11Device* d3dDevice, uint16_t resourceId,
                                _COM_Outptr_ ID3D11PixelShader** ppPixelShader)
{
    auto const bytecode =
        GetModuleResource(nullptr, L"SHADER", MAKEINTRESOURCEW(resourceId));
    if (bytecode.empty())
        return E_INVALIDARG;

    return LoadPixelShader(d3dDevice, bytecode, ppPixelShader);
}

HRESULT LoadVertexShaderResource(_In_ ID3D11Device* d3dDevice, uint16_t resourceId,
                                 cspan<D3D11_INPUT_ELEMENT_DESC> inputElementDesc,
                                 _COM_Outptr_ ID3D11VertexShader** ppVertexShader,
                                 _COM_Outptr_opt_ ID3D11InputLayout** ppInputLayout)
{
    auto const bytecode =
        GetModuleResource(nullptr, L"SHADER", MAKEINTRESOURCEW(resourceId));
    if (bytecode.empty())
        return E_INVALIDARG;

    return LoadVertexShader(d3dDevice, bytecode, inputElementDesc, ppVertexShader,
                            ppInputLayout);
}

} // namespace gt
