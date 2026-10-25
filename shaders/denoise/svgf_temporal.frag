#version 460 core

layout(location = 0) out vec4 out_integratedIllum; // RGB = integrated illumination, A = variance
layout(location = 1) out vec4 out_moments;         // R = 1st moment, G = 2nd moment, B = history length, A = 1.0
layout(location = 2) out vec4 out_historyNormal;   // RGB = normal, A = hit mask
layout(location = 3) out vec4 out_historyWorldPos; // RGB = world position, A = 1.0

in vec2 v_texCoord;

uniform sampler2D t_currIllum;     // RGB = raw illumination, A = linear depth
uniform sampler2D t_currNormal;    // RGB = world normal, A = hit mask
uniform sampler2D t_currWorldPos;  // RGB = world position, A = 1.0

uniform sampler2D t_prevIllum;     // Previous first spatial result + variance
uniform sampler2D t_prevMoments;   // Previous moments (1st, 2nd, history length)
uniform sampler2D t_prevNormal;    // Previous normal + hit mask
uniform sampler2D t_prevWorldPos;  // Previous world position

uniform mat4 u_prevViewProj;
uniform vec2 u_resolution;
uniform float u_alpha;             // Temporal blend weight (default ~0.15)
uniform float u_maxHistory;        // Max history frames (default ~32.0)
uniform int u_resetHistory;        // 1 rejects all previous samples
uniform int u_cameraMoved;         // 1 if camera moved this frame, 0 if static
uniform float u_historyFixFrames;  // Pixels with a shorter history get a spatial prefilter (0 = off)

