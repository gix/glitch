struct VInput
{
    float2 Position : POSITION;
    half2 TexCoord : TEXCOORD0;
};

struct VOutput
{
    float4 Position : SV_Position;
    half2 UV : TEXCOORD0;
};

cbuffer Constants : register(b0)
{
    matrix Projection;
};

VOutput main(VInput input)
{
    VOutput output;
    output.Position = mul(Projection, float4(input.Position, 0.5, 1));
    output.UV = input.TexCoord;

    return output;
}
