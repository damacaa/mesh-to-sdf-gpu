#version 460 core

in vec2 v_texCoord;
out vec4 FragColor;

uniform sampler2D t_screen;      // Resolved screen texture (RGB: sRGB, A: Precomputed Luminance)
uniform int u_enableFxaa;        // 1 to enable anti-aliasing, 0 for passthrough
uniform int u_fxaaMode;          // 0: Full FXAA 3.11 (weird-engine), 1: Derivative FXAA (dFdx/dFdy)
uniform float u_edgeThreshold;   // Edge detection threshold (default: 0.05)
uniform vec2 u_texelSize;        // 1.0 / screen resolution

// Full FXAA 3.11 Quality implementation from weird-engine
vec3 applyWeirdEngineFXAA(vec2 uv)
{
	vec2 rcpFrame = u_texelSize;

	vec4 centerSample = texture(t_screen, uv);
	float lumaM = centerSample.a;

	float lumaN = texture(t_screen, uv + vec2(0.0, -rcpFrame.y)).a;
	float lumaS = texture(t_screen, uv + vec2(0.0, rcpFrame.y)).a;
	float lumaW = texture(t_screen, uv + vec2(-rcpFrame.x, 0.0)).a;
	float lumaE = texture(t_screen, uv + vec2(rcpFrame.x, 0.0)).a;

	float rangeMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
	float rangeMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
	float range = rangeMax - rangeMin;

	const float FXAA_EDGE_THRESHOLD = 0.125;
	const float FXAA_EDGE_THRESHOLD_MIN = 0.05;
	const float FXAA_SUBPIX_TRIM = 0.75;

	// Early exit if contrast below threshold (not an edge)
	if (range < max(FXAA_EDGE_THRESHOLD_MIN, rangeMax * FXAA_EDGE_THRESHOLD))
	{
		return centerSample.rgb;
	}

	float lumaNW = texture(t_screen, uv + vec2(-rcpFrame.x, -rcpFrame.y)).a;
	float lumaNE = texture(t_screen, uv + vec2(rcpFrame.x, -rcpFrame.y)).a;
	float lumaSW = texture(t_screen, uv + vec2(-rcpFrame.x, rcpFrame.y)).a;
	float lumaSE = texture(t_screen, uv + vec2(rcpFrame.x, rcpFrame.y)).a;

	// Sub-pixel antialiasing filter
	float lumaL = lumaN + lumaS + lumaW + lumaE;
	float subpixA = 2.0 * lumaL + (lumaNW + lumaNE + lumaSW + lumaSE);
	float subpixB = subpixA * (1.0 / 12.0) - lumaM;
	float subpixC = clamp(abs(subpixB) / range, 0.0, 1.0);
	float subpixBlend = smoothstep(0.0, 1.0, subpixC);
	float subpixelOffset = subpixBlend * subpixBlend * FXAA_SUBPIX_TRIM;

	// Edge direction (horizontal vs vertical)
	float edgeHorz = abs(-2.0 * lumaM + lumaN + lumaS) * 2.0 +
	                 abs(-2.0 * lumaW + lumaNW + lumaSW) +
	                 abs(-2.0 * lumaE + lumaNE + lumaSE);
	float edgeVert = abs(-2.0 * lumaM + lumaW + lumaE) * 2.0 +
	                 abs(-2.0 * lumaN + lumaNW + lumaNE) +
	                 abs(-2.0 * lumaS + lumaSW + lumaSE);
	bool isHorizontal = (edgeHorz >= edgeVert);

	// Select edge orientation
	float luma1 = isHorizontal ? lumaN : lumaW;
	float luma2 = isHorizontal ? lumaS : lumaE;
	float grad1 = abs(luma1 - lumaM);
	float grad2 = abs(luma2 - lumaM);

	bool is1Steeper = grad1 >= grad2;
	float gradScaled = max(grad1, grad2) * 0.25;

	float stepLength = isHorizontal ? rcpFrame.y : rcpFrame.x;
	if (is1Steeper)
	{
		stepLength = -stepLength;
	}

	vec2 uvEdge = uv;
	if (isHorizontal)
	{
		uvEdge.y += stepLength * 0.5;
	}
	else
	{
		uvEdge.x += stepLength * 0.5;
	}

	vec2 stepDir = isHorizontal ? vec2(rcpFrame.x, 0.0) : vec2(0.0, rcpFrame.y);

	// Explore along edge tangent in negative and positive directions
	vec2 uvNeg = uvEdge - stepDir;
	vec2 uvPos = uvEdge + stepDir;

	float lumaNeg = texture(t_screen, uvNeg).a;
	float lumaPos = texture(t_screen, uvPos).a;

	float edgeLuma = is1Steeper ? (luma1 + lumaM) * 0.5 : (luma2 + lumaM) * 0.5;

	bool doneNeg = abs(lumaNeg - edgeLuma) >= gradScaled;
	bool donePos = abs(lumaPos - edgeLuma) >= gradScaled;

	const int MAX_STEPS = 8;
	for (int i = 1; i < MAX_STEPS; i++)
	{
		float stepScale = (i < 4) ? 1.0 : ((i < 6) ? 1.5 : 2.0);
		if (!doneNeg)
		{
			uvNeg -= stepDir * stepScale;
			lumaNeg = texture(t_screen, uvNeg).a;
			doneNeg = abs(lumaNeg - edgeLuma) >= gradScaled;
		}
		if (!donePos)
		{
			uvPos += stepDir * stepScale;
			lumaPos = texture(t_screen, uvPos).a;
			donePos = abs(lumaPos - edgeLuma) >= gradScaled;
		}
		if (doneNeg && donePos)
		{
			break;
		}
	}

	float dstNeg = isHorizontal ? (uv.x - uvNeg.x) : (uv.y - uvNeg.y);
	float dstPos = isHorizontal ? (uvPos.x - uv.x) : (uvPos.y - uv.y);

	bool isCloserNeg = dstNeg < dstPos;
	float dst = min(dstNeg, dstPos);
	float edgeSpan = dstNeg + dstPos;

	float edgeOffset = 0.5 - dst / edgeSpan;

	// Check if end of edge has same sign as center (if not, it's not a valid edge span)
	float lumaEnd = isCloserNeg ? lumaNeg : lumaPos;
	bool goodSpan = (lumaEnd - edgeLuma < 0.0) != (lumaM - edgeLuma < 0.0);
	if (!goodSpan)
	{
		edgeOffset = 0.0;
	}

	float finalOffset = max(subpixelOffset, edgeOffset);

	vec2 finalUV = uv;
	if (isHorizontal)
	{
		finalUV.y += stepLength * finalOffset;
	}
	else
	{
		finalUV.x += stepLength * finalOffset;
	}

	return texture(t_screen, finalUV).rgb;
}

