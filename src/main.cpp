#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "renderer/Camera.h"
#include "renderer/PathTracerPipeline.h"
#include "renderer/Scenes.h"

#ifndef SHADERS_PATH
#define SHADERS_PATH "./shaders"
#endif

#ifndef ASSETS_PATH
#define ASSETS_PATH "./assets"
#endif

int main(int argc, char* argv[])
{
	bool isTestMode = (argc > 1 && std::string(argv[1]) == "--test");

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
	{
		std::cerr << "Failed to initialize SDL3: " << SDL_GetError() << std::endl;
		return 1;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	int windowWidth = 960;
	int windowHeight = 540;

	SDL_Window* window = SDL_CreateWindow(
		"TFM Path Tracer Prototype",
		windowWidth,
		windowHeight,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (isTestMode ? SDL_WINDOW_HIDDEN : 0)
	);

	if (!window)
	{
		std::cerr << "Failed to create SDL3 window: " << SDL_GetError() << std::endl;
		SDL_Quit();
		return 1;
	}

	SDL_GLContext glContext = SDL_GL_CreateContext(window);
	if (!glContext)
	{
		std::cerr << "Failed to create OpenGL context: " << SDL_GetError() << std::endl;
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	SDL_GL_MakeCurrent(window, glContext);
	SDL_GL_SetSwapInterval(0); // Uncapped framerate for rapid path tracing accumulation

	if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
	{
		std::cerr << "Failed to initialize GLAD OpenGL loader!" << std::endl;
		SDL_GL_DestroyContext(glContext);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
	std::cout << "GLSL Version:   " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
	std::cout << "GPU Renderer:   " << glGetString(GL_RENDERER) << std::endl;

	// Setup Dear ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	(void)io;
	ImGui::StyleColorsDark();

	ImGui_ImplSDL3_InitForOpenGL(window, glContext);
	ImGui_ImplOpenGL3_Init("#version 460");

	// Predefined scenes
	auto scenes = renderer::getPredefinedScenes();
	int currentSceneIndex = 0;

	// Setup camera
	renderer::Camera camera(scenes[currentSceneIndex].cameraPosition);
	camera.yaw = scenes[currentSceneIndex].cameraYaw;
	camera.pitch = scenes[currentSceneIndex].cameraPitch;
	camera.updateVectors();
	camera.updateMatrix(windowWidth, windowHeight);

	// Setup Path Tracer pipeline
	renderer::PathTracerPipeline::Config config;
	config.renderWidth = windowWidth;
	config.renderHeight = windowHeight;
	config.rayBounces = 3;
	config.maxAccumulationFrames = 150;
	config.contrast = 1.15f;

	renderer::PathTracerPipeline pipeline(config, SHADERS_PATH, ASSETS_PATH);
	pipeline.setScene(scenes[currentSceneIndex], currentSceneIndex);

	auto switchScene = [&](int newIndex) {
		if (newIndex >= 0 && newIndex < static_cast<int>(scenes.size()) && newIndex != currentSceneIndex)
		{
			scenes[currentSceneIndex].enableSdf3DTexture = pipeline.getConfig().enableSdf3DTexture;
			scenes[currentSceneIndex].sdfTextureResolution = pipeline.getConfig().sdfTextureResolution;
			scenes[currentSceneIndex].sdfTextureWorldSize = pipeline.getConfig().sdfTextureWorldSize;
			scenes[currentSceneIndex].sdfTextureOffset = pipeline.getConfig().sdfTextureOffset;
			scenes[currentSceneIndex].sdfBakeMode = pipeline.getConfig().sdfBakeMode;

			currentSceneIndex = newIndex;
			pipeline.setScene(scenes[currentSceneIndex], currentSceneIndex);
			camera.position = scenes[currentSceneIndex].cameraPosition;
			camera.yaw = scenes[currentSceneIndex].cameraYaw;
			camera.pitch = scenes[currentSceneIndex].cameraPitch;
			camera.updateVectors();
		}
	};

	int testFrames = 0;

	bool running = true;
	bool mouseLooking = false;
	float moveSpeed = 4.0f;
	bool takeScreenshotRequested = false;
	std::string lastScreenshotPath;
	bool showUi = true;
	bool enableFpsLimit = false;
	int targetFps = 60;
	float smoothedWorkMs = 0.0f;
	float smoothedIdleMs = 0.0f;

	auto lastTime = std::chrono::high_resolution_clock::now();
	auto startTime = lastTime;

	while (running)
	{
		auto frameStartTime = std::chrono::high_resolution_clock::now();
		auto currentTime = frameStartTime;
		float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
		lastTime = currentTime;
		double totalTime = std::chrono::duration<double>(currentTime - startTime).count();

		if (isTestMode)
		{
			testFrames++;
			if (testFrames == 20)
			{
				switchScene(1);
			}
			else if (testFrames == 40)
			{
				switchScene(2);
			}
			else if (testFrames == 60)
			{
				switchScene(3);
			}
			else if (testFrames == 80)
			{
				switchScene(4);
			}
			else if (testFrames >= 100)
			{
				std::cout << "All scenes rendered successfully without errors." << std::endl;
				running = false;
				break;
			}
		}

		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			ImGui_ImplSDL3_ProcessEvent(&event);

			switch (event.type)
			{
				case SDL_EVENT_QUIT:
					running = false;
					break;

				case SDL_EVENT_KEY_DOWN:
					if (event.key.scancode == SDL_SCANCODE_F3 && !event.key.repeat)
					{
						showUi = !showUi;
						break;
					}

					if (!io.WantCaptureKeyboard && !event.key.repeat)
					{
						int targetScene = -1;
						if (event.key.scancode == SDL_SCANCODE_1) targetScene = 0;
						else if (event.key.scancode == SDL_SCANCODE_2) targetScene = 1;
						else if (event.key.scancode == SDL_SCANCODE_3) targetScene = 2;
						else if (event.key.scancode == SDL_SCANCODE_4) targetScene = 3;
						else if (event.key.scancode == SDL_SCANCODE_5) targetScene = 4;

						if (targetScene >= 0)
						{
							switchScene(targetScene);
						}
					}
					break;

				case SDL_EVENT_WINDOW_RESIZED:
				case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				{
					int newW = 0, newH = 0;
					SDL_GetWindowSizeInPixels(window, &newW, &newH);
					if (newW > 0 && newH > 0 && (newW != windowWidth || newH != windowHeight))
					{
						windowWidth = newW;
						windowHeight = newH;
						int targetRenderW = std::max(1, static_cast<int>(windowWidth * pipeline.getConfig().renderScale));
						int targetRenderH = std::max(1, static_cast<int>(windowHeight * pipeline.getConfig().renderScale));
						pipeline.resize(targetRenderW, targetRenderH);
						camera.updateMatrix(windowWidth, windowHeight);
					}
					break;
				}

				case SDL_EVENT_MOUSE_BUTTON_DOWN:
					if (event.button.button == SDL_BUTTON_RIGHT)
					{
						mouseLooking = true;
						SDL_SetWindowRelativeMouseMode(window, true);
					}
					break;

				case SDL_EVENT_MOUSE_BUTTON_UP:
					if (event.button.button == SDL_BUTTON_RIGHT)
					{
						mouseLooking = false;
						SDL_SetWindowRelativeMouseMode(window, false);
					}
					break;

				case SDL_EVENT_MOUSE_MOTION:
					if (mouseLooking)
					{
						camera.processMouseMovement(event.motion.xrel, event.motion.yrel);
						pipeline.resetAccumulation();
					}
					break;

				default:
					break;
			}
		}

		// Keyboard input for camera motion
		if (!io.WantCaptureKeyboard || mouseLooking)
		{
			const bool* keyStates = SDL_GetKeyboardState(nullptr);
			float fwd = 0.0f, right = 0.0f, up = 0.0f;

			if (keyStates[SDL_SCANCODE_W]) fwd += 1.0f;
			if (keyStates[SDL_SCANCODE_S]) fwd -= 1.0f;
			if (keyStates[SDL_SCANCODE_D]) right += 1.0f;
			if (keyStates[SDL_SCANCODE_A]) right -= 1.0f;
			if (keyStates[SDL_SCANCODE_E] || keyStates[SDL_SCANCODE_SPACE]) up += 1.0f;
			if (keyStates[SDL_SCANCODE_Q] || keyStates[SDL_SCANCODE_LCTRL]) up -= 1.0f;

			float currentSpeed = moveSpeed;
			if (keyStates[SDL_SCANCODE_LSHIFT])
			{
				currentSpeed *= 2.5f;
			}

			if (fwd != 0.0f || right != 0.0f || up != 0.0f)
			{
				camera.processKeyboard(fwd, right, up, deltaTime, currentSpeed);
				pipeline.resetAccumulation();
			}
		}

		camera.updateMatrix(windowWidth, windowHeight);

		// Start ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		// Debug Settings Window
		if (showUi)
		{
			constexpr float panelWidth = 400.0f;
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(panelWidth, io.DisplaySize.y), ImGuiCond_Always);
			const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
			ImGui::Begin("Path Tracer Controls", nullptr, windowFlags);

			float displayFrameTimeMs = (enableFpsLimit && smoothedWorkMs > 0.0f)
				? smoothedWorkMs
				: (1000.0f / (io.Framerate > 0.0f ? io.Framerate : 1.0f));
			ImGui::Text("FPS: %.1f (%.2f ms)", io.Framerate, displayFrameTimeMs);
			ImGui::SameLine();
			ImGui::TextDisabled("(F3 to hide)");

			static float fpsHistory[120] = { 0.0f };
			static int fpsOffset = 0;
			fpsHistory[fpsOffset] = io.Framerate;
			fpsOffset = (fpsOffset + 1) % IM_ARRAYSIZE(fpsHistory);

			float maxFpsVal = 60.0f;
			for (int n = 0; n < IM_ARRAYSIZE(fpsHistory); n++)
			{
				if (fpsHistory[n] > maxFpsVal) maxFpsVal = fpsHistory[n];
			}
			maxFpsVal = std::ceil(maxFpsVal / 30.0f) * 30.0f;

			ImGui::PlotLines("##FPSGraph", fpsHistory, IM_ARRAYSIZE(fpsHistory), fpsOffset, nullptr, 0.0f, maxFpsVal, ImVec2(0.0f, 45.0f));

			ImGui::Checkbox("Limit FPS", &enableFpsLimit);
			ImGui::SameLine();
			if (!enableFpsLimit) ImGui::BeginDisabled(true);
			ImGui::SetNextItemWidth(130.0f);
			ImGui::SliderInt("##TargetFPS", &targetFps, 15, 240, "%d FPS");
			if (!enableFpsLimit) ImGui::EndDisabled();

			if (enableFpsLimit)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(idle: %.1f ms)", smoothedIdleMs);
			}

			if (ImGui::Button("Take Screenshot"))
			{
				takeScreenshotRequested = true;
			}

			if (!lastScreenshotPath.empty())
			{
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
				ImGui::TextUnformatted(lastScreenshotPath.c_str());
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered())
				{
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
					ImGui::SetTooltip("Click to open");
				}
				if (ImGui::IsItemClicked())
				{
					std::string url = "file://" + lastScreenshotPath;
					SDL_OpenURL(url.c_str());
				}
			}

			ImGui::Separator();

			if (ImGui::BeginTabBar("ControlsTabBar", ImGuiTabBarFlags_None))
			{
				if (ImGui::BeginTabItem("Scene Settings"))
				{
					if (ImGui::BeginChild("SceneTabScroll", ImVec2(0.0f, 0.0f)))
					{
						ImGui::SeparatorText("Scene Selection");
						const char* sceneNames[] = {
							"1. Cornell Box",
							"2. Primitives Showcase",
							"3. Hybrid Mesh + SDF",
							"4. 3D Texture Only",
							"5. 3D Texture Room"
						};
						int selectedScene = currentSceneIndex;
						if (ImGui::Combo("Scene", &selectedScene, sceneNames, IM_ARRAYSIZE(sceneNames)))
						{
							switchScene(selectedScene);
						}
						ImGui::TextDisabled("Shortcuts: Keys 1, 2, 3, 4, 5");

						ImGui::SeparatorText("3D SDF Volume Texture (Compute)");
						if (ImGui::Checkbox("Enable 3D SDF Texture", &pipeline.getConfig().enableSdf3DTexture))
						{
							pipeline.resetAccumulation(true);
						}

						if (pipeline.getConfig().enableSdf3DTexture)
						{
							bool needsRegen = false;

							const char* bakeModes[] = { "Sphere (1.0m)", "Scene 2 Primitives", "Room with Window" };
							if (ImGui::Combo("Bake Mode", &pipeline.getConfig().sdfBakeMode, bakeModes, IM_ARRAYSIZE(bakeModes)))
							{
								needsRegen = true;
							}

							const char* resOptions[] = { "32 x 32 x 32", "64 x 64 x 64", "128 x 128 x 128", "256 x 256 x 256" };
							int currentResIdx = 1;
							if (pipeline.getConfig().sdfTextureResolution <= 32) currentResIdx = 0;
							else if (pipeline.getConfig().sdfTextureResolution <= 64) currentResIdx = 1;
							else if (pipeline.getConfig().sdfTextureResolution <= 128) currentResIdx = 2;
							else currentResIdx = 3;

							if (ImGui::Combo("Resolution", &currentResIdx, resOptions, IM_ARRAYSIZE(resOptions)))
							{
								int resolutions[] = { 32, 64, 128, 256 };
								pipeline.getConfig().sdfTextureResolution = resolutions[currentResIdx];
								needsRegen = true;
							}

							if (ImGui::SliderFloat("World Size (m)", &pipeline.getConfig().sdfTextureWorldSize, 1.0f, 25.0f, "%.2f"))
							{
								needsRegen = true;
							}

							if (ImGui::DragFloat3("World Offset (m)", glm::value_ptr(pipeline.getConfig().sdfTextureOffset), 0.05f))
							{
								pipeline.resetAccumulation(true);
							}

							if (ImGui::Button("Regenerate 3D SDF") || needsRegen)
							{
								pipeline.generateSdf3DTexture();
								pipeline.resetAccumulation(true);
							}

							if (pipeline.getConfig().sdfBakeMode == 0)
							{
								ImGui::TextDisabled("Bake Shape: 1.0m diameter sphere");
							}
							else if (pipeline.getConfig().sdfBakeMode == 1)
							{
								ImGui::TextDisabled("Bake Shape: Scene 2 geometric primitives");
							}
							else if (pipeline.getConfig().sdfBakeMode == 2)
							{
								ImGui::TextDisabled("Bake Shape: Closed room with subtract cube window");
							}
						}
					}
					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Render Settings"))
				{
					if (ImGui::BeginChild("RenderTabScroll", ImVec2(0.0f, 0.0f)))
					{
						ImGui::SeparatorText("Path Tracer Settings");
						if (ImGui::SliderInt("Bounces", &pipeline.getConfig().rayBounces, 1, 8))
						{
							pipeline.resetAccumulation(true);
						}
						if (ImGui::SliderFloat("Contrast", &pipeline.getConfig().contrast, 1.0f, 3.0f))
						{
							pipeline.resetAccumulation();
						}
						if (ImGui::Checkbox("Mesh Casts Shadows", &pipeline.getConfig().enableMeshShadows))
						{
							pipeline.resetAccumulation(true);
						}

						ImGui::SeparatorText("Denoising & Accumulation");
						if (ImGui::Checkbox("Enable SVGF Denoiser", &pipeline.getConfig().enableDenoising))
						{
							pipeline.resetAccumulation(true);
						}
						if (ImGui::Checkbox("Spatiotemporal Blue Noise (STBN)", &pipeline.getConfig().enableBlueNoise))
						{
							pipeline.resetAccumulation(true);
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("Switch between Spatiotemporal Blue Noise (NVIDIA STBN) and White Noise (PCG).\nSTBN can reduce residual noise after filtering, especially while the camera is still.");
						}

						if (pipeline.getConfig().enableDenoising)
						{
							const char* debugViewNames[] = {
								"Final Denoised",
								"Raw 1-SPP Noisy",
								"Denoised Irradiance",
								"Estimated Variance",
								"Surface Normals",
								"Surface Albedo"
							};
							ImGui::Combo("View Mode", &pipeline.getConfig().debugView, debugViewNames, IM_ARRAYSIZE(debugViewNames));
							if (ImGui::SliderInt("À-Trous Iterations", &pipeline.getConfig().atrousIterations, 0, 5))
							{
								pipeline.resetAccumulation(true);
							}
							ImGui::SliderFloat("Temporal Alpha", &pipeline.getConfig().temporalAlpha, 0.02f, 0.5f, "%.2f");
							ImGui::SliderFloat("Normal Sensitivity", &pipeline.getConfig().phiNormal, 4.0f, 128.0f, "%.1f");
							ImGui::SliderFloat("Depth Sensitivity", &pipeline.getConfig().phiDepth, 0.01f, 1.0f, "%.2f");
							ImGui::SliderFloat("Luminance Sensitivity", &pipeline.getConfig().phiLuminance, 0.5f, 10.0f, "%.1f");
							if (ImGui::Checkbox("Demodulate Albedo", &pipeline.getConfig().demodulateAlbedo))
							{
								pipeline.resetAccumulation(true);
							}
						}
						else
						{
							if (ImGui::SliderInt("Max Accumulation", &pipeline.getConfig().maxAccumulationFrames, 10, 500))
							{
								pipeline.resetAccumulation();
							}

							int currentFrames = pipeline.getFrameCounter();
							int maxFrames = pipeline.getConfig().maxAccumulationFrames;
							float progress = static_cast<float>(std::min(currentFrames, maxFrames)) / static_cast<float>(maxFrames);

							ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f));
							ImGui::SameLine();
							ImGui::Text("%d / %d", currentFrames, maxFrames);

							if (ImGui::Button("Reset Accumulation"))
							{
								pipeline.resetAccumulation(true);
							}
						}

						ImGui::SeparatorText("Resolution Scaling & Upscaling");
						float scalePercent = pipeline.getConfig().renderScale * 100.0f;
						if (ImGui::SliderFloat("Render Scale", &scalePercent, 10.0f, 100.0f, "%.0f%%"))
						{
							pipeline.getConfig().renderScale = scalePercent / 100.0f;
							int targetRenderW = std::max(1, static_cast<int>(windowWidth * pipeline.getConfig().renderScale));
							int targetRenderH = std::max(1, static_cast<int>(windowHeight * pipeline.getConfig().renderScale));
							if (targetRenderW != static_cast<int>(pipeline.getConfig().renderWidth) ||
								targetRenderH != static_cast<int>(pipeline.getConfig().renderHeight))
							{
								pipeline.resize(targetRenderW, targetRenderH);
								pipeline.resetAccumulation(true);
							}
						}
						ImGui::Text("Internal Resolution: %d x %d (Native: %d x %d)",
							pipeline.getConfig().renderWidth, pipeline.getConfig().renderHeight,
							windowWidth, windowHeight);

						const char* upscaleFilters[] = { "Point (Nearest)", "Linear (Bilinear)" };
						ImGui::Combo("Upscale Filtering", &pipeline.getConfig().upscaleFilter, upscaleFilters, IM_ARRAYSIZE(upscaleFilters));

						ImGui::SeparatorText("Post-Processing Anti-Aliasing (FXAA)");
						ImGui::Checkbox("Enable FXAA", &pipeline.getConfig().enableFxaa);
						if (pipeline.getConfig().enableFxaa)
						{
							const char* fxaaModes[] = { "Standard FXAA (Multi-Tap)", "Derivative FXAA (dFdx / dFdy)" };
							ImGui::Combo("FXAA Algorithm", &pipeline.getConfig().fxaaMode, fxaaModes, IM_ARRAYSIZE(fxaaModes));
							if (pipeline.getConfig().fxaaMode == 1)
							{
								ImGui::SliderFloat("Derivative Threshold", &pipeline.getConfig().fxaaThreshold, 0.01f, 0.2f, "%.3f");
							}
						}
					}
					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Camera"))
				{
					if (ImGui::BeginChild("CameraTabScroll", ImVec2(0.0f, 0.0f)))
					{
						ImGui::SeparatorText("Camera Transform");
						if (ImGui::DragFloat3("Position", glm::value_ptr(camera.position), 0.05f))
						{
							camera.updateVectors();
							camera.updateMatrix(windowWidth, windowHeight);
							pipeline.resetAccumulation();
						}

						bool orientationChanged = false;
						if (ImGui::SliderFloat("Yaw", &camera.yaw, -360.0f, 360.0f, "%.1f°"))
						{
							orientationChanged = true;
						}
						if (ImGui::SliderFloat("Pitch", &camera.pitch, -89.0f, 89.0f, "%.1f°"))
						{
							orientationChanged = true;
						}
						if (orientationChanged)
						{
							camera.updateVectors();
							camera.updateMatrix(windowWidth, windowHeight);
							pipeline.resetAccumulation();
						}

						if (ImGui::Button("Reset to Scene Default"))
						{
							camera.position = scenes[currentSceneIndex].cameraPosition;
							camera.yaw = scenes[currentSceneIndex].cameraYaw;
							camera.pitch = scenes[currentSceneIndex].cameraPitch;
							camera.updateVectors();
							camera.updateMatrix(windowWidth, windowHeight);
							pipeline.resetAccumulation();
						}

						ImGui::SeparatorText("Optics & Projection");
						if (ImGui::SliderFloat("FOV", &camera.fov, 20.0f, 100.0f, "%.1f°"))
						{
							camera.updateMatrix(windowWidth, windowHeight);
							pipeline.resetAccumulation();
						}
						if (ImGui::DragFloat("Near Plane", &camera.nearPlane, 0.01f, 0.01f, 5.0f, "%.2f m"))
						{
							camera.updateMatrix(windowWidth, windowHeight);
							pipeline.resetAccumulation();
						}
						if (ImGui::DragFloat("Far Plane", &camera.farPlane, 1.0f, 10.0f, 1000.0f, "%.1f m"))
						{
							camera.updateMatrix(windowWidth, windowHeight);
							pipeline.resetAccumulation();
						}

						ImGui::SeparatorText("Navigation & Speed");
						ImGui::SliderFloat("Move Speed", &moveSpeed, 1.0f, 20.0f, "%.1f m/s");
						ImGui::Spacing();
						ImGui::TextDisabled("Controls:");
						ImGui::BulletText("Hold Right Mouse Button: Look around");
						ImGui::BulletText("W / A / S / D: Move horizontally");
						ImGui::BulletText("E / Space: Move up");
						ImGui::BulletText("Q / Left Ctrl: Move down");
						ImGui::BulletText("Left Shift: Boost speed");
					}
					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Materials"))
				{
					if (ImGui::BeginChild("MaterialsTabScroll", ImVec2(0.0f, 0.0f)))
					{
						auto& currentMaterials = scenes[currentSceneIndex].materials;

						ImGui::SeparatorText("Scene Materials");

						if (ImGui::Button("Reset to Defaults"))
						{
							currentMaterials = renderer::getPredefinedScenes()[currentSceneIndex].materials;
							pipeline.resetAccumulation(true);
						}

						static bool showAllSlots = false;
						ImGui::SameLine();
						ImGui::Checkbox("Show All 16 Slots", &showAllSlots);

						ImGui::Spacing();

						for (size_t i = 0; i < currentMaterials.size(); ++i)
						{
							auto& mat = currentMaterials[i];
							if (mat.name.empty() && !showAllSlots)
							{
								continue;
							}

							ImGui::PushID(static_cast<int>(i));

							char headerLabel[80];
							std::snprintf(headerLabel, sizeof(headerLabel), "[%d] %s###MaterialHeader_%d",
								static_cast<int>(i), mat.name.empty() ? "(Unused Slot)" : mat.name.c_str(), static_cast<int>(i));

							if (ImGui::CollapsingHeader(headerLabel, ImGuiTreeNodeFlags_DefaultOpen))
							{
								if (ImGui::ColorEdit3("Color", glm::value_ptr(mat.color)))
								{
									pipeline.resetAccumulation(true);
								}

								if (ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f, "%.2f"))
								{
									pipeline.resetAccumulation(true);
								}

								if (ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f, "%.2f"))
								{
									pipeline.resetAccumulation(true);
								}

								if (ImGui::DragFloat("Emission", &mat.emission, 0.1f, 0.0f, 50.0f, "%.2f"))
								{
									pipeline.resetAccumulation(true);
								}

								const char* patternNames[] = { "None", "Checkers", "Perlin Noise", "Waves" };
								int patternIdx = static_cast<int>(mat.pattern);
								if (ImGui::Combo("Pattern", &patternIdx, patternNames, IM_ARRAYSIZE(patternNames)))
								{
									mat.pattern = static_cast<renderer::MaterialPattern>(patternIdx);
									pipeline.resetAccumulation(true);
								}

								if (mat.pattern != renderer::MaterialPattern::None)
								{
									if (ImGui::ColorEdit3("Secondary Color", glm::value_ptr(mat.secondaryColor)))
									{
										pipeline.resetAccumulation(true);
									}
									if (ImGui::SliderFloat("Pattern Scale", &mat.patternScale, 0.1f, 20.0f, "%.2f"))
									{
										pipeline.resetAccumulation(true);
									}
								}

								ImGui::Spacing();
							}

							ImGui::PopID();
						}
					}
					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Lights"))
				{
					if (ImGui::BeginChild("LightsTabScroll", ImVec2(0.0f, 0.0f)))
					{
						auto& currentLights = scenes[currentSceneIndex].lights;

						ImGui::SeparatorText("Scene Lights");
						ImGui::Text("Active Lights: %d / 8", static_cast<int>(currentLights.size()));

						if (currentLights.size() < 8)
						{
							if (ImGui::Button("+ Add Light"))
							{
								renderer::Light3D newLight;
								newLight.type = 1; // Point light
								newLight.position = camera.position + camera.orientation * 2.0f;
								newLight.direction = glm::vec3(0.0f, -1.0f, 0.0f);
								newLight.color = glm::vec4(1.0f, 1.0f, 1.0f, 2.0f);
								currentLights.push_back(newLight);
								pipeline.resetAccumulation(true);
							}
						}
						else
						{
							ImGui::BeginDisabled(true);
							ImGui::Button("+ Add Light");
							ImGui::EndDisabled();
							ImGui::SameLine();
							ImGui::TextDisabled("(Maximum 8 lights reached)");
						}

						ImGui::SameLine();
						if (ImGui::Button("Reset to Defaults"))
						{
							currentLights = renderer::getPredefinedScenes()[currentSceneIndex].lights;
							pipeline.resetAccumulation(true);
						}

						ImGui::Spacing();

						if (currentLights.empty())
						{
							ImGui::TextDisabled("No lights in current scene. Click \"+ Add Light\" to add one.");
						}

						int lightToDelete = -1;
						for (size_t i = 0; i < currentLights.size(); ++i)
						{
							auto& light = currentLights[i];
							ImGui::PushID(static_cast<int>(i));

							const char* typeNames[] = { "Directional", "Point", "Spot" };
							const char* currentTypeName = (light.type <= 2) ? typeNames[light.type] : "Unknown";

							char headerLabel[64];
							std::snprintf(headerLabel, sizeof(headerLabel), "Light %d: %s###LightHeader_%d",
								static_cast<int>(i + 1), currentTypeName, static_cast<int>(i));

							if (ImGui::CollapsingHeader(headerLabel, ImGuiTreeNodeFlags_DefaultOpen))
							{
								int selectedType = static_cast<int>(light.type);
								if (ImGui::Combo("Type", &selectedType, typeNames, IM_ARRAYSIZE(typeNames)))
								{
									light.type = static_cast<uint32_t>(selectedType);
									pipeline.resetAccumulation(true);
								}

								if (ImGui::ColorEdit3("Color", glm::value_ptr(light.color)))
								{
									pipeline.resetAccumulation(true);
								}

								if (ImGui::DragFloat("Intensity", &light.color.w, 0.05f, 0.0f, 50.0f, "%.2f"))
								{
									pipeline.resetAccumulation(true);
								}

								if (light.type == 1 || light.type == 2)
								{
									if (ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.05f))
									{
										pipeline.resetAccumulation(true);
									}

									if (ImGui::Button("Set to Camera Position"))
									{
										light.position = camera.position;
										pipeline.resetAccumulation(true);
									}
								}
								else
								{
									ImGui::BeginDisabled(true);
									ImGui::DragFloat3("Position (N/A)", glm::value_ptr(light.position), 0.05f);
									ImGui::EndDisabled();
									if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
									{
										ImGui::SetTooltip("Directional lights do not use position.");
									}
								}

								if (light.type == 0 || light.type == 2)
								{
									if (ImGui::DragFloat3("Direction", glm::value_ptr(light.direction), 0.02f, -1.0f, 1.0f, "%.2f"))
									{
										if (glm::length(light.direction) > 0.0001f)
										{
											light.direction = glm::normalize(light.direction);
										}
										pipeline.resetAccumulation(true);
									}

									if (ImGui::Button("Set to Camera Direction"))
									{
										light.direction = camera.orientation;
										pipeline.resetAccumulation(true);
									}
								}
								else
								{
									ImGui::BeginDisabled(true);
									ImGui::DragFloat3("Direction (N/A)", glm::value_ptr(light.direction), 0.02f);
									ImGui::EndDisabled();
									if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
									{
										ImGui::SetTooltip("Point lights radiate equally in all directions.");
									}
								}

								ImGui::Spacing();
								ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
								ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
								ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.1f, 0.1f, 1.0f));
								if (ImGui::Button("Delete Light"))
								{
									lightToDelete = static_cast<int>(i);
								}
								ImGui::PopStyleColor(3);

								ImGui::Spacing();
							}

							ImGui::PopID();
						}

						if (lightToDelete >= 0 && lightToDelete < static_cast<int>(currentLights.size()))
						{
							currentLights.erase(currentLights.begin() + lightToDelete);
							pipeline.resetAccumulation(true);
						}
					}
					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				ImGui::EndTabBar();
			}

			ImGui::End();
		}

		// Render Pipeline
		pipeline.render(camera, totalTime, scenes[currentSceneIndex], windowWidth, windowHeight);

		// Screenshot capture
		if (takeScreenshotRequested)
		{
			takeScreenshotRequested = false;
			std::time_t t = std::time(nullptr);
			char timeBuf[32];
			struct tm tmBuf;
#if defined(_WIN32)
			localtime_s(&tmBuf, &t);
#else
			localtime_r(&t, &tmBuf);
#endif
			std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", &tmBuf);
			std::string filename = std::string("screenshot_") + timeBuf + ".png";
			pipeline.saveScreenshot(filename);
			lastScreenshotPath = std::filesystem::absolute(filename).string();
		}

		if (isTestMode && (testFrames == 19 || testFrames == 39 || testFrames == 59 || testFrames == 79 || testFrames == 99))
		{
			std::vector<unsigned char> pixels(windowWidth * windowHeight * 4);
			glReadPixels(0, 0, windowWidth, windowHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			double sum = 0.0;
			for (size_t i = 0; i < pixels.size(); i += 4)
			{
				sum += (pixels[i] + pixels[i + 1] + pixels[i + 2]) / (3.0 * 255.0);
			}
			double avg = sum / (windowWidth * windowHeight);
			int scIdx = (testFrames == 19) ? 0 : ((testFrames == 39) ? 1 : ((testFrames == 59) ? 2 : ((testFrames == 79) ? 3 : 4)));
			std::cout << "Scene " << (scIdx + 1) << " (" << scenes[scIdx].name << ") Average Brightness: " << avg << std::endl;

			std::string ppmName = "scene" + std::to_string(scIdx + 1) + ".ppm";
			std::ofstream ofs(ppmName, std::ios::binary);
			if (ofs)
			{
				ofs << "P6\n" << windowWidth << " " << windowHeight << "\n255\n";
				for (int y = windowHeight - 1; y >= 0; --y)
				{
					for (int x = 0; x < windowWidth; ++x)
					{
						size_t idx = (y * windowWidth + x) * 4;
						ofs.put(pixels[idx]);
						ofs.put(pixels[idx + 1]);
						ofs.put(pixels[idx + 2]);
					}
				}
			}
		}

		// Render ImGui
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		SDL_GL_SwapWindow(window);

		if (enableFpsLimit && targetFps > 0)
		{
			glFinish();
			auto workEndTime = std::chrono::high_resolution_clock::now();
			double targetSec = 1.0 / static_cast<double>(targetFps);

			auto now = workEndTime;
			double elapsed = std::chrono::duration<double>(now - frameStartTime).count();
			while (elapsed < targetSec)
			{
				double remaining = targetSec - elapsed;
				if (remaining > 0.002)
				{
					SDL_Delay(static_cast<uint32_t>((remaining - 0.0015) * 1000.0));
				}
				else
				{
					std::this_thread::yield();
				}
				now = std::chrono::high_resolution_clock::now();
				elapsed = std::chrono::duration<double>(now - frameStartTime).count();
			}

			auto frameEndTime = now;
			double totalSec = std::chrono::duration<double>(frameEndTime - frameStartTime).count();
			double idleSec = std::chrono::duration<double>(frameEndTime - workEndTime).count();
			if (idleSec < 0.0) idleSec = 0.0;
			double workSec = totalSec - idleSec;
			if (workSec < 0.0) workSec = 0.0;

			float workMs = static_cast<float>(workSec * 1000.0);
			float idleMs = static_cast<float>(idleSec * 1000.0);
			smoothedWorkMs = (smoothedWorkMs <= 0.0f) ? workMs : (smoothedWorkMs * 0.90f + workMs * 0.10f);
			smoothedIdleMs = (smoothedIdleMs <= 0.0f) ? idleMs : (smoothedIdleMs * 0.90f + idleMs * 0.10f);
		}
		else
		{
			auto frameEndTime = std::chrono::high_resolution_clock::now();
			double totalSec = std::chrono::duration<double>(frameEndTime - frameStartTime).count();
			float totalMs = static_cast<float>(totalSec * 1000.0);
			smoothedWorkMs = (smoothedWorkMs <= 0.0f) ? totalMs : (smoothedWorkMs * 0.90f + totalMs * 0.10f);
			smoothedIdleMs = 0.0f;
		}
	}

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	SDL_GL_DestroyContext(glContext);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
