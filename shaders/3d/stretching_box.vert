#version 460 core
uniform mat4 u_camMatrix;
uniform float u_time;
uniform float u_thickness;
out vec3 v_worldPos, v_normal, v_color;
out vec2 v_texCoord;
void main()
{
	const vec2 corners[6] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1),
	                              vec2(-1,-1), vec2(1,1), vec2(-1,1));
	int face = gl_VertexID / 6;
	int axis = face / 2;
	float side = (face % 2 == 0) ? 1.0 : -1.0;
	vec2 corner = corners[gl_VertexID % 6];
	vec3 local = vec3(0);
	local[axis] = side;
	local[(axis + 1) % 3] = side * corner.x;
	local[(axis + 2) % 3] = corner.y;
	vec3 normal = vec3(0);
	normal[axis] = side;

	float stretch = sin(u_time * 1.5);
	vec3 halfSize = vec3(1.05 + 0.5 * stretch, 1.05 - 0.5 * stretch, 0.5 * u_thickness);
	// Turn the broad face toward the entrance camera.
	const float yaw = 0.87;
	mat3 rotation = mat3(vec3(cos(yaw), 0, -sin(yaw)), vec3(0, 1, 0), vec3(sin(yaw), 0, cos(yaw)));
	v_worldPos = vec3(0, 1.8, 0) + rotation * (local * halfSize);
	v_normal = rotation * normal;
	v_texCoord = corner * 0.5 + 0.5;
	v_color = vec3(1);
	gl_Position = u_camMatrix * vec4(v_worldPos, 1);
}
