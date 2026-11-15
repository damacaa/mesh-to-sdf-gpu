#version 460 core

in vec2 v_texCoord;
out vec4 FragColor;

uniform sampler2D t_filteredIllum; // Filtered illumination (RGB) + variance (A)
uniform sampler2D t_albedo;        // Albedo texture (RGB)
uniform sampler2D t_rawIllum;      // Raw 1-SPP illumination (RGB)
uniform sampler2D t_normal;        // Normal texture (RGB)
uniform sampler3D t_sdfTexture3D;  // Candidate 3D SDF volume
uniform sampler3D t_refSdfTexture3D; // Exact reference 3D SDF volume

uniform int u_debugView;           // 0: Final Denoised, 1: Raw 1-SPP, 2: Irradiance, 3: Variance, 4: Normals, 5: Albedo
uniform int u_demodulateAlbedo;    // 1 if illumination was demodulated
uniform float u_contrast;          // Contrast adjustment
uniform int u_enableDenoising;     // 1 if SVGF is enabled

uniform int u_enableSliceInspector; // 1 to show 2D orthogonal SDF slice viewer
uniform int u_sliceAxis;            // 0: X (YZ), 1: Y (XZ), 2: Z (XY)
uniform float u_slicePosition;      // Normalized slice depth in [0, 1]
uniform int u_sliceViewMode;        // 0: SDF Isolines & Sign, 1: Error Heatmap |JFA - Ref|
uniform float u_sdfWorldSize;       // World size of SDF volume box
uniform int u_resolution;           // Voxel grid resolution along each axis

vec3 heatmapRamp(float t)
{
	float x = clamp(t, 0.0, 1.0);
	vec3 c0 = vec3(0.05, 0.03, 0.16);
	vec3 c1 = vec3(0.36, 0.07, 0.43);
	vec3 c2 = vec3(0.79, 0.24, 0.25);
	vec3 c3 = vec3(0.98, 0.61, 0.05);
	vec3 c4 = vec3(0.99, 0.99, 0.64);
	if (x < 0.25) return mix(c0, c1, x * 4.0);
	if (x < 0.50) return mix(c1, c2, (x - 0.25) * 4.0);
	if (x < 0.75) return mix(c2, c3, (x - 0.50) * 4.0);
	return mix(c3, c4, (x - 0.75) * 4.0);
}

