cbuffer Constants : register(b0)
{
	float4x4 view;
	float4x4 proj;
}

float4 main(float3 position : POSITION) : SV_POSITION
{
	return mul(proj, mul(view, float4(position, 1.0)));
}
