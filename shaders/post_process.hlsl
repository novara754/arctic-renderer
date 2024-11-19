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

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 tm_aces(float3 color)
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
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
