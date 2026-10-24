#include "../sharkdiv/FlockGpsParser.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
static std::string sentence(const char* body) {
  unsigned check=0;for(const char* p=body;*p;++p)check^=static_cast<unsigned char>(*p);
  char hex[4];snprintf(hex,sizeof(hex),"*%02X",check);
  return std::string("$")+body+hex;
}
int main(){
  using namespace FlockGpsParser;
  Fix f;
  auto gga=sentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  assert(parse(gga.c_str(),f)==ValidFix);assert(fabs(f.latitude-48.1173)<0.00001);
  assert(fabs(f.longitude-11.5166667)<0.00001);assert(!strcmp(f.utc,"12:35:19"));
  auto rmc=sentence("GNRMC,091205.00,A,3351.1234,S,15112.1234,E,0.0,0.0,241026,,,A");
  assert(parse(rmc.c_str(),f)==ValidFix);assert(f.latitude<0&&f.longitude>151);
  assert(!strcmp(f.utc,"09:12:05"));
  auto nofix=sentence("GPGGA,123520,,,,,0,00,99.9,,,,,,");
  assert(parse(nofix.c_str(),f)==NoFix);
  auto invalid=sentence("GNRMC,091206,V,3351.1234,S,15112.1234,E,0,0,241026,,,A");
  assert(parse(invalid.c_str(),f)==NoFix);
  auto empty=sentence("GPGGA,123519,,N,01131.000,E,1,08,0.9,0,M,0,M,,");
  assert(parse(empty.c_str(),f)==NoFix); // empty fields never shift
  gga.back()=gga.back()=='0'?'1':'0';assert(parse(gga.c_str(),f)==Ignored);
  assert(parse("$GPGGA,broken",f)==Ignored);
  auto zero=sentence("GPGGA,000000,0000.000,N,00000.000,E,1,08,1,0,M,0,M,,");
  assert(parse(zero.c_str(),f)==ValidFix&&f.latitude==0&&f.longitude==0);
  float v;
  assert(!coordinate("1260.00","N",true,v));
  assert(!coordinate("9100.00","N",true,v));
  assert(!coordinate("1e100","E",false,v));
  assert(!coordinate("1250x","N",true,v));
  puts("Flock NMEA tests passed");
}