float luminance(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
	vec4 currIllumSample = texture(t_currIllum, v_texCoord);
	vec3 currIllum = currIllumSample.rgb;
	if (any(isnan(currIllum)) || any(isinf(currIllum)))
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

	vec2 prevUV;
	if (u_cameraMoved == 0)
	{
		prevUV = v_texCoord;
	}
	else
	{
		vec4 prevClip = u_prevViewProj * vec4(currWorldPos, 1.0);
		prevUV = (prevClip.w > 1e-5) ? (prevClip.xy / prevClip.w * 0.5 + 0.5) : vec2(-1.0);
	}

	bool inside = (prevUV.x >= 0.0 && prevUV.x <= 1.0 && prevUV.y >= 0.0 && prevUV.y <= 1.0);

	// 4-tap bilateral bilinear reprojection prevents bleeding across depth/normal edges
	vec2 prevPos = prevUV * u_resolution - 0.5;
	ivec2 p0 = ivec2(floor(prevPos));
	vec2 f = clamp(fract(prevPos), 0.0, 1.0);

	float w[4];
	w[0] = (1.0 - f.x) * (1.0 - f.y);
	w[1] = f.x * (1.0 - f.y);
	w[2] = (1.0 - f.x) * f.y;
	w[3] = f.x * f.y;

	ivec2 offsets[4] = ivec2[4](
		ivec2(0, 0),
		ivec2(1, 0),
		ivec2(0, 1),
		ivec2(1, 1)
	);

	vec3 prevIllumSum = vec3(0.0);
	vec2 prevMomentsSum = vec2(0.0);
	float prevHistorySum = 0.0;
	float weightSum = 0.0;

	for (int i = 0; i < (u_cameraMoved == 0 ? 1 : 4) && inside && u_resetHistory == 0; ++i)
	{
		ivec2 tapCoord = (u_cameraMoved == 0) ? ivec2(gl_FragCoord.xy) : (p0 + offsets[i]);
		if (tapCoord.x < 0 || tapCoord.x >= int(u_resolution.x) ||
		    tapCoord.y < 0 || tapCoord.y >= int(u_resolution.y))
		{
			continue;
		}

		vec4 tapNormalSample = texelFetch(t_prevNormal, tapCoord, 0);
		if (tapNormalSample.a < 0.5)
		{
			continue;
		}

		vec3 tapNormal = tapNormalSample.rgb;
		vec3 tapPos = texelFetch(t_prevWorldPos, tapCoord, 0).rgb;

		// Bilateral rejection: discard taps belonging to different geometry (e.g. foreground emissive object)
		bool tapNormalValid = dot(currNormal, tapNormal) > 0.75;
		float tapPlaneDist = abs(dot(currNormal, currWorldPos - tapPos));
		bool tapDepthValid = (tapPlaneDist < 0.15);

		if (tapNormalValid && tapDepthValid)
		{
			float tapW = (u_cameraMoved == 0) ? 1.0 : w[i];
			vec4 tapIllum = texelFetch(t_prevIllum, tapCoord, 0);
			vec4 tapMoments = texelFetch(t_prevMoments, tapCoord, 0);

			if (!any(isnan(tapIllum)) && !any(isinf(tapIllum)) &&
			    !any(isnan(tapMoments)) && !any(isinf(tapMoments)) && tapMoments.b >= 1.0)
			{
				prevIllumSum += tapIllum.rgb * tapW;
				prevMomentsSum += tapMoments.rg * tapW;
				prevHistorySum += tapMoments.b * tapW;
				weightSum += tapW;
			}
		}
	}

	bool historyValid = (weightSum > 1e-4);

	float currLum = luminance(currIllum);
	vec2 currMoments = vec2(currLum, currLum * currLum);

	vec3 integratedIllum;
	vec2 integratedMoments;
	float historyLength;

	if (historyValid)
	{
		vec3 prevIllum = prevIllumSum / weightSum;
		vec2 prevMoments = prevMomentsSum / weightSum;
		float prevHistory = prevHistorySum / weightSum;

		historyLength = min(prevHistory + 1.0, u_maxHistory);
		float alpha = max(u_alpha, 1.0 / historyLength);

		// Accumulate the same input signal in the color and the moments.
		integratedIllum = mix(prevIllum, currIllum, alpha);
		integratedMoments = mix(prevMoments, currMoments, alpha);
	}
	else
	{
		historyLength = 1.0;
		integratedIllum = currIllum;
		integratedMoments = currMoments;
	}

	float variance = max(0.0, integratedMoments.y - integratedMoments.x * integratedMoments.x);
	bool historyFix = historyLength < u_historyFixFrames;
	if (historyLength < 4.0 || historyFix)
	{
		// Estimate luminance variance from nearby geometry while history is short.
		// Disoccluded pixels also get a wider spatial average of the current frame (history fix).
		int stride = historyFix ? 2 : 1;
		vec3 spatialColor = vec3(0.0);
		vec2 spatialMoments = vec2(0.0);
		float spatialWeight = 0.0;
		ivec2 pixel = ivec2(gl_FragCoord.xy);
		for (int dy = -3; dy <= 3; ++dy)
		{
			for (int dx = -3; dx <= 3; ++dx)
			{
				ivec2 tap = pixel + ivec2(dx, dy) * stride;
				if (any(lessThan(tap, ivec2(0))) || any(greaterThanEqual(tap, ivec2(u_resolution)))) continue;
				vec4 normal = texelFetch(t_currNormal, tap, 0);
				if (normal.a < 0.5) continue;
				vec3 position = texelFetch(t_currWorldPos, tap, 0).xyz;
				vec3 color = texelFetch(t_currIllum, tap, 0).rgb;
				if (any(isnan(color)) || any(isinf(color))) continue;
				float planeDist = abs(dot(currNormal, currWorldPos - position));
				float weight = pow(max(dot(currNormal, normal.xyz), 0.0), 32.0);
				weight *= exp(-planeDist / 0.15 - float(dx * dx + dy * dy) / 8.0);
				float lum = luminance(color);
				spatialColor += color * weight;
				spatialMoments += vec2(lum, lum * lum) * weight;
				spatialWeight += weight;
			}
		}
		if (spatialWeight > 1e-5)
		{
			spatialColor /= spatialWeight;
			spatialMoments /= spatialWeight;
			if (historyLength < 4.0)
			{
				float spatialVariance = max(0.0, spatialMoments.y - spatialMoments.x * spatialMoments.x);
				variance = max(variance, spatialVariance);
			}
			if (historyFix)
			{
				// Start from the spatial average and move to the temporal result as history grows.
				float t = clamp((historyLength - 1.0) / max(u_historyFixFrames - 1.0, 1.0), 0.0, 1.0);
				integratedIllum = mix(spatialColor, integratedIllum, t);
			}
		}
	}

	if (any(isnan(integratedIllum)) || any(isinf(integratedIllum)))
	{
		integratedIllum = currIllum;
	}
	// Convert the per-sample variance into the variance of the accumulated mean.
	float effAlpha = historyValid ? max(u_alpha, 1.0 / historyLength) : 1.0;
	variance *= effAlpha / (2.0 - effAlpha);

	if (isnan(variance) || isinf(variance) || variance < 0.0)
	{
		variance = 0.0;
	}

	out_integratedIllum = vec4(integratedIllum, variance);
	out_moments = vec4(integratedMoments, historyLength, 1.0);
}
