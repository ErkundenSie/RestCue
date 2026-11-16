#pragma once

#include <algorithm>
#include <cstdint>

namespace restcue {

enum class Phase { Stopped, Working, Resting, Paused };

// All times are monotonic milliseconds, injected so the scheduler is testable.
class Timer {
public:
    void start(std::uint64_t now, std::uint64_t workMs, std::uint64_t restMs) {
        workMs_ = std::max<std::uint64_t>(1, workMs);
        restMs_ = std::max<std::uint64_t>(1, restMs);
        restartWork(now);
    }

    void restartWork(std::uint64_t now) {
        phase_ = Phase::Working;
        deadline_ = now + workMs_;
    }

    void stop() { phase_ = Phase::Stopped; }

    void tick(std::uint64_t now) {
        if (now < deadline_) return;
        if (phase_ == Phase::Working) {
            // A delayed UI tick must still give the user a full break.
            phase_ = Phase::Resting;
            deadline_ = now + restMs_;
        } else if (phase_ == Phase::Resting) {
            restartWork(now);
        }
    }

    void pause(std::uint64_t now) {
        tick(now);
        if (phase_ != Phase::Working && phase_ != Phase::Resting) return;
        savedPhase_ = phase_;
        savedRemaining_ = remaining(now);
        phase_ = Phase::Paused;
    }

    void resume(std::uint64_t now) {
        if (phase_ != Phase::Paused) return;
        phase_ = savedPhase_;
        deadline_ = now + savedRemaining_;
    }

    void restNow(std::uint64_t now) {
        phase_ = Phase::Resting;
        deadline_ = now + restMs_;
    }

    void skipRest(std::uint64_t now) {
        if (phase_ == Phase::Resting) restartWork(now);
    }

    // Do not count system sleep as working time or show an overdue break.
    void onSystemResume(std::uint64_t now) {
        if (phase_ == Phase::Working || phase_ == Phase::Resting) restartWork(now);
    }

    Phase phase() const { return phase_; }
    std::uint64_t restDuration() const { return restMs_; }
    std::uint64_t remaining(std::uint64_t now) const {
        if (phase_ == Phase::Paused) return savedRemaining_;
        if (phase_ == Phase::Stopped || now >= deadline_) return 0;
        return deadline_ - now;
    }

private:
    Phase phase_ = Phase::Stopped;
    Phase savedPhase_ = Phase::Working;
    std::uint64_t deadline_ = 0;
    std::uint64_t savedRemaining_ = 0;
    std::uint64_t workMs_ = 45 * 60 * 1000;
    std::uint64_t restMs_ = 5 * 60 * 1000;
};

inline unsigned fadeAlpha(std::uint64_t elapsed, std::uint64_t remaining,
                          std::uint64_t duration, unsigned opacityPercent) {
    const auto fadeMs = std::max<std::uint64_t>(1, std::min<std::uint64_t>(1500, duration / 3));
    const auto ramp = std::min({fadeMs, elapsed, remaining});
    return static_cast<unsigned>(255ULL * std::min(opacityPercent, 100U) * ramp / (100 * fadeMs));
}

} // namespace restcue
