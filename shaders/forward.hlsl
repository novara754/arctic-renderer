cbuffer Camera : register(b0)
{
	float4x4 view;
	float4x4 proj;
}

Texture2D t_diffuse : register(t0);
SamplerState s_sampler : register(s0);

struct VSIn
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float2 tex_coords : TEXCOORD;
};

struct VSOut
{
	float4 clip_position : SV_POSITION;
	float2 tex_coords : TEXCOORD;
};

VSOut vs_main(uint id : SV_VERTEXID, VSIn vs_in)
{
	VSOut vs_out;

	vs_out.clip_position = mul(proj, mul(view, float4(vs_in.position, 1.0)));
	vs_out.tex_coords = vs_in.tex_coords;

	return vs_out;
}

float4 ps_main(VSOut vs_out) : SV_TARGET
{
	return t_diffuse.Sample(s_sampler, vs_out.tex_coords);
}
