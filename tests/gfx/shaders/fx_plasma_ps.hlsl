float time : register(c0);
struct PS_IN { float2 Tex : TEXCOORD0; float4 Color : COLOR; };
float4 main( PS_IN In ) : COLOR {
    float2 p = In.Tex * 10.0;
    float v = sin(p.x + time) + sin(p.y + time * 1.3)
            + sin((p.x + p.y) * 0.5 + time)
            + sin(length(p - 5.0) - time * 1.7);
    v *= 0.25;
    float3 c = 0.5 + 0.5 * float3(sin(v*3.1416), sin(v*3.1416+2.09), sin(v*3.1416+4.19));
    return float4(c, 1.0);
}
