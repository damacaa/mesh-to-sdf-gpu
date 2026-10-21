#version 460 core

layout(location = 0) out vec4 out_illumination;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_worldPos;
layout(location = 3) out vec4 out_albedo;

in vec2 v_texCoord;

// Textures
uniform sampler2D t_previousColor;
uniform sampler2D t_depthTexture;       // GBuffer front depth
uniform sampler2D t_gbufferAlbedo;      // GBuffer albedo
uniform sampler2D t_gbufferWorldPos;    // GBuffer world position
uniform sampler2D t_gbufferNormal;      // GBuffer world normal
uniform isampler2D t_gbufferMaterial;   // GBuffer material index
uniform sampler2D t_gbufferBackDepth;   // GBuffer back depth
uniform sampler3D t_sdfTexture3D;       // 3D volumetric SDF texture

// 3D SDF texture parameters
uniform int u_enableSdf3DTexture;
uniform float u_sdfTextureWorldSize;
uniform vec3 u_sdfTextureOffset;
uniform int u_sdfTextureMaterialId;

// Path tracer settings
uniform int u_frameCounter;
uniform int u_rayBounces;
uniform float u_contrast;
uniform int u_enableDenoising;
uniform int u_demodulateAlbedo;
uniform int u_enableMeshShadows;

// Camera
uniform mat4 u_camMatrix;
uniform mat4 u_viewProjection;
uniform float u_fov;
uniform vec2 u_resolution;
uniform float u_near;
uniform float u_far;
uniform float u_time;

// Materials
struct MaterialData
{
	vec4 color;
	float metallic;
	float roughness;
	int pattern;
	float patternScale;
	vec4 secondaryColor;
	float emission;
};
uniform MaterialData u_materials[16];

// Lights
struct Light
{
	vec3 position;
	vec3 direction;
	vec4 color;
	int type;
};
#define MAX_LIGHTS 8
uniform int u_numLights;
uniform Light u_lights[MAX_LIGHTS];

const int MAX_STEPS = 256;
const float RAYMARCH_EPSILON = 0.001;
const float NORMAL_EPSILON = 0.0001;

// HG_SDF shapes and operators
#include "hg_sdf.glsl"

