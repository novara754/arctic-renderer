cbuffer Scene : register(b0)
{
	float4x4 view;
	float4x4 proj;

	float3 sun_dir;
	float ambient;
	float3 sun_color;
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
	float3 normal : NORMAL;
};

VSOut vs_main(uint id : SV_VERTEXID, VSIn vs_in)
{
	VSOut vs_out;

	vs_out.clip_position = mul(proj, mul(view, float4(vs_in.position, 1.0)));
	vs_out.tex_coords = vs_in.tex_coords;
	vs_out.normal = vs_in.normal;

	return vs_out;
}

float4 ps_main(VSOut vs_out) : SV_TARGET
{
	float3 normal = normalize(vs_out.normal);
	float3 base_color = t_diffuse.Sample(s_sampler, vs_out.tex_coords).rgb;
	float3 ambient_color = ambient * base_color;
	float3 sunlight_color = dot(normal, -sun_dir) * sun_color * base_color;
	float3 color = ambient_color + sunlight_color;
	return float4(color, 1.0);
}
