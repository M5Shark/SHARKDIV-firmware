#pragma once
// flock_score.h — 0-100 confidence from the passive evidence.
namespace flock_score {
int compute(bool wifiMatch, bool bleMatch, int lteStrength,
            int nrfActivity, bool irFlash);
const char* label(int score);   // Confirmed / Likely / Possible / Quiet
}  // namespace flock_score
