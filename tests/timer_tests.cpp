#include "../timer.hpp"
#include <cstdlib>
#include <iostream>

int checks = 0;
void expect(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
        std::exit(1);
    }
}

int main() {
    using namespace restcue;
    Timer t;
    expect(t.phase() == Phase::Stopped, "initially stopped");
    expect(t.remaining(100) == 0, "stopped has no countdown");
    t.start(100, 60000, 5000);
    expect(t.remaining(100) == 60000, "full working interval");
    t.tick(60099);
    expect(t.phase() == Phase::Working, "do not transition early");
    t.tick(60100);
    expect(t.phase() == Phase::Resting, "enter break on boundary");
    expect(t.remaining(60100) == 5000, "full break duration");
    t.tick(65100);
    expect(t.phase() == Phase::Working, "return to work");
    expect(t.remaining(65100) == 60000, "new work interval starts after break");

    t.pause(66100);
    expect(t.phase() == Phase::Paused, "pause work");
    expect(t.remaining(999999) == 59000, "paused countdown stays frozen");
    t.tick(999999);
    expect(t.phase() == Phase::Paused, "tick cannot unpause");
    t.resume(1000000);
    expect(t.remaining(1000000) == 59000, "resume preserves remaining time");
    t.tick(1059000);
    expect(t.phase() == Phase::Resting, "resumed countdown expires correctly");
    t.pause(1060000);
    expect(t.remaining(2000000) == 4000, "pause a break");
    t.resume(2000000);
    expect(t.phase() == Phase::Resting, "resume a break");
    t.tick(2004000);
    expect(t.phase() == Phase::Working, "resumed break ends");

    t.restNow(3000000);
    expect(t.remaining(3000000) == 5000, "manual break uses configured duration");
    t.skipRest(3000100);
    expect(t.phase() == Phase::Working, "skip break");
    expect(t.remaining(3000100) == 60000, "skip resets work interval");
    t.skipRest(3000200);
    expect(t.remaining(3000200) == 59900, "skip does not reset an active work interval");

    t.start(0, 100, 50);
    t.tick(10000);
    expect(t.phase() == Phase::Resting && t.remaining(10000) == 50, "late tick gives a full break");
    t.tick(20000);
    expect(t.phase() == Phase::Working && t.remaining(20000) == 100, "late break completion resets work");
    t.onSystemResume(30000);
    expect(t.remaining(30000) == 100, "system resume resets work");
    t.restNow(40000);
    t.onSystemResume(40001);
    expect(t.phase() == Phase::Working, "system resume dismisses a stale break");
    t.pause(40002);
    const auto frozen = t.remaining(40002);
    t.onSystemResume(90000);
    expect(t.phase() == Phase::Paused && t.remaining(90000) == frozen, "system resume respects pause");
    t.stop();
    t.resume(100000);
    t.tick(100000);
    expect(t.phase() == Phase::Stopped, "stop remains stopped");
    t.restNow(200000);
    expect(t.phase() == Phase::Resting, "manual break can start from stopped");
    t.start(0, 0, 0);
    expect(t.remaining(0) == 1, "zero durations are guarded");
    t.tick(1);
    expect(t.phase() == Phase::Resting && t.remaining(1) == 1, "guarded break duration");

    expect(fadeAlpha(0, 60000, 60000, 70) == 0, "starts transparent");
    expect(fadeAlpha(1500, 58500, 60000, 70) == 178, "reaches selected opacity");
    expect(fadeAlpha(30000, 30000, 60000, 70) == 178, "holds opacity");
    expect(fadeAlpha(59250, 750, 60000, 70) == 89, "fades out");
    expect(fadeAlpha(60000, 0, 60000, 70) == 0, "ends transparent");
    expect(fadeAlpha(400, 600, 1000, 70) == 178, "short break still reaches opacity");
    expect(fadeAlpha(400, 600, 1000, 200) == 255, "opacity is clamped");
    expect(fadeAlpha(0, 0, 0, 70) == 0, "zero duration fade is safe");
    std::cout << checks << " checks passed\n";
}
