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
uniform sampler3D t_sdfTexture3D;       // 3D volumetric SDF texture

// 3D SDF texture parameters
uniform int u_enableSdf3DTexture;
uniform float u_sdfTextureWorldSize;
uniform vec3 u_sdfTextureOffset;
uniform int u_sdfTextureMaterialId;
uniform int u_unsignedMeshDistance;
uniform int u_approximateMeshDistance;

// Spatiotemporal Blue Noise (STBN) textures
uniform sampler3D t_stbn0;              // RGB: 3D vector, A: 1D scalar void-and-cluster
uniform sampler3D t_stbn1;              // RG: 2D vector; BA: unused packed data
uniform int u_enableBlueNoise;          // 0 = White Noise (PCG PRNG), 1 = STBN
uniform int u_noiseFrameIndex;          // Continuously advancing noise frame index
uniform int u_samplingSeed;            // Fixed stream offset for repeatable experiments

// Path tracer settings
uniform int u_frameCounter;
uniform int u_rayBounces;
uniform float u_contrast;
uniform int u_enableDenoising;
uniform int u_demodulateAlbedo;

// Camera
uniform mat4 u_camMatrix;
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

float randomFloat(inout uint state)
{
	return float(pcg(state)) * (1.0 / 4294967296.0);
}

vec2 hash2(inout uint state)
{
	return vec2(randomFloat(state), randomFloat(state));
}

vec3 hash3(inout uint state)
{
	return vec3(randomFloat(state), randomFloat(state), randomFloat(state));
}

// Each bounce reserves domains for the lobe, diffuse direction, specular direction, material, and eight lights.
const int NOISE_DOMAINS_PER_BOUNCE = 12;
const vec2 R2_SEQUENCE = vec2(0.7548777, 0.5698403);

ivec3 noiseCoord(int domain)
{
	// Keep each spatial offset fixed over time to preserve the STBN temporal sequence.
	ivec2 offset = ivec2(floor(fract(float(domain) * R2_SEQUENCE) * 128.0));
	uint stream = uint(u_samplingSeed) * 747796405u;
	if (u_enableDenoising == 0 && u_frameCounter >= 64)
	{
		uint cycle = uint(u_frameCounter) / 64u;
		stream ^= cycle * 277803737u;
	}
	offset += ivec2(int(stream & 127u), int((stream >> 7u) & 127u));
	ivec2 pixel = (ivec2(gl_FragCoord.xy) + offset) & ivec2(127);
	return ivec3(pixel, u_noiseFrameIndex & 63);
}

vec4 decodeNoise(vec4 value)
{
	// Use bin centers so the normalized samples never reach zero or one.
	return (value * 255.0 + 0.5) / 256.0;
}

float sampleNoise1(int domain, inout uint state)
{
	if (u_enableBlueNoise == 0) return randomFloat(state);
	float val = decodeNoise(texelFetch(t_stbn0, noiseCoord(domain), 0)).w;
	if (u_enableDenoising == 0 && u_frameCounter >= 64)
	{
		val = fract(val + randomFloat(state));
	}
	return val;
}

vec2 sampleNoise2(int domain, inout uint state)
{
	if (u_enableBlueNoise == 0) return hash2(state);
	vec2 val = decodeNoise(texelFetch(t_stbn1, noiseCoord(domain), 0)).xy;
	if (u_enableDenoising == 0 && u_frameCounter >= 64)
	{
		val = fract(val + hash2(state));
	}
	return val;
}

vec3 sampleNoise3(int domain, inout uint state)
{
	if (u_enableBlueNoise == 0) return hash3(state);
	vec3 val = decodeNoise(texelFetch(t_stbn0, noiseCoord(domain), 0)).xyz;
	if (u_enableDenoising == 0 && u_frameCounter >= 64)
	{
		val = fract(val + hash3(state));
	}
	return val;
}

