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

struct CurveSample { double value, slope; };
inline CurveSample EaseOut(double progress) {
    // cubic-bezier(0.16, 1, 0.30, 1): a distinctly fast departure followed by
    // a long deceleration, with exactly zero endpoint velocity. Solve x(u)
    // first; substituting elapsed time directly for u would be a different curve.
    double t=std::clamp(progress,0.0,1.0);
    if(t==0) return {0,6.25};
    if(t==1) return {1,0};
    double u=t;
    for(int i=0;i<8;i++) {
        double x=u*(.48+u*(-.06+.58*u));
        double dx=.48+u*(-.12+1.74*u); // Always >= .477, no singularity.
        u=std::clamp(u-(x-t)/dx,0.0,1.0);
    }
    double remaining=1-u;
    return {1-remaining*remaining*remaining,
        3*remaining*remaining/(.48+u*(-.12+1.74*u))};
}

class Transition {
    double from_=0, to_=0;
    double start_=0;
    double duration_=0;
public:
    static constexpr double DurationMs=720;
    double Value(double now) const {
        if(duration_<=0) return to_;
        double t=std::clamp((now-start_)/duration_,0.0,1.0);
        return from_+(to_-from_)*EaseOut(t).value;
    }
    double Velocity(double now) const {
        if(!Running(now)) return 0;
        double t=std::clamp((now-start_)/duration_,0.0,1.0);
        return (to_-from_)*EaseOut(t).slope/duration_;
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
