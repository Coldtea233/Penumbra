#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace focus_animation {
inline double Smooth(double value) {
    double t=std::clamp(value,0.0,1.0);
    return t*t*(3-2*t);
}
// One easing curve, not an ease-out followed by another, rapidly saturating
// opacity curve. The uploaded icon cutouts never move during a fade.
inline double Opacity(double visibility) { return std::clamp(visibility,0.0,1.0); }

class Transition {
    double from_=0, to_=0;
    double start_=0;
    double duration_=0;
public:
    static constexpr double DurationMs=420;
    double Value(double now) const {
        if(duration_<=0) return to_;
        double t=std::clamp((now-start_)/duration_,0.0,1.0);
        // A single quadratic ease-out: responsive start and zero final speed.
        // Unlike the old compound curve, alpha uses the entire fade duration.
        double eased=1-(1-t)*(1-t);
        return from_+(to_-from_)*eased;
    }
    double Velocity(double now) const {
        if(!Running(now)) return 0;
        double t=std::clamp((now-start_)/duration_,0.0,1.0);
        return (to_-from_)*2*(1-t)/duration_;
    }
    bool Running(double now) const { return duration_>0 && now-start_<duration_; }
    double Remaining(double now) const { return std::max(0.0,start_+duration_-now); }
    bool Target() const { return to_>0; }
    void Reset(bool visible) { from_=to_=visible?1:0; duration_=0; }
    bool Retarget(bool visible,double now) {
        double target=visible?1:0;
        if(target==to_) return false; // App-to-app changes never restart the fade.
        from_=Value(now); to_=target; start_=now;
        // Continue from the current value after a reversal, never an endpoint.
        duration_=DurationMs*std::max(.35,std::sqrt(std::abs(to_-from_)));
        return true;
    }
};
}
