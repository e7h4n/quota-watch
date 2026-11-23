#include "board.h"
#include "quota.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
extern "C" {
#include "okou_agent.h"
#include "okou_ble.h"
}
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
constexpr unsigned bg=0x101916, ink=0xf2f4e8, muted=0xbac8bc, track=0x31413b;
constexpr unsigned lime=0xc7e79b, blue=0xa5c7e1;
void text(const char *str,int x,int y,const lgfx::IFont *font,unsigned color,
          lgfx::textdatum_t align=lgfx::textdatum_t::middle_center) {
    auto &c=board_canvas();
    c.setFont(font); c.setTextSize(1); c.setTextColor(color); c.setTextDatum(align);
    c.drawString(str,x,y);
}
void ring(int radius,float percent,unsigned color) {
    auto &c=board_canvas();
    auto paint=[&](float sweep,unsigned rgb) {
        if(sweep<=0) return;
        c.fillArc(234,233,radius,radius-7,142,std::min(359.99f,142+sweep),rgb);
        if(142+sweep>=360) c.fillArc(234,233,radius,radius-7,0,142+sweep-360,rgb);
    };
    paint(256,track);
    paint(2.56f*percent,color);
}
int remaining(uint8_t pct,uint32_t reset,time_t now) {
    return pct>100 || now<1600000000 || (reset && now>=reset) ? -1 : pct;
}
void row(int y,const char *period,int pct,uint32_t reset,time_t now,unsigned color) {
    char percent[16]="--", left[32]="--";
    if(pct>=0) snprintf(percent,sizeof percent,"%d%%",pct);
    if(reset && now>=1600000000) {
        int minutes=std::max<int64_t>(0,(int64_t(reset)-now+59)/60);
        if(!minutes) strcpy(left,"Refresh");
        else if(minutes>=1440) snprintf(left,sizeof left,"%dd %dh",minutes/1440,minutes%1440/60);
        else if(minutes>=60) snprintf(left,sizeof left,"%dh %dm",minutes/60,minutes%60);
        else snprintf(left,sizeof left,"%dm",minutes);
    }
    text(period,140,y,&fonts::Font2,color,lgfx::textdatum_t::middle_left);
    text(percent,177,y,&fonts::Font2,color,lgfx::textdatum_t::middle_left);
    text(left,328,y,&fonts::Font4,ink,lgfx::textdatum_t::middle_right);
}
void face(const quota_card &card,int short_pct,int week_pct,float inner,float outer,int dx,time_t now) {
    ring(216,outer,blue); ring(189,inner,lime);
    auto &c=board_canvas();
    c.setFont(&fonts::Font2);
    char email[sizeof card.email]; memcpy(email,card.email,sizeof email);
    size_t n=strlen(email);
    if(c.textWidth(email)>290) {
        while(n>3 && c.textWidth(email)>265) email[--n]=0;
        while(n && ((unsigned char)card.email[n]&0xc0)==0x80) email[--n]=0;
        if(n+4<=sizeof email) strcat(email,"...");
    }
    text(email,234+dx,139,&fonts::Font2,muted);
    text(card.provider?"Claude":"ChatGPT",234+dx,195,&fonts::FreeSansBold24pt7b,ink);
    text(card.tier,234+dx,245,&fonts::Font4,muted);
    if(now<1600000000) text("Waiting for clock",234,274,&fonts::Font2,muted);
    else if(now-time_t(card.captured)>=3600) {
        char age[40];
        snprintf(age,sizeof age,"Synced %lldh ago",(long long)((now-card.captured)/3600));
        text(age,234,274,&fonts::Font2,muted);
    }
    if(!card.provider && card.credit_cents>0) {
        char credits[40];
        snprintf(credits,sizeof credits,"%ld.%02ld credits",long(card.credit_cents/100),long(card.credit_cents%100));
        text(credits,234,304,&fonts::Font2,ink);
    }
    row(354,"5h",short_pct,card.short_reset,now,lime);
    row(391,"7d",week_pct,card.week_reset,now,blue);
}
}
extern "C" void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    accounts_init(); board_init(); launcher_start();
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    while(okou_ble_host_owned()) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    bluetooth_start();
    const int64_t boot=esp_timer_get_time();
    unsigned index=0, generation=0;
    bool validated=false, dirty=true, touching=false, next_down=false, prev_down=false;
    int start_x=0,last_x=0, prior_code=-2, direction=0;
    int64_t animation=boot;
    float from_short=0,from_week=0,inner=0,outer=0;
    time_t last_time=-1;
    for(;;) {
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        int64_t tick=esp_timer_get_time();
        int code=bluetooth_code();
        BoardInput input=board_input();
        int step=0;
        if(code<0) {
            if(input.next && !next_down) step=1;
            if(input.select && !prev_down) step=-1;
            if(input.touch_valid) {
                if(input.touch) {
                    if(!touching) start_x=input.x;
                    last_x=input.x;
                } else if(touching && std::abs(last_x-start_x)>42) step=last_x<start_x?1:-1;
                touching=input.touch;
            }
        } else touching=false;
        next_down=input.next; prev_down=input.select;
        unsigned gen=accounts_generation();
        quota_card card={};
        unsigned count=accounts_read(index,&card);
        if(index>=count && index) { index=0; accounts_read(index,&card); }
        if((step && count>1) || gen!=generation) {
            if(step && count>1) {
                index=(int(index)+step+count)%count;
                accounts_read(index,&card);
            }
            generation=gen; direction=step;
            animation=tick; from_short=inner; from_week=outer; dirty=true;
        }
        time_t now=time(nullptr);
        const int64_t duration=direction?320000:600000;
        float t=std::min(1.f,float(tick-animation)/duration);
        if(dirty || prior_code!=code || (code<0 && (last_time/60!=now/60 || t<1 || animation))) {
            dirty=false; prior_code=code; last_time=now;
            auto &c=board_canvas(); c.fillScreen(bg);
            if(code>=0) {
                char digits[12]; snprintf(digits,sizeof digits,"%03d %03d",code/1000,code%1000);
                text("Bluetooth pairing",234,160,&fonts::Font4,muted);
                text(digits,234,229,&fonts::FreeSansBold24pt7b,lime);
                text("Enter this code on your computer",234,293,&fonts::Font2,ink);
            } else if(!count) {
                text("Quota Watch",234,176,&fonts::FreeSansBold24pt7b,ink);
                text("Add an account via Bluetooth",234,254,&fonts::Font4,muted);
                text("e7h4n.github.io/quota-watch",234,292,&fonts::Font2,lime);
            } else {
                int h=remaining(card.short_pct,card.short_reset,now),w=remaining(card.week_pct,card.week_reset,now);
                float rest=(1-t)*(1-t)*(1-t);
                inner=std::max(0,h)*(1-rest)+from_short*rest;
                float outer_t=direction?t:std::clamp((t-.08f)/.92f,0.f,1.f);
                float outer_rest=(1-outer_t)*(1-outer_t)*(1-outer_t);
                outer=std::max(0,w)*(1-outer_rest)+from_week*outer_rest;
                face(card,h,w,inner,outer,int(30*direction*rest),now);
            }
            board_present();
            if(t>=1) animation=0;
        }
        if(!validated && tick-boot>=10000000 && bluetooth_ready()) {
            ESP_ERROR_CHECK(okou_app_mark_valid()); validated=true;
        }
        vTaskDelay(pdMS_TO_TICKS(28));
    }
}
