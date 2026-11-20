#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "renderer/Shader.h"
#include "renderer/Texture.h"

namespace renderer
{
	struct Vertex
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec3 color;
		glm::vec2 texUV;
	};

	// Packed triangle layout that matches std430 in GLSL compute shaders.
	struct GpuTriangle
	{
		glm::vec4 v0; // xyz = vertex 0 position, w = 0
		glm::vec4 v1; // xyz = vertex 1 position, w = 0
		glm::vec4 v2; // xyz = vertex 2 position, w = 0
		glm::vec4 n0; // xyz = angle-weighted vertex 0 normal, w = 0
		glm::vec4 n1; // xyz = angle-weighted vertex 1 normal, w = 0
		glm::vec4 n2; // xyz = angle-weighted vertex 2 normal, w = 0
	};
	static_assert(sizeof(GpuTriangle) == 96, "GpuTriangle must match std430 layout.");

	struct PrimitiveRange
	{
		size_t firstIndex;
		GLsizei count;
		int material;
	};

	class Model
	{
	public:
		Model() = default;
		explicit Model(const std::string& gltfPath);
		~Model();

		Model(Model&& other) noexcept;
		Model& operator=(Model&& other) noexcept;

		Model(const Model&) = delete;
		Model& operator=(const Model&) = delete;

		bool loadGLTF(const std::string& gltfPath);
		void draw(const Shader& shader, const glm::mat4& modelMatrix, int materialIndex = 0) const;
		void drawWithMaterials(const Shader& shader, const glm::mat4& transform, int materialIndex);
		void free();

		bool isValid() const { return !m_triangles.empty(); }
		size_t getTriangleCount() const { return m_triangles.size(); }
		glm::vec3 getAabbMin() const { return m_aabbMin; }
		glm::vec3 getAabbMax() const { return m_aabbMax; }

		// Computes a transform matrix that centers and scales the mesh inside the SDF cube.
		glm::mat4 computeFitTransform(float worldSize, const glm::vec3& worldOffset, float paddingFraction = 0.15f) const;

		// Transforms model triangles and angle-weighted normals into world space for GPU upload.
		std::vector<GpuTriangle> buildGpuTriangles(const glm::mat4& transform) const;

	private:
		GLuint m_vao = 0;
		GLuint m_vbo = 0;
		GLuint m_ebo = 0;
		GLsizei m_indexCount = 0;

		glm::vec3 m_aabbMin = glm::vec3(0.0f);
		glm::vec3 m_aabbMax = glm::vec3(0.0f);
		std::vector<GpuTriangle> m_triangles;
		struct SurfaceMaterial
		{
			glm::vec4 color{1.0f};
			float roughness = 1.0f;
			float metallic = 1.0f;
			float normalScale = 1.0f;
			float alphaCutoff = -1.0f;
			int colorMap = -1;
			int normalMap = -1;
			int surfaceMap = -1;
		};
		struct ImageMap { std::string path; bool srgb; };
		std::vector<PrimitiveRange> m_ranges;
		std::vector<SurfaceMaterial> m_materials;
		std::vector<ImageMap> m_maps;
		std::vector<Texture> m_textures;
	};
} // namespace renderer
