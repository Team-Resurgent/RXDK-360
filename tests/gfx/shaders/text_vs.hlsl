struct VS_IN  { float4 Pos : POSITION; float2 Tex : TEXCOORD0; float4 Color : COLOR; };
struct VS_OUT { float4 Pos : POSITION; float2 Tex : TEXCOORD0; float4 Color : COLOR; };
VS_OUT main( VS_IN In ) {
    VS_OUT Out;
    Out.Pos = In.Pos;
    Out.Tex = In.Tex;
    Out.Color = In.Color;
    return Out;
}
