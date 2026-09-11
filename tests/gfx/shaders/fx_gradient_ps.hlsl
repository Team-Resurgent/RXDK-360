float time : register(c0);
struct PS_IN { float2 Tex : TEXCOORD0; float4 Color : COLOR; };
float4 main( PS_IN In ) : COLOR {
    return float4(In.Tex.x, In.Tex.y, 0.5 + 0.5 * sin(time), 1.0);
}
