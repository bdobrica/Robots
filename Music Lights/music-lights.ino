#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <Adafruit_NeoPixel.h>

#ifndef LOCAL_SSID
/**
 * Update the WiFi network connection details.
 */
#define LOCAL_SSID  "the-wifi-network-name"
#define LOCAL_PASS  "the-wifi-network-password"
#endif

#ifndef LOCAL_HOSTNAME
#define LOCAL_HOSTNAME
/**
 * Remember to update the hostname so you can easy find it on the network.
 */
#define HOSTNAME "hostname"
#endif

#ifndef EEPROM_ADDR
#define EEPROM_ADDR                 0x20
#define EEPROM_ADDR_HUE_HIGH        0x21
#define EEPROM_ADDR_HUE_LOW         0x22
#define EEPROM_ADDR_ALL_PIXELS_HIGH 0x23
#define EEPROM_ADDR_ALL_PIXELS_LOW  0x24
#define EEPROM_ADDR_LENGTH_HIGH     0x25
#define EEPROM_ADDR_LENGTH_LOW      0x26
#define EEPROM_ADDR_MODE            0x27
#define EEPROM_ADDR_OFFSET_HIGH     0x28
#define EEPROM_ADDR_OFFSET_LOW      0x29
#endif

const char* ssid     = LOCAL_SSID;
const char* password = LOCAL_PASS;

ESP8266WebServer server(80);
Adafruit_NeoPixel * pixels;

uint16_t _min = 1023;
uint16_t _max = 0;

uint16_t _hue = 40000;
uint16_t _all_pixels = 256;
uint16_t _length = 32;
uint16_t _offset = 0;
uint8_t _mode = 1;

uint32_t black = pixels->Color(0, 0, 0);

uint16_t pixel = 0;
uint16_t pos = 0;
uint16_t pos_step = 0;
uint16_t last_pixel = 0;

void eeprom_read() {
  if (EEPROM.read(EEPROM_ADDR) != '#') {
    return;
  }
  _hue = (uint16_t) EEPROM.read(EEPROM_ADDR_HUE_HIGH) << 8 | (uint16_t) EEPROM.read(EEPROM_ADDR_HUE_LOW);
  _all_pixels = (uint16_t) EEPROM.read(EEPROM_ADDR_ALL_PIXELS_HIGH) << 8 | (uint16_t) EEPROM.read(EEPROM_ADDR_ALL_PIXELS_LOW);
  _length = (uint16_t) EEPROM.read(EEPROM_ADDR_LENGTH_HIGH) << 8 | (uint16_t) EEPROM.read(EEPROM_ADDR_LENGTH_LOW);
  _mode = EEPROM.read(EEPROM_ADDR_MODE);
  _offset = (uint16_t) EEPROM.read(EEPROM_ADDR_OFFSET_HIGH) << 8 | (uint16_t) EEPROM.read(EEPROM_ADDR_OFFSET_LOW);
}

void eeprom_commit() {
  EEPROM.write(EEPROM_ADDR, '#');
  EEPROM.write(EEPROM_ADDR_HUE_HIGH, (uint8_t) ((_hue >> 8) & 0xFF));
  EEPROM.write(EEPROM_ADDR_HUE_LOW, (uint8_t) (_hue & 0xFF));
  EEPROM.write(EEPROM_ADDR_ALL_PIXELS_HIGH, (uint8_t) ((_all_pixels >> 8) & 0xFF));
  EEPROM.write(EEPROM_ADDR_ALL_PIXELS_LOW, (uint8_t) (_all_pixels & 0xFF));
  EEPROM.write(EEPROM_ADDR_LENGTH_HIGH, (uint8_t) ((_length >> 8) & 0xFF));
  EEPROM.write(EEPROM_ADDR_LENGTH_LOW, (uint8_t) (_length & 0xFF));
  EEPROM.write(EEPROM_ADDR_MODE, _mode);
  EEPROM.write(EEPROM_ADDR_OFFSET_HIGH, (uint8_t) ((_offset >> 8) & 0xFF));
  EEPROM.write(EEPROM_ADDR_OFFSET_LOW, (uint8_t) (_offset & 0xFF));
  EEPROM.commit();
}

