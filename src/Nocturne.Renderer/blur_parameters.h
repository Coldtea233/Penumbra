#pragma once
#include <algorithm>
#include <cmath>

namespace blur_parameters {
// The source is unfiltered; zero is identity and the slider directly controls
// the Gaussian standard deviation. Never add a system-blur calibration floor.
inline double Sigma(double radius) {
    return std::isfinite(radius)?std::clamp(radius,0.0,48.0):12.0;
}
}
