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
		m_accumFbo[0].free();
		m_accumFbo[1].free();
		m_outputFbo.free();

		m_gbufferAlbedo.dispose();
		m_gbufferWorldPos.dispose();
		m_gbufferNormal.dispose();
		m_gbufferMaterial.dispose();
		m_depthTexture.dispose();
		m_backDepthTexture.dispose();
		m_accumTexture[0].dispose();
		m_accumTexture[1].dispose();
		m_outputTexture.dispose();
	}

	void PathTracerPipeline::resize(unsigned int width, unsigned int height)
	{
		freeResources();

		m_config.renderWidth = width;
		m_config.renderHeight = height;

		// 1. GBuffer textures & framebuffer
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

		// 3. Accumulation ping-pong buffers (RGBA32F)
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

		// 4. Output texture
		m_outputTexture = Texture(width, height, Texture::TextureType::LinearData);
		m_outputFbo = Framebuffer();
		m_outputFbo.attachColorTexture(m_outputTexture, 0);

		m_accumIdx = 0;
		m_frameCounter = 0;
	}

	void PathTracerPipeline::setScene(const SceneDefinition& scene)
	{
		m_sdfShader.setFragmentIncludeCode("shapes", scene.glslShapesCode, true);
		resetAccumulation();
	}

	void PathTracerPipeline::resetAccumulation()
	{
		m_frameCounter = 0;
	}

	void PathTracerPipeline::render(const Camera& camera, double time, const SceneDefinition& scene)
	{
		// Detect camera movement
		if (m_oldCameraView != camera.view || m_sdfShader.hasRecompiled())
		{
			m_frameCounter = 0;
			m_oldCameraView = camera.view;
		}

		// -------------------------------------------------------------
		// 1. GBuffer Rasterization Pass
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
		// 2. SDF Path Tracing Accumulation Pass
		// -------------------------------------------------------------
		if (m_frameCounter < m_config.maxAccumulationFrames)
		{
			int prevAccumIdx = m_accumIdx;
			m_accumIdx = (m_accumIdx + 1) % 2;

			m_accumFbo[m_accumIdx].bind();
			const GLenum singleBuf[1] = { GL_COLOR_ATTACHMENT0 };
			glDrawBuffers(1, singleBuf);
			glReadBuffer(GL_COLOR_ATTACHMENT0);

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

			// Bind textures
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

			m_frameCounter++;
		}

		// -------------------------------------------------------------
		// 3. Resolve Pass (Linear to sRGB gamma tone mapping)
		// -------------------------------------------------------------
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, m_config.renderWidth, m_config.renderHeight);
		glDrawBuffer(GL_BACK);
		glReadBuffer(GL_BACK);

		m_resolveShader.use();
		m_resolveShader.setUniform("t_input", 0);
		m_accumTexture[m_accumIdx].bind(0);

		glDisable(GL_DEPTH_TEST);
		m_quad.draw();
		glEnable(GL_DEPTH_TEST);
	}

	void PathTracerPipeline::saveScreenshot(const std::string& filename)
	{
		m_outputFbo.bind();
		glViewport(0, 0, m_config.renderWidth, m_config.renderHeight);
		const GLenum drawBuf[1] = { GL_COLOR_ATTACHMENT0 };
		glDrawBuffers(1, drawBuf);

		m_resolveShader.use();
		m_resolveShader.setUniform("t_input", 0);
		m_accumTexture[m_accumIdx].bind(0);

		glDisable(GL_DEPTH_TEST);
		m_quad.draw();
		glEnable(GL_DEPTH_TEST);

		m_outputTexture.saveToDisk(filename);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
} // namespace renderer
