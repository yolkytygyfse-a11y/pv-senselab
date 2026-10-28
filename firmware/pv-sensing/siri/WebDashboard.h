#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <esp_partition.h>
#include "Config.h"
#include "SensorMath.h"
#include "DashboardStore.h"
#include "DashboardPage.h"

namespace dashboard {
class Web {
  WebServer server{80};
  cfg::Settings& config;
  bool& streaming;
  uint32_t& revision;
  const char *device = nullptr, *boot = nullptr;
  bool (*beginSampling)() = nullptr;
  void (*endSampling)() = nullptr;
  File journal;
  bool ready = false, full = false, failed = false, apReady = false;
  uint64_t records = 0;
  uint32_t errors = 0, lastFlush = 0;
  String ssid, password, token, filename;
  static constexpr size_t Reserve = 16384;
  static String num(double v) { return std::isfinite(v) ? String(v, 9) : String("null"); }
  static String u64(uint64_t v) { char b[24]; snprintf(b,sizeof(b),"%llu",static_cast<unsigned long long>(v)); return String(b); }
  static String boolean(bool v) { return v ? "true" : "false"; }
  static bool logName(const String& name) {
    if (name.length()!=26 || !name.startsWith("log-") || !name.endsWith(".jsonl")) return false;
    for (size_t i=4;i<20;++i) if (!isxdigit(static_cast<unsigned char>(name[i]))) return false;
    return true;
  }
  void fail(const char* reason, int code=409) {
    server.send(code,"application/json",String("{\"ok\":false,\"error\":\"")+reason+"\"}");
  }
  bool authorized() {
    if (!apReady || server.header("X-PV-Token") != token) { fail("invalid_token",403); return false; }
    return true;
  }
  void mount() {
    ready = LittleFS.begin(false);
    if (!ready) {
      // Never use formatOnFail: preserve any unrecognized, nonempty filesystem.
      const esp_partition_t* part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"spiffs");
      bool blank=part!=nullptr;
      uint8_t bytes[512];
      if (part) for (size_t off=0;blank && off<part->size;off+=sizeof(bytes)) {
        size_t n=std::min(sizeof(bytes),static_cast<size_t>(part->size)-off);
        if (esp_partition_read(part,off,bytes,n)!=ESP_OK) { blank=false; break; }
        for (size_t i=0;i<n;++i) if (bytes[i]!=0xff) { blank=false; break; }
        yield();
      }
      if (blank && LittleFS.format()) ready=LittleFS.begin(false);
    }
    if (!ready) { ++errors; failed=true; return; }
    filename="log-"+String(boot)+".jsonl";
    if (LittleFS.exists("/"+filename)) { ready=false; failed=true; ++errors; return; }
    journal=LittleFS.open("/"+filename,FILE_APPEND);
    if (!journal) { ready=false; failed=true; ++errors; }
  }
  void state() {
    const char* ids[]={"irradiance","ambient_temperature","humidity","pt100"};
    String out; out.reserve(5000);
    out="{\"version\":\""+String(cfg::VERSION)+"\",\"device_id\":\""+device+"\",\"boot_id\":\""+boot+"\",\"streaming\":"+boolean(streaming);
    out+=",\"mono_us\":"+u64(esp_timer_get_time())+",\"free_heap\":"+String(ESP.getFreeHeap())+",\"config_revision\":"+String(revision);
    out+=",\"token\":\""+token+"\",\"ap\":{\"ssid\":\""+ssid+"\",\"ip\":\""+WiFi.softAPIP().toString()+"\"}";
    out+=",\"storage\":{\"ready\":"+boolean(ready)+",\"used\":"+String(ready?LittleFS.usedBytes():0)+",\"total\":"+String(ready?LittleFS.totalBytes():0);
    out+=",\"records\":"+u64(records)+",\"errors\":"+String(errors)+",\"full\":"+boolean(full)+",\"current_file\":\""+filename+"\"}";
    out+=",\"config\":{\"period_ms\":"+String(config.periodMs)+",\"rref_ohm\":"+num(config.rrefOhm)+",\"irradiance_scale\":"+num(config.irradianceScale)+",\"env_scale\":"+num(config.envScale)+"}";
    out+=",\"cursor\":"+u64(store.cursor())+",\"max_points\":1024,\"series_epoch\":"+String(store.epoch)+",\"channels\":[";
    for (unsigned i=0;i<4;++i) {
      const Channel& c=store.channels[i]; const Stats& s=c.stats;
      if (i) out+=',';
      out+="{\"id\":\""+String(ids[i])+"\",\"unit\":\""+unit(i)+"\",\"value\":"+num(c.value)+",\"raw\":"+num(c.raw)+",\"status\":\""+c.status+"\",\"last_us\":"+u64(c.last);
      out+=",\"stats\":{\"n\":"+String(s.n)+",\"invalid\":"+String(s.invalid)+",\"min\":"+num(s.min)+",\"max\":"+num(s.max)+",\"mean\":"+num(s.n?s.mean:NAN)+",\"std\":"+num(s.stddev())+"}}";
    }
    out+="]}"; server.send(200,"application/json",out);
  }
  void history() {
    uint64_t after=0, limit=64;
    if ((server.hasArg("after")&&!pv::parseUnsigned(server.arg("after").c_str(),after)) ||
        (server.hasArg("limit")&&!pv::parseUnsigned(server.arg("limit").c_str(),limit)) || limit<1 || limit>64) { fail("invalid_cursor",400); return; }
    uint64_t first=after+1;
    bool lost=after>store.cursor() || (after!=0 && first<store.oldest());
    if (after>store.cursor() || first<store.oldest()) first=store.oldest();
    uint64_t last=std::min(store.cursor(),first+limit-1);
    String out; out.reserve(11000);
    out="{\"epoch\":"+String(store.epoch)+",\"oldest\":"+u64(store.oldest())+",\"cursor\":"+u64(last)+",\"lost\":"+boolean(lost)+",\"more\":"+boolean(last<store.cursor())+",\"points\":[";
    bool comma=false;
    for (uint64_t id=first;id<=last;++id) {
      const Point* p=store.point(id); if (!p) continue;
      if (comma) out+=','; comma=true;
      out+="{\"id\":"+u64(id)+",\"channel\":"+String(p->channel)+",\"t_us\":"+u64(p->t)+",\"value\":"+num(p->value)+",\"raw\":"+num(p->raw)+",\"status\":\""+p->status+"\"}";
    }
    out+="]}"; server.send(200,"application/json",out);
  }
  void configure() {
    if (!authorized()) return;
    if (streaming) { fail("stop_before_configuration"); return; }
    if (server.args()!=4) { fail("invalid_configuration",400); return; }
    uint64_t period=0; double rref=0, solar=0, env=0;
    if (!pv::parseUnsigned(server.arg("period_ms").c_str(),period) || period<1000 || period>60000 ||
        !pv::parseFinite(server.arg("rref_ohm").c_str(),rref) || rref<350 || rref>10000 ||
        !pv::parseFinite(server.arg("irradiance_scale").c_str(),solar) || solar<0 || solar>100 ||
        !pv::parseFinite(server.arg("env_scale").c_str(),env) || env<0 || env>100) { fail("invalid_configuration",400); return; }
    config.periodMs=period; config.rrefOhm=rref; config.irradianceScale=solar; config.envScale=env;
    ++revision; store.reset();
    server.send(200,"application/json","{\"ok\":true}");
  }
  void listFiles() {
    if (!ready) { fail("storage_unavailable",503); return; }
    journal.flush();
    // A bounded streamed response avoids allocating all archived filenames in RAM.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200,"application/json","");
    server.sendContent("{\"files\":[");
    File root=LittleFS.open("/"); File file=root.openNextFile(); bool comma=false;
    while (file) {
      String name=file.name(); if (name.startsWith("/")) name.remove(0,1);
      if (!file.isDirectory() && logName(name)) {
        if (comma) server.sendContent(","); comma=true;
        server.sendContent("{\"name\":\""+name+"\",\"bytes\":"+String(file.size())+",\"current\":"+boolean(name==filename)+"}");
      }
      file.close(); file=root.openNextFile();
    }
    root.close(); server.sendContent("]}"); server.sendContent("");
  }
public:
  Store<1024> store;
  Web(cfg::Settings& c,bool& active,uint32_t& rev):config(c),streaming(active),revision(rev) {}
  const char* unit(unsigned channel) const {
    if (channel==0) return config.irradianceScale>0?"W/m²":"count";
    if (channel==3) return "°C";
    return config.envScale>0?(channel==1?"°C":"%RH"):"count";
  }
  void observe(uint8_t channel,uint64_t t,double value,double raw,const char* status) {
    store.add(channel,t,value,raw,status);
  }
  void append(const char* data,size_t size) {
    if (!ready || failed) return;
    if (!journal || journal.write(reinterpret_cast<const uint8_t*>(data),size)!=size) { ++errors; failed=true; return; }
    ++records;
  }
  void begin(const char* id,const char* bootId,bool (*startFn)(),void (*stopFn)()) {
    device=id; boot=bootId; beginSampling=startFn; endSampling=stopFn;
    mount();
    char entropy[33]; snprintf(entropy,sizeof(entropy),"%08lx%08lx%08lx%08lx",static_cast<unsigned long>(esp_random()),static_cast<unsigned long>(esp_random()),static_cast<unsigned long>(esp_random()),static_cast<unsigned long>(esp_random()));
    token=entropy; password=token.substring(0,16); ssid="PV-SenseLab-"+String(id).substring(12);
    WiFi.persistent(false); WiFi.mode(WIFI_AP); WiFi.setSleep(false);
    apReady=WiFi.softAP(ssid.c_str(),password.c_str(),1,false,4);
    const char* headers[]={"X-PV-Token"}; server.collectHeaders(headers,1);
    server.on("/",HTTP_GET,[this](){
      server.sendHeader("Content-Encoding","gzip");
      server.sendHeader("Cache-Control","no-cache");
      server.send_P(200,"text/html; charset=utf-8",reinterpret_cast<const char*>(DASHBOARD_GZIP),sizeof(DASHBOARD_GZIP));
    });
    server.on("/api/state",HTTP_GET,[this](){state();});
    server.on("/api/history",HTTP_GET,[this](){history();});
    server.on("/api/config",HTTP_POST,[this](){configure();});
    server.on("/api/start",HTTP_POST,[this](){
      if (!authorized()) return;
      if (!canRecord()) { fail("storage_unavailable",503); return; }
      if (!streaming) {
        if (!beginSampling()) { fail("start_failed"); return; }
      }
      server.send(200,"application/json","{\"ok\":true}");
    });
    server.on("/api/stop",HTTP_POST,[this](){
      if (!authorized()) return; endSampling(); journal.flush();
      server.send(200,"application/json","{\"ok\":true}");
    });
    server.on("/api/files",HTTP_GET,[this](){listFiles();});
    server.on("/api/raw",HTTP_GET,[this](){
      String name=server.arg("name");
      if (!ready || !logName(name)) { fail("invalid_file",400); return; }
      journal.flush(); File file=LittleFS.open("/"+name,FILE_READ);
      if (!file) { fail("file_not_found",404); return; }
      server.sendHeader("Content-Disposition","attachment; filename=\""+name+"\"");
      server.streamFile(file,"application/x-ndjson"); file.close();
    });
    server.onNotFound([this](){fail("not_found",404);});
    server.begin();
  }
  bool canRecord() {
    if (!ready || failed) return false;
    full=LittleFS.totalBytes()-LittleFS.usedBytes()<Reserve;
    return !full;
  }
  bool fault() const { return failed || full; }
  void tick() {
    server.handleClient();
    if (journal && millis()-lastFlush>=1000) { journal.flush(); lastFlush=millis(); canRecord(); }
  }
  void printConnection() {
    // Explicit operator command only; never passed to the raw-journal hook.
    Serial.printf("{\"type\":\"web_connection\",\"ssid\":\"%s\",\"password\":\"%s\",\"ip\":\"%s\",\"ready\":%s}\n",ssid.c_str(),password.c_str(),WiFi.softAPIP().toString().c_str(),apReady?"true":"false");
  }
};
} // namespace dashboard
