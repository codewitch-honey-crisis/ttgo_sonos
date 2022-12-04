#include <Arduino.h>

#include <gfx.hpp>
#include <htcw_button.hpp>
#include <st7789.hpp>
#include <tft_io.hpp>
#include <fonts/Ubuntu.hpp>
#include <SPIFFS.h>
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
using display_t = st7789<LCD_WIDTH, LCD_HEIGHT, PIN_NUM_DC, PIN_NUM_RST, PIN_NUM_BCKL, bus_t, 1, true, 400, 200>;
using color_t = color<typename display_t::pixel_type>;

using button_1_t = button<35, 10, true>;
using button_2_t = button<0, 10, true>;

static void draw_room(int index);

static display_t dsp;

static button_1_t button_1;
static button_2_t button_2;

static int speaker_index = 0;
static int speaker_count = 0;
static char* speaker_strings = nullptr;
static void button_1_cb(bool pressed, void* state) {
    if(pressed) {
        ++speaker_index;
        if(speaker_index>=speaker_count) {
            speaker_index = 0;
        }
        draw_room(speaker_index);
    }
}
static void button_2_cb(bool pressed, void* state) {
    Serial.printf("Button 2 %s\n",pressed?"pressed":"released");
}
static void draw_center_text(const char* text) {
    open_text_info oti;
    oti.font = &Ubuntu;
    oti.text = text;
    oti.transparent_background = false;
    // 25 pixel high font
    oti.scale = oti.font->scale(35);
    // center the text
    ssize16 text_size = oti.font->measure_text(ssize16::max(),spoint16::zero(),oti.text,oti.scale);
    srect16 text_rect = text_size.bounds();
    text_rect.center_inplace((srect16)dsp.bounds());
    draw::text(dsp,text_rect,oti,color_t::white);

}
static void draw_room(int index) {
    draw::filled_rectangle(dsp, dsp.bounds(), color_t::black);
    const char* sz = speaker_strings;
    for(int i = 0;i<index;++i) {
        sz = sz+strlen(sz)+1;
    }
    draw_center_text(sz);
}
void setup() {
    Serial.begin(115200);
    SPIFFS.begin();
    button_1.initialize();
    button_2.initialize();
    button_1.callback(button_1_cb);
    button_2.callback(button_2_cb);
    File file = SPIFFS.open("/speakers.csv");
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
    draw_room(speaker_index);
}
void loop() {
    

    button_1.update();
    button_2.update();
}