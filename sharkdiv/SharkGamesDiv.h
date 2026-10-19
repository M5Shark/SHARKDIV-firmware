#pragma once

// SharkGamesDiv — the M5SHARK games (SharkGames.cpp), retimed for the
// ESP32-DIV feature contract: Setup once, Loop every pass, exit on leaving.
// The touch nav bar doubles as the D-pad in Snake; center button is Exit.

namespace SnakeGame {
  void snakeSetup();
  void snakeLoop();
  void exit();
}

namespace ReactionGame {
  void reactionSetup();
  void reactionLoop();
  void exit();
}
