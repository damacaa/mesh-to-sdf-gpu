#include <glad/glad.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "renderer/PathTracerPipeline.h"

namespace
{
constexpr int width = 96;
constexpr int height = 64;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void checkGl()
{
    require(glGetError() == GL_NO_ERROR, "OpenGL reported an error.");
}

void fill(renderer::Texture& texture, glm::vec4 value)
{
    glClearTexImage(texture.id, 0, GL_RGBA, GL_FLOAT, &value.x);
}

glm::vec4 readPixel(int attachment = 0)
{
    glm::vec4 pixel;
    glReadBuffer(GL_COLOR_ATTACHMENT0 + attachment);
    glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_FLOAT, &pixel.x);
    checkGl();
    return pixel;
}

void testTemporal()
{
    renderer::Shader shader(std::string(SHADERS_PATH) + "/common/screen_plane.vert",
        std::string(SHADERS_PATH) + "/denoise/svgf_temporal.frag");
    require(shader.id != 0, "The temporal shader did not compile.");
    renderer::FullscreenQuad quad;
    renderer::Framebuffer target;
    std::array<renderer::Texture, 4> outputs;
    for (int i = 0; i < 4; ++i)
    {
        outputs[i] = renderer::Texture(width, height, renderer::Texture::TextureType::AccumulationData);
        target.attachColorTexture(outputs[i], i);
    }
    std::array<renderer::Texture, 7> inputs;
    for (auto& texture : inputs)
        texture = renderer::Texture(width, height, renderer::Texture::TextureType::AccumulationData);

    fill(inputs[0], glm::vec4(1.0f));
    fill(inputs[1], glm::vec4(0, 0, 1, 1));
    fill(inputs[2], glm::vec4(0, 0, 0, 1));
    fill(inputs[3], glm::vec4(4, 4, 4, 0));
    fill(inputs[4], glm::vec4(4, 16, 32, 1));
    fill(inputs[5], glm::vec4(0, 0, 1, 1));
    fill(inputs[6], glm::vec4(0, 0, 0, 1));
    const char* names[] = {"t_currIllum", "t_currNormal", "t_currWorldPos",
        "t_prevIllum", "t_prevMoments", "t_prevNormal", "t_prevWorldPos"};
    shader.use();
    for (int i = 0; i < 7; ++i)
    {
        shader.setUniform(names[i], i);
        inputs[i].bind(i);
    }
    shader.setUniform("u_resolution", glm::vec2(width, height));
    shader.setUniform("u_prevViewProj", glm::mat4(1.0f));
    shader.setUniform("u_alpha", 0.15f);
    shader.setUniform("u_maxHistory", 32.0f);
    shader.setUniform("u_cameraMoved", 0);
    shader.setUniform("u_resetHistory", 0);
    target.bind();
    const GLenum buffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
    glDrawBuffers(4, buffers);
    glDisable(GL_DEPTH_TEST);
    quad.draw();
    require(std::abs(readPixel().x - 3.55f) < 0.001f, "Valid history did not accumulate.");
    require(readPixel(1).z == 32.0f, "Valid history did not retain its length.");

    shader.setUniform("u_resetHistory", 1);
    fill(inputs[3], glm::vec4(std::numeric_limits<float>::quiet_NaN()));
    quad.draw();
    require(readPixel().x == 1.0f, "Reset history leaked into the current color.");
    require(readPixel(1).z == 1.0f, "Reset did not restart the history length.");

    shader.setUniform("u_resetHistory", 0);
    fill(inputs[3], glm::vec4(4, 4, 4, 0));
    fill(inputs[6], glm::vec4(0, 0, 1, 1));
    quad.draw();
    require(readPixel().x == 1.0f, "A depth discontinuity did not reject history.");

    fill(inputs[6], glm::vec4(0, 0, 0, 1));
    shader.setUniform("u_cameraMoved", 1);
    glm::mat4 behindCamera(1.0f);
    behindCamera[3][3] = -1.0f;
    shader.setUniform("u_prevViewProj", behindCamera);
    quad.draw();
    require(readPixel(1).z == 1.0f, "A point behind the previous camera retained history.");

    // A fresh pixel must use its neighbors when the history fix is on.
    shader.setUniform("u_resetHistory", 1);
    fill(inputs[0], glm::vec4(1.0f));
    const glm::vec4 bright(10.0f, 10.0f, 10.0f, 1.0f);
    inputs[0].bind(0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, width / 2, height / 2, 1, 1, GL_RGBA, GL_FLOAT, &bright.x);
    shader.setUniform("u_historyFixFrames", 0.0f);
    quad.draw();
    require(readPixel().x == 10.0f, "A disabled history fix changed a fresh pixel.");
    shader.setUniform("u_historyFixFrames", 4.0f);
    quad.draw();
    float fixedValue = readPixel().x;
    require(fixedValue > 1.0f && fixedValue < 2.0f, "The history fix did not average a fresh pixel.");
    std::cout << "PASS: temporal accumulation, reset, disocclusion, behind-camera rejection, and history fix\n";
}

