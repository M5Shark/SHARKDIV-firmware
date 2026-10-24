#pragma once
namespace flock_score {
// Session evidence index. Radio energy never contributes identity confidence.
int compute(bool wifiMatch, bool bleMatch, int subGhz, int nrf, bool ir);
const char* label(int score);
}
