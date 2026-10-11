#version 460 core

in vec2 v_texCoord;
out vec4 FragColor;

uniform sampler2D t_input;

void main()
{
	vec4 col = texture(t_input, v_texCoord);
	FragColor = vec4(pow(max(col.rgb, vec3(0.0)), vec3(0.4545)), col.a);
}
