// flock_score.cpp — weights live in flock_config.h and must sum to 100:
//   WiFi match      35  (camera AP is definitive)
//   BLE advert      25  (corroborating)
//   LTE burst       20  (scaled by burst strength)
//   2.4G activity   10  (scaled by activity index)
//   IR flash        10  (needs photodiode; never earned on TSOP boards)
#include "flock_score.h"
#include "flock_config.h"

namespace flock_score {

int compute(bool wifiMatch, bool bleMatch, int lteStrength,
            int nrfActivity, bool irFlash) {
  int s = 0;
  if (wifiMatch) s += flk::kScoreWifi;
  if (bleMatch)  s += flk::kScoreBle;
  if (lteStrength > 0) s += flk::kScoreLte * lteStrength / 100;
  if (nrfActivity > 0) s += flk::kScoreNrf * nrfActivity / 100;
  if (irFlash)    s += flk::kScoreIr;
  if (s > 100) s = 100;
  return s;
}

const char* label(int score) {
  if (score >= 60) return "CONFIRMED";
  if (score >= 35) return "LIKELY";
  if (score >= 10) return "POSSIBLE";
  return "QUIET";
}

}  // namespace flock_score
