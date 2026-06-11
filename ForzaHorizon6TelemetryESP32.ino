/*
  Forza Horizon 6 Telemetry Live Dashboard for ESP32-S3
  Version 2.2.0

  Standalone project:
    - Receives FH6 Data Out UDP packets on the local network.
    - Serves a browser dashboard from the ESP32.
    - Pushes live telemetry to connected browsers with WebSocket.

  Required Arduino libraries:
    - WebSockets by Markus Sattler
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#endif

#include "car_data.h"

#ifndef FH6_WIFI_SSID
#define FH6_WIFI_SSID ""
#endif

#ifndef FH6_WIFI_PASSWORD
#define FH6_WIFI_PASSWORD ""
#endif

#ifndef FH6_UDP_PORT
#define FH6_UDP_PORT 7777
#endif

static const char* WIFI_SSID = FH6_WIFI_SSID;
static const char* WIFI_PASSWORD = FH6_WIFI_PASSWORD;
static const uint16_t UDP_PORT = FH6_UDP_PORT;

static const uint16_t HTTP_PORT = 80;
static const uint16_t WS_PORT = 81;
static const size_t FH6_PACKET_SIZE = 324;
static const uint32_t WIFI_RETRY_MS = 10000;
static const uint32_t WS_PUSH_MS = 16;
static const uint32_t SIGNAL_TIMEOUT_MS = 1500;
static const uint32_t STATUS_PRINT_MS = 5000;

WiFiUDP udp;
WebServer server(HTTP_PORT);
WebSocketsServer webSocket(WS_PORT);

uint8_t packetBuffer[FH6_PACKET_SIZE];
uint32_t lastWifiAttemptMs = 0;
uint32_t lastPacketMs = 0;
uint32_t lastPushMs = 0;
uint32_t lastStatusPrintMs = 0;
uint32_t packetsReceived = 0;
uint32_t packetsRejected = 0;
uint32_t websocketClients = 0;
bool networkServicesStarted = false;
bool routesRegistered = false;
wl_status_t previousWiFiStatus = WL_IDLE_STATUS;

struct Telemetry {
  int32_t isRaceOn = 0;
  uint32_t timestampMs = 0;
  float engineMaxRpm = 0;
  float engineIdleRpm = 0;
  float currentEngineRpm = 0;
  float accelerationX = 0;
  float accelerationY = 0;
  float accelerationZ = 0;
  float velocityX = 0;
  float velocityY = 0;
  float velocityZ = 0;
  float angularVelocityX = 0;
  float angularVelocityY = 0;
  float angularVelocityZ = 0;
  float yaw = 0;
  float pitch = 0;
  float roll = 0;
  float suspension[4] = {0, 0, 0, 0};
  float tireSlipRatio[4] = {0, 0, 0, 0};
  float wheelRotationSpeed[4] = {0, 0, 0, 0};
  int32_t wheelOnRumbleStrip[4] = {0, 0, 0, 0};
  int32_t wheelInPuddle[4] = {0, 0, 0, 0};
  float surfaceRumble[4] = {0, 0, 0, 0};
  float tireSlipAngle[4] = {0, 0, 0, 0};
  float tireCombinedSlip[4] = {0, 0, 0, 0};
  float suspensionMeters[4] = {0, 0, 0, 0};
  int32_t carOrdinal = 0;
  int32_t carClass = 0;
  int32_t carPerformanceIndex = 0;
  int32_t drivetrainType = 0;
  int32_t numCylinders = 0;
  uint32_t carGroup = 0;
  float smashableVelDiff = 0;
  float smashableMass = 0;
  float positionX = 0;
  float positionY = 0;
  float positionZ = 0;
  float speed = 0;
  float power = 0;
  float torque = 0;
  float tireTemp[4] = {0, 0, 0, 0};
  float boost = 0;
  float fuel = 0;
  float distanceTraveled = 0;
  float bestLap = 0;
  float lastLap = 0;
  float currentLap = 0;
  float currentRaceTime = 0;
  uint16_t lapNumber = 0;
  uint8_t racePosition = 0;
  uint8_t accel = 0;
  uint8_t brake = 0;
  uint8_t clutch = 0;
  uint8_t handBrake = 0;
  uint8_t gear = 0;
  int8_t steer = 0;
  int8_t normalizedDrivingLine = 0;
  int8_t normalizedAiBrakeDifference = 0;
};

Telemetry latest;

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<title>FH6 Telemetry</title>
<style>
:root{color-scheme:dark;--bg:#05090d;--panel:#0b1218;--panel2:#0e1a20;--line:#23404a;--text:#edf8ff;--muted:#83a7b4;--green:#58ff9a;--cyan:#38d8ff;--accent:#58ff9a;--accent2:#38d8ff;--accent-rgb:88,255,154;--accent2-rgb:56,216,255;--amber:#ffd45c;--red:#ff4f72;--violet:#a986ff}
	*{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at 50% 104%,rgba(var(--accent-rgb),.13),transparent 38%),linear-gradient(135deg,#071016 0,#05090d 46%,#0b1114 100%);color:var(--text);font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;letter-spacing:0;overflow-x:hidden}
	body:before{content:"";position:fixed;inset:0;pointer-events:none;background:linear-gradient(rgba(var(--accent2-rgb),.055) 1px,transparent 1px),linear-gradient(90deg,rgba(var(--accent-rgb),.045) 1px,transparent 1px);background-size:48px 48px;mask-image:linear-gradient(180deg,rgba(0,0,0,.85),rgba(0,0,0,.22));}
main{width:100%;height:100dvh;min-height:0;margin:0 auto;padding:var(--dash-pad,10px);display:grid;grid-template-rows:auto minmax(0,1fr) var(--trace-h,108px) var(--foot-h,72px);gap:var(--dash-gap,8px);overflow:hidden}
	.top{position:relative;z-index:6;display:grid;grid-template-columns:1fr auto;align-items:center;gap:10px;min-height:44px}.brand{display:flex;align-items:baseline;gap:12px}.brand h1{font-size:clamp(22px,3vw,38px);margin:0;font-weight:900;letter-spacing:0}.brand span{color:var(--muted);font-size:13px}.status{position:relative;z-index:7;display:flex;gap:8px;align-items:center;justify-content:flex-end;flex-wrap:wrap}.pill,.iconBtn{border:1px solid var(--line);background:rgba(7,14,18,.82);padding:7px 10px;border-radius:999px;color:var(--muted);font-size:12px;font-weight:900;box-shadow:inset 0 0 18px rgba(var(--accent2-rgb),.08)}.iconBtn{position:relative;z-index:8;width:34px;height:34px;padding:0;color:var(--text);cursor:pointer;pointer-events:auto;touch-action:manipulation;-webkit-tap-highlight-color:transparent}#age{min-width:64px;text-align:center;font-variant-numeric:tabular-nums}.pill.live{color:#03120b;background:var(--accent);border-color:var(--accent)}.pill.warn{color:#18070b;background:var(--red);border-color:var(--red)}.label,.mini strong,.row span,.row b,.temp,.unit{white-space:nowrap}
	.dash{min-height:0;display:grid;grid-template-columns:minmax(300px,.95fr) minmax(480px,1.55fr) minmax(300px,.95fr);gap:10px}.dash>*{min-width:0;min-height:0}.panel{min-width:0;min-height:0;overflow:hidden;background:linear-gradient(180deg,rgba(14,26,32,.94),rgba(6,11,15,.96));border:1px solid var(--line);border-radius:8px;padding:10px;box-shadow:0 14px 42px rgba(0,0,0,.32),inset 0 1px rgba(255,255,255,.04),0 0 28px rgba(var(--accent-rgb),.045)}.stack{min-height:0;display:grid;grid-template-rows:minmax(0,1.08fr) minmax(0,.92fr);gap:10px}.label{font-size:10px;color:var(--muted);font-weight:900;text-transform:uppercase}.big{font-size:clamp(76px,12vw,148px);line-height:.82;font-weight:950;font-variant-numeric:tabular-nums}.unit{color:var(--muted);font-size:17px;font-weight:900}.speedbox{min-height:0;display:grid;place-items:center;text-align:center;position:relative;overflow:hidden}.speedbox:before{content:"";position:absolute;inset:12% 7%;border:1px solid rgba(var(--accent2-rgb),.28);clip-path:polygon(8% 0,92% 0,100% 18%,100% 82%,92% 100%,8% 100%,0 82%,0 18%)}.speedbox:after{content:"";position:absolute;inset:auto 8% 13% 8%;height:24%;background:linear-gradient(90deg,transparent,var(--accent2),var(--accent),var(--amber),transparent);filter:blur(28px);opacity:.3;transform:skewX(var(--tilt,0deg))}.speedbox>*{position:relative}.gear{font-size:clamp(48px,7vw,76px);font-weight:950;line-height:1}.rpmbar{height:11px;background:#05090d;border:1px solid var(--line);border-radius:999px;overflow:hidden}.rpmfill{height:100%;width:0;background:linear-gradient(90deg,var(--accent2),var(--accent),var(--amber),var(--red));border-radius:999px}.row{display:grid;grid-template-columns:1fr auto;align-items:center;gap:8px;min-width:0}.metric{display:grid;gap:5px;min-height:0}.value{font-size:20px;font-weight:900;font-variant-numeric:tabular-nums}.bar{height:9px;background:#05090d;border:1px solid var(--line);border-radius:999px;overflow:hidden}.fill{height:100%;width:0;background:var(--accent2);border-radius:999px}.fill.brake{background:var(--red)}.fill.throttle{background:var(--accent)}.fill.clutch{background:var(--amber)}
.tires{display:grid;grid-template-columns:1fr 1fr;gap:8px}.tire{border:1px solid var(--line);border-radius:8px;padding:6px;background:rgba(5,9,13,.82);min-height:0;overflow:hidden}.tire .temp{display:block;width:5.6ch;max-width:100%;font-size:clamp(16px,1.3vw,24px);line-height:1;font-weight:950;font-variant-numeric:tabular-nums}.slip{height:5px;background:#030608;border-radius:999px;overflow:hidden;margin-top:4px}.slip i{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--green),var(--amber),var(--red))}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px;min-height:0}.mini{min-width:0;overflow:hidden;background:linear-gradient(180deg,rgba(11,18,24,.96),rgba(5,9,13,.96));border:1px solid var(--line);border-radius:8px;padding:9px;min-height:0}.mini strong{display:block;font-size:clamp(16px,1.25vw,20px);line-height:1.08;margin-top:3px;font-variant-numeric:tabular-nums;overflow:hidden;text-overflow:ellipsis}.steerwrap{height:86px;display:grid;grid-template-columns:58px 1fr;gap:10px;align-items:center}.wheel{width:54px;height:54px;border:6px solid var(--text);border-radius:50%;position:relative;transform:rotate(0deg);transition:transform .08s linear;box-shadow:0 0 18px rgba(56,216,255,.25)}.wheel:before{content:"";position:absolute;left:50%;top:8px;transform:translateX(-50%);width:7px;height:30px;background:var(--amber);border-radius:999px;box-shadow:0 0 10px rgba(255,212,92,.8)}.wheel:after{content:"";position:absolute;left:50%;top:15px;transform:translateX(-50%);width:31px;height:7px;border-radius:999px;background:var(--amber);box-shadow:0 0 10px rgba(255,212,92,.8)}.steerBox{display:grid;gap:5px;min-width:0}.steerMeta{display:flex;justify-content:space-between;gap:8px;color:var(--muted);font-size:10px;font-weight:900}.steerValue{color:var(--text);font-variant-numeric:tabular-nums}.steerBar{height:11px;position:relative;border:1px solid var(--line);border-radius:999px;background:#05090d;overflow:hidden}.steerBar:before{content:"";position:absolute;left:50%;top:0;bottom:0;width:1px;background:rgba(237,248,255,.4)}.steerFill{position:absolute;top:0;bottom:0;left:50%;width:0;background:linear-gradient(90deg,var(--violet),var(--cyan));border-radius:999px}
canvas{width:100%;height:100%;background:rgba(5,9,13,.9);border:1px solid var(--line);border-radius:8px}.gauge{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);width:min(112%,800px);height:auto;aspect-ratio:720/460;background:transparent;border:0;border-radius:0}.readout{display:grid;gap:8px;place-items:center}.speedbox .big{font-size:clamp(50px,7.6vw,98px)}.speedbox .value{font-size:24px}.speedbox .rpmbar{height:8px;width:min(300px,58vw);margin:auto}.peakStrip{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:clamp(4px,.7vmin,8px);width:min(430px,70%);margin-top:clamp(4px,1dvh,12px);align-items:stretch}.peakStrip div{min-width:0;padding:clamp(2px,.5dvh,5px) clamp(4px,.7vmin,8px);border:1px solid rgba(var(--accent2-rgb),.18);border-radius:6px;background:rgba(4,10,13,.58);box-shadow:inset 0 0 14px rgba(var(--accent-rgb),.05)}.peakStrip span{display:block;color:var(--muted);font-size:clamp(7px,.85vw,8px);line-height:1;font-weight:900;text-transform:uppercase;overflow:hidden;text-overflow:ellipsis}.peakStrip b{display:block;margin-top:2px;font-size:clamp(10px,min(1.05vw,1.9dvh),13px);line-height:1.05;font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden;text-overflow:clip}.tireMap{height:calc(100% - 16px);min-height:0;display:grid;grid-template-columns:minmax(0,1fr) 36px minmax(0,1fr);grid-template-rows:minmax(0,1fr) minmax(0,1fr);gap:6px;align-items:stretch}.carCore{grid-column:2;grid-row:1/3;border:1px solid rgba(56,216,255,.28);border-radius:18px;background:linear-gradient(180deg,rgba(56,216,255,.08),rgba(88,255,154,.05));box-shadow:0 0 18px rgba(56,216,255,.16);position:relative}.carCore:before{content:"";position:absolute;left:50%;top:12px;bottom:12px;transform:translateX(-50%);width:20px;border:2px solid rgba(237,248,255,.3);border-radius:16px 16px 10px 10px;background:linear-gradient(180deg,rgba(56,216,255,.2),transparent 45%,rgba(88,255,154,.14))}.carCore:after{content:"";position:absolute;left:5px;right:5px;top:50%;height:1px;background:rgba(237,248,255,.22);box-shadow:0 -42px rgba(237,248,255,.16),0 42px rgba(237,248,255,.16)}.tire{min-width:0;position:relative;background:linear-gradient(180deg,rgba(5,9,13,.72),rgba(5,9,13,.94)),linear-gradient(90deg,var(--thermal,#38d8ff),transparent)!important;box-shadow:inset 0 -3px 0 var(--thermal,#38d8ff),0 0 var(--glow,0px) var(--thermal,#38d8ff)}.tire:after{content:"";position:absolute;top:16px;bottom:16px;width:5px;border-radius:999px;background:var(--thermal,#38d8ff);opacity:.86;box-shadow:0 0 12px var(--thermal,#38d8ff)}.tire:nth-child(1):after,.tire:nth-child(4):after{right:-4px}.tire:nth-child(3):after,.tire:nth-child(5):after{left:-4px}.tire.hot .temp{color:var(--amber)}.tire.critical .temp{color:var(--red)}.tireMeta{display:flex;justify-content:space-between;color:var(--muted);font-size:7px;font-weight:900;margin-top:2px}.wet{height:4px;margin-top:3px;border-radius:999px;background:#071018;border:1px solid rgba(56,216,255,.25);overflow:hidden}.wet i{display:block;height:100%;width:0;background:linear-gradient(90deg,#38d8ff,#a986ff)}.tire.wetOn{border-color:rgba(56,216,255,.8);box-shadow:inset 0 -3px 0 var(--thermal,#38d8ff),0 0 14px rgba(56,216,255,.42)}.rightStack{grid-template-rows:minmax(224px,.92fr) minmax(0,1.08fr)}.dynPanel{display:grid;grid-template-columns:minmax(112px,.74fr) minmax(0,1fr);gap:8px;align-items:center;overflow:hidden}.gRadar{width:100%;height:auto;aspect-ratio:1/1;min-height:0;align-self:center}.dynStats{min-width:0;display:grid;grid-template-columns:1fr 1fr;gap:8px}.dynStats .mini{padding:7px;min-height:0}.dynStats .mini strong{font-size:clamp(12px,1.05vw,16px);line-height:1.05;min-width:0;text-align:right}.signed.negative{color:var(--cyan)}.signed.positive{color:var(--green)}.foot{display:grid;grid-template-columns:1.4fr 1fr 1fr 1fr;gap:8px;min-height:0}.foot .mini{display:grid;align-content:center}.muted{color:var(--muted)}.settings{display:none;position:fixed;inset:0;z-index:20;background:rgba(0,0,0,.55);place-items:center}.settings.open{display:grid}.settingsBox{width:min(520px,92vw);background:#071018;border:1px solid var(--line);border-radius:8px;padding:16px;box-shadow:0 28px 80px rgba(0,0,0,.6)}.settingsHead{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}.seg,.swatches{display:flex;gap:8px;flex-wrap:wrap}.seg button,.swatch{border:1px solid var(--line);background:#0c151b;color:var(--text);border-radius:999px;padding:8px 12px;font-weight:900;cursor:pointer}.swatch{width:42px;height:28px;padding:0;background:var(--sw)}.seg button.active{background:var(--green);color:#03120b}@media(max-width:980px){main{height:auto;min-height:100vh;overflow:visible}.dash{grid-template-columns:1fr}.stack,.rightStack{grid-template-rows:auto}.speedbox{min-height:420px}.foot{grid-template-columns:1fr 1fr}.top{grid-template-columns:1fr}}@media(max-width:520px){main{padding:10px}.panel{padding:10px}.grid2,.foot{grid-template-columns:1fr}.big{font-size:76px}.brand{display:block}.status{justify-content:flex-start}}
.dash{--dash-density:1}.dash,.panel,.stack,.grid2,.tireMap,.dynPanel,.foot{contain:layout}.panel{padding:clamp(6px,.75vmin,10px)}.top{min-height:clamp(34px,5dvh,44px)}.brand h1{font-size:clamp(22px,3vw,38px);line-height:1}.pill{padding:clamp(5px,.8vmin,7px) clamp(8px,1vmin,10px)}.iconBtn{width:clamp(32px,4dvh,34px);height:clamp(32px,4dvh,34px)}.stack{gap:var(--dash-gap,8px)}.metric{gap:clamp(3px,.55dvh,5px)}.row{font-size:clamp(14px,1.35vw,16px)}.value{font-size:clamp(16px,1.55vw,20px)}.speedbox .big{font-size:clamp(56px,min(7.6vw,13dvh),98px)}.speedbox .value{font-size:clamp(18px,2.7dvh,24px)}.gear{font-size:clamp(48px,8dvh,76px)}.gauge{width:min(112%,calc(92dvh - var(--trace-h,108px) - var(--foot-h,72px)))}.tireMap{gap:clamp(4px,.7vmin,6px);grid-template-columns:minmax(0,1fr) clamp(30px,3vw,36px) minmax(0,1fr)}.tire{padding:clamp(4px,.65vmin,6px);display:grid;grid-template-rows:auto auto auto auto auto auto;align-content:space-evenly}.tire .temp{font-size:clamp(15px,min(1.3vw,3dvh),24px)}.tireMeta{font-size:clamp(6px,.85vw,8px);line-height:1.05}.slip{height:clamp(4px,.7dvh,5px);margin-top:clamp(2px,.45dvh,4px)}.wet{height:clamp(3px,.6dvh,4px);margin-top:clamp(2px,.45dvh,3px)}.carCore:before{top:clamp(8px,1.6dvh,12px);bottom:clamp(8px,1.6dvh,12px)}.mini{padding:clamp(6px,.75vmin,9px)}.mini strong{font-size:clamp(14px,min(1.25vw,2.5dvh),20px)}.dynPanel{grid-template-columns:minmax(0,.95fr) minmax(112px,1.05fr);gap:clamp(6px,.8vmin,10px);align-items:stretch;align-content:stretch}.gRadar{align-self:stretch;justify-self:stretch;width:100%;height:100%;aspect-ratio:auto}.dynStats{height:100%;gap:clamp(5px,.7vmin,8px);grid-template-columns:1fr;grid-template-rows:repeat(4,minmax(0,1fr))}.dynStats .mini{display:grid;align-content:center}.dynStats .mini strong{font-size:clamp(12px,min(1.05vw,2.2dvh),16px);text-align:right}#trace{display:block;height:100%}.foot .mini strong{font-size:clamp(14px,min(1.25vw,2.5dvh),20px)}@media(min-width:981px){:root{--dash-pad:clamp(6px,.9dvh,10px);--dash-gap:clamp(5px,.9dvh,8px);--trace-h:clamp(64px,12dvh,108px);--foot-h:clamp(54px,8dvh,72px)}.dash{grid-template-columns:minmax(280px,.95fr) minmax(420px,1.55fr) minmax(280px,.95fr)}.stack{grid-template-rows:minmax(0,1fr) minmax(0,.72fr)}.rightStack{grid-template-rows:minmax(188px,.92fr) minmax(188px,1.08fr)}.grid2{grid-template-rows:repeat(2,minmax(0,1fr))}.tireMap{height:calc(100% - clamp(14px,2dvh,18px))}}@media(min-width:1400px) and (min-height:800px){:root{--dash-pad:12px;--dash-gap:10px;--trace-h:118px;--foot-h:86px}.brand h1{font-size:clamp(38px,2.65vw,48px)}.pill{font-size:14px;padding:8px 14px}.iconBtn{width:40px;height:40px}.panel{padding:12px}.label{font-size:12px}.row{font-size:22px}.value{font-size:28px}.bar{height:12px}.steerwrap{height:104px;grid-template-columns:70px 1fr}.wheel{width:64px;height:64px;border-width:7px}.wheel:before{height:36px;width:9px}.wheel:after{width:38px;height:9px}.steerMeta{font-size:12px}.grid2{gap:10px}.mini strong{font-size:clamp(23px,1.55vw,29px)}.speedbox .big{font-size:clamp(108px,6.8vw,132px)}.speedbox .value{font-size:30px}.speedbox .rpmbar{width:min(430px,48vw);height:11px}.unit{font-size:25px}.gear{font-size:90px}.peakStrip{width:min(520px,72%)}.peakStrip span{font-size:10px}.peakStrip b{font-size:17px}.tireMap{grid-template-columns:minmax(0,1fr) 44px minmax(0,1fr);gap:9px}.tire .temp{font-size:clamp(28px,1.9vw,34px)}.tireMeta{font-size:9px}.slip{height:7px}.wet{height:6px}.dynStats .mini strong{font-size:clamp(19px,1.25vw,23px)}.foot .mini strong{font-size:clamp(23px,1.55vw,29px)}}@media(min-width:981px) and (max-height:760px){:root{--dash-pad:6px;--dash-gap:5px;--trace-h:clamp(54px,10dvh,72px);--foot-h:clamp(48px,7dvh,58px)}.top{min-height:32px}.brand h1{font-size:clamp(22px,3vw,31px)}.pill{font-size:11px;padding:5px 8px}.iconBtn{width:32px;height:32px}.metric{gap:2px}.steerwrap{height:58px;grid-template-columns:44px 1fr}.wheel{width:42px;height:42px;border-width:5px}.wheel:before{top:6px;height:22px}.wheel:after{top:12px;width:24px}.mini strong{font-size:clamp(13px,1.15vw,17px)}.tire .temp{font-size:clamp(14px,1.15vw,19px)}.tireMeta{font-size:6px}.carCore:after{box-shadow:0 -32px rgba(237,248,255,.16),0 32px rgba(237,248,255,.16)}.dynPanel{grid-template-columns:minmax(0,.95fr) minmax(112px,1.05fr)}}@media(min-width:981px) and (max-height:660px){:root{--trace-h:46px;--foot-h:44px}.brand h1{font-size:26px}.row{font-size:13px}.bar{height:7px}.speedbox .big{font-size:clamp(50px,11dvh,78px)}.peakStrip{width:min(360px,64%);margin-top:3px}.peakStrip div{padding:2px 4px}.peakStrip span{display:none}.peakStrip b{font-size:10px}.tireMap{grid-template-columns:minmax(0,1fr) 28px minmax(0,1fr);gap:4px}.tire{padding:4px}.tire .temp{font-size:14px}.tireMeta:nth-of-type(2),.wet{display:none}.rightStack{grid-template-rows:minmax(150px,.8fr) minmax(150px,1fr)}}@media(max-width:980px){main{height:auto;min-height:100dvh;overflow:visible}.dash{grid-template-columns:1fr}.stack,.rightStack{grid-template-rows:auto}.speedbox{min-height:420px}.foot{grid-template-columns:1fr 1fr}.top{grid-template-columns:1fr}}@media(max-width:520px){main{padding:10px}.panel{padding:10px}.grid2,.foot{grid-template-columns:1fr}.big{font-size:76px}.brand{display:block}.status{justify-content:flex-start}}
@media(min-width:1180px) and (max-width:1300px) and (min-height:1020px) and (max-height:1120px){:root{--dash-pad:8px;--dash-gap:7px;--trace-h:96px;--foot-h:68px}.brand h1{font-size:36px}.pill{font-size:13px;padding:7px 11px}.iconBtn{width:38px;height:38px}.dash{grid-template-columns:minmax(254px,.9fr) minmax(426px,1.5fr) minmax(254px,.9fr);gap:7px}.panel{padding:8px}.label{font-size:11px}.row{font-size:17px}.value{font-size:22px}.bar{height:10px}.steerwrap{height:80px;grid-template-columns:56px 1fr}.wheel{width:52px;height:52px;border-width:6px}.speedbox .big{font-size:92px}.speedbox .value{font-size:24px}.speedbox .rpmbar{width:min(360px,48vw);height:9px}.gear{font-size:74px}.peakStrip{width:min(420px,68%)}.peakStrip span{font-size:8px}.peakStrip b{font-size:13px}.tireMap{grid-template-columns:minmax(0,1fr) 34px minmax(0,1fr);gap:6px}.tire .temp{font-size:23px}.tireMeta{font-size:7px}.dynPanel{grid-template-columns:minmax(0,.9fr) minmax(118px,1.1fr)}.dynStats .mini strong{font-size:16px}.foot .mini strong,.mini strong{font-size:20px}}
@media(max-width:980px) and (orientation:landscape){:root{--dash-pad:3px;--dash-gap:3px;--trace-h:34px;--foot-h:30px}body{height:100dvh;min-height:0;overflow:hidden}main{height:100dvh;min-height:0;overflow:hidden;padding:3px;grid-template-rows:22px minmax(0,1fr) 34px 30px;gap:3px}.top{grid-template-columns:minmax(0,1fr) auto;min-height:22px;gap:4px}.brand h1{font-size:17px;line-height:1;white-space:nowrap}.status{gap:3px;flex-wrap:nowrap}.pill{font-size:7px;line-height:1;padding:4px 5px;min-width:auto}.iconBtn{width:22px;height:22px;font-size:10px}.dash{display:grid;grid-template-columns:minmax(0,.88fr) minmax(0,1.42fr) minmax(0,.9fr);gap:3px;min-height:0}.panel{padding:3px;border-radius:5px}.stack{display:grid;grid-template-rows:minmax(0,1.02fr) minmax(0,.72fr);gap:3px}.rightStack{display:grid;grid-template-rows:minmax(0,.92fr) minmax(0,1fr);gap:3px}.metric{gap:1px}.row{font-size:8px;gap:3px}.value{font-size:10px}.label{font-size:6px;line-height:1}.bar{height:4px}.steerwrap{height:28px;grid-template-columns:24px minmax(0,1fr);gap:4px}.wheel{width:22px;height:22px;border-width:3px}.wheel:before{top:3px;width:3px;height:13px}.wheel:after{top:7px;width:14px;height:3px}.steerMeta{font-size:6px}.steerBar{height:5px}.grid2{gap:3px;grid-template-columns:1fr 1fr;grid-template-rows:repeat(2,minmax(0,1fr))}.mini{padding:3px;border-radius:5px}.mini strong{font-size:10px;line-height:1.05;margin-top:1px}.speedbox{min-height:0}.speedbox:before{inset:8% 4%}.gauge{width:min(112%,330px)}.readout{gap:3px}.speedbox .big{font-size:42px;line-height:.78}.speedbox .value{font-size:12px}.unit{font-size:10px}.gear{font-size:31px}.speedbox .rpmbar{width:min(150px,42vw);height:4px}.peakStrip{width:min(190px,72%);gap:3px;margin-top:3px}.peakStrip div{padding:2px 3px;border-radius:4px}.peakStrip span{display:none}.peakStrip b{font-size:7px}.tireMap{height:calc(100% - 8px);grid-template-columns:minmax(0,1fr) 18px minmax(0,1fr);gap:3px}.tire{padding:2px;border-radius:5px;grid-template-rows:auto auto auto auto;align-content:space-evenly}.tire .temp{font-size:8px;line-height:1;width:5.8ch}.tireMeta{font-size:5px}.tireMeta:nth-of-type(2),.wet{display:none}.slip{height:3px;margin-top:1px}.carCore{border-radius:10px}.carCore:before{width:10px;top:6px;bottom:6px}.carCore:after{left:3px;right:3px;box-shadow:0 -22px rgba(237,248,255,.16),0 22px rgba(237,248,255,.16)}.tire:after{top:8px;bottom:8px;width:3px}.dynPanel{grid-template-columns:minmax(0,.82fr) minmax(0,1.18fr);gap:3px}.dynStats{grid-template-columns:1fr 1fr;grid-template-rows:repeat(2,minmax(0,1fr));gap:3px}.dynStats .mini strong{font-size:8px}.foot{grid-template-columns:1.35fr .9fr .9fr 1fr;gap:3px}.foot .mini strong{font-size:10px}canvas{border-radius:5px}#trace{height:100%}}
@media(min-width:2200px) and (min-height:1200px){:root{--dash-pad:18px;--dash-gap:14px;--trace-h:150px;--foot-h:112px}.brand h1{font-size:64px}.pill{font-size:18px;padding:11px 18px}.iconBtn{width:54px;height:54px;font-size:20px}.panel{padding:18px}.label{font-size:15px}.row{font-size:30px}.value{font-size:38px}.bar{height:16px}.steerwrap{height:134px;grid-template-columns:92px 1fr}.wheel{width:82px;height:82px;border-width:9px}.wheel:before{height:48px;width:11px}.wheel:after{width:50px;height:11px}.steerMeta{font-size:16px}.mini strong{font-size:38px}.speedbox .big{font-size:178px}.speedbox .value{font-size:40px}.speedbox .rpmbar{width:min(620px,48vw);height:15px}.unit{font-size:34px}.gear{font-size:118px}.peakStrip{width:min(720px,72%)}.peakStrip span{font-size:13px}.peakStrip b{font-size:24px}.tireMap{grid-template-columns:minmax(0,1fr) 58px minmax(0,1fr);gap:14px}.tire .temp{font-size:44px}.tireMeta{font-size:12px}.slip{height:9px}.wet{height:8px}.dynStats .mini strong,.foot .mini strong{font-size:34px}}
</style>


</head>
<body>
<main>
  <section class="top">
	    <div class="brand"><h1>Telemetry Live Dashboard</h1></div>
    <div class="status"><span id="state" class="pill warn">NO SIGNAL</span><span id="hz" class="pill">0 Hz</span><span id="age" class="pill">-- ms</span><button id="settingsBtn" class="iconBtn" type="button">⚙</button></div>
  </section>
  <section class="dash">
    <div class="stack">
      <div class="panel metric"><div class="label">Inputs</div><div class="row"><span>Throttle</span><b id="accelText">0.0%</b></div><div class="bar"><div id="accel" class="fill throttle"></div></div><div class="row"><span>Brake</span><b id="brakeText">0.0%</b></div><div class="bar"><div id="brake" class="fill brake"></div></div><div class="row"><span>Clutch</span><b id="clutchText">0.0%</b></div><div class="bar"><div id="clutch" class="fill clutch"></div></div><div class="row"><span>Handbrake</span><b id="handText">0.0%</b></div><div class="bar"><div id="hand" class="fill"></div></div><div class="steerwrap"><div id="wheel" class="wheel"></div><div class="steerBox"><div class="steerMeta"><span>Steer</span><span id="steerText" class="steerValue">+0.0%</span></div><div class="steerBar"><div id="steerFill" class="steerFill"></div></div></div></div></div>
      <div class="panel grid2"><div class="mini"><span class="label">Power</span><strong id="power">0 PS</strong></div><div class="mini"><span class="label">Torque</span><strong id="torque">0 Nm</strong></div><div class="mini"><span class="label">Boost</span><strong id="boost">0 psi</strong></div><div class="mini"><span class="label">Fuel</span><strong id="fuel">0%</strong></div></div>
    </div>
    <div class="panel speedbox" id="speedbox"><canvas id="gauge" class="gauge" width="720" height="460"></canvas><div class="readout"><div class="label">RPM / Speed</div><div><span id="speed" class="big">0.0</span><span class="unit">km/h</span></div><div class="rpmbar"><div id="rpmfill" class="rpmfill"></div></div><div class="row" style="margin-top:6px;min-width:250px"><div><div class="label">RPM</div><div class="value" id="rpm">0</div></div><div><div class="label">Gear</div><div class="gear" id="gear">N</div></div></div><div class="peakStrip"><div><span>Peak km/h</span><b id="peakSpeed">0.0</b></div><div><span>Peak RPM</span><b id="peakRpm">0</b></div><div><span>Peak G</span><b id="peakGText">0.00</b></div></div></div></div>
    <div class="stack rightStack">
      <div class="panel"><div class="label">Tires</div><div class="tireMap"><div class="tire"><span class="label">FL</span><div id="tfl" class="temp">0C</div><div class="tireMeta"><span>TEMP</span><span>SLIP</span></div><div class="slip"><i id="sfl"></i></div><div class="tireMeta"><span>WET</span></div><div class="wet"><i id="wfl"></i></div></div><div class="carCore"></div><div class="tire"><span class="label">FR</span><div id="tfr" class="temp">0C</div><div class="tireMeta"><span>TEMP</span><span>SLIP</span></div><div class="slip"><i id="sfr"></i></div><div class="tireMeta"><span>WET</span></div><div class="wet"><i id="wfr"></i></div></div><div class="tire"><span class="label">RL</span><div id="trl" class="temp">0C</div><div class="tireMeta"><span>TEMP</span><span>SLIP</span></div><div class="slip"><i id="srl"></i></div><div class="tireMeta"><span>WET</span></div><div class="wet"><i id="wrl"></i></div></div><div class="tire"><span class="label">RR</span><div id="trr" class="temp">0C</div><div class="tireMeta"><span>TEMP</span><span>SLIP</span></div><div class="slip"><i id="srr"></i></div><div class="tireMeta"><span>WET</span></div><div class="wet"><i id="wrr"></i></div></div></div></div>
      <div class="panel dynPanel"><canvas id="gradar" class="gRadar" width="240" height="240"></canvas><div class="dynStats"><div class="mini"><span class="label">Lateral G</span><strong id="latg">0.00 G</strong></div><div class="mini"><span class="label">Long G</span><strong id="longg">0.00 G</strong></div><div class="mini"><span class="label">Roll</span><strong id="roll">0 deg</strong></div><div class="mini"><span class="label">Yaw</span><strong id="yaw">0 deg</strong></div></div></div>
    </div>
  </section>
  <section class="panel"><div class="label">Dynamics Trace</div><canvas id="trace" width="960" height="180"></canvas></section>
  <section class="foot"><div class="mini"><span class="label">Vehicle ID</span><strong id="car">--</strong></div><div class="mini"><span class="label">Class / PI</span><strong id="classpi">--</strong></div><div class="mini"><span class="label">Drivetrain</span><strong id="drive">--</strong></div><div class="mini"><span class="label">Engine / Group</span><strong id="engine">--</strong></div></section>
</main>
<div id="settings" class="settings"><div class="settingsBox"><div class="settingsHead"><strong>Display Settings</strong><button id="closeSettings" class="iconBtn" type="button">×</button></div><div class="label">View</div><div class="seg"><button id="dashView" class="active" type="button">Dashboard</button><button id="holoView" type="button">Hologram</button></div><div class="label" style="margin-top:14px">Mode</div><div class="seg"><button id="driveMode" type="button">Drive</button><button id="raceMode" type="button">Race</button></div><div class="label" style="margin-top:14px">Palette</div><div id="swatches" class="swatches"></div></div></div>
<script>
const $=id=>document.getElementById(id);
const el={state:$('state'),hz:$('hz'),age:$('age'),settingsBtn:$('settingsBtn'),settings:$('settings'),closeSettings:$('closeSettings'),dashView:$('dashView'),holoView:$('holoView'),driveMode:$('driveMode'),raceMode:$('raceMode'),swatches:$('swatches'),speed:$('speed'),gear:$('gear'),rpm:$('rpm'),rpmfill:$('rpmfill'),peakSpeed:$('peakSpeed'),peakRpm:$('peakRpm'),peakGText:$('peakGText'),speedbox:$('speedbox'),gauge:$('gauge'),gradar:$('gradar'),accel:$('accel'),brake:$('brake'),clutch:$('clutch'),hand:$('hand'),accelText:$('accelText'),brakeText:$('brakeText'),clutchText:$('clutchText'),handText:$('handText'),wheel:$('wheel'),steerText:$('steerText'),steerFill:$('steerFill'),power:$('power'),torque:$('torque'),boost:$('boost'),fuel:$('fuel'),tfl:$('tfl'),tfr:$('tfr'),trl:$('trl'),trr:$('trr'),sfl:$('sfl'),sfr:$('sfr'),srl:$('srl'),srr:$('srr'),wfl:$('wfl'),wfr:$('wfr'),wrl:$('wrl'),wrr:$('wrr'),latg:$('latg'),longg:$('longg'),roll:$('roll'),yaw:$('yaw'),car:$('car'),classpi:$('classpi'),drive:$('drive'),engine:$('engine')};
let data=null,lastRx=0,rxCount=0,lastHzAt=performance.now(),lastAgeAt=0,ageAvg=0,trace=[],peakG={x:0,y:0,t:0},peaks={speed:0,rpm:0,g:0,reset:performance.now()},mode=localStorage.getItem('fh6mode')||'drive';
const palettes=[['Green','#58ff9a','#38d8ff'],['Cyan','#38d8ff','#58ff9a'],['Blue','#4e8cff','#38d8ff'],['Violet','#a986ff','#38d8ff'],['Pink','#ff5ad9','#a986ff'],['Red','#ff4f72','#ffd45c'],['Amber','#ffd45c','#ff7b4f'],['Mono','#edf8ff','#9fb3bf']];
function hexRgb(c){const n=parseInt(c.slice(1),16);return[(n>>16)&255,(n>>8)&255,n&255].join(',')}
function applyPalette(a,b){document.documentElement.style.setProperty('--accent',a);document.documentElement.style.setProperty('--accent2',b);document.documentElement.style.setProperty('--accent-rgb',hexRgb(a));document.documentElement.style.setProperty('--accent2-rgb',hexRgb(b));document.documentElement.style.setProperty('--green',a);document.documentElement.style.setProperty('--cyan',b);localStorage.setItem('fh6palette',a+','+b)}
function setMode(m){mode=m;localStorage.setItem('fh6mode',m);el.driveMode.classList.toggle('active',m==='drive');el.raceMode.classList.toggle('active',m==='race')}
palettes.forEach(p=>{const b=document.createElement('button');b.className='swatch';b.style.setProperty('--sw',p[1]);b.title=p[0];b.onclick=()=>applyPalette(p[1],p[2]);el.swatches.appendChild(b)});{const saved=(localStorage.getItem('fh6palette')||'#58ff9a,#38d8ff').split(',');applyPalette(saved[0],saved[1]||saved[0])}setMode(mode);el.settingsBtn.onclick=()=>el.settings.classList.add('open');el.closeSettings.onclick=()=>el.settings.classList.remove('open');el.dashView.onclick=()=>location.href='/';el.holoView.onclick=()=>location.href='/holo';el.driveMode.onclick=()=>setMode('drive');el.raceMode.onclick=()=>setMode('race');el.settings.onclick=e=>{if(e.target===el.settings)el.settings.classList.remove('open')};
function fmtTime(s){if(!s||s<0.01)return'--';const m=Math.floor(s/60),r=(s-m*60).toFixed(2).padStart(5,'0');return m+':'+r}
function pct(v){return Math.max(0,Math.min(100,v/255*100))}
function cssVar(n,f){return getComputedStyle(document.documentElement).getPropertyValue(n).trim()||f}
function fitCanvas(c){const r=c.getBoundingClientRect(),d=Math.min(2,devicePixelRatio||1),w=Math.max(1,Math.round(r.width*d)),h=Math.max(1,Math.round(r.height*d));if(c.width!==w||c.height!==h){c.width=w;c.height=h}}
function setBar(node,v){node.style.width=pct(v)+'%'}
function fmtLatency(v){return String(Math.min(999,Math.max(0,Math.round(v)))).padStart(3,'0')+' ms'}
function signedFixed(v,d=2){const n=Number.isFinite(v)?v:0;return(n>=0?'+':'')+n.toFixed(d)}
function driveName(v){return ['FWD','RWD','AWD'][v]||'--'}
function piClass(pi,label){return label||'--'}
function gearName(g){return g===0?'R':String(g)}
function connect(){const ws=new WebSocket(`ws://${location.hostname}:81/`);ws.onopen=()=>{el.state.textContent='CONNECTED';el.state.className='pill live'};ws.onmessage=e=>{data=JSON.parse(e.data);lastRx=performance.now();rxCount++};ws.onclose=()=>{el.state.textContent='RECONNECTING';el.state.className='pill warn';setTimeout(connect,900)};ws.onerror=()=>ws.close()}
function drawTrace(){const c=$('trace');fitCanvas(c);const ctx=c.getContext('2d'),w=c.width,h=c.height,a=cssVar('--accent','#58ff9a'),b=cssVar('--accent2','#38d8ff');ctx.clearRect(0,0,w,h);ctx.strokeStyle='#26333a';ctx.lineWidth=1;for(let i=1;i<5;i++){ctx.beginPath();ctx.moveTo(0,h*i/5);ctx.lineTo(w,h*i/5);ctx.stroke()}ctx.strokeStyle='rgba(237,248,255,.35)';ctx.beginPath();ctx.moveTo(0,h/2);ctx.lineTo(w,h/2);ctx.stroke();const line=(key,color,scale,zero=.5)=>{ctx.strokeStyle=color;ctx.lineWidth=3;ctx.beginPath();trace.forEach((v,i)=>{const x=i/(trace.length-1||1)*w,y=h*(zero-Math.max(-1,Math.min(1,v[key]/scale))*.42);if(i)ctx.lineTo(x,y);else ctx.moveTo(x,y)});ctx.stroke()};line('lat',b,1.6);line('long',a,1.6);line('slip','#ffd45c',1,.88)}
function arc(ctx,cx,cy,r,a0,a1,color,w){ctx.beginPath();ctx.arc(cx,cy,r,a0,a1);ctx.strokeStyle=color;ctx.lineWidth=w;ctx.lineCap='round';ctx.stroke()}
function lerpColor(a,b,t){const x=n=>parseInt(n,16),r=Math.round(x(a.slice(1,3))+(x(b.slice(1,3))-x(a.slice(1,3)))*t),g=Math.round(x(a.slice(3,5))+(x(b.slice(3,5))-x(a.slice(3,5)))*t),bb=Math.round(x(a.slice(5,7))+(x(b.slice(5,7))-x(a.slice(5,7)))*t);return`rgb(${r},${g},${bb})`}
function tempColor(t){if(t<55)return lerpColor('#38d8ff','#58ff9a',Math.max(0,t-20)/35);if(t<85)return lerpColor('#58ff9a','#ffd45c',(t-55)/30);return lerpColor('#ffd45c','#ff4f72',Math.min(1,(t-85)/30))}
function drawGauge(d){const c=el.gauge,ctx=c.getContext('2d'),w=c.width,h=c.height,cx=w/2,cy=h*.58,a=cssVar('--accent','#58ff9a'),b=cssVar('--accent2','#38d8ff'),base=Math.min(w,h),r1=base*.413,r2=base*.326,r3=base*.257,stroke1=Math.max(12,base*.039),stroke2=Math.max(6,base*.017),stroke3=Math.max(5,base*.013);ctx.clearRect(0,0,w,h);const start=Math.PI*.82,end=Math.PI*2.18,rpmPct=Math.max(0,Math.min(1,d.rpm/Math.max(1,d.maxRpm))),spdPct=Math.max(0,Math.min(1,d.speedKmh/420)),pwrPct=Math.max(0,Math.min(1,d.powerPs/1400));arc(ctx,cx,cy,r1,start,end,'rgba(131,167,180,.18)',stroke1);arc(ctx,cx,cy,r1,start,start+(end-start)*rpmPct,a,stroke1);if(rpmPct>.78)arc(ctx,cx,cy,r1,start+(end-start)*.78,start+(end-start)*rpmPct,'#ffd45c',stroke1);if(rpmPct>.92)arc(ctx,cx,cy,r1,start+(end-start)*.92,start+(end-start)*rpmPct,'#ff4f72',stroke1);arc(ctx,cx,cy,r2,start,end,'rgba(131,167,180,.13)',stroke2);arc(ctx,cx,cy,r2,start,start+(end-start)*spdPct,b,stroke2);arc(ctx,cx,cy,r3,start,end,'rgba(255,212,92,.12)',stroke3);arc(ctx,cx,cy,r3,start,start+(end-start)*pwrPct,'#ffd45c',stroke3);ctx.strokeStyle='rgba(237,248,255,.28)';ctx.lineWidth=Math.max(1.5,base*.004);for(let i=0;i<=10;i++){const x=start+(end-start)*i/10,ri=r1-(i>=8?base*.061:base*.05),ro=r1-base*.017;ctx.beginPath();ctx.moveTo(cx+Math.cos(x)*ri,cy+Math.sin(x)*ri);ctx.lineTo(cx+Math.cos(x)*ro,cy+Math.sin(x)*ro);ctx.stroke()}ctx.fillStyle='rgba(237,248,255,.82)';ctx.font=`700 ${Math.max(14,base*.039)}px system-ui`;ctx.textAlign='center';ctx.fillText('RPM',cx,Math.max(28,h*.09));ctx.fillStyle='rgba(131,167,180,.9)';ctx.font=`700 ${Math.max(12,base*.03)}px system-ui`;ctx.fillText('km/h',cx-r1*.82,cy+r1*.48);ctx.fillText('PS',cx+r1*.82,cy+r1*.48)}
function drawGRadar(d){const c=el.gradar;fitCanvas(c);const ctx=c.getContext('2d'),w=c.width,h=c.height,cx=w/2,cy=h/2,r=Math.min(w,h)*.38,accent=cssVar('--accent','#58ff9a'),rgb=cssVar('--accent-rgb','88,255,154'),lat=-(d.latG||0),long=-(d.longG||0);ctx.clearRect(0,0,w,h);ctx.strokeStyle='rgba(131,167,180,.22)';ctx.lineWidth=1;for(let i=1;i<=4;i++){ctx.beginPath();ctx.arc(cx,cy,r*i/4,0,Math.PI*2);ctx.stroke()}ctx.beginPath();ctx.moveTo(cx-r,cy);ctx.lineTo(cx+r,cy);ctx.moveTo(cx,cy-r);ctx.lineTo(cx,cy+r);ctx.stroke();const mag=Math.hypot(lat,long);if(mag>=Math.hypot(peakG.x,peakG.y)||performance.now()-peakG.t>3500){peakG={x:lat,y:long,t:performance.now()}}const px=Math.max(-4,Math.min(4,peakG.x))/4*r,py=-Math.max(-4,Math.min(4,peakG.y))/4*r;ctx.fillStyle='#ff4f72';ctx.beginPath();ctx.arc(cx+px,cy+py,Math.max(4,r*.045),0,Math.PI*2);ctx.fill();const x=Math.max(-4,Math.min(4,lat))/4*r,y=-Math.max(-4,Math.min(4,long))/4*r;ctx.strokeStyle='rgba(255,212,92,.55)';ctx.lineWidth=Math.max(3,r*.035);ctx.beginPath();ctx.moveTo(cx,cy);ctx.lineTo(cx+x,cy+y);ctx.stroke();ctx.fillStyle=accent;ctx.beginPath();ctx.arc(cx+x,cy+y,Math.max(8,r*.075),0,Math.PI*2);ctx.fill();ctx.strokeStyle=`rgba(${rgb},.42)`;ctx.lineWidth=Math.max(7,r*.07);ctx.beginPath();ctx.arc(cx+x,cy+y,Math.max(8,r*.075),0,Math.PI*2);ctx.stroke();ctx.fillStyle='rgba(237,248,255,.82)';ctx.font=`700 ${Math.max(12,r*.11)}px ui-monospace,monospace`;ctx.textAlign='center';ctx.fillText('G',cx,Math.max(18,h*.08));ctx.fillStyle='rgba(131,167,180,.9)';ctx.fillText(signedFixed(lat)+', '+signedFixed(long),cx,h-Math.max(10,h*.04))}
function slipPct(v){return Math.min(100,Math.sqrt(Math.max(0,Math.abs(v)))*34)}
function updateTire(k,temp,slip,wet){const tempNode=el['t'+k],slipNode=el['s'+k],wetNode=el['w'+k],card=tempNode.parentElement,color=tempColor(temp);tempNode.textContent=temp.toFixed(1)+'℃';slipNode.style.width=slipPct(slip)+'%';wetNode.style.width=wet? '100%':'0%';card.style.setProperty('--thermal',color);card.style.setProperty('--glow',Math.round(Math.min(18,Math.sqrt(Math.max(0,Math.abs(slip)))*8))+'px');card.classList.toggle('hot',temp>=85);card.classList.toggle('critical',temp>=115||Math.abs(slip)>8);card.classList.toggle('wetOn',!!wet)}
function render(){const now=performance.now();if(now-lastHzAt>1000){el.hz.textContent=rxCount+' Hz';rxCount=0;lastHzAt=now}const socketAge=lastRx?now-lastRx:99999;const packetAge=data?data.ageMs:999999;if(data&&data.signal&&now-lastAgeAt>250){ageAvg=ageAvg?ageAvg*.82+packetAge*.18:packetAge;el.age.textContent=fmtLatency(ageAvg);lastAgeAt=now}else if(!data||!data.signal){el.age.textContent='--- ms'}if(!data||socketAge>1800||!data.signal){el.state.textContent='NO SIGNAL';el.state.className='pill warn';requestAnimationFrame(render);return}el.state.textContent=data.race?'LIVE':'PAUSED';el.state.className=data.race?'pill live':'pill';const kmh=data.speedKmh||0,latG=-(data.latG||0),longG=-(data.longG||0);el.speed.textContent=kmh.toFixed(1);el.gear.textContent=gearName(data.gear);el.rpm.textContent=Math.round(data.rpm).toLocaleString();el.rpmfill.style.width=Math.min(100,data.rpm/Math.max(1,data.maxRpm)*100)+'%';drawGauge(data);drawGRadar(data);el.speedbox.style.setProperty('--tilt',(data.steer*0.45)+'deg');setBar(el.accel,data.accel);setBar(el.brake,data.brake);setBar(el.clutch,data.clutch);setBar(el.hand,data.handBrake);el.accelText.textContent=pct(data.accel).toFixed(1)+'%';el.brakeText.textContent=pct(data.brake).toFixed(1)+'%';el.clutchText.textContent=pct(data.clutch).toFixed(1)+'%';el.handText.textContent=pct(data.handBrake).toFixed(1)+'%';const steerPct=Math.max(-100,Math.min(100,data.steer/127*100));const steerWidth=Math.abs(steerPct)/2;el.wheel.style.transform=`rotate(${data.steer*1.4}deg)`;el.steerText.textContent=(steerPct>=0?'+':'')+steerPct.toFixed(1)+'%';el.steerFill.style.left=(steerPct<0?50-steerWidth:50)+'%';el.steerFill.style.width=steerWidth+'%';el.power.textContent=data.powerPs.toFixed(1)+' PS';el.torque.textContent=data.torque.toFixed(1)+' Nm';el.power.className='signed '+(data.powerPs<0?'negative':'positive');el.torque.className='signed '+(data.torque<0?'negative':'positive');el.boost.textContent=data.boost.toFixed(1)+' psi';el.fuel.textContent=(data.fuel*100).toFixed(1)+'%';['fl','fr','rl','rr'].forEach((k,i)=>updateTire(k,data.tireTemp[i],data.tireSlip[i],data.puddle[i]));const gMag=Math.hypot(latG,longG);if(now-peaks.reset>15000){peaks={speed:kmh,rpm:data.rpm,g:gMag,reset:now}}if(kmh>=peaks.speed){peaks.speed=kmh}if(data.rpm>=peaks.rpm){peaks.rpm=data.rpm}if(gMag>=peaks.g){peaks.g=gMag}el.peakSpeed.textContent=peaks.speed.toFixed(1);el.peakRpm.textContent=Math.round(peaks.rpm).toLocaleString();el.peakGText.textContent=peaks.g.toFixed(2);el.latg.textContent=signedFixed(latG)+' G';el.longg.textContent=signedFixed(longG)+' G';el.roll.textContent=signedFixed(data.rollDeg)+'°';el.yaw.textContent=signedFixed(data.yawDeg)+'°';if(mode==='race'){el.car.textContent='Lap '+data.lap;el.classpi.textContent='Pos '+(data.position||'--');el.drive.textContent='Best '+fmtTime(data.bestLap);el.engine.textContent='Now '+fmtTime(data.currentLap)}else{el.car.textContent=data.carName&&data.carName.length?data.carName:'#'+data.carOrdinal;el.classpi.textContent=piClass(data.pi,data.classLabel)+' / '+data.pi;el.drive.textContent=driveName(data.drivetrain);el.engine.textContent=(data.cylinders||'--')+' cyl / '+(data.carType||('Type #'+data.carGroup))}trace.push({lat:latG,long:longG,slip:Math.max(...data.tireSlip.map(Math.abs))});if(trace.length>160)trace.shift();drawTrace();requestAnimationFrame(render)}
const __uiRO=new ResizeObserver(()=>{[el.gauge,document.getElementById('trace'),el.gradar].filter(Boolean).forEach(fitCanvas)});
[document.body,el.gauge,document.getElementById('trace'),el.gradar].filter(Boolean).forEach(n=>__uiRO.observe(n));
connect();render();
</script>
</body>
</html>
)HTML";

const char HOLO_HTML[] PROGMEM = R"HOLO(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<title>FH6 Hologram Telemetry</title>
<style>
:root{color-scheme:dark;--bg:#030609;--text:#eefaff;--muted:#87a6b3;--line:#21414b;--accent:#58ff9a;--accent2:#38d8ff;--accent-rgb:88,255,154;--accent2-rgb:56,216,255;--red:#ff4f72;--amber:#ffd45c}
*{box-sizing:border-box}body{margin:0;height:100vh;overflow:hidden;overscroll-behavior:none;background:radial-gradient(circle at 50% 72%,rgba(var(--accent-rgb),.18),transparent 34%),linear-gradient(135deg,#020406,#071217 52%,#030609);color:var(--text);font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;letter-spacing:0}
body:before{content:"";position:fixed;inset:0;pointer-events:none;background:linear-gradient(rgba(var(--accent2-rgb),.06) 1px,transparent 1px),linear-gradient(90deg,rgba(var(--accent-rgb),.045) 1px,transparent 1px);background-size:52px 52px;mask-image:radial-gradient(circle at 50% 55%,#000 20%,transparent 78%)}
#scene{position:fixed;inset:0;width:100%;height:100%;display:block;touch-action:none;user-select:none;-webkit-user-select:none;-webkit-touch-callout:none;pointer-events:auto;z-index:0}
.hud{position:fixed;inset:12px;display:grid;grid-template-columns:minmax(250px,15vw) minmax(0,1fr) minmax(300px,18vw);grid-template-rows:auto minmax(0,1fr) auto;gap:10px;pointer-events:none}
	.top{grid-column:1/4;display:flex;align-items:center;justify-content:space-between;gap:10px;min-width:0;position:relative;z-index:6;pointer-events:auto}.brand{display:flex;align-items:baseline;gap:14px;min-width:0}.brand h1{margin:0;font-size:clamp(20px,2.4vw,34px);font-weight:950;white-space:nowrap}.brand span{color:var(--muted);font-size:12px;font-weight:800}.cluster{display:flex;gap:8px;align-items:center;flex-wrap:wrap;justify-content:flex-end;position:relative;z-index:7;pointer-events:auto}.pill,.btn{border:1px solid var(--line);background:rgba(4,10,13,.82);color:var(--muted);border-radius:999px;padding:7px 10px;font-size:12px;font-weight:900;box-shadow:inset 0 0 20px rgba(var(--accent2-rgb),.08);font-variant-numeric:tabular-nums}.btn{position:relative;z-index:8;width:34px;height:34px;padding:0;color:var(--text);cursor:pointer;pointer-events:auto;touch-action:manipulation;-webkit-tap-highlight-color:transparent}.live{background:var(--accent);border-color:var(--accent);color:#03120b}.warn{background:var(--red);border-color:var(--red);color:#19060a}.hotPill{border-color:rgba(255,79,114,.62);color:#ffd7df;box-shadow:0 0 18px rgba(255,79,114,.2),inset 0 0 18px rgba(255,79,114,.08)}
.panel{position:relative;z-index:2;background:linear-gradient(180deg,rgba(9,18,23,.78),rgba(3,7,10,.86));border:1px solid rgba(var(--accent2-rgb),.28);border-radius:8px;padding:10px;box-shadow:0 18px 44px rgba(0,0,0,.34),inset 0 1px rgba(255,255,255,.045),0 0 28px rgba(var(--accent-rgb),.05);min-width:0}.left{grid-column:1;grid-row:2;align-self:end}.speedReadout{display:grid;grid-template-columns:minmax(0,1fr);align-items:end;row-gap:3px;max-width:100%;overflow:hidden}.left .big{display:block;min-width:0;max-width:100%;white-space:nowrap;font-size:clamp(58px,6.2vw,72px);line-height:.92;letter-spacing:0}.left .unit{display:block;text-align:right;margin:0 0 3px;font-size:clamp(13px,1.15vw,17px);line-height:1;white-space:nowrap}.right{grid-column:3;grid-row:2;display:grid;gap:10px;align-content:start}.bottom{grid-column:1/4;grid-row:3;display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:10px}.label{color:var(--muted);font-size:10px;font-weight:950;text-transform:uppercase;white-space:nowrap}.big{font-size:clamp(58px,9vw,118px);font-weight:950;line-height:.85;font-variant-numeric:tabular-nums}.unit{font-size:17px;color:var(--muted);font-weight:900}.metric{display:grid;grid-template-columns:1fr auto;align-items:end;gap:8px;margin-top:8px}.metric b{font-size:22px;font-variant-numeric:tabular-nums;white-space:nowrap}.bar{height:9px;background:#05090d;border:1px solid var(--line);border-radius:999px;overflow:hidden}.bar i{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--accent2),var(--accent));border-radius:999px}.bar.brake i{background:var(--red)}.bar.steer{position:relative}.bar.steer:before{content:"";position:absolute;left:50%;top:0;bottom:0;width:1px;background:rgba(238,250,255,.45)}#steerBar{position:absolute;top:0;bottom:0;left:50%;width:0;background:linear-gradient(90deg,var(--accent2),var(--accent));border-radius:999px}
.tires{display:grid;grid-template-columns:1fr 1fr;gap:8px}.tire{border:1px solid var(--line);border-radius:8px;padding:8px;background:rgba(4,9,12,.72);display:grid;gap:5px;min-width:0}.tireTop{display:flex;justify-content:space-between;gap:6px}.tempMain{font-size:23px;font-weight:950;font-variant-numeric:tabular-nums;line-height:1;color:var(--accent);width:6.2ch;white-space:nowrap}.slipMini{font-size:13px;font-weight:950;font-variant-numeric:tabular-nums;width:4.2ch;text-align:right;white-space:nowrap}.slipMeta{display:grid;grid-template-columns:1fr 1fr;gap:5px}.slipMeta span{font-size:10px;color:var(--muted);font-weight:900;white-space:nowrap}.slipMeta b{display:inline-block;font-size:12px;font-variant-numeric:tabular-nums;width:4.5ch;text-align:right;white-space:nowrap}.slipBar,.wet{height:5px;background:#030609;border-radius:999px;overflow:hidden}.slipBar i{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--accent2),var(--accent),var(--amber),var(--red))}.wet i{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--accent2),#a986ff)}.radarGrid{display:grid;grid-template-columns:1fr 1fr;gap:8px}.radarCell{min-width:0}.radarCell canvas{width:100%;aspect-ratio:1/1;display:block;background:rgba(3,7,10,.58);border:1px solid var(--line);border-radius:8px;margin-top:5px}.mini{min-height:66px}.mini b{display:block;margin-top:4px;font-size:19px;font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.trace{grid-column:2/4}.settings{display:none;position:fixed;inset:0;z-index:5;background:rgba(0,0,0,.58);place-items:center}.settings.open{display:grid}.box{width:min(540px,92vw);background:#071018;border:1px solid var(--line);border-radius:8px;padding:16px;box-shadow:0 28px 80px rgba(0,0,0,.6)}.head{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}.seg,.swatches{display:flex;gap:8px;flex-wrap:wrap}.seg button,.swatch{border:1px solid var(--line);background:#0c151b;color:var(--text);border-radius:999px;padding:8px 12px;font-weight:900;cursor:pointer}.swatch{width:42px;height:28px;padding:0;background:var(--sw)}.seg .active{background:var(--accent);color:#03120b}
.camControls{position:fixed;right:16px;bottom:16px;z-index:4;display:flex;gap:8px;pointer-events:auto}.camBtn{width:42px;height:42px;border-radius:999px;border:1px solid rgba(var(--accent2-rgb),.42);background:rgba(4,10,13,.78);color:var(--text);font-size:18px;font-weight:950;cursor:pointer;touch-action:manipulation;-webkit-tap-highlight-color:transparent;box-shadow:0 0 22px rgba(var(--accent2-rgb),.18),inset 0 0 18px rgba(var(--accent-rgb),.08)}
@media(min-width:1400px) and (min-height:800px){.hud{grid-template-columns:minmax(320px,17vw) minmax(0,1fr) minmax(360px,19vw)}.brand h1{font-size:clamp(34px,2.35vw,46px)}.pill,.btn{font-size:14px;padding:8px 13px}.btn{width:40px;height:40px}.label{font-size:12px}.panel{padding:12px}.speedReadout{grid-template-columns:minmax(0,1fr) auto;column-gap:6px}.left .big{font-size:clamp(78px,4.2vw,94px)}.left .unit{font-size:clamp(18px,1.2vw,22px);margin:0 0 .06em}.metric{margin-top:10px}.metric b{font-size:27px}.bar{height:12px}.tempMain{font-size:30px}.slipMini{font-size:16px}.slipMeta span{font-size:12px}.slipMeta b{font-size:14px}.slipBar,.wet{height:7px}.mini b{font-size:26px}.camBtn{width:48px;height:48px;font-size:22px}}
		@media(max-width:980px){body{height:auto;min-height:100vh;overflow:auto}.hud{position:relative;inset:auto;min-height:100vh;grid-template-columns:1fr;grid-template-rows:auto minmax(420px,56vh) auto auto auto;padding:10px}.top{grid-column:1;grid-row:1}.left{grid-column:1;grid-row:3;align-self:auto}.right{grid-column:1;grid-row:4;grid-template-columns:1fr}.bottom{grid-column:1;grid-row:5;grid-template-columns:1fr 1fr}#scene{position:relative;inset:auto;grid-column:1;grid-row:2;min-height:420px;height:56vh;border:1px solid rgba(var(--accent2-rgb),.18);border-radius:8px;background:rgba(3,7,10,.46);z-index:0}.hud>*{position:relative;z-index:1}}@media(max-width:560px){.bottom{grid-template-columns:1fr}.brand{display:block}.brand h1{font-size:22px}.top{align-items:flex-start}.big{font-size:76px}.left .big{font-size:clamp(58px,18vw,76px)}#scene{min-height:360px;height:52vh}.hud{grid-template-rows:auto minmax(360px,52vh) auto auto auto}}@media(min-width:1180px) and (max-width:1300px) and (min-height:1020px) and (max-height:1120px){.hud{inset:10px;grid-template-columns:minmax(270px,22vw) minmax(0,1fr) minmax(320px,27vw);gap:9px}.brand h1{font-size:34px}.pill,.btn{font-size:12px;padding:7px 10px}.panel{padding:10px}.label{font-size:11px}.left .big{font-size:76px}.left .unit{font-size:17px}.metric b{font-size:24px}.bar{height:10px}.tempMain{font-size:27px}.slipMini{font-size:14px}.radarGrid{gap:7px}.bottom{grid-template-columns:1fr 1fr 1.55fr}.trace{grid-column:auto}.mini b{font-size:22px}.camControls{bottom:88px}.camBtn{width:46px;height:46px;font-size:21px}}@media(min-width:2200px) and (min-height:1200px){.hud{inset:22px;grid-template-columns:minmax(430px,18vw) minmax(0,1fr) minmax(500px,20vw);gap:18px}.brand h1{font-size:62px}.pill,.btn{font-size:18px;padding:11px 18px}.btn{width:54px;height:54px;font-size:20px}.panel{padding:18px}.label{font-size:15px}.left .big{font-size:clamp(118px,5vw,142px)}.left .unit{font-size:30px}.metric{margin-top:14px}.metric b{font-size:38px}.bar{height:16px}.tempMain{font-size:42px}.slipMini{font-size:20px}.slipMeta span{font-size:15px}.slipMeta b{font-size:18px}.slipBar,.wet{height:9px}.mini b{font-size:36px}.camControls{right:22px;bottom:22px}.camBtn{width:58px;height:58px;font-size:26px}}
</style>


</head>
<body>
<div class="hud">
  <header class="top"><div class="brand"><h1>Telemetry Hologram UI</h1></div><div class="cluster"><span id="state" class="pill warn">NO SIGNAL</span><span id="camState" class="pill">FIXED</span><span id="driveState" class="pill">FWD</span><span id="impactState" class="pill">IMPACT 0%</span><span id="hz" class="pill">0 Hz</span><span id="age" class="pill">--- ms</span><button id="settingsBtn" class="btn" type="button">⚙</button></div></header>
  <canvas id="scene"></canvas>
  <section class="panel left"><div class="label">Kinematics</div><div class="speedReadout"><span id="speed" class="big">000.0</span><span class="unit">km/h</span></div><div class="metric"><span class="label">RPM</span><b id="rpm">0</b></div><div class="bar"><i id="rpmBar"></i></div><div class="metric"><span class="label">Gear</span><b id="gear">N</b></div><div class="metric"><span class="label">Steer</span><b id="steer">+0.0%</b></div><div class="bar steer"><i id="steerBar"></i></div><div class="metric"><span class="label">Throttle</span><b id="throttleText">0.0%</b></div><div class="bar"><i id="throttle"></i></div><div class="metric"><span class="label">Brake</span><b id="brakeText">0.0%</b></div><div class="bar brake"><i id="brake"></i></div></section>
  <section class="right"><div class="panel"><div class="label">Tire State</div><div class="tires"><div class="tire"><div class="tireTop"><span class="label">FL</span><span id="flSlip" class="slipMini">0.00</span></div><div id="flTemp" class="tempMain">000.0℃</div><div class="slipBar"><i id="flSlipBar"></i></div><div class="slipMeta"><span>R <b id="flRatio">+0.00</b></span><span>A <b id="flAngle">+0.00</b></span></div><div class="wet"><i id="flWet"></i></div></div><div class="tire"><div class="tireTop"><span class="label">FR</span><span id="frSlip" class="slipMini">0.00</span></div><div id="frTemp" class="tempMain">000.0℃</div><div class="slipBar"><i id="frSlipBar"></i></div><div class="slipMeta"><span>R <b id="frRatio">+0.00</b></span><span>A <b id="frAngle">+0.00</b></span></div><div class="wet"><i id="frWet"></i></div></div><div class="tire"><div class="tireTop"><span class="label">RL</span><span id="rlSlip" class="slipMini">0.00</span></div><div id="rlTemp" class="tempMain">000.0℃</div><div class="slipBar"><i id="rlSlipBar"></i></div><div class="slipMeta"><span>R <b id="rlRatio">+0.00</b></span><span>A <b id="rlAngle">+0.00</b></span></div><div class="wet"><i id="rlWet"></i></div></div><div class="tire"><div class="tireTop"><span class="label">RR</span><span id="rrSlip" class="slipMini">0.00</span></div><div id="rrTemp" class="tempMain">000.0℃</div><div class="slipBar"><i id="rrSlipBar"></i></div><div class="slipMeta"><span>R <b id="rrRatio">+0.00</b></span><span>A <b id="rrAngle">+0.00</b></span></div><div class="wet"><i id="rrWet"></i></div></div></div></div><div class="panel"><div class="radarGrid"><div class="radarCell"><div class="label">G Vector</div><canvas id="radar" width="150" height="150"></canvas></div><div class="radarCell"><div class="label">Route X/Z</div><canvas id="route" width="150" height="150"></canvas></div></div></div></section>
  <section class="bottom"><div class="panel mini"><span class="label">Power</span><b id="power">0.0 PS</b></div><div class="panel mini"><span class="label">Torque</span><b id="torque">0.0 Nm</b></div><div class="panel mini trace"><span class="label">Vehicle</span><b id="vehicle">--</b></div></section>
</div>
<div class="camControls"><button id="zoomOut" class="camBtn" type="button" title="Zoom out">-</button><button id="zoomIn" class="camBtn" type="button" title="Zoom in">+</button><button id="camReset" class="camBtn" type="button" title="Reset camera">⟲</button></div>
<div id="settings" class="settings"><div class="box"><div class="head"><strong>Display Settings</strong><button id="closeSettings" class="btn" type="button">×</button></div><div class="label">View</div><div class="seg"><button id="dashView" type="button">Dashboard</button><button class="active" type="button">Hologram</button></div><div class="label" style="margin-top:14px">Camera</div><div class="seg"><button id="fixedCam" type="button">視点固定</button><button id="followCam" type="button">カメラ追従</button></div><div class="label" style="margin-top:14px">Quality</div><div class="seg"><button id="qualityLow" type="button">Low</button><button id="qualityMed" type="button">Medium</button><button id="qualityHigh" type="button">High</button></div><div class="label" style="margin-top:14px">Palette</div><div id="swatches" class="swatches"></div></div></div>
<script>
const $=id=>document.getElementById(id),E={scene:$('scene'),state:$('state'),camState:$('camState'),driveState:$('driveState'),impactState:$('impactState'),hz:$('hz'),age:$('age'),settings:$('settings'),settingsBtn:$('settingsBtn'),closeSettings:$('closeSettings'),dashView:$('dashView'),fixedCam:$('fixedCam'),followCam:$('followCam'),qualityLow:$('qualityLow'),qualityMed:$('qualityMed'),qualityHigh:$('qualityHigh'),camReset:$('camReset'),zoomIn:$('zoomIn'),zoomOut:$('zoomOut'),speed:$('speed'),rpm:$('rpm'),rpmBar:$('rpmBar'),gear:$('gear'),steer:$('steer'),steerBar:$('steerBar'),throttle:$('throttle'),brake:$('brake'),throttleText:$('throttleText'),brakeText:$('brakeText'),power:$('power'),torque:$('torque'),vehicle:$('vehicle'),radar:$('radar'),route:$('route')};
const tireKeys=['fl','fr','rl','rr'],palettes=[['Green','#58ff9a','#38d8ff'],['Cyan','#38d8ff','#58ff9a'],['Blue','#4e8cff','#38d8ff'],['Violet','#a986ff','#38d8ff'],['Pink','#ff5ad9','#a986ff'],['Red','#ff4f72','#ffd45c'],['Amber','#ffd45c','#ff7b4f'],['Mono','#edf8ff','#9fb3bf']];
const qualities={low:{dpr:1,gridX:22,gridZ:18,track:120,dash:[10,14],glow:.72},medium:{dpr:1.5,gridX:30,gridZ:24,track:220,dash:[7,9],glow:1},high:{dpr:2,gridX:38,gridZ:30,track:340,dash:[5,7],glow:1.25}};
const defaultCam={yaw:1.57,pitch:-.34,dist:10.4,scale:1},demoMode=new URLSearchParams(location.search).has('test');
let data=null,lastRx=0,rxCount=0,lastHzAt=performance.now(),lastFrameAt=performance.now(),ageAvg=0,camMode=localStorage.getItem('fh6holoCam')||'fixed',qualityName=localStorage.getItem('fh6holoQuality')||'medium',quality=qualities[qualityName]||qualities.medium,cam={...defaultCam,drag:false,x:0,y:0},smooth={speed:0,rpm:0,roll:0,pitch:0,bodyHeading:0,travelHeading:0,slipAngle:0,x:0,z:0,steer:0,lat:0,long:0},track=[],origin=null,trackCar=null,gridPhase=0,impactPulse=0,prevSpeed=0;
function hexRgb(c){const n=parseInt(c.slice(1),16);return[(n>>16)&255,(n>>8)&255,n&255].join(',')}
function css(n,f){return getComputedStyle(document.documentElement).getPropertyValue(n).trim()||f}
function applyPalette(a,b){document.documentElement.style.setProperty('--accent',a);document.documentElement.style.setProperty('--accent2',b);document.documentElement.style.setProperty('--accent-rgb',hexRgb(a));document.documentElement.style.setProperty('--accent2-rgb',hexRgb(b));localStorage.setItem('fh6palette',a+','+b)}
palettes.forEach(p=>{const b=document.createElement('button');b.className='swatch';b.style.setProperty('--sw',p[1]);b.title=p[0];b.onclick=()=>applyPalette(p[1],p[2]);$('swatches').appendChild(b)});{const s=(localStorage.getItem('fh6palette')||'#58ff9a,#38d8ff').split(',');applyPalette(s[0],s[1]||s[0])}
function setCamMode(m){camMode=m;localStorage.setItem('fh6holoCam',m);E.fixedCam.classList.toggle('active',m==='fixed');E.followCam.classList.toggle('active',m==='follow');E.camState.textContent=m==='fixed'?'FIXED':'FOLLOW'}
function setQuality(q){qualityName=qualities[q]?q:'medium';quality=qualities[qualityName];localStorage.setItem('fh6holoQuality',qualityName);E.qualityLow.classList.toggle('active',qualityName==='low');E.qualityMed.classList.toggle('active',qualityName==='medium');E.qualityHigh.classList.toggle('active',qualityName==='high');resize()}
function resetCamera(){Object.assign(cam,defaultCam,{drag:false})}
setCamMode(camMode);setQuality(qualityName);
E.settingsBtn.onclick=()=>E.settings.classList.add('open');E.closeSettings.onclick=()=>E.settings.classList.remove('open');E.dashView.onclick=()=>location.href='/';E.fixedCam.onclick=()=>{setCamMode('fixed');resetCamera()};E.followCam.onclick=()=>{setCamMode('follow');resetCamera()};E.qualityLow.onclick=()=>setQuality('low');E.qualityMed.onclick=()=>setQuality('medium');E.qualityHigh.onclick=()=>setQuality('high');E.camReset.onclick=resetCamera;E.zoomIn.onclick=()=>zoomStep(1);E.zoomOut.onclick=()=>zoomStep(-1);E.settings.onclick=e=>{if(e.target===E.settings)E.settings.classList.remove('open')};
function pct(v){return Math.max(0,Math.min(100,v/255*100))}function gear(g){return g===0?'R':String(g)}function latency(v){return String(Math.min(999,Math.max(0,Math.round(v)))).padStart(3,'0')+' ms'}function fixedAbs(v,d,w){return Math.min(999.9,Math.abs(v||0)).toFixed(d).padStart(w,'0')}function fixedSigned(v,d){const n=Math.max(-9.99,Math.min(9.99,v||0));return(n>=0?'+':'')+n.toFixed(d)}function lerp(a,b,t){return a+(b-a)*t}function wrap(a){return Math.atan2(Math.sin(a),Math.cos(a))}function lerpAngle(a,b,t){return a+wrap(b-a)*t}function piClass(pi,label){return label||'--'}
function setZoom(v){cam.scale=Math.max(.52,Math.min(1.55,v))}function zoomStep(dir){setZoom(cam.scale*(dir>0?1.12:.89))}
function tempColor(t){if(t<55)return '#38d8ff';if(t<85)return '#58ff9a';if(t<105)return '#ffd45c';return '#ff4f72'}
function slipColor(s){const a=Math.abs(s);if(a<.18)return '#58ff9a';if(a<.45)return '#ffd45c';return '#ff4f72'}
function connect(){if(demoMode)return;const ws=new WebSocket(`ws://${location.hostname}:81/`);ws.onopen=()=>{E.state.textContent='CONNECTED';E.state.className='pill live'};ws.onmessage=e=>{data=JSON.parse(e.data);lastRx=performance.now();rxCount++};ws.onclose=()=>{E.state.textContent='RECONNECTING';E.state.className='pill warn';setTimeout(connect,900)};ws.onerror=()=>ws.close()}
function demoData(t){const hit=Math.sin(t*.21)>0.92?1:0,x=65*Math.sin(t*.75),z=95*Math.sin(t*1.5),vx=48.75*Math.cos(t*.75),vz=142.5*Math.cos(t*1.5),travel=Math.atan2(vz,vx),drift=.55*Math.sin(t*1.35),body=travel-drift,localSide=Math.sin(drift)*Math.hypot(vx,vz),localForward=Math.cos(drift)*Math.hypot(vx,vz),rev=Math.sin(t*.29)>0.88,spd=Math.hypot(vx,vz)*3.6-hit*120*Math.max(0,Math.sin(t*8));return{signal:true,race:true,ageMs:6,speedKmh:Math.max(0,spd),rpm:2400+Math.max(0,spd)*19,maxRpm:9000,gear:rev?0:Math.max(1,Math.min(8,Math.floor(spd/45)+1)),steer:Math.round(112*Math.sin(t*1.9)),accel:hit?0:220,brake:hit?210:0,powerPs:hit?-260:620,torque:hit?-680:520,carOrdinal:9999,carName:'TEST SPORTS COUPE',classLabel:'S2',pi:950,tireTemp:[72+18*Math.sin(t),76+16*Math.cos(t*.7),88+15*Math.sin(t*.8),87+14*Math.cos(t*.9)],slipRatio:[.2*Math.sin(t*1.1),.2*Math.cos(t*1.2),.45*Math.sin(t*2),.45*Math.cos(t*1.9)],slipAngle:[drift*.55,drift*.45,drift*.9,drift*.8],combinedSlip:[.15+hit*1.8,.12+hit*1.5,.22+Math.abs(Math.sin(t*2))*.5,.2+Math.abs(Math.cos(t*2))*.45],puddle:[0,0,0,0],latG:1.8*Math.sin(t*1.8),longG:hit?-2.8:.9*Math.cos(t*1.1),rollDeg:22*Math.sin(t*1.6)+hit*12,pitchDeg:10*Math.sin(t*1.2)-hit*16,yawDeg:body*57.2958,position3:[x,0,z],velocity:[rev?-localSide:localSide,0,rev?-localForward:localForward],suspensionMeters:[.045*Math.sin(t*3)+hit*.06,.045*Math.cos(t*2.7)-hit*.03,.036*Math.sin(t*2.1),.036*Math.cos(t*2.3)]}}
function resize(){const dpr=Math.min(quality.dpr,devicePixelRatio||1),r=E.scene.getBoundingClientRect();E.scene.width=Math.max(1,Math.round(r.width*dpr));E.scene.height=Math.max(1,Math.round(r.height*dpr))}addEventListener('resize',resize);resize();
const pointers=new Map();let pinchDist=0,pinchScale=1;function pinchDistance(){const p=[...pointers.values()];return Math.hypot(p[0].x-p[1].x,p[0].y-p[1].y)}function endPointer(e){pointers.delete(e.pointerId);if(pointers.size===1){const p=[...pointers.values()][0];cam.drag=true;cam.x=p.x;cam.y=p.y}else{cam.drag=false;pinchDist=0}}E.scene.onpointerdown=e=>{e.preventDefault();pointers.set(e.pointerId,{x:e.clientX,y:e.clientY});E.scene.setPointerCapture(e.pointerId);if(pointers.size===1){cam.drag=true;cam.x=e.clientX;cam.y=e.clientY}else if(pointers.size===2){cam.drag=false;pinchDist=pinchDistance();pinchScale=cam.scale}};E.scene.onpointermove=e=>{if(!pointers.has(e.pointerId))return;e.preventDefault();pointers.set(e.pointerId,{x:e.clientX,y:e.clientY});if(pointers.size>=2){if(pinchDist>0)setZoom(pinchScale*pinchDistance()/pinchDist);return}if(!cam.drag)return;cam.yaw+=(e.clientX-cam.x)*.006;cam.pitch=Math.max(-1.05,Math.min(.2,cam.pitch+(e.clientY-cam.y)*.004));cam.x=e.clientX;cam.y=e.clientY};E.scene.onpointerup=endPointer;E.scene.onpointercancel=endPointer;E.scene.onpointerleave=endPointer;E.scene.onwheel=e=>{e.preventDefault();setZoom(cam.scale*(e.deltaY>0?.92:1.09))};
['touchstart','touchmove'].forEach(ev=>E.scene.addEventListener(ev,e=>e.preventDefault(),{passive:false}));['gesturestart','gesturechange','gestureend'].forEach(ev=>addEventListener(ev,e=>e.preventDefault(),{passive:false}));
function rot(p,r,pit,y){let x=p[0],yy=p[1],z=p[2],cy=Math.cos(y),sy=Math.sin(y),nx=x*cy-z*sy,nz=x*sy+z*cy;x=nx;z=nz;let cp=Math.cos(pit),sp=Math.sin(pit),ny=x*sp+yy*cp;nx=x*cp-yy*sp;x=nx;yy=ny;let cr=Math.cos(r),sr=Math.sin(r);ny=yy*cr-z*sr;nz=yy*sr+z*cr;return[x,ny,nz]}
function camRot(p){let x=p[0],y=p[1],z=p[2],cy=Math.cos(cam.yaw),sy=Math.sin(cam.yaw),nx=x*cy-z*sy,nz=x*sy+z*cy;x=nx;z=nz;let cp=Math.cos(cam.pitch),sp=Math.sin(cam.pitch),ny=y*cp-z*sp;nz=y*sp+z*cp;return[x,ny,nz+cam.dist]}
function project(p,w,h){const q=camRot(p),s=Math.min(w,h)*.95*cam.scale/q[2];return{x:w/2+q[0]*s,y:h*.56-q[1]*s,z:q[2],s:s}}
function drawable(p){return p&&p.z>.55&&Number.isFinite(p.x)&&Number.isFinite(p.y)&&Math.abs(p.x)<9000&&Math.abs(p.y)<9000}
function line(ctx,a,b,col,w=2){if(!drawable(a)||!drawable(b))return;ctx.strokeStyle=col;ctx.lineWidth=w;ctx.beginPath();ctx.moveTo(a.x,a.y);ctx.lineTo(b.x,b.y);ctx.stroke()}
const gtrCad={
base:[[-2.58,-.25,1.08],[-2.29,-.27,1.1],[-2,-.29,1.13],[-1.72,-.29,1.13],[-1.43,-.3,1.13],[-1.15,-.3,1.13],[-.86,-.31,1.13],[-.57,-.31,1.12],[-.29,-.31,1.12],[0,-.3,1.12],[.29,-.3,1.12],[.57,-.29,1.11],[.86,-.28,1.11],[1.15,-.29,1.11],[1.43,-.3,1.11],[1.72,-.3,1.11],[2,-.29,1.11],[2.29,-.28,1.05],[2.58,-.28,1.02],[2.58,-.28,-1.02],[2.29,-.28,-1.05],[2,-.29,-1.11],[1.72,-.3,-1.11],[1.43,-.3,-1.11],[1.15,-.29,-1.11],[.86,-.28,-1.11],[.57,-.29,-1.11],[.29,-.3,-1.12],[0,-.3,-1.12],[-.29,-.31,-1.12],[-.57,-.31,-1.12],[-.86,-.31,-1.13],[-1.15,-.3,-1.13],[-1.43,-.3,-1.13],[-1.72,-.29,-1.13],[-2,-.29,-1.13],[-2.29,-.27,-1.1],[-2.58,-.25,-1.08]],
belt:[[-2.58,.29,1.03],[-2.29,.24,1.06],[-2,.18,1.12],[-1.72,.12,1.12],[-1.43,.12,1.12],[-1.15,.19,1.12],[-.86,.25,1.12],[-.57,.31,1.1],[-.29,.3,1.09],[0,.29,1.08],[.29,.28,1.08],[.57,.26,1.08],[.86,.24,1.09],[1.15,.22,1.1],[1.43,.22,1.11],[1.72,.22,1.11],[2,.21,1.08],[2.29,.18,.99],[2.58,.15,.92],[2.58,.15,-.92],[2.29,.18,-.99],[2,.21,-1.08],[1.72,.22,-1.11],[1.43,.22,-1.11],[1.15,.22,-1.1],[.86,.24,-1.09],[.57,.26,-1.08],[.29,.28,-1.08],[0,.29,-1.08],[-.29,.3,-1.09],[-.57,.31,-1.1],[-.86,.25,-1.12],[-1.15,.19,-1.12],[-1.43,.12,-1.12],[-1.72,.12,-1.12],[-2,.18,-1.12],[-2.29,.24,-1.06],[-2.58,.29,-1.03]],
roof:[[-1.72,.54,.77],[-1.43,.6,.82],[-1.15,.65,.86],[-.86,.68,.89],[-.57,.7,.9],[-.29,.7,.87],[0,.68,.89],[.29,.59,.91],[.57,.5,.94],[.86,.42,.77],[1.15,.4,.76],[1.43,.38,.79],[1.43,.38,-.79],[1.15,.4,-.76],[.86,.42,-.77],[.57,.5,-.94],[.29,.59,-.91],[0,.68,-.89],[-.29,.7,-.87],[-.57,.7,-.9],[-.86,.68,-.89],[-1.15,.65,-.86],[-1.43,.6,-.82],[-1.72,.54,-.77]],
center:[[-2.58,.29,0],[-2.29,.24,0],[-2,.18,0],[-1.72,.12,0],[-1.43,.12,0],[-1.15,.19,0],[-.86,.25,0],[-.57,.31,0],[-.29,.3,0],[0,.29,0],[.29,.28,0],[.57,.26,0],[.86,.24,0],[1.15,.22,0],[1.43,.22,0],[1.72,.22,0],[2,.21,0],[2.29,.18,0],[2.58,.15,0]]
};
function drawScene(){
const c=E.scene,ctx=c.getContext('2d'),w=c.width,h=c.height,a=css('--accent','#58ff9a'),b=css('--accent2','#38d8ff'),amber='#ffd45c',red='#ff4f72';
ctx.clearRect(0,0,w,h);ctx.lineCap='round';ctx.lineJoin='round';
const flow=gridPhase%0.72;
const roll=smooth.roll*Math.PI/180,pit=-smooth.pitch*Math.PI/180,bodyHeading=smooth.bodyHeading||0,travelHeading=smooth.travelHeading||bodyHeading,bodyYaw=camMode==='follow'?0:bodyHeading;
const GP=(wx,wz)=>{let x=wx-smooth.x,z=wz-smooth.z;if(camMode==='follow'){const c=Math.cos(-bodyHeading),s=Math.sin(-bodyHeading),nx=x*c-z*s;z=x*s+z*c;x=nx}return project([x,-.95,z],w,h)};
const cell=.72,startX=Math.floor((smooth.x-10)/cell)*cell,startZ=Math.floor((smooth.z-7.2)/cell)*cell;
for(let i=0;i<=quality.gridX;i++){const wx=startX+i*cell;line(ctx,GP(wx,smooth.z-7.8),GP(wx,smooth.z+7.8),`rgba(135,166,179,${.2*quality.glow})`,1.15)}
for(let i=0;i<=quality.gridZ;i++){const wz=startZ+i*cell;line(ctx,GP(smooth.x-10.5,wz),GP(smooth.x+10.5,wz),`rgba(135,166,179,${.17*quality.glow})`,1.05)}
if(track.length>1){ctx.save();ctx.strokeStyle=`rgba(${css('--accent2-rgb','56,216,255')},${.54*quality.glow})`;ctx.lineWidth=qualityName==='low'?2:3;ctx.setLineDash(quality.dash);ctx.beginPath();let open=false;track.slice(-quality.track).forEach((p,i)=>{const dx=p.x-smooth.x,dz=p.z-smooth.z,near=Math.hypot(dx,dz)<95,q=near?GP(p.x,p.z):null;if(!near||!drawable(q)){open=false;return}if(open)ctx.lineTo(q.x,q.y);else{ctx.moveTo(q.x,q.y);open=true}});ctx.stroke();ctx.setLineDash([]);for(let i=Math.max(0,track.length-18);i<track.length;i++){const p=track[i],q=GP(p.x,p.z),alpha=(i-(track.length-18))/18;if(!drawable(q))continue;ctx.fillStyle=`rgba(${css('--accent-rgb','88,255,154')},${Math.max(.08,alpha*.42*quality.glow)})`;ctx.beginPath();ctx.arc(q.x,q.y,Math.max(2,q.s*.035),0,Math.PI*2);ctx.fill()}ctx.restore()}
const fx=Math.cos(travelHeading),fz=Math.sin(travelHeading),sx=-Math.sin(travelHeading),sz=Math.cos(travelHeading);
for(let i=0;i<9;i++){const d=i*.85+flow*.95;[-1.22,1.22].forEach(side=>{const ax=smooth.x-fx*(d+.15)+sx*side,az=smooth.z-fz*(d+.15)+sz*side,bx=smooth.x-fx*(d+.7)+sx*side,bz=smooth.z-fz*(d+.7)+sz*side;line(ctx,GP(ax,az),GP(bx,bz),`rgba(${css('--accent-rgb','88,255,154')},${.38-i*.028})`,2.7)})}
const R=p=>rot(p,roll,pit,bodyYaw),P=p=>project(R(p),w,h),G=p=>project(rot(p,0,0,bodyYaw),w,h),strokePath=(pts,col=a,lw=2,close=true)=>{const q=pts.map(P);ctx.strokeStyle=col;ctx.lineWidth=lw;ctx.beginPath();q.forEach((p,i)=>i?ctx.lineTo(p.x,p.y):ctx.moveTo(p.x,p.y));if(close)ctx.closePath();ctx.stroke();return q},fillPath=(pts,fill,stroke=a,lw=2)=>{const q=pts.map(P);ctx.fillStyle=fill;ctx.beginPath();q.forEach((p,i)=>i?ctx.lineTo(p.x,p.y):ctx.moveTo(p.x,p.y));ctx.closePath();ctx.fill();ctx.strokeStyle=stroke;ctx.lineWidth=lw;ctx.stroke();return q};
const impact=impactPulse>0?`rgba(255,79,114,${.2*impactPulse})`:`rgba(${css('--accent-rgb','88,255,154')},.06)`;
const hi=qualityName==='high';
const cad=(pts,w=1,z=1)=>pts.map(p=>[p[0]*1.06,p[1]*.78-.04,p[2]*z]);
const cadRoof=pts=>pts.map(p=>[p[0]*1.06,p[1]*.88-.05,p[2]*1.03]);
const frontLimit=x=>x<2.1?1.02:x<2.5?1.02-(x-2.1)*.6:x<2.82?.78-(x-2.5)*1.13:.42;
const planLimit=x=>x<-2.6?.96:x<-2.3?.96+(x+2.6)*.27:x<-.45?1.045:x<.45?1.08:x<2.05?1.03:frontLimit(x);
const capZ=(x,z)=>z?Math.sign(z)*Math.min(Math.abs(z),planLimit(x)):0;
const P3=(x,y,z)=>[x,y,capZ(x,z)];
const frontTaper=(pts,tipY)=>{const out=[];pts.forEach((p,i)=>{const q=[p[0],p[1],capZ(p[0],p[2])];out.push(q);const n=pts[i+1];if(n&&p[0]>2.35&&n[0]>2.35&&p[2]>0&&n[2]<0)out.push([2.82,tipY,0])});return out};
const base=hi?frontTaper(cad(gtrCad.base,1,1.08),-.25):[[-2.58,-.36,-.88],[.55,-.37,-1.02],[2.16,-.28,-.86],[2.72,-.18,-.56],[2.86,-.16,0],[2.72,-.18,.56],[2.16,-.28,.86],[.55,-.37,1.02],[-2.58,-.36,.88]];
const belt=hi?frontTaper(cad(gtrCad.belt,1,1.03),.06):[[-2.42,.04,-.7],[-.72,.16,-.82],[.82,.16,-.74],[1.95,.04,-.56],[2.5,-.02,-.34],[2.64,-.02,0],[2.5,-.02,.34],[1.95,.04,.56],[.82,.16,.74],[-.72,.16,.82],[-2.42,.04,.7]];
const roof=hi?cadRoof(gtrCad.roof):[[-1.0,.38,-.36],[-.28,.58,-.34],[.48,.5,-.3],[.72,.28,0],[.48,.5,.3],[-.28,.58,.34],[-1.0,.38,.36]];
const center=hi?cad(gtrCad.center,1,1):[];
fillPath(base,impact,a,3);
fillPath(belt,`rgba(${css('--accent-rgb','88,255,154')},.075)`,a,2.4);
fillPath(roof,`rgba(${css('--accent-rgb','88,255,154')},.07)`,a,2.2);
[ -.64,.64].forEach(z=>strokePath(hi?[P3(-2.28,.13,z),P3(-1.55,.24,z*1.24),P3(-.2,.31,z*1.4),P3(.98,.23,z*1.32),P3(2.22,.02,z*.96)]:[[-2.34,.08,z],[-.82,.22,z],[.9,.2,z],[2.2,.02,z],[2.66,-.02,z]],`rgba(${css('--accent-rgb','88,255,154')},.5)`,hi?2.1:1.3,false));
if(hi){const panel=`rgba(${css('--accent-rgb','88,255,154')},.58)`,glass=`rgba(${css('--accent2-rgb','56,216,255')},.82)`,cabin=`rgba(${css('--accent-rgb','88,255,154')},.66)`,dimGlass=`rgba(238,250,255,.34)`;
const roofSide=roof.slice(0,roof.length/2),roofOpp=roof.slice(roof.length/2),roofMid=i=>roofSide[Math.max(0,Math.min(roofSide.length-1,i))],beltAt=p=>belt.reduce((m,q)=>q[2]>0&&Math.abs(q[0]-p[0])<Math.abs(m[0]-p[0])?q:m,belt[0]);
roofSide.forEach((rp,i)=>{if(i%3)return;const bp=beltAt(rp),rn=roofOpp[roofOpp.length-1-i],bn=[bp[0],bp[1],-bp[2]];strokePath([bp,rp],cabin,1.8,false);strokePath([bn,rn],cabin,1.8,false)});
strokePath(roofSide,cabin,2.5,false);strokePath(roofOpp,cabin,2.5,false);
strokePath([[2.24,-.16,-.98],[2.62,-.08,-.78],[2.78,.02,-.42],[2.82,.02,.42],[2.62,-.08,.78],[2.24,-.16,.98]],glass,2.7,false);strokePath([[2.5,.14,-.62],[2.76,.04,-.24],[2.82,-.04,0],[2.76,.04,.24],[2.5,.14,.62]],`rgba(238,250,255,.72)`,2.1,false);strokePath([[2.6,-.1,-.36],[2.72,-.04,-.14],[2.74,-.04,.14],[2.6,-.1,.36]],`rgba(238,250,255,.7)`,1.5,true);
strokePath([[2.02,-.03,-.98],[2.5,.01,-.78],[2.66,.13,-.43],[2.06,.08,-.54]],amber,2.8,true);strokePath([[2.02,-.03,.98],[2.5,.01,.78],[2.66,.13,.43],[2.06,.08,.54]],amber,2.8,true);strokePath([[2.12,.04,-.72],[2.5,.08,-.5],[2.58,.12,-.3]],`rgba(255,212,92,.55)`,1.3,false);strokePath([[2.12,.04,.72],[2.5,.08,.5],[2.58,.12,.3]],`rgba(255,212,92,.55)`,1.3,false);
strokePath([P3(-2.74,-.15,-1.05),P3(-2.74,.26,-1.02),P3(-2.74,.28,1.02),P3(-2.74,-.15,1.05)],panel,2.7,true);strokePath([P3(-1.72,.42,-.8),P3(-2.18,.37,-.96),P3(-2.58,.3,-1.03),P3(-2.86,.27,-.96),P3(-2.9,.27,.96),P3(-2.58,.3,1.03),P3(-2.18,.37,.96),P3(-1.72,.42,.8)],cabin,2.3,false);strokePath([P3(-1.58,.42,-.72),P3(-2.04,.36,-.88),P3(-2.5,.29,-.96),P3(-2.88,.25,-.96),P3(-2.98,.24,-.7),P3(-2.98,.24,.7),P3(-2.88,.25,.96),P3(-2.5,.29,.96),P3(-2.04,.36,.88),P3(-1.58,.42,.72)],cabin,1.6,false);
fillPath([[-2.98,.38,-1.08],[-2.78,.49,-.94],[-2.78,.49,.94],[-2.98,.38,1.08]],`rgba(${css('--accent-rgb','88,255,154')},.1)`,cabin,2.7);strokePath([[-3.04,.36,-1.18],[-2.8,.51,-.98],[-2.8,.51,.98],[-3.04,.36,1.18]],cabin,2.4,false);strokePath([[-2.88,.47,-.86],[-2.78,.53,-.68],[-2.78,.53,.68],[-2.88,.47,.86]],`rgba(238,250,255,.42)`,1.4,false);[-1,1].forEach(s=>{strokePath([[-2.54,.26,.82*s],[-2.7,.42,.92*s],[-2.86,.49,1.02*s]],panel,1.9,false);strokePath([[-2.96,.36,1.08*s],[-2.84,.52,.98*s],[-2.72,.46,.88*s],[-2.82,.32,.98*s]],cabin,1.5,true)});
strokePath([[-2.58,.07,-.98],[-2.76,.08,-.78],[-2.82,.07,-.6]],red,2.7,false);strokePath([[-2.58,.07,.98],[-2.76,.08,.78],[-2.82,.07,.6]],red,2.7,false);strokePath([[-2.44,.04,-.74],[-2.64,.06,-.54]],red,2,false);strokePath([[-2.44,.04,.74],[-2.64,.06,.54]],red,2,false);
[-1,1].forEach(s=>{strokePath([P3(-2.52,.12,1.0*s),P3(-2.0,.27,1.04*s),P3(-1.34,.29,1.05*s),P3(-.66,.31,1.08*s),P3(.5,.28,1.04*s),P3(1.26,.18,.92*s),P3(2.24,-.16,.98*s),P3(2.62,-.08,.78*s)],panel,2.2,false);strokePath([P3(-2.4,-.1,1.1*s),P3(-1.65,-.16,1.17*s),P3(-.42,-.18,1.2*s),P3(.92,-.15,1.12*s),P3(2.24,-.16,.98*s),P3(2.62,-.08,.78*s)],`rgba(${css('--accent-rgb','88,255,154')},.5)`,1.4,false);strokePath([P3(.58,.28,.78*s),P3(.74,.38,.92*s),P3(1.02,.34,.9*s),P3(.98,.22,.76*s)],glass,1.8,true);strokePath([P3(-1.58,.42,.78*s),P3(-2.04,.36,.94*s),P3(-2.5,.29,1.02*s),P3(-2.86,.26,.96*s)],cabin,2,false);strokePath([P3(-1.1,.22,1.04*s),P3(-.9,.03,1.12*s),P3(-.55,-.06,1.16*s)],panel,1.5,false);strokePath([P3(.92,.18,1.02*s),P3(1.28,.04,1.1*s),P3(1.74,-.05,1.0*s),P3(2.24,-.16,.98*s)],panel,1.7,false)});
[[1.34,.99],[1.34,-.99],[-1.42,.99],[-1.42,-.99]].forEach(p=>{strokePath([[p[0]-.42,-.2,p[1]],[p[0]-.25,.1,p[1]],[p[0]+.25,.1,p[1]],[p[0]+.42,-.2,p[1]]],panel,2.2,false);strokePath([[p[0]-.34,-.1,p[1]],[p[0]+.34,-.1,p[1]]],`rgba(238,250,255,.3)`,1.2,false)});
strokePath([[2.24,-.16,-.98],[2.62,-.08,-.78],[2.78,.02,-.42],[2.82,.02,.42],[2.62,-.08,.78],[2.24,-.16,.98]],panel,2,false);strokePath([[2.24,-.23,-.9],[2.62,-.15,-.62],[2.78,-.13,-.28],[2.82,-.13,.28],[2.62,-.15,.62],[2.24,-.23,.9]],panel,1.6,false);strokePath([[2.36,-.02,-.22],[2.5,.0,-.08],[2.52,.0,.08],[2.36,-.02,.22]],glass,1.5,true)}
if(!hi){base.forEach((p,i)=>{if(i%2===0)line(ctx,P(p),P(belt[Math.min(i,belt.length-1)]),`rgba(${css('--accent-rgb','88,255,154')},.4)`,1.2)});strokePath([[-2.74,.08,-.52],[-2.92,.12,-.34],[-2.92,.12,.34],[-2.74,.08,.52]],red,2.4,false)}
const nose=P([hi?2.56:2.92,-.04,0]),hood=P([hi?1.22:1.36,.2,0]),tail=P([hi?-2.56:-2.92,.16,0]);if(!hi){line(ctx,hood,nose,amber,3);line(ctx,P([2.36,-.08,-.5]),P([2.78,-.06,-.31]),amber,2);line(ctx,P([2.36,-.08,.5]),P([2.78,-.06,.31]),amber,2);line(ctx,P([2.72,-.08,-.58]),P([2.72,-.08,.58]),amber,1.9);line(ctx,P([-2.68,.16,.55]),tail,red,2.2);line(ctx,P([-2.68,.16,-.55]),tail,red,2.2)}
if(hi){const braking=data?.brake>8||data?.gear===0,glow=braking?(data?.gear===0 ? .95 : Math.max(.28,(data?.brake||0)/255)):.26;ctx.save();ctx.shadowColor=`rgba(255,79,114,${glow})`;ctx.shadowBlur=braking?30*glow:10;ctx.strokeStyle=red;ctx.lineWidth=braking?4.1:2.4;[[-2.78,.07,-.94],[-2.82,.07,-.64],[-2.82,.07,.64],[-2.78,.07,.94]].forEach(p=>{const q=P(p);if(!drawable(q))return;ctx.beginPath();ctx.arc(q.x,q.y,Math.max(5.4,q.s*.09),0,Math.PI*2);ctx.stroke()});ctx.restore()}
if(qualityName==='high'&&(data?.brake>8||data?.gear===0)){const glow=data?.gear===0 ? .95 : Math.max(.28,(data?.brake||0)/255);ctx.save();ctx.shadowColor=`rgba(255,79,114,${glow})`;ctx.shadowBlur=22*glow;line(ctx,P([-2.5,.07,-.92]),P([-2.76,.08,-.74]),red,4.2);line(ctx,P([-2.5,.07,.92]),P([-2.76,.08,.74]),red,4.2);ctx.restore()}
const vecLen=Math.min(1.85,Math.max(.35,smooth.speed/185)),vecY=qualityName==='high'?-1.08:-.89,v0=G([0,vecY,0]),moveYaw=wrap(travelHeading-bodyHeading),moveLocal=[Math.cos(moveYaw),0,Math.sin(moveYaw)],v1=G([moveLocal[0]*vecLen,vecY,moveLocal[2]*vecLen]);ctx.save();ctx.globalAlpha=qualityName==='high'?.62:1;line(ctx,v0,v1,b,impactPulse>0?4:2.4);line(ctx,G([moveLocal[0]*vecLen-Math.cos(moveYaw-.48)*.24,vecY,moveLocal[2]*vecLen-Math.sin(moveYaw-.48)*.24]),v1,b,1.7);line(ctx,G([moveLocal[0]*vecLen-Math.cos(moveYaw+.48)*.24,vecY,moveLocal[2]*vecLen-Math.sin(moveYaw+.48)*.24]),v1,b,1.7);ctx.restore();
if(!hi){ctx.fillStyle=amber;ctx.font=`${Math.max(12,nose.s*.18)}px ui-monospace,monospace`;ctx.fillText('FRONT',nose.x+8,nose.y)}
const wheels=[[1.34,-.52,.99],[1.34,-.52,-.99],[-1.42,-.52,.99],[-1.42,-.52,-.99]],hubs=[[1.34,-.12,.74],[1.34,-.12,-.74],[-1.42,-.12,.74],[-1.42,-.12,-.74]];
wheels.forEach((p,i)=>{const susp=((data?.suspensionMeters?.[i]||0)*11)+(impactPulse*(i<2?.1:-.07)),wp=[p[0],p[1]+susp,p[2]],pp=P(wp),hp=P(hubs[i]),temp=data?.tireTemp?.[i]||40,ratio=data?.slipRatio?.[i]??0,angle=data?.slipAngle?.[i]??0,slip=Math.abs(data?.combinedSlip?.[i]??data?.tireSlip?.[i]??0),wet=data?.puddle?.[i]||0,rot=smooth.steer*(i<2?.012:0)+angle*.55;line(ctx,hp,pp,'rgba(238,250,255,.54)',2);line(ctx,P([p[0]-.28,wp[1],p[2]]),P([p[0]+.28,wp[1],p[2]]),'rgba(238,250,255,.28)',1.4);ctx.strokeStyle=slipColor(slip);ctx.lineWidth=(qualityName==='low'?6:7)+Math.min(11,slip*5+Math.abs(ratio)*2);ctx.beginPath();ctx.ellipse(pp.x,pp.y,Math.max(8,pp.s*.15),Math.max(18,pp.s*.3),rot,0,Math.PI*2);ctx.stroke();if(Math.abs(angle)>.05){line(ctx,P([p[0]-.32,wp[1]+.02,p[2]-angle*.36]),P([p[0]+.32,wp[1]+.02,p[2]+angle*.36]),slipColor(angle),2)}if(wet){ctx.strokeStyle=b;ctx.lineWidth=2;ctx.stroke()}ctx.fillStyle=tempColor(temp);ctx.font=`${Math.max(11,pp.s*.16)}px ui-monospace,monospace`;ctx.fillText(tireKeys[i].toUpperCase(),pp.x+11,pp.y-16)});
if(impactPulse>.04){ctx.strokeStyle=`rgba(255,79,114,${impactPulse})`;ctx.lineWidth=3;ctx.beginPath();ctx.ellipse(w/2,h*.58,Math.min(w,h)*.32*cam.scale*(1+impactPulse*.12),Math.min(w,h)*.09*cam.scale*(1+impactPulse*.12),0,0,Math.PI*2);ctx.stroke()}
ctx.strokeStyle=`rgba(${css('--accent-rgb','88,255,154')},.25)`;ctx.lineWidth=2;ctx.beginPath();ctx.ellipse(w/2,h*.58,Math.min(w,h)*.28*cam.scale,Math.min(w,h)*.075*cam.scale,0,0,Math.PI*2);ctx.stroke();
ctx.fillStyle='rgba(238,250,255,.84)';ctx.font=`${Math.max(13,Math.min(w,h)*.018)}px ui-monospace,monospace`;ctx.fillText(`roll ${smooth.roll.toFixed(1)}°  pitch ${(-smooth.pitch).toFixed(1)}°  slip ${(smooth.slipAngle*57.2958).toFixed(0)}°  zoom ${(cam.scale*100).toFixed(0)}%`,w/2-185,h*.78)}
function drawRadar(){const c=E.radar,ctx=c.getContext('2d'),w=c.width,h=c.height,cx=w/2,cy=h/2,r=w*.38,a=css('--accent','#58ff9a'),lat=Math.max(-4,Math.min(4,smooth.lat)),long=Math.max(-4,Math.min(4,smooth.long));ctx.clearRect(0,0,w,h);ctx.strokeStyle='rgba(135,166,179,.23)';ctx.lineWidth=1;for(let i=1;i<=4;i++){ctx.beginPath();ctx.arc(cx,cy,r*i/4,0,Math.PI*2);ctx.stroke()}ctx.beginPath();ctx.moveTo(cx-r,cy);ctx.lineTo(cx+r,cy);ctx.moveTo(cx,cy-r);ctx.lineTo(cx,cy+r);ctx.stroke();ctx.strokeStyle='rgba(255,212,92,.55)';ctx.lineWidth=3;ctx.beginPath();ctx.moveTo(cx,cy);ctx.lineTo(cx+lat/4*r,cy-long/4*r);ctx.stroke();ctx.fillStyle=a;ctx.beginPath();ctx.arc(cx+lat/4*r,cy-long/4*r,7,0,Math.PI*2);ctx.fill()}
function drawRoute(){const c=E.route,ctx=c.getContext('2d'),w=c.width,h=c.height,cx=w/2,cy=h/2,r=w*.42,b=css('--accent2','#38d8ff');ctx.clearRect(0,0,w,h);ctx.strokeStyle='rgba(135,166,179,.18)';ctx.lineWidth=1;ctx.beginPath();ctx.arc(cx,cy,r,0,Math.PI*2);ctx.stroke();ctx.beginPath();ctx.moveTo(cx-r,cy);ctx.lineTo(cx+r,cy);ctx.moveTo(cx,cy-r);ctx.lineTo(cx,cy+r);ctx.stroke();if(track.length<2)return;let minX=track[0].x,maxX=track[0].x,minZ=track[0].z,maxZ=track[0].z;track.forEach(p=>{minX=Math.min(minX,p.x);maxX=Math.max(maxX,p.x);minZ=Math.min(minZ,p.z);maxZ=Math.max(maxZ,p.z)});const span=Math.max(36,maxX-minX,maxZ-minZ),s=r*1.55/span,ox=cx-(minX+maxX)*.5*s,oy=cy+(minZ+maxZ)*.5*s;ctx.save();ctx.beginPath();ctx.arc(cx,cy,r*.98,0,Math.PI*2);ctx.clip();ctx.strokeStyle=`rgba(${css('--accent2-rgb','56,216,255')},.9)`;ctx.lineWidth=2;ctx.setLineDash([3,5]);ctx.beginPath();track.forEach((p,i)=>{const x=ox+p.x*s,y=oy-p.z*s;if(i)ctx.lineTo(x,y);else ctx.moveTo(x,y)});ctx.stroke();ctx.setLineDash([]);const last=track[track.length-1],lx=ox+last.x*s,ly=oy-last.z*s,body=smooth.bodyHeading,travel=smooth.travelHeading;ctx.fillStyle=b;ctx.beginPath();ctx.arc(lx,ly,4,0,Math.PI*2);ctx.fill();ctx.translate(lx,ly);ctx.rotate(Math.PI/2-body);ctx.strokeStyle='rgba(255,255,255,.78)';ctx.lineWidth=1.7;ctx.beginPath();ctx.moveTo(0,-11);ctx.lineTo(6,6);ctx.lineTo(-6,6);ctx.closePath();ctx.stroke();ctx.rotate(body-travel);ctx.strokeStyle=`rgba(${css('--accent-rgb','88,255,154')},.7)`;ctx.beginPath();ctx.moveTo(0,0);ctx.lineTo(0,-18);ctx.stroke();ctx.restore()}
function updateHud(d){const kmh=Math.min(999.9,Math.max(0,d.speedKmh||0));E.speed.textContent=kmh.toFixed(1).padStart(5,'0');E.rpm.textContent=Math.round(d.rpm).toLocaleString();E.rpmBar.style.width=Math.min(100,d.rpm/Math.max(1,d.maxRpm)*100)+'%';E.gear.textContent=gear(d.gear);E.driveState.textContent=d.gear===0?'REV':'FWD';E.driveState.classList.toggle('hotPill',d.gear===0);E.impactState.textContent='IMPACT '+String(Math.min(99,Math.round(impactPulse*100))).padStart(2,'0')+'%';E.impactState.classList.toggle('hotPill',impactPulse>.18);const sp=Math.max(-100,Math.min(100,d.steer/127*100));E.steer.textContent=(sp>=0?'+':'')+sp.toFixed(1)+'%';E.steerBar.style.left=(sp<0?50-Math.abs(sp)/2:50)+'%';E.steerBar.style.width=Math.abs(sp)/2+'%';E.throttle.style.width=pct(d.accel)+'%';E.brake.style.width=pct(d.brake)+'%';E.throttleText.textContent=pct(d.accel).toFixed(1)+'%';E.brakeText.textContent=pct(d.brake).toFixed(1)+'%';E.power.textContent=(d.powerPs||0).toFixed(1)+' PS';E.torque.textContent=(d.torque||0).toFixed(1)+' Nm';E.vehicle.textContent=(d.carName&&d.carName.length?d.carName:'#'+d.carOrdinal)+'  '+piClass(d.pi,d.classLabel)+' / '+d.pi;tireKeys.forEach((k,i)=>{const temp=d.tireTemp?.[i]||0,ratio=d.slipRatio?.[i]??0,angle=d.slipAngle?.[i]??0,slip=d.combinedSlip?.[i]??d.tireSlip?.[i]??0,absSlip=Math.abs(slip);$(`${k}Temp`).textContent=fixedAbs(temp,1,5)+'℃';$(`${k}Temp`).style.color=tempColor(temp);$(`${k}Slip`).textContent=absSlip.toFixed(2).padStart(4,'0');$(`${k}Slip`).style.color=slipColor(absSlip);$(`${k}SlipBar`).style.width=Math.min(100,absSlip*80)+'%';$(`${k}Ratio`).textContent=fixedSigned(ratio,2);$(`${k}Angle`).textContent=fixedSigned(angle,2);$(`${k}Wet`).style.width=(d.puddle?.[i]?100:0)+'%'})}
function tick(){const now=performance.now(),dt=Math.min(.08,(now-lastFrameAt)/1000);lastFrameAt=now;if(demoMode){data=demoData(now/1000);lastRx=now;rxCount=60}if(now-lastHzAt>1000){E.hz.textContent=(demoMode?'SIM':rxCount+' Hz');rxCount=0;lastHzAt=now}const ok=data&&data.signal&&now-lastRx<1800;if(ok){E.state.textContent=demoMode?'TEST':(data.race?'LIVE':'PAUSED');E.state.className=data.race||demoMode?'pill live':'pill';ageAvg=ageAvg?ageAvg*.86+data.ageMs*.14:data.ageMs;E.age.textContent=latency(ageAvg);const decel=(prevSpeed-(data.speedKmh||0))/Math.max(.016,dt);if(decel>120)impactPulse=Math.min(1,impactPulse+(decel-120)/260);prevSpeed=data.speedKmh||0;impactPulse=Math.max(0,impactPulse-dt*1.9);gridPhase+=(data.speedKmh||0)*dt*.018;smooth.speed=lerp(smooth.speed,data.speedKmh||0,.16);smooth.rpm=lerp(smooth.rpm,data.rpm||0,.16);smooth.roll=lerp(smooth.roll,data.rollDeg||0,.12);smooth.pitch=lerp(smooth.pitch,data.pitchDeg||0,.12);smooth.steer=lerp(smooth.steer,data.steer||0,.18);smooth.lat=lerp(smooth.lat,-(data.latG||0),.22);smooth.long=lerp(smooth.long,-(data.longG||0),.22);let travelHeading=null;if(data.position3){if(trackCar!==data.carOrdinal){trackCar=data.carOrdinal;origin=null;track=[]}if(!origin)origin={x:data.position3[0],z:data.position3[2]};let p={x:data.position3[0]-origin.x,z:data.position3[2]-origin.z};const prev=track[track.length-1];if(prev&&Math.hypot(p.x-prev.x,p.z-prev.z)>320){origin={x:data.position3[0],z:data.position3[2]};track=[];p={x:0,z:0}}const last=track[track.length-1];const move=last?Math.hypot(p.x-last.x,p.z-last.z):0;if(last&&move>.12){travelHeading=Math.atan2(p.z-last.z,p.x-last.x)}if(!last||move>.8)track.push(p);if(track.length>420)track.shift();smooth.x=lerp(smooth.x,p.x,.24);smooth.z=lerp(smooth.z,p.z,.24)}if(travelHeading!==null){smooth.travelHeading=lerpAngle(smooth.travelHeading,travelHeading,.24)}const yawRad=(data.yawDeg||0)/57.2957795,bodyHeading=wrap(Math.PI/2-yawRad),v=data.velocity||[0,0,0],forward=v[2]??0,side=v[0]??0,localHeading=Math.atan2(side,forward||.0001);smooth.bodyHeading=lerpAngle(smooth.bodyHeading,bodyHeading,.22);smooth.slipAngle=lerpAngle(smooth.slipAngle,localHeading,.22);updateHud(data)}else{E.state.textContent='NO SIGNAL';E.state.className='pill warn';impactPulse=Math.max(0,impactPulse-dt)}drawScene();drawRadar();drawRoute();requestAnimationFrame(tick)}
const __holoRO=new ResizeObserver(()=>{resize();['radar','route','scene'].map(id=>document.getElementById(id)).filter(Boolean).forEach(c=>{const r=c.getBoundingClientRect(),d=Math.min(quality.dpr||1,devicePixelRatio||1);if(r.width&&r.height&&(c.width!==Math.round(r.width*d)||c.height!==Math.round(r.height*d))){c.width=Math.round(r.width*d);c.height=Math.round(r.height*d);}})});
[document.body,E.scene,E.radar,E.route].filter(Boolean).forEach(n=>__holoRO.observe(n));
connect();tick();
</script>
</body>
</html>
)HOLO";

uint32_t readU32(const uint8_t* b, size_t& o) {
  uint32_t v = (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
  o += 4;
  return v;
}

int32_t readS32(const uint8_t* b, size_t& o) {
  return (int32_t)readU32(b, o);
}

uint16_t readU16(const uint8_t* b, size_t& o) {
  uint16_t v = (uint16_t)b[o] | ((uint16_t)b[o + 1] << 8);
  o += 2;
  return v;
}

uint8_t readU8(const uint8_t* b, size_t& o) {
  return b[o++];
}

int8_t readS8(const uint8_t* b, size_t& o) {
  return (int8_t)b[o++];
}

float readF32(const uint8_t* b, size_t& o) {
  uint32_t raw = readU32(b, o);
  float v;
  memcpy(&v, &raw, sizeof(v));
  return v;
}

void parsePacket(const uint8_t* b, Telemetry& t) {
  size_t o = 0;
  t.isRaceOn = readS32(b, o);
  t.timestampMs = readU32(b, o);
  t.engineMaxRpm = readF32(b, o);
  t.engineIdleRpm = readF32(b, o);
  t.currentEngineRpm = readF32(b, o);
  t.accelerationX = readF32(b, o);
  t.accelerationY = readF32(b, o);
  t.accelerationZ = readF32(b, o);
  t.velocityX = readF32(b, o);
  t.velocityY = readF32(b, o);
  t.velocityZ = readF32(b, o);
  t.angularVelocityX = readF32(b, o);
  t.angularVelocityY = readF32(b, o);
  t.angularVelocityZ = readF32(b, o);
  t.yaw = readF32(b, o);
  t.pitch = readF32(b, o);
  t.roll = readF32(b, o);
  for (uint8_t i = 0; i < 4; i++) t.suspension[i] = readF32(b, o);
  for (uint8_t i = 0; i < 4; i++) t.tireSlipRatio[i] = readF32(b, o);
  for (uint8_t i = 0; i < 4; i++) t.wheelRotationSpeed[i] = readF32(b, o);
  for (uint8_t i = 0; i < 4; i++) t.wheelOnRumbleStrip[i] = readS32(b, o);
  for (uint8_t i = 0; i < 4; i++) t.wheelInPuddle[i] = readS32(b, o);
  for (uint8_t i = 0; i < 4; i++) t.surfaceRumble[i] = readF32(b, o);
  for (uint8_t i = 0; i < 4; i++) t.tireSlipAngle[i] = readF32(b, o);
  for (uint8_t i = 0; i < 4; i++) t.tireCombinedSlip[i] = readF32(b, o);
  for (uint8_t i = 0; i < 4; i++) t.suspensionMeters[i] = readF32(b, o);
  t.carOrdinal = readS32(b, o);
  t.carClass = readS32(b, o);
  t.carPerformanceIndex = readS32(b, o);
  t.drivetrainType = readS32(b, o);
  t.numCylinders = readS32(b, o);
  t.carGroup = readU32(b, o);
  t.smashableVelDiff = readF32(b, o);
  t.smashableMass = readF32(b, o);
  t.positionX = readF32(b, o);
  t.positionY = readF32(b, o);
  t.positionZ = readF32(b, o);
  t.speed = readF32(b, o);
  t.power = readF32(b, o);
  t.torque = readF32(b, o);
  for (uint8_t i = 0; i < 4; i++) t.tireTemp[i] = readF32(b, o);
  t.boost = readF32(b, o);
  t.fuel = readF32(b, o);
  t.distanceTraveled = readF32(b, o);
  t.bestLap = readF32(b, o);
  t.lastLap = readF32(b, o);
  t.currentLap = readF32(b, o);
  t.currentRaceTime = readF32(b, o);
  t.lapNumber = readU16(b, o);
  t.racePosition = readU8(b, o);
  t.accel = readU8(b, o);
  t.brake = readU8(b, o);
  t.clutch = readU8(b, o);
  t.handBrake = readU8(b, o);
  t.gear = readU8(b, o);
  t.steer = readS8(b, o);
  t.normalizedDrivingLine = readS8(b, o);
  t.normalizedAiBrakeDifference = readS8(b, o);
}

const char* classLabel(int32_t carClass, int32_t pi) {
  switch (carClass) {
    case 0: return "D";
    case 1: return "C";
    case 2: return "B";
    case 3: return "A";
    case 4: return "S1";
    case 5: return "S2";
    case 6:
    case 7: return "X";
    default: break;
  }
  if (pi >= 999) return "X";
  if (pi >= 901) return "S2";
  if (pi >= 801) return "S1";
  if (pi >= 701) return "A";
  if (pi >= 601) return "B";
  if (pi >= 501) return "C";
  return "D";
}

void appendJsonString(String& json, const char* value) {
  json += "\"";
  if (value) {
    for (const char* p = value; *p; ++p) {
      const char c = *p;
      switch (c) {
        case '\"': json += "\\\""; break;
        case '\\': json += "\\\\"; break;
        case '\b': json += "\\b"; break;
        case '\f': json += "\\f"; break;
        case '\n': json += "\\n"; break;
        case '\r': json += "\\r"; break;
        case '\t': json += "\\t"; break;
        default:
          if ((uint8_t)c < 0x20) {
            char escaped[7];
            snprintf(escaped, sizeof(escaped), "\\u%04x", (uint8_t)c);
            json += escaped;
          } else {
            json += c;
          }
      }
    }
  }
  json += "\"";
}

float tireTempCelsius(uint8_t index) {
  if (index >= 4) return 0.0f;
  return (latest.tireTemp[index] - 32.0f) * (5.0f / 9.0f);
}

void appendTelemetryJson(String& json) {
  const bool hasSignal = millis() - lastPacketMs <= SIGNAL_TIMEOUT_MS;
  const CarDataEntry* carData = findCarData(latest.carOrdinal);
  json.reserve(2400);
  json = "{";
  json += "\"signal\":" + String(hasSignal ? "true" : "false");
  json += ",\"race\":" + String(latest.isRaceOn == 1 ? "true" : "false");
  json += ",\"ageMs\":" + String(lastPacketMs == 0 ? 999999 : millis() - lastPacketMs);
  json += ",\"packets\":" + String(packetsReceived);
  json += ",\"timestamp\":" + String(latest.timestampMs);
  json += ",\"speedKmh\":" + String(latest.speed * 3.6f, 1);
  json += ",\"rpm\":" + String(latest.currentEngineRpm, 0);
  json += ",\"maxRpm\":" + String(latest.engineMaxRpm, 0);
  json += ",\"gear\":" + String(latest.gear);
  json += ",\"accel\":" + String(latest.accel);
  json += ",\"brake\":" + String(latest.brake);
  json += ",\"clutch\":" + String(latest.clutch);
  json += ",\"handBrake\":" + String(latest.handBrake);
  json += ",\"steer\":" + String(latest.steer);
  json += ",\"powerKw\":" + String(latest.power / 1000.0f, 1);
  json += ",\"powerPs\":" + String(latest.power / 735.49875f, 1);
  json += ",\"torque\":" + String(latest.torque, 1);
  json += ",\"boost\":" + String(latest.boost, 2);
  json += ",\"fuel\":" + String(latest.fuel, 3);
  json += ",\"latG\":" + String(latest.accelerationX / 9.80665f, 3);
  json += ",\"longG\":" + String(latest.accelerationZ / 9.80665f, 3);
  json += ",\"vertG\":" + String(latest.accelerationY / 9.80665f, 3);
  json += ",\"yawDeg\":" + String(latest.yaw * 57.2957795f, 1);
  json += ",\"pitchDeg\":" + String(latest.pitch * 57.2957795f, 1);
  json += ",\"rollDeg\":" + String(latest.roll * 57.2957795f, 1);
  json += ",\"velocity\":[" + String(latest.velocityX, 2) + "," + String(latest.velocityY, 2) + "," + String(latest.velocityZ, 2) + "]";
  json += ",\"accelMps2\":[" + String(latest.accelerationX, 2) + "," + String(latest.accelerationY, 2) + "," + String(latest.accelerationZ, 2) + "]";
  json += ",\"angularVelocity\":[" + String(latest.angularVelocityX, 3) + "," + String(latest.angularVelocityY, 3) + "," + String(latest.angularVelocityZ, 3) + "]";
  json += ",\"position3\":[" + String(latest.positionX, 1) + "," + String(latest.positionY, 1) + "," + String(latest.positionZ, 1) + "]";
  json += ",\"tireTemp\":[" + String(tireTempCelsius(0), 1) + "," + String(tireTempCelsius(1), 1) + "," + String(tireTempCelsius(2), 1) + "," + String(tireTempCelsius(3), 1) + "]";
  json += ",\"tireTempUnit\":\"C\"";
  json += ",\"tireSlip\":[" + String(latest.tireCombinedSlip[0], 3) + "," + String(latest.tireCombinedSlip[1], 3) + "," + String(latest.tireCombinedSlip[2], 3) + "," + String(latest.tireCombinedSlip[3], 3) + "]";
  json += ",\"slipRatio\":[" + String(latest.tireSlipRatio[0], 3) + "," + String(latest.tireSlipRatio[1], 3) + "," + String(latest.tireSlipRatio[2], 3) + "," + String(latest.tireSlipRatio[3], 3) + "]";
  json += ",\"slipAngle\":[" + String(latest.tireSlipAngle[0], 3) + "," + String(latest.tireSlipAngle[1], 3) + "," + String(latest.tireSlipAngle[2], 3) + "," + String(latest.tireSlipAngle[3], 3) + "]";
  json += ",\"combinedSlip\":[" + String(latest.tireCombinedSlip[0], 3) + "," + String(latest.tireCombinedSlip[1], 3) + "," + String(latest.tireCombinedSlip[2], 3) + "," + String(latest.tireCombinedSlip[3], 3) + "]";
  json += ",\"suspension\":[" + String(latest.suspension[0], 3) + "," + String(latest.suspension[1], 3) + "," + String(latest.suspension[2], 3) + "," + String(latest.suspension[3], 3) + "]";
  json += ",\"suspensionMeters\":[" + String(latest.suspensionMeters[0], 3) + "," + String(latest.suspensionMeters[1], 3) + "," + String(latest.suspensionMeters[2], 3) + "," + String(latest.suspensionMeters[3], 3) + "]";
  json += ",\"wheelRotationSpeed\":[" + String(latest.wheelRotationSpeed[0], 2) + "," + String(latest.wheelRotationSpeed[1], 2) + "," + String(latest.wheelRotationSpeed[2], 2) + "," + String(latest.wheelRotationSpeed[3], 2) + "]";
  json += ",\"surfaceRumble\":[" + String(latest.surfaceRumble[0], 3) + "," + String(latest.surfaceRumble[1], 3) + "," + String(latest.surfaceRumble[2], 3) + "," + String(latest.surfaceRumble[3], 3) + "]";
  json += ",\"rumble\":[" + String(latest.wheelOnRumbleStrip[0]) + "," + String(latest.wheelOnRumbleStrip[1]) + "," + String(latest.wheelOnRumbleStrip[2]) + "," + String(latest.wheelOnRumbleStrip[3]) + "]";
  json += ",\"puddle\":[" + String(latest.wheelInPuddle[0]) + "," + String(latest.wheelInPuddle[1]) + "," + String(latest.wheelInPuddle[2]) + "," + String(latest.wheelInPuddle[3]) + "]";
  json += ",\"lap\":" + String(latest.lapNumber);
  json += ",\"position\":" + String(latest.racePosition);
  json += ",\"bestLap\":" + String(latest.bestLap, 3);
  json += ",\"lastLap\":" + String(latest.lastLap, 3);
  json += ",\"currentLap\":" + String(latest.currentLap, 3);
  json += ",\"raceTime\":" + String(latest.currentRaceTime, 3);
  json += ",\"distance\":" + String(latest.distanceTraveled, 1);
  json += ",\"carOrdinal\":" + String(latest.carOrdinal);
  json += ",\"carName\":";
  appendJsonString(json, carData ? carData->name : "");
  json += ",\"carType\":";
  appendJsonString(json, carData ? carData->type : "");
  json += ",\"carClass\":" + String(latest.carClass);
  json += ",\"classLabel\":\"" + String(classLabel(latest.carClass, latest.carPerformanceIndex)) + "\"";
  json += ",\"pi\":" + String(latest.carPerformanceIndex);
  json += ",\"drivetrain\":" + String(latest.drivetrainType);
  json += ",\"cylinders\":" + String(latest.numCylinders);
  json += ",\"carGroup\":" + String(latest.carGroup);
  json += "}";
}

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleHolo() {
  server.send_P(200, "text/html; charset=utf-8", HOLO_HTML);
}

void handleTelemetry() {
  String json;
  appendTelemetryJson(json);
  server.send(200, "application/json", json);
}

void handleStatus() {
  String json;
  json.reserve(480);
  json = "{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  json += ",\"rssi\":" + String(WiFi.RSSI());
  json += ",\"udpPort\":" + String(UDP_PORT);
  json += ",\"wsPort\":" + String(WS_PORT);
  json += ",\"packetsReceived\":" + String(packetsReceived);
  json += ",\"packetsRejected\":" + String(packetsRejected);
  json += ",\"websocketClients\":" + String(websocketClients);
  json += ",\"freeHeap\":" + String(ESP.getFreeHeap());
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  (void)payload;
  (void)length;
  if (type == WStype_CONNECTED || type == WStype_DISCONNECTED) {
    websocketClients = webSocket.connectedClients();
    Serial.printf("WebSocket client %u %s, clients=%lu\n", num, type == WStype_CONNECTED ? "connected" : "disconnected", (unsigned long)websocketClients);
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  uint32_t now = millis();
  if (lastWifiAttemptMs != 0 && now - lastWifiAttemptMs < WIFI_RETRY_MS) return;
  lastWifiAttemptMs = now;

  Serial.printf("Connecting Wi-Fi SSID '%s'\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
#if defined(FH6_USE_STATIC_IP)
  IPAddress ip(FH6_STATIC_IP);
  IPAddress gateway(FH6_GATEWAY_IP);
  IPAddress subnet(FH6_SUBNET_MASK);
  IPAddress dns(FH6_DNS_IP);
  WiFi.config(ip, gateway, subnet, dns);
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void stopNetworkServices() {
  if (!networkServicesStarted) return;
  webSocket.disconnect();
  webSocket.close();
  udp.stop();
  websocketClients = 0;
  networkServicesStarted = false;
  Serial.println("Wi-Fi disconnected; network services paused.");
}

void startNetworkServices() {
  if (networkServicesStarted || WiFi.status() != WL_CONNECTED) return;

  udp.begin(UDP_PORT);
  if (!routesRegistered) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/holo", HTTP_GET, handleHolo);
    server.on("/api/telemetry", HTTP_GET, handleTelemetry);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.onNotFound(handleNotFound);
    routesRegistered = true;
  }
  server.begin();

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  networkServicesStarted = true;
  Serial.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Open http://%s/\n", WiFi.localIP().toString().c_str());
  Serial.printf("Set FH6 Data Out IP to %s and port to %u.\n", WiFi.localIP().toString().c_str(), UDP_PORT);
}

void processUdp() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) return;
  if ((size_t)packetSize != FH6_PACKET_SIZE) {
    while (udp.available()) udp.read();
    packetsRejected++;
    return;
  }
  int bytesRead = udp.read(packetBuffer, FH6_PACKET_SIZE);
  if (bytesRead != (int)FH6_PACKET_SIZE) {
    packetsRejected++;
    return;
  }
  parsePacket(packetBuffer, latest);
  packetsReceived++;
  lastPacketMs = millis();
}

void pushTelemetry() {
  uint32_t now = millis();
  if (websocketClients == 0 || now - lastPushMs < WS_PUSH_MS) return;
  lastPushMs = now;
  String json;
  appendTelemetryJson(json);
  webSocket.broadcastTXT(json);
}

void printStatus() {
  uint32_t now = millis();
  if (now - lastStatusPrintMs < STATUS_PRINT_MS) return;
  lastStatusPrintMs = now;
  Serial.printf("IP=%s UDP=%u HTTP=%u WS=%u packets=%lu rejected=%lu clients=%lu heap=%lu\n",
                WiFi.localIP().toString().c_str(),
                UDP_PORT,
                HTTP_PORT,
                WS_PORT,
                (unsigned long)packetsReceived,
                (unsigned long)packetsRejected,
                (unsigned long)websocketClients,
                (unsigned long)ESP.getFreeHeap());
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("FH6 Telemetry ESP32-S3");

  connectWiFi();
  uint32_t startWait = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWait < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi not connected yet; continuing retries in loop.");
  }
  startNetworkServices();
}

void loop() {
  wl_status_t wifiStatus = WiFi.status();
  if (previousWiFiStatus == WL_CONNECTED && wifiStatus != WL_CONNECTED) {
    stopNetworkServices();
  }
  previousWiFiStatus = wifiStatus;
  connectWiFi();
  startNetworkServices();
  if (networkServicesStarted) {
    processUdp();
    server.handleClient();
    webSocket.loop();
    pushTelemetry();
  }
  printStatus();
}
