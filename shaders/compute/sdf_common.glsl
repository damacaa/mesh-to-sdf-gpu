// Shared GPU triangle and geometry functions for SDF compute shaders.

struct GpuTriangle
{
	vec4 v0;
	vec4 v1;
	vec4 v2;
	vec4 n0;
	vec4 n1;
	vec4 n2;
};

// Maps an integer voxel index to its world-space center.
vec3 voxelToWorld(ivec3 coord, ivec3 resolution, float worldSize, vec3 worldOffset)
{
	vec3 uvw = (vec3(coord) + 0.5) / vec3(resolution);
	return (uvw - 0.5) * worldSize + worldOffset;
}

// Computes the closest point on triangle ABC to point P and outputs barycentric weights.
vec3 closestPointOnTriangle(vec3 p, vec3 a, vec3 b, vec3 c, out vec3 bary)
{
	vec3 ab = b - a;
	vec3 ac = c - a;
	vec3 ap = p - a;

	float d1 = dot(ab, ap);
	float d2 = dot(ac, ap);
	if (d1 <= 0.0 && d2 <= 0.0)
	{
		bary = vec3(1.0, 0.0, 0.0);
		return a;
	}

	vec3 bp = p - b;
	float d3 = dot(ab, bp);
	float d4 = dot(ac, bp);
	if (d3 >= 0.0 && d4 <= d3)
	{
		bary = vec3(0.0, 1.0, 0.0);
		return b;
	}

	float vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
	{
		float denomAB = d1 - d3;
		float v = (denomAB > 1e-20) ? (d1 / denomAB) : 0.0;
		bary = vec3(1.0 - v, v, 0.0);
		return a + v * ab;
	}

	vec3 cp = p - c;
	float d5 = dot(ab, cp);
	float d6 = dot(ac, cp);
	if (d6 >= 0.0 && d5 <= d6)
	{
		bary = vec3(0.0, 0.0, 1.0);
		return c;
	}

	float vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
	{
		float denomAC = d2 - d6;
		float w = (denomAC > 1e-20) ? (d2 / denomAC) : 0.0;
		bary = vec3(1.0 - w, 0.0, w);
		return a + w * ac;
	}

	float va = d3 * d6 - d5 * d4;
	if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
	{
		float e1 = d4 - d3;
		float e2 = d5 - d6;
		float denomBC = e1 + e2;
		float w = (denomBC > 1e-20) ? (e1 / denomBC) : 0.0;
		bary = vec3(0.0, 1.0 - w, w);
		return b + w * (c - b);
	}

	float denom = 1.0 / max(va + vb + vc, 1e-20);
	float v = vb * denom;
	float w = vc * denom;
	float u = 1.0 - v - w;
	bary = vec3(u, v, w);
	return a + ab * v + ac * w;
}

// Computes the signed solid angle of triangle ABC seen from point P.
float triangleSolidAngle(vec3 p, vec3 a, vec3 b, vec3 c)
{
	vec3 qa = a - p;
	vec3 qb = b - p;
	vec3 qc = c - p;
	float la = length(qa);
	float lb = length(qb);
	float lc = length(qc);
	if (la < 1e-6 || lb < 1e-6 || lc < 1e-6)
	{
		return 0.0;
	}

	float num = dot(qa, cross(qb, qc));
	float den = la * lb * lc + dot(qa, qb) * lc + dot(qb, qc) * la + dot(qc, qa) * lb;
	if (abs(num) < 1e-12 && abs(den) < 1e-12)
	{
		return 0.0;
	}
	return 2.0 * atan(num, den);
}
