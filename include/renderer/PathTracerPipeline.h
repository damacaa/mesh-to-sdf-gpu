#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "renderer/Camera.h"
#include "renderer/ComputeShader.h"
#include "renderer/Framebuffer.h"
#include "renderer/FullscreenQuad.h"
#include "renderer/Light.h"
#include "renderer/Material.h"
#include "renderer/Model.h"
#include "renderer/Scenes.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "renderer/Texture3D.h"

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
			bool enableMeshShadows = true;

			// SVGF Denoising Parameters
			bool enableDenoising = true;
			int atrousIterations = 4;
			float temporalAlpha = 0.15f;
			float phiNormal = 32.0f;
			float phiDepth = 1.0f;
			float phiLuminance = 4.0f;
			bool demodulateAlbedo = false;
			int debugView = 0;

			// 3D SDF Texture Parameters
			bool enableSdf3DTexture = true;
			int sdfTextureResolution = 64;
			float sdfTextureWorldSize = 2.0f;
			glm::vec3 sdfTextureOffset = glm::vec3(0.0f, 0.5f, 0.0f);
			int sdfTextureMaterialId = 14;
			int sdfBakeMode = 0; // 0 = Sphere, 1 = Scene 2 Primitives, 2 = Room with Window

			// Post-Processing Anti-Aliasing (FXAA)
			bool enableFxaa = true;
			int fxaaMode = 0; // 0 = Standard FXAA, 1 = Derivative FXAA (dFdx/dFdy)
			float fxaaThreshold = 0.05f;
		};


		PathTracerPipeline(const Config& config, const std::string& shadersRoot, const std::string& assetsRoot);
		~PathTracerPipeline();

		void resize(unsigned int width, unsigned int height);
		void setScene(const SceneDefinition& scene, int sceneIndex = 0);
		void resetAccumulation(bool resetTemporal = false);
		void generateSdf3DTexture(int sceneIndex);
		void generateSdf3DTexture(int sceneIndex, int bakeMode, int resolution, float worldSize, const glm::vec3& worldOffset);
		void generateSdf3DTexture();
		void initSceneTextures(const std::vector<SceneDefinition>& scenes);

		void render(const Camera& camera, double time, const SceneDefinition& scene);
		void saveScreenshot(const std::string& filename);
		void debugPixel(int x, int y);

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
		Shader m_temporalShader;
		Shader m_atrousShader;
		Shader m_resolveShader;
		Shader m_fxaaShader;
		ComputeShader m_sdfComputeShader;

		// 3D SDF volume textures per scene
		std::vector<Texture3D> m_scene3DTextures;
		int m_currentSceneIndex = 0;

		// GBuffer resources for mesh rasterization
		Texture m_gbufferAlbedo;
		Texture m_gbufferWorldPos;
		Texture m_gbufferNormal;
		Texture m_gbufferMaterial;
		Texture m_depthTexture;
		Texture m_backDepthTexture;

		Framebuffer m_gbufferFbo;
		Framebuffer m_backDepthFbo;

		// Primary 1-SPP Path Tracer MRT resources
		Texture m_primaryIllum;
		Texture m_primaryNormal;
		Texture m_primaryWorldPos;
		Texture m_primaryAlbedo;
		Texture m_primaryDepth;
		Framebuffer m_primaryFbo;

		// SVGF Temporal Accumulation & Moments ping-pong resources
		Texture m_temporalIllum[2];
		Texture m_temporalMoments[2];
		Texture m_temporalNormal[2];
		Texture m_temporalWorldPos[2];
		Framebuffer m_temporalFbo[2];
		int m_temporalIdx = 0;

		// SVGF Multi-Scale À-Trous Wavelet filter ping-pong resources
		Texture m_atrousPingTexture;
		Texture m_atrousPongTexture;
		Framebuffer m_atrousPingFbo;
		Framebuffer m_atrousPongFbo;

		// Legacy double-buffered accumulation targets
		Texture m_accumTexture[2];
		Framebuffer m_accumFbo[2];
		int m_accumIdx = 0;

		// Resolved intermediate sRGB target before FXAA
		Texture m_resolvedTexture;
		Framebuffer m_resolvedFbo;

		// Final resolve output
		Texture m_outputTexture;
		Framebuffer m_outputFbo;

		FullscreenQuad m_quad;
		Model m_monkeyModel;

		int m_frameCounter = 0;
		glm::mat4 m_oldCameraView = glm::mat4(0.0f);
		glm::mat4 m_prevCameraMatrix = glm::mat4(1.0f);
		bool m_firstFrame = true;

		void freeResources();
	};
} // namespace renderer
