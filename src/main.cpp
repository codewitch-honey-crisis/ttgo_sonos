#include <Arduino.h>
#include <ttgo.hpp>
#include <config.h>
#include <tft_io.hpp>
#include <fonts/SonosFont.hpp>
#include <logo.hpp>
#include <SPIFFS.h>
#include <WiFi.h>
#include <HTTPClient.h>

// background color for the display (24 bit, followed by display's native pixel type)
constexpr static const rgb_pixel<24> bg_color_24(/*R*/12,/*G*/12,/*B*/12);
constexpr static const lcd_t::pixel_type bg_color = convert<rgb_pixel<24>,lcd_t::pixel_type>(bg_color_24);

// function prototypes
static void ensure_connected();
static void draw_room(int index);
static const char* room_for_index(int index);
static const char* string_for_index(const char* strings,int index);
static void do_request(int index,const char* url_fmt);

// font
static const open_font& speaker_font = SonosFont;
static const uint16_t speaker_font_height = 35;
// global state
static HTTPClient http;
// current speaker/room
static int speaker_index = 0;
// number of speakers/rooms
static int speaker_count = 0;
// series of concatted null 
// termed strings for speakers/rooms
static char* speaker_strings = nullptr;
// how many urls are in api txt
static int format_url_count = 0;
// the format string urls
static char* format_urls = nullptr;
// temp for formatting urls
static char url[1024];
static char url_encoded[1024];
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
using frame_buffer_t = bitmap<typename lcd_t::pixel_type>;
// reversed due to LCD orientation:
constexpr static const size16 frame_buffer_size({lcd_t::base_height,speaker_font_height});
static uint8_t frame_buffer_data[frame_buffer_t::sizeof_buffer(frame_buffer_size)];
static frame_buffer_t frame_buffer(frame_buffer_size,frame_buffer_data);

static void button_a_on_click(int clicks,void* state) {
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
static void button_b_on_click(int clicks,void* state) {
    if(clicks<format_url_count) {
        const char* fmt_url = string_for_index(format_urls, clicks);
        if(fmt_url!=nullptr) {
            do_request(speaker_index, fmt_url);
        }
    }
    // reset the dimmer
    dimmer.wake();
}
static void button_b_on_long_click(void* state) {
    // play the first URL
    if(format_urls!=nullptr) {
        do_request(speaker_index,format_urls);
    }
    // reset the dimmer
    dimmer.wake();
}
static void url_encode(const char *str, char *enc){

    for (; *str; str++){
        int i = *str;
        if(isalnum(i)|| i == '~' || i == '-' || i == '.' || i == '_') {
            *enc=*str;
        } else {
            sprintf( enc, "%%%02X", *str);
        }
        while (*++enc);
    }
}
static void do_request(int index, const char* url_fmt) {
    const char* room = string_for_index(speaker_strings, index);
    url_encode(room,url_encoded);
    snprintf(url,1024,url_fmt,url_encoded);
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
        Serial.printf("Connecting to %s...\n",wifi_ssid);
        WiFi.begin(wifi_ssid,wifi_pass);
        while(WiFi.status()!=WL_CONNECTED) {
            delay(10);
        }
        Serial.println("Connected.");
    }
}
static void draw_center_text(const char* text) {
    // set up the font
    open_text_info oti;
    oti.font = &speaker_font;
    oti.text = text;
    // 35 pixel high font
    oti.scale = oti.font->scale(speaker_font_height);
    // center the text
    ssize16 text_size = oti.font->measure_text(
        ssize16::max(),
        spoint16::zero(),
        oti.text,
        oti.scale);
    srect16 text_rect = text_size.bounds();
    text_rect.center_horizontal_inplace((srect16)frame_buffer.bounds());
    draw::text(frame_buffer,text_rect,oti,color_t::white,bg_color);

}
static const char* string_for_index(const char* strings,int index) {
    if(strings==nullptr) {
        return nullptr;
    }
    // move through the string list 
    // a string at a time until the
    // index is hit, and return
    // the pointer when it is
    const char* sz = strings;
    for(int i = 0;i<index;++i) {
        sz = sz+strlen(sz)+1;
    }
    return sz;
}

static void draw_room(int index) {
    draw::wait_all_async(lcd);
    // clear the frame buffer
    frame_buffer.fill(frame_buffer.bounds(), bg_color);
    // get the room string
    const char* sz = string_for_index(speaker_strings, index);
    // and draw it. Note we are only drawing the text region
    draw_center_text(sz);
    srect16 bmp_rect(0,0,frame_buffer.dimensions().width-1,speaker_font_height-1);
    bmp_rect.center_vertical_inplace((srect16)lcd.bounds());
    bmp_rect.offset_inplace(0,23);
    draw::bitmap_async(lcd,bmp_rect,frame_buffer,frame_buffer.bounds());
}
void setup() {
    char *sz = (char*)malloc(0);
    sz = strchr("",1);
    // start everything up
    Serial.begin(115200);
    ttgo_initialize();
    SPIFFS.begin();
    // set the button callbacks
    button_b.on_click(button_b_on_click);
    button_b.on_long_click(button_b_on_long_click);
    button_a.on_click(button_a_on_click);
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
    size = 0;
    file = SPIFFS.open("/api.txt");
    s=file.readStringUntil('\n');
    s.trim();
    while(!s.isEmpty()) {
        if(format_urls==nullptr) {
            format_urls = (char*)malloc(s.length()+1);
            if(format_urls==nullptr) {
                Serial.println("Out of memory loading API urls (malloc)");
                while(true);
            }
        } else {
            format_urls = (char*)realloc(
                format_urls, 
                size+s.length()+1);
            if(format_urls==nullptr) {
                Serial.println("Out of memory loading API urls");
                while(true);
            }
        }
        ++format_url_count;
        strcpy(format_urls+size,s.c_str());
        size+=s.length()+1;
        s = file.readStringUntil('\n');
        s.trim();
    }
    file.close();
    // parse wifi.txt
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
    // draw logo to screen
    draw::image(lcd,lcd.bounds(),&logo);
    // clear the remainder
    // split the remaining rect by the 
    // rect of the text area, and fill those
    rect16 scrr = lcd.bounds().offset(0,47).crop(lcd.bounds());
    rect16 tr(scrr.x1,0,scrr.x2,speaker_font_height-1);
    tr.center_vertical_inplace(lcd.bounds());
    tr.offset_inplace(0,23);
    rect16 outr[4];
    size_t rc = scrr.split(tr,4,outr);
    // we're only drawing part of the screen
    // we don't draw later
    for(int i = 0;i<rc;++i) {
        draw::filled_rectangle(lcd,outr[i],bg_color);
    }
    
    // initial draw
    draw_room(speaker_index);
}

void loop() {
    // pump all our objects
    dimmer.update();
    button_a.update();
    button_b.update();

    // if we're faded all the way, sleep
    if(dimmer.faded()) {
        // write the state
        file = SPIFFS.open("/state","wb",true);
        file.seek(0);
        file.write((uint8_t*)&speaker_index,sizeof(speaker_index));
        file.close();
        lcd.sleep();
        // make sure we can wake up on button_a
        esp_sleep_enable_ext0_wakeup((gpio_num_t)button_a_t::pin,0);
        // go to sleep
        esp_deep_sleep_start();
        
    } 
}