vec3 cosineSampleHemisphere(vec3 N, vec2 xi)
{
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

// Injected shape helper functions or scene definitions
#include "helper_functions"

float sample3DTextureSdf(vec3 p)
{
	vec3 q = p - u_sdfTextureOffset;
	vec3 boxHalf = vec3(u_sdfTextureWorldSize * 0.5);
	vec3 dBox = abs(q) - boxHalf;
	float distToBox = length(max(dBox, 0.0));

	vec3 uvw = (q / u_sdfTextureWorldSize) + 0.5;
	float texDist = textureLod(t_sdfTexture3D, clamp(uvw, 0.0, 1.0), 0.0).r;

	if (u_unsignedMeshDistance == 1)
	{
		// Cover the <= 0.5 voxel trilinear maximum of an unsigned distance field on any 2D surface.
		float voxelSize = u_sdfTextureWorldSize / float(textureSize(t_sdfTexture3D, 0).x);
		texDist -= 0.55 * voxelSize;
	}

	if (distToBox > 0.0)
	{
		return max(distToBox + 0.002, texDist - distToBox);
	}
	return texDist;
}

bool sampleStaticVolume = true;
vec3 sceneSdf(vec3 p)
{
	float minDist = u_far;
	int finalMaterialId = 0;
	float globalBlend = 0.0;

	// Injected shape SDF code
	#include "shapes"

	if (sampleStaticVolume && u_enableSdf3DTexture == 1)
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

RayHit rayMarch(vec3 ro, vec3 rd, float startDistance, float maxDistance, bool occlusionOnly)
{
	bool useVolume = sampleStaticVolume && u_enableSdf3DTexture == 1;
	float voxelSize = useVolume ? u_sdfTextureWorldSize / float(textureSize(t_sdfTexture3D, 0).x) : 0.0;
	float traveled = startDistance;
	float previousT = traveled;
	float previousDist = 0.0;
	int stepLimit = useVolume ? 2 * MAX_STEPS : MAX_STEPS;

	for (int i = 0; i < stepLimit && traveled <= maxDistance; ++i)
	{
		vec3 p = ro + (traveled * rd);
		vec3 surface = sceneSdf(p);
		float distance = surface.x;
		bool fromVolume = useVolume && int(surface.y) == u_sdfTextureMaterialId;

		// Check crossings before a signed distance can move the ray backwards.
		if ((useVolume || occlusionOnly) && i > 0 && (distance < 0.0) != (previousDist < 0.0))
		{
			// A sign change between samples is enough to confirm occlusion.
			if (occlusionOnly)
				return RayHit(previousT, true);

			float lo = previousT;
			float hi = traveled;
			for (int j = 0; j < 10; ++j)
			{
				float mid = (lo + hi) * 0.5;
				float midDist = sceneSdf(ro + mid * rd).x;
				if ((midDist < 0.0) == (previousDist < 0.0))
					lo = mid;
				else
					hi = mid;
			}
			return RayHit((lo + hi) * 0.5, true);
		}

		float hitTolerance = max(RAYMARCH_EPSILON, traveled * 0.001);
		if (fromVolume)
		{
			hitTolerance = min(hitTolerance, max(RAYMARCH_EPSILON, voxelSize * 0.05));
		}
		if (abs(distance) < hitTolerance)
			return RayHit(traveled, true);

		if (occlusionOnly && traveled == maxDistance)
			return RayHit(traveled, false);

		float advance = useVolume ? abs(distance) : distance;
		if (fromVolume && u_approximateMeshDistance == 1)
		{
			vec3 q = abs(p - u_sdfTextureOffset);
			if (all(lessThanEqual(q, vec3(u_sdfTextureWorldSize * 0.5))))
			{
				// Keep a safety margin where approximate grid distances can slightly overestimate.
				advance *= 0.85;
			}
		}
		previousT = traveled;
		previousDist = distance;
		traveled += advance;
		if (occlusionOnly)
			traveled = min(traveled, maxDistance);
	}

	if (!useVolume && traveled <= maxDistance)
	{
		float finalDistance = sceneSdf(ro + traveled * rd).x;
		return RayHit(traveled, abs(finalDistance) < max(RAYMARCH_EPSILON, traveled * 0.002));
	}
	return RayHit(traveled, false);
}

vec3 get3DTextureNormal(vec3 p)
{
	float voxelSize = u_sdfTextureWorldSize / float(textureSize(t_sdfTexture3D, 0).x);
	vec3 e = vec3(voxelSize * 0.5, 0.0, 0.0);
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
				bias = max(bias, voxelSize * 0.125);
			}
		}
	}
	return bias;
}

