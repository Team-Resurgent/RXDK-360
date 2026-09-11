struct PS_IN { float4 Color : COLOR; };
float4 main( PS_IN In ) : COLOR { return In.Color; }
