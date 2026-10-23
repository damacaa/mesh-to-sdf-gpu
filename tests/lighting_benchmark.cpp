#include "benchmark/LightingMetrics.h"
#include "renderer/PathTracerPipeline.h"
#include <SDL3/SDL.h>
#include <stb/stb_image_write.h>
#include <json.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using Pipeline = renderer::PathTracerPipeline;
using benchmark::Image;
using benchmark::Mask;

namespace
{
void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void keys(const Json& object, const std::set<std::string>& allowed)
{
    require(object.is_object(), "Expected a JSON object.");
    for (const auto& entry : object.items())
        require(allowed.contains(entry.key()), "Unknown setting: " + entry.key());
}

int integer(const Json& value, int low, int high, const std::string& name)
{
    require(value.is_number_integer(), name + " must be an integer.");
    const auto number = value.get<int64_t>();
    require(number >= low && number <= high, name + " is outside its allowed range.");
    return static_cast<int>(number);
}

double number(const Json& value, double low, double high, const std::string& name)
{
    require(value.is_number(), name + " must be numeric.");
    double result = value.get<double>();
    require(std::isfinite(result) && result >= low && result <= high, name + " is outside its allowed range.");
    return result;
}

std::ofstream outputFile(const fs::path& path, bool binary = false)
{
    std::ofstream file(path, binary ? std::ios::binary : std::ios::out);
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file << std::setprecision(12);
    return file;
}

void writeJson(const fs::path& path, const Json& value)
{
    auto file = outputFile(path);
    file << value.dump(2) << '\n';
}

std::string fingerprint(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "Cannot fingerprint: " + path.string());
    uint64_t hash = 14695981039346656037ull;
    std::array<char, 65536> bytes;
    while (file)
    {
        file.read(bytes.data(), bytes.size());
        for (std::streamsize i = 0; i < file.gcount(); ++i)
        {
            hash ^= static_cast<unsigned char>(bytes[size_t(i)]);
            hash *= 1099511628211ull;
        }
    }
    require(file.eof(), "Could not read: " + path.string());
    std::ostringstream result;
    result << std::hex << std::setw(16) << std::setfill('0') << hash;
    return result.str();
}

Json fingerprints(const fs::path& executable)
{
    Json result = Json::object();
    result[fs::absolute(executable).generic_string()] = fingerprint(executable);
    for (const fs::path root : {fs::path(SHADERS_PATH), fs::path(ASSETS_PATH)})
        for (const auto& entry : fs::recursive_directory_iterator(root))
            if (entry.is_regular_file()) result[entry.path().generic_string()] = fingerprint(entry.path());
    return result;
}

void saveImage(const fs::path& base, const Image& image, int width, int height)
{
    auto pfm = outputFile(base.string() + ".pfm", true);
    pfm << "PF\n" << width << ' ' << height << "\n-1.0\n";
    // PFM stores bottom-up RGB. The supported desktop platforms use little-endian floats.
    for (const auto& pixel : image)
    {
        const float rgb[3] = {float(pixel.r), float(pixel.g), float(pixel.b)};
        pfm.write(reinterpret_cast<const char*>(rgb), sizeof(rgb));
    }
    std::vector<unsigned char> preview(image.size() * 3);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c)
            {
                double linear = std::clamp(image[size_t(y) * width + x][c], 0.0, 1.0);
                double srgb = linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
                preview[(size_t(height - 1 - y) * width + x) * 3 + c] = static_cast<unsigned char>(std::lround(srgb * 255.0));
            }
    require(stbi_write_png((base.string() + ".png").c_str(), width, height, 3, preview.data(), width * 3) != 0,
        "Cannot write the preview image.");
}

std::string glFailure;
void APIENTRY glMessage(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message, const void*)
{
    if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH) glFailure = message;
}

void checkGl()
{
    require(glFailure.empty(), "OpenGL: " + glFailure);
    GLenum error = glGetError();
    require(error == GL_NO_ERROR, "OpenGL error: " + std::to_string(error));
}

class Context
{
public:
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    Context(int width, int height)
    {
        try
        {
            require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
            window = SDL_CreateWindow("Lighting benchmark", width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
            require(window != nullptr, SDL_GetError());
            context = SDL_GL_CreateContext(window);
            require(context != nullptr, SDL_GetError());
            require(SDL_GL_MakeCurrent(window, context), SDL_GetError());
            require(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)) != 0, "Cannot load OpenGL.");
            SDL_GL_SetSwapInterval(0);
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(glMessage, nullptr);
        }
        catch (...) { close(); throw; }
    }
    ~Context() { close(); }