void handle_root() {
  server.send(200, "text/html", "");
}

void handle_404() {
  server.send(404, "application/json", "{\"error\":1,\"message\":\"not found\"}");
}

void handle_form () {
  bool commit = false;
  long convert;
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":1,\"message\":\"method not allowed\"}");
    return;
  }
  for (uint8_t c = 0; c < server.args(); c++) {
    if (server.argName(c) == String("mode")) {
      convert = server.arg(c).toInt();
      _mode = convert < 0 ? 0 : (convert > 255 ? 255 : (uint8_t) convert);
      commit = true;
    }
    if (server.argName(c) == String("hue")) {
      convert = server.arg(c).toInt();
      _hue = convert < 0 ? 0 : (convert > 65535 ? 65535 : (uint16_t) convert);
      commit = true;
    }
    if (server.argName(c) == String("length")) {
      convert = server.arg(c).toInt();
      _length = convert <  0 ? 0 : (convert > _all_pixels ? _all_pixels : (uint16_t) convert);
      commit = true;
    }
    if (server.argName(c) == String("pixels")) {
      convert = server.arg(c).toInt();
      _all_pixels = convert <  0 ? 0 : (convert > 65535 ? 65535 : (uint16_t) convert);
      commit = true;
      
      if (pixels) {
        delete pixels;
      }
      pixels = new Adafruit_NeoPixel(_all_pixels, D5, NEO_GRB + NEO_KHZ800);
      pixels->begin();
      pixels->clear();
    }    
    if (server.argName(c) == String("reset") && server.arg(c) == String("on")) {
      _min = 1023;
      _max = 0;
    }
  }
  if (commit) {
    Serial.println("saving data to eeprom");
    eeprom_commit();
  }
  server.send (200, "application/json", "{\"mode\":" + String(_mode) + ",\"length\":" + String(_length) + ",\"hue\":" + String(_hue) + ",\"min\":" + String(_min) + ",\"max\":" + String(_max) + "}");
}

void fill_pixels_rainbow(uint8_t brightness, uint16_t start_pos, uint16_t end_pos) {
  uint32_t color;
  uint16_t hue = _hue;
  float hue_step = 65536.0 / (float)_length;
  uint16_t pixel = 0;
  
  if (start_pos + _length == end_pos) {
    for (uint16_t c = start_pos; c < end_pos; ++c) {
      pixels->setPixelColor(c, pixels->ColorHSV(hue, 255, brightness));
      hue = (uint16_t)(hue + pixel * hue_step);
      pixel++;
    }
  }
  else {
    for (uint16_t c = start_pos; c < _all_pixels; ++c) {
      pixels->setPixelColor(c, pixels->ColorHSV(hue, 255, brightness));
      hue = (uint16_t)(hue + pixel * hue_step);
      pixel++;
    }
    for (uint16_t c = 0; c < end_pos; ++c) {
      pixels->setPixelColor(c, pixels->ColorHSV(hue, 255, brightness));
      hue = (uint16_t)(hue + pixel * hue_step);
      pixel++;
    }
  }
}

void fill_pixels_vumeter(uint8_t brightness, uint16_t start_pos, uint16_t end_pos) {
  uint32_t color;
  uint16_t hue = _hue;
  uint16_t hue_step = _hue + (uint16_t)(0.66 * 65536);
  uint16_t pixel = 0;
  uint16_t threshold = (uint16_t)(0.75 * _length);
  
  if (start_pos + _length == end_pos) {
    for (uint16_t c = start_pos; c < end_pos; ++c) {
      if (pixel++ > threshold) {
        color = pixels->gamma32(pixels->ColorHSV(hue + hue_step, 255, brightness));
      }
      else {
        color = pixels->gamma32(pixels->ColorHSV(hue, 255, brightness));
      }
      pixels->setPixelColor(c, color);
    }
  }
  else {
    for (uint16_t c = start_pos; c < _all_pixels; ++c) {
      if (pixel++ > threshold) {
        color = pixels->gamma32(pixels->ColorHSV(hue + hue_step, 255, brightness));
      }
      else {
        color = pixels->gamma32(pixels->ColorHSV(hue, 255, brightness));
      }
      pixels->setPixelColor(c, color);
    }
    for (uint16_t c = 0; c < end_pos; ++c) {
      if (pixel++ > threshold) {
        color = pixels->gamma32(pixels->ColorHSV(hue + hue_step, 255, brightness));
      }
      else {
        color = pixels->gamma32(pixels->ColorHSV(hue, 255, brightness));
      }
      pixels->setPixelColor(c, color);
    }
  }
}

