#version 460 core

layout(location = 0) out vec4 out_filtered; // RGB = filtered illumination, A = filtered variance

in vec2 v_texCoord;

uniform sampler2D t_illumination; // RGB = illumination, A = variance
uniform sampler2D t_normal;       // RGB = world normal, A = hit mask
uniform sampler2D t_worldPos;     // RGB = world position, A = 1.0

uniform int u_stepSize;           // 1, 2, 4, 8, ...
uniform vec2 u_resolution;
uniform float u_phiNormal;        // Normal sensitivity (default ~32.0)
uniform float u_phiDepth;         // Depth sensitivity (default ~1.0)
uniform float u_phiLuminance;     // Luminance/variance sensitivity (default ~4.0)

const float kernelWeights[5] = float[5](0.0625, 0.25, 0.375, 0.25, 0.0625);

float luminance(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
	vec4 centerIllumVar = texture(t_illumination, v_texCoord);
	vec3 centerColor = centerIllumVar.rgb;
	float centerVariance = centerIllumVar.a;

	if (isnan(centerColor.r) || isnan(centerColor.g) || isnan(centerColor.b) || isinf(centerColor.r))
	{
		centerColor = vec3(0.0);
	}
	if (isnan(centerVariance) || isinf(centerVariance) || centerVariance < 0.0)
	{
		centerVariance = 0.0;
	}

	vec4 centerNormalSample = texture(t_normal, v_texCoord);
	vec3 centerNormal = centerNormalSample.rgb;
	float centerHit = centerNormalSample.a;

	// Pass sky / miss pixels straight through without filtering
	if (centerHit < 0.5)
	{
		out_filtered = vec4(centerColor, centerVariance);
		return;
	}

	vec3 centerPos = texture(t_worldPos, v_texCoord).rgb;
	float centerLum = luminance(centerColor);
	float stdDev = sqrt(max(centerVariance, 1e-4));

	vec3 sumColor = vec3(0.0);
	float sumVariance = 0.0;
	float sumWeight = 0.0;

	for (int dy = -2; dy <= 2; ++dy)
	{
		for (int dx = -2; dx <= 2; ++dx)
		{
			vec2 offset = vec2(float(dx), float(dy)) * float(u_stepSize) / u_resolution;
			vec2 tapUV = v_texCoord + offset;

			if (tapUV.x < 0.0 || tapUV.x > 1.0 || tapUV.y < 0.0 || tapUV.y > 1.0)
			{
				continue;
			}

			vec4 tapNormalSample = texture(t_normal, tapUV);
			float tapHit = tapNormalSample.a;
			if (tapHit < 0.5)
			{
				continue;
			}

			vec3 tapNormal = tapNormalSample.rgb;
			vec3 tapPos = texture(t_worldPos, tapUV).rgb;
			vec4 tapIllumVar = texture(t_illumination, tapUV);
			vec3 tapColor = tapIllumVar.rgb;
			float tapVariance = tapIllumVar.a;

			if (isnan(tapColor.r) || isnan(tapColor.g) || isnan(tapColor.b) || isinf(tapColor.r))
			{
				continue;
			}
			if (isnan(tapVariance) || isinf(tapVariance) || tapVariance < 0.0)
			{
				tapVariance = 0.0;
			}

			float tapLum = luminance(tapColor);

			// Edge-stopping weights
			float dotN = clamp(dot(centerNormal, tapNormal), 0.0, 1.0);
			float wNormal = pow(dotN, u_phiNormal);

			float planeDist = abs(dot(centerNormal, centerPos - tapPos));
			float wDepth = exp(-min(planeDist / max(u_phiDepth, 1e-3), 20.0));

			float lumDenom = max(u_phiLuminance * stdDev, 1e-3);
			float wLum = exp(-min(abs(centerLum - tapLum) / lumDenom, 20.0));

			float wSpatial = kernelWeights[dx + 2] * kernelWeights[dy + 2];
			float w = wSpatial * wNormal * wDepth * wLum;

			sumColor += tapColor * w;
			sumVariance += tapVariance * w * w;
			sumWeight += w;
		}
	}

	if (sumWeight <= 1e-5 || isnan(sumWeight))
	{
		out_filtered = vec4(centerColor, centerVariance);
		return;
	}

	vec3 filteredColor = sumColor / sumWeight;
	float filteredVariance = sumVariance / (sumWeight * sumWeight);

	if (isnan(filteredColor.r) || isnan(filteredColor.g) || isnan(filteredColor.b))
	{
		filteredColor = centerColor;
	}
	if (isnan(filteredVariance) || isinf(filteredVariance))
	{
		filteredVariance = centerVariance;
	}

	out_filtered = vec4(filteredColor, filteredVariance);
}