private:
    void close()
    {
        if (context) SDL_GL_DestroyContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

struct Case
{
    std::string name;
    Pipeline::Config config;
    Json settings;
};

Json defaultSettings()
{
    return {{"noise", "white"}, {"temporal_alpha", 0.15}, {"max_history_frames", 32},
        {"atrous_iterations", 4}, {"phi_normal", 32.0}, {"phi_depth", 0.1},
        {"phi_luminance", 4.0}, {"demodulate_albedo", false}};
}

Case parseCase(const Json& input, const Pipeline::Config& base)
{
    keys(input, {"name", "noise", "temporal_alpha", "max_history_frames", "atrous_iterations",
        "phi_normal", "phi_depth", "phi_luminance", "demodulate_albedo"});
    Case result;
    result.name = input.at("name").get<std::string>();
    require(!result.name.empty() && result.name.size() <= 64 &&
        result.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos,
        "Case names must contain 1-64 letters, digits, underscores, or hyphens.");
    result.settings = defaultSettings();
    result.settings.update(input);
    auto& s = result.settings;
    auto& c = result.config;
    c = base;
    std::string noise = s.at("noise").get<std::string>();
    require(noise == "white" || noise == "blue", "Noise must be white or blue.");
    c.enableBlueNoise = noise == "blue";
    c.temporalAlpha = float(number(s["temporal_alpha"], 0.0, 1.0, "temporal_alpha"));
    // The current half-float history texture represents consecutive integers through 2048.
    c.maxHistoryFrames = integer(s["max_history_frames"], 1, 2048, "max_history_frames");
    c.atrousIterations = integer(s["atrous_iterations"], 0, 5, "atrous_iterations");
    c.phiNormal = float(number(s["phi_normal"], 0.01, 10000.0, "phi_normal"));
    c.phiDepth = float(number(s["phi_depth"], 0.00001, 10000.0, "phi_depth"));
    c.phiLuminance = float(number(s["phi_luminance"], 0.00001, 10000.0, "phi_luminance"));
    c.demodulateAlbedo = s.at("demodulate_albedo").get<bool>();
    return result;
}

Json errorJson(const benchmark::Error& error)
{
    return {{"rmse", error.rmse()}, {"mae", error.mae}, {"luminance_bias", error.luminanceBias},
        {"relative_rmse", error.referencePower > 0.0 ? Json(std::sqrt(error.mse / error.referencePower)) : Json(nullptr)},
        {"psnr_db_peak4", error.mse > 0.0 ? Json(10.0 * std::log10(16.0 / error.mse)) : Json(nullptr)}};
}

double percentile(std::vector<double> values, double fraction)
{
    std::sort(values.begin(), values.end());
    return values[size_t(std::ceil(fraction * values.size())) - 1];
}

void progress(const std::string& phase, int frame, int total)
{
    if (frame == 0 || (frame + 1) % 128 == 0 || frame + 1 == total)
        std::cout << phase << ": " << frame + 1 << '/' << total << std::endl;
    SDL_PumpEvents();
}

void run(Json input, const fs::path& output, const fs::path& executable)
{
    keys(input, {"width", "height", "scene", "frames", "warmup_frames", "reference_frames", "reference_seed",
        "seeds", "ray_bounces", "mask", "roi", "cases"});
    Json resolved = {{"width", 192}, {"height", 108}, {"scene", 5}, {"frames", 256},
        {"warmup_frames", 64}, {"reference_frames", 2048}, {"reference_seed", 104729},
        {"seeds", {1, 17, 53}}, {"ray_bounces", 3}, {"mask", "surfaces"}};
    resolved.update(input);
    int width = integer(resolved["width"], 8, 4096, "width");
    int height = integer(resolved["height"], 8, 4096, "height");
    int frames = integer(resolved["frames"], 2, 1000000, "frames");
    int warmup = integer(resolved["warmup_frames"], 0, 1000000, "warmup_frames");
    int refFrames = integer(resolved["reference_frames"], 2, 1000000, "reference_frames");
    require(refFrames % 2 == 0, "reference_frames must be even.");
    int refSeed = integer(resolved["reference_seed"], 0, 2147483647, "reference_seed");
    auto scenes = renderer::getPredefinedScenes();
    int sceneIndex = integer(resolved["scene"], 1, int(scenes.size()), "scene") - 1;
    auto scene = scenes[size_t(sceneIndex)];
    std::string maskName = resolved.at("mask").get<std::string>();
    require(maskName == "surfaces" || maskName == "all", "mask must be surfaces or all.");
    require(resolved["seeds"].is_array() && !resolved["seeds"].empty(), "seeds must be a nonempty array.");
    std::vector<int> seeds;
    for (const auto& seed : resolved["seeds"])
    {
        int value = integer(seed, 0, 2147483647, "seed");
        require(value != refSeed && std::find(seeds.begin(), seeds.end(), value) == seeds.end(), "Seeds must be unique and exclude reference_seed.");
        seeds.push_back(value);
    }
    std::array<int, 4> roi{0, 0, width, height};
    if (resolved.contains("roi"))
    {
        require(resolved["roi"].is_array() && resolved["roi"].size() == 4, "roi requires [x, y, width, height].");
        for (int i = 0; i < 4; ++i) roi[i] = integer(resolved["roi"][i], i < 2 ? 0 : 1, 4096, "roi");
        require(roi[0] + roi[2] <= width && roi[1] + roi[3] <= height, "roi exceeds the image.");
    }
    resolved["roi"] = roi;
    Pipeline::Config base;
    base.renderWidth = width;
    base.renderHeight = height;
    base.rayBounces = integer(resolved["ray_bounces"], 1, 16, "ray_bounces");
    base.enableAntialiasing = false;
    base.enableFxaa = false;
    base.enableBlueNoise = false;
    base.contrast = 1.0f;
    require(resolved.contains("cases") && resolved["cases"].is_array() && !resolved["cases"].empty(), "Supply a nonempty cases array.");
    std::vector<Case> cases;
    std::set<std::string> names;
    for (const auto& entry : resolved["cases"])
    {
        auto test = parseCase(entry, base);
        std::string foldedName = test.name;
        for (char& c : foldedName) if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
        require(names.insert(foldedName).second, "Case names must be unique, ignoring case.");
        cases.push_back(test);
    }
    resolved["cases"] = Json::array();
    for (const auto& test : cases) resolved["cases"].push_back(test.settings);
    require(!fs::exists(output), "The output path already exists. Choose a new path.");
    fs::create_directories(output);
    writeJson(output / "config.json", resolved);

    Context context(width, height);
    Pipeline pipeline(base, SHADERS_PATH, ASSETS_PATH);
    pipeline.setScene(scene, sceneIndex);
    for (auto& test : cases)
    {
        // setScene supplies the exact SDF configuration used by the application.
        test.config = parseCase(test.settings, pipeline.getConfig()).config;
        require(!test.config.enableBlueNoise || pipeline.isBlueNoiseAvailable(), "The STBN assets are unavailable.");
    }
    renderer::Camera camera(scene.cameraPosition);
    camera.yaw = scene.cameraYaw;
    camera.pitch = scene.cameraPitch;
    camera.updateVectors();
    camera.updateMatrix(width, height);
    Json hashes = fingerprints(executable);
    Json manifest = {{"schema_version", 1}, {"status", "running"}, {"config", resolved},
        {"started_unix_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()},
        {"gpu", reinterpret_cast<const char*>(glGetString(GL_RENDERER))},
        {"vendor", reinterpret_cast<const char*>(glGetString(GL_VENDOR))},
        {"opengl", reinterpret_cast<const char*>(glGetString(GL_VERSION))},
        {"scene_name", scene.name}, {"camera_position", {camera.position.x, camera.position.y, camera.position.z}},
        {"camera_yaw", camera.yaw}, {"camera_pitch", camera.pitch}, {"camera_fov", camera.fov},
        {"file_fingerprints_fnv1a64", hashes}, {"fixed_time_seconds", 0.0},
        {"enable_denoising", true}, {"enable_antialiasing", false}, {"enable_fxaa", false},
        {"enable_mesh_shadows", base.enableMeshShadows}, {"contrast", 1.0},
        {"measurement_space", "linear RGB before contrast, gamma, and FXAA; albedo remodulated as in resolve"},
        {"reference", "mean of raw PCG frames; same transport, RGB16F buffers, and firefly cap 4"},
        {"blue_repeats", "fixed spatial shifts and temporal phases of one 128x128x64 STBN volume"},
        {"timing", "GPU elapsed time for render passes; excludes readback, metrics, and file output; serialized workload"}};
    writeJson(output / "manifest.json", manifest);
    checkGl();
    const size_t pixelCount = size_t(width) * height;
    Image reference(pixelCount, glm::dvec3(0.0));
    Image halves[2] = {reference, reference};
    pipeline.getConfig().atrousIterations = 0;
    pipeline.restartSampling(refSeed);
    Mask mask;
    for (int frame = 0; frame < refFrames; ++frame)
    {
        pipeline.render(camera, 0.0, scene);
        Image image = benchmark::linearImage(pipeline.readLighting(Pipeline::LightingStage::Raw));
        if (frame == 0) mask = pipeline.readSurfaceMask();
        for (size_t i = 0; i < pixelCount; ++i) halves[frame % 2][i] += image[i] / double(refFrames / 2);
        checkGl();
        progress("Reference", frame, refFrames);
    }
    size_t maskPixels = 0;
    Image maskImage(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        reference[i] = (halves[0][i] + halves[1][i]) * 0.5;
        int x = int(i % width), y = height - 1 - int(i / width);
        mask[i] = (maskName == "all" || mask[i]) && x >= roi[0] && x < roi[0] + roi[2] && y >= roi[1] && y < roi[1] + roi[3];
        maskPixels += mask[i] != 0;
        maskImage[i] = glm::dvec3(mask[i]);
    }
    auto splitError = benchmark::compare(halves[0], halves[1], mask);
    manifest["mask_pixels"] = maskPixels;
    manifest["reference_split_rmse"] = splitError.rmse();
    manifest["reference_estimated_rmse"] = splitError.rmse() * 0.5;
    manifest["reference_uncertainty_note"] = "Half the interleaved half-mean difference estimates full-reference noise only under independent unbiased samples.";
    writeJson(output / "manifest.json", manifest);
    saveImage(output / "reference", reference, width, height);
    saveImage(output / "mask", maskImage, width, height);

    const std::array<Pipeline::LightingStage, 3> stages{Pipeline::LightingStage::Raw, Pipeline::LightingStage::Temporal, Pipeline::LightingStage::Filtered};
    const std::array<std::string, 3> stageNames{"raw", "temporal", "filtered"};
    Json results = {{"schema_version", 1}, {"runs", Json::array()}, {"aggregates", Json::array()}};
    auto csv = outputFile(output / "frames.csv");
    csv << "case,seed,frame,stage,rmse,mae,luminance_bias,flicker_rms,gpu_ms\n";
    GLuint query = 0;
    glGenQueries(1, &query);
    for (size_t repetition = 0; repetition < seeds.size(); ++repetition)
    {
        // Reverse alternating repetitions to reduce systematic order effects in timing.
        for (size_t index = 0; index < cases.size(); ++index)
        {
            const auto& test = cases[repetition % 2 ? cases.size() - 1 - index : index];
            int seed = seeds[repetition];
            const std::string label = test.name + "_seed" + std::to_string(seed);
            fs::path directory = output / label;
            require(fs::create_directory(directory), "The case output directory already exists.");
            pipeline.getConfig() = test.config;
            pipeline.restartSampling(seed);
            for (int frame = 0; frame < warmup; ++frame)
            {
                pipeline.render(camera, 0.0, scene);
                progress(label + " warmup", frame, warmup);
            }
            glFinish();
            checkGl();
            std::array<benchmark::Sequence, 3> sequences{benchmark::Sequence(reference, mask),
                benchmark::Sequence(reference, mask), benchmark::Sequence(reference, mask)};
            std::vector<double> gpuTimes;
            for (int frame = 0; frame < frames; ++frame)
            {
                glBeginQuery(GL_TIME_ELAPSED, query);
                pipeline.render(camera, 0.0, scene);
                glEndQuery(GL_TIME_ELAPSED);
                GLuint64 nanos = 0;
                glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanos);
                double gpuMs = double(nanos) / 1000000.0;
                gpuTimes.push_back(gpuMs);
                for (size_t stage = 0; stage < stages.size(); ++stage)
                {
                    Image image = benchmark::linearImage(pipeline.readLighting(stages[stage]));
                    auto error = sequences[stage].add(image);
                    csv << test.name << ',' << seed << ',' << frame << ',' << stageNames[stage] << ','
                        << error.rmse() << ',' << error.mae << ',' << error.luminanceBias << ',';
                    if (frame > 0) csv << sequences[stage].lastFlicker;
                    csv << ',' << gpuMs << '\n';
                    if (frame + 1 == frames) saveImage(directory / (stageNames[stage] + "_final"), image, width, height);
                }
                checkGl();
                progress(label, frame, frames);
            }
            Json run = {{"case", test.name}, {"seed", seed}, {"settings", test.settings},
                {"gpu_ms_mean", std::accumulate(gpuTimes.begin(), gpuTimes.end(), 0.0) / gpuTimes.size()},
                {"gpu_ms_p50", percentile(gpuTimes, 0.50)}, {"gpu_ms_p95", percentile(gpuTimes, 0.95)},
                {"stages", Json::object()}};
            for (size_t stage = 0; stage < stages.size(); ++stage)
            {
                const auto& sequence = sequences[stage];
                Json metrics = errorJson(sequence.averageError);
                metrics["frame_rmse_mean"] = sequence.averageFrameRmse;
                metrics["flicker_rms"] = sequence.flickerRms();
                metrics["temporal_std"] = sequence.temporalStd();
                metrics["mean_image_rmse"] = benchmark::compare(sequence.mean, reference, mask).rmse();
                run["stages"][stageNames[stage]] = metrics;
                saveImage(directory / (stageNames[stage] + "_mean"), sequence.mean, width, height);
            }
            results["runs"].push_back(run);
            writeJson(output / "results.partial.json", results);
            std::cout << label << " filtered: " << run["stages"]["filtered"].dump() << std::endl;
        }
    }
    glDeleteQueries(1, &query);
    auto summary = outputFile(output / "summary.csv");
    summary << "case,stage,metric,mean,repeat_stddev,repeats\n";
    for (const auto& test : cases)
        for (const auto& stage : stageNames)
        {
            Json aggregate = {{"case", test.name}, {"stage", stage}, {"metrics", Json::object()}};
            Json first;
            for (const auto& run : results["runs"]) if (run["case"] == test.name) { first = run["stages"][stage]; break; }
            first["gpu_ms_mean"] = 0.0;
            for (const auto& metric : first.items())
            {
                std::vector<double> values;
                for (const auto& run : results["runs"])
                {
                    if (run["case"] != test.name) continue;
                    const auto& value = metric.key() == "gpu_ms_mean" ? run["gpu_ms_mean"] : run["stages"][stage][metric.key()];
                    if (value.is_number()) values.push_back(value.get<double>());
                }
                if (values.size() != seeds.size())
                {
                    aggregate["metrics"][metric.key()] = {{"mean", nullptr}, {"repeat_stddev", nullptr}, {"repeats", values.size()}};
                    summary << test.name << ',' << stage << ',' << metric.key() << ",,," << values.size() << '\n';
                    continue;
                }
                double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
                double variance = 0.0;
                for (double value : values) variance += (value - mean) * (value - mean);
                double sd = values.size() > 1 ? std::sqrt(variance / (values.size() - 1)) : 0.0;
                aggregate["metrics"][metric.key()] = {{"mean", mean}, {"repeat_stddev", sd}, {"repeats", values.size()}};
                summary << test.name << ',' << stage << ',' << metric.key() << ',' << mean << ',';
                if (values.size() > 1) summary << sd;
                summary << ',' << values.size() << '\n';
                if (values.size() == 1) aggregate["metrics"][metric.key()]["repeat_stddev"] = nullptr;
            }
            results["aggregates"].push_back(aggregate);
        }
    require(fingerprints(executable) == hashes, "A shader, asset, or executable changed during the experiment.");
    checkGl();
    csv.close();
    summary.close();
    writeJson(output / "results.json", results);
    manifest["status"] = "complete";
    writeJson(output / "manifest.json", manifest);
    std::cout << "Complete: " << fs::absolute(output) << std::endl;
}
}

int main(int argc, char** argv)
{
    fs::path configPath, output;
    int framesOverride = 0;
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string option = argv[i];
            if (option == "--help")
            {
                std::cout << "Usage: tfm_lighting_benchmark --config FILE --output NEW_DIRECTORY [--frames N]\n"
                    << "The benchmark uses a fixed camera and linear RGB. See docs/benchmark_iluminacion.md.\n";
                return 0;
            }
            require(i + 1 < argc, "Missing value for " + option);
            std::string value = argv[++i];
            if (option == "--config") configPath = value;
            else if (option == "--output") output = value;
            else if (option == "--frames") framesOverride = integer(Json::parse(value), 2, 1000000, "frames");
            else throw std::runtime_error("Unknown option: " + option);
        }
        require(!configPath.empty() && !output.empty(), "Supply --config FILE and --output NEW_DIRECTORY. Use --help for details.");
        std::ifstream file(configPath);
        require(bool(file), "Cannot open the configuration file.");
        Json config = Json::parse(file);
        if (framesOverride) config["frames"] = framesOverride;
        run(config, output, fs::absolute(argv[0]));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
