#include "flock_score.h"
#include "flock_config.h"
namespace flock_score {
int compute(bool wifiMatch, bool bleMatch, int, int, bool) {
  // Ambient RF and a nearby BLE device cannot be assigned to a Wi-Fi target.
  // This is a session evidence index, not an identity probability.
  return (wifiMatch ? flk::kScoreWifi : 0) + (bleMatch ? flk::kScoreBle : 0);
}
const char* label(int score) {
  if (score >= flk::kScoreWifi + flk::kScoreBle) return "MULTIPLE MATCHES";
  if (score > 0) return "CANDIDATE";
  return "NO MATCHES";
}
} // namespace flock_score
