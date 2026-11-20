#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include "renderer/ComputeShader.h"
#include "renderer/Framebuffer.h"
#include "renderer/Model.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Texture3D.h"

namespace renderer
{
	enum class SdfMethod
	{
		ExactBaseline = 0,
		JfaVoxelShell = 1,
		JfaGBufferSplat = 2,
		JfaCameraGBuffer = 3
	};

	enum class JfaVariant
	{
		Standard = 0,
		JfaPlus1 = 1,
		OnePlusJfa = 2
	};

	enum class SdfFormat
	{
		R32F = 0,
		R16F = 1
	};

	struct SdfStats
	{
		float totalGpuMs = 0.0f;
		float seedGpuMs = 0.0f;
		float floodGpuMs = 0.0f;
		float finalizeGpuMs = 0.0f;
		float staticVramMb = 0.0f;
		float tempVramMb = 0.0f;
		float mae = 0.0f;
		float rmse = 0.0f;
		float maxError = 0.0f;
		float signMatchPercent = 100.0f;
		int triangleCount = 0;
		bool hasReferenceComparison = false;
	};

	struct SdfGenerationParams
	{
		SdfMethod method = SdfMethod::ExactBaseline;
		JfaVariant jfaVariant = JfaVariant::Standard;
		SdfFormat format = SdfFormat::R32F;
		int resolution = 64;
		float worldSize = 2.0f;
		glm::vec3 worldOffset = glm::vec3(0.0f, 0.5f, 0.0f);
		glm::mat4 modelTransform = glm::mat4(1.0f);
		glm::mat4 cameraViewProj = glm::mat4(1.0f);
		int cameraViewportWidth = 512;
		int cameraViewportHeight = 512;
		bool accumulateSeeds = false;
		bool resetAccumulatedSeeds = false;
		bool unsignedDistance = false;
		float shellHalfThicknessVoxels = 0.0f;
	};

	struct CameraSurfaceCapture
	{
		const Texture& worldPositions;
		const Texture& normals;
		const Texture& depth;
		const Texture& materials;
		int materialId;
		glm::vec3 origin;
		glm::mat3 invRotation = glm::mat3(1.0f);
	};

	class SdfGenerator
	{
	public:
		SdfGenerator() = default;
		explicit SdfGenerator(const std::string& shadersRoot);
		~SdfGenerator();

		SdfGenerator(SdfGenerator&& other) noexcept;
		SdfGenerator& operator=(SdfGenerator&& other) noexcept;

		SdfGenerator(const SdfGenerator&) = delete;
		SdfGenerator& operator=(const SdfGenerator&) = delete;

		void init(const std::string& shadersRoot);
		void dispose();

		// Generates a 3D SDF texture from a triangle model and returns GPU metrics.
		SdfStats generate(const Model& model, const SdfGenerationParams& params, Texture3D& outputTexture);
		void generateFromCapture(const CameraSurfaceCapture& capture, SdfGenerationParams params, Texture3D& distance);

		// Compares a candidate 3D SDF volume against a reference volume voxel by voxel.
		void computeErrorMetrics(const Texture3D& candidateTex, const Texture3D& referenceTex, SdfStats& stats) const;

		// Clears persistent camera G-Buffer seeds so the next frame starts fresh.
		void clearPersistentSeeds();

	private:
		SdfStats generateImpl(const Model* model, const CameraSurfaceCapture* capture,
			const SdfGenerationParams& params, Texture3D& distance);
		ComputeShader m_exactBaselineShaderR32F;
		ComputeShader m_exactBaselineShaderR16F;
		ComputeShader m_jfaClearShader;
		ComputeShader m_jfaSeedVoxelizeShader;
		ComputeShader m_jfaSeedGBufferShader;
		ComputeShader m_jfaSeedResolveShader;
		ComputeShader m_jfaStepShader;
		ComputeShader m_jfaWindingVoxelizeShader;
		ComputeShader m_jfaWindingPrefixShader;
		ComputeShader m_jfaFinalizeShaderR32F;
		ComputeShader m_jfaFinalizeShaderR16F;

		Shader m_gbufferShader;

		GLuint m_triangleSsbo = 0;
		size_t m_triangleSsboCapacityBytes = 0;
		size_t m_cachedSsboTriangleCount = 0;
		glm::vec3 m_cachedSsboAabbMin = glm::vec3(0.0f);
		glm::vec3 m_cachedSsboAabbMax = glm::vec3(0.0f);
		glm::mat4 m_cachedSsboTransform = glm::mat4(0.0f);
		GLuint m_timeQuery = 0;

		Texture3D m_atomicSeedTex;
		Texture3D m_windingDeltaTex;
		Texture3D m_seedPosTex[2];
		Texture3D m_seedNormTex[2];
		Texture3D m_persistentSeedPosTex;
		Texture3D m_persistentSeedNormTex;
		int m_allocatedJfaRes = 0;
		bool m_persistentSeedsValid = false;
		int m_persistentRes = 0;
		float m_persistentWorldSize = 0.0f;
		glm::vec3 m_persistentWorldOffset = glm::vec3(0.0f);
		glm::mat4 m_persistentModelTransform = glm::mat4(0.0f);

		// 3x2 orthographic atlas FBO for 6-view G-Buffer capture.
		Texture m_orthoAlbedo;
		Texture m_orthoWorldPos;
		Texture m_orthoNormal;
		Texture m_orthoMaterial;
		Texture m_orthoDepth;
		Framebuffer m_orthoFbo;
		int m_orthoTileSize = 0;

		void ensureTriangleSsbo(const std::vector<GpuTriangle>& triangles);
		void ensureJfaVolumes(int resolution);
		void ensureOrthoAtlas(int tileSize);
		void renderSixViewGBuffer(const Model& model, const SdfGenerationParams& params);
		void renderCameraGBuffer(const Model& model, const SdfGenerationParams& params, int camW, int camH);
	};
} // namespace renderer