void main()
{
	if (u_enableSliceInspector == 1)
	{
		ivec2 fbSize = textureSize(t_filteredIllum, 0);
		float aspect = float(fbSize.x) / float(max(fbSize.y, 1));
		float boxScale = 0.90 * min(aspect, 1.0);
		vec2 sliceUv = (v_texCoord - 0.5) * vec2(aspect, 1.0) / boxScale + 0.5;

		vec3 outRgb = vec3(0.08, 0.09, 0.11);
		if (all(greaterThanEqual(sliceUv, vec2(0.0))) && all(lessThanEqual(sliceUv, vec2(1.0))))
		{
			vec3 uvw;
			if (u_sliceAxis == 0)
			{
				uvw = vec3(clamp(u_slicePosition, 0.0, 1.0), sliceUv.y, sliceUv.x);
			}
			else if (u_sliceAxis == 1)
			{
				uvw = vec3(sliceUv.x, clamp(u_slicePosition, 0.0, 1.0), 1.0 - sliceUv.y);
			}
			else
			{
				uvw = vec3(sliceUv.x, sliceUv.y, clamp(u_slicePosition, 0.0, 1.0));
			}

			float d = texture(t_sdfTexture3D, uvw).r;
			float worldSize = max(u_sdfWorldSize, 1e-4);
			float voxelSize = worldSize / float(max(u_resolution, 1));

			if (u_sliceViewMode == 0)
			{
				float normD = clamp(abs(d) / (worldSize * 0.4), 0.0, 1.0);
				vec3 insideCol = mix(vec3(0.10, 0.45, 0.90), vec3(0.03, 0.12, 0.35), normD);
				vec3 outsideCol = mix(vec3(0.95, 0.48, 0.14), vec3(0.28, 0.12, 0.06), normD);
				outRgb = (d < 0.0) ? insideCol : outsideCol;

				float bandStep = max(voxelSize * 2.0, 0.02);
				float wave = abs(fract(d / bandStep + 0.5) - 0.5);
				float isoLine = 1.0 - smoothstep(0.03, 0.09, wave);
				outRgb = mix(outRgb, outRgb * 0.45, isoLine * 0.65);

				float zeroContour = 1.0 - smoothstep(0.0, voxelSize * 0.9, abs(d));
				outRgb = mix(outRgb, vec3(1.0), zeroContour);
			}
			else
			{
				float refD = texture(t_refSdfTexture3D, uvw).r;
				float absErr = abs(d - refD);
				float errScale = max(voxelSize * 2.0, 1e-4);
				outRgb = heatmapRamp(absErr / errScale);

				float zeroContour = 1.0 - smoothstep(0.0, voxelSize * 0.75, abs(refD));
				outRgb = mix(outRgb, vec3(0.2, 0.95, 1.0), zeroContour * 0.75);

				if ((d < 0.0) != (refD < 0.0))
				{
					vec2 checker = floor(sliceUv * float(max(u_resolution, 16)));
					float pattern = mod(checker.x + checker.y, 2.0);
					outRgb = mix(outRgb, vec3(1.0, 0.15, 0.55), 0.35 * pattern);
				}
			}

			vec2 edgeDist = min(sliceUv, 1.0 - sliceUv);
			float border = 1.0 - smoothstep(0.0, 0.004, min(edgeDist.x, edgeDist.y));
			outRgb = mix(outRgb, vec3(0.75), border);
		}

		float sliceLuma = dot(outRgb, vec3(0.299, 0.587, 0.114));
		FragColor = vec4(outRgb, sliceLuma);
		return;
	}

	vec4 filteredSample = texture(t_filteredIllum, v_texCoord);
	vec3 filteredIllum = filteredSample.rgb;
	float variance = filteredSample.a;

	vec3 albedo = texture(t_albedo, v_texCoord).rgb;
	vec3 rawIllum = texture(t_rawIllum, v_texCoord).rgb;
	vec4 normalSample = texture(t_normal, v_texCoord);

	vec3 col = filteredIllum;

	if (u_enableDenoising == 0)
	{
		// Static accumulation: filteredIllum is accumulated linear radiance
		col = mix(vec3(0.5), filteredIllum, u_contrast);
		col = clamp(col, 0.0, 1.0);
	}
	else
	{
		if (u_debugView == 0)
		{
			// Final Denoised Image: re-modulate with albedo
			col = filteredIllum;
			if (u_demodulateAlbedo == 1)
			{
				col *= albedo;
			}
			col = mix(vec3(0.5), col, u_contrast);
			col = clamp(col, 0.0, 1.0);
		}
		else if (u_debugView == 1)
		{
			// Raw 1-SPP Noisy Input
			col = rawIllum;
			if (u_demodulateAlbedo == 1)
			{
				col *= albedo;
			}
			col = mix(vec3(0.5), col, u_contrast);
			col = clamp(col, 0.0, 1.0);
		}
		else if (u_debugView == 2)
		{
			// Denoised Irradiance only (without surface textures)
			col = mix(vec3(0.5), filteredIllum, u_contrast);
			col = clamp(col, 0.0, 1.0);
		}
		else if (u_debugView == 3)
		{
			// Estimated Variance visualization (standard deviation false color)
			float sd = sqrt(max(variance, 0.0)) * 2.5;
			col = clamp(vec3(sd * 1.5, sd * 0.7, sd * 0.2), 0.0, 1.0);
			float luma = dot(col, vec3(0.299, 0.587, 0.114));
			FragColor = vec4(col, luma);
			return;
		}
		else if (u_debugView == 4)
		{
			// Surface normals
			vec3 n = normalSample.rgb * 0.5 + 0.5;
			float luma = dot(n, vec3(0.299, 0.587, 0.114));
			FragColor = vec4(n, luma);
			return;
		}
		else if (u_debugView == 5)
		{
			// Surface albedo
			vec3 srgbAlbedo = pow(max(albedo, vec3(0.0)), vec3(0.4545));
			float luma = dot(srgbAlbedo, vec3(0.299, 0.587, 0.114));
			FragColor = vec4(srgbAlbedo, luma);
			return;
		}
	}

	// sRGB gamma conversion and perceptual luminance packing for FXAA
	vec3 srgbCol = pow(max(col, vec3(0.0)), vec3(0.4545));
	float luma = dot(srgbCol, vec3(0.299, 0.587, 0.114));
	FragColor = vec4(srgbCol, luma);
}
