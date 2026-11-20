// Store an octahedral normal as an exact integer in a 32-bit float. Zero marks an empty pixel.
float packCaptureNormal(vec3 n)
{
	n /= max(dot(abs(n), vec3(1.0)), 1e-8);
	vec2 signs = mix(vec2(-1), vec2(1), greaterThanEqual(n.xy, vec2(0)));
	vec2 oct = n.z < 0.0 ? (1.0 - abs(n.yx)) * signs : n.xy;
	uvec2 bits = uvec2(round((oct * 0.5 + 0.5) * 2047.0));
	return float(1u + bits.x + (bits.y << 11u));
}

vec3 unpackCaptureNormal(float code)
{
	uint bits = uint(code) - 1u;
	vec2 oct = vec2(bits & 2047u, bits >> 11u) * (2.0 / 2047.0) - 1.0;
	vec3 n = vec3(oct, 1.0 - abs(oct.x) - abs(oct.y));
	vec2 signs = mix(vec2(-1), vec2(1), greaterThanEqual(n.xy, vec2(0)));
	n.xy -= signs * max(-n.z, 0.0);
	return normalize(n);
}
