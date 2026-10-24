#version 460 core

in vec2 v_texCoord;
out vec4 FragColor;

uniform sampler2D t_texture;

void main()
{
	FragColor = texture(t_texture, v_texCoord);
}
