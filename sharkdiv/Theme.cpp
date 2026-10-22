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

// Shark theme: the SHARKDIV brand — the web flasher's phosphor green on black.
//   bg     black
//   fg     card / key surface (dark green)
//   icon   phosphor green accent
//   text   green-tinted ink
//   line   green hairline
//   lable  header / nav band
//   warn   amber
#define SHARK_BG     0x0000
#define SHARK_FG     BRAND_CARD
#define SHARK_ICON   BRAND_ACCENT
#define SHARK_TEXT   BRAND_INK
#define SHARK_LINE   BRAND_LINE
#define SHARK_LABLE  BRAND_BAND
#define SHARK_ACCENT BRAND_ACCENT
#define SHARK_WARN   BRAND_WARN

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
    // SHARKDIV brand: black surfaces, green-tinted ink, phosphor green accent.
    // The accent preset is ignored so the system color stays brand green.
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