void fill_pixels(uint32_t color, uint16_t start_pos, uint16_t end_pos) {
  if (start_pos + _length == end_pos) {
    for (uint16_t c = start_pos; c < end_pos; ++c) {
      pixels->setPixelColor(c, color);
    }
  }
  else {
    for (uint16_t c = start_pos; c < _all_pixels; ++c) {
      pixels->setPixelColor(c, color);
    }
    for (uint16_t c = 0; c < end_pos; ++c) {
      pixels->setPixelColor(c, color);
    }
  }
}

void setup() {
  EEPROM.begin(512);
  eeprom_read();
  
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.begin(ssid, password);
  Serial.print("hostname:");
  Serial.println(HOSTNAME);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("#");
  }
  
  Serial.println("");
  Serial.println(WiFi.localIP());
  server.on("/", handle_root);
  server.on("/rpc/", handle_form);
  server.onNotFound(handle_404);
  server.begin();

  pixels = new Adafruit_NeoPixel(_all_pixels, D5, NEO_GRB + NEO_KHZ800);
  
  Serial.print("pixels: ");
  Serial.println(_all_pixels);
  Serial.print("length: ");
  Serial.println(_length);
  
  pixels->begin();
  pixels->clear();
  Serial.print("mode: ");
  Serial.println(_mode);
  Serial.print("hue: ");
  Serial.println(_hue);

  pinMode (D6, OUTPUT);
  pinMode (D7, OUTPUT);
  pinMode (D8, OUTPUT);
  digitalWrite (D6, LOW);
  digitalWrite (D7, LOW);
  digitalWrite (D8, LOW);
}

