#include "thais.h"
static thais_fb_t *splash_fb;
static int last_percent=-1;
static int box_w, box_h, box_x, box_y;
void splash_show(thais_fb_t *fb){
    splash_fb=fb;
    last_percent=-1;
    fb_clear(fb_color(0x0a,0x0a,0x12));
    box_w = (int)(fb->width * 3 / 5);
    if(box_w < 400) box_w=400;
    if(box_w > 800) box_w=800;
    box_h=180;
    if((uint64_t)box_h+60 > fb->height) box_h=(int)fb->height-60;
    box_x=(int)(fb->width/2 - box_w/2);
    box_y=(int)(fb->height/2 - box_h/2);
    fb_draw_rect(box_x-2,box_y-2,box_w+4,box_h+4, fb_color(0x7a,0x5a,0xff));
    fb_draw_rect(box_x,box_y,box_w,box_h, fb_color(0x14,0x14,0x1e));
    const char *t1="Siath OS v1.0.0 alpha";
    int t1_w=(int)strlen(t1)*8*2;
    int t1_x=(int)(fb->width/2 - t1_w/2);
    fb_draw_text_scaled(t1_x, box_y+18, t1, fb_color(0xff,0xff,0xff), fb_color(0x14,0x14,0x1e), 2);
    const char *t2="O Siath OS esta iniciando... Aguarde";
    int t2_x=(int)(fb->width/2 - strlen(t2)*8/2);
    if(t2_x < box_x+8) t2_x=box_x+8;
    fb_draw_text(t2_x, box_y+58, t2, fb_color(0xcc,0xcc,0xdd), fb_color(0x14,0x14,0x1e));
    const char *t3="Made with love by Thais (op3n/op3ny)";
    int t3_x=(int)(fb->width/2 - strlen(t3)*8/2);
    if(t3_x < box_x+8) t3_x=box_x+8;
    fb_draw_text(t3_x, box_y+box_h-30, t3, fb_color(0xff,0x8a,0xc8), fb_color(0x14,0x14,0x1e));
    uint64_t r=rand64()%PHRASE_COUNT;
    const char *phrase=boot_phrases[r];
    int max_chars=(box_w-16)/8;
    if(max_chars>80) max_chars=80;
    if(max_chars<20) max_chars=20;
    char buf[96]; int j=0;
    for(int i=0; phrase[i] && j<max_chars; i++){ unsigned char c=(unsigned char)phrase[i]; if(c<32||c>126) continue; buf[j++]=c; }
    buf[j]=0;
    int ph_x=(int)(fb->width/2 - j*8/2);
    if(ph_x < box_x+8) ph_x=box_x+8;
    fb_draw_text(ph_x, box_y+box_h-50, buf, fb_color(0x9a,0x9a,0xb0), fb_color(0x14,0x14,0x1e));
}
void splash_set_spinner(int frame){
    if(!splash_fb || last_percent>=50) return;
    const char sp[4]={'|','/','-','\\'};
    char s[2]={sp[frame%4],0};
    int x=box_x+box_w-24;
    int y=box_y+58;
    fb_draw_char(x,y,s[0], fb_color(0x7a,0x5a,0xff), fb_color(0x14,0x14,0x1e));
}
void splash_set_progress(int percent){
    if(!splash_fb) return;
    last_percent=percent;
    int bar_w=box_w-40;
    if(bar_w<200) bar_w=200;
    int bar_h=16;
    int cx=(int)(splash_fb->width/2 - bar_w/2);
    int cy=box_y+box_h/2+10;
    if(percent<50){
        fb_draw_rect(cx,cy,bar_w,bar_h, fb_color(0x2a,0x2a,0x3a));
        fb_draw_rect(cx,cy,bar_w,2, fb_color(0x4a,0x4a,0x6a));
        fb_draw_rect(cx,cy+bar_h-2,bar_w,2, fb_color(0x4a,0x4a,0x6a));
    } else {
        int fill=(bar_w-4)*(percent-50)/50;
        fb_draw_rect(cx,cy,bar_w,bar_h, fb_color(0x2a,0x2a,0x3a));
        fb_draw_rect(cx+2,cy+2,fill,bar_h-4, fb_color(0x7a,0x5a,0xff));
        char buf[16]; snprintf(buf,16,"%d%%",percent);
        fb_draw_text(cx+bar_w+10, cy, buf, fb_color(0xff,0xff,0xff), fb_color(0x0a,0x0a,0x12));
    }
    if(percent>=50){
        char bar[32]; int filled=percent/5; for(int i=0;i<20;i++) bar[i]=i<filled?'=':' '; bar[20]=0;
        char line[64]; snprintf(line,64,"[%s>]",bar);
        int lx=(int)(splash_fb->width/2 - strlen(line)*8/2);
        fb_draw_text(lx, cy+22, line, fb_color(0x7a,0x5a,0xff), fb_color(0x14,0x14,0x1e));
    }
}