vec3 getNormal(vec3 p)
{
	vec3 scene = sceneSdf(p);
	if (sampleStaticVolume && u_enableSdf3DTexture == 1 && int(scene.y) == u_sdfTextureMaterialId)
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

#include "../common/dynamic_grid.glsl"
layout(binding = 10) uniform sampler3D t_dynamicField;
layout(binding = 11) uniform sampler3D t_dynamicCapture;
uniform int u_hasDynamicField;
uniform int u_meshView;

float dynamicFieldHit(sampler3D field, vec3 origin, vec3 direction, float limit, bool occlusionOnly)
{
	vec3 localOrigin = dynamicGridInvRot * (origin - dynamicGridCenter);
	vec3 localDir = dynamicGridInvRot * direction;
	vec3 safeDir = mix(localDir, vec3(1e-8), lessThan(abs(localDir), vec3(1e-8)));
	vec3 halfExtent = vec3(0.5 * dynamicGridSize);
	vec3 a = (-halfExtent - localOrigin) / safeDir;
	vec3 b = (halfExtent - localOrigin) / safeDir;
	vec3 entry = min(a, b), leave = max(a, b);
	float t = max(0.0, max(entry.x, max(entry.y, entry.z)));
	float end = min(limit, min(leave.x, min(leave.y, leave.z)));
	if (t > end) return -1.0;

	float voxel = dynamicGridSize / float(textureSize(field, 0).x);
	float prevT = t;
	float prevD = 0.0;
	for (int i = 0; i < 192 && t <= end; ++i)
	{
		vec3 uvw = (localOrigin + localDir * t) / dynamicGridSize + 0.5;
		float d = textureLod(field, uvw, 0.0).r;
		if (i > 0 && (d < 0.0) != (prevD < 0.0))
		{
			if (occlusionOnly) return prevT;
			float lo = prevT;
			float hi = t;
			for (int j = 0; j < 8; ++j)
			{
				float mid = 0.5 * (lo + hi);
				vec3 midUvw = (localOrigin + localDir * mid) / dynamicGridSize + 0.5;
				float midD = textureLod(field, midUvw, 0.0).r;
				if ((midD < 0.0) == (prevD < 0.0)) lo = mid;
				else hi = mid;
			}
			return 0.5 * (lo + hi);
		}
		if (abs(d) <= 0.01 * voxel) return t;
		if (i == 0 && d < 0.0) return t;
		prevT = t;
		prevD = d;
		t += clamp(abs(d) * 0.75, 0.05 * voxel, 2.0 * voxel);
	}
	return -1.0;
}

vec3 dynamicFieldNormal(sampler3D field, vec3 p, vec3 direction)
{
	vec3 uvw = (dynamicGridInvRot * (p - dynamicGridCenter)) / dynamicGridSize + 0.5;
	vec3 h = 0.5 / vec3(textureSize(field, 0));
	vec3 gradient;
	for (int axis = 0; axis < 3; ++axis)
	{
		vec3 offset = vec3(0);
		offset[axis] = h[axis];
		gradient[axis] = textureLod(field, uvw + offset, 0).r - textureLod(field, uvw - offset, 0).r;
	}
	vec3 n = dot(gradient, gradient) > 1e-8
		? normalize(transpose(dynamicGridInvRot) * gradient)
		: -direction;
	return faceforward(n, direction, n);
}

vec3 shadeSurface(vec3 p, vec3 rd, vec3 initialColor, int materialId, vec3 firstN, vec2 surfaceParams, inout uint state)
{
#ifdef GEOMETRY_PREVIEW
	float facing = max(dot(firstN, -rd), 0.0);
	float skyFill = firstN.y * 0.5 + 0.5;
	return min(initialColor, vec3(1.0)) * (0.40 + 0.45 * facing + 0.15 * skyFill);
#else
	int currentMatId = clamp(materialId, 0, 15);

	vec3 finalColor = max(initialColor - vec3(1.0), vec3(0.0)) + (u_materials[currentMatId].emission * initialColor);
	vec3 currentAlbedo = min(initialColor, vec3(1.0));

	vec3 currentRd = rd;
	vec3 surfaceNormal = firstN;
	float roughness = surfaceParams.x >= 0.0 ? surfaceParams.x : u_materials[currentMatId].roughness;
	float f0 = surfaceParams.x >= 0.0 ? surfaceParams.y : u_materials[currentMatId].metallic;

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

		vec3 N = surfaceNormal;
		vec3 rayOrigin = p + N * getSurfaceBias(p);
		float startDistance = u_enableSdf3DTexture == 1 ? RAYMARCH_EPSILON : u_near;

		bool terminalBounce = bounce == u_rayBounces && bounce > 0;
		float viewFresnel = currentF0 + (1.0 - currentF0)
			* pow(clamp(1.0 - dot(-currentRd, N), 0.0, 1.0), fresnelPower);

		int noiseDomain = bounce * NOISE_DOMAINS_PER_BOUNCE;
		float randVal = sampleNoise1(noiseDomain, state);
		// Reduce grazing reflections on rough surfaces.
		float specularChance = mix(viewFresnel, currentF0, currentRoughness);
		bool isSpecular = randVal < specularChance;

		// Direct Lighting from all active lights
		vec3 directLighting = vec3(0.0);

		for (int i = 0; i < u_numLights; i++)
		{
			Light light = u_lights[i];
			vec3 L;
			float lightDist;
			float attenuation = 1.0;

			vec3 lightJitter = sampleNoise3(noiseDomain + 4 + i, state);
			if (light.color.w == 0.0)
				continue;

			if (light.type == 0) // Directional
			{
				L = normalize(light.direction + (lightJitter * 2.0 - 1.0) * 0.05);
				lightDist = u_far * 0.5;
			}
			else if (light.type == 1) // Point
			{
				vec3 lightVec = light.position - p;
				lightDist = length(lightVec);
				vec3 jitter = (lightJitter * 2.0 - 1.0) * 0.25;
				L = normalize(lightVec + jitter);

				float a = 3.0;
				float b = 0.7;
				attenuation = 10.0 / (a * lightDist * lightDist + b * lightDist + 1.0);
			}
			else // Cone / Spot
			{
				vec3 lightVec = light.position - p;
				lightDist = length(lightVec);
				vec3 jitter = (lightJitter * 2.0 - 1.0) * 0.15;
				L = normalize(lightVec + jitter);

				float a = 3.0;
				float b = 0.7;
				attenuation = 10.0 / (a * lightDist * lightDist + b * lightDist + 1.0);

				vec3 surfaceToLight = normalize(light.position - p);
				float spotEffect = dot(surfaceToLight, normalize(light.direction));
				attenuation *= smoothstep(0.80, 0.95, spotEffect);
			}

			float directFresnel = 0.0;
			vec3 specular = vec3(0.0);
			if (!terminalBounce)
			{
				vec3 hVec = L - currentRd;
				float hLen = length(hVec);
				vec3 H = hLen > 1e-4 ? hVec / hLen : N;
				// Use the angle to the half vector for this light sample.
				float viewHalfCos = clamp(dot(-currentRd, H), 0.0, 1.0);
				directFresnel = currentF0 + (1.0 - currentF0) * pow(1.0 - viewHalfCos, fresnelPower);
				float specPow = pow(max(dot(N, H), 0.0), specExponent);
				specular = specMultiplier * specPow * directFresnel;
			}

			vec3 diffuse = currentAlbedo * max(dot(N, L), 0.0) * (1.0 - directFresnel);
			vec3 contribution = (diffuse + specular) * attenuation * light.color.xyz * light.color.w;
			if (all(equal(contribution, vec3(0.0))))
				continue;

			RayHit shadowHit = rayMarch(rayOrigin, L, startDistance, min(lightDist, u_far), true);
			if (!shadowHit.hit || shadowHit.dist >= lightDist)
			{
				if (u_hasDynamicField == 0 || currentMatId == 15 || dynamicFieldHit(t_dynamicField, p + N * RAYMARCH_EPSILON, L, lightDist, true) < 0.0)
					directLighting += contribution;
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
		vec2 diffuseXi = sampleNoise2(noiseDomain + 1, state);
		vec3 specJitter = sampleNoise3(noiseDomain + 2, state);

		vec3 diffuseDir = cosineSampleHemisphere(N, diffuseXi);
		vec3 reflectDir = reflect(currentRd, N);
		vec3 jVec = specJitter * 2.0 - 1.0;
		float jLen = length(jVec);
		vec3 jitter = jLen > 1e-4 ? jVec / jLen : vec3(0.0);
		vec3 sVec = reflectDir + jitter * currentRoughness;
		float sLen = length(sVec);
		vec3 specularDir = sLen > 1e-4 ? sVec / sLen : reflectDir;
		if (dot(specularDir, N) < 0.0)
			specularDir = reflectDir;

		vec3 bounceDir = isSpecular ? specularDir : diffuseDir;
		currentRd = bounceDir;

		RayHit bounceHit = rayMarch(rayOrigin, bounceDir, startDistance, u_far, false);
		float d = bounceHit.dist;
		vec3 dynamicOrigin = p + N * RAYMARCH_EPSILON;
		float dynamicT = -1.0;
		if (u_hasDynamicField != 0 && currentMatId != 15)
		{
			float limit = bounceHit.hit ? dot(rayOrigin + d * bounceDir - dynamicOrigin, bounceDir) : u_far;
			dynamicT = dynamicFieldHit(t_dynamicField, dynamicOrigin, bounceDir, limit, false);
		}
		bool hitDynamic = dynamicT >= 0.0;
		if (hitDynamic) d = dynamicT;

		if (isSpecular)
			throughput *= mix(vec3(1.0), currentAlbedo, currentF0);
		else
			throughput *= currentAlbedo;

		bool bounceHitObstacle = (hitDynamic || bounceHit.hit) && (d < u_far);

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

			if (hitDynamic)
			{
				p = dynamicOrigin + d * bounceDir;
				surfaceNormal = dynamicFieldNormal(t_dynamicField, p, bounceDir);
				currentMatId = 15;
				currentAlbedo = getMaterial(p, currentMatId);
				roughness = u_materials[currentMatId].roughness;
				f0 = u_materials[currentMatId].metallic;
				finalColor += throughput * u_materials[currentMatId].emission * currentAlbedo;
				continue;
			}
			p = rayOrigin + d * bounceDir;
			surfaceNormal = getNormal(p);
			vec3 preBounceScene = sceneSdf(p);
			float bounceBlend = preBounceScene.z;
			float bounceJitterMask = smoothstep(0.0, 0.5, bounceBlend);

			vec3 jitterVec = sampleNoise3((bounce + 1) * NOISE_DOMAINS_PER_BOUNCE + 3, state);
			vec3 bounceMaterialPos = p + (jitterVec - 0.5) * 0.04 * max(d, 1.0) * bounceJitterMask;
			vec3 bounceScene = sceneSdf(bounceMaterialPos);
			int bounceMatId = clamp(int(bounceScene.y), 0, 15);
			vec3 bounceMaterial = getMaterial(bounceMaterialPos, bounceMatId);

			currentMatId = bounceMatId;
			roughness = u_materials[bounceMatId].roughness;
			f0 = u_materials[bounceMatId].metallic;

			vec3 emission = max(bounceMaterial - vec3(1.0), vec3(0.0)) + (u_materials[bounceMatId].emission * bounceMaterial);
			currentAlbedo = min(bounceMaterial, vec3(1.0));

			finalColor += throughput * emission;
		}
	}

	return finalColor;
#endif
}

struct HitData
{
	vec3 color;
	vec3 albedo;
	vec3 normal;
	vec3 worldPos;
	float linearDepth;
	bool dynamicSurface;
	bool hit;
};

HitData render(in vec2 uv, inout uint state)
{
	vec3 ro = -u_camMatrix[3].xyz;
	ro = vec3(dot(u_camMatrix[0].xyz, ro), dot(u_camMatrix[1].xyz, ro), dot(u_camMatrix[2].xyz, ro));

	vec3 rd = (vec4(normalize(vec3(uv, -u_fov)), 0.0) * u_camMatrix).xyz;

	ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
	float meshDepthSample = texelFetch(t_depthTexture, pixelCoord, 0).r;
	float meshDepthLinear = linearizeGBufferDepth(meshDepthSample);

	sampleStaticVolume = u_meshView == 0;
	RayHit sdfHit = rayMarch(ro, rd, u_near, u_far, false);
	float dynamicT = -1.0;
	if (u_meshView == 0 && u_hasDynamicField != 0)
	{
		float limit = sdfHit.hit ? sdfHit.dist : u_far;
		float t = dynamicFieldHit(t_dynamicCapture, ro + rd * u_near, rd, limit - u_near, false);
		if (t >= 0.0) dynamicT = t + u_near;
	}
	bool dynamicPrimary = dynamicT >= 0.0;
	if (dynamicPrimary) sdfHit = RayHit(dynamicT, true);
	float object = sdfHit.dist;
	bool sdfValid = sdfHit.hit && (object < u_far);

	vec3 rdCam = normalize(vec3(uv, -u_fov));
	float absCosAngle = max(abs(rdCam.z), 0.0001);
	float planarDepth = object * absCosAngle;
	float minDepth = sdfValid ? planarDepth : u_far;

	float z_e = max(planarDepth, u_near);
	float z_n = (u_far + u_near - (2.0 * u_near * u_far) / z_e) / (u_far - u_near);
	float sdfDepthNDC = sdfValid ? ((z_n + 1.0) * 0.5) : 1.0;

	bool meshValid = u_meshView != 0 && meshDepthSample < 1.0 && texelFetch(t_gbufferWorldPos, pixelCoord, 0).w > 0.5;
	bool meshInFront = meshValid && (!sdfValid || meshDepthLinear < minDepth);

	// Atmospheric distance fog: keep foreground and midground clear, fade smoothly into horizon in the distance
	float fogStart = u_far * 0.50;
	float fogEnd = u_far * 0.95;

	// Distant atmospheric horizon fog color
	vec3 fogColor = getFogColor(rd);

	HitData hit;
	hit.dynamicSurface = false;

	if (meshInFront)
	{
		vec3 meshAlbedo = texelFetch(t_gbufferAlbedo, pixelCoord, 0).rgb;
		vec3 meshWorldPos = texelFetch(t_gbufferWorldPos, pixelCoord, 0).rgb;
		vec4 normalSample = texelFetch(t_gbufferNormal, pixelCoord, 0);
		vec3 rawNorm = normalSample.rgb;
		vec3 meshNormal = length(rawNorm) > 0.001 ? normalize(rawNorm) : vec3(0.0, 1.0, 0.0);

		int materialId = texelFetch(t_gbufferMaterial, pixelCoord, 0).r;
		hit.dynamicSurface = u_hasDynamicField != 0 && materialId == 15;
		vec2 params = vec2(texelFetch(t_gbufferAlbedo, pixelCoord, 0).a, normalSample.a);
		if (params.x < 0.0) meshAlbedo *= getMaterial(meshWorldPos, materialId);
		sampleStaticVolume = true;

		vec3 col = shadeSurface(meshWorldPos, rd, meshAlbedo, materialId, meshNormal, params, state);

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

		int materialId;
		vec3 baseColor, norm;
		if (dynamicPrimary)
		{
			hit.dynamicSurface = true;
			materialId = 15;
			baseColor = getMaterial(p, materialId);
			norm = dynamicFieldNormal(t_dynamicCapture, p, rd);
		}
		else
		{
			vec3 preScene = sceneSdf(p);
			float blendFactor = preScene.z;
			float jitterMask = smoothstep(0.0, 0.5, blendFactor);
			vec3 matJitter = sampleNoise3(3, state);
			vec3 materialSamplePos = p + (matJitter - 0.5) * 0.01 * max(object, 5.0) * jitterMask;

			vec3 sceneResult = sceneSdf(materialSamplePos);
			materialId = int(sceneResult.y);
			baseColor = getMaterial(materialSamplePos, materialId);
			norm = getNormal(p);
		}

		sampleStaticVolume = true;
		vec3 col = shadeSurface(p, rd, baseColor, materialId, norm, vec2(-1), state);

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

void main()
{
	vec2 screenUV = v_texCoord;
	vec2 uv = (2.0 * v_texCoord) - 1.0;
	float aspectRatio = u_resolution.x / u_resolution.y;

	ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
	uvec2 pixelCoordU = uvec2(pixelCoord);
	uint timeBits = floatBitsToUint(u_time);
	uint h = (pixelCoordU.x * 374761393u) ^ (pixelCoordU.y * 668265263u) ^ (uint(u_frameCounter) * 3266489917u) ^ (timeBits * 1274126177u) ^ (uint(u_samplingSeed) * 277803737u);
	h = (h ^ (h >> 13u)) * 1274126177u;
	h = h ^ (h >> 16u);
	uint state = h * 747796405u + 2891336453u;

#ifdef ANTIALIASING
	// Only apply subpixel camera jitter during offline accumulation; keep primary rays centered for SVGF
	if (u_enableDenoising == 0)
	{
		vec2 jitter = hash2(state) - 0.5;
		uv += jitter * 2.0 / u_resolution;
	}
#endif

	uv.x *= aspectRatio;

	HitData hit = render(uv, state);
	vec3 col = hit.color;

	// Firefly suppression
	float maxComp = max(col.r, max(col.g, col.b));
	if (maxComp > 4.0)
	{
		col *= 4.0 / maxComp;
	}

	if (u_enableDenoising == 0)
	{
		// Static linear radiance accumulation
		if (u_frameCounter > 0)
		{
			vec4 prevColor = texelFetch(t_previousColor, pixelCoord, 0);
			float weight = 1.0 / float(u_frameCounter + 1);
			col = mix(prevColor.xyz, col, weight);
		}

		out_illumination = vec4(col, hit.linearDepth);
		out_normal = vec4(hit.normal, hit.hit ? 1.0 : 0.0);
		out_worldPos = vec4(hit.worldPos, hit.dynamicSurface ? 2.0 : 1.0);
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
	out_worldPos = vec4(hit.worldPos, hit.dynamicSurface ? 2.0 : 1.0);
	out_albedo = vec4(hit.albedo, 1.0);
}
