#version 460 core

in vec2 v_texCoord;
out vec4 FragColor;

uniform sampler2D t_filteredIllum; // Filtered illumination (RGB) + variance (A)
uniform sampler2D t_albedo;        // Albedo texture (RGB)
uniform sampler2D t_rawIllum;      // Raw 1-SPP illumination (RGB)
uniform sampler2D t_normal;        // Normal texture (RGB)

uniform int u_debugView;           // 0: Final Denoised, 1: Raw 1-SPP, 2: Irradiance, 3: Variance, 4: Normals, 5: Albedo
uniform int u_demodulateAlbedo;    // 1 if illumination was demodulated
uniform float u_contrast;          // Contrast adjustment
uniform int u_enableDenoising;     // 1 if SVGF is enabled

void main()
{
	vec4 filteredSample = texture(t_filteredIllum, v_texCoord);
	vec3 filteredIllum = filteredSample.rgb;
	float variance = filteredSample.a;

	vec3 albedo = texture(t_albedo, v_texCoord).rgb;
	vec3 rawIllum = texture(t_rawIllum, v_texCoord).rgb;
	vec4 normalSample = texture(t_normal, v_texCoord);

	vec3 col = filteredIllum;

	if (u_enableDenoising == 0)
	{
		// Legacy accumulation: filteredIllum is already accumulated color
		col = filteredIllum;
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
