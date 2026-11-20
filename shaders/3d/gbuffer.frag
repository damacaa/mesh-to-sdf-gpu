#version 460 core

layout(location = 0) out vec4 out_albedo;   // RGB = diffuse color, A = roughness, or -1 for scene materials
layout(location = 1) out vec4 out_worldPos; // RGB = world position, A = encoded mesh normal or zero for empty pixels
layout(location = 2) out vec4 out_normal;   // RGB = world normal (signed, not remapped), A = metallic for glTF shading, or the sheet flag for SDF capture
layout(location = 3) out int out_material;  // Material ID

in vec3 v_worldPos;
in vec3 v_normal;
in vec3 v_color;
in vec2 v_texCoord;

layout(binding=0) uniform sampler2D t_baseColor;
layout(binding=1) uniform sampler2D t_normal;
layout(binding=2) uniform sampler2D t_surface;
uniform int u_materialIndex, u_useSurface, u_colorMap, u_normalMap, u_surfaceMap;
uniform vec4 u_colorFactor, u_surfaceFactors;
#include "../common/capture_normal.glsl"

void main()
{
	vec3 N = length(v_normal) > 0.001 ? normalize(v_normal) : vec3(0,1,0);
	// Save the mesh normal before material textures change the shading normal.
	out_worldPos = vec4(v_worldPos, packCaptureNormal(N));
	out_albedo = vec4(1,1,1,-1);
	out_normal = vec4(N, v_color.b);
	if (u_useSurface != 0)
	{
		vec4 base = u_colorFactor;
		if (u_colorMap != 0) base *= texture(t_baseColor, v_texCoord);
		if (base.a < u_surfaceFactors.w) discard;
		vec2 rm = u_surfaceFactors.xy;
		if (u_surfaceMap != 0) rm *= texture(t_surface, v_texCoord).gb;
		if (u_normalMap != 0)
		{
			vec2 du = dFdx(v_texCoord), dv = dFdy(v_texCoord);
			float det = du.x * dv.y - du.y * dv.x;
			vec3 tangent = (dFdx(v_worldPos) * dv.y - dFdy(v_worldPos) * du.y) * sign(det);
			tangent -= dot(N,tangent) * N;
			if (length(tangent) > 1e-10)
			{
				vec3 T = normalize(tangent);
				vec3 B = cross(N,T) * sign(det) * (gl_FrontFacing ? 1.0 : -1.0);
				vec3 n = texture(t_normal, v_texCoord).xyz * 2.0 - 1.0;
				N = normalize(T * n.x * u_surfaceFactors.z + B * n.y * u_surfaceFactors.z + N * n.z);
			}
		}
		out_albedo = vec4(base.rgb, rm.x);
		out_normal = vec4(N, rm.y);
	}
	out_material = u_materialIndex;
}
