#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "renderer/Camera.h"
#include "renderer/Framebuffer.h"
#include "renderer/FullscreenQuad.h"
#include "renderer/Light.h"
#include "renderer/Material.h"
#include "renderer/Model.h"
#include "renderer/Scenes.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"

namespace renderer
{
	class PathTracerPipeline
	{
	public:
		struct Config
		{
			unsigned int renderWidth = 960;
			unsigned int renderHeight = 540;
			int maxAccumulationFrames = 120;
			int rayBounces = 3;
			float contrast = 1.15f;
			bool enableAntialiasing = true;
		};

		PathTracerPipeline(const Config& config, const std::string& shadersRoot, const std::string& assetsRoot);
		~PathTracerPipeline();

		void resize(unsigned int width, unsigned int height);
		void setScene(const SceneDefinition& scene);
		void resetAccumulation();

		void render(const Camera& camera, double time, const SceneDefinition& scene);
		void saveScreenshot(const std::string& filename);

		Config& getConfig() { return m_config; }
		const Config& getConfig() const { return m_config; }
		int getFrameCounter() const { return m_frameCounter; }
		Shader& getSdfShader() { return m_sdfShader; }

	private:
		Config m_config;
		std::string m_shadersRoot;
		std::string m_assetsRoot;

		Shader m_gbufferShader;
		Shader m_sdfShader;
		Shader m_resolveShader;

		// GBuffer resources
		Texture m_gbufferAlbedo;
		Texture m_gbufferWorldPos;
		Texture m_gbufferNormal;
		Texture m_gbufferMaterial;
		Texture m_depthTexture;
		Texture m_backDepthTexture;

		Framebuffer m_gbufferFbo;
		Framebuffer m_backDepthFbo;

		// Double-buffered accumulation targets
		Texture m_accumTexture[2];
		Framebuffer m_accumFbo[2];
		int m_accumIdx = 0;

		// Final resolve output
		Texture m_outputTexture;
		Framebuffer m_outputFbo;

		FullscreenQuad m_quad;
		Model m_monkeyModel;

		int m_frameCounter = 0;
		glm::mat4 m_oldCameraView = glm::mat4(0.0f);

		void freeResources();
	};
} // namespace renderer