void testVarianceFilter()
{
    renderer::Shader shader(std::string(SHADERS_PATH) + "/common/screen_plane.vert",
        std::string(SHADERS_PATH) + "/denoise/svgf_atrous.frag");
    require(shader.id != 0, "The spatial shader did not compile.");
    renderer::FullscreenQuad quad;
    renderer::Texture illumination(width, height, renderer::Texture::TextureType::AccumulationData);
    renderer::Texture normal(width, height, renderer::Texture::TextureType::AccumulationData);
    renderer::Texture position(width, height, renderer::Texture::TextureType::AccumulationData);
    renderer::Texture output(width, height, renderer::Texture::TextureType::AccumulationData);
    renderer::Framebuffer target;
    target.attachColorTexture(output, 0);
    fill(illumination, glm::vec4(2, 2, 2, 1));
    fill(normal, glm::vec4(0, 0, 1, 1));
    fill(position, glm::vec4(0, 0, 0, 1));
    shader.use();
    shader.setUniform("t_illumination", 0);
    shader.setUniform("t_normal", 1);
    shader.setUniform("t_worldPos", 2);
    shader.setUniform("u_resolution", glm::vec2(width, height));
    shader.setUniform("u_stepSize", 1);
    shader.setUniform("u_phiNormal", 32.0f);
    shader.setUniform("u_phiDepth", 0.1f);
    shader.setUniform("u_phiLuminance", 4.0f);
    illumination.bind(0);
    normal.bind(1);
    position.bind(2);
    target.bind();
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_DEPTH_TEST);
    quad.draw();
    glm::vec4 result = readPixel();
    require(std::abs(result.x - 2.0f) < 0.001f, "The filter changed constant illumination.");
    require(result.w > 0.0f && result.w < 1.0f, "The filter did not reduce independent variance.");

    // A dark pixel with zero variance must still use the variance of its neighbors.
    illumination.bind(0);
    const glm::vec4 dark(0.0f);
    glTexSubImage2D(GL_TEXTURE_2D, 0, width / 2, height / 2, 1, 1, GL_RGBA, GL_FLOAT, &dark.x);
    quad.draw();
    require(readPixel().x > 0.1f, "The variance prefilter did not admit neighboring illumination.");
    std::cout << "PASS: variance prefilter and constant-signal preservation\n";
}

