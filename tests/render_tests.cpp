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
    std::cout << "PASS: temporal accumulation, reset, disocclusion, and behind-camera rejection\n";
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

void testPipeline()
{
    renderer::PathTracerPipeline::Config config;
    config.renderWidth = width;
    config.renderHeight = height;
    config.enableBlueNoise = false;
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

    // Compare the second temporal result with the expected blend of the first spatial result.
    pipeline.getConfig().atrousIterations = 1;
    pipeline.resetAccumulation(true);
    auto filteredFirst = capture(pipeline, camera, scene, 1.0);
    pipeline.getConfig().debugView = 1;
    auto rawSecond = capture(pipeline, camera, scene, 2.0);
    pipeline.getConfig().debugView = 0;
    pipeline.resetAccumulation(true);
    capture(pipeline, camera, scene, 1.0);
    pipeline.getConfig().atrousIterations = 0;
    auto temporalSecond = capture(pipeline, camera, scene, 2.0);
    double totalError = 0.0;
    int compared = 0;
    for (size_t i = 0; i < temporalSecond.size(); ++i)
    {
        // Exclude alpha and colors clipped by the resolve pass.
        if (i % 4 == 3 || filteredFirst[i] < 10 || filteredFirst[i] > 245 ||
            rawSecond[i] < 10 || rawSecond[i] > 245) continue;
        double firstLinear = std::pow(filteredFirst[i] / 255.0, 1.0 / 0.4545);
        double secondLinear = std::pow(rawSecond[i] / 255.0, 1.0 / 0.4545);
        double expected = std::pow(0.5 * (firstLinear + secondLinear), 0.4545);
        totalError += std::abs(temporalSecond[i] / 255.0 - expected);
        ++compared;
    }
    require(compared > 100, "Too few pixels could verify spatial feedback.");
    require(totalError / compared < 2.0 / 255.0, "The first spatial result did not enter temporal history.");
    std::cout << "PASS: first-iteration feedback; mean error = " << totalError / compared << '\n';

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