// Perlin noise and helpers
float hash(vec2 p)
{
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float fade(float t)
{
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

vec3 hash3D(vec3 p)
{
	p = vec3(dot(p, vec3(127.1, 311.7, 74.7)), dot(p, vec3(269.5, 183.3, 246.1)), dot(p, vec3(113.5, 271.9, 124.6)));
	return normalize(fract(sin(p) * 43758.5453123) * 2.0 - 1.0);
}

float grad3D(vec3 p, vec3 ip)
{
	return dot(p - ip, hash3D(ip));
}

float perlin3D(vec3 p)
{
	vec3 ip = floor(p);
	vec3 fp = fract(p);

	float a000 = grad3D(p, ip + vec3(0.0, 0.0, 0.0));
	float a100 = grad3D(p, ip + vec3(1.0, 0.0, 0.0));
	float a010 = grad3D(p, ip + vec3(0.0, 1.0, 0.0));
	float a110 = grad3D(p, ip + vec3(1.0, 1.0, 0.0));
	float a001 = grad3D(p, ip + vec3(0.0, 0.0, 1.0));
	float a101 = grad3D(p, ip + vec3(1.0, 0.0, 1.0));
	float a011 = grad3D(p, ip + vec3(0.0, 1.0, 1.0));
	float a111 = grad3D(p, ip + vec3(1.0, 1.0, 1.0));

	vec3 f = vec3(fade(fp.x), fade(fp.y), fade(fp.z));

	float mix00 = mix(a000, a100, f.x);
	float mix01 = mix(a010, a110, f.x);
	float mix10 = mix(a001, a101, f.x);
	float mix11 = mix(a011, a111, f.x);

	float mix0 = mix(mix00, mix01, f.y);
	float mix1 = mix(mix10, mix11, f.y);

	return mix(mix0, mix1, f.z);
}

// Random functions for path tracing
uint pcg(inout uint state)
{
	state = state * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

float randomFloat(inout float seed)
{
	uint state = floatBitsToUint(seed);
	float result = float(pcg(state)) * (1.0 / 4294967296.0);
	seed = uintBitsToFloat(state);
	return result;
}

vec2 hash2(inout float seed)
{
	return vec2(randomFloat(seed), randomFloat(seed));
}

vec3 hash3(inout float seed)
{
	return vec3(randomFloat(seed), randomFloat(seed), randomFloat(seed));
}

vec3 cosineSampleHemisphere(vec3 N, inout float seed)
{
	vec2 xi = hash2(seed);
	float phi = 2.0 * PI * xi.x;
	float cosTheta = sqrt(xi.y);
	float sinTheta = sqrt(max(1.0 - xi.y, 0.0));

	vec3 H;
	H.x = cos(phi) * sinTheta;
	H.y = sin(phi) * sinTheta;
	H.z = cosTheta;

	vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 tVec = cross(up, N);
	float tLen = length(tVec);
	vec3 tangent = tLen > 1e-4 ? tVec / tLen : vec3(1.0, 0.0, 0.0);
	vec3 bitangent = cross(N, tangent);

	return tangent * H.x + bitangent * H.y + N * H.z;
}

// Returns the linearised (eye-space) depth from a [0,1] depth-buffer value.
float linearizeGBufferDepth(float d)
{
	float z_n = d * 2.0 - 1.0;
	return (2.0 * u_near * u_far) / (u_far + u_near - z_n * (u_far - u_near));
}

// Projects a world-space point into GBuffer UV coordinates.
vec2 worldToGBufferUV(vec3 worldPos, out bool valid)
{
	vec4 clipPos = u_viewProjection * vec4(worldPos, 1.0);

	if (clipPos.w <= 0.0)
	{
		valid = false;
		return vec2(0.0);
	}

	vec3 ndc = clipPos.xyz / clipPos.w;

	const float MARGIN = 0.02;
	if (abs(ndc.x) > 1.0 - MARGIN || abs(ndc.y) > 1.0 - MARGIN || ndc.z > 1.0 || ndc.z < -1.0)
	{
		valid = false;
		return vec2(0.0);
	}

	valid = true;
	return ndc.xy * 0.5 + 0.5;
}

// Approximate signed distance to the rasterized mesh surface.
float meshSDF(vec3 p)
{
	bool valid;
	vec2 uv = worldToGBufferUV(p, valid);

	if (!valid)
		return u_far;

	float frontDepthRaw = texture(t_depthTexture, uv).r;
	if (frontDepthRaw > 1.0 - 0.0001)
		return u_far;

	float backDepthRaw = texture(t_gbufferBackDepth, uv).r;

	float frontDepthLinear = linearizeGBufferDepth(frontDepthRaw);
	float backDepthLinear = linearizeGBufferDepth(backDepthRaw);

	vec4 clipPos = u_viewProjection * vec4(p, 1.0);
	float sampleDepthRaw = clipPos.z / clipPos.w * 0.5 + 0.5;
	float sampleDepthLinear = linearizeGBufferDepth(sampleDepthRaw);

	bool insideSlab = (sampleDepthLinear > frontDepthLinear) && (sampleDepthLinear < backDepthLinear);
	const float SHADOW_BIAS = 0.05;

	if (insideSlab)
	{
		vec3 meshWorldPos = texture(t_gbufferWorldPos, uv).rgb;
		float distToFront = length(p - meshWorldPos);

		if (distToFront < SHADOW_BIAS)
		{
			return SHADOW_BIAS - distToFront;
		}

		float zDistToBack = backDepthLinear - sampleDepthLinear;
		if (zDistToBack < SHADOW_BIAS)
		{
			return SHADOW_BIAS - zDistToBack;
		}

		return 0.0;
	}
	else
	{
		if (sampleDepthLinear <= frontDepthLinear)
		{
			vec3 meshWorldPos = texture(t_gbufferWorldPos, uv).rgb;
			float distToFront = length(p - meshWorldPos);
			return min(distToFront + SHADOW_BIAS, 2.0);
		}
		else
		{
			float zDistToBack = sampleDepthLinear - backDepthLinear;
			return min(zDistToBack * 0.5 + SHADOW_BIAS, 2.0);
		}
	}
}

// Injected shape helper functions or scene definitions
#include "helper_functions"

float sample3DTextureSdf(vec3 p)
{
	vec3 q = p - u_sdfTextureOffset;
	vec3 boxHalf = vec3(u_sdfTextureWorldSize * 0.5);
	vec3 dBox = abs(q) - boxHalf;
	float distToBox = length(max(dBox, 0.0));

	vec3 uvw = (q / u_sdfTextureWorldSize) + 0.5;
	float texDist = texture(t_sdfTexture3D, clamp(uvw, 0.0, 1.0)).r;

	if (all(greaterThanEqual(uvw, vec3(0.0))) && all(lessThanEqual(uvw, vec3(1.0))))
	{
		return texDist;
	}
	else
	{
		return distToBox + texDist;
	}
}

vec3 sceneSdf(vec3 p)
{
	float minDist = u_far;
	int finalMaterialId = 0;
	float globalBlend = 0.0;

	// Injected shape SDF code
	#include "shapes"

#ifdef MESH_SHADOW_SDF
	if (u_enableMeshShadows == 1)
	{
		float mDist = meshSDF(p);
		if (mDist < minDist)
		{
			minDist = mDist;
			finalMaterialId = 15;
		}
	}
#endif

	if (u_enableSdf3DTexture == 1)
	{
		float texDist = sample3DTextureSdf(p);
		if (texDist < minDist)
		{
			minDist = texDist;
			finalMaterialId = u_sdfTextureMaterialId;
		}
	}

	return vec3(minDist, max(float(finalMaterialId), 0.0), globalBlend);
}

vec3 getMaterial(vec3 p, int id)
{
	vec3 color = id < 16 ? u_materials[id].color.xyz : (0.83 * vec3(1.0));
	vec3 secondaryColor = id < 16 ? u_materials[id].secondaryColor.xyz : vec3(0.0);
	int pattern = id < 16 ? u_materials[id].pattern : 0;
	float patternScale = id < 16 ? u_materials[id].patternScale : 1.0;

	if (pattern > 0)
	{
		float patternShape = 0.0;
		vec3 sp = p * patternScale;

		if (pattern == 1) // Checkers
		{
			patternShape = mod(floor(sp.x) + floor(sp.y) + floor(sp.z), 2.0);
		}
		else if (pattern == 2) // Perlin Noise
		{
			patternShape = perlin3D(sp.xyz * 10.0);
		}
		else if (pattern == 3) // Waves
		{
			patternShape = (sin(10.0 * perlin3D(sp.xyz * 10.0)) + 1.0) * 0.5;
			patternShape = smoothstep(0.3, 0.7, patternShape);
		}

		return mix(color, secondaryColor, patternShape);
	}

	return color;
}

struct RayHit
{
	float dist;
	bool hit;
};

RayHit rayMarch(vec3 ro, vec3 rd)
{
	float hit = 1.0;
	float traveled = u_near;
	bool hasHit = false;

	for (int i = 0; i < MAX_STEPS; i++)
	{
		vec3 p = ro + (traveled * rd);
		hit = sceneSdf(p).x;

		// Distance-adaptive precision: scale threshold with distance
		float precis = max(RAYMARCH_EPSILON, traveled * 0.0005);

		if (abs(hit) < precis)
		{
			hasHit = true;
			break;
		}

		if (traveled > u_far)
			break;

		traveled += hit;
	}

	// Also treat rays that reached near the surface before running out of steps as hits
	if (!hasHit && traveled <= u_far)
	{
		float fallbackPrecis = max(RAYMARCH_EPSILON, traveled * 0.002);
		if (abs(hit) < fallbackPrecis)
		{
			hasHit = true;
		}
	}

	return RayHit(traveled, hasHit && (traveled <= u_far));
}

vec3 get3DTextureNormal(vec3 p)
{
	float voxelSize = u_sdfTextureWorldSize / float(textureSize(t_sdfTexture3D, 0).x);
	vec3 e = vec3(voxelSize, 0.0, 0.0);
	float dx = sample3DTextureSdf(p + e.xyz) - sample3DTextureSdf(p - e.xyz);
	float dy = sample3DTextureSdf(p + e.yxz) - sample3DTextureSdf(p - e.yxz);
	float dz = sample3DTextureSdf(p + e.yzx) - sample3DTextureSdf(p - e.yzx);
	vec3 n = vec3(dx, dy, dz);
	float nLen = length(n);
	return nLen > 1e-5 ? n / nLen : vec3(0.0, 1.0, 0.0);
}

float getSurfaceBias(vec3 p)
{
	float bias = 0.01;
	if (u_enableSdf3DTexture == 1)
	{
		float texRes = float(textureSize(t_sdfTexture3D, 0).x);
		if (texRes > 1.0)
		{
			float voxelSize = u_sdfTextureWorldSize / texRes;
			vec3 q = abs(p - u_sdfTextureOffset) - vec3(u_sdfTextureWorldSize * 0.5);
			float distToBox = length(max(q, vec3(0.0)));
			if (distToBox <= voxelSize)
			{
				bias = max(bias, voxelSize * 0.75);
			}
		}
	}
	return bias;
}

vec3 getNormal(vec3 p)
{
	vec3 scene = sceneSdf(p);
	if (u_enableSdf3DTexture == 1 && int(scene.y) == u_sdfTextureMaterialId)
	{
		return get3DTextureNormal(p);
	}

	vec2 e = vec2(NORMAL_EPSILON, 0.0);
	vec3 n = vec3(scene.x) - vec3(sceneSdf(p - e.xyy).x, sceneSdf(p - e.yxy).x, sceneSdf(p - e.yyx).x);
	float nLen = length(n);
	return nLen > 1e-5 ? n / nLen : vec3(0.0, 1.0, 0.0);
}

vec3 getSkyColor(vec3 dir)
{
	float t = max(dir.y, 0.0);
	vec3 zenithColor = vec3(0.05, 0.25, 0.7);
	vec3 horizonColor = vec3(0.5, 0.6, 0.7);
	vec3 groundColor = vec3(0.1, 0.1, 0.12);

	vec3 sky = mix(horizonColor, zenithColor, pow(t, 0.85));
	if (dir.y < 0.0)
	{
		sky = mix(horizonColor, groundColor, pow(-dir.y, 0.85));
	}

	if (u_numLights > 0 && u_lights[0].type == 0)
	{
		float sun = max(dot(dir, normalize(u_lights[0].direction)), 0.0);
		sky += vec3(1.0, 0.85, 0.6) * pow(sun, 4000.0) * 2.0;
		sky += vec3(1.0, 0.6, 0.2) * pow(sun, 200.0) * 0.4;
		sky *= u_lights[0].color.w > 0.0 ? 1.0 : 0.0;
	}

	return sky;
}

vec3 getFogColor(vec3 dir)
{
	// Atmosphere fog color matches the horizon sky color in that azimuthal direction
	vec2 horiz = dir.xz;
	float len = length(horiz);
	vec3 horizonDir = (len > 0.0001) ? vec3(horiz.x / len, 0.0, horiz.y / len) : vec3(0.0, 0.0, 1.0);
	return getSkyColor(horizonDir);
}

vec3 pathTrace(vec3 p, vec3 rd, vec3 initialColor, int materialId, vec3 firstN, inout float seed)
{
	int currentMatId = clamp(materialId, 0, 15);

	vec3 finalColor = max(initialColor - vec3(1.0), vec3(0.0)) + (u_materials[currentMatId].emission * initialColor);
	vec3 currentAlbedo = min(initialColor, vec3(1.0));

	vec3 currentRd = rd;
	float roughness = u_materials[currentMatId].roughness;
	float f0 = u_materials[currentMatId].metallic;

	float fresnelPower = 5.0;
	float specExponent = 2000.0;
	vec3 specMultiplier = vec3(5.0);
	vec3 throughput = vec3(1.0);

	for (int bounce = 0; bounce <= u_rayBounces; bounce++)
	{
		float currentF0 = f0;
		float currentRoughness = u_rayBounces == 1 ? max(roughness, 0.2)
												  : min(roughness + (float(bounce) * 0.1 / float(u_rayBounces)), 1.0);
		fresnelPower = mix(50.0, 1.0, currentF0);

		vec3 N = (bounce == 0) ? firstN : getNormal(p);
		float bias = getSurfaceBias(p);

		float fresnel = currentF0 + ((1.0 - currentF0) * pow(clamp(1.0 - dot(-currentRd, N), 0.0, 1.0), fresnelPower));

		if (bounce == u_rayBounces && bounce > 0)
		{
			fresnel = 0.0;
		}

		float randVal = hash2(seed).x;
		bool isSpecular = randVal < fresnel;

		// Direct Lighting from all active lights
		vec3 directLighting = vec3(0.0);

		for (int i = 0; i < u_numLights; i++)
		{
			Light light = u_lights[i];
			vec3 L;
			float lightDist;
			float attenuation = 1.0;

			if (light.type == 0) // Directional
			{
				L = normalize(light.direction + (vec3(hash2(seed), hash2(seed).x) * 2.0 - 1.0) * 0.05);
				lightDist = u_far * 0.5;
			}
			else if (light.type == 1) // Point
			{
				vec3 lightVec = light.position - p;
				lightDist = length(lightVec);
				vec3 jitter = (vec3(hash2(seed), hash2(seed).x) * 2.0 - 1.0) * 0.25;
				L = normalize(lightVec + jitter);

				float a = 3.0;
				float b = 0.7;
				attenuation = 10.0 / (a * lightDist * lightDist + b * lightDist + 1.0);
			}
			else // Cone / Spot
			{
				vec3 lightVec = light.position - p;
				lightDist = length(lightVec);
				vec3 jitter = (vec3(hash2(seed), hash2(seed).x) * 2.0 - 1.0) * 0.15;
				L = normalize(lightVec + jitter);

				float a = 3.0;
				float b = 0.7;
				attenuation = 10.0 / (a * lightDist * lightDist + b * lightDist + 1.0);

				vec3 surfaceToLight = normalize(light.position - p);
				float spotEffect = dot(surfaceToLight, normalize(light.direction));
				attenuation *= smoothstep(0.80, 0.95, spotEffect);
			}

			RayHit shadowHit = rayMarch(p + N * bias, L);
			bool hitObstacle = shadowHit.hit && (shadowHit.dist < lightDist);

			if (!hitObstacle)
			{
				vec3 diffuse = currentAlbedo * max(dot(N, L), 0.0) * (1.0 - fresnel);
				vec3 hVec = L - currentRd;
				float hLen = length(hVec);
				vec3 H = hLen > 1e-4 ? hVec / hLen : N;
				float specPow = pow(max(dot(N, H), 0.0), specExponent);
				vec3 specular = specMultiplier * specPow * fresnel;

				directLighting += (diffuse + specular) * attenuation * light.color.xyz * light.color.w;
			}
		}

		finalColor += throughput * directLighting;

		if (bounce == u_rayBounces)
		{
			if (bounce > 0)
			{
				finalColor += throughput * currentAlbedo * getSkyColor(N) * 0.15;
			}
			break;
		}

		// Indirect bounce
		vec3 diffuseDir = cosineSampleHemisphere(N, seed);
		vec3 reflectDir = reflect(currentRd, N);
		vec3 jVec = vec3(hash2(seed), hash2(seed).x) * 2.0 - 1.0;
		float jLen = length(jVec);
		vec3 jitter = jLen > 1e-4 ? jVec / jLen : vec3(0.0);
		vec3 sVec = reflectDir + jitter * currentRoughness;
		float sLen = length(sVec);
		vec3 specularDir = sLen > 1e-4 ? sVec / sLen : reflectDir;
		if (dot(specularDir, N) < 0.0)
			specularDir = reflectDir;

		vec3 bounceDir = isSpecular ? specularDir : diffuseDir;
		currentRd = bounceDir;

		RayHit bounceHit = rayMarch(p + N * bias, bounceDir);
		float d = bounceHit.dist;

		if (isSpecular)
			throughput *= mix(vec3(1.0), currentAlbedo, currentF0);
		else
			throughput *= currentAlbedo;

		bool bounceHitObstacle = bounceHit.hit && (d < u_far);

		// Distance fog along the bounce ray
		float bounceFogStart = u_far * 0.50;
		float bounceFogEnd = u_far * 0.95;
		float bounceFog = smoothstep(bounceFogStart, bounceFogEnd, d);
		vec3 bounceFogColor = getFogColor(bounceDir);
		float bounceFactor = isSpecular ? 1.0 : 0.5;

		if (!bounceHitObstacle)
		{
			float skyIntensity = isSpecular ? 1.0 : 0.5;
			vec3 skyCol = (bounceDir.y < 0.0) ? bounceFogColor : getSkyColor(bounceDir);
			finalColor += throughput * skyCol * skyIntensity;
			break;
		}
		else
		{
			// Apply atmospheric distance fog to the reflection
			finalColor += throughput * bounceFogColor * bounceFog * bounceFactor;
			throughput *= (1.0 - bounceFog);

			if (bounceFog >= 1.0)
			{
				break;
			}

			p = p + N * bias + d * bounceDir;
			vec3 preBounceScene = sceneSdf(p);
			float bounceBlend = preBounceScene.z;
			float bounceJitterMask = smoothstep(0.0, 0.5, bounceBlend);

			vec3 bounceMaterialPos = p + (hash3(seed) - 0.5) * 0.04 * max(d, 1.0) * bounceJitterMask;
			vec3 bounceScene = sceneSdf(bounceMaterialPos);
			int bounceMatId = clamp(int(bounceScene.y), 0, 15);
			vec3 bounceMaterial = getMaterial(bounceMaterialPos, bounceMatId);

			roughness = u_materials[bounceMatId].roughness;
			f0 = u_materials[bounceMatId].metallic;

			vec3 emission = max(bounceMaterial - vec3(1.0), vec3(0.0)) + (u_materials[bounceMatId].emission * bounceMaterial);
			currentAlbedo = min(bounceMaterial, vec3(1.0));

			finalColor += throughput * emission;
		}
	}

	return finalColor;
}

struct HitData
{
	vec3 color;
	vec3 albedo;
	vec3 normal;
	vec3 worldPos;
	float linearDepth;
	bool hit;
};

HitData render(in vec2 uv)
{
	vec3 ro = -u_camMatrix[3].xyz;
	ro = vec3(dot(u_camMatrix[0].xyz, ro), dot(u_camMatrix[1].xyz, ro), dot(u_camMatrix[2].xyz, ro));

	vec3 rd = (vec4(normalize(vec3(uv, -u_fov)), 0.0) * u_camMatrix).xyz;

	ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
	float meshDepthSample = texelFetch(t_depthTexture, pixelCoord, 0).r;
	float meshDepthLinear = linearizeGBufferDepth(meshDepthSample);

	RayHit sdfHit = rayMarch(ro, rd);
	float object = sdfHit.dist;
	bool sdfValid = sdfHit.hit && (object < u_far);

	vec3 rdCam = normalize(vec3(uv, -u_fov));
	float absCosAngle = max(abs(rdCam.z), 0.0001);
	float planarDepth = object * absCosAngle;
	float minDepth = sdfValid ? planarDepth : u_far;

	float z_e = max(planarDepth, u_near);
	float z_n = (u_far + u_near - (2.0 * u_near * u_far) / z_e) / (u_far - u_near);
	float sdfDepthNDC = sdfValid ? ((z_n + 1.0) * 0.5) : 1.0;

	bool meshValid = (meshDepthSample < 1.0 - 0.0001);
	bool meshInFront = meshValid && (!sdfValid || meshDepthLinear < minDepth);

	// Atmospheric distance fog: keep foreground and midground clear, fade smoothly into horizon in the distance
	float fogStart = u_far * 0.50;
	float fogEnd = u_far * 0.95;

	// Distant atmospheric horizon fog color
	vec3 fogColor = getFogColor(rd);

	uint pixelIndex = uint(pixelCoord.x) + uint(pixelCoord.y) * uint(u_resolution.x);
	uint temporalSeed = uint(u_frameCounter) ^ floatBitsToUint(u_time);
	uint state = pixelIndex ^ (temporalSeed * 747796405u + 2891336453u);
	pcg(state);
	float seed = uintBitsToFloat(state);

	HitData hit;

	if (meshInFront)
	{
		vec3 meshAlbedo = texelFetch(t_gbufferAlbedo, pixelCoord, 0).rgb;
		vec3 meshWorldPos = texelFetch(t_gbufferWorldPos, pixelCoord, 0).rgb;
		vec3 rawNorm = texelFetch(t_gbufferNormal, pixelCoord, 0).rgb;
		vec3 meshNormal = length(rawNorm) > 0.001 ? normalize(rawNorm) : vec3(0.0, 1.0, 0.0);

		int materialId = texelFetch(t_gbufferMaterial, pixelCoord, 0).r;
		meshAlbedo *= getMaterial(meshWorldPos, materialId);

		vec3 col = pathTrace(meshWorldPos, rd, meshAlbedo, materialId, meshNormal, seed);

		// Fog at mesh depth (Euclidean distance along ray)
		float meshEuclideanDist = meshDepthLinear / absCosAngle;
		float fog = smoothstep(fogStart, fogEnd, meshEuclideanDist);
		col = mix(col, fogColor, fog);

		gl_FragDepth = meshDepthSample;

		hit.color = col;
		hit.albedo = meshAlbedo;
		hit.normal = meshNormal;
		hit.worldPos = meshWorldPos;
		hit.linearDepth = meshDepthLinear;
		hit.hit = true;
	}
	else if (sdfValid)
	{
		vec3 p = ro + object * rd;

		vec3 preScene = sceneSdf(p);
		float blendFactor = preScene.z;
		float jitterMask = smoothstep(0.0, 0.5, blendFactor);
		vec3 materialSamplePos = p + (hash3(seed) - 0.5) * 0.01 * max(object, 5.0) * jitterMask;

		vec3 sceneResult = sceneSdf(materialSamplePos);
		int materialId = int(sceneResult.y);
		vec3 baseColor = getMaterial(materialSamplePos, materialId);
		vec3 norm = getNormal(p);

		vec3 col = pathTrace(p, rd, baseColor, materialId, norm, seed);

		// Fog at SDF depth (Euclidean distance along ray)
		float fog = smoothstep(fogStart, fogEnd, object);
		col = mix(col, fogColor, fog);

		gl_FragDepth = sdfDepthNDC;

		hit.color = col;
		hit.albedo = baseColor;
		hit.normal = norm;
		hit.worldPos = p;
		hit.linearDepth = minDepth;
		hit.hit = true;
	}
	else
	{
		gl_FragDepth = 1.0;

		vec3 skyColor = (rd.y < 0.0) ? fogColor : getSkyColor(rd);
		hit.color = skyColor;
		hit.albedo = vec3(1.0);
		hit.normal = -rd;
		hit.worldPos = ro + rd * u_far;
		hit.linearDepth = u_far;
		hit.hit = false;
	}

	if (isnan(hit.color.r) || isnan(hit.color.g) || isnan(hit.color.b) || isinf(hit.color.r) || isinf(hit.color.g) || isinf(hit.color.b))
	{
		vec3 fallbackSky = (rd.y < 0.0) ? fogColor : getSkyColor(rd);
		hit.color = hit.hit ? hit.albedo * 0.15 : fallbackSky;
	}

	return hit;
}

float rand(vec2 co)
{
	return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
	vec2 screenUV = v_texCoord;
	vec2 uv = (2.0 * v_texCoord) - 1.0;
	float aspectRatio = u_resolution.x / u_resolution.y;

#ifdef ANTIALIASING
	// Only apply subpixel camera jitter during offline accumulation; keep primary rays centered for SVGF
	if (u_enableDenoising == 0)
	{
		uv += (vec2(rand(uv + u_time), rand(uv + u_time * 1.5)) - 0.5) * 2.0 / u_resolution;
	}
#endif

	uv.x *= aspectRatio;

	HitData hit = render(uv);
	vec3 col = hit.color;

	// Firefly suppression
	float maxComp = max(col.r, max(col.g, col.b));
	if (maxComp > 4.0)
	{
		col *= 4.0 / maxComp;
	}

	if (u_enableDenoising == 0)
	{
		// Legacy static accumulation mode
		col = mix(vec3(0.5), col, u_contrast);
		col = clamp(col, 0.0, 1.0);

		if (u_frameCounter > 0)
		{
			vec4 prevColor = texture(t_previousColor, screenUV);
			float weight = 1.0 / float(u_frameCounter + 1);
			col = mix(prevColor.xyz, col, weight);
		}

		out_illumination = vec4(col, hit.linearDepth);
		out_normal = vec4(hit.normal, hit.hit ? 1.0 : 0.0);
		out_worldPos = vec4(hit.worldPos, 1.0);
		out_albedo = vec4(hit.albedo, 1.0);
		return;
	}

	// Real-time SVGF mode: Demodulate illumination from albedo
	vec3 illumination = col;
	if (u_demodulateAlbedo == 1 && hit.hit)
	{
		vec3 safeAlbedo = max(hit.albedo, vec3(0.05));
		illumination = col / safeAlbedo;
	}

	out_illumination = vec4(illumination, hit.linearDepth);
	out_normal = vec4(hit.normal, hit.hit ? 1.0 : 0.0);
	out_worldPos = vec4(hit.worldPos, 1.0);
	out_albedo = vec4(hit.albedo, 1.0);
}
