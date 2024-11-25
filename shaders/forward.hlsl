cbuffer Scene : register(b0)
{
	float4x4 model;
	float4x4 proj_view;
	float4x4 light_proj_view;
	float3 sun_dir;
	float ambient;
	float3 sun_color;
}

Texture2D t_shadow_map : register(t0);
Texture2D t_diffuse : register(t1);
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
	float4 light_space_position : POSITION;
};

VSOut vs_main(uint id : SV_VERTEXID, VSIn vs_in)
{
	float4 world_pos = mul(model, float4(vs_in.position, 1.0));

	VSOut vs_out;

	vs_out.clip_position = mul(proj_view, world_pos);
	vs_out.tex_coords = vs_in.tex_coords;
	vs_out.normal = vs_in.normal;
	vs_out.light_space_position = mul(light_proj_view, world_pos);

	return vs_out;
}

float calculate_shadow(float3 normal, float4 light_space_position)
{
	float3 proj_coords = light_space_position.xyz / light_space_position.w;
	proj_coords.xy = proj_coords.xy * 0.5 + 0.5;
	proj_coords.y = 1.0 - proj_coords.y;

	if (proj_coords.z > 1.0 || proj_coords.x < 0.0 || proj_coords.y < 0.0 || proj_coords.x > 1.0 || proj_coords.y > 1.0)
	{
		return 0.0;
	}

	float bias = 0.0; // max(0.05 * (1.0 - dot(normal, sun_dir)), 0.005);
	float current_depth = proj_coords.z;
	float shadow = 0.0;
	for (int i = -2; i <= 2; ++i)
	{
		for (int j = -2; j <= 2; ++j)
		{
			float2 offset = float2(i * 0.0001, j * 0.0001);
			float closest_depth = t_shadow_map.SampleLevel(s_sampler, proj_coords.xy + offset, 0).r;
			shadow += (current_depth - bias) > closest_depth ? 1.0 : 0.0;
		}
	}
	shadow /= 25.0;

	return shadow;
}

float4 ps_main(VSOut vs_out) : SV_TARGET
{
	float3 normal = normalize(vs_out.normal);
	float3 base_color = t_diffuse.Sample(s_sampler, vs_out.tex_coords).rgb;

	float shadow = calculate_shadow(normal, vs_out.light_space_position);

	float3 ambient_color = ambient * base_color;
	float3 sunlight_color = dot(normal, -sun_dir) * sun_color * base_color;
	float3 color = ambient_color + (1.0 - shadow) * sunlight_color;
	return float4(color, 1.0);
}
