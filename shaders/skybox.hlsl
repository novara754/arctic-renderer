static const float3 CUBE_VERTICES[] = {
	float3(-1.0f,  1.0f, -1.0f),
	float3(-1.0f, -1.0f, -1.0f),
	float3( 1.0f, -1.0f, -1.0f),
	float3( 1.0f, -1.0f, -1.0f),
	float3( 1.0f,  1.0f, -1.0f),
	float3(-1.0f,  1.0f, -1.0f),

	float3(-1.0f, -1.0f,  1.0f),
	float3(-1.0f, -1.0f, -1.0f),
	float3(-1.0f,  1.0f, -1.0f),
	float3(-1.0f,  1.0f, -1.0f),
	float3(-1.0f,  1.0f,  1.0f),
	float3(-1.0f, -1.0f,  1.0f),

	float3( 1.0f, -1.0f, -1.0f),
	float3( 1.0f, -1.0f,  1.0f),
	float3( 1.0f,  1.0f,  1.0f),
	float3( 1.0f,  1.0f,  1.0f),
	float3( 1.0f,  1.0f, -1.0f),
	float3( 1.0f, -1.0f, -1.0f),

	float3(-1.0f, -1.0f,  1.0f),
	float3(-1.0f,  1.0f,  1.0f),
	float3( 1.0f,  1.0f,  1.0f),
	float3( 1.0f,  1.0f,  1.0f),
	float3( 1.0f, -1.0f,  1.0f),
	float3(-1.0f, -1.0f,  1.0f),

	float3(-1.0f,  1.0f, -1.0f),
	float3( 1.0f,  1.0f, -1.0f),
	float3( 1.0f,  1.0f,  1.0f),
	float3( 1.0f,  1.0f,  1.0f),
	float3(-1.0f,  1.0f,  1.0f),
	float3(-1.0f,  1.0f, -1.0f),

	float3(-1.0f, -1.0f, -1.0f),
	float3(-1.0f, -1.0f,  1.0f),
	float3( 1.0f, -1.0f, -1.0f),
	float3( 1.0f, -1.0f, -1.0f),
	float3(-1.0f, -1.0f,  1.0f),
	float3( 1.0f, -1.0f,  1.0f)
};

static const float2 INV_ATAN = float2(0.1591, 0.3183);

cbuffer Constants : register(b0)
{
	float4x4 proj_view;
}

Texture2D t_environment : register(t0);
SamplerState s_sampler : register(s0);

struct VSOut
{
	float4 clip_position : SV_POSITION;
	float3 tex_coords : TEXCOORDS;
};

VSOut vs_main(uint id : SV_VERTEXID)
{
	VSOut vs_out;

	float3 v = CUBE_VERTICES[id];
	float4 pos = mul(proj_view, float4(v, 1.0));

	vs_out.clip_position = pos.xyww;
	vs_out.tex_coords = v;

	return vs_out;
}

float3 sample_environment(float3 dir)
{
	dir = normalize(dir);
	float2 uv = float2(atan2(dir.z, dir.x), asin(dir.y));
	uv *= INV_ATAN;
	uv += 0.5;
	uv.y = -uv.y;

	return t_environment.Sample(s_sampler, uv).rgb;
}

float4 ps_main(VSOut vs_out) : SV_TARGET
{
	return float4(sample_environment(vs_out.tex_coords), 1.0);
}
