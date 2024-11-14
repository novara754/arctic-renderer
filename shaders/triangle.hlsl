cbuffer Constants : register(b0)
{
	float3 top_vertex_color;
}

struct VSOut
{
	float4 clip_position : SV_POSITION;
	float4 color : COLOR0;
};

VSOut vs_main(uint id : SV_VERTEXID)
{
	VSOut vs_out;

	if (id == 0)
	{
		vs_out.clip_position = float4(0.0, 0.5, 0.0, 1.0);
		vs_out.color = float4(top_vertex_color, 1.0); // float4(1.0, 0.0, 0.0, 1.0);
	}
	else if (id == 1)
	{
		vs_out.clip_position = float4(-0.5, -0.5, 0.0, 1.0);
		vs_out.color = float4(0.0, 1.0, 0.0, 1.0);
	}
	else
	{
		vs_out.clip_position = float4(0.5, -0.5, 0.0, 1.0);
		vs_out.color = float4(0.0, 0.0, 1.0, 1.0);
	}

	return vs_out;
}

float4 ps_main(VSOut vs_out) : SV_TARGET
{
	return vs_out.color;
}
