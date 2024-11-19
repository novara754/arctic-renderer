#define TM_REINHARD 0
#define TM_EXPOSURE 1
#define TM_ACES 2

cbuffer Settings : register(b0)
{
	float gamma;
	uint tm_method;
	float exposure;
}

RWTexture2D<float4> tex[2] : register(u0);

static float3x3 ACES_INPUT_MAT = float3x3(
	0.59719, 0.35458, 0.04823,
	0.07600, 0.90834, 0.01566,
	0.02840, 0.13383, 0.837 //
);

static float3x3 ACES_OUTPUT_MAT = float3x3(
	1.60475, -0.53108, -0.07367,
	-0.10208, 1.10813, -0.00605,
	-0.00327, -0.07276, 1.07 //
);

float3 rrt_and_odt_fit(float3 color)
{
	float3 a = color * (color + 0.0245786f) - 0.000090537f;
	float3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
	return a / b;
}

float3 correct_gamma(float3 color)
{
	return pow(abs(color), 1.0 / gamma);
}

float3 tm_reinhard(float3 color)
{
	return color / (color + float3(1.0, 1.0, 1.0));
}

float3 tm_exposure(float3 color)
{
	return float3(1.0, 1.0, 1.0) - exp(-color * exposure);
}

// Credits to: https://x.com/self_shadow
float3 tm_aces(float3 color)
{
	color = mul(ACES_INPUT_MAT, color);
	color = rrt_and_odt_fit(color);
	color = mul(ACES_OUTPUT_MAT, color);
	color = saturate(color);
	return color;
}

[numthreads(16, 16, 1)] void main(uint3 thread_id : SV_DISPATCHTHREADID)
{
	uint width, height;
	tex[0].GetDimensions(width, height);

	uint2 coord = thread_id.xy;

	if (coord.x >= width || coord.y >= height)
	{
		return;
	}

	float3 color = tex[0][coord].rgb;

	switch (tm_method)
	{
	case TM_REINHARD:
	default:
		color = tm_reinhard(color);
		break;
	case TM_EXPOSURE:
		color = tm_exposure(color);
		break;
	case TM_ACES:
		color = tm_aces(color);
		break;
	}

	color = correct_gamma(color);

	tex[1][coord] = float4(color, 1.0);
}
