#version 460 core

layout(location = 0) out vec4 out_integratedIllum; // RGB = integrated illumination, A = variance
layout(location = 1) out vec4 out_moments;         // R = 1st moment, G = 2nd moment, B = history length, A = 1.0
layout(location = 2) out vec4 out_historyNormal;   // RGB = normal, A = hit mask
layout(location = 3) out vec4 out_historyWorldPos; // RGB = world position, A = 1.0

in vec2 v_texCoord;

uniform sampler2D t_currIllum;     // RGB = raw illumination, A = linear depth
uniform sampler2D t_currNormal;    // RGB = world normal, A = hit mask
uniform sampler2D t_currWorldPos;  // RGB = world position, A = 1.0
uniform sampler2D t_currAlbedo;    // RGB = albedo, A = 1.0

uniform sampler2D t_prevIllum;     // Previous integrated illumination + variance
uniform sampler2D t_prevMoments;   // Previous moments (1st, 2nd, history length)
uniform sampler2D t_prevNormal;    // Previous normal + hit mask
uniform sampler2D t_prevWorldPos;  // Previous world position

uniform mat4 u_prevViewProj;
uniform vec2 u_resolution;
uniform float u_alpha;             // Temporal blend weight (default ~0.15)
uniform float u_maxHistory;        // Max history frames (default ~32.0)

float luminance(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
	vec4 currIllumSample = texture(t_currIllum, v_texCoord);
	vec3 currIllum = currIllumSample.rgb;
	if (isnan(currIllum.r) || isnan(currIllum.g) || isnan(currIllum.b) || isinf(currIllum.r))
	{
		currIllum = vec3(0.0);
	}
	vec4 currNormalSample = texture(t_currNormal, v_texCoord);
	vec3 currNormal = currNormalSample.rgb;
	float currHit = currNormalSample.a;
	vec3 currWorldPos = texture(t_currWorldPos, v_texCoord).rgb;

	// Always write current geometry to history buffers for next frame
	out_historyNormal = currNormalSample;
	out_historyWorldPos = vec4(currWorldPos, 1.0);

	// If background / sky hit, no temporal reprojection is performed
	if (currHit < 0.5)
	{
		float lum = luminance(currIllum);
		out_integratedIllum = vec4(currIllum, 0.0);
		out_moments = vec4(lum, lum * lum, 1.0, 1.0);
		return;
	}

	// Reproject current world position into previous clip space
	vec4 prevClip = u_prevViewProj * vec4(currWorldPos, 1.0);
	vec2 prevNDC = prevClip.xy / max(prevClip.w, 1e-5);
	vec2 prevUV = prevNDC * 0.5 + 0.5;

	bool inside = (prevUV.x >= 0.0 && prevUV.x <= 1.0 && prevUV.y >= 0.0 && prevUV.y <= 1.0);

	// Sample previous geometry buffer at reprojected UV
	vec4 prevNormalSample = texture(t_prevNormal, prevUV);
	vec3 prevNormal = prevNormalSample.rgb;
	float prevHit = prevNormalSample.a;
	vec3 prevWorldPos = texture(t_prevWorldPos, prevUV).rgb;

	// Validate history consistency using normal alignment and plane distance
	bool normalValid = dot(currNormal, prevNormal) > 0.75;
	float planeDist = abs(dot(currNormal, currWorldPos - prevWorldPos));
	bool depthValid = (planeDist < 0.25);
	bool historyValid = inside && (prevHit > 0.5) && normalValid && depthValid;

	// Compute 3x3 local color neighborhood bounding box for history clamping
	vec3 cMin = currIllum;
	vec3 cMax = currIllum;
	vec3 m1 = vec3(0.0);
	vec3 m2 = vec3(0.0);
	float nWeight = 0.0;

	for (int dy = -1; dy <= 1; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			vec2 offset = vec2(float(dx), float(dy)) / u_resolution;
			vec4 tapNormalSample = texture(t_currNormal, v_texCoord + offset);
			if (tapNormalSample.a < 0.5)
			{
				continue;
			}

			vec3 c = texture(t_currIllum, v_texCoord + offset).rgb;
			cMin = min(cMin, c);
			cMax = max(cMax, c);
			m1 += c;
			m2 += c * c;
			nWeight += 1.0;
		}
	}

	if (nWeight > 0.0)
	{
		m1 /= nWeight;
		m2 /= nWeight;
	}
	else
	{
		m1 = currIllum;
		m2 = currIllum * currIllum;
	}
	vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));
	vec3 boxMin = min(cMin, m1 - 1.5 * sigma);
	vec3 boxMax = max(cMax, m1 + 1.5 * sigma);

	float currLum = luminance(currIllum);
	vec2 currMoments = vec2(currLum, currLum * currLum);

	vec3 integratedIllum;
	vec2 integratedMoments;
	float historyLength;

	if (historyValid)
	{
		vec4 prevIllumSample = texture(t_prevIllum, prevUV);
		vec3 clampedPrevColor = clamp(prevIllumSample.rgb, boxMin, boxMax);
		vec4 prevMomentsSample = texture(t_prevMoments, prevUV);

		historyLength = min(prevMomentsSample.b + 1.0, u_maxHistory);
		float alpha = max(u_alpha, 1.0 / historyLength);

		integratedIllum = mix(clampedPrevColor, currIllum, alpha);
		integratedMoments = mix(prevMomentsSample.rg, currMoments, alpha);
	}
	else
	{
		historyLength = 1.0;
		integratedIllum = currIllum;
		integratedMoments = currMoments;
	}

	float variance = max(0.0, integratedMoments.y - integratedMoments.x * integratedMoments.x);
	if (historyLength < 4.0)
	{
		float spatialVar = dot(sigma * sigma, vec3(0.3333));
		variance = max(variance, spatialVar);
	}

	if (isnan(integratedIllum.r) || isnan(integratedIllum.g) || isnan(integratedIllum.b))
	{
		integratedIllum = currIllum;
	}
	if (isnan(variance) || isinf(variance) || variance < 0.0)
	{
		variance = 0.0;
	}

	out_integratedIllum = vec4(integratedIllum, variance);
	out_moments = vec4(integratedMoments, historyLength, 1.0);
}
