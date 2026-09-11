sampler2D fontTex : register(s0);
struct PS_IN { float2 Tex : TEXCOORD0; float4 Color : COLOR; };
float4 main( PS_IN In ) : COLOR {
    float a = tex2D(fontTex, In.Tex).a;
    return float4(In.Color.rgb, In.Color.a * a);
}
