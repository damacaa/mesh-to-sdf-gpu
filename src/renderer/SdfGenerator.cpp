#include "renderer/SdfGenerator.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace renderer
{
	namespace
	{
		constexpr int kOrthoAtlasTileSize = 256;

		uint32_t divUp(int total, int groupSize)
		{
			return static_cast<uint32_t>((total + groupSize - 1) / groupSize);
		}

		std::vector<int> buildJfaSteps(int resolution, JfaVariant variant)
		{
			std::vector<int> steps;
			if (variant == JfaVariant::OnePlusJfa)
			{
				steps.push_back(1);
			}

			int startStep = 1;
			while (startStep * 2 < resolution)
			{
				startStep *= 2;
			}

			for (int step = startStep; step >= 1; step /= 2)
			{
				steps.push_back(step);
			}

			if (variant == JfaVariant::JfaPlus1)
			{
				steps.push_back(1);
			}

			return steps;
		}

		template <typename Func>
		float measureGpuPass(GLuint queryId, Func&& fn)
		{
			if (queryId == 0)
			{
				fn();
				return 0.0f;
			}

			glBeginQuery(GL_TIME_ELAPSED, queryId);
			fn();
			glEndQuery(GL_TIME_ELAPSED);

			GLuint64 elapsedNs = 0;
			glGetQueryObjectui64v(queryId, GL_QUERY_RESULT, &elapsedNs);
			return static_cast<float>(elapsedNs) / 1000000.0f;
		}

		// Saves GL FBO and depth/cull state while rendering G-Buffer geometry.
		struct ScopedGBufferCapture
		{
			GLint prevFbo = 0;
			GLint prevViewport[4] = { 0, 0, 0, 0 };
			GLboolean wasCullFace = GL_FALSE;
			GLboolean wasDepthTest = GL_FALSE;
			GLint prevDepthFunc = GL_LESS;

			explicit ScopedGBufferCapture(const Framebuffer& fbo)
			{
				glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
				glGetIntegerv(GL_VIEWPORT, prevViewport);
				wasCullFace = glIsEnabled(GL_CULL_FACE);
				wasDepthTest = glIsEnabled(GL_DEPTH_TEST);
				glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);

				fbo.bind();
				const GLenum drawBuffers[4] = {
					GL_COLOR_ATTACHMENT0,
					GL_COLOR_ATTACHMENT1,
					GL_COLOR_ATTACHMENT2,
					GL_COLOR_ATTACHMENT3
				};
				glDrawBuffers(4, drawBuffers);

				glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
				glClearDepth(1.0);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

				// Keep both sides of the mesh visible without flipping rasterized normals.
				glEnable(GL_DEPTH_TEST);
				glDepthFunc(GL_LESS);
				glDepthMask(GL_TRUE);
				glDisable(GL_CULL_FACE);
			}

			~ScopedGBufferCapture()
			{
				if (wasCullFace)
				{
					glEnable(GL_CULL_FACE);
				}
				else
				{
					glDisable(GL_CULL_FACE);
				}
				if (!wasDepthTest)
				{
					glDisable(GL_DEPTH_TEST);
				}
				glDepthFunc(static_cast<GLenum>(prevDepthFunc));
				glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
				glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
			}
		};
	} // namespace

	SdfGenerator::SdfGenerator(const std::string& shadersRoot)
	{
		init(shadersRoot);
	}

	SdfGenerator::~SdfGenerator()
	{
		dispose();
	}

	SdfGenerator::SdfGenerator(SdfGenerator&& other) noexcept
		: m_exactBaselineShaderR32F(std::move(other.m_exactBaselineShaderR32F))
		, m_exactBaselineShaderR16F(std::move(other.m_exactBaselineShaderR16F))
		, m_jfaClearShader(std::move(other.m_jfaClearShader))
		, m_jfaSeedVoxelizeShader(std::move(other.m_jfaSeedVoxelizeShader))
		, m_jfaSeedGBufferShader(std::move(other.m_jfaSeedGBufferShader))
		, m_jfaSeedResolveShader(std::move(other.m_jfaSeedResolveShader))
		, m_jfaStepShader(std::move(other.m_jfaStepShader))
		, m_jfaFinalizeShaderR32F(std::move(other.m_jfaFinalizeShaderR32F))
		, m_jfaFinalizeShaderR16F(std::move(other.m_jfaFinalizeShaderR16F))
		, m_gbufferShader(std::move(other.m_gbufferShader))
		, m_triangleSsbo(other.m_triangleSsbo)
		, m_triangleSsboCapacityBytes(other.m_triangleSsboCapacityBytes)
		, m_cachedSsboTriangleCount(other.m_cachedSsboTriangleCount)
		, m_cachedSsboAabbMin(other.m_cachedSsboAabbMin)
		, m_cachedSsboAabbMax(other.m_cachedSsboAabbMax)
		, m_cachedSsboTransform(other.m_cachedSsboTransform)
		, m_timeQuery(other.m_timeQuery)
		, m_atomicSeedTex(std::move(other.m_atomicSeedTex))
		, m_seedPosTex{ std::move(other.m_seedPosTex[0]), std::move(other.m_seedPosTex[1]) }
		, m_seedNormTex{ std::move(other.m_seedNormTex[0]), std::move(other.m_seedNormTex[1]) }
		, m_persistentSeedPosTex(std::move(other.m_persistentSeedPosTex))
		, m_persistentSeedNormTex(std::move(other.m_persistentSeedNormTex))
		, m_allocatedJfaRes(other.m_allocatedJfaRes)
		, m_persistentSeedsValid(other.m_persistentSeedsValid)
		, m_persistentRes(other.m_persistentRes)
		, m_persistentWorldSize(other.m_persistentWorldSize)
		, m_persistentWorldOffset(other.m_persistentWorldOffset)
		, m_persistentModelTransform(other.m_persistentModelTransform)
		, m_orthoAlbedo(std::move(other.m_orthoAlbedo))
		, m_orthoWorldPos(std::move(other.m_orthoWorldPos))
		, m_orthoNormal(std::move(other.m_orthoNormal))
		, m_orthoMaterial(std::move(other.m_orthoMaterial))
		, m_orthoDepth(std::move(other.m_orthoDepth))
		, m_orthoFbo(std::move(other.m_orthoFbo))
		, m_orthoTileSize(other.m_orthoTileSize)
	{
		other.m_triangleSsbo = 0;
		other.m_triangleSsboCapacityBytes = 0;
		other.m_cachedSsboTriangleCount = 0;
		other.m_cachedSsboAabbMin = glm::vec3(0.0f);
		other.m_cachedSsboAabbMax = glm::vec3(0.0f);
		other.m_cachedSsboTransform = glm::mat4(0.0f);
		other.m_timeQuery = 0;
		other.m_allocatedJfaRes = 0;
		other.m_persistentSeedsValid = false;
		other.m_persistentRes = 0;
		other.m_persistentWorldSize = 0.0f;
		other.m_persistentWorldOffset = glm::vec3(0.0f);
		other.m_persistentModelTransform = glm::mat4(0.0f);
		other.m_orthoTileSize = 0;
	}

	SdfGenerator& SdfGenerator::operator=(SdfGenerator&& other) noexcept
	{
		if (this != &other)
		{
			dispose();
			m_exactBaselineShaderR32F = std::move(other.m_exactBaselineShaderR32F);
			m_exactBaselineShaderR16F = std::move(other.m_exactBaselineShaderR16F);
			m_jfaClearShader = std::move(other.m_jfaClearShader);
			m_jfaSeedVoxelizeShader = std::move(other.m_jfaSeedVoxelizeShader);
			m_jfaSeedGBufferShader = std::move(other.m_jfaSeedGBufferShader);
			m_jfaSeedResolveShader = std::move(other.m_jfaSeedResolveShader);
			m_jfaStepShader = std::move(other.m_jfaStepShader);
			m_jfaFinalizeShaderR32F = std::move(other.m_jfaFinalizeShaderR32F);
			m_jfaFinalizeShaderR16F = std::move(other.m_jfaFinalizeShaderR16F);
			m_gbufferShader = std::move(other.m_gbufferShader);
			m_triangleSsbo = other.m_triangleSsbo;
			m_triangleSsboCapacityBytes = other.m_triangleSsboCapacityBytes;
			m_cachedSsboTriangleCount = other.m_cachedSsboTriangleCount;
			m_cachedSsboAabbMin = other.m_cachedSsboAabbMin;
			m_cachedSsboAabbMax = other.m_cachedSsboAabbMax;
			m_cachedSsboTransform = other.m_cachedSsboTransform;
			m_timeQuery = other.m_timeQuery;
			m_atomicSeedTex = std::move(other.m_atomicSeedTex);
			m_seedPosTex[0] = std::move(other.m_seedPosTex[0]);
			m_seedPosTex[1] = std::move(other.m_seedPosTex[1]);
			m_seedNormTex[0] = std::move(other.m_seedNormTex[0]);
			m_seedNormTex[1] = std::move(other.m_seedNormTex[1]);
			m_persistentSeedPosTex = std::move(other.m_persistentSeedPosTex);
			m_persistentSeedNormTex = std::move(other.m_persistentSeedNormTex);
			m_allocatedJfaRes = other.m_allocatedJfaRes;
			m_persistentSeedsValid = other.m_persistentSeedsValid;
			m_persistentRes = other.m_persistentRes;
			m_persistentWorldSize = other.m_persistentWorldSize;
			m_persistentWorldOffset = other.m_persistentWorldOffset;
			m_persistentModelTransform = other.m_persistentModelTransform;
			m_orthoAlbedo = std::move(other.m_orthoAlbedo);
			m_orthoWorldPos = std::move(other.m_orthoWorldPos);
			m_orthoNormal = std::move(other.m_orthoNormal);
			m_orthoMaterial = std::move(other.m_orthoMaterial);
			m_orthoDepth = std::move(other.m_orthoDepth);
			m_orthoFbo = std::move(other.m_orthoFbo);
			m_orthoTileSize = other.m_orthoTileSize;

			other.m_triangleSsbo = 0;
			other.m_triangleSsboCapacityBytes = 0;
			other.m_cachedSsboTriangleCount = 0;
			other.m_cachedSsboAabbMin = glm::vec3(0.0f);
			other.m_cachedSsboAabbMax = glm::vec3(0.0f);
			other.m_cachedSsboTransform = glm::mat4(0.0f);
			other.m_timeQuery = 0;
			other.m_allocatedJfaRes = 0;
			other.m_persistentSeedsValid = false;
			other.m_persistentRes = 0;
			other.m_persistentWorldSize = 0.0f;
			other.m_persistentWorldOffset = glm::vec3(0.0f);
			other.m_persistentModelTransform = glm::mat4(0.0f);
			other.m_orthoTileSize = 0;
		}
		return *this;
	}

	void SdfGenerator::init(const std::string& shadersRoot)
	{
		dispose();

		m_exactBaselineShaderR32F = ComputeShader(shadersRoot + "/compute/sdf_exact_baseline.comp", { "SDF_IMAGE_FORMAT r32f" });
		m_exactBaselineShaderR16F = ComputeShader(shadersRoot + "/compute/sdf_exact_baseline.comp", { "SDF_IMAGE_FORMAT r16f" });
		m_jfaClearShader = ComputeShader(shadersRoot + "/compute/jfa_clear.comp");
		m_jfaSeedVoxelizeShader = ComputeShader(shadersRoot + "/compute/jfa_seed_voxelize.comp");
		m_jfaSeedGBufferShader = ComputeShader(shadersRoot + "/compute/jfa_seed_gbuffer.comp");
		m_jfaSeedResolveShader = ComputeShader(shadersRoot + "/compute/jfa_seed_resolve.comp");
		m_jfaStepShader = ComputeShader(shadersRoot + "/compute/jfa_step.comp");
		m_jfaFinalizeShaderR32F = ComputeShader(shadersRoot + "/compute/jfa_finalize.comp", { "SDF_IMAGE_FORMAT r32f" });
		m_jfaFinalizeShaderR16F = ComputeShader(shadersRoot + "/compute/jfa_finalize.comp", { "SDF_IMAGE_FORMAT r16f" });

		m_gbufferShader = Shader(shadersRoot + "/3d/geometry.vert", shadersRoot + "/3d/gbuffer.frag");

		glGenQueries(1, &m_timeQuery);
	}

	void SdfGenerator::dispose()
	{
		m_exactBaselineShaderR32F.free();
		m_exactBaselineShaderR16F.free();
		m_jfaClearShader.free();
		m_jfaSeedVoxelizeShader.free();
		m_jfaSeedGBufferShader.free();
		m_jfaSeedResolveShader.free();
		m_jfaStepShader.free();
		m_jfaFinalizeShaderR32F.free();
		m_jfaFinalizeShaderR16F.free();
		m_gbufferShader.free();

		if (m_triangleSsbo != 0)
		{
			glDeleteBuffers(1, &m_triangleSsbo);
			m_triangleSsbo = 0;
		}
		m_triangleSsboCapacityBytes = 0;
		m_cachedSsboTriangleCount = 0;
		m_cachedSsboAabbMin = glm::vec3(0.0f);
		m_cachedSsboAabbMax = glm::vec3(0.0f);
		m_cachedSsboTransform = glm::mat4(0.0f);

		if (m_timeQuery != 0)
		{
			glDeleteQueries(1, &m_timeQuery);
			m_timeQuery = 0;
		}

		m_atomicSeedTex.dispose();
		m_seedPosTex[0].dispose();
		m_seedPosTex[1].dispose();
		m_seedNormTex[0].dispose();
		m_seedNormTex[1].dispose();
		m_persistentSeedPosTex.dispose();
		m_persistentSeedNormTex.dispose();
		m_allocatedJfaRes = 0;
		m_persistentSeedsValid = false;
		m_persistentRes = 0;
		m_persistentWorldSize = 0.0f;
		m_persistentWorldOffset = glm::vec3(0.0f);
		m_persistentModelTransform = glm::mat4(0.0f);

		m_orthoFbo.free();
		m_orthoAlbedo.dispose();
		m_orthoWorldPos.dispose();
		m_orthoNormal.dispose();
		m_orthoMaterial.dispose();
		m_orthoDepth.dispose();
		m_orthoTileSize = 0;
	}

	void SdfGenerator::ensureTriangleSsbo(const std::vector<GpuTriangle>& triangles)
	{
		if (m_triangleSsbo == 0)
		{
			glGenBuffers(1, &m_triangleSsbo);
		}

		const size_t neededBytes = std::max<size_t>(1, triangles.size()) * sizeof(GpuTriangle);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_triangleSsbo);
		if (neededBytes > m_triangleSsboCapacityBytes)
		{
			glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(neededBytes), nullptr, GL_DYNAMIC_DRAW);
			m_triangleSsboCapacityBytes = neededBytes;
		}

		if (!triangles.empty())
		{
			glBufferSubData(
				GL_SHADER_STORAGE_BUFFER,
				0,
				static_cast<GLsizeiptr>(triangles.size() * sizeof(GpuTriangle)),
				triangles.data());
		}
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}

	void SdfGenerator::ensureJfaVolumes(int resolution)
	{
		if (m_allocatedJfaRes == resolution && m_atomicSeedTex.id != 0)
		{
			return;
		}

		m_atomicSeedTex.allocateWithData(
			resolution, resolution, resolution,
			GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr,
			GL_NEAREST, GL_CLAMP_TO_EDGE);

		for (int i = 0; i < 2; ++i)
		{
			m_seedPosTex[i].allocateWithData(
				resolution, resolution, resolution,
				GL_RGBA32F, GL_RGBA, GL_FLOAT, nullptr,
				GL_NEAREST, GL_CLAMP_TO_EDGE);
			m_seedNormTex[i].allocateWithData(
				resolution, resolution, resolution,
				GL_RGBA32F, GL_RGBA, GL_FLOAT, nullptr,
				GL_NEAREST, GL_CLAMP_TO_EDGE);
		}

		m_persistentSeedPosTex.allocateWithData(
			resolution, resolution, resolution,
			GL_RGBA32F, GL_RGBA, GL_FLOAT, nullptr,
			GL_NEAREST, GL_CLAMP_TO_EDGE);
		m_persistentSeedNormTex.allocateWithData(
			resolution, resolution, resolution,
			GL_RGBA32F, GL_RGBA, GL_FLOAT, nullptr,
			GL_NEAREST, GL_CLAMP_TO_EDGE);

		m_allocatedJfaRes = resolution;
		m_persistentSeedsValid = false;
	}

	void SdfGenerator::clearPersistentSeeds()
	{
		m_persistentSeedsValid = false;
	}

	void SdfGenerator::ensureOrthoAtlas(int tileSize)
	{
		if (m_orthoTileSize == tileSize && m_orthoFbo.id != 0)
		{
			return;
		}

		const int atlasWidth = tileSize * 3;
		const int atlasHeight = tileSize * 2;

		m_orthoAlbedo = Texture(atlasWidth, atlasHeight, Texture::TextureType::LinearData);
		m_orthoWorldPos = Texture(atlasWidth, atlasHeight, Texture::TextureType::AccumulationData);
		m_orthoNormal = Texture(atlasWidth, atlasHeight, Texture::TextureType::AccumulationData);
		m_orthoMaterial = Texture(atlasWidth, atlasHeight, Texture::TextureType::IntData);
		m_orthoDepth = Texture(atlasWidth, atlasHeight, Texture::TextureType::Depth);

		m_orthoFbo = Framebuffer();
		m_orthoFbo.attachColorTexture(m_orthoAlbedo, 0);
		m_orthoFbo.attachColorTexture(m_orthoWorldPos, 1);
		m_orthoFbo.attachColorTexture(m_orthoNormal, 2);
		m_orthoFbo.attachColorTexture(m_orthoMaterial, 3);
		m_orthoFbo.attachDepthTexture(m_orthoDepth);

		m_orthoTileSize = tileSize;
	}

	void SdfGenerator::renderSixViewGBuffer(const Model& model, const SdfGenerationParams& params)
	{
		ScopedGBufferCapture capture(m_orthoFbo);

		const float halfSize = params.worldSize * 0.5f;
		const glm::mat4 orthoProj = glm::ortho(
			-halfSize, halfSize,
			-halfSize, halfSize,
			-0.05f * params.worldSize, params.worldSize * 1.05f);

		const glm::vec3 viewDirs[6] = {
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(-1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, -1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f),
			glm::vec3(0.0f, 0.0f, -1.0f)
		};
		const glm::vec3 viewUps[6] = {
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, -1.0f),
			glm::vec3(0.0f, 0.0f, 1.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f)
		};

		m_gbufferShader.use();
		for (int viewIdx = 0; viewIdx < 6; ++viewIdx)
		{
			const int tileX = viewIdx % 3;
			const int tileY = viewIdx / 3;
			glViewport(tileX * m_orthoTileSize, tileY * m_orthoTileSize, m_orthoTileSize, m_orthoTileSize);

			const glm::vec3 eye = params.worldOffset + viewDirs[viewIdx] * halfSize;
			const glm::mat4 view = glm::lookAt(eye, params.worldOffset, viewUps[viewIdx]);
			m_gbufferShader.setUniform("u_camMatrix", orthoProj * view);

			model.draw(m_gbufferShader, params.modelTransform, 0);
		}
	}

	void SdfGenerator::renderCameraGBuffer(const Model& model, const SdfGenerationParams& params, int camW, int camH)
	{
		ScopedGBufferCapture capture(m_orthoFbo);
		glViewport(0, 0, camW, camH);
		m_gbufferShader.use();
		m_gbufferShader.setUniform("u_camMatrix", params.cameraViewProj);
		model.draw(m_gbufferShader, params.modelTransform, 0);
	}

	SdfStats SdfGenerator::generate(const Model& model, const SdfGenerationParams& params, Texture3D& outputTexture)
	{
		SdfStats stats{};
		const int res = std::max(8, params.resolution);
		const GLenum glFormat = (params.format == SdfFormat::R16F) ? GL_R16F : GL_R32F;

		if (outputTexture.id == 0
			|| outputTexture.getWidth() != res
			|| outputTexture.getHeight() != res
			|| outputTexture.getDepth() != res
			|| outputTexture.getInternalFormat() != glFormat)
		{
			outputTexture.allocate(res, res, res, glFormat);
		}

		const size_t voxelCount = static_cast<size_t>(res) * static_cast<size_t>(res) * static_cast<size_t>(res);
		const size_t outBytesPerVoxel = (params.format == SdfFormat::R16F) ? 2u : 4u;
		stats.staticVramMb = static_cast<float>(voxelCount * outBytesPerVoxel) / (1024.0f * 1024.0f);

		const bool needsTriangleSsbo = (params.method == SdfMethod::ExactBaseline || params.method == SdfMethod::JfaVoxelShell);
		const bool isGBufferMethod = (params.method == SdfMethod::JfaGBufferSplat || params.method == SdfMethod::JfaCameraGBuffer);
		const size_t currCount = model.isValid() ? model.getTriangleCount() : 0u;
		const uint32_t triCount = static_cast<uint32_t>(currCount);
		stats.triangleCount = static_cast<int>(triCount);

		const glm::ivec3 resVec(res, res, res);
		const GLuint groups4 = divUp(res, 4);

		// Keep a valid SSBO bound to slot 0 for compute shaders that declare TriangleBuffer.
		if (needsTriangleSsbo)
		{
			const glm::vec3 currMin = model.getAabbMin();
			const glm::vec3 currMax = model.getAabbMax();
			const bool cacheValid = (m_triangleSsbo != 0)
				&& (m_cachedSsboTriangleCount == currCount)
				&& (m_cachedSsboAabbMin == currMin)
				&& (m_cachedSsboAabbMax == currMax)
				&& (m_cachedSsboTransform == params.modelTransform);
			if (!cacheValid)
			{
				std::vector<GpuTriangle> triangles;
				if (model.isValid())
				{
					triangles = model.buildGpuTriangles(params.modelTransform);
				}
				ensureTriangleSsbo(triangles);
				m_cachedSsboTriangleCount = currCount;
				m_cachedSsboAabbMin = currMin;
				m_cachedSsboAabbMax = currMax;
				m_cachedSsboTransform = params.modelTransform;
			}
		}
		else if (m_triangleSsbo == 0)
		{
			const std::vector<GpuTriangle> emptyTriangles;
			ensureTriangleSsbo(emptyTriangles);
			m_cachedSsboTriangleCount = 0;
			m_cachedSsboAabbMin = glm::vec3(0.0f);
			m_cachedSsboAabbMax = glm::vec3(0.0f);
			m_cachedSsboTransform = glm::mat4(0.0f);
		}

		const size_t ssboBytes = isGBufferMethod
			? 0u
			: static_cast<size_t>(triCount) * sizeof(GpuTriangle);

		if (params.method == SdfMethod::ExactBaseline)
		{
			stats.tempVramMb = static_cast<float>(ssboBytes) / (1024.0f * 1024.0f);

			ComputeShader& shader = (params.format == SdfFormat::R16F)
				? m_exactBaselineShaderR16F
				: m_exactBaselineShaderR32F;

			stats.seedGpuMs = measureGpuPass(m_timeQuery, [&]() {
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_triangleSsbo);
				outputTexture.bindImage(0, GL_WRITE_ONLY);
				shader.use();
				shader.setUniform("u_resolution", resVec);
				shader.setUniform("u_worldSize", params.worldSize);
				shader.setUniform("u_worldOffset", params.worldOffset);
				shader.setUniform("u_triangleCount", triCount);
				shader.setUniform("u_unsignedDistance", params.unsignedDistance ? 1 : 0);
				shader.dispatch(groups4, groups4, groups4);
				shader.memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
			});

			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
			stats.totalGpuMs = stats.seedGpuMs;
			return stats;
		}

		ensureJfaVolumes(res);
		size_t tempBytes = ssboBytes + voxelCount * (4u + 6u * 16u);
		if (isGBufferMethod)
		{
			ensureOrthoAtlas(kOrthoAtlasTileSize);
			const size_t atlasPixels = static_cast<size_t>(m_orthoTileSize * 3) * static_cast<size_t>(m_orthoTileSize * 2);
			tempBytes += atlasPixels * (8u + 16u + 16u + 4u + 4u);
		}
		stats.tempVramMb = static_cast<float>(tempBytes) / (1024.0f * 1024.0f);

		const int atlasWidth = m_orthoTileSize * 3;
		const int atlasHeight = m_orthoTileSize * 2;
		int gbufW = atlasWidth;
		int gbufH = atlasHeight;
		if (params.method == SdfMethod::JfaCameraGBuffer)
		{
			const int srcW = std::max(1, params.cameraViewportWidth);
			const int srcH = std::max(1, params.cameraViewportHeight);
			const float scaleX = static_cast<float>(atlasWidth) / static_cast<float>(srcW);
			const float scaleY = static_cast<float>(atlasHeight) / static_cast<float>(srcH);
			const float scale = std::min(scaleX, scaleY);
			gbufW = std::clamp(static_cast<int>(std::round(static_cast<float>(srcW) * scale)), 1, atlasWidth);
			gbufH = std::clamp(static_cast<int>(std::round(static_cast<float>(srcH) * scale)), 1, atlasHeight);
		}

		const bool useAccumulation = (params.method == SdfMethod::JfaCameraGBuffer && params.accumulateSeeds);
		const bool persistentMismatch = !m_persistentSeedsValid
			|| m_persistentRes != res
			|| m_persistentWorldSize != params.worldSize
			|| m_persistentWorldOffset != params.worldOffset
			|| m_persistentModelTransform != params.modelTransform;
		const bool clearPersistent = useAccumulation && (params.resetAccumulatedSeeds || persistentMismatch);
		if (!useAccumulation)
		{
			m_persistentSeedsValid = false;
		}

		// 1. Seed initialization pass
		stats.seedGpuMs = measureGpuPass(m_timeQuery, [&]() {
			if (params.method == SdfMethod::JfaGBufferSplat && model.isValid() && triCount > 0u)
			{
				renderSixViewGBuffer(model, params);
			}
			else if (params.method == SdfMethod::JfaCameraGBuffer && model.isValid() && triCount > 0u)
			{
				renderCameraGBuffer(model, params, gbufW, gbufH);
			}

			m_seedPosTex[0].bindImage(0, GL_WRITE_ONLY);
			m_seedNormTex[0].bindImage(1, GL_WRITE_ONLY);
			m_atomicSeedTex.bindImage(2, GL_WRITE_ONLY);
			m_persistentSeedPosTex.bindImage(3, GL_WRITE_ONLY);
			m_persistentSeedNormTex.bindImage(4, GL_WRITE_ONLY);
			m_jfaClearShader.use();
			m_jfaClearShader.setUniform("u_resolution", resVec);
			m_jfaClearShader.setUniform("u_clearPersistent", clearPersistent ? 1 : 0);
			m_jfaClearShader.dispatch(groups4, groups4, groups4);
			m_jfaClearShader.memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

			if (params.method == SdfMethod::JfaVoxelShell)
			{
				if (triCount > 0u)
				{
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_triangleSsbo);
					m_atomicSeedTex.bindImage(2, GL_READ_WRITE);
					m_jfaSeedVoxelizeShader.use();
					m_jfaSeedVoxelizeShader.setUniform("u_resolution", resVec);
					m_jfaSeedVoxelizeShader.setUniform("u_worldSize", params.worldSize);
					m_jfaSeedVoxelizeShader.setUniform("u_worldOffset", params.worldOffset);
					m_jfaSeedVoxelizeShader.setUniform("u_triangleCount", triCount);
					m_jfaSeedVoxelizeShader.dispatch(divUp(static_cast<int>(triCount), 64), 1, 1);
					m_jfaSeedVoxelizeShader.memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
				}

				m_seedPosTex[0].bindImage(0, GL_WRITE_ONLY);
				m_seedNormTex[0].bindImage(1, GL_WRITE_ONLY);
				m_atomicSeedTex.bindImage(2, GL_READ_ONLY);
				m_persistentSeedPosTex.bindImage(3, GL_READ_WRITE);
				m_persistentSeedNormTex.bindImage(4, GL_READ_WRITE);
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_triangleSsbo);
				m_jfaSeedResolveShader.use();
				m_jfaSeedResolveShader.setUniform("u_resolution", resVec);
				m_jfaSeedResolveShader.setUniform("u_worldSize", params.worldSize);
				m_jfaSeedResolveShader.setUniform("u_worldOffset", params.worldOffset);
				m_jfaSeedResolveShader.setUniform("u_triangleCount", triCount);
				m_jfaSeedResolveShader.setUniform("u_resolveMode", 0);
				m_jfaSeedResolveShader.setUniform("u_accumulateSeeds", 0);
				m_jfaSeedResolveShader.dispatch(groups4, groups4, groups4);
				m_jfaSeedResolveShader.memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
			}
			else
			{
				if (model.isValid() && triCount > 0u)
				{
					m_atomicSeedTex.bindImage(2, GL_READ_WRITE);
					m_orthoWorldPos.bind(0);
					m_orthoDepth.bind(1);
					m_jfaSeedGBufferShader.use();
					m_jfaSeedGBufferShader.setUniform("t_gbufferWorldPos", 0);
					m_jfaSeedGBufferShader.setUniform("t_gbufferDepth", 1);
					m_jfaSeedGBufferShader.setUniform("u_resolution", resVec);
					m_jfaSeedGBufferShader.setUniform("u_worldSize", params.worldSize);
					m_jfaSeedGBufferShader.setUniform("u_worldOffset", params.worldOffset);
					glUniform2i(m_jfaSeedGBufferShader.getUniformLocation("u_atlasSize"), gbufW, gbufH);
					m_jfaSeedGBufferShader.dispatch(divUp(gbufW, 8), divUp(gbufH, 8), 1);
					m_jfaSeedGBufferShader.memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
				}

				m_seedPosTex[0].bindImage(0, GL_WRITE_ONLY);
				m_seedNormTex[0].bindImage(1, GL_WRITE_ONLY);
				m_atomicSeedTex.bindImage(2, GL_READ_ONLY);
				m_persistentSeedPosTex.bindImage(3, GL_READ_WRITE);
				m_persistentSeedNormTex.bindImage(4, GL_READ_WRITE);
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_triangleSsbo);
				m_orthoWorldPos.bind(0);
				m_orthoNormal.bind(1);
				m_jfaSeedResolveShader.use();
				m_jfaSeedResolveShader.setUniform("t_gbufferWorldPos", 0);
				m_jfaSeedResolveShader.setUniform("t_gbufferNormal", 1);
				m_jfaSeedResolveShader.setUniform("u_resolution", resVec);
				m_jfaSeedResolveShader.setUniform("u_worldSize", params.worldSize);
				m_jfaSeedResolveShader.setUniform("u_worldOffset", params.worldOffset);
				m_jfaSeedResolveShader.setUniform("u_triangleCount", triCount);
				m_jfaSeedResolveShader.setUniform("u_resolveMode", 1);
				m_jfaSeedResolveShader.setUniform("u_accumulateSeeds", useAccumulation ? 1 : 0);
				glUniform2i(m_jfaSeedResolveShader.getUniformLocation("u_atlasSize"), gbufW, gbufH);
				m_jfaSeedResolveShader.dispatch(groups4, groups4, groups4);
				m_jfaSeedResolveShader.memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
			}
		});

		if (useAccumulation)
		{
			m_persistentSeedsValid = true;
			m_persistentRes = res;
			m_persistentWorldSize = params.worldSize;
			m_persistentWorldOffset = params.worldOffset;
			m_persistentModelTransform = params.modelTransform;
		}

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);

		// 2. 3D Jump Flooding pass
		int readIdx = 0;
		const std::vector<int> steps = buildJfaSteps(res, params.jfaVariant);
		stats.floodGpuMs = measureGpuPass(m_timeQuery, [&]() {
			m_jfaStepShader.use();
			m_jfaStepShader.setUniform("u_resolution", resVec);
			m_jfaStepShader.setUniform("u_worldSize", params.worldSize);
			m_jfaStepShader.setUniform("u_worldOffset", params.worldOffset);

			for (int stepSize : steps)
			{
				const int writeIdx = 1 - readIdx;
				m_seedPosTex[readIdx].bindImage(0, GL_READ_ONLY);
				m_seedNormTex[readIdx].bindImage(1, GL_READ_ONLY);
				m_seedPosTex[writeIdx].bindImage(2, GL_WRITE_ONLY);
				m_seedNormTex[writeIdx].bindImage(3, GL_WRITE_ONLY);
				m_jfaStepShader.setUniform("u_stepSize", stepSize);
				m_jfaStepShader.dispatch(groups4, groups4, groups4);
				m_jfaStepShader.memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
				readIdx = writeIdx;
			}
		});

		// 3. Final distance and sign pass
		ComputeShader& finalizeShader = (params.format == SdfFormat::R16F)
			? m_jfaFinalizeShaderR16F
			: m_jfaFinalizeShaderR32F;

		stats.finalizeGpuMs = measureGpuPass(m_timeQuery, [&]() {
			outputTexture.bindImage(0, GL_WRITE_ONLY);
			m_seedPosTex[readIdx].bindImage(1, GL_READ_ONLY);
			m_seedNormTex[readIdx].bindImage(2, GL_READ_ONLY);
			finalizeShader.use();
			finalizeShader.setUniform("u_resolution", resVec);
			finalizeShader.setUniform("u_worldSize", params.worldSize);
			finalizeShader.setUniform("u_worldOffset", params.worldOffset);
			finalizeShader.setUniform("u_unsignedDistance", params.unsignedDistance ? 1 : 0);
			finalizeShader.dispatch(groups4, groups4, groups4);
			finalizeShader.memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
		});

		stats.totalGpuMs = stats.seedGpuMs + stats.floodGpuMs + stats.finalizeGpuMs;
		return stats;
	}

	void SdfGenerator::computeErrorMetrics(const Texture3D& candidateTex, const Texture3D& referenceTex, SdfStats& stats) const
	{
		stats.hasReferenceComparison = false;
		if (candidateTex.id == 0 || referenceTex.id == 0)
		{
			return;
		}

		const int w = candidateTex.getWidth();
		const int h = candidateTex.getHeight();
		const int d = candidateTex.getDepth();
		if (w <= 0 || h <= 0 || d <= 0
			|| w != referenceTex.getWidth()
			|| h != referenceTex.getHeight()
			|| d != referenceTex.getDepth())
		{
			return;
		}

		const size_t voxelCount = static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(d);
		std::vector<float> candidateData(voxelCount, 0.0f);
		std::vector<float> referenceData(voxelCount, 0.0f);

		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glBindTexture(GL_TEXTURE_3D, candidateTex.id);
		glGetTexImage(GL_TEXTURE_3D, 0, GL_RED, GL_FLOAT, candidateData.data());
		glBindTexture(GL_TEXTURE_3D, referenceTex.id);
		glGetTexImage(GL_TEXTURE_3D, 0, GL_RED, GL_FLOAT, referenceData.data());
		glBindTexture(GL_TEXTURE_3D, 0);

		double absSum = 0.0;
		double sqSum = 0.0;
		float maxErr = 0.0f;
		size_t signMatches = 0;
		size_t validCount = 0;

		for (size_t i = 0; i < voxelCount; ++i)
		{
			const float c = candidateData[i];
			const float r = referenceData[i];
			if (!std::isfinite(c) || !std::isfinite(r))
			{
				continue;
			}

			const float err = std::abs(c - r);
			absSum += static_cast<double>(err);
			sqSum += static_cast<double>(err) * static_cast<double>(err);
			maxErr = std::max(maxErr, err);

			if ((c < 0.0f) == (r < 0.0f))
			{
				++signMatches;
			}
			++validCount;
		}

		if (validCount == 0)
		{
			return;
		}

		stats.mae = static_cast<float>(absSum / static_cast<double>(validCount));
		stats.rmse = static_cast<float>(std::sqrt(sqSum / static_cast<double>(validCount)));
		stats.maxError = maxErr;
		stats.signMatchPercent = static_cast<float>((100.0 * static_cast<double>(signMatches)) / static_cast<double>(validCount));
		stats.hasReferenceComparison = true;
	}
} // namespace renderer
