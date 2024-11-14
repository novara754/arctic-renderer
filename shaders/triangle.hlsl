cbuffer Constants : register(b0)
{
	float3 top_vertex_color;
}

struct VSIn
{
	float3 position : POSITION;
	float3 color : COLOR;
};

struct VSOut
{
	float4 clip_position : SV_POSITION;
	float4 color : COLOR0;
};

VSOut vs_main(uint id : SV_VERTEXID, VSIn vs_in)
{
	VSOut vs_out;

	vs_out.clip_position = float4(vs_in.position, 1.0);
	if (id == 0)
	{
		vs_out.color = float4(top_vertex_color, 1.0);
	}
	else
	{
		vs_out.color = float4(vs_in.color, 1.0);
	}

	return vs_out;
}

float4 ps_main(VSOut vs_out) : SV_TARGET
{
	return vs_out.color;
}
