#pragma once

#include <array>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>
#include "renderer/Camera.h"
#include "renderer/SdfGenerator.h"
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
		enum class DynamicObject { MovingMesh, StretchingBox };

		struct Config
		{
			unsigned int renderWidth = 960;
			unsigned int renderHeight = 540;
			int maxAccumulationFrames = 120;
			int rayBounces = 3;
			float contrast = 1.15f;
			bool enableAntialiasing = true;
			bool geometryPreview = false;
			bool showVolumeBounds = false;
			bool showTexturedMeshes = false;
			DynamicObject dynamicObject = DynamicObject::MovingMesh;
			bool pauseDynamicObject = false;
			bool useDynamicReference = false;
			int dynamicResolution = 48;
			float dynamicGridSize = 4.0f;
			float boxThicknessMm = 10.0f;
			bool fixedBoxPose = false;
			bool collectGpuTimings = false;

			// SVGF Denoising Parameters
			bool enableDenoising = true;
			int atrousIterations = 4;
			float temporalAlpha = 0.15f;
			int maxHistoryFrames = 32;
			float phiNormal = 32.0f;
			float phiDepth = 0.1f;
			float phiLuminance = 4.0f;
			int historyFixFrames = 4;          // Frames of spatial prefilter after disocclusion (0 = off)
			float maxFilterRadiusScale = 4.0f; // Max ÃƒÂ -trous radius multiplier for short history (1 = off)
			bool demodulateAlbedo = true;
			int debugView = 0;

			// 3D SDF Texture Parameters
			bool enableSdf3DTexture = true;
			int sdfTextureResolution = 64;
			float sdfTextureWorldSize = 2.0f;
			glm::vec3 sdfTextureOffset = glm::vec3(0.0f, 0.5f, 0.0f);
			int sdfTextureMaterialId = 14;
			SdfMethod sdfMethod = SdfMethod::ExactBaseline;
			JfaVariant jfaVariant = JfaVariant::JfaPlus1;
			SdfFormat sdfFormat = SdfFormat::R32F;
			std::string sdfModelPath = "assets/monkey/demo.gltf";
			bool autoFitSdfMesh = true;
			bool realtimeSdfUpdate = false;
			bool accumulateCameraSdfSeeds = true;
			bool unsignedMeshDistance = false;

			// 2D Orthogonal SDF Slice Inspector
			bool enableSliceInspector = false;
			int sliceAxis = 2; // 0 = X, 1 = Y, 2 = Z
			float slicePosition = 0.5f;
			int sliceViewMode = 0; // 0 = Isolines & Sign, 1 = Error Heatmap |JFA - Ref|

			// Post-Processing Anti-Aliasing (FXAA)
			bool enableFxaa = true;
			int fxaaMode = 0; // 0 = Standard FXAA, 1 = Derivative FXAA (dFdx/dFdy)
			float fxaaThreshold = 0.05f;

			// Noise Generation (White Noise vs Spatiotemporal Blue Noise)
			bool enableBlueNoise = true;

			// Resolution Scaling & Upscaling
			float renderScale = 1.0f; // Scale percentage: 0.1f (10%) to 1.0f (100%)
			int upscaleFilter = 1;    // 0 = Point (Nearest), 1 = Linear (Bilinear)
		};


		PathTracerPipeline(const Config& config, const std::string& shadersRoot, const std::string& assetsRoot);
		~PathTracerPipeline();

		void resize(unsigned int width, unsigned int height);
		void setScene(const SceneDefinition& scene, int sceneIndex = 0);
		void resetAccumulation(bool resetTemporal = false);
		void generateSdf3DTexture(int sceneIndex);
		void generateSdf3DTexture(
			int sceneIndex,
			SdfMethod method,
			JfaVariant jfaVariant,
			SdfFormat format,
			int resolution,
			float worldSize,
			const glm::vec3& worldOffset,
			const std::string& modelPath,
			bool autoFit);
		void generateSdf3DTexture();
		void clearAccumulatedSdfSeeds();
		void clearDynamicCapture() { m_clearDynamicSeeds = true; }
		void restartDynamicExperiment();
		void runReferenceComparison();
		const SdfStats& getLastSdfStats() const { return m_lastSdfStats; }
		void initSceneTextures(const std::vector<SceneDefinition>& scenes);

		struct FrameGpuTimings
		{
			float sdfBuildMs = 0.0f;
			float renderMs = 0.0f;
			float pathTraceMs = 0.0f;
		};

		const std::optional<FrameGpuTimings>& getFrameGpuTimings() const { return m_frameGpuTimings; }

		void render(const Camera& camera, double time, const SceneDefinition& scene, int displayWidth = 0, int displayHeight = 0);
		enum class LightingStage { Raw, Temporal, Filtered };
		// Read linear RGB before contrast, gamma, and FXAA. Rows start at the bottom.
		std::vector<glm::vec4> readLighting(LightingStage stage) const;
		std::vector<unsigned char> readSurfaceMask() const;
		void restartSampling(int seed = 0);
		bool isBlueNoiseAvailable() const { return m_blueNoiseAvailable; }
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

		void loadSdfModel(const std::string& resolvedPath);
		void drawStaticModel(const Camera& camera, bool writeSurface);
		void drawDynamicObject(const Camera& camera, double time);
		void updateDynamicPose(double time);
		void buildDynamicReference();
		void buildDynamicField();
		Shader m_boxShader;
		SdfGenerator m_dynamicSdfGenerator;
		Texture3D m_dynamicDistance;
		Texture3D m_dynamicReference;
		glm::mat4 m_dynamicMeshTransform{1.0f};
		glm::vec3 m_dynamicCenter{0.0f};
		glm::mat3 m_dynamicRotation{1.0f};
		DynamicObject m_previousDynamicObject = DynamicObject::MovingMesh;
		bool m_clearDynamicSeeds = true;
		bool m_previousDynamicReference = false;
		bool m_previousMeshView = false;
		int m_builtDynamicResolution = 0;
		float m_builtDynamicGridSize = 0.0f;
		int m_dynamicReferenceResolution = 0;
		float m_dynamicReferenceGridSize = 0.0f;
		std::optional<double> m_lastDynamicTime;
		double m_dynamicTime = 0.0;
		Shader m_boundsShader;
		bool m_hasDynamicField = false;
		void drawVolumeBounds();
		GLuint m_proceduralVao = 0;
		Shader m_gbufferShader;
		Shader m_sdfShader;
		Shader m_temporalShader;
		Shader m_atrousShader;
		Shader m_resolveShader;
		Shader m_fxaaShader;
		Shader m_blitShader;
		SdfGenerator m_sdfGenerator;
		Model m_sdfModel;
		std::string m_loadedSdfModelPath;
		glm::mat4 m_lastCameraMatrix = glm::mat4(1.0f);
		bool m_resetCameraSeedsRequested = false;

		// 3D SDF volume textures per scene
		struct MeshVolume
		{
			Texture3D distance;
		};
		std::vector<MeshVolume> m_sceneVolumes;
		int m_currentSceneIndex = 0;

		// Reference SDF cache and metrics for live numerical comparison
		Texture3D m_referenceSdfTexture;
		int m_refResolution = 0;
		float m_refWorldSize = 0.0f;
		glm::vec3 m_refWorldOffset = glm::vec3(0.0f);
		std::string m_refModelPath;
		bool m_refAutoFit = true;
		bool m_refUnsignedDistance = false;
		SdfStats m_lastSdfStats;

		struct FrameGpuQueries
		{
			enum Timestamp { RenderStart, DynamicFieldStart, DynamicFieldEnd, TraceStart, TraceEnd, RenderEnd, Count };
			std::array<GLuint, Count> timestamps{};
			float sdfBuildMs = 0.0f;
			bool traced = false;
			bool builtDynamicField = false;
		};
		std::array<FrameGpuQueries, 8> m_gpuQueries{};
		size_t m_nextGpuTimingResult = 0;
		size_t m_pendingGpuTimings = 0;
		float m_pendingSdfBuildMs = 0.0f;
		std::optional<FrameGpuTimings> m_frameGpuTimings;

		// STBN Spatiotemporal Blue Noise 3D textures
		Texture3D m_stbnTexture0;
		Texture3D m_stbnTexture1;
		int m_noiseFrameCounter = 0;
		int m_samplingSeed = 0;
		bool m_blueNoiseAvailable = false;

		// GBuffer resources for mesh rasterization
		Texture m_gbufferAlbedo;
		Texture m_gbufferWorldPos;
		Texture m_gbufferNormal;
		Texture m_gbufferMaterial;
		Texture m_depthTexture;
		Framebuffer m_gbufferFbo;

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
		Texture m_filteredHistory;
		bool m_historyValid = false;

		// SVGF Multi-Scale Ãƒâ‚¬-Trous Wavelet filter ping-pong resources
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
		Model m_dynamicModel;

		int m_frameCounter = 0;
		glm::mat4 m_oldCameraView = glm::mat4(0.0f);
		glm::mat4 m_prevCameraMatrix = glm::mat4(1.0f);
		bool m_firstFrame = true;
		bool m_geometryPreview = false;

		void freeResources();
		FrameGpuQueries* beginFrameGpuTiming();
	};
} // namespace renderer
