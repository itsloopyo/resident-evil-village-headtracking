#pragma once

#include <cameraunlock/math/smoothing_utils.h>

namespace RE8HT {

// Frame-rate-independent exponential smoother for a single scalar.
// Seeds to the first raw value, then lerps toward subsequent values by the
// supplied per-frame factor t (from CalculateSmoothingFactor).
struct ExponentialSmoother {
    float value = 0.0f;
    bool initialized = false;

    float Update(float raw, float t) {
        if (!initialized) {
            value = raw;
            initialized = true;
        } else {
            value = cameraunlock::math::Lerp(value, raw, t);
        }
        return value;
    }
};

} // namespace RE8HT
