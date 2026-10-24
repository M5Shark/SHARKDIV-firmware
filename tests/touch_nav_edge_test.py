#!/usr/bin/env python3
"""Compile the production touch-edge helper with a deterministic input fixture.

Run: python3 tests/touch_nav_edge_test.py
Requires a C++ compiler; does not require Arduino headers or hardware.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
sketch = (repo / "sharkdiv" / "sharkdiv.ino").read_text()
start = sketch.index("bool isTouchNavButtonPressedEdge(int buttonPin) {")
end = sketch.index("bool isButtonPressedEdge(int buttonPin)", start)
production_helper = sketch[start:end]

fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
bool feature_active = true;
bool touchButtonInputEnabled = true;
bool s_touchNavHeld[5] = {};
uint32_t s_navTapLatchMs[5] = {};
bool s_navTapLatchNew[5] = {};
bool rawDown[5] = {};
uint32_t clockMs = 1000;
uint32_t millis() { return clockMs; }
int touchNavIndexForPin(int pin) { return pin >= 0 && pin < 5 ? pin : -1; }
bool isTouchNavSlotDown(int index) { return rawDown[index]; }
'''
checks = r'''
int main() {
  // A short Hold tap must not toggle again after the 80 ms scanner debounce.
  rawDown[0] = true;
  assert(isTouchNavButtonPressedEdge(0));
  rawDown[0] = false;
  clockMs += 80;
  assert(!isTouchNavButtonPressedEdge(0));
  clockMs += 80;
  assert(!isTouchNavButtonPressedEdge(0));

  // Holding a button produces one edge, even if a level reader rearms its latch.
  rawDown[0] = true;
  assert(isTouchNavButtonPressedEdge(0));
  for (int i = 0; i < 10; ++i) {
    clockMs += 80;
    s_navTapLatchNew[0] = true;
    assert(!isTouchNavButtonPressedEdge(0));
  }
  rawDown[0] = false;
  s_navTapLatchNew[0] = true;
  assert(!isTouchNavButtonPressedEdge(0));

  // A new press is accepted immediately after a release, independently by slot.
  rawDown[0] = true;
  assert(isTouchNavButtonPressedEdge(0));
  rawDown[1] = true;
  assert(isTouchNavButtonPressedEdge(1));
  assert(!isTouchNavButtonPressedEdge(0));
  rawDown[0] = rawDown[1] = false;
  assert(!isTouchNavButtonPressedEdge(0));
  assert(!isTouchNavButtonPressedEdge(1));

  // Taps captured between polls / injected by VM still arrive exactly once.
  clockMs += 80;
  s_navTapLatchMs[2] = clockMs;
  s_navTapLatchNew[2] = true;
  assert(isTouchNavButtonPressedEdge(2));
  assert(!isTouchNavButtonPressedEdge(2));
  s_navTapLatchNew[2] = true;
  clockMs += 350;
  assert(!isTouchNavButtonPressedEdge(2));

  // Unsigned elapsed time keeps a captured tap valid across millis() wrap.
  s_navTapLatchMs[3] = UINT32_MAX - 10;
  s_navTapLatchNew[3] = true;
  clockMs = 20;
  assert(isTouchNavButtonPressedEdge(3));
  assert(!isTouchNavButtonPressedEdge(3));

  rawDown[4] = true;
  feature_active = false;
  assert(!isTouchNavButtonPressedEdge(4));
  feature_active = true;
  touchButtonInputEnabled = false;
  assert(!isTouchNavButtonPressedEdge(4));
  touchButtonInputEnabled = true;
  assert(!isTouchNavButtonPressedEdge(-1));
  assert(!isTouchNavButtonPressedEdge(5));
  puts("Touch navigation edge tests passed");
}
'''
with tempfile.TemporaryDirectory(prefix="sharkdiv-touch-test-") as directory:
    source = Path(directory) / "touch_nav_edge_test.cpp"
    binary = Path(directory) / "touch_nav_edge_test"
    source.write_text(fixture + production_helper + checks)
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    subprocess.run(compiler + ["-std=c++11", "-Wall", "-Wextra", "-Werror",
                              str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
