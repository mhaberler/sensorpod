#ifdef OTA_WEB_UPDATER

#include <Arduino.h>
#include <WebServer.h>
#include <Update.h>
#include "http_server.hpp"

static const char *csrfHeaders[2] = {"Origin", "Host"};
static uint8_t otaDone = 0;

static const char OTA_BODY[] =
  "<h1>Firmware update</h1>"
  "<p><a href='/'>&larr; back</a></p>"
  "<p><b>Upload the <code>*_ota.bin</code> file, not <code>*firmware.bin</code></b> "
  "(<code>firmware.bin</code> is the merged image with bootloader + partition table, "
  "only for flashing over USB).</p>"
  "<p>Get firmware releases from "
  "<a href='https://github.com/mhaberler/sensorpod/tags' target='_blank' rel='noopener'>"
  "github.com/mhaberler/sensorpod/tags</a>.</p>"
  "<form id='f' method='POST' action='/update' enctype='multipart/form-data'>"
  "<input type='file' name='firmware' accept='.bin' required> "
  "<input type='submit' value='Upload'>"
  "</form>"
  "<p><progress id='p' value='0' max='100' style='display:none'></progress></p>"
  "<pre id='log'></pre>"
  "<script>"
  "const f=document.getElementById('f');"
  "f.addEventListener('submit',e=>{e.preventDefault();"
  "const fd=new FormData(f);const file=fd.get('firmware');"
  "const x=new XMLHttpRequest();x.open('POST','/update?size='+file.size);"
  "const p=document.getElementById('p');p.style.display='inline-block';"
  "x.upload.onprogress=ev=>{p.value=ev.loaded*100/ev.total;};"
  "x.onload=()=>{document.getElementById('log').textContent='status '+x.status+': '+x.responseText;};"
  "x.send(fd);});"
  "</script>";

static String ota_page() {
  String s;
  s.reserve(1800);
  s += "<!DOCTYPE html><html><head><meta charset='utf-8'><title>OTA</title>";
  s += HTTP_PAGE_STYLE;
  s += "</head><body>";
  s += OTA_BODY;
  s += "</body></html>";
  return s;
}

static void handleUpdateEnd() {
  http_server.sendHeader("Connection", "close");
  if (Update.hasError()) {
    http_server.send(502, "text/plain", Update.errorString());
  } else {
    http_server.sendHeader("Refresh", "10");
    http_server.sendHeader("Location", "/");
    http_server.send(307);
    delay(500);
    ESP.restart();
  }
}

static void handleUpdate() {
  HTTPUpload &upload = http_server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String origin = http_server.header(String(csrfHeaders[0]));
    String host   = http_server.header(String(csrfHeaders[1]));
    String expectedOrigin = String("http://") + host;
    if (origin.length() && origin != expectedOrigin) {
      log_w("OTA: bad Origin '%s' expected '%s'", origin.c_str(), expectedOrigin.c_str());
      otaDone = 0;
      return;
    }
    size_t fsize = http_server.hasArg("size")
                     ? (size_t)http_server.arg("size").toInt()
                     : UPDATE_SIZE_UNKNOWN;
    log_w("OTA begin %s size=%u", upload.filename.c_str(), (unsigned)fsize);
    if (!Update.begin(fsize)) {
      otaDone = 0;
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    } else if (Update.size()) {
      otaDone = 100 * Update.progress() / Update.size();
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      log_w("OTA done %u bytes, rebooting", (unsigned)upload.totalSize);
    } else {
      log_e("OTA fail: %s", Update.errorString());
      otaDone = 0;
    }
  }
}

void ota_setup(WebServer &srv) {
  srv.collectHeaders(csrfHeaders, 2);
  srv.on("/update", HTTP_GET, []() {
    http_server.send(200, "text/html", ota_page());
  });
  srv.on("/update", HTTP_POST, handleUpdateEnd, handleUpdate);
}

#endif // OTA_WEB_UPDATER
