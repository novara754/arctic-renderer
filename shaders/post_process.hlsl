cbuffer Settings : register(b0)
{
	float gamma;
}

RWTexture2D<float4> target : register(u0);

[numthreads(16, 16, 1)] void main(uint3 thread_id : SV_DISPATCHTHREADID)
{
	uint width, height;
	target.GetDimensions(width, height);

	uint2 coord = thread_id.xy;

	if (coord.x >= width || coord.y >= height)
	{
		return;
	}

	float3 color = target[coord].rgb;
	target[coord] = float4(pow(abs(color), 1.0 / gamma), 1.0);
}