void main()
{
	vec4 centerSample = texture(t_screen, v_texCoord);

	if (u_enableFxaa == 0)
	{
		FragColor = vec4(centerSample.rgb, 1.0);
		return;
	}

	if (u_fxaaMode == 1)
	{
		// -------------------------------------------------------------
		// Mode 1: Derivative-Based Anti-Aliasing (dFdx / dFdy)
		// -------------------------------------------------------------
		float lumaM = centerSample.a;

		float dLx = dFdxFine(lumaM);
		float dLy = dFdyFine(lumaM);
		float gradMag = length(vec2(dLx, dLy));

		if (gradMag < u_edgeThreshold)
		{
			FragColor = vec4(centerSample.rgb, 1.0);
			return;
		}

		vec2 edgeNormal = normalize(vec2(dLx, dLy));

		vec2 offsetNorm = edgeNormal * u_texelSize * 0.75;
		vec3 samplePos = texture(t_screen, v_texCoord + offsetNorm).rgb;
		vec3 sampleNeg = texture(t_screen, v_texCoord - offsetNorm).rgb;
		vec3 normBlur = 0.5 * (samplePos + sampleNeg);

		vec2 edgeTangent = vec2(-edgeNormal.y, edgeNormal.x);
		vec2 offsetTan = edgeTangent * u_texelSize * 0.5;
		vec3 tanPos = texture(t_screen, v_texCoord + offsetTan).rgb;
		vec3 tanNeg = texture(t_screen, v_texCoord - offsetTan).rgb;
		vec3 tanBlur = 0.5 * (tanPos + tanNeg);

		vec3 blended = 0.5 * normBlur + 0.5 * tanBlur;

		float blendFactor = clamp(gradMag * 4.0, 0.0, 0.75);
		vec3 finalColor = mix(centerSample.rgb, blended, blendFactor);

		FragColor = vec4(finalColor, 1.0);
		return;
	}

	// -------------------------------------------------------------
	// Mode 0: Full FXAA 3.11 (weird-engine's exact algorithm)
	// -------------------------------------------------------------
	FragColor = vec4(applyWeirdEngineFXAA(v_texCoord), 1.0);
}
