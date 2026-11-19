#version 460 core
uniform vec3 u_color;
layout(location = 0) out vec4 color;
void main()
{
	color = vec4(u_color, 1);
}
