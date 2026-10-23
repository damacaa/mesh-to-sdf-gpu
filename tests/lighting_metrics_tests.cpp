#include "benchmark/LightingMetrics.h"
#include <iostream>
#include <limits>

void near(double actual, double expected)
{
    if (std::abs(actual - expected) > 1e-10) throw std::runtime_error("Unexpected metric value.");
}

int main()
{
    try
    {
        using namespace benchmark;
        Image reference(2, glm::dvec3(1.0));
        Mask mask{1, 0};
        Sequence sequence(reference, mask);
        Image low{glm::dvec3(0.0), glm::dvec3(100.0)};
        Image high{glm::dvec3(2.0), glm::dvec3(200.0)};
        near(sequence.add(low).rmse(), 1.0);
        near(sequence.add(high).mae, 1.0);
        near(sequence.averageError.luminanceBias, 0.0);
        near(sequence.flickerRms(), 2.0);
        near(sequence.temporalStd(), 1.0);
        near(compare(sequence.mean, reference, mask).rmse(), 0.0);
        Sequence biased(reference, mask);
        biased.add(high);
        biased.add(high);
        near(biased.flickerRms(), 0.0);
        near(biased.temporalStd(), 0.0);
        near(biased.averageError.luminanceBias, 1.0);
        near(compare(biased.mean, reference, mask).rmse(), 1.0);
        bool rejected = false;
        try { compare(low, reference, Mask{0, 0}); }
        catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("An empty mask was accepted.");
        high[1].x = std::numeric_limits<double>::quiet_NaN();
        rejected = false;
        try { compare(high, reference, mask); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) throw std::runtime_error("A nonfinite pixel was accepted.");
        std::cout << "PASS: image error, mask, bias, flicker, variance, and invalid input\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
