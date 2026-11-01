#pragma once
#include <algorithm>
#include <cmath>

namespace blur_parameters {
// HostBackdrop already supplies a system-owned blur. Treat the control as an
// increment to the effective radius, rather than adding a tiny variance to an
// already large variance. 30 is a calibration estimate, not an OS guarantee;
// preview and UI explicitly describe this as additional/approximate blur.
constexpr double BaselineSigma = 30.0;
inline double AdditionalSigma(double radius) {
    radius=std::isfinite(radius)?std::clamp(radius,0.0,48.0):12.0;
    return std::sqrt(radius*(radius+2*BaselineSigma));
}
}
