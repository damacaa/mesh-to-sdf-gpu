#include "renderer/PathTracerPipeline.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace renderer
{
	PathTracerPipeline::PathTracerPipeline(const Config& config, const std::string& shadersRoot, const std::string& assetsRoot)
		: m_config(config)
		, m_shadersRoot(shadersRoot)
		, m_assetsRoot(assetsRoot)
	{
		m_gbufferShader = Shader(shadersRoot + "/3d/geometry.vert", shadersRoot + "/3d/gbuffer.frag");
		m_sdfShader = Shader(shadersRoot + "/common/screen_plane.vert", shadersRoot + "/3d/sdf_raymarching.frag");
		m_temporalShader = Shader(shadersRoot + "/common/screen_plane.vert", shadersRoot + "/denoise/svgf_temporal.frag");
		m_atrousShader = Shader(shadersRoot + "/common/screen_plane.vert", shadersRoot + "/denoise/svgf_atrous.frag");
		m_resolveShader = Shader(shadersRoot + "/common/screen_plane.vert", shadersRoot + "/postprocess/linear_to_srgb.frag");
		m_fxaaShader = Shader(shadersRoot + "/common/screen_plane.vert", shadersRoot + "/postprocess/fxaa.frag");
		m_blitShader = Shader(shadersRoot + "/common/screen_plane.vert", shadersRoot + "/postprocess/blit.frag");
		m_sdfComputeShader = ComputeShader(shadersRoot + "/compute/generate_3d_sdf.comp");

		if (m_config.enableAntialiasing)
		{
			m_sdfShader.addDefine("ANTIALIASING");
		}
		m_sdfShader.addDefine("MESH_SHADOW_SDF");

		std::string monkeyPath = assetsRoot + "/monkey/demo.gltf";
		if (std::filesystem::exists(monkeyPath))
		{
			m_monkeyModel.loadGLTF(monkeyPath);
		}

		// Load Spatiotemporal Blue Noise (STBN) textures
		std::string stbn0Path = assetsRoot + "/stbn/stbn0.bin";
		std::string stbn1Path = assetsRoot + "/stbn/stbn1.bin";
		if (!m_stbnTexture0.loadFromFile(stbn0Path, 128, 128, 64, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_REPEAT))
		{
			std::cerr << "PathTracerPipeline: Failed to load STBN texture 0: " << stbn0Path << std::endl;
		}
		if (!m_stbnTexture1.loadFromFile(stbn1Path, 128, 128, 64, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_REPEAT))
		{
			std::cerr << "PathTracerPipeline: Failed to load STBN texture 1: " << stbn1Path << std::endl;
		}

		m_blueNoiseAvailable = m_stbnTexture0.id != 0 && m_stbnTexture1.id != 0;
		m_config.enableBlueNoise = m_config.enableBlueNoise && m_blueNoiseAvailable;

		auto predefinedScenes = getPredefinedScenes();
		initSceneTextures(predefinedScenes);
		resize(m_config.renderWidth, m_config.renderHeight);
	}

	PathTracerPipeline::~PathTracerPipeline()
	{
		for (auto& tex : m_scene3DTextures)
		{
			tex.dispose();
		}
		m_scene3DTextures.clear();
		m_stbnTexture0.dispose();
		m_stbnTexture1.dispose();
		freeResources();
	}

	void PathTracerPipeline::freeResources()
	{
		m_gbufferFbo.free();
		m_backDepthFbo.free();
		m_primaryFbo.free();
		m_temporalFbo[0].free();
		m_temporalFbo[1].free();
		m_atrousPingFbo.free();
		m_atrousPongFbo.free();
		m_accumFbo[0].free();
		m_accumFbo[1].free();
		m_resolvedFbo.free();
		m_outputFbo.free();

		m_gbufferAlbedo.dispose();
		m_gbufferWorldPos.dispose();
		m_gbufferNormal.dispose();
		m_gbufferMaterial.dispose();
		m_depthTexture.dispose();
		m_backDepthTexture.dispose();

		m_primaryIllum.dispose();
		m_primaryNormal.dispose();
		m_primaryWorldPos.dispose();
		m_primaryAlbedo.dispose();
		m_primaryDepth.dispose();

		m_temporalIllum[0].dispose();
		m_temporalIllum[1].dispose();
		m_temporalMoments[0].dispose();
		m_temporalMoments[1].dispose();
		m_temporalNormal[0].dispose();
		m_temporalNormal[1].dispose();
		m_temporalWorldPos[0].dispose();
		m_temporalWorldPos[1].dispose();

		m_filteredHistory.dispose();
		m_atrousPingTexture.dispose();
		m_atrousPongTexture.dispose();

		m_accumTexture[0].dispose();
		m_accumTexture[1].dispose();
		m_resolvedTexture.dispose();
		m_outputTexture.dispose();
	}

	void PathTracerPipeline::resize(unsigned int width, unsigned int height)
	{
		freeResources();

		m_config.renderWidth = width;
		m_config.renderHeight = height;

		// 1. GBuffer textures & framebuffer (for mesh rasterization)
		m_gbufferAlbedo = Texture(width, height, Texture::TextureType::LinearData);
		m_gbufferWorldPos = Texture(width, height, Texture::TextureType::LinearData);
		m_gbufferNormal = Texture(width, height, Texture::TextureType::LinearData);
		m_gbufferMaterial = Texture(width, height, Texture::TextureType::IntData);
		m_depthTexture = Texture(width, height, Texture::TextureType::Depth);

		m_gbufferFbo = Framebuffer();
		m_gbufferFbo.attachColorTexture(m_gbufferAlbedo, 0);
		m_gbufferFbo.attachColorTexture(m_gbufferWorldPos, 1);
		m_gbufferFbo.attachColorTexture(m_gbufferNormal, 2);
		m_gbufferFbo.attachColorTexture(m_gbufferMaterial, 3);
		m_gbufferFbo.attachDepthTexture(m_depthTexture);

		// 2. Back-face depth target
		m_backDepthTexture = Texture(width, height, Texture::TextureType::Depth);
		m_backDepthFbo = Framebuffer();
		m_backDepthFbo.attachDepthTexture(m_backDepthTexture);

		// 3. Primary 1-SPP MRT Framebuffer
		m_primaryIllum = Texture(width, height, Texture::TextureType::LinearData);
		m_primaryNormal = Texture(width, height, Texture::TextureType::LinearData);
		m_primaryWorldPos = Texture(width, height, Texture::TextureType::LinearData);
		m_primaryAlbedo = Texture(width, height, Texture::TextureType::LinearData);
		m_primaryDepth = Texture(width, height, Texture::TextureType::Depth);

		m_primaryFbo = Framebuffer();
		m_primaryFbo.attachColorTexture(m_primaryIllum, 0);
		m_primaryFbo.attachColorTexture(m_primaryNormal, 1);
		m_primaryFbo.attachColorTexture(m_primaryWorldPos, 2);
		m_primaryFbo.attachColorTexture(m_primaryAlbedo, 3);
		m_primaryFbo.attachDepthTexture(m_primaryDepth);

		// 4. Temporal accumulation ping-pong buffers
		for (int i = 0; i < 2; ++i)
		{
			m_temporalIllum[i] = Texture(width, height, Texture::TextureType::LinearData);
			m_temporalMoments[i] = Texture(width, height, Texture::TextureType::LinearData);
			m_temporalNormal[i] = Texture(width, height, Texture::TextureType::LinearData);
			m_temporalWorldPos[i] = Texture(width, height, Texture::TextureType::LinearData);

			m_temporalFbo[i] = Framebuffer();
			m_temporalFbo[i].attachColorTexture(m_temporalIllum[i], 0);
			m_temporalFbo[i].attachColorTexture(m_temporalMoments[i], 1);
			m_temporalFbo[i].attachColorTexture(m_temporalNormal[i], 2);
			m_temporalFbo[i].attachColorTexture(m_temporalWorldPos[i], 3);
		}

		m_filteredHistory = Texture(width, height, Texture::TextureType::LinearData);
		m_historyValid = false;

		// 5. À-Trous Wavelet filter ping-pong buffers
		m_atrousPingTexture = Texture(width, height, Texture::TextureType::LinearData);
		m_atrousPongTexture = Texture(width, height, Texture::TextureType::LinearData);

		m_atrousPingFbo = Framebuffer();
		m_atrousPingFbo.attachColorTexture(m_atrousPingTexture, 0);

		m_atrousPongFbo = Framebuffer();
		m_atrousPongFbo.attachColorTexture(m_atrousPongTexture, 0);

		// 6. Legacy accumulation ping-pong buffers (RGBA32F)
		m_accumTexture[0] = Texture(width, height, Texture::TextureType::AccumulationData);
		m_accumTexture[1] = Texture(width, height, Texture::TextureType::AccumulationData);

		m_accumFbo[0] = Framebuffer();
		m_accumFbo[0].attachColorTexture(m_accumTexture[0], 0);

		m_accumFbo[1] = Framebuffer();
		m_accumFbo[1].attachColorTexture(m_accumTexture[1], 0);

		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		m_accumFbo[0].bind();
		glClear(GL_COLOR_BUFFER_BIT);
		m_accumFbo[1].bind();
		glClear(GL_COLOR_BUFFER_BIT);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// 7. Resolved intermediate sRGB target (before FXAA)
		m_resolvedTexture = Texture(width, height, Texture::TextureType::LinearData);
		m_resolvedFbo = Framebuffer();
		m_resolvedFbo.attachColorTexture(m_resolvedTexture, 0);

		// 8. Output texture
		m_outputTexture = Texture(width, height, Texture::TextureType::LinearData);
		m_outputFbo = Framebuffer();
		m_outputFbo.attachColorTexture(m_outputTexture, 0);

		m_accumIdx = 0;
		m_temporalIdx = 0;
		m_frameCounter = 0;
		m_firstFrame = true;
	}

	void PathTracerPipeline::initSceneTextures(const std::vector<SceneDefinition>& scenes)
	{
		for (auto& tex : m_scene3DTextures)
		{
			tex.dispose();
		}
		m_scene3DTextures.clear();
		m_scene3DTextures.resize(scenes.size());

		for (size_t i = 0; i < scenes.size(); ++i)
		{
			generateSdf3DTexture(static_cast<int>(i), scenes[i].sdfBakeMode, scenes[i].sdfTextureResolution,
								scenes[i].sdfTextureWorldSize, scenes[i].sdfTextureOffset);
		}
	}

	void PathTracerPipeline::setScene(const SceneDefinition& scene, int sceneIndex)
	{
		m_currentSceneIndex = sceneIndex;
		m_config.enableSdf3DTexture = scene.enableSdf3DTexture;
		m_config.sdfTextureResolution = scene.sdfTextureResolution;
		m_config.sdfTextureWorldSize = scene.sdfTextureWorldSize;
		m_config.sdfTextureOffset = scene.sdfTextureOffset;
		m_config.sdfTextureMaterialId = scene.sdfTextureMaterialId;
		m_config.sdfBakeMode = scene.sdfBakeMode;

		if (m_currentSceneIndex >= static_cast<int>(m_scene3DTextures.size()))
		{
			m_scene3DTextures.resize(m_currentSceneIndex + 1);
		}

		if (m_scene3DTextures[m_currentSceneIndex].id == 0)
		{
			generateSdf3DTexture(m_currentSceneIndex, m_config.sdfBakeMode, m_config.sdfTextureResolution,
								m_config.sdfTextureWorldSize, m_config.sdfTextureOffset);
		}

		m_sdfShader.setFragmentIncludeCode("shapes", scene.glslShapesCode, true);
		resetAccumulation(true);
	}

	void PathTracerPipeline::resetAccumulation(bool resetTemporal)
	{
		m_frameCounter = 0;
		if (resetTemporal)
		{
			m_historyValid = false;
			m_firstFrame = true;
			m_noiseFrameCounter = m_samplingSeed & 63;
			m_accumIdx = 0;
		}
	}

	void PathTracerPipeline::restartSampling(int seed)
	{
		if (seed < 0) throw std::invalid_argument("The sampling seed must be nonnegative.");
		m_samplingSeed = seed;
		m_noiseFrameCounter = seed & 63;
		resetAccumulation(true);
	}

	std::vector<glm::vec4> PathTracerPipeline::readLighting(LightingStage stage) const
	{
		if (!m_config.enableDenoising || !m_historyValid)
			throw std::logic_error("Render a denoised frame before reading linear lighting.");
		const Texture* texture = &m_primaryIllum;
		if (stage != LightingStage::Raw) texture = &m_temporalIllum[(m_temporalIdx + 1) % 2];
		int iterations = std::clamp(m_config.atrousIterations, 0, 5);
		if (stage == LightingStage::Filtered && iterations > 0)
			texture = (iterations % 2 == 1) ? &m_atrousPingTexture : &m_atrousPongTexture;
		std::vector<glm::vec4> pixels(size_t(m_config.renderWidth) * m_config.renderHeight);
		glGetTextureImage(texture->id, 0, GL_RGBA, GL_FLOAT,
			static_cast<GLsizei>(pixels.size() * sizeof(glm::vec4)), pixels.data());
		if (m_config.demodulateAlbedo)
		{
			std::vector<glm::vec4> albedo(pixels.size());
			glGetTextureImage(m_primaryAlbedo.id, 0, GL_RGBA, GL_FLOAT,
				static_cast<GLsizei>(albedo.size() * sizeof(glm::vec4)), albedo.data());
			// Match the resolve pass, including its treatment of dark albedos.
			for (size_t i = 0; i < pixels.size(); ++i)
				for (int channel = 0; channel < 3; ++channel) pixels[i][channel] *= albedo[i][channel];
		}
		return pixels;
	}

	std::vector<unsigned char> PathTracerPipeline::readSurfaceMask() const
	{
		if (!m_historyValid) throw std::logic_error("Render a frame before reading its surface mask.");
		std::vector<glm::vec4> normals(size_t(m_config.renderWidth) * m_config.renderHeight);
		glGetTextureImage(m_primaryNormal.id, 0, GL_RGBA, GL_FLOAT,
			static_cast<GLsizei>(normals.size() * sizeof(glm::vec4)), normals.data());
		std::vector<unsigned char> mask(normals.size());
		for (size_t i = 0; i < mask.size(); ++i) mask[i] = normals[i].w > 0.5f ? 1 : 0;
		return mask;
	}

	void PathTracerPipeline::generateSdf3DTexture(int sceneIndex, int bakeMode, int resolution, float worldSize, const glm::vec3& worldOffset)
	{
		if (sceneIndex < 0) return;
		if (sceneIndex >= static_cast<int>(m_scene3DTextures.size()))
		{
			m_scene3DTextures.resize(sceneIndex + 1);
		}

		int res = std::max(resolution, 8);
		auto& tex = m_scene3DTextures[sceneIndex];
		if (tex.id == 0 || tex.getWidth() != res || tex.getHeight() != res || tex.getDepth() != res)
		{
			tex.allocate(res, res, res, GL_R32F);
		}

		m_sdfComputeShader.use();
		tex.bindImage(0, GL_WRITE_ONLY);

		m_sdfComputeShader.setUniform("u_resolution", glm::ivec3(res, res, res));
		m_sdfComputeShader.setUniform("u_worldSize", worldSize);
		m_sdfComputeShader.setUniform("u_worldOffset", worldOffset);
		m_sdfComputeShader.setUniform("u_bakeMode", bakeMode);

		GLuint groups = static_cast<GLuint>((res + 7) / 8);
		m_sdfComputeShader.dispatch(groups, groups, groups);
		m_sdfComputeShader.memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
	}

	void PathTracerPipeline::generateSdf3DTexture(int sceneIndex)
	{
		generateSdf3DTexture(sceneIndex, m_config.sdfBakeMode, m_config.sdfTextureResolution,
							m_config.sdfTextureWorldSize, m_config.sdfTextureOffset);
	}

	void PathTracerPipeline::generateSdf3DTexture()
	{
		generateSdf3DTexture(m_currentSceneIndex);
	}

	void PathTracerPipeline::render(const Camera& camera, double time, const SceneDefinition& scene, int displayWidth, int displayHeight)
	{
		m_noiseFrameCounter = (m_noiseFrameCounter + 1) & 63;

		// Detect shader changes before the current frame uses history.
		bool shaderChanged = m_sdfShader.hasRecompiled();
		m_sdfShader.use();
		if (shaderChanged || m_sdfShader.hasRecompiled())
		{
			resetAccumulation(true);
		}

		bool cameraMoved = (m_oldCameraView != camera.view || m_prevCameraMatrix != camera.cameraMatrix);
		if (cameraMoved)
		{
			m_frameCounter = 0;
			m_oldCameraView = camera.view;
		}

		if (m_firstFrame)
		{
			m_prevCameraMatrix = camera.cameraMatrix;
			m_firstFrame = false;
		}
		glm::mat4 prevCamMatrix = m_prevCameraMatrix;

		// -------------------------------------------------------------
		// 1. GBuffer Rasterization Pass (Mesh geometry)
		// -------------------------------------------------------------
		glViewport(0, 0, m_config.renderWidth, m_config.renderHeight);
		m_gbufferFbo.bind();

		const GLenum drawBuffers[4] = {
			GL_COLOR_ATTACHMENT0,
			GL_COLOR_ATTACHMENT1,
			GL_COLOR_ATTACHMENT2,
			GL_COLOR_ATTACHMENT3
		};
		glDrawBuffers(4, drawBuffers);

		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClearDepth(1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);

		if (scene.renderMonkeyMesh && m_monkeyModel.isValid())
		{
			m_gbufferShader.use();
			m_gbufferShader.setUniform("u_camMatrix", camera.cameraMatrix);
			m_monkeyModel.draw(m_gbufferShader, scene.monkeyTransform, scene.monkeyMaterialIndex);

			// Back-depth slab pass
			m_backDepthFbo.bind();
			const GLenum noneBuff[1] = { GL_NONE };
			glDrawBuffers(1, noneBuff);
			glReadBuffer(GL_NONE);

			glClearDepth(0.0f);
			glClear(GL_DEPTH_BUFFER_BIT);

			glDepthFunc(GL_GREATER);
			glCullFace(GL_FRONT);

			m_gbufferShader.use();
			m_monkeyModel.draw(m_gbufferShader, scene.monkeyTransform, scene.monkeyMaterialIndex);

			glCullFace(GL_BACK);
			glDepthFunc(GL_LESS);
			glClearDepth(1.0f);
		}
		else
		{
			// Clear back-depth when no mesh is rendered
			m_backDepthFbo.bind();
			glClearDepth(1.0f);
			glClear(GL_DEPTH_BUFFER_BIT);
		}

		// -------------------------------------------------------------
		// 2. Primary Path Tracing Pass
		// -------------------------------------------------------------
		bool shouldRaymarch = m_config.enableDenoising || (m_frameCounter < m_config.maxAccumulationFrames);

		if (shouldRaymarch)
		{
			int prevAccumIdx = m_accumIdx;
			if (!m_config.enableDenoising)
			{
				m_accumIdx = (m_accumIdx + 1) % 2;
				m_accumFbo[m_accumIdx].bind();
				const GLenum singleBuf[1] = { GL_COLOR_ATTACHMENT0 };
				glDrawBuffers(1, singleBuf);
				glReadBuffer(GL_COLOR_ATTACHMENT0);
			}
			else
			{
				m_primaryFbo.bind();
				const GLenum primaryBuffers[4] = {
					GL_COLOR_ATTACHMENT0,
					GL_COLOR_ATTACHMENT1,
					GL_COLOR_ATTACHMENT2,
					GL_COLOR_ATTACHMENT3
				};
				glDrawBuffers(4, primaryBuffers);
				glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			}

			m_sdfShader.use();

			float shaderFov = 1.0f / std::tan(glm::radians(camera.fov) * 0.5f);
			m_sdfShader.setUniform("u_camMatrix", camera.view);
			m_sdfShader.setUniform("u_viewProjection", camera.cameraMatrix);
			m_sdfShader.setUniform("u_fov", shaderFov);
			m_sdfShader.setUniform("u_time", static_cast<float>(time));
			m_sdfShader.setUniform("u_resolution", glm::vec2(m_config.renderWidth, m_config.renderHeight));
			m_sdfShader.setUniform("u_near", camera.nearPlane);
			m_sdfShader.setUniform("u_far", camera.farPlane);
			m_sdfShader.setUniform("u_frameCounter", m_frameCounter);
			m_sdfShader.setUniform("u_rayBounces", m_config.rayBounces);
			m_sdfShader.setUniform("u_contrast", m_config.contrast);
			m_sdfShader.setUniform("u_enableDenoising", m_config.enableDenoising ? 1 : 0);
			m_sdfShader.setUniform("u_demodulateAlbedo", m_config.demodulateAlbedo ? 1 : 0);
			m_sdfShader.setUniform("u_enableMeshShadows", m_config.enableMeshShadows ? 1 : 0);

			// 3D SDF Texture Parameters
			m_sdfShader.setUniform("u_enableSdf3DTexture", m_config.enableSdf3DTexture ? 1 : 0);
			m_sdfShader.setUniform("u_sdfTextureWorldSize", m_config.sdfTextureWorldSize);
			m_sdfShader.setUniform("u_sdfTextureOffset", m_config.sdfTextureOffset);
			m_sdfShader.setUniform("u_sdfTextureMaterialId", m_config.sdfTextureMaterialId);

			// Materials
			for (int i = 0; i < 16; ++i)
			{
				std::string prefix = "u_materials[" + std::to_string(i) + "].";
				if (i < static_cast<int>(scene.materials.size()))
				{
					const auto& mat = scene.materials[i];
					m_sdfShader.setUniform(prefix + "color", mat.color);
					m_sdfShader.setUniform(prefix + "metallic", mat.metallic);
					m_sdfShader.setUniform(prefix + "roughness", mat.roughness);
					m_sdfShader.setUniform(prefix + "pattern", static_cast<int>(mat.pattern));
					m_sdfShader.setUniform(prefix + "patternScale", mat.patternScale);
					m_sdfShader.setUniform(prefix + "secondaryColor", mat.secondaryColor);
					m_sdfShader.setUniform(prefix + "emission", mat.emission);
				}
				else
				{
					m_sdfShader.setUniform(prefix + "color", glm::vec4(1.0f));
					m_sdfShader.setUniform(prefix + "metallic", 0.0f);
					m_sdfShader.setUniform(prefix + "roughness", 1.0f);
					m_sdfShader.setUniform(prefix + "pattern", 0);
					m_sdfShader.setUniform(prefix + "patternScale", 1.0f);
					m_sdfShader.setUniform(prefix + "secondaryColor", glm::vec4(0.0f));
					m_sdfShader.setUniform(prefix + "emission", 0.0f);
				}
			}

			// Lights
			int numLights = std::min(static_cast<int>(scene.lights.size()), 8);
			m_sdfShader.setUniform("u_numLights", numLights);
			for (int i = 0; i < numLights; ++i)
			{
				std::string prefix = "u_lights[" + std::to_string(i) + "].";
				m_sdfShader.setUniform(prefix + "position", scene.lights[i].position);
				m_sdfShader.setUniform(prefix + "direction", scene.lights[i].direction);
				m_sdfShader.setUniform(prefix + "color", scene.lights[i].color);
				m_sdfShader.setUniform(prefix + "type", static_cast<int>(scene.lights[i].type));
			}

			// Bind G-buffer and accumulation textures
			m_sdfShader.setUniform("t_previousColor", 0);
			m_accumTexture[prevAccumIdx].bind(0);

			m_sdfShader.setUniform("t_depthTexture", 1);
			m_depthTexture.bind(1);

			m_sdfShader.setUniform("t_sdfTexture3D", 2);
			if (m_currentSceneIndex >= 0 && m_currentSceneIndex < static_cast<int>(m_scene3DTextures.size()))
			{
				m_scene3DTextures[m_currentSceneIndex].bind(2);
			}

			m_sdfShader.setUniform("t_gbufferAlbedo", 3);
			m_gbufferAlbedo.bind(3);

			m_sdfShader.setUniform("t_gbufferWorldPos", 4);
			m_gbufferWorldPos.bind(4);

			m_sdfShader.setUniform("t_gbufferNormal", 5);
			m_gbufferNormal.bind(5);

			m_sdfShader.setUniform("t_gbufferMaterial", 6);
			m_gbufferMaterial.bind(6);

			m_sdfShader.setUniform("t_gbufferBackDepth", 7);
			m_backDepthTexture.bind(7);

			m_sdfShader.setUniform("u_enableBlueNoise", (m_config.enableBlueNoise && m_blueNoiseAvailable) ? 1 : 0);
			m_sdfShader.setUniform("u_noiseFrameIndex", m_noiseFrameCounter);
			m_sdfShader.setUniform("u_samplingSeed", m_samplingSeed);

			m_sdfShader.setUniform("t_stbn0", 8);
			m_stbnTexture0.bind(8);

			m_sdfShader.setUniform("t_stbn1", 9);
			m_stbnTexture1.bind(9);

			glDisable(GL_DEPTH_TEST);
			m_quad.draw();
			glEnable(GL_DEPTH_TEST);
		}

		// -------------------------------------------------------------
		// 3. SVGF Denoising Pipeline
		// -------------------------------------------------------------
		const Texture* finalFilteredTexture = nullptr;

		if (m_config.enableDenoising)
		{
			int prevTempIdx = (m_temporalIdx + 1) % 2;

			// --- Pass 3.1: Temporal Reprojection & Moments Pass ---
			m_temporalFbo[m_temporalIdx].bind();
			const GLenum tempBuffers[4] = {
				GL_COLOR_ATTACHMENT0,
				GL_COLOR_ATTACHMENT1,
				GL_COLOR_ATTACHMENT2,
				GL_COLOR_ATTACHMENT3
			};
			glDrawBuffers(4, tempBuffers);

			m_temporalShader.use();
			if (m_temporalShader.hasRecompiled()) m_historyValid = false;
			m_temporalShader.setUniform("u_resetHistory", m_historyValid ? 0 : 1);
			m_temporalShader.setUniform("u_prevViewProj", prevCamMatrix);
			m_temporalShader.setUniform("u_resolution", glm::vec2(m_config.renderWidth, m_config.renderHeight));
			m_temporalShader.setUniform("u_alpha", m_config.temporalAlpha);
			// The history cap must allow alpha to reach u_alpha.
			float maxHistory = std::max(float(std::max(m_config.maxHistoryFrames, 1)),
				std::ceil(1.0f / std::max(m_config.temporalAlpha, 1e-3f)));
			m_temporalShader.setUniform("u_maxHistory", maxHistory);
			m_temporalShader.setUniform("u_cameraMoved", cameraMoved ? 1 : 0);
			m_temporalShader.setUniform("u_historyFixFrames", float(std::max(m_config.historyFixFrames, 0)));

			// Current frame inputs
			m_temporalShader.setUniform("t_currIllum", 0);
			m_primaryIllum.bind(0);

			m_temporalShader.setUniform("t_currNormal", 1);
			m_primaryNormal.bind(1);

			m_temporalShader.setUniform("t_currWorldPos", 2);
			m_primaryWorldPos.bind(2);

			// Previous history inputs
			m_temporalShader.setUniform("t_prevIllum", 4);
			m_filteredHistory.bind(4);

			m_temporalShader.setUniform("t_prevMoments", 5);
			m_temporalMoments[prevTempIdx].bind(5);

			m_temporalShader.setUniform("t_prevNormal", 6);
			m_temporalNormal[prevTempIdx].bind(6);

			m_temporalShader.setUniform("t_prevWorldPos", 7);
			m_temporalWorldPos[prevTempIdx].bind(7);

			glDisable(GL_DEPTH_TEST);
			m_quad.draw();
			glEnable(GL_DEPTH_TEST);

			// --- Pass 3.2: Multi-Scale À-Trous Wavelet Filter Passes ---
			finalFilteredTexture = &m_temporalIllum[m_temporalIdx];

			int iterations = std::clamp(m_config.atrousIterations, 0, 5);
			for (int i = 0; i < iterations; ++i)
			{
				Framebuffer& targetFbo = (i % 2 == 0) ? m_atrousPingFbo : m_atrousPongFbo;
				targetFbo.bind();
				const GLenum singleBuf[1] = { GL_COLOR_ATTACHMENT0 };
				glDrawBuffers(1, singleBuf);

				m_atrousShader.use();
				int stepSize = 1 << i; // 1, 2, 4, 8, 16
				m_atrousShader.setUniform("u_stepSize", stepSize);
				m_atrousShader.setUniform("u_resolution", glm::vec2(m_config.renderWidth, m_config.renderHeight));
				m_atrousShader.setUniform("u_phiNormal", m_config.phiNormal);
				m_atrousShader.setUniform("u_phiDepth", m_config.phiDepth);
				m_atrousShader.setUniform("u_phiLuminance", m_config.phiLuminance);

				const Texture* inputTex = nullptr;
				if (i == 0)
				{
					inputTex = &m_temporalIllum[m_temporalIdx];
				}
				else if (i % 2 == 1)
				{
					inputTex = &m_atrousPingTexture;
				}
				else
				{
					inputTex = &m_atrousPongTexture;
				}

				m_atrousShader.setUniform("t_illumination", 0);
				inputTex->bind(0);

				m_atrousShader.setUniform("t_normal", 1);
				m_primaryNormal.bind(1);

				m_atrousShader.setUniform("t_worldPos", 2);
				m_primaryWorldPos.bind(2);

				m_atrousShader.setUniform("t_moments", 3);
				m_temporalMoments[m_temporalIdx].bind(3);
				m_atrousShader.setUniform("u_alpha", m_config.temporalAlpha);
				m_atrousShader.setUniform("u_maxRadiusScale", m_config.maxFilterRadiusScale);

				glDisable(GL_DEPTH_TEST);
				m_quad.draw();
				glEnable(GL_DEPTH_TEST);

				if (i % 2 == 0)
				{
					finalFilteredTexture = &m_atrousPingTexture;
				}
				else
				{
					finalFilteredTexture = &m_atrousPongTexture;
				}
			}
			// Keep the unfiltered temporal result as history. Spatial blur must not feed back.
			glCopyImageSubData(m_temporalIllum[m_temporalIdx].id, GL_TEXTURE_2D, 0, 0, 0, 0,
				m_filteredHistory.id, GL_TEXTURE_2D, 0, 0, 0, 0,
				m_config.renderWidth, m_config.renderHeight, 1);
			m_historyValid = true;
		}
		else
		{
			m_historyValid = false;
		}

		// -------------------------------------------------------------
		// 4. Resolve Pass (Modulation, Debug Views, sRGB Conversion)
		// -------------------------------------------------------------
		m_resolvedFbo.bind();
		glViewport(0, 0, m_config.renderWidth, m_config.renderHeight);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		m_resolveShader.use();
		m_resolveShader.setUniform("u_debugView", m_config.debugView);
		m_resolveShader.setUniform("u_demodulateAlbedo", m_config.demodulateAlbedo ? 1 : 0);
		m_resolveShader.setUniform("u_contrast", m_config.contrast);
		m_resolveShader.setUniform("u_enableDenoising", m_config.enableDenoising ? 1 : 0);

		if (m_config.enableDenoising && finalFilteredTexture)
		{
			m_resolveShader.setUniform("t_filteredIllum", 0);
			finalFilteredTexture->bind(0);

			m_resolveShader.setUniform("t_albedo", 1);
			m_primaryAlbedo.bind(1);

			m_resolveShader.setUniform("t_rawIllum", 2);
			m_primaryIllum.bind(2);

			m_resolveShader.setUniform("t_normal", 3);
			m_primaryNormal.bind(3);
		}
		else
		{
			m_resolveShader.setUniform("t_filteredIllum", 0);
			m_accumTexture[m_accumIdx].bind(0);

			m_resolveShader.setUniform("t_albedo", 1);
			m_primaryAlbedo.bind(1);

			m_resolveShader.setUniform("t_rawIllum", 2);
			m_accumTexture[m_accumIdx].bind(2);

			m_resolveShader.setUniform("t_normal", 3);
			m_primaryNormal.bind(3);
		}

		glDisable(GL_DEPTH_TEST);
		m_quad.draw();
		glEnable(GL_DEPTH_TEST);

		// -------------------------------------------------------------
		// 5. Post-Processing Anti-Aliasing (FXAA)
		// -------------------------------------------------------------
		m_outputFbo.bind();
		glViewport(0, 0, m_config.renderWidth, m_config.renderHeight);
		const GLenum singleBuf[1] = { GL_COLOR_ATTACHMENT0 };
		glDrawBuffers(1, singleBuf);

		m_fxaaShader.use();
		m_fxaaShader.setUniform("t_screen", 0);
		m_resolvedTexture.bind(0);
		m_fxaaShader.setUniform("u_enableFxaa", m_config.enableFxaa ? 1 : 0);
		m_fxaaShader.setUniform("u_fxaaMode", m_config.fxaaMode);
		m_fxaaShader.setUniform("u_edgeThreshold", m_config.fxaaThreshold);
		m_fxaaShader.setUniform("u_texelSize", glm::vec2(1.0f / m_config.renderWidth, 1.0f / m_config.renderHeight));

		glDisable(GL_DEPTH_TEST);
		m_quad.draw();
		glEnable(GL_DEPTH_TEST);

		// -------------------------------------------------------------
		// 6. Final Upscale & Screen Presentation Pass
		// -------------------------------------------------------------
		int targetWidth = (displayWidth > 0) ? displayWidth : static_cast<int>(m_config.renderWidth);
		int targetHeight = (displayHeight > 0) ? displayHeight : static_cast<int>(m_config.renderHeight);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, targetWidth, targetHeight);
		glDrawBuffer(GL_BACK);

		m_outputTexture.bind(0);
		GLint upscaleFilterMode = (m_config.upscaleFilter == 1) ? GL_LINEAR : GL_NEAREST;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, upscaleFilterMode);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, upscaleFilterMode);

		m_blitShader.use();
		m_blitShader.setUniform("t_texture", 0);

		glDisable(GL_DEPTH_TEST);
		m_quad.draw();
		glEnable(GL_DEPTH_TEST);

		// Advance temporal state
		m_temporalIdx = (m_temporalIdx + 1) % 2;
		m_prevCameraMatrix = camera.cameraMatrix;
		if (shouldRaymarch)
		{
			m_frameCounter++;
		}
	}

	void PathTracerPipeline::saveScreenshot(const std::string& filename)
	{
		m_resolvedFbo.bind();
		glViewport(0, 0, m_config.renderWidth, m_config.renderHeight);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		m_resolveShader.use();
		m_resolveShader.setUniform("u_debugView", m_config.debugView);
		m_resolveShader.setUniform("u_demodulateAlbedo", m_config.demodulateAlbedo ? 1 : 0);
		m_resolveShader.setUniform("u_contrast", m_config.contrast);
		m_resolveShader.setUniform("u_enableDenoising", m_config.enableDenoising ? 1 : 0);

		if (m_config.enableDenoising)
		{
			const Texture* finalTex = nullptr;
			int iterations = std::clamp(m_config.atrousIterations, 0, 5);
			if (iterations <= 0)
			{
				int prevTempIdx = (m_temporalIdx + 1) % 2;
				finalTex = &m_temporalIllum[prevTempIdx];
			}
			else if ((iterations - 1) % 2 == 0)
			{
				finalTex = &m_atrousPingTexture;
			}
			else
			{
				finalTex = &m_atrousPongTexture;
			}

			m_resolveShader.setUniform("t_filteredIllum", 0);
			finalTex->bind(0);

			m_resolveShader.setUniform("t_albedo", 1);
			m_primaryAlbedo.bind(1);

			m_resolveShader.setUniform("t_rawIllum", 2);
			m_primaryIllum.bind(2);

			m_resolveShader.setUniform("t_normal", 3);
			m_primaryNormal.bind(3);
		}
		else
		{
			m_resolveShader.setUniform("t_filteredIllum", 0);
			m_accumTexture[m_accumIdx].bind(0);

			m_resolveShader.setUniform("t_albedo", 1);
			m_primaryAlbedo.bind(1);

			m_resolveShader.setUniform("t_rawIllum", 2);
			m_accumTexture[m_accumIdx].bind(2);

			m_resolveShader.setUniform("t_normal", 3);
			m_primaryNormal.bind(3);
		}

		glDisable(GL_DEPTH_TEST);
		m_quad.draw();
		glEnable(GL_DEPTH_TEST);

		// Post-process FXAA into m_outputFbo for file export
		m_outputFbo.bind();
		glViewport(0, 0, m_config.renderWidth, m_config.renderHeight);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		m_fxaaShader.use();
		m_fxaaShader.setUniform("t_screen", 0);
		m_resolvedTexture.bind(0);
		m_fxaaShader.setUniform("u_enableFxaa", m_config.enableFxaa ? 1 : 0);
		m_fxaaShader.setUniform("u_fxaaMode", m_config.fxaaMode);
		m_fxaaShader.setUniform("u_edgeThreshold", m_config.fxaaThreshold);
		m_fxaaShader.setUniform("u_texelSize", glm::vec2(1.0f / m_config.renderWidth, 1.0f / m_config.renderHeight));

		glDisable(GL_DEPTH_TEST);
		m_quad.draw();
		glEnable(GL_DEPTH_TEST);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		m_outputTexture.saveToDisk(filename);
	}

	void PathTracerPipeline::debugPixel(int x, int y)
	{
		float pix[4] = { 0 };

		// Read GBuffer
		m_gbufferFbo.bind();
		glReadBuffer(GL_COLOR_ATTACHMENT0); // albedo
		glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, pix);
		std::cout << "[DEBUG " << x << "," << y << "] GBuffer Albedo: " << pix[0] << ", " << pix[1] << ", " << pix[2] << ", " << pix[3] << "\n";

		float depth = 0;
		glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
		std::cout << "[DEBUG " << x << "," << y << "] GBuffer Depth: " << depth << "\n";

		// Read Primary
		m_primaryFbo.bind();
		glReadBuffer(GL_COLOR_ATTACHMENT0); // illum
		glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, pix);
		std::cout << "[DEBUG " << x << "," << y << "] Primary Illum: " << pix[0] << ", " << pix[1] << ", " << pix[2] << ", " << pix[3] << "\n";

		glReadBuffer(GL_COLOR_ATTACHMENT1); // normal
		glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, pix);
		std::cout << "[DEBUG " << x << "," << y << "] Primary Normal: " << pix[0] << ", " << pix[1] << ", " << pix[2] << ", Hit=" << pix[3] << "\n";

		glReadBuffer(GL_COLOR_ATTACHMENT3); // albedo
		glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, pix);
		std::cout << "[DEBUG " << x << "," << y << "] Primary Albedo: " << pix[0] << ", " << pix[1] << ", " << pix[2] << ", " << pix[3] << "\n";

		// Read Temporal
		m_temporalFbo[0].bind();
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, pix);
		std::cout << "[DEBUG " << x << "," << y << "] Temporal Illum: " << pix[0] << ", " << pix[1] << ", " << pix[2] << ", Var=" << pix[3] << "\n";

		// Read Atrous Pong
		m_atrousPongFbo.bind();
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, pix);
		std::cout << "[DEBUG " << x << "," << y << "] Atrous Pong: " << pix[0] << ", " << pix[1] << ", " << pix[2] << ", Var=" << pix[3] << "\n";

		// Read Screen Back Buffer
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glReadBuffer(GL_BACK);
		unsigned char screenPix[4] = { 0 };
		glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, screenPix);
		std::cout << "[DEBUG " << x << "," << y << "] Screen Back Buffer: " << (int)screenPix[0] << ", " << (int)screenPix[1] << ", " << (int)screenPix[2] << "\n";

	}
} // namespace renderer

