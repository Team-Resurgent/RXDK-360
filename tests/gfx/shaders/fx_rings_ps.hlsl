float time : register(c0);
struct PS_IN { float2 Tex : TEXCOORD0; float4 Color : COLOR; };
float4 main( PS_IN In ) : COLOR {
    float2 d = In.Tex - 0.5;
    float r = length(d);
    float v = 0.5 + 0.5 * sin(42.0 * r - time * 3.0);
    return float4(v, v * 0.55, 1.0 - v, 1.0);
}
