cbuffer Constants : register(b0)
{
	float4x4 model;
	float4x4 proj_view;
}

float4 main(float3 position : POSITION) : SV_POSITION
{
	return mul(proj_view, mul(model, float4(position, 1.0)));
}
