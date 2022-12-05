#include <Arduino.h>

#include <gfx.hpp>
#include <htcw_button.hpp>
#include <st7789.hpp>
#include <tft_io.hpp>
#include <lcd_miser.hpp>
#include <fonts/Ubuntu.hpp>
#include <SPIFFS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#define LCD_WIDTH 135
#define LCD_HEIGHT 240
#define LCD_HOST VSPI
#define PIN_NUM_MISO -1
#define PIN_NUM_MOSI 19
#define PIN_NUM_CLK 18
#define PIN_NUM_CS 5
#define PIN_NUM_DC 16
#define PIN_NUM_RST 23
#define PIN_NUM_BCKL 4
using namespace arduino;
using namespace gfx;

using bus_t = tft_spi_ex<LCD_HOST, PIN_NUM_CS, PIN_NUM_MOSI, PIN_NUM_MISO, PIN_NUM_CLK, SPI_MODE0,true,LCD_WIDTH*LCD_HEIGHT*2+8,2>;
using display_t = st7789<LCD_WIDTH, LCD_HEIGHT, PIN_NUM_DC, PIN_NUM_RST, -1 /* PIN_NUM_BCKL */, bus_t, 1, true, 400, 200>;
using color_t = color<typename display_t::pixel_type>;

using button_1_t = button<35,10, true>;
using button_2_t = button<0,10, true>;
static void ensure_connected();
static void draw_room(int index);
static void play_pause(int index);
static void next_track(int index);
static display_t dsp;
static lcd_miser<PIN_NUM_BCKL> dsp_miser;
static button_1_t button_1;
static button_2_t button_2;
static HTTPClient http;
static int speaker_index = 0;
static int speaker_count = 0;
static char* speaker_strings = nullptr;
static uint32_t last_press_ts = 0;
static char play_pause_url[1024];
static char next_track_url[1024];
static char url[1024];
static char wifi_ssid[256];
static char wifi_pass[256];
static File file;
static void button_1_on_click(bool pressed,void* state) {

    if(!pressed) {
        if(!dsp_miser.dimmed()) {
            ++speaker_index;
            if(speaker_index>=speaker_count) {
                speaker_index = 0;
            }
            draw_room(speaker_index);
        }
    }
    dsp_miser.wake();
}
static void button_2_on_click(bool pressed, void* state) {
    if(pressed) {
        if(last_press_ts==0) {
            last_press_ts = millis();
        }
    } else {
        if(last_press_ts+500<=millis()) {
            if(!dsp_miser.dimmed()) {
                next_track(speaker_index);
            }
        } else {
            play_pause(speaker_index);
        }
        last_press_ts=0;
    }
    dsp_miser.wake();
}

static void ensure_connected() {
    if(WiFi.status()!=WL_CONNECTED) {
        WiFi.begin(wifi_ssid,wifi_pass);
    }
}
static void draw_center_text(const char* text) {
    open_text_info oti;
    oti.font = &Ubuntu;
    oti.text = text;
    oti.transparent_background = false;
    // 35 pixel high font
    oti.scale = oti.font->scale(35);
    // center the text
    ssize16 text_size = oti.font->measure_text(ssize16::max(),spoint16::zero(),oti.text,oti.scale);
    srect16 text_rect = text_size.bounds();
    text_rect.center_inplace((srect16)dsp.bounds());
    draw::text(dsp,text_rect,oti,color_t::white);

}
static const char* room_for_index(int index) {
    const char* sz = speaker_strings;
    for(int i = 0;i<index;++i) {
        sz = sz+strlen(sz)+1;
    }
    return sz;
}

static void play_pause(int index) {
    const char* sz = room_for_index(index);
    snprintf(url,sizeof(url),play_pause_url,sz);
    ensure_connected();
    Serial.print("Sending ");
    Serial.println(url);
    http.begin(url);
    http.GET();
    http.end();    
}

static void next_track(int index) {
    const char* sz = room_for_index(index);
    snprintf(url,sizeof(url),next_track_url,sz);
    ensure_connected();
    Serial.print("Sending ");
    Serial.println(url);
    http.begin(url);
    http.GET();
    http.end();    
}
static void draw_room(int index) {
    draw::filled_rectangle(dsp, dsp.bounds(), color_t::black);
    const char* sz = room_for_index(index);
    draw_center_text(sz);
}
void setup() {
    Serial.begin(115200);
    SPIFFS.begin();
    dsp_miser.initialize();
    button_1.initialize();
    button_2.initialize();
    button_1.callback(button_1_on_click);
    button_2.callback(button_2_on_click);
    file = SPIFFS.open("/speakers.csv");
    String s = file.readStringUntil(',');
    size_t size = 0;
    while(!s.isEmpty()) {
        if(speaker_strings==nullptr) {
            speaker_strings = (char*)malloc(s.length()+1);
            if(speaker_strings==nullptr) {
                Serial.println("Out of memory loading speakers (malloc)");
                while(true);
            }
        } else {
            speaker_strings = (char*)realloc(speaker_strings, size+s.length()+1);
            if(speaker_strings==nullptr) {
                Serial.println("Out of memory loading speakers");
                while(true);
            }
        }
        strcpy(speaker_strings+size,s.c_str());
        size+=s.length()+1;
        s = file.readStringUntil(',');
        ++speaker_count;
    }
    file.close();
    file = SPIFFS.open("/api.txt");
    s = file.readStringUntil('\n');
    s.trim();
    strcpy(play_pause_url,s.c_str());
    s = file.readStringUntil('\n');
    s.trim();
    strcpy(next_track_url,s.c_str());
    file.close();
    file = SPIFFS.open("/wifi.txt");
    s = file.readStringUntil('\n');
    s.trim();
    strcpy(wifi_ssid,s.c_str());
    s = file.readStringUntil('\n');
    s.trim();
    strcpy(wifi_pass,s.c_str());
    file.close();
    if(SPIFFS.exists("/state")) {
        file = SPIFFS.open("/state","rb");
        file.read((uint8_t*)&speaker_index,sizeof(speaker_index));
        file.close();
        if(speaker_index>=speaker_count) {
            speaker_index = 0;
        }
    }
    Serial.printf("Connecting to %s...\n",wifi_ssid);
    WiFi.begin(wifi_ssid,wifi_pass);
    
    draw_room(speaker_index);
}
static uint32_t fade_ts=0;
void loop() {
    
    dsp_miser.update();
    button_1.update();
    button_2.update();
    if(dsp_miser.dimmed()) {
        if(fade_ts==0) {
            fade_ts = millis()+dsp_miser.timeout();
        }
        if(millis()>fade_ts) {
            file = SPIFFS.open("/state","wb",true);
            file.seek(0);
            file.write((uint8_t*)&speaker_index,sizeof(speaker_index));
            file.close();
            esp_sleep_enable_ext0_wakeup((gpio_num_t)button_1_t::pin,0);
            esp_deep_sleep_start();
        }
    } else {
        fade_ts = 0;
    }
}