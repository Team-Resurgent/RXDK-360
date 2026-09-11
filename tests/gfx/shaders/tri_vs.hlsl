float4x4 matWVP : register(c0);
struct VS_IN  { float4 Pos : POSITION; float4 Color : COLOR; };
struct VS_OUT { float4 Pos : POSITION; float4 Color : COLOR; };
VS_OUT main( VS_IN In ) {
    VS_OUT Out;
    Out.Pos = mul( matWVP, In.Pos );
    Out.Color = In.Color;
    return Out;
}
