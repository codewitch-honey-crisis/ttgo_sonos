#include <Arduino.h>
#include <config.h>
#include <gfx.hpp>
#include <htcw_button.hpp>
#include <st7789.hpp>
#include <tft_io.hpp>
#include <lcd_miser.hpp>
#include <fonts/SonosFont.hpp>
#include <logo.hpp>
#include <SPIFFS.h>
#include <WiFi.h>
#include <HTTPClient.h>
using namespace arduino;
using namespace gfx;

// configure the display
using bus_t = tft_spi_ex<LCD_HOST, 
                        PIN_NUM_CS, 
                        PIN_NUM_MOSI, 
                        PIN_NUM_MISO, 
                        PIN_NUM_CLK, 
                        SPI_MODE0,
                        true,
                        LCD_WIDTH*LCD_HEIGHT*2+8,2>;

using display_t = st7789<LCD_WIDTH,
                        LCD_HEIGHT, 
                        PIN_NUM_DC, 
                        PIN_NUM_RST, 
                        -1 /* PIN_NUM_BCKL */, 
                        bus_t, 
                        1, 
                        true, 
                        400, 
                        200>;
using color_t = color<typename display_t::pixel_type>;

// background color for the display (24 bit, followed by display's native pixel type)
constexpr static const rgb_pixel<24> bg_color_24(/*R*/12,/*G*/12,/*B*/12);
constexpr static const display_t::pixel_type bg_color = convert<rgb_pixel<24>,display_t::pixel_type>(bg_color_24);

static display_t dsp;

// configure the buttons
using button_1_t = button_ex<PIN_BUTTON_1,
                        10, 
                        true>;
using button_2_t = button_ex<PIN_BUTTON_2,
                        10, 
                        true>;

static button_1_t button_1;
static button_2_t button_2;

// configure the backlight manager
static lcd_miser<PIN_NUM_BCKL> dimmer;

// function prototypes
static void ensure_connected();
static void draw_room(int index);
static const char* room_for_index(int index);
static void do_request(int index,const char* url_fmt);
// global state
static HTTPClient http;
// current speaker/room
static int speaker_index = 0;
// number of speakers/rooms
static int speaker_count = 0;
// series of concatted null 
// termed strings for speakers/rooms
static char* speaker_strings = nullptr;
// the format string url for play/pause
static char play_pause_url[1024];
// the format string url for the next track
static char next_track_url[1024];
// the format string url for the previous track
static char prev_track_url[1024];
// temp for formatting urls
static char url[1024];
// the Wifi SSID
static char wifi_ssid[256];
// the Wifi password
static char wifi_pass[256];
// temp for using a file
static File file;
// begin fade timestamp
static uint32_t fade_ts=0;

// rather than draw directly to the display, we draw
// to a bitmap, and then draw that to the display
// for less flicker. Here we create the bitmap
using frame_buffer_t = bitmap<typename display_t::pixel_type>;
// reversed due to LCD orientation:
constexpr static const size16 frame_buffer_size({LCD_HEIGHT,LCD_WIDTH-47});
static uint8_t frame_buffer_data[frame_buffer_t::sizeof_buffer(frame_buffer_size)];
static frame_buffer_t frame_buffer(frame_buffer_size,frame_buffer_data);

static void button_1_on_click(int clicks,void* state) {
    // if we're dimming/dimmed we don't want 
    // to actually increment
    if(!dimmer.dimmed()) {
        // move to the next speaker
        speaker_index+=clicks;
        while(speaker_index>=speaker_count) {
            // wrap around
            speaker_index -= speaker_count;
        }
        // redraw
        draw_room(speaker_index);
    }
    // reset the dimmer
    dimmer.wake();
}
static void button_2_on_click(int clicks,void* state) {
    do_request(speaker_index,clicks==1?play_pause_url:prev_track_url);
    // reset the dimmer
    dimmer.wake();
}
static void button_2_on_long_click(void* state) {
    do_request(speaker_index,next_track_url);
    // reset the dimmer
    dimmer.wake();
}
static void do_request(int speaker_index, const char* url_fmt) {
    const char* room = room_for_index(speaker_index);
    snprintf(url,1024,url_fmt,room);
    // connect if necessary
    ensure_connected();
    // send the command
    Serial.print("Sending ");
    Serial.println(url);
    http.begin(url);
    http.GET();
    http.end();
}

