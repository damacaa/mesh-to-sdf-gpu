#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace benchmark
{
using Image = std::vector<glm::dvec3>;
using Mask = std::vector<unsigned char>;

inline double luminance(const glm::dvec3& color)
{
    return glm::dot(color, glm::dvec3(0.2126, 0.7152, 0.0722));
}

inline Image linearImage(const std::vector<glm::vec4>& pixels)
{
    Image image;
    image.reserve(pixels.size());
    for (const auto& pixel : pixels)
    {
        for (int c = 0; c < 3; ++c)
            if (!std::isfinite(pixel[c])) throw std::runtime_error("Nonfinite lighting pixel.");
        image.emplace_back(pixel);
    }
    return image;
}

struct Error
{
    double mse = 0.0;
    double mae = 0.0;
    double referencePower = 0.0;
    double luminanceBias = 0.0;
    double rmse() const { return std::sqrt(mse); }
};

inline Error compare(const Image& image, const Image& reference, const Mask& mask)
{
    if (image.size() != reference.size() || image.size() != mask.size())
        throw std::invalid_argument("Metric image dimensions differ.");
    Error error;
    size_t count = 0;
    for (size_t i = 0; i < image.size(); ++i)
    {
        for (int c = 0; c < 3; ++c)
            if (!std::isfinite(image[i][c]) || !std::isfinite(reference[i][c]))
                throw std::runtime_error("Nonfinite metric input.");
        if (!mask[i]) continue;
        const auto delta = image[i] - reference[i];
        error.mse += glm::dot(delta, delta) / 3.0;
        error.mae += (std::abs(delta.x) + std::abs(delta.y) + std::abs(delta.z)) / 3.0;
        error.referencePower += glm::dot(reference[i], reference[i]) / 3.0;
        error.luminanceBias += luminance(delta);
        ++count;
    }
    if (count == 0) throw std::invalid_argument("The metric mask has no pixels.");
    error.mse /= count;
    error.mae /= count;
    error.referencePower /= count;
    error.luminanceBias /= count;
    return error;
}

class Sequence
{
public:
    Image mean;
    size_t frames = 0;
    Error averageError;
    double averageFrameRmse = 0.0;
    double lastFlicker = 0.0;

    Sequence(const Image& reference, const Mask& mask)
        : mean(reference.size(), glm::dvec3(0.0)), m_reference(reference), m_mask(mask),
          m_previous(reference.size()), m_luminanceM2(reference.size(), 0.0) {}

    Error add(const Image& image)
    {
        const Error error = compare(image, m_reference, m_mask);
        ++frames;
        averageError.mse += (error.mse - averageError.mse) / frames;
        averageError.mae += (error.mae - averageError.mae) / frames;
        averageError.referencePower = error.referencePower;
        averageError.luminanceBias += (error.luminanceBias - averageError.luminanceBias) / frames;
        averageFrameRmse += (error.rmse() - averageFrameRmse) / frames;
        double flickerSquared = 0.0;
        size_t count = 0;
        for (size_t i = 0; i < image.size(); ++i)
        {
            const auto delta = image[i] - mean[i];
            mean[i] += delta / double(frames);
            m_luminanceM2[i] += luminance(delta) * luminance(image[i] - mean[i]);
            if (m_mask[i])
            {
                const double difference = luminance(image[i]) - m_previous[i];
                flickerSquared += difference * difference;
                ++count;
            }
            m_previous[i] = luminance(image[i]);
        }
        lastFlicker = frames > 1 ? std::sqrt(flickerSquared / count) : 0.0;
        if (frames > 1) m_flickerSquared += flickerSquared / count;
        return error;
    }

    double flickerRms() const { return frames > 1 ? std::sqrt(m_flickerSquared / (frames - 1)) : 0.0; }
    double temporalStd() const
    {
        double variance = 0.0;
        size_t count = 0;
        for (size_t i = 0; i < mean.size(); ++i)
            if (m_mask[i]) { variance += m_luminanceM2[i]; ++count; }
        // Population variance describes the observed sequence, not an independent-sample estimate.
        return frames && count ? std::sqrt(std::max(0.0, variance / (frames * count))) : 0.0;
    }

private:
    const Image& m_reference;
    const Mask& m_mask;
    std::vector<double> m_previous;
    std::vector<double> m_luminanceM2;
    double m_flickerSquared = 0.0;
};
}
