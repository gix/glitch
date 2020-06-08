#pragma once
#include "Span.h"
#include <d3d11.h>

namespace gt
{

HRESULT LoadPixelShader(_In_ ID3D11Device* d3dDevice, cspan<std::byte> bytecode,
                        _COM_Outptr_ ID3D11PixelShader** ppPixelShader);

HRESULT LoadVertexShader(_In_ ID3D11Device* d3dDevice, cspan<std::byte> bytecode,
                         cspan<D3D11_INPUT_ELEMENT_DESC> inputElementDesc,
                         _COM_Outptr_ ID3D11VertexShader** ppVertexShader,
                         _COM_Outptr_opt_ ID3D11InputLayout** ppInputLayout);

HRESULT LoadPixelShaderResource(_In_ ID3D11Device* d3dDevice, uint16_t resourceId,
                                _COM_Outptr_ ID3D11PixelShader** ppPixelShader);

HRESULT LoadVertexShaderResource(_In_ ID3D11Device* d3dDevice, uint16_t resourceId,
                                 cspan<D3D11_INPUT_ELEMENT_DESC> inputElementDesc,
                                 _COM_Outptr_ ID3D11VertexShader** ppVertexShader,
                                 _COM_Outptr_opt_ ID3D11InputLayout** ppInputLayout);

} // namespace gt