static void ensure_connected() {
    // if not connected, reconnect
    if(WiFi.status()!=WL_CONNECTED) {
        WiFi.begin(wifi_ssid,wifi_pass);
    }
}
static void draw_center_text(const char* text) {
    // set up the font
    open_text_info oti;
    oti.font = &SonosFont;
    oti.text = text;
    // 35 pixel high font
    oti.scale = oti.font->scale(35);
    // center the text
    ssize16 text_size = oti.font->measure_text(
        ssize16::max(),
        spoint16::zero(),
        oti.text,
        oti.scale);
    srect16 text_rect = text_size.bounds();
    text_rect.center_inplace((srect16)frame_buffer.bounds());
    draw::text(frame_buffer,text_rect,oti,color_t::white,bg_color);

}
static const char* room_for_index(int index) {
    // move through the string list 
    // a string at a time until the
    // index is hit, and return
    // the pointer when it is
    const char* sz = speaker_strings;
    for(int i = 0;i<index;++i) {
        sz = sz+strlen(sz)+1;
    }
    return sz;
}

static void draw_room(int index) {
    draw::wait_all_async(dsp);
    // clear the frame buffer
    draw::filled_rectangle(frame_buffer, frame_buffer.bounds(), bg_color);
    // get the room string
    const char* sz = room_for_index(index);
    // and draw it. Note we offset it by the jpg height
    // since we're only drawing the lower portion of
    // the screen
    draw_center_text(sz);
    draw::bitmap_async(dsp,dsp.bounds().offset(0,47).crop(dsp.bounds()),frame_buffer,frame_buffer.bounds());
}
void setup() {
    // start everything up
    Serial.begin(115200);
    SPIFFS.begin();
    dimmer.initialize();
    button_1.initialize();
    button_2.initialize();
    // set the button callbacks
    button_1.on_click(button_1_on_click);
    button_2.on_click(button_2_on_click);
    button_2.on_long_click(button_2_on_long_click);
    // parse speakers.csv into speaker_strings
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
            speaker_strings = (char*)realloc(
                speaker_strings, 
                size+s.length()+1);
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
    // parse api.txt into our url format strings
    file = SPIFFS.open("/api.txt");
    s = file.readStringUntil('\n');
    s.trim();
    strcpy(play_pause_url,s.c_str());
    s = file.readStringUntil('\n');
    s.trim();
    strcpy(next_track_url,s.c_str());
    s = file.readStringUntil('\n');
    s.trim();
    strcpy(prev_track_url,s.c_str());
    file.close();
    file = SPIFFS.open("/wifi.txt");
    s = file.readStringUntil('\n');
    s.trim();
    strcpy(wifi_ssid,s.c_str());
    s = file.readStringUntil('\n');
    s.trim();
    strcpy(wifi_pass,s.c_str());
    file.close();
    // when we sleep we store the last room
    // so we can boot with it. it's written
    // to a /state file so we see if it exists
    // and if so, set the speaker_index to the
    // contents
    if(SPIFFS.exists("/state")) {
        file = SPIFFS.open("/state","rb");
        file.read(
            (uint8_t*)&speaker_index,
            sizeof(speaker_index));
        file.close();
        // in case /state is stale relative to speakers.csv:
        if(speaker_index>=speaker_count) {
            speaker_index = 0;
        }
    }
    // initial connect
    Serial.printf("Connecting to %s...\n",wifi_ssid);
    WiFi.begin(wifi_ssid,wifi_pass);
    Serial.println("Connected.");
    // draw logo to screen
    draw::image(dsp,dsp.bounds(),&logo);
    
    // initial draw
    draw_room(speaker_index);
}

void loop() {
    // pump all our objects
    dimmer.update();
    button_1.update();
    button_2.update();

    // if we're faded all the way, sleep
    if(dimmer.faded()) {
        // write the state
        file = SPIFFS.open("/state","wb",true);
        file.seek(0);
        file.write((uint8_t*)&speaker_index,sizeof(speaker_index));
        file.close();
        dsp.sleep();
        // make sure we can wake up on button_1
        esp_sleep_enable_ext0_wakeup((gpio_num_t)button_1_t::pin,0);
        // go to sleep
        esp_deep_sleep_start();
        
    } 
}