void loop() {
  // put your main code here, to run repeatedly:
  uint8_t
    b_map,
    part,
    parts = (uint8_t) (_all_pixels / _length),
    offset = 1;
  uint16_t
    a_val,
    a_map,
    len,
    start_pos,
    end_pos;
  uint32_t
    color;
  
  server.handleClient();
  
  a_val = analogRead(A0);
  if (_min > a_val) {
    _min = a_val;
  }
  if (_max < a_val) {
    _max = a_val;
  }
  //Serial.println(a_val);

  switch (_mode) {
    case 0:
      /**
       * the analog input controls the length of the light up segment and it's brightness; rainbow effect
       */
      b_map = _max > _min ? (uint8_t) (255.0 * (float)(a_val - _min) / (float) (_max - _min)) : 0;

      len = _max > _min ? (uint16_t) ((float) _length * (float)(a_val - _min) / (float) (_max - _min)) : 0;
      start_pos = _offset;
      
      for (part = 0; part < parts; ++part) {
        end_pos = (start_pos + len) % _all_pixels;
        fill_pixels_rainbow(b_map, start_pos, end_pos);
        
        start_pos = end_pos;
        end_pos = (start_pos + (_length - len)) % _all_pixels;
        
        fill_pixels(black, start_pos, end_pos);
        start_pos = end_pos;
      }
      
      end_pos = (start_pos + (_all_pixels - parts * _length)) % _all_pixels;
      fill_pixels(black, start_pos, end_pos);
      break;
    case 1:
      /**
       * the analog input controls the length of the light up segment and it's brightness; rainbow effect
       */
      b_map = _max > _min ? (uint8_t) (255.0 * (float)(a_val - _min) / (float) (_max - _min)) : 0;

      len = _max > _min ? (uint16_t) ((float) _length * (float)(a_val - _min) / (float) (_max - _min)) : 0;
      start_pos = _offset;
      
      for (part = 0; part < parts; ++part) {
        end_pos = (start_pos + len) % _all_pixels;
        fill_pixels_rainbow(b_map, start_pos, end_pos);
        
        start_pos = end_pos;
        end_pos = (start_pos + (_length - len)) % _all_pixels;
        
        fill_pixels(black, start_pos, end_pos);
        start_pos = end_pos;
      }
      
      end_pos = (start_pos + (_all_pixels - parts * _length)) % _all_pixels;
      fill_pixels(black, start_pos, end_pos);
      break;
    case 2:
      /**
       * the analog input controls the length of the light up segment and it's brightness; vu-meter effect
       */
      b_map = _max > _min ? (uint8_t) (255.0 * (float)(a_val - _min) / (float) (_max - _min)) : 0;

      len = _max > _min ? (uint16_t) ((float) _length * (float)(a_val - _min) / (float) (_max - _min)) : 0;
      start_pos = _offset;
      
      for (part = 0; part < parts; ++part) {
        end_pos = (start_pos + len) % _all_pixels;
        fill_pixels_vumeter(b_map, start_pos, end_pos);
        
        start_pos = end_pos;
        end_pos = (start_pos + (_length - len)) % _all_pixels;
        
        fill_pixels(black, start_pos, end_pos);
        start_pos = end_pos;
      }
      
      end_pos = (start_pos + (_all_pixels - parts * _length)) % _all_pixels;
      fill_pixels(black, start_pos, end_pos);
      break;
    case 3:
      /**
       * the analog input controls the length of the light up segment and it's brightness; vu-meter effect
       */
      b_map = _max > _min ? (uint8_t) (255.0 * (float)(a_val - _min) / (float) (_max - _min)) : 0;

      len = _max > _min ? (uint16_t) ((float) _length * (float)(a_val - _min) / (float) (_max - _min)) : 0;
      start_pos = _offset;
      
      for (part = 0; part < parts; ++part) {
        end_pos = (start_pos + len) % _all_pixels;
        fill_pixels_vumeter(b_map, start_pos, end_pos);
        
        start_pos = end_pos;
        end_pos = (start_pos + (_length - len)) % _all_pixels;
        
        fill_pixels(black, start_pos, end_pos);
        start_pos = end_pos;
      }
      
      end_pos = (start_pos + (_all_pixels - parts * _length)) % _all_pixels;
      fill_pixels(black, start_pos, end_pos);
      break;
    case 4:
      /**
       * the analog input controls the length of the light up segment and it's brightness; single color
       */
      b_map = _max > _min ? (uint8_t) (255.0 * (float)(a_val - _min) / (float) (_max - _min)) : 0;
      color = pixels->gamma32(pixels->ColorHSV(_hue, 255, b_map));

      len = _max > _min ? (uint16_t) ((float) _length * (float)(a_val - _min) / (float) (_max - _min)) : 0;
      start_pos = _offset;
      
      for (part = 0; part < parts; ++part) {
        end_pos = (start_pos + len) % _all_pixels;
        fill_pixels(color, start_pos, end_pos);
        
        start_pos = end_pos;
        end_pos = (start_pos + (_length - len)) % _all_pixels;
        
        fill_pixels(black, start_pos, end_pos);
        start_pos = end_pos;
      }
      
      end_pos = (start_pos + (_all_pixels - parts * _length)) % _all_pixels;
      fill_pixels(black, start_pos, end_pos);
      break;
    case 5:
      /**
       * the analog input controls the length of the light up segment and it's brightness; single color
       */
      b_map = _max > _min ? (uint8_t) (255.0 * (float)(a_val - _min) / (float) (_max - _min)) : 0;
      color = pixels->gamma32(pixels->ColorHSV(_hue, 255, b_map));

      len = _max > _min ? (uint16_t) ((float) _length * (float)(a_val - _min) / (float) (_max - _min)) : 0;
      start_pos = _offset;
      
      for (part = 0; part < parts; ++part) {
        end_pos = (start_pos + len) % _all_pixels;
        fill_pixels(color, start_pos, end_pos);
        
        start_pos = end_pos;
        end_pos = (start_pos + (_length - len)) % _all_pixels;
        
        fill_pixels(black, start_pos, end_pos);
        start_pos = end_pos;
      }
      
      end_pos = (start_pos + (_all_pixels - parts * _length)) % _all_pixels;
      fill_pixels(black, start_pos, end_pos);
      break;
  }
  pixels->show();
  delay(2);
}
