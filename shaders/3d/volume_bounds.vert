#version 460 core
uniform mat4 u_viewProjection;
uniform vec3 u_minimum;
uniform float u_size;
void main()
{
	int corner = gl_VertexID / 6;
	int axis = (gl_VertexID / 2) % 3;
	vec3 offset = vec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
	if ((gl_VertexID & 1) != 0)
		offset[axis] = mix(offset[axis], 0.5, 0.2);
	gl_Position = u_viewProjection * vec4(u_minimum + offset * u_size, 1);
}
