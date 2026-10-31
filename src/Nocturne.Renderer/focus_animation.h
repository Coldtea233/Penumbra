#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace focus_animation {
inline double Smooth(double value) {
    double t=std::clamp(value,0.0,1.0);
    return t*t*(3-2*t);
}
// A single reversible path: hide expands first, then fades in the latter half.
// Show follows the same path backwards. Icon cutouts are applied after scaling.
inline double Scale(double visibility) { return 1+.45*(1-std::clamp(visibility,0.0,1.0)); }
inline double Opacity(double visibility) { return Smooth(visibility*2); }

class Transition {
    double from_=0, to_=0;
    uint64_t start_=0;
    double duration_=0;
public:
    static constexpr double DurationMs=360;
    double Value(uint64_t now) const {
        if(duration_<=0) return to_;
        double t=std::clamp((now-start_)/duration_,0.0,1.0);
        // Respond immediately, then settle softly. Do not ease the scale again:
        // double easing made the first third look like a delayed hard switch.
        double eased=1-(1-t)*(1-t)*(1-t);
        return from_+(to_-from_)*eased;
    }
    bool Running(uint64_t now) const { return duration_>0 && now-start_<duration_; }
    bool Target() const { return to_>0; }
    void Reset(bool visible) { from_=to_=visible?1:0; duration_=0; }
    bool Retarget(bool visible,uint64_t now) {
        double target=visible?1:0;
        if(target==to_) return false; // App-to-app changes never restart the fade.
        from_=Value(now); to_=target; start_=now;
        // Continue from the current value after a reversal, never an endpoint.
        duration_=DurationMs*std::max(.25,std::abs(to_-from_));
        return true;
    }
};
}
