#include "renderer/PathTracerPipeline.h"

#include <algorithm>
#include <cmath>
#include <iostream>

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

		resize(m_config.renderWidth, m_config.renderHeight);
	}

	PathTracerPipeline::~PathTracerPipeline()
	{
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

		m_atrousPingTexture.dispose();
		m_atrousPongTexture.dispose();

		m_accumTexture[0].dispose();
		m_accumTexture[1].dispose();
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

		// 7. Output texture
		m_outputTexture = Texture(width, height, Texture::TextureType::LinearData);
		m_outputFbo = Framebuffer();
		m_outputFbo.attachColorTexture(m_outputTexture, 0);

		m_accumIdx = 0;
		m_temporalIdx = 0;
		m_frameCounter = 0;
		m_firstFrame = true;
	}

	void PathTracerPipeline::setScene(const SceneDefinition& scene)
	{
		m_sdfShader.setFragmentIncludeCode("shapes", scene.glslShapesCode, true);
		resetAccumulation();
	}

	void PathTracerPipeline::resetAccumulation()
	{
		m_frameCounter = 0;
		m_firstFrame = true;
	}

	void PathTracerPipeline::render(const Camera& camera, double time, const SceneDefinition& scene)
	{
		// Detect camera movement or shader recompile
		if (m_oldCameraView != camera.view || m_sdfShader.hasRecompiled())
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

		glDisable(GL_DEPTH_TEST);
		m_quad.draw();
		glEnable(GL_DEPTH_TEST);

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
			m_temporalShader.setUniform("u_prevViewProj", prevCamMatrix);
			m_temporalShader.setUniform("u_resolution", glm::vec2(m_config.renderWidth, m_config.renderHeight));
			m_temporalShader.setUniform("u_alpha", m_config.temporalAlpha);
			m_temporalShader.setUniform("u_maxHistory", 32.0f);

			// Current frame inputs
			m_temporalShader.setUniform("t_currIllum", 0);
			m_primaryIllum.bind(0);

			m_temporalShader.setUniform("t_currNormal", 1);
			m_primaryNormal.bind(1);

			m_temporalShader.setUniform("t_currWorldPos", 2);
			m_primaryWorldPos.bind(2);

			m_temporalShader.setUniform("t_currAlbedo", 3);
			m_primaryAlbedo.bind(3);

			// Previous history inputs
			m_temporalShader.setUniform("t_prevIllum", 4);
			m_temporalIllum[prevTempIdx].bind(4);

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
		}

		// -------------------------------------------------------------
		// 4. Resolve Pass (Modulation, Debug Views, sRGB Conversion)
		// -------------------------------------------------------------
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, m_config.renderWidth, m_config.renderHeight);
		glDrawBuffer(GL_BACK);
		glReadBuffer(GL_BACK);

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

		// Advance temporal state
		m_temporalIdx = (m_temporalIdx + 1) % 2;
		m_prevCameraMatrix = camera.cameraMatrix;
		m_frameCounter++;
	}

	void PathTracerPipeline::saveScreenshot(const std::string& filename)
	{
		m_outputFbo.bind();
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