std::vector<unsigned char> capture(renderer::PathTracerPipeline& pipeline,
    renderer::Camera& camera, const renderer::SceneDefinition& scene, double time)
{
    pipeline.render(camera, time, scene);
    std::vector<unsigned char> pixels(width * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    checkGl();
    return pixels;
}

void testGlobalGeometrySelection()
{
    renderer::PathTracerPipeline::Config config;
    config.renderWidth = width;
    config.renderHeight = height;
    config.geometryPreview = true;
    config.pauseDynamicObject = true;
    renderer::PathTracerPipeline pipeline(config, SHADERS_PATH, ASSETS_PATH);
    const auto scenes = renderer::getPredefinedScenes();

    // Compare the visible models in Sponza and in the pedestal scene.
    pipeline.getConfig().showTexturedMeshes = true;
    for (int index : {5, 2})
    {
        const auto& scene = scenes[index];
        pipeline.setScene(scene, index);
        require(pipeline.getConfig().showTexturedMeshes, "Changing scenes reset the global geometry selection.");
        renderer::Camera camera(scene.cameraPosition);
        camera.yaw = scene.cameraYaw;
        camera.pitch = scene.cameraPitch;
        camera.updateVectors();
        camera.updateMatrix(width, height);
        const auto mesh = capture(pipeline, camera, scene, 0.0);
        pipeline.getConfig().showTexturedMeshes = false;
        const auto field = capture(pipeline, camera, scene, 0.0);
        require(mesh != field, "Both geometry modes produced the same mesh image.");
        pipeline.getConfig().showTexturedMeshes = true;
        require(mesh == capture(pipeline, camera, scene, 0.0), "The original mesh view did not return.");
    }
}

void testMovingDynamicMeshShadow()
{
    using Pipeline = renderer::PathTracerPipeline;
    Pipeline::Config config;
    config.renderWidth = width;
    config.renderHeight = height;
    config.enableFxaa = false;
    auto scenes = renderer::getPredefinedScenes();
    const auto& scene = scenes.back();
    Pipeline pipeline(config, SHADERS_PATH, ASSETS_PATH);
    pipeline.setScene(scene, static_cast<int>(scenes.size()) - 1);
    renderer::Camera camera(scene.cameraPosition);
    auto aim = [&](float yaw, float pitch) {
        camera.yaw = yaw;
        camera.pitch = pitch;
        camera.updateVectors();
        camera.updateMatrix(width, height);
    };
    aim(0.0f, -3.0f);
    pipeline.getConfig().atrousIterations = 0;
    pipeline.render(camera, 0.0, scene);
    using Stage = Pipeline::LightingStage;
    const auto first = pipeline.readLighting(Stage::Raw);
    const auto fresh = pipeline.readLighting(Stage::Temporal);
    for (size_t i = 0; i < first.size(); ++i)
        require(glm::vec3(first[i]) == glm::vec3(fresh[i]), "Zero iterations blurred a fresh Sponza frame.");
    pipeline.render(camera, 0.016, scene);
    const auto second = pipeline.readLighting(Stage::Raw);
    const auto integrated = pipeline.readLighting(Stage::Temporal);
    size_t blended = 0;
    for (size_t i = 0; i < first.size(); ++i)
    {
        float mean = 0.5f * (first[i].r + second[i].r);
        if (std::abs(first[i].r - second[i].r) > 0.02f && std::abs(integrated[i].r - mean) < 0.005f)
            ++blended;
    }
    require(blended > first.size() / 10, "The dynamic mesh's motion discarded history on the static architecture.");
    pipeline.restartDynamicExperiment();
    pipeline.render(camera, 0.0, scene);
    camera.position = glm::vec3(6.0f, 2.0f, 0.0f);
    aim(180.0f, 0.0f);
    pipeline.render(camera, 0.0, scene);

    // Keep the floor visible while the observed mesh moves outside the shot.
    camera.position = glm::vec3(-2.5f, 2.3f, 0.0f);
    aim(0.0f, -44.0f);
    auto floorLighting = [&]() {
        pipeline.restartSampling(0);
        pipeline.render(camera, 0.45, scene);
        return pipeline.readLighting(Pipeline::LightingStage::Raw);
    };
    auto retained = floorLighting();
    pipeline.getConfig().useDynamicReference = true;
    auto reference = floorLighting();
    pipeline.clearDynamicCapture();
    require(reference == floorLighting(), "Clearing camera observations changed the complete mesh reference.");
    pipeline.getConfig().useDynamicReference = false;
    auto empty = floorLighting();
    auto shadowedPixels = [&](const std::vector<glm::vec4>& lighting) {
        size_t count = 0;
        for (size_t i = 0; i < empty.size(); ++i)
            if (empty[i].r - lighting[i].r > 0.02f) ++count;
        return count;
    };
    require(shadowedPixels(retained) > empty.size() / 100, "The observed mesh lost its shadow outside the camera view.");
    require(shadowedPixels(reference) > empty.size() / 100, "The complete mesh did not cast a shadow.");

    // Resize the grid and change voxel resolution at runtime.
    pipeline.getConfig().dynamicResolution = 32;
    pipeline.getConfig().dynamicGridSize = 3.2f;
    pipeline.getConfig().useDynamicReference = true;
    auto resizedReference = floorLighting();
    require(shadowedPixels(resizedReference) > empty.size() / 100, "Resized reference grid did not cast a shadow.");

    pipeline.getConfig().useDynamicReference = false;
    camera.position = scene.cameraPosition;
    aim(0.0f, -3.0f);
    pipeline.render(camera, 0.0, scene);
    camera.position = glm::vec3(-2.5f, 2.3f, 0.0f);
    aim(0.0f, -44.0f);
    auto resizedRetained = floorLighting();
    require(shadowedPixels(resizedRetained) > empty.size() / 100, "Resized live dynamic grid did not cast a shadow.");

    auto customScene = scene;
    customScene.dynamicResolution = 64;
    customScene.dynamicGridSize = 3.5f;
    pipeline.setScene(customScene, static_cast<int>(scenes.size()) - 1);
    require(pipeline.getConfig().dynamicResolution == 64 && pipeline.getConfig().dynamicGridSize == 3.5f,
        "Scene overrides for dynamic resolution and size did not apply.");
    pipeline.setScene(scene, static_cast<int>(scenes.size()) - 1);
    require(pipeline.getConfig().dynamicResolution == 48 && pipeline.getConfig().dynamicGridSize == 4.0f,
        "Default dynamic resolution and size did not restore on scene switch.");

    // Verify the moving mesh rotates 180 degrees between +X and -X travel at the same position (x = 1.5).
    auto dynOnly = scene;
    dynOnly.sdfModelPath.clear();
    dynOnly.enableSdf3DTexture = false;
    pipeline.setScene(dynOnly, static_cast<int>(scenes.size()) - 1);
    pipeline.restartDynamicExperiment();
    camera.position = glm::vec3(-1.5f, 2.0f, 0.0f);
    aim(0.0f, 0.0f);
    pipeline.getConfig().showTexturedMeshes = true;
    pipeline.render(camera, 0.0, dynOnly);
    const auto meshGoingAway = pipeline.readLighting(Pipeline::LightingStage::Raw);
    const double halfCycle = 2.0 * 3.14159265358979323846;
    pipeline.render(camera, halfCycle, dynOnly);
    const auto meshComingBack = pipeline.readLighting(Pipeline::LightingStage::Raw);
    const auto meshBackMask = pipeline.readSurfaceMask();
    int depthDiffCount = 0;
    for (size_t i = 0; i < meshGoingAway.size(); ++i)
        if (std::abs(meshGoingAway[i].w - meshComingBack[i].w) > 0.05f) ++depthDiffCount;
    require(depthDiffCount > 50, "The moving mesh did not rotate when reversing direction.");

    // Verify the rotated local SDF matches the rotated mesh after accumulating across the 180-degree turn.
    pipeline.getConfig().showTexturedMeshes = false;
    pipeline.render(camera, halfCycle, dynOnly);
    const auto fieldComingBack = pipeline.readLighting(Pipeline::LightingStage::Raw);
    const auto fieldBackMask = pipeline.readSurfaceMask();
    const float voxel = dynOnly.dynamicGridSize / dynOnly.dynamicResolution;
    int backVis = 0, backHit = 0, backAcc = 0;
    for (size_t i = 0; i < meshComingBack.size(); ++i)
    {
        if (!meshBackMask[i]) continue;
        ++backVis;
        if (fieldBackMask[i])
        {
            ++backHit;
            if (std::abs(meshComingBack[i].w - fieldComingBack[i].w) < 0.25f * voxel) ++backAcc;
        }
    }
    require(backVis > 200 && backHit > 0.98f * backVis && backAcc > 0.85f * backVis,
        "The rotated dynamic SDF did not align with the rotated mesh.");

    checkGl();
    std::cout << "PASS: moving dynamic mesh retains its shadow outside the camera view\n";
}

void testSubvoxelDepth()
{
    using Pipeline = renderer::PathTracerPipeline;
    Pipeline::Config config;
    config.renderWidth = width;
    config.renderHeight = height;
    config.enableAntialiasing = false;
    config.rayBounces = 1;
    config.dynamicObject = Pipeline::DynamicObject::StretchingBox;
    config.fixedBoxPose = true;
    Pipeline pipeline(config, SHADERS_PATH, ASSETS_PATH);
    const auto scenes = renderer::getPredefinedScenes();

    auto checkDepth = [&](const renderer::SceneDefinition& scene, float voxel) {
        renderer::Camera camera(scene.cameraPosition);
        camera.yaw = scene.cameraYaw;
        camera.pitch = scene.cameraPitch;
        camera.updateVectors();
        camera.updateMatrix(width, height);
        pipeline.getConfig().showTexturedMeshes = true;
        pipeline.render(camera, 0, scene);
        const auto mesh = pipeline.readLighting(Pipeline::LightingStage::Raw);
        const auto meshMask = pipeline.readSurfaceMask();
        pipeline.getConfig().showTexturedMeshes = false;
        pipeline.render(camera, 0, scene);
        const auto field = pipeline.readLighting(Pipeline::LightingStage::Raw);
        const auto fieldMask = pipeline.readSurfaceMask();
        int visible = 0;
        int hitBoth = 0;
        int accurate = 0;
        for (size_t i = 0; i < mesh.size(); ++i)
        {
            if (!meshMask[i]) continue;
            ++visible;
            if (fieldMask[i])
            {
                ++hitBoth;
                if (std::abs(mesh[i].w - field[i].w) < 0.25f * voxel) ++accurate;
            }
        }
        require(visible > 200, "Too few mesh pixels are visible for the depth check.");
        require(hitBoth > 0.98f * visible, "The generated surface has holes compared to the rasterized mesh.");
        require(accurate > 0.85f * visible, "The generated surface lost its position within the voxel.");
    };

    auto monkey = scenes[3];
    monkey.sdfTextureResolution = 48;
    for (auto method : {renderer::SdfMethod::JfaVoxelShell, renderer::SdfMethod::JfaGBufferSplat,
                        renderer::SdfMethod::JfaCameraGBuffer})
    {
        monkey.sdfMethod = method;
        pipeline.setScene(monkey, 3);
        checkDepth(monkey, monkey.sdfTextureWorldSize / monkey.sdfTextureResolution);
    }

    auto box = scenes.back();
    box.sdfModelPath.clear();
    box.enableSdf3DTexture = false;
    pipeline.setScene(box, static_cast<int>(scenes.size()) - 1);
    for (float thicknessMm : {0.1f, 40.0f})
    {
        pipeline.getConfig().boxThicknessMm = thicknessMm;
        checkDepth(box, box.dynamicGridSize / box.dynamicResolution);
    }
    checkGl();
    std::cout << "PASS: continuous surface depth for static JFA and live camera captures\n";
}

void testPipeline()
{
    renderer::PathTracerPipeline::Config config;
    config.renderWidth = width;
    config.renderHeight = height;
    config.enableBlueNoise = false;
    config.enableFxaa = false;
    config.collectGpuTimings = true;
    config.historyFixFrames = 0; // Fresh temporal lighting must equal raw lighting.
    auto scenes = renderer::getPredefinedScenes();
    auto scene = scenes.front();
    renderer::Camera camera(scene.cameraPosition);
    camera.yaw = scene.cameraYaw;
    camera.pitch = scene.cameraPitch;
    camera.updateVectors();
    camera.updateMatrix(width, height);
    renderer::PathTracerPipeline pipeline(config, SHADERS_PATH, ASSETS_PATH);
    pipeline.setScene(scene, 0);
    capture(pipeline, camera, scene, 0.0);
    glFinish();
    capture(pipeline, camera, scene, 0.0);
    const auto buildFrame = pipeline.getFrameGpuTimings();
    require(buildFrame && buildFrame->sdfBuildMs > 0.0f, "The first frame must report the SDF build.");
    require(buildFrame->pathTraceMs > 0.0f && buildFrame->pathTraceMs <= buildFrame->renderMs,
        "The path tracing time must be part of the render time.");
    glFinish();
    capture(pipeline, camera, scene, 0.0);
    const auto cachedFrame = pipeline.getFrameGpuTimings();
    require(cachedFrame && cachedFrame->sdfBuildMs == 0.0f && cachedFrame->renderMs > 0.0f,
        "A cached SDF must have zero build time.");
    std::cout << "PASS: GPU frame timings for SDF builds and cached frames\n";

    pipeline.getConfig().debugView = 4;
    const auto litNormals = capture(pipeline, camera, scene, 0.0);
    const auto litMask = pipeline.readSurfaceMask();
    pipeline.getConfig().geometryPreview = true;
    pipeline.getConfig().enableDenoising = false;
    const auto previewNormals = capture(pipeline, camera, scene, 0.0);
    require(previewNormals == litNormals && pipeline.readSurfaceMask() == litMask,
        "The geometry preview changed the primary surface hits or normals.");
    pipeline.getConfig().geometryPreview = false;
    pipeline.getConfig().enableDenoising = true;
    pipeline.getConfig().debugView = 0;
    std::cout << "PASS: geometry preview preserves primary surface hits and normals\n";

    for (bool blue : {false, true})
    {
        pipeline.getConfig().enableBlueNoise = blue;
        pipeline.getConfig().atrousIterations = 0;
        pipeline.restartSampling(17);
        capture(pipeline, camera, scene, 0.0);
        auto raw = pipeline.readLighting(renderer::PathTracerPipeline::LightingStage::Raw);
        auto temporal = pipeline.readLighting(renderer::PathTracerPipeline::LightingStage::Temporal);
        auto filtered = pipeline.readLighting(renderer::PathTracerPipeline::LightingStage::Filtered);
        for (size_t i = 0; i < raw.size(); ++i)
        {
            require(glm::vec3(raw[i]) == glm::vec3(temporal[i]), "Fresh temporal lighting differs from raw lighting.");
            require(filtered[i] == temporal[i], "Zero spatial iterations returned another texture.");
        }
        capture(pipeline, camera, scene, 0.0);
        require(raw != pipeline.readLighting(renderer::PathTracerPipeline::LightingStage::Raw), "The fixed-time stream did not advance.");
        pipeline.restartSampling(17);
        capture(pipeline, camera, scene, 0.0);
        require(raw == pipeline.readLighting(renderer::PathTracerPipeline::LightingStage::Raw), "Sampling restart is not repeatable.");
        pipeline.restartSampling(53);
        capture(pipeline, camera, scene, 0.0);
        require(raw != pipeline.readLighting(renderer::PathTracerPipeline::LightingStage::Raw), "Changing the seed did not change the stream.");
        require(pipeline.readSurfaceMask().size() == raw.size(), "The surface mask dimensions differ.");
    }
    std::cout << "PASS: linear readback and deterministic white/blue streams\n";
    pipeline.restartSampling(0);
    pipeline.getConfig().enableBlueNoise = false;
    for (int iterations : {1, 4})
    {
        pipeline.getConfig().atrousIterations = iterations;
        pipeline.restartSampling(0);
        auto screen = capture(pipeline, camera, scene, 0.0);
        auto linear = pipeline.readLighting(renderer::PathTracerPipeline::LightingStage::Filtered);
        for (size_t i = 0; i < linear.size(); ++i)
            for (int channel = 0; channel < 3; ++channel)
            {
                double contrasted = 0.5 + (linear[i][channel] - 0.5) * pipeline.getConfig().contrast;
                double expected = 255.0 * std::pow(std::clamp(contrasted, 0.0, 1.0), 0.4545);
                require(std::abs(screen[i * 4 + channel] - expected) < 2.0,
                    "Linear readback does not match the texture used by the resolve pass.");
            }
    }
    for (int iterations : {0, 1, 4})
    {
        pipeline.getConfig().atrousIterations = iterations;
        pipeline.resetAccumulation(true);
        auto initial = capture(pipeline, camera, scene, 1.0);
        auto warm = initial;
        for (int frame = 0; frame < 12; ++frame)
            warm = capture(pipeline, camera, scene, 2.0 + frame);
        require(warm != initial, "The scene did not exercise temporal sampling.");
        pipeline.resetAccumulation(true);
        auto reset = capture(pipeline, camera, scene, 1.0);
        require(initial == reset, "Pipeline reset differs from a fresh temporal history.");
    }

    // The spatial filter must not feed back into the temporal history.
    auto temporalAfterTwoFrames = [&](int iterations)
    {
        pipeline.getConfig().atrousIterations = iterations;
        pipeline.restartSampling(0);
        capture(pipeline, camera, scene, 1.0);
        capture(pipeline, camera, scene, 2.0);
        return pipeline.readLighting(renderer::PathTracerPipeline::LightingStage::Temporal);
    };
    require(temporalAfterTwoFrames(1) == temporalAfterTwoFrames(0),
        "The spatial result entered the temporal history.");
    std::cout << "PASS: unfiltered temporal feedback\n";

    pipeline.getConfig().enableBlueNoise = true;
    pipeline.getConfig().debugView = 1;
    pipeline.resetAccumulation(true);
    auto first = capture(pipeline, camera, scene, 1.0);
    auto second = capture(pipeline, camera, scene, 1.0);
    require(first != second, "STBN did not advance between frames.");
    for (int frame = 2; frame < 64; ++frame) capture(pipeline, camera, scene, 1.0);
    require(first == capture(pipeline, camera, scene, 1.0), "STBN did not wrap at 64 frames.");

    pipeline.getConfig().enableBlueNoise = false;
    pipeline.getConfig().debugView = 0;
    pipeline.resetAccumulation(true);
    auto switched = capture(pipeline, camera, scene, 1.0);
    pipeline.resetAccumulation(true);
    require(switched == capture(pipeline, camera, scene, 1.0), "A noise-mode switch retained old history.");

    // Exercise camera reprojection and resource recreation with a live GL context.
    camera.position.x += 0.1f;
    camera.updateMatrix(width, height);
    capture(pipeline, camera, scene, 2.0);
    pipeline.resize(width, height);
    capture(pipeline, camera, scene, 3.0);
    std::cout << "PASS: pipeline reset with 0/1/4 iterations, STBN animation, mode switch, motion, and resize\n";
}

void testStaticAccumulationConvergence()
{
    renderer::PathTracerPipeline::Config config;
    config.renderWidth = width;
    config.renderHeight = height;
    config.enableDenoising = false;
    config.maxAccumulationFrames = 100;
    config.enableFxaa = false;

    auto scenes = renderer::getPredefinedScenes();
    auto scene = scenes.front();
    renderer::Camera camera(scene.cameraPosition);
    camera.yaw = scene.cameraYaw;
    camera.pitch = scene.cameraPitch;
    camera.updateVectors();
    camera.updateMatrix(width, height);

    renderer::PathTracerPipeline pipeline(config, SHADERS_PATH, ASSETS_PATH);
    pipeline.setScene(scene, 0);

    for (bool blue : {false, true})
    {
        pipeline.getConfig().enableBlueNoise = blue;
        pipeline.resetAccumulation(true);

        auto frame0 = capture(pipeline, camera, scene, 0.0);
        for (int f = 1; f < 64; ++f)
        {
            capture(pipeline, camera, scene, 0.0);
        }
        auto frame63 = capture(pipeline, camera, scene, 0.0);
        require(frame63 != frame0, "Static accumulation did not advance across the first 64 frames.");

        auto frame64 = capture(pipeline, camera, scene, 0.0);
        require(frame64 != frame63, "Static accumulation froze at frame 64.");
        for (int f = 65; f < 80; ++f)
        {
            capture(pipeline, camera, scene, 0.0);
        }
        auto frame80 = capture(pipeline, camera, scene, 0.0);
        require(frame80 != frame64, "Static accumulation froze past frame 64.");

        pipeline.resetAccumulation(true);
        auto resetFrame0 = capture(pipeline, camera, scene, 0.0);
        require(resetFrame0 == frame0, "Static accumulation reset is not repeatable.");
    }
    std::cout << "PASS: static accumulation convergence and continuous sample generation past 64 frames\n";
}

std::vector<float> readDistanceVoxels(const renderer::Texture3D& tex, int resolution)
{
    require(tex.id != 0, "Generated 3D SDF texture ID is invalid.");
    const int w = tex.getWidth();
    const int h = tex.getHeight();
    const int d = tex.getDepth();
    require(w == resolution && h == resolution && d == resolution, "The distance volume has the wrong dimensions.");

    const size_t voxelCount = static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(d);
    std::vector<float> voxels(voxelCount, 0.0f);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_3D, tex.id);
    glGetTexImage(GL_TEXTURE_3D, 0, GL_RED, GL_FLOAT, voxels.data());
    glBindTexture(GL_TEXTURE_3D, 0);
    checkGl();

    for (float v : voxels)
    {
        require(std::isfinite(v), "Generated 3D SDF volume contains non-finite values.");
    }
    return voxels;
}

