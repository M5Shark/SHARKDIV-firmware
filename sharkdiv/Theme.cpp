#include "shared.h"
#include "SettingsStore.h"


// Hacker theme: phosphor-green terminal on pure black.
//   bg    0x0000  pure black
//   fg    0x03C0  (0,120,0)   green tile fill (readable on black)
//   icon  0x05E0  (0,188,0)   bright-green icons/borders
//   text  0x07E0  (0,255,0)   full phosphor green
//   line  0x0560  (0,172,0)   green separators
//   lable 0x0560  (0,172,0)    dim fills / dim text
//   accent 0x07FF (0,255,200) mint-white selection highlight
#define HACK_BG     0x0000
#define HACK_FG     0x03C0
#define HACK_ICON   0x05E0
#define HACK_TEXT   0x07E0
#define HACK_LINE   0x0560
#define HACK_LABLE  0x0560
#define HACK_ACCENT 0x07FF

// Shark theme: M5SHARK "ctOS" look — bone white on black, one cyan accent.
//   bg    0x0000  pure black
//   fg    0x10A3  dark tile surface (deep blue-grey)
//   icon  0x07FF  cyan system accent
//   text  0xC618  bone white labels
//   line  0x29C8  dim cyan edge
//   lable 0x1904  near-black panel fill (status bar reads as black)
//   accent 0x07FF cyan
//   warn  0xFD20  amber warnings/toggles
#define SHARK_BG     0x0000
#define SHARK_FG     0x10A3
#define SHARK_ICON   0x07FF
#define SHARK_TEXT   0xC618
#define SHARK_LINE   0x29C8
#define SHARK_LABLE  0x1904
#define SHARK_ACCENT 0x07FF
#define SHARK_WARN   0xFD20

UiPalette UI = { SHARK_BG, SHARK_FG, SHARK_ICON, SHARK_TEXT, SHARK_ACCENT,
                 SHARK_LINE, SHARK_LABLE, SHARK_WARN, SHARK_ICON };

uint16_t uiUniversalColor() {
  return UI.warn;
}

void applyThemeToPalette(Theme t) {
  const uint16_t universal = accentColor565(settings().accentColor);

  if (t == Theme::Light) {
    UI.bg     =  BG_Light;
    UI.fg     =  FG_Light;
    UI.icon   =  universal;
    UI.text   =  TEXT_Light;
    UI.line   =  LINE_Light;
    UI.accent =  UI_ACCENT;
    UI.lable  =  L_Light;
    UI.warn   =  universal;
    UI.ok     =  GREEN;

  } else if (t == Theme::Hacker) {
    // Green terminal style: everything phosphor green on black, ignoring
    // the accent preset so the look stays monochrome-green.
    UI.bg     =  HACK_BG;
    UI.fg     =  HACK_FG;
    UI.icon   =  HACK_ICON;
    UI.text   =  HACK_TEXT;
    UI.line   =  HACK_LINE;
    UI.accent =  HACK_ACCENT;
    UI.lable  =  HACK_LABLE;
    UI.warn   =  HACK_ACCENT;
    UI.ok     =  HACK_TEXT;

  } else if (t == Theme::Shark) {
    // M5SHARK ctOS style: black surfaces, bone text, single cyan accent.
    // The accent preset is ignored so the system color stays cyan.
    UI.bg     =  SHARK_BG;
    UI.fg     =  SHARK_FG;
    UI.icon   =  SHARK_ICON;
    UI.text   =  SHARK_TEXT;
    UI.line   =  SHARK_LINE;
    UI.accent =  SHARK_ACCENT;
    UI.lable  =  SHARK_LABLE;
    UI.warn   =  SHARK_WARN;
    UI.ok     =  SHARK_ICON;

  } else {
    UI.bg     =  BG_Dark;
    UI.fg     =  FG_Dark;
    UI.icon   =  universal;
    UI.text   =  TEXT_Dark;
    UI.line   =  LINE_Dark;
    UI.accent =  UI_ACCENT;
    UI.lable  =  L_Dark;
    UI.warn   =  universal;
    UI.ok     =  GREEN;
  }
}
