#!/usr/bin/env python3
"""Exercise the production menu fit/window functions with a host TFT fixture."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "sharkdiv/sharkdiv.ino").read_text()


def function(name):
    match = re.search(r"static (?:void|int|bool|String) " + name + r"\([^;]*?\)\s*\{", source)
    assert match, name
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


fixture = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#include <cstdio>
class String : public std::string {
 public:
 using std::string::string;
 String(const std::string& value): std::string(value) {}
 void remove(size_t index) { erase(index); }
};
enum {TL_DATUM, MC_DATUM};
constexpr uint16_t UI_BG=0, UI_LINE=1, UI_ICON=2, UI_TEXT=3, TFT_BLACK=0, FEATURE_BG=0;
const unsigned char bitmap_icon_LEFT[]={0}, bitmap_icon_DOWN[]={0},
 bitmap_icon_go_back[]={0}, bitmap_icon_UP[]={0}, bitmap_icon_RIGHT[]={0};
namespace FeatureUI {
enum class ButtonStyle {Secondary};
struct Button {int16_t x,y,w,h; const char* label; ButtonStyle style; bool active;};
}
constexpr int TOUCH_NAV_BAR_H=52;
FeatureUI::Button s_touchNavBtns[5];
const char* s_touchNavLabels[5] = {};
bool s_touchNavLabelsConfigured=true;
struct Text {String value; int x,y,width,font;};
struct MockTft {
 int font=1, size=1, icons=0;
 std::vector<Text> text;
 int width() const {return 240;}
 int height() const {return 320;}
 // Deliberately different fonts expose measuring one font and drawing another.
 int textWidth(const String& value, int f) const {return int(value.length())*(f==2?9:6)*size;}
 void setTextFont(int value) {font=value;}
 void setTextSize(int value) {size=value;}
 void setTextDatum(int) {}
 void setTextColor(uint16_t,uint16_t) {}
 void fillRect(int,int,int,int,uint16_t) {}
 void drawRect(int,int,int,int,uint16_t) {}
 void drawFastHLine(int,int,int,uint16_t) {}
 void drawBitmap(int,int,const unsigned char*,int,int,uint16_t) {++icons;}
 void drawString(const String& value,int x,int y,int f) {
   text.push_back({value,x,y,textWidth(value,f),f});
 }
} tft;
int active_submenu_size=13, current_submenu_index=0, s_submenuWindowStart=0;
bool submenu_initialized=true;
bool rawTouch=false;
bool readTouchXY(int& x,int& y) {x=180;y=300;return rawTouch;}
'''

fixture += "\n".join(function(name) for name in [
    "layoutTouchNavBtns", "fitTouchNavLabel", "drawTouchNavBar",
    "submenuRowCapacity", "submenuHasOverflow", "updateSubmenuWindow", "submenuItemY",
    "readSubmenuTouchPress",
])
fixture += r'''
int main() {
 assert(fitTouchNavLabel(nullptr,40,2).empty());
 assert(fitTouchNavLabel("Tools",0,2).empty());
 assert(fitTouchNavLabel("Long text",10,2).empty());
 assert(fitTouchNavLabel("Home",40,2)=="Home");
 assert(tft.textWidth(fitTouchNavLabel("Long title",40,2),2)<=40);
 // Entering a feature after a large-font screen must reset the inherited size.
 tft.font=2; tft.size=3;
 const char* labels[]={"Previous","Details","Select","Up","Next"};
 for(int i=0;i<5;++i) s_touchNavLabels[i]=labels[i];
 drawTouchNavBar();
 assert(tft.text.size()==5 && tft.size==1 && tft.font==1);
 for(size_t i=0;i<tft.text.size();++i) {
   const auto& text=tft.text[i];
   assert(text.x==int(i)*48+24);
   assert(text.width<=40); // four pixels clear on either side of the full target
   assert(text.y>=268 && text.y<320);
 }
 // Unassigned center/side slots must have no actionable-looking placeholder.
 tft.text.clear(); tft.icons=0;
 s_touchNavLabels[0]=nullptr; s_touchNavLabels[2]="";
 drawTouchNavBar();
 assert(tft.text.size()==3 && tft.icons==0);
 s_touchNavLabelsConfigured=false;
 for(auto& label:s_touchNavLabels) label=nullptr;
 drawTouchNavBar(); assert(tft.icons==5);

 // All installed apps can be revealed; hidden rows cannot steal footer taps.
 assert(submenuRowCapacity()==8 && submenuHasOverflow());
 for(int selected=0;selected<12;++selected) {
   current_submenu_index=selected; updateSubmenuWindow();
   assert(submenuItemY(selected)>=30 && submenuItemY(selected)+28<=268);
   int visible=0;
   for(int i=0;i<12;++i) {
     const int y=submenuItemY(i);
     if(y>=0) {++visible; assert(y+28<290);}
   }
   assert(visible<=8); assert(submenuItemY(12)==290);
 }
 current_submenu_index=12; updateSubmenuWindow();
 assert(s_submenuWindowStart==8); // Home retains the current page.
 current_submenu_index=0; updateSubmenuWindow(); assert(s_submenuWindowStart==0);
 current_submenu_index=11; updateSubmenuWindow();
 active_submenu_size=8; current_submenu_index=7; updateSubmenuWindow();
 assert(!submenuHasOverflow() && s_submenuWindowStart==0); // entering another menu
 assert(submenuItemY(7)==290);
 // Holding the page key flips once; release and a new press can flip again.
 bool held=false; int x=0,y=0;
 assert(!readSubmenuTouchPress(x,y,held));
 rawTouch=true; assert(readSubmenuTouchPress(x,y,held));
 for(int i=0;i<100;++i) assert(!readSubmenuTouchPress(x,y,held));
 rawTouch=false; assert(!readSubmenuTouchPress(x,y,held));
 rawTouch=true; assert(readSubmenuTouchPress(x,y,held));
 puts("Menu text bounds, inactive controls and overflow navigation passed");
}
'''
with tempfile.TemporaryDirectory(prefix="sharkdiv-menu-test-") as temp:
    cpp = Path(temp) / "test.cpp"
    binary = Path(temp) / "test"
    cpp.write_text(fixture)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
