struct VOutput
{
    float4 Position : SV_POSITION;
    half2 UV : TEXCOORD0;
};

struct POutput
{
    float4 Color : SV_Target;
};

Texture2D mainTex : register(t0);
SamplerState mainSampler : register(s0);
Texture2D noiseTex : register(t1);
SamplerState noiseSampler : register(s1);
Texture2D trashTex : register(t2);
SamplerState trashSampler : register(s2);

cbuffer Constants : register(b0)
{
    float intensity;
};

float4 main(VOutput input) : SV_Target
{
    float4 glitch = noiseTex.Sample(noiseSampler, input.UV);

    float thresh = 1.001 - intensity * 1.001;
    float w_d = step(thresh, pow(glitch.z, 2.5)); // displacement glitch
    float w_f = step(thresh, pow(glitch.w, 2.5)); // frame glitch
    float w_c = step(thresh, pow(glitch.z, 3.5)); // color glitch

    // Displacement.
    float2 uv = frac(input.UV + glitch.xy * w_d);
    float4 source = mainTex.Sample(mainSampler, uv);

    // Mix with trash frame.
    float3 color = lerp(source, trashTex.Sample(trashSampler, uv), w_f).rgb;

    // Shuffle color components.
    float3 neg = saturate(color.grb + (1 - dot(color, 1)) * 0.5);
    //color = lerp(color, neg, w_c);

    return float4(color, source.a);
}