void validateSdfVolumeVoxels(const renderer::Texture3D& tex)
{
    const auto voxels = readDistanceVoxels(tex, 32);
    const auto [minDist, maxDist] = std::minmax_element(voxels.begin(), voxels.end());
    require(*minDist < 0.0f && *maxDist > 0.0f, "Generated 3D SDF volume lacks interior or exterior signs.");
}

void testMeshToSdfGenerator()
{
    renderer::Model model;
    const std::string modelPath = std::string(ASSETS_PATH) + "/monkey/demo.gltf";
    require(model.loadGLTF(modelPath), "Failed to load demo.gltf mesh for SDF test.");
    require(model.isValid() && model.getTriangleCount() > 0, "Loaded glTF mesh has no valid triangles.");

    renderer::SdfGenerator generator(SHADERS_PATH);

    renderer::SdfGenerationParams baseParams;
    baseParams.method = renderer::SdfMethod::ExactBaseline;
    baseParams.jfaVariant = renderer::JfaVariant::Standard;
    baseParams.format = renderer::SdfFormat::R32F;
    baseParams.resolution = 32;
    baseParams.worldSize = 2.0f;
    baseParams.worldOffset = glm::vec3(0.0f);
    baseParams.modelTransform = model.computeFitTransform(2.0f, glm::vec3(0.0f), 0.15f);

    renderer::Texture3D refTex;
    renderer::SdfStats refStats = generator.generate(model, baseParams, refTex);
    require(refStats.triangleCount == static_cast<int>(model.getTriangleCount()),
        "Triangle count mismatch in ExactBaseline stats.");
    require(refStats.staticVramMb > 0.0f && refStats.totalGpuMs >= 0.0f,
        "Invalid GPU metrics returned by ExactBaseline generator.");
    validateSdfVolumeVoxels(refTex);

    // Check GL_R16F quantization against GL_R32F reference.
    {
        renderer::SdfGenerationParams r16Params = baseParams;
        r16Params.format = renderer::SdfFormat::R16F;
        renderer::Texture3D r16Tex;
        renderer::SdfStats stats = generator.generate(model, r16Params, r16Tex);
        validateSdfVolumeVoxels(r16Tex);
        generator.computeErrorMetrics(r16Tex, refTex, stats);
        require(stats.hasReferenceComparison, "R16F reference comparison failed.");
        require(stats.mae < 0.01f, "ExactBaseline R16F MAE exceeded threshold.");
        require(stats.signMatchPercent > 99.5f, "ExactBaseline R16F sign agreement fell below 99.5%.");
    }

    // Check JfaVoxelShell across all JFA variants.
    const std::array<renderer::JfaVariant, 3> variants = {
        renderer::JfaVariant::Standard,
        renderer::JfaVariant::JfaPlus1,
        renderer::JfaVariant::OnePlusJfa
    };
    for (renderer::JfaVariant variant : variants)
    {
        renderer::SdfGenerationParams jfaParams = baseParams;
        jfaParams.method = renderer::SdfMethod::JfaVoxelShell;
        jfaParams.jfaVariant = variant;
        renderer::Texture3D jfaTex;
        renderer::SdfStats stats = generator.generate(model, jfaParams, jfaTex);
        validateSdfVolumeVoxels(jfaTex);
        generator.computeErrorMetrics(jfaTex, refTex, stats);
        require(stats.hasReferenceComparison, "JfaVoxelShell reference comparison failed.");
        require(stats.mae < 0.01f && stats.rmse < 0.02f, "JfaVoxelShell error exceeded threshold.");
        require(stats.signMatchPercent > 99.0f, "JfaVoxelShell sign agreement fell below 99%.");
    }

    // Check JfaGBufferSplat with JfaPlus1.
    {
        renderer::SdfGenerationParams gbufParams = baseParams;
        gbufParams.method = renderer::SdfMethod::JfaGBufferSplat;
        gbufParams.jfaVariant = renderer::JfaVariant::JfaPlus1;
        renderer::Texture3D gbufTex;
        renderer::SdfStats stats = generator.generate(model, gbufParams, gbufTex);
        validateSdfVolumeVoxels(gbufTex);
        generator.computeErrorMetrics(gbufTex, refTex, stats);
        require(stats.hasReferenceComparison, "JfaGBufferSplat reference comparison failed.");
        require(stats.mae < 0.15f && stats.rmse < 0.25f, "JfaGBufferSplat error exceeded threshold.");
        require(stats.signMatchPercent > 85.0f, "JfaGBufferSplat sign agreement fell below 85%.");
    }

    // Compare the distances in the Sponza seed band to the direct triangle distances.
    {
        renderer::Model sponzaModel;
        const std::string sponzaPath = std::string(ASSETS_PATH) + "/sponza-gltf-pbr/sponza.glb";
        require(sponzaModel.loadGLTF(sponzaPath), "Failed to load sponza.glb mesh for SDF test.");
        require(sponzaModel.isValid() && sponzaModel.getTriangleCount() > 200000,
            "Loaded Sponza GLB mesh has fewer triangles than expected.");

        renderer::SdfGenerationParams sponzaParams;
        sponzaParams.method = renderer::SdfMethod::JfaVoxelShell;
        sponzaParams.jfaVariant = renderer::JfaVariant::JfaPlus1;
        sponzaParams.format = renderer::SdfFormat::R32F;
        sponzaParams.resolution = 16;
        sponzaParams.worldSize = 30.0f;
        sponzaParams.worldOffset = glm::vec3(0.0f, 5.2f, 0.0f);
        sponzaParams.modelTransform = sponzaModel.computeFitTransform(30.0f, sponzaParams.worldOffset, 0.15f);
        sponzaParams.unsignedDistance = true;

        renderer::Texture3D sponzaTex;
        renderer::SdfStats sponzaStats = generator.generate(sponzaModel, sponzaParams, sponzaTex);
        require(sponzaStats.triangleCount == static_cast<int>(sponzaModel.getTriangleCount()),
            "Triangle count mismatch in Sponza stats.");
        const auto distances = readDistanceVoxels(sponzaTex, sponzaParams.resolution);
        sponzaParams.method = renderer::SdfMethod::ExactBaseline;
        renderer::Texture3D sponzaRefTex;
        generator.generate(sponzaModel, sponzaParams, sponzaRefTex);
        const auto reference = readDistanceVoxels(sponzaRefTex, sponzaParams.resolution);

        const float voxelSize = sponzaParams.worldSize / float(sponzaParams.resolution);
        float maxNearError = 0.0f;
        int nearCount = 0;
        for (size_t i = 0; i < distances.size(); ++i)
        {
            require(distances[i] >= 0.0f && reference[i] >= 0.0f, "The unsigned distance volume has negative values.");
            if (reference[i] < 1.74f * voxelSize)
            {
                ++nearCount;
                maxNearError = std::max(maxNearError, std::abs(distances[i] - reference[i]));
            }
        }
        require(nearCount > 0, "The Sponza reference volume has no samples in the seed band.");
        require(maxNearError < 0.002f * voxelSize, "The Sponza seed band differs from the direct triangle distances.");
        generator.computeErrorMetrics(sponzaTex, sponzaRefTex, sponzaStats);
        require(sponzaStats.mae < 0.05f * voxelSize, "The Sponza unsigned distance error exceeded the limit.");
        std::cout << "PASS: Sponza unsigned distance MAE " << sponzaStats.mae
            << ", max seed band error " << maxNearError << '\n';
    }

    // Check JfaCameraGBuffer with temporal seed accumulation across two opposite camera views.
    {
        const glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 50.0f);
        renderer::SdfGenerationParams camParams = baseParams;
        camParams.method = renderer::SdfMethod::JfaCameraGBuffer;
        camParams.jfaVariant = renderer::JfaVariant::JfaPlus1;
        camParams.cameraViewportWidth = 512;
        camParams.cameraViewportHeight = 512;
        camParams.accumulateSeeds = true;
        camParams.resetAccumulatedSeeds = true;
        camParams.cameraViewProj = proj * glm::lookAt(
            glm::vec3(0.0f, 0.0f, 3.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));

        renderer::Texture3D camTex;
        renderer::SdfStats statsFront = generator.generate(model, camParams, camTex);
        validateSdfVolumeVoxels(camTex);
        generator.computeErrorMetrics(camTex, refTex, statsFront);
        require(statsFront.hasReferenceComparison, "JfaCameraGBuffer front reference comparison failed.");

        camParams.resetAccumulatedSeeds = false;
        camParams.cameraViewProj = proj * glm::lookAt(
            glm::vec3(0.0f, 0.0f, -3.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        renderer::SdfStats statsAccum = generator.generate(model, camParams, camTex);
        validateSdfVolumeVoxels(camTex);
        generator.computeErrorMetrics(camTex, refTex, statsAccum);
        require(statsAccum.hasReferenceComparison, "JfaCameraGBuffer accumulated reference comparison failed.");
        require(statsAccum.mae < statsFront.mae,
            "Accumulating seeds from a second camera view did not reduce SDF MAE.");
    }

    std::cout << "PASS: mesh-to-SDF generator validation across Exact, JfaVoxelShell, JfaGBufferSplat, and JfaCameraGBuffer\n";
}
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow("Render regression tests", width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    int result = 0;
    try
    {
        require(context != nullptr, SDL_GetError());
        require(SDL_GL_MakeCurrent(window, context), SDL_GetError());
        require(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)) != 0,
            "OpenGL could not load.");
        std::cout << "GPU: " << glGetString(GL_RENDERER) << '\n';
        testTemporal();
        testVarianceFilter();
        testPipeline();
        testMovingDynamicMeshShadow();
        testSubvoxelDepth();
        testGlobalGeometrySelection();
        testStaticAccumulationConvergence();
        testMeshToSdfGenerator();
        checkGl();
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
