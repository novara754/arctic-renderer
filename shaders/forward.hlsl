#define PI 3.14159265

cbuffer Scene : register(b0)
{
	float3 eye;
	float4x4 model;
	float4x4 proj_view;
	float4x4 light_proj_view;
	float3 sun_dir;
	float ambient;
	float3 sun_color;
	uint shadow_map_idx;
	uint environment_idx;
	uint material_offset;
	uint lights_buffer_idx;
}

SamplerState s_sampler : register(s0);

struct PointLight
{
	float3 position;
	float3 color;
};

struct Lights
{
	uint len;
	PointLight lights[16];
};

struct VSIn
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : BITANGENT;
	float2 tex_coords : TEXCOORD;
};

struct VSOut
{
	float4 clip_position : SV_POSITION;
	float2 tex_coords : TEXCOORD;
	float3x3 tbn : NORMAL;
	float3 world_position : POSITION0;
	float4 light_space_position : POSITION1;
};

VSOut vs_main(VSIn vs_in)
{
	float4 world_pos = mul(model, float4(vs_in.position, 1.0));

	float3 t = normalize(vs_in.tangent);
	float3 n = normalize(vs_in.normal);
	float3 b = normalize(vs_in.bitangent);

	VSOut vs_out;
	vs_out.clip_position = mul(proj_view, world_pos);
	vs_out.tex_coords = vs_in.tex_coords;
	vs_out.tbn = transpose(float3x3(t, b, n));
	vs_out.world_position = world_pos.xyz;
	vs_out.light_space_position = mul(light_proj_view, world_pos);

	return vs_out;
}

float calculate_shadow(float3 normal, float4 light_space_position)
{
	Texture2D<float4> t_shadow_map = ResourceDescriptorHeap[shadow_map_idx];

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

float3 get_base_color(float2 tex_coords)
{
	Texture2D<float4> t_diffuse = ResourceDescriptorHeap[material_offset + 0];
	return t_diffuse.Sample(s_sampler, tex_coords).rgb;
}

float3 get_normal(float2 tex_coords, float3x3 tbn)
{
	Texture2D<float4> t_normal = ResourceDescriptorHeap[material_offset + 1];
	float3 normal = t_normal.Sample(s_sampler, tex_coords).rgb;
	normal.g = 1.0 - normal.g;
	normal = normal * 2.0 - 1.0;
	normal = normalize(mul(tbn, normal));
	return normal;
}

float get_metalness(float2 tex_coords)
{
	Texture2D<float4> t_metalness = ResourceDescriptorHeap[material_offset + 2];
	return t_metalness.Sample(s_sampler, tex_coords).b;
}

float get_roughness(float2 tex_coords)
{
	Texture2D<float4> t_roughness = ResourceDescriptorHeap[material_offset + 2];
	return t_roughness.Sample(s_sampler, tex_coords).g;
}

float3 fresnel_schlick(float3 cos_theta, float3 F0)
{
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

float distribution_ggx(float3 n, float3 h, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float n_dot_h = max(dot(n, h), 0.0);
	float n_dot_h2 = n_dot_h * n_dot_h;

	float num = a2;
	float denom = n_dot_h2 * (a2 - 1.0) + 1.0;
	denom = PI * denom * denom;

	return num / denom;
}

float geometry_schlick_ggx(float n_dot_wo, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;

	float num = n_dot_wo;
	float denom = n_dot_wo * (1.0 - k) + k;

	return num / denom;
}

float geometry_smith(float3 n, float3 wo, float3 wi, float roughness)
{
	float n_dot_wo = max(dot(n, wo), 0.0);
	float n_dot_wi = max(dot(n, wi), 0.0);
	float ggx1 = geometry_schlick_ggx(n_dot_wo, roughness);
	float ggx2 = geometry_schlick_ggx(n_dot_wi, roughness);
	return ggx1 * ggx2;
}

float3 brdf_cook_torrance(float3 n, float3 h, float3 wo, float3 wi, float roughness, float3 F)
{
	float NDF = distribution_ggx(n, h, roughness);
	float G = geometry_smith(n, wo, wi, roughness);

	float3 num = NDF * G * F;
	// add 0.0001 to avoid div by 0
	float denom = 4.0 * max(dot(n, wo), 0.0) * max(dot(n, wi), 0.0) + 0.0001;

	return num / denom;
}

float3 calculate_outgoing_radiance(float3 n, float3 wo, float3 wi, float3 ingoing_radiance, float3 base_color, float metalness, float roughness)
{
	float3 h = normalize(wo + wi);

	float3 F0 = float3(0.04, 0.04, 0.04);
	F0 = lerp(F0, base_color, metalness);
	float3 F = fresnel_schlick(max(dot(h, wo), 0.0), F0);

	float3 specular = brdf_cook_torrance(n, h, wo, wi, roughness, F);

	float3 kS = F;
	float3 kD = 1.0 - kS;
	kD *= 1.0 - metalness;

	float n_dot_wi = max(dot(n, wi), 0.0);
	return (kD * base_color / PI + specular) * ingoing_radiance * n_dot_wi;
}

static const float2 inv_atan = float2(0.1591, 0.3183);

float3 sample_environment(float3 dir)
{
	Texture2D<float4> t_environment = ResourceDescriptorHeap[environment_idx];

	float2 uv = float2(atan2(dir.z, dir.x), asin(dir.y));
	uv *= inv_atan;
	uv += 0.5;

	return t_environment.Sample(s_sampler, uv).rgb;
}

float4 ps_main(VSOut vs_out) : SV_TARGET
{
	ConstantBuffer<Lights> point_lights = ResourceDescriptorHeap[lights_buffer_idx];

	float3 base_color = get_base_color(vs_out.tex_coords);
	float3 n = get_normal(vs_out.tex_coords, vs_out.tbn);
	float metalness = get_metalness(vs_out.tex_coords);
	float roughness = get_roughness(vs_out.tex_coords);

	float3 wo = normalize(eye - vs_out.world_position);
	float3 Lo = float3(0.0, 0.0, 0.0);

	/* Sun */
	float shadow = calculate_shadow(n, vs_out.light_space_position);
	Lo += (1.0 - shadow) * calculate_outgoing_radiance(n, wo, -sun_dir, sun_color, base_color, metalness, roughness);

	for (uint i = 0; i < point_lights.len; ++i)
	{
		float3 light_dir = point_lights.lights[i].position - vs_out.world_position;
		float dist = length(light_dir);
		float3 wi = light_dir / dist;
		float3 radiance = point_lights.lights[i].color / (dist * dist);
		Lo += (1.0 - shadow) * calculate_outgoing_radiance(n, wo, wi, radiance, base_color, metalness, roughness);
	}

	float3 color = Lo + ambient * base_color;
	return float4(color, 1.0);
}
