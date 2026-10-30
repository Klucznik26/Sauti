/* Sayari Sauti: Linux window integration only (SDL2).
 * The portable ASM core owns audio window history, FFT, dB and all pixels.
 * Optional PipeWire implementation lives in a separate Linux-only adapter.
 */
#include "sauti.h"
#include "sauti_playlist.h"
#include "sauti_transport.h"
#include "sauti_app_state.h"
#include "sauti_library.h"
#include "sauti_record_ring.h"
#include "sauti_wav.h"
#include "sauti_playback_time.h"
#include "sauti_playback_backend.h"
#include "sauti_mp3_sdl_player.h"
#include <unistd.h>
#include <sys/wait.h>
#include <SDL.h>
#include <SDL_mixer.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#ifdef SAUTI_HAS_PIPEWIRE
#include "pipewire_capture.h"
#endif

enum SautiSource { SAUTI_GENERATOR, SAUTI_MIC, SAUTI_MONITOR };
typedef struct {
    int frames;
    enum SautiSource source;
    const char *target;
    int accelerated;
    int stereo_bars;
} Options;

/* Decorative frames only. Audio pixels remain owned by NASM.
 * Geometry deliberately mirrors the centered, half-scale renderer. */
/* One-pixel rounded outline, rendered using SDL primitives with no SDL_gfx.
 * Keep border drawing separate from the NASM audio/LED rendering path. */
/* Theme affects main-window decorations only, never audio samples or settings UI. */
static SDL_Renderer *sauti_theme_renderer=NULL;
static int sauti_theme_bg=5,sauti_theme_frames=5;
static const SDL_Color sauti_frame_colors[8]={
    {215,215,218,255},{157,190,155,255},{133,176,209,255},
    {230,143,62,255},{88,177,98,255},{100,88,126,255},
    {149,54,82,255},{65,111,186,255}
};
static int sauti_is_light_bg(int choice){return choice>=0&&choice<=4;}
static void draw_rounded_frame(SDL_Renderer *renderer, SDL_Rect rect, int radius) {
    if(renderer==sauti_theme_renderer && sauti_theme_frames!=5){
        SDL_Color c=sauti_frame_colors[sauti_theme_frames];
        SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,255);
    }
    if (rect.w < 2 || rect.h < 2) return;
    if (radius < 1) {
        SDL_RenderDrawRect(renderer, &rect);
        return;
    }
    if (radius > (rect.w - 1) / 2) radius = (rect.w - 1) / 2;
    if (radius > (rect.h - 1) / 2) radius = (rect.h - 1) / 2;
    const int x0 = rect.x, x1 = rect.x + rect.w - 1;
    const int y0 = rect.y, y1 = rect.y + rect.h - 1;
    SDL_RenderDrawLine(renderer, x0 + radius, y0, x1 - radius, y0);
    SDL_RenderDrawLine(renderer, x0 + radius, y1, x1 - radius, y1);
    SDL_RenderDrawLine(renderer, x0, y0 + radius, x0, y1 - radius);
    SDL_RenderDrawLine(renderer, x1, y0 + radius, x1, y1 - radius);
    /* Circular corner rasterization; use integer arithmetic for predictability. */
    for (int dy = 0; dy <= radius; ++dy) {
        int dx = radius;
        while (dx > 0 && dx * dx + dy * dy > radius * radius) --dx;
        SDL_RenderDrawPoint(renderer, x0 + radius - dx, y0 + radius - dy);
        SDL_RenderDrawPoint(renderer, x1 - radius + dx, y0 + radius - dy);
        SDL_RenderDrawPoint(renderer, x0 + radius - dx, y1 - radius + dy);
        SDL_RenderDrawPoint(renderer, x1 - radius + dx, y1 - radius + dy);
    }
}

static void paint_stereo_frames(SDL_Renderer *renderer, int w, int h) {
    if (w < 256 || h < 180) return;
    int panel_w = w / 4 - 36;
    int original_panel_h = (h - 54) / 2;
    int panel_h = original_panel_h * 85 / 100;
    int rise = h / 4 - 24;
    if (rise < 0) rise = 0;
    if (rise > 90) rise = 90;
    int top = (h - original_panel_h) / 2 - 19 - rise;
    SDL_Rect left = { w / 2 - panel_w - 40, top, panel_w + 28, panel_h + 38 };
    SDL_Rect right = { w / 2 + 12, top, panel_w + 28, panel_h + 38 };
    SDL_Rect outer = { left.x - 19, top - 21,
                       right.x + right.w - left.x + 38, panel_h + 80 };

    SDL_SetRenderDrawColor(renderer, 100, 88, 126, 255);
    draw_rounded_frame(renderer, left, 8);
    draw_rounded_frame(renderer, right, 8);
    SDL_SetRenderDrawColor(renderer, 56, 50, 75, 255);
    SDL_Rect inner_l = { left.x+3, left.y+3, left.w-6, left.h-6 };
    SDL_Rect inner_r = { right.x+3, right.y+3, right.w-6, right.h-6 };
    draw_rounded_frame(renderer, inner_l, 6);
    draw_rounded_frame(renderer, inner_r, 6);
    SDL_SetRenderDrawColor(renderer, 128, 111, 150, 255);
    draw_rounded_frame(renderer, outer, 12);
    SDL_SetRenderDrawColor(renderer, 51, 46, 67, 255);
    SDL_Rect outer_inner = { outer.x+3, outer.y+3, outer.w-6, outer.h-6 };
    draw_rounded_frame(renderer, outer_inner, 10);


}


/* Twin sliders and passive meters: window-only SDL controls. */
static int clamp100(int x) {return x<0?0:x>100?100:x;}
static void panel_geometry(int w,int h,SDL_Rect *outer,SDL_Rect *empty,
                           SDL_Rect *sliders,SDL_Rect *lm,SDL_Rect *rm) {
    int pw=w/4-36, oldh=(h-54)/2, ph=oldh*85/100;
    int rise=h/4-24;
    if(rise<0)rise=0;
    if(rise>90)rise=90;
    int top=(h-oldh)/2-19-rise;
    /* Match paint_stereo_frames(): right channel ends at w/2+12+pw+28.
     * Previous +137 made lower/control frames 17 px too wide. */
    *outer=(SDL_Rect){w/2-pw-59,top-21,2*pw+120,ph+80};
    /* The EQ frame is fixed. Reserve a 34px title strip below it,
     * then shift only the controls and the bottom potentiometers. */
    int title_y=outer->y+outer->h+6;
    int ey=title_y+34+6;
    /* Fit title, two rows of buttons, progress and volume controls at
     * 1024x640 without moving the EQ frame or its audio pixels. */
    int sh=54;
    int sy=h-8-sh;
    int progress_y=sy-6-32;
    int eh=progress_y-6-ey;
    if(eh>108)eh=108;
    if(eh<0)eh=0;
    *empty=(SDL_Rect){outer->x,ey,outer->w,eh};
    *sliders=(SDL_Rect){outer->x,sy,outer->w,sh};
    *lm=(SDL_Rect){outer->x-34,top+12,17,ph-24};
    *rm=(SDL_Rect){outer->x+outer->w+17,top+12,17,ph-24};
}
static void small_label(SDL_Renderer *r,int x,int y,const char *text) {
    /* 5x7 all-caps pixel lettering. */
    static const char *alphabet="ASEIMTUY0123456789%";
    static const unsigned char map[][7]={
        {14,17,17,31,17,17,17},{14,17,16,14,1,17,14},
        {31,16,16,30,16,16,31},{14,4,4,4,4,4,14},
        {17,27,21,21,17,17,17},{31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14},{17,17,10,4,4,4,4},
        {14,19,21,21,25,17,14},{4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
        {25,25,2,4,8,19,19}
    };
    if(r==sauti_theme_renderer && sauti_is_light_bg(sauti_theme_bg))
        SDL_SetRenderDrawColor(r,38,43,53,255);
    else SDL_SetRenderDrawColor(r,188,178,205,255);
    for(;*text;++text,x+=6){
        const char *q=strchr(alphabet,*text);
        if(!q)continue;
        const unsigned char *bits=map[q-alphabet];
        for(int j=0;j<7;++j)
            for(int i=0;i<5;++i)
                if(bits[j]&(16>>i))SDL_RenderDrawPoint(r,x+i,y+j);
    }
}
/* A compact icon-and-label control deck. Purely visual until playback,
 * recording and playlist modules exist; no misleading clickable actions. */
static void button_label(SDL_Renderer *r,int x,int y,const char *text) {
    static const unsigned char glyphs[26][7]={
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14},{17,17,10,10,4,4,4},
        {17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    int len=(int)strlen(text);
    x-=len*3-1;
    if(r==sauti_theme_renderer && sauti_is_light_bg(sauti_theme_bg))
        SDL_SetRenderDrawColor(r,28,32,40,255);
    else SDL_SetRenderDrawColor(r,199,190,218,255);
    for(;*text;++text,x+=6){
        if(*text<'A'||*text>'Z')continue;
        const unsigned char *g=glyphs[*text-'A'];
        for(int j=0;j<7;++j)
            for(int i=0;i<5;++i)
                if(g[j]&(16>>i))SDL_RenderDrawPoint(r,x+i,y+j);
    }
}
static void button_icon(SDL_Renderer *r,int cx,int cy,int type){
    /* Symbols stay legible at the default 1024x640 window size. */
    switch(type){
        case 0: /* source */
            SDL_RenderDrawLine(r,cx-3,cy-4,cx+3,cy-4);
            SDL_RenderDrawLine(r,cx-3,cy-4,cx-3,cy+2);
            SDL_RenderDrawLine(r,cx+3,cy-4,cx+3,cy+2);
            SDL_RenderDrawLine(r,cx-3,cy+2,cx+3,cy+2);
            SDL_RenderDrawLine(r,cx,cy+3,cx,cy+6);
            break;
        case 1: case 4: /* previous / next */
            for(int i=0;i<5;++i){
                int xx=type==1?cx-4+i:cx+4-i;
                SDL_RenderDrawLine(r,xx,cy-i,xx,cy+i);
            }
            SDL_RenderDrawLine(r,type==1?cx-5:cx+5,cy-5,
                               type==1?cx-5:cx+5,cy+5);
            break;
        case 2: /* play */
            for(int i=0;i<6;++i)
                SDL_RenderDrawLine(r,cx-3+i,cy-5+i/2,cx-3+i,cy+5-i/2);
            break;
        case 3: /* pause */
            SDL_RenderDrawLine(r,cx-4,cy-5,cx-4,cy+5);
            SDL_RenderDrawLine(r,cx-3,cy-5,cx-3,cy+5);
            SDL_RenderDrawLine(r,cx+3,cy-5,cx+3,cy+5);
            SDL_RenderDrawLine(r,cx+4,cy-5,cx+4,cy+5);
            break;
        case 5: /* rec */
            {SDL_Rect dot={cx-4,cy-4,8,8};SDL_RenderFillRect(r,&dot);}
            break;
        case 6: /* repeat */
            SDL_RenderDrawLine(r,cx-6,cy-3,cx+4,cy-3);
            SDL_RenderDrawLine(r,cx+4,cy-3,cx+2,cy-5);
            SDL_RenderDrawLine(r,cx+4,cy-3,cx+2,cy-1);
            SDL_RenderDrawLine(r,cx-4,cy+3,cx+6,cy+3);
            SDL_RenderDrawLine(r,cx-4,cy+3,cx-2,cy+1);
            SDL_RenderDrawLine(r,cx-4,cy+3,cx-2,cy+5);
            break;
        case 7: /* mute */
            SDL_RenderDrawLine(r,cx-6,cy-2,cx-3,cy-2);
            SDL_RenderDrawLine(r,cx-3,cy-2,cx+1,cy-5);
            SDL_RenderDrawLine(r,cx+1,cy-5,cx+1,cy+5);
            SDL_RenderDrawLine(r,cx+1,cy+5,cx-3,cy+2);
            SDL_RenderDrawLine(r,cx-6,cy+2,cx-3,cy+2);
            SDL_RenderDrawLine(r,cx+3,cy-3,cx+7,cy+3);
            SDL_RenderDrawLine(r,cx+7,cy-3,cx+3,cy+3);
            break;
        case 8: /* stop */
            {SDL_Rect stop={cx-4,cy-4,8,8};SDL_RenderFillRect(r,&stop);}
            break;
        case 9: /* folder */
            SDL_RenderDrawLine(r,cx-6,cy-4,cx-1,cy-4);
            SDL_RenderDrawLine(r,cx-1,cy-4,cx+1,cy-2);
            SDL_RenderDrawLine(r,cx+1,cy-2,cx+6,cy-2);
            SDL_RenderDrawLine(r,cx-6,cy-2,cx-6,cy+5);
            SDL_RenderDrawLine(r,cx-6,cy+5,cx+6,cy+5);
            SDL_RenderDrawLine(r,cx+6,cy-2,cx+6,cy+5);
            break;
        case 10: /* shuffle */
            SDL_RenderDrawLine(r,cx-6,cy-4,cx+6,cy+4);
            SDL_RenderDrawLine(r,cx-6,cy+4,cx+6,cy-4);
            break;
        case 11: /* settings */
            for(int j=-4;j<=4;j+=4)
                SDL_RenderDrawLine(r,cx-5,cy+j,cx+6,cy+j);
            break;
    }
}
static void paint_control_buttons(SDL_Renderer *r,SDL_Rect area,Uint32 ms,int repeat,int shuffle,int skip,int mic,int recording){
    static const char *names[12]={
        "SOURCE","PREV","PLAY","PAUSE","NEXT","REC",
        "REPEAT","MUTE","STOP","FOLDER","SHUFFLE","SETTINGS"
    };
    if(area.w<390||area.h<75)return;
    const int gapx=7,gapy=6,padx=13,pady=9;
    int bw=(area.w-2*padx-5*gapx)/6;
    int bh=(area.h-2*pady-gapy)/2;
    if(bh<25)return;
    for(int i=0;i<12;++i){
        int col=i%6,row=i/6;
        SDL_Rect button={area.x+padx+col*(bw+gapx),
                         area.y+pady+row*(bh+gapy),bw,bh};
        int mx=-1,my=-1;
        if(SDL_GetMouseFocus()==SDL_RenderGetWindow(r))
            SDL_GetMouseState(&mx,&my);
        int hovered=mx>=button.x&&mx<button.x+button.w&&
                    my>=button.y&&my<button.y+button.h;
        int enabled=(i==5 && recording)||(i==6 && repeat)||(i==10 && shuffle)||
                    ((i==1||i==4) && i==skip);
        SDL_SetRenderDrawColor(r,hovered?54:(enabled?44:32),
                               hovered?57:(enabled?53:30),
                               hovered?77:(enabled?67:42),255);
        SDL_RenderFillRect(r,&button);
        SDL_SetRenderDrawColor(r,hovered?151:79,hovered?132:70,
                               hovered?183:103,255);
        draw_rounded_frame(r,button,6);
        SDL_Rect inset={button.x+2,button.y+2,button.w-4,button.h-4};
        SDL_SetRenderDrawColor(r,hovered?119:50,hovered?101:45,
                               hovered?150:68,255);
        draw_rounded_frame(r,inset,4);
        SDL_Color accent=i==5?(SDL_Color){231,61,101,255}:
            (i==2||i==0||i==6)?(SDL_Color){32,201,151,255}:
            (i==6||i==10)?(SDL_Color){158,125,239,255}:
            (SDL_Color){74,141,234,255};
        SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r,accent.r,accent.g,accent.b,
                               (Uint8)(38+((ms/220+(Uint32)i)%3)*7));
        SDL_Rect glow={button.x+5,button.y+2,button.w-10,4};
        SDL_RenderFillRect(r,&glow);
        SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
        if(enabled){
            SDL_SetRenderDrawColor(r,32,201,151,255);
            SDL_Rect indicator={button.x+5,button.y+4,button.w-10,2};
            SDL_RenderFillRect(r,&indicator);
        }
        SDL_SetRenderDrawColor(r,accent.r,accent.g,accent.b,255);
        button_icon(r,button.x+button.w/2,button.y+bh/2-5,i);
        /* Button faces remain dark in all themes: force a bright label
         * without changing automatic contrast elsewhere in the UI. */
        SDL_Renderer *saved_theme_renderer=sauti_theme_renderer;
        sauti_theme_renderer=NULL;
        button_label(r,button.x+button.w/2,button.y+bh-12,i==0?(mic?"MIC":"OUT"):names[i]);
        sauti_theme_renderer=saved_theme_renderer;
    }
}

static void side_meter(SDL_Renderer *r,SDL_Rect rect,int percent,Uint32 ms){
    const int lit=(clamp100(percent)*20+99)/100;
    for(int i=0;i<20;++i){
        int y=rect.y+rect.h-(i+1)*rect.h/20+1;
        int height=rect.h/20-4;
        if(height<3)height=3;
        SDL_Rect led={rect.x,y,rect.w,height};
        /* Off state matches the graphite-violet cells of the spectrum. */
        SDL_SetRenderDrawColor(r,39,35,49,255);
        SDL_RenderFillRect(r,&led);
        /* Match the main ASM LED cells: hide four extreme corner pixels.
         * Also apply this to inactive segments, not just lit ones. */
        SDL_SetRenderDrawColor(r,16,16,22,255);
        SDL_RenderDrawPoint(r,led.x,led.y);
        SDL_RenderDrawPoint(r,led.x+led.w-1,led.y);
        SDL_RenderDrawPoint(r,led.x,led.y+led.h-1);
        SDL_RenderDrawPoint(r,led.x+led.w-1,led.y+led.h-1);
        if(i>=lit)continue;
        SDL_Color color=i==0?(SDL_Color){255,191,64,255}:i<9?
            (SDL_Color){32,201,151,255}:i<15?
            (SDL_Color){50,125,244,255}:(SDL_Color){231,61,101,255};
        SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
        /* Two soft, slightly animated light layers, rather than a flat bar. */
        Uint8 pulse=(Uint8)(27+((ms/130+(Uint32)i)%3)*7);
        SDL_Rect outer={led.x-5,led.y-5,led.w+10,led.h+10};
        SDL_SetRenderDrawColor(r,color.r,color.g,color.b,pulse);
        SDL_RenderFillRect(r,&outer);
        SDL_Rect inner={led.x-2,led.y-2,led.w+4,led.h+4};
        SDL_SetRenderDrawColor(r,color.r,color.g,color.b,67);
        SDL_RenderFillRect(r,&inner);
        SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r,color.r,color.g,color.b,255);
        SDL_RenderFillRect(r,&led);
        /* Glassy illuminated face, with subtle 1px rounded corners. */
        if(led.w>=5 && led.h>=4){
            SDL_SetRenderDrawColor(r,16,16,22,255);
            SDL_RenderDrawPoint(r,led.x,led.y);
            SDL_RenderDrawPoint(r,led.x+led.w-1,led.y);
            SDL_RenderDrawPoint(r,led.x,led.y+led.h-1);
            SDL_RenderDrawPoint(r,led.x+led.w-1,led.y+led.h-1);
            SDL_SetRenderDrawColor(r,
                (Uint8)(color.r+(255-color.r)/3),
                (Uint8)(color.g+(255-color.g)/3),
                (Uint8)(color.b+(255-color.b)/3),255);
            SDL_Rect face={led.x+1,led.y+1,led.w-2,led.h-2};
            SDL_RenderFillRect(r,&face);
        }
    }
}

static void slider_paint(SDL_Renderer *r,SDL_Rect box,const char *label,int val,Uint32 ms){
    small_label(r,box.x+8,box.y+5,label);
    char number[8];
    snprintf(number,sizeof(number),"%d%%",val);
    small_label(r,box.x+box.w-36,box.y+5,number);
    int x=box.x+12,y=box.y+box.h-17,width=box.w-24;
    SDL_Rect rail={x,y,width,5};
    SDL_SetRenderDrawColor(r,39,35,52,255);
    SDL_RenderFillRect(r,&rail);
    SDL_SetRenderDrawColor(r,67,62,81,255);
    SDL_Rect fill={x,y,width*val/100,5};
    SDL_RenderFillRect(r,&fill);
    int knob=x+(width-1)*val/100;
    /* Potentiometer handle is a glassy horizontal LED segment (not a line).
     * Track remains understated. Glow pulses without moving the handle. */
    /* Same rising color bands as the 20-segment spectrum:
     * segment 0 amber, 1..8 emerald, 9..14 sapphire, 15..19 ruby. */
    int segment=clamp100(val)*20/100;
    if(segment>19)segment=19;
    const SDL_Color neon=segment==0?(SDL_Color){255,191,64,255}:
        segment<9?(SDL_Color){32,201,151,255}:
        segment<15?(SDL_Color){50,125,244,255}:
                   (SDL_Color){231,61,101,255};
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    Uint8 pulse=(Uint8)(24+(ms/130%3)*9);
    SDL_SetRenderDrawColor(r,neon.r,neon.g,neon.b,pulse);
    SDL_Rect outer={knob-16,y-12,32,28};
    SDL_RenderFillRect(r,&outer);
    SDL_SetRenderDrawColor(r,neon.r,neon.g,neon.b,78);
    SDL_Rect inner={knob-12,y-9,24,22};
    SDL_RenderFillRect(r,&inner);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r,neon.r,neon.g,neon.b,255);
    SDL_Rect thumb={knob-10,y-6,20,16};
    SDL_RenderFillRect(r,&thumb);
    /* Rounded 1px corners and a brighter glass-like inset. */
    SDL_SetRenderDrawColor(r,16,16,22,255);
    SDL_RenderDrawPoint(r,thumb.x,thumb.y);
    SDL_RenderDrawPoint(r,thumb.x+thumb.w-1,thumb.y);
    SDL_RenderDrawPoint(r,thumb.x,thumb.y+thumb.h-1);
    SDL_RenderDrawPoint(r,thumb.x+thumb.w-1,thumb.y+thumb.h-1);
    SDL_SetRenderDrawColor(r,
        (Uint8)(neon.r+(255-neon.r)/3),
        (Uint8)(neon.g+(255-neon.g)/3),
        (Uint8)(neon.b+(255-neon.b)/3),255);
    SDL_Rect face={thumb.x+1,thumb.y+2,thumb.w-2,thumb.h-4};
    SDL_RenderFillRect(r,&face);
}
/* RAYS visualizer: same geometry for left/right, independent stereo
 * spectral band magnitudes. SDL-only decoration; never touches the ASM FFT. */
/* Rotation is channel-local. Reverse only on a new low-frequency
 * transient, with hysteresis and a cooldown to avoid flutter. */
typedef struct {
    float angle;
    float speed;
    float previous_bass;
    int armed;
    Uint32 previous_ms;
    Uint32 last_reverse_ms;
} SautiRaysMotion;
static SautiRaysMotion rays_motion[2]={
    {0.0f,0.62f,0.0f,1,0,0},
    {0.0f,-0.62f,0.0f,1,0,0}
};
static float ray_slice_energy(const float *bands,int slice,int slices){
    if(!bands||slice<0||slices<1)return 0.0f;
    int start=slice*(int)SAUTI_LOG_BANDS/slices;
    int count=(int)SAUTI_LOG_BANDS/slices;
    if(count<1)count=1;
    float energy=0.0f;
    for(int k=0;k<count && start+k<(int)SAUTI_LOG_BANDS;++k){
        float value=bands[start+k];
        if(value>energy)energy=value;
    }
    if(energy<0.0f)energy=0.0f;
    if(energy>1.0f)energy=1.0f;
    return energy;
}
static void sauti_rays_advance(SautiRaysMotion *m,const float *bands,Uint32 now){
    float bass=ray_slice_energy(bands,0,12);
    if(bass<0.38f)m->armed=1;
    if(m->armed && bass>0.62f && bass-m->previous_bass>0.09f &&
       (m->last_reverse_ms==0 || now-m->last_reverse_ms>=360)){
        m->speed=-m->speed;
        m->last_reverse_ms=now;
        m->armed=0;
    }
    if(m->previous_ms){
        float dt=(float)(now-m->previous_ms)*0.001f;
        if(dt>0.05f)dt=0.05f;
        m->angle+=m->speed*dt*(1.0f+0.50f*bass);
        if(m->angle>6.2831853f)m->angle-=6.2831853f;
        if(m->angle< -6.2831853f)m->angle+=6.2831853f;
    }
    m->previous_ms=now;
    m->previous_bass=bass;
}
/* Discreet virtual loudspeaker artwork behind RAYS, independently animated
 * for left and right channels. SDL primitives only; no new assets needed. */
static void sauti_speaker_circle(SDL_Renderer *r,int cx,int cy,int radius){
    if(radius<2)return;
    const int segments=96;
    for(int i=0;i<segments;++i){
        float a=(float)i*6.28318530718f/(float)segments;
        float b=(float)(i+1)*6.28318530718f/(float)segments;
        SDL_RenderDrawLine(r,cx+(int)lrintf(cosf(a)*radius),
            cy+(int)lrintf(sinf(a)*radius),
            cx+(int)lrintf(cosf(b)*radius),
            cy+(int)lrintf(sinf(b)*radius));
    }
}
static void sauti_speaker_disc(SDL_Renderer *r,int cx,int cy,int radius){
    if(radius<2)return;
    for(int dy=-radius;dy<=radius;++dy){
        int dx=(int)sqrtf((float)(radius*radius-dy*dy));
        SDL_RenderDrawLine(r,cx-dx,cy+dy,cx+dx,cy+dy);
    }
}
static void sauti_speaker_backdrop(SDL_Renderer *r,SDL_Rect bay,const float *bands){
    if(bay.w<90||bay.h<120)return;
    SDL_BlendMode previous;
    SDL_GetRenderDrawBlendMode(r,&previous);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    int light=sauti_is_light_bg(sauti_theme_bg);
    Uint8 base=light?30:135;
    Uint8 alpha=light?43:34;
    SDL_Rect cabinet={bay.x+12,bay.y+12,bay.w-24,bay.h-24};
    SDL_SetRenderDrawColor(r,base,base,light?40:163,alpha);
    /* Use plain translucent lines here: themed rounded frames intentionally
     * force opaque borders and would overpower the speaker artwork. */
    SDL_RenderDrawRect(r,&cabinet);
    SDL_Rect inner={cabinet.x+3,cabinet.y+3,cabinet.w-6,cabinet.h-6};
    SDL_RenderDrawRect(r,&inner);
    int cx=bay.x+bay.w/2,cy=bay.y+bay.h/2;
    float bass=0.f;
    if(bands){
        for(int i=0;i<4;++i)if(bands[i]>bass)bass=bands[i];
        if(bass>1.f)bass=1.f;
        if(bass<0.f)bass=0.f;
    }
    int radius=bay.w/2-19;
    if(radius>bay.h/2-28)radius=bay.h/2-28;
    if(radius<15)radius=15;
    int pulse=(int)(bass*2.5f);
    /* Layered woofer surface: shadow, suspension, cone, dust cap.
     * The dark surfaces preserve bright RAYS drawn on top. */
    SDL_SetRenderDrawColor(r,light?36:5,light?43:7,light?54:15,light?38:148);
    sauti_speaker_disc(r,cx,cy,radius+pulse);
    SDL_SetRenderDrawColor(r,light?52:39,light?59:43,light?77:67,light?75:150);
    sauti_speaker_circle(r,cx,cy,radius+pulse);
    sauti_speaker_circle(r,cx,cy,radius-3+pulse);
    SDL_SetRenderDrawColor(r,light?48:17,light?55:20,light?68:34,light?50:134);
    sauti_speaker_disc(r,cx,cy,radius-7+pulse);
    SDL_SetRenderDrawColor(r,light?58:52,light?67:61,light?82:89,light?60:116);
    sauti_speaker_circle(r,cx,cy,radius-11+pulse);
    sauti_speaker_circle(r,cx,cy,radius-18+pulse);
    /* Tiny upper tweeter, integrated into the cabinet above the woofer. */
    int tweeter_y=cabinet.y+19;
    SDL_SetRenderDrawColor(r,light?43:15,light?49:20,light?64:36,light?42:160);
    sauti_speaker_disc(r,cx,tweeter_y,11);
    SDL_SetRenderDrawColor(r,light?61:78,light?70:85,light?87:119,light?80:156);
    sauti_speaker_circle(r,cx,tweeter_y,11);
    sauti_speaker_circle(r,cx,tweeter_y,5);
    for(int j=0;j<4;++j){
        int x=j%2?cabinet.x+cabinet.w-9:cabinet.x+9;
        int y=j/2?cabinet.y+cabinet.h-9:cabinet.y+9;
        SDL_RenderDrawLine(r,x-2,y,x+2,y);
        SDL_RenderDrawLine(r,x,y-2,x,y+2);
    }
    SDL_SetRenderDrawBlendMode(r,previous);
}
/* Opaque dust cap OVER the rays, like the center of a real woofer.
 * Its outer edge masks the ray origins, while RAYS remain visible outside. */
static void sauti_speaker_dust_cap(SDL_Renderer *r,SDL_Rect bay,const float *bands){
    if(bay.w<90||bay.h<120)return;
    int cx=bay.x+bay.w/2,cy=bay.y+bay.h/2;
    int radius=bay.w/2-19;
    if(radius>bay.h/2-28)radius=bay.h/2-28;
    if(radius<15)radius=15;
    float bass=0.f;
    if(bands){
        for(int i=0;i<4;++i)if(bands[i]>bass)bass=bands[i];
        if(bass<0.f)bass=0.f;
        if(bass>1.f)bass=1.f;
    }
    int pulse=(int)(bass*2.5f);
    int cap=radius/3+pulse;
    if(cap<15)cap=15;
    int light=sauti_is_light_bg(sauti_theme_bg);
    SDL_BlendMode old;
    SDL_GetRenderDrawBlendMode(r,&old);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
    /* Opaque filled silhouette creates the overlap missing in the mockup. */
    SDL_SetRenderDrawColor(r,light?57:12,light?65:17,light?77:31,255);
    sauti_speaker_disc(r,cx,cy,cap);
    SDL_SetRenderDrawColor(r,light?93:48,light?104:57,light?122:81,255);
    sauti_speaker_circle(r,cx,cy,cap);
    sauti_speaker_circle(r,cx,cy,cap-2);
    SDL_SetRenderDrawColor(r,light?80:26,light?92:36,light?109:53,255);
    sauti_speaker_disc(r,cx-1,cy-2,cap-5);
    SDL_SetRenderDrawColor(r,light?124:87,light?137:107,light?154:143,255);
    sauti_speaker_circle(r,cx-2,cy-2,cap-6);
    SDL_SetRenderDrawBlendMode(r,old);
}
static void sauti_rays(SDL_Renderer *r,SDL_Rect frame,const float *bands,float rotation,Uint32 ms){
    const SDL_Color colors[10]={
        {239,179,80,255},{167,225,71,255},
        {32,201,151,255},{40,218,203,255},
        {74,195,245,255},{74,147,245,255},
        {145,113,243,255},{216,93,213,255},
        {231,61,101,255},{245,125,91,255}
    };
    const int rays=40,slices=12;
    int cx=frame.x+frame.w/2,cy=frame.y+frame.h/2;
    float half_w=(float)(frame.w/2-13);
    float half_h=(float)(frame.h/2-13);
    SDL_Rect clip={frame.x+5,frame.y+5,frame.w-10,frame.h-10};
    if(clip.w<8||clip.h<8||half_w<8.0f||half_h<8.0f)return;
    SDL_RenderSetClipRect(r,&clip);
    for(int i=0;i<rays;++i){
        float angle=(float)i*3.14159265358979323846f/(float)rays+rotation;
        float ux=cosf(angle),uy=sinf(angle);
        float energy=ray_slice_energy(bands,i*slices/rays,slices);
        float bass=ray_slice_energy(bands,0,slices);
        /* Aggressive dynamics: a short resting radius and a steep response.
         * Gate the quiet floor instead of sqrt() expanding weak signals:
         * the rays now collapse visibly between beats. */
        float driven=(energy-0.09f)*1.75f;
        if(driven<0.0f)driven=0.0f;
        if(driven>1.0f)driven=1.0f;
        float energy_boost=driven*driven*(2.0f-driven);
        float bass_drive=(bass-0.16f)*1.35f;
        if(bass_drive<0.0f)bass_drive=0.0f;
        if(bass_drive>1.0f)bass_drive=1.0f;
        float radius_x=half_w/(fabsf(ux)>0.001f?fabsf(ux):0.001f);
        float radius_y=half_h/(fabsf(uy)>0.001f?fabsf(uy):0.001f);
        float maximum=radius_x<radius_y?radius_x:radius_y;
        /* Deterministic per-ray variation: neighboring beams get distinctly
         * different resting lengths and responses, rather than repeating
         * a five-ray fan. The two channels share the same geometric pattern. */
        unsigned seed=(unsigned)i*747796405u+2891336453u;
        seed=(seed^(seed>>16))*2246822519u;
        seed^=seed>>13;
        float shape=(float)(seed&255u)/255.0f;
        float response=(float)((seed>>8)&255u)/255.0f;
        float variation=0.025f+0.105f*shape;
        float strength=variation+(0.56f+0.39f*response)*energy_boost
                       +(0.07f+0.13f*(1.0f-response))*bass_drive;
        if(strength>0.98f)strength=0.98f;
        float length=maximum*strength;
        int dx=(int)(ux*length),dy=(int)(uy*length);
        SDL_Color c=colors[i%10];
        /* Wide bright neon halo around a darker, crisp colored ray.
         * Software SDL2 has no blur shader: offset lines approximate it.
         * Keep the 10px width, but brighten the aura RGB towards white;
         * draw a darker opaque core LAST so its contour remains legible. */
        if(energy_boost>0.02f){
            float nx=-uy,ny=ux;
            float glow=0.28f+0.72f*energy_boost;
            float pulse=0.94f+0.06f*(float)((ms/110u+(Uint32)i)%3u);
            Uint8 gr=(Uint8)(c.r+(255-c.r)*0.52f);
            Uint8 gg=(Uint8)(c.g+(255-c.g)*0.52f);
            Uint8 gb=(Uint8)(c.b+(255-c.b)*0.52f);
            SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
            for(int off=-10;off<=10;++off){
                int distance=off<0?-off:off;
                float falloff=1.0f-(float)distance/11.0f;
                Uint8 alpha=(Uint8)((30.0f+75.0f*glow)*falloff*falloff*pulse);
                int ox=(int)lroundf(nx*(float)off);
                int oy=(int)lroundf(ny*(float)off);
                SDL_SetRenderDrawColor(r,gr,gg,gb,alpha);
                SDL_RenderDrawLine(r,cx-dx+ox,cy-dy+oy,cx+dx+ox,cy+dy+oy);
            }
            for(int off=-4;off<=4;++off){
                int distance=off<0?-off:off;
                float falloff=1.0f-(float)distance/5.0f;
                Uint8 alpha=(Uint8)((65.0f+120.0f*glow)*falloff*pulse);
                int ox=(int)lroundf(nx*(float)off);
                int oy=(int)lroundf(ny*(float)off);
                SDL_SetRenderDrawColor(r,gr,gg,gb,alpha);
                SDL_RenderDrawLine(r,cx-dx+ox,cy-dy+oy,cx+dx+ox,cy+dy+oy);
            }
        }
        SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
        /* Darker saturated core, visually separated from luminous bloom. */
        SDL_SetRenderDrawColor(r,
            (Uint8)((float)c.r*(0.34f+0.29f*energy_boost)),
            (Uint8)((float)c.g*(0.34f+0.29f*energy_boost)),
            (Uint8)((float)c.b*(0.34f+0.29f*energy_boost)),255);
        SDL_RenderDrawLine(r,cx-dx,cy-dy,cx+dx,cy+dy);
        SDL_RenderDrawLine(r,cx-dx+1,cy-dy,cx+dx+1,cy+dy);
        if(energy_boost>0.62f)
            SDL_RenderDrawLine(r,cx-dx,cy-dy+1,cx+dx,cy+dy+1);
    }
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r,220,207,241,255);
    SDL_Rect centre={cx-3,cy-3,6,6};
    SDL_RenderFillRect(r,&centre);
    SDL_SetRenderDrawColor(r,16,16,22,255);
    SDL_RenderDrawPoint(r,centre.x,centre.y);
    SDL_RenderDrawPoint(r,centre.x+centre.w-1,centre.y);
    SDL_RenderDrawPoint(r,centre.x,centre.y+centre.h-1);
    SDL_RenderDrawPoint(r,centre.x+centre.w-1,centre.y+centre.h-1);
    SDL_RenderSetClipRect(r,NULL);
}
/* Nine lightweight animated fractal-like branching patterns. Style 0 is
 * the original RAYS; the other eight vary recursion, branching and symmetry.
 * SDL-only visualization, driven by channel-specific NASM spectrum data. */
static int sauti_visual_style=0;
static void sauti_fractal_branch(SDL_Renderer *r,float x,float y,float angle,
                                float length,int depth,int style,float energy,
                                unsigned seed){
    if(depth<=0 || length<2.0f)return;
    float endx=x+cosf(angle)*length,endy=y+sinf(angle)*length;
    Uint8 red=(Uint8)(55+(seed*59u+style*33u)%160u);
    Uint8 green=(Uint8)(85+(seed*23u+style*71u)%140u);
    Uint8 blue=(Uint8)(110+(seed*41u+style*47u)%135u);
    SDL_SetRenderDrawColor(r,red,green,blue,255);
    SDL_RenderDrawLine(r,(int)x,(int)y,(int)endx,(int)endy);
    float spread=0.22f+0.10f*(float)(style%4)+0.12f*energy;
    float scale=0.55f+0.05f*(float)(style%3);
    if(style==2||style==6)spread*=1.6f;
    if(style==4)scale=0.70f;
    float twist=0.07f*sinf((float)seed*1.71f+energy*4.0f);
    sauti_fractal_branch(r,endx,endy,angle-spread+twist,length*scale,
                         depth-1,style,energy,seed*3u+1u);
    sauti_fractal_branch(r,endx,endy,angle+spread+twist,length*scale,
                         depth-1,style,energy,seed*3u+2u);
    if((style==3||style==7) && depth>2)
        sauti_fractal_branch(r,endx,endy,angle+twist,length*0.46f,
                             depth-1,style,energy,seed*3u+3u);
}
static void sauti_fractal(SDL_Renderer *r,SDL_Rect frame,const float *bands,
                          Uint32 ms,int style,int channel){
    if(frame.w<24||frame.h<24)return;
    float bass=ray_slice_energy(bands,0,12);
    float mid=ray_slice_energy(bands,4,12);
    float energy=(bass+mid)*0.5f;
    float cx=(float)frame.x+(float)frame.w*0.5f;
    float cy=(float)frame.y+(float)frame.h*0.5f;
    float span=(float)(frame.w<frame.h?frame.w:frame.h);
    float phase=(float)ms*0.0003f*(channel? -1.0f:1.0f);
    int branches=style==1?3:style==2?5:style==3?6:
                 style==4?4:style==5?7:style==6?8:style==7?5:9;
    int depth=style==4?6:style==3?4:5;
    SDL_Rect clip={frame.x+4,frame.y+4,frame.w-8,frame.h-8};
    SDL_RenderSetClipRect(r,&clip);
    for(int i=0;i<branches;++i){
        float angle=6.2831853f*(float)i/(float)branches+phase;
        float size=span*(0.13f+0.10f*energy);
        if(style==5)size*=0.75f;
        if(style==8)size*=0.95f+0.25f*sinf(phase*3.0f+(float)i);
        sauti_fractal_branch(r,cx,cy,angle,size,depth,style,energy,
                             (unsigned)(i+1));
    }
    SDL_RenderSetClipRect(r,NULL);
}
static SDL_Rect sauti_visual_bay(int w,int h,int side){
    SDL_Rect outer,buttons,sliders,lm,rm;
    panel_geometry(w,h,&outer,&buttons,&sliders,&lm,&rm);
    SDL_Rect title={outer.x,outer.y+outer.h+6,outer.w,34};
    int bay_x=outer.x+outer.w+15,bay_w=w-bay_x-16;
    SDL_Rect bay={side==0?outer.x-15-bay_w:bay_x,title.y,
                  bay_w,sliders.y+sliders.h-title.y};
    if(w<650||h<550||bay.w<65||bay.h<55||bay.x<8||
       bay.x+bay.w>w-8)bay.w=0;
    return bay;
}
typedef struct {SDL_Window *window;SDL_Renderer *renderer;} SautiVisualPicker;
static void sauti_picker_close(SautiVisualPicker *p){
    if(p->renderer)SDL_DestroyRenderer(p->renderer);
    if(p->window)SDL_DestroyWindow(p->window);
    p->renderer=NULL;p->window=NULL;
}
static void sauti_picker_open(SautiVisualPicker *p){
    if(p->window){SDL_RaiseWindow(p->window);return;}
    p->window=SDL_CreateWindow("Sauti - 9 stylow fraktalnych",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,744,654,0);
    if(!p->window)return;
    p->renderer=SDL_CreateRenderer(p->window,-1,SDL_RENDERER_SOFTWARE);
    if(!p->renderer)sauti_picker_close(p);
}
static const SDL_Color sauti_backgrounds[8]={
    {212,214,216,255}, /* light gray */
    {181,201,176,255}, /* muted green */
    {180,201,218,255}, /* muted blue */
    {216,135,59,255},  /* orange */
    {85,148,92,255},   /* green */
    {16,16,24,255},    /* original */
    {73,29,48,255},    /* burgundy */
    {23,40,74,255}     /* navy */
};
static void sauti_background_apply(uint32_t *pixels,int w,int h,int choice){
    if(!pixels||w<=0||h<=0||choice<0||choice>=8||choice==5)return;
    SDL_Color bg=sauti_backgrounds[choice];
    for(size_t i=0,n=(size_t)w*(size_t)h;i<n;++i){
        uint32_t px=pixels[i];
        unsigned red=(px>>16)&255u,green=(px>>8)&255u,blue=px&255u;
        /* Replace only very dark neutral backdrop pixels from NASM.
         * Bright spectrum bars and saturated stereo visuals remain unchanged. */
        unsigned max=red>green?red:green;
        if(blue>max)max=blue;
        unsigned min=red<green?red:green;
        if(blue<min)min=blue;
        if(max<=36u && max-min<=16u)
            pixels[i]=0xff000000u|((uint32_t)bg.r<<16)|
                      ((uint32_t)bg.g<<8)|(uint32_t)bg.b;
    }
}
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    int page; /* 1 appearance, 2 folders, 3 language, 4 controls, 5 audio */
    int language; /* 0 English, 1 Polski */
    int accent;   /* 0 green, 1 violet, 2 blue */
    int background; /* 0-7, default 5 */
    int frame_color; /* 0-7, default 5 */
} SautiSettings;
static void sauti_settings_path(char *out,size_t cap){
    const char *base=getenv("XDG_CONFIG_HOME"),*home=getenv("HOME");
    if(base&&*base)snprintf(out,cap,"%s/sayari-sauti/settings.ini",base);
    else snprintf(out,cap,"%s/.config/sayari-sauti/settings.ini",home&&*home?home:"/tmp");
}
static void sauti_settings_save(const SautiSettings *p){
    char path[PATH_MAX],folder[PATH_MAX];
    sauti_settings_path(path,sizeof(path));
    snprintf(folder,sizeof(folder),"%s",path);
    char *slash=strrchr(folder,'/');
    if(!slash)return;
    *slash=0;
    if(mkdir(folder,0700)!=0&&errno!=EEXIST)return;
    FILE *fp=fopen(path,"w");
    if(!fp)return;
    fprintf(fp,"language=%d\naccent=%d\nbackground=%d\nframes=%d\n",p->language,p->accent,p->background,p->frame_color);
    fclose(fp);
}
static void sauti_settings_load(SautiSettings *p){
    char path[PATH_MAX],line[128];
    sauti_settings_path(path,sizeof(path));
    FILE *fp=fopen(path,"r");
    if(!fp)return;
    while(fgets(line,sizeof(line),fp)){
        int value=-1;
        if(sscanf(line,"language=%d",&value)==1&&value>=0&&value<=2)p->language=value;
        if(sscanf(line,"accent=%d",&value)==1&&value>=0&&value<=2)p->accent=value;
        if(sscanf(line,"background=%d",&value)==1&&value>=0&&value<8)p->background=value;
        if(sscanf(line,"frames=%d",&value)==1&&value>=0&&value<8)p->frame_color=value;
    }
    fclose(fp);
}
static void sauti_settings_close(SautiSettings *p){
    if(p->renderer)SDL_DestroyRenderer(p->renderer);
    if(p->window)SDL_DestroyWindow(p->window);
    p->renderer=NULL;p->window=NULL;p->page=5;
}
static void sauti_settings_open(SautiSettings *p){
    if(p->window){SDL_RaiseWindow(p->window);return;}
    p->page=5;
    p->window=SDL_CreateWindow("Sauti - Ustawienia / Settings",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,580,630,0);
    if(!p->window)return;
    p->renderer=SDL_CreateRenderer(p->window,-1,SDL_RENDERER_SOFTWARE);
    if(!p->renderer)sauti_settings_close(p);
}
/* Subtle symmetric trapezoids with common bottom baseline. */
static int sauti_settings_tab_hit(int x,int y){
    for(int i=0;i<5;i++){
        int left=20+i*108,top=103,bottom=144;
        if(y<top||y>=bottom)continue;
        int inset=(bottom-y)*11/(bottom-top);
        if(x>=left+inset&&x<left+104-inset)return i;
    }
    return -1;
}
static void sauti_settings_tabs(SDL_Renderer *r,int page,int polish){
    static const SDL_Color colors[5]={
        {75,118,133,255},{110,89,131,255},{131,109,77,255},
        {73,117,103,255},{123,89,105,255}
    };
    static const char *pl[]={"DZWIEK","WYGLAD","FOLDERY","JEZYK","STEROW."};
    static const char *en[]={"AUDIO","LOOK","FOLDERS","LANGUAGE","CONTROL"};
    static const int pages[]={5,1,2,3,4};
    for(int i=0;i<5;i++){
        int x=20+i*108,top=page==pages[i]?98:103,bottom=145;
        SDL_Color c=colors[i];
        for(int y=top;y<bottom;y++){
            int inset=(bottom-y)*11/(bottom-top);
            SDL_SetRenderDrawColor(r,
                page==pages[i]?(Uint8)(c.r/2+22):(Uint8)(c.r/4+20),
                page==pages[i]?(Uint8)(c.g/2+22):(Uint8)(c.g/4+20),
                page==pages[i]?(Uint8)(c.b/2+22):(Uint8)(c.b/4+20),255);
            SDL_RenderDrawLine(r,x+inset,y,x+103-inset,y);
        }
        SDL_SetRenderDrawColor(r,c.r,c.g,c.b,255);
        SDL_RenderDrawLine(r,x+11,top,x+92,top);
        SDL_RenderDrawLine(r,x+11,top,x,bottom-1);
        SDL_RenderDrawLine(r,x+92,top,x+103,bottom-1);
        if(page!=pages[i])SDL_RenderDrawLine(r,x,bottom-1,x+103,bottom-1);
        button_label(r,x+52,123,polish?pl[i]:en[i]);
    }
}
static void sauti_settings_panel(SDL_Renderer *r,int x,int y,int w,int h){
    SDL_Rect rect={x,y,w,h};
    SDL_SetRenderDrawColor(r,29,32,45,255);SDL_RenderFillRect(r,&rect);
    SDL_SetRenderDrawColor(r,54,61,79,255);draw_rounded_frame(r,rect,7);
}
static void sauti_settings_choice(SDL_Renderer *r,int x,int y,int w,int h,
                                   const char *name,int selected,SDL_Color accent){
    SDL_Rect rect={x,y,w,h};
    SDL_SetRenderDrawColor(r,selected?41:31,selected?48:35,selected?61:48,255);
    SDL_RenderFillRect(r,&rect);
    SDL_SetRenderDrawColor(r,selected?accent.r:58,selected?accent.g:65,selected?accent.b:80,255);
    draw_rounded_frame(r,rect,6);
    button_label(r,x+w/2,y+h/2-3,name);
    if(selected){
        SDL_SetRenderDrawColor(r,accent.r,accent.g,accent.b,255);
        SDL_Rect line={x+12,y+h-5,w-24,2};SDL_RenderFillRect(r,&line);
    }
}
static void sauti_settings_swatch(SDL_Renderer *r,int cx,int cy,SDL_Color c,int selected){
    const int radius=14;
    SDL_SetRenderDrawColor(r,c.r,c.g,c.b,255);
    for(int dy=-radius;dy<=radius;dy++){
        int dx=0;while(dx*dx+dy*dy<=radius*radius)dx++;
        SDL_RenderDrawLine(r,cx-dx+1,cy+dy,cx+dx-1,cy+dy);
    }
    if(selected){
        SDL_SetRenderDrawColor(r,235,237,245,255);
        for(int dy=-radius-4;dy<=radius+4;dy++){
            int dx=0;while(dx*dx+dy*dy<=(radius+4)*(radius+4))dx++;
            SDL_RenderDrawPoint(r,cx-dx+1,cy+dy);
            SDL_RenderDrawPoint(r,cx+dx-1,cy+dy);
        }
        SDL_RenderDrawLine(r,cx-5,cy,cx-1,cy+4);
        SDL_RenderDrawLine(r,cx-1,cy+4,cx+7,cy-5);
    }
}
static void sauti_settings_paint(SautiSettings *p,int repeat,int shuffle){
    if(!p->renderer)return;
    SDL_Renderer *r=p->renderer;
    SDL_SetRenderDrawColor(r,15,17,26,255);SDL_RenderClear(r);
    SDL_SetRenderDrawColor(r,213,218,230,255);
    button_label(r,290,43,p->language?"SAYARI SAUTI  /  USTAWIENIA":"SAYARI SAUTI  /  SETTINGS");
    SDL_SetRenderDrawColor(r,69,78,97,255);
    SDL_RenderDrawLine(r,25,73,555,73);
    sauti_settings_tabs(r,p->page,p->language);
    sauti_settings_panel(r,20,145,540,462);
    SDL_Color accent={89,156,165,255};
    if(p->page==1){
        button_label(r,290,171,p->language?"KOLORY INTERFEJSU":"INTERFACE COLORS");
        sauti_settings_panel(r,35,193,510,86);
        button_label(r,109,210,p->language?"AKCENT":"ACCENT");
        static const SDL_Color a[3]={{51,167,134,255},{145,110,189,255},{83,142,202,255}};
        static const char *names[]={"GREEN","VIOLET","BLUE"};
        for(int i=0;i<3;i++){
            int x=157+i*126;
            sauti_settings_choice(r,x,214,116,46,names[i],p->accent==i,a[i]);
        }
        sauti_settings_panel(r,35,293,510,128);
        button_label(r,290,312,p->language?"KOLOR TLA":"BACKGROUND");
        sauti_settings_panel(r,35,435,510,128);
        button_label(r,290,454,p->language?"KOLOR RAMEK":"FRAMES");
        static const char *colors[]={"GRAY","M.GREEN","M.BLUE","ORANGE","GREEN","ORIGINAL","BURGUNDY","NAVY"};
        for(int i=0;i<8;i++){
            int cx=80+i*62,cy=355;
            sauti_settings_swatch(r,cx,cy,sauti_backgrounds[i],p->background==i);
            button_label(r,cx,390,colors[i]);
            sauti_settings_swatch(r,cx,497,sauti_frame_colors[i],p->frame_color==i);
            button_label(r,cx,532,colors[i]);
        }
    }else if(p->page==2){
        button_label(r,290,175,p->language?"BIBLIOTEKA I FOLDERY":"LIBRARY AND FOLDERS");
        sauti_settings_panel(r,35,202,510,200);
        button_label(r,290,240,p->language?"DOMYSLNA LOKALIZACJA MUZYKI":"DEFAULT MUSIC LOCATION");
        sauti_settings_choice(r,57,278,466,47,p->language?"OTWORZ PRZEGLADARKE":"OPEN FILE BROWSER",0,accent);
        sauti_settings_choice(r,57,340,466,47,p->language?"ZAPISZ DOMYSLNY FOLDER":"SAVE DEFAULT FOLDER",0,accent);
    }else if(p->page==3){
        button_label(r,290,175,p->language?"JEZYK INTERFEJSU":"INTERFACE LANGUAGE");
        sauti_settings_panel(r,35,202,510,172);
        sauti_settings_choice(r,57,229,466,51,"ENGLISH",p->language==0,accent);
        sauti_settings_choice(r,57,299,466,51,"POLSKI",p->language==1,accent);
    }else if(p->page==4){
        button_label(r,290,175,p->language?"STEROWANIE ODTWARZANIEM":"PLAYBACK CONTROLS");
        sauti_settings_panel(r,35,202,510,172);
        sauti_settings_choice(r,57,229,466,51,"REPEAT",repeat,accent);
        sauti_settings_choice(r,57,299,466,51,"SHUFFLE",shuffle,accent);
    }else{
        button_label(r,290,175,p->language?"TOR ODTWARZANIA":"PLAYBACK PIPELINE");
        sauti_settings_panel(r,35,202,510,162);
        button_label(r,290,226,p->language?"AKTYWNE WYJSCIE":"ACTIVE AUDIO OUTPUT");
        sauti_settings_choice(r,57,247,466,53,"SDL2  /  FLOAT32",1,accent);
        button_label(r,290,334,p->language?"WYBOR SILNIKA: W PRZYGOTOWANIU":"BACKEND SELECTOR: PLANNED");
        sauti_settings_panel(r,35,378,510,130);
        button_label(r,290,406,p->language?"CZESTOTLIWOSC WG PLIKU":"SOURCE SAMPLE RATE");
        button_label(r,290,457,p->language?"PIPEWIRE: AKTUALNIE PRZECHWYTYWANIE":"PIPEWIRE: CAPTURE ONLY");
    }
    SDL_SetRenderDrawColor(r,63,71,88,255);
    SDL_RenderDrawLine(r,25,617,555,617);
    SDL_RenderPresent(r);
}
static int sauti_picker_choice(int x,int y){
    int col=(x-18)/236,row=(y-18)/206;
    if(x<18||y<18||col<0||col>=3||row<0||row>=3)return -1;
    int lx=(x-18)%236,ly=(y-18)%206;
    if(lx>=226||ly>=196)return -1;
    return row*3+col;
}
static void sauti_picker_paint(SautiVisualPicker *p,Uint32 ms,
                               const float *left,const float *right){
    if(!p->renderer)return;
    SDL_Renderer *r=p->renderer;
    SDL_SetRenderDrawColor(r,16,16,22,255);
    SDL_RenderClear(r);
    for(int i=0;i<9;++i){
        SDL_Rect tile={18+(i%3)*236,18+(i/3)*206,226,196};
        SDL_SetRenderDrawColor(r,i==sauti_visual_style?32:91,
            i==sauti_visual_style?201:80,i==sauti_visual_style?151:117,255);
        draw_rounded_frame(r,tile,8);
        SDL_Rect inner={tile.x+5,tile.y+5,tile.w-10,tile.h-10};
        if(i==0){
            sauti_rays(r,inner,left,rays_motion[0].angle,ms);
        }else{
            sauti_fractal(r,inner,i%2?left:right,ms,i,0);
        }
        SDL_SetRenderDrawColor(r,32,201,151,255);
        SDL_Rect dot={tile.x+tile.w-14,tile.y+9,5,5};
        if(i==sauti_visual_style)SDL_RenderFillRect(r,&dot);
    }
    SDL_RenderPresent(r);
}
static void copy_playing_label(char *dst,size_t cap,const char *path){
    if(!dst||cap<2)return;
    dst[0]=0;
    if(!path||!path[0])return;
    const char *name=strrchr(path,'/');
    name=name?name+1:path;
    const char *end=strrchr(name,'.');
    if(!end||end==name)end=name+strlen(name);
    size_t n=0;
    for(;name<end&&n+1<cap;++name){
        unsigned char c=(unsigned char)*name;
        if(c>='a'&&c<='z')c=(unsigned char)(c-32);
        if((c>='A'&&c<='Z')||c==' '|| (c>='0'&&c<='9'))
            dst[n++]=(char)c;
    }
    dst[n]=0;
}
static void paint_audio_controls(SDL_Renderer *r,int w,int h,int gain,int vol,int position,Uint32 ms,const float *left_bands,const float *right_bands,const char *playing,int repeat,int shuffle,int skip,int mic,int recording){
    if(w<650||h<550)return;
    SDL_Rect outer,empty,sliders,lm,rm;
    panel_geometry(w,h,&outer,&empty,&sliders,&lm,&rm);
    if(sliders.h<40)return;
    SDL_SetRenderDrawColor(r,105,93,132,255);
    draw_rounded_frame(r,sliders,10);
    SDL_SetRenderDrawColor(r,51,46,67,255);
    SDL_Rect inside={sliders.x+3,sliders.y+3,sliders.w-6,sliders.h-6};
    draw_rounded_frame(r,inside,8);
    /* Now playing: selected audio file metadata. */
    SDL_Rect title_box={outer.x,outer.y+outer.h+6,outer.w,34};
    SDL_SetRenderDrawColor(r,105,93,132,255);
    draw_rounded_frame(r,title_box,9);
    SDL_SetRenderDrawColor(r,51,46,67,255);
    SDL_Rect title_inset={title_box.x+3,title_box.y+3,
                          title_box.w-6,title_box.h-6};
    draw_rounded_frame(r,title_inset,7);
    /* Reserved upper-right module bay. Between the top/right window
     * margins and the SYSTEM meter; ends above the lower RAYS panel. */
    SDL_Rect upper_right={rm.x+rm.w+12,12,
                          w-16-(rm.x+rm.w+12),title_box.y-24};
    SDL_Rect upper_left={16,12,
                         lm.x-12-16,title_box.y-24};
    SDL_Rect upper_bays[2]={upper_left,upper_right};
    for(int i=0;i<2;++i){
        SDL_Rect bay=upper_bays[i];
        if(bay.w<70||bay.h<70)continue;
        SDL_SetRenderDrawColor(r,105,93,132,255);
        draw_rounded_frame(r,bay,10);
        SDL_SetRenderDrawColor(r,51,46,67,255);
        SDL_Rect inset={bay.x+3,bay.y+3,bay.w-6,bay.h-6};
        draw_rounded_frame(r,inset,8);
    }
    /* Right-hand auxiliary bay: top flush with NOW PLAYING, bottom flush
     * with the volume-slider bay. Keep all existing panel positions intact. */
    int bay_x=outer.x+outer.w+15;
    int bay_w=w-bay_x-16;
    int bay_h=sliders.y+sliders.h-title_box.y;
    if(bay_w>=65 && bay_h>=55){
        /* Match the side bays exactly; on the standard window both
         * occupy the same amount of space beside the central controls. */
        SDL_Rect bays[2]={
            {outer.x-15-bay_w,title_box.y,bay_w,bay_h},
            {bay_x,title_box.y,bay_w,bay_h}
        };
        for(int i=0;i<2;++i){
            if(bays[i].x<8 || bays[i].x+bays[i].w>w-8)continue;
            SDL_SetRenderDrawColor(r,105,93,132,255);
            draw_rounded_frame(r,bays[i],10);
            SDL_SetRenderDrawColor(r,51,46,67,255);
            SDL_Rect inset={bays[i].x+3,bays[i].y+3,
                            bays[i].w-6,bays[i].h-6};
            draw_rounded_frame(r,inset,8);
            sauti_speaker_backdrop(r,bays[i],i==0?left_bands:right_bands);
            sauti_rays_advance(&rays_motion[i],i==0?left_bands:right_bands,ms);
            if(sauti_visual_style==0)
                sauti_rays(r,bays[i],i==0?left_bands:right_bands,rays_motion[i].angle,ms);
            else sauti_fractal(r,bays[i],i==0?left_bands:right_bands,ms,sauti_visual_style,i);
            sauti_speaker_dust_cap(r,bays[i],i==0?left_bands:right_bands);
        }
    }
    SDL_SetRenderDrawColor(r,32,201,151,255);
    SDL_Rect title_mark={title_box.x+13,title_box.y+13,3,8};
    SDL_RenderFillRect(r,&title_mark);
    button_label(r,title_box.x+106,title_box.y+14,"NOW PLAYING");
    if(playing && *playing){
        char shown[120];
        copy_playing_label(shown,sizeof(shown),playing);
        if(shown[0]){
            /* Clip the scrolling track label to the right half of NOW PLAYING;
             * use the existing centered bitmap lettering. */
            int start=title_box.x+title_box.w/2+4;
            int area=title_box.x+title_box.w-12-start;
            int chars=area/6;
            int count=(int)strlen(shown);
            if(chars>0 && count>0){
                char visible[64];
                int offset=0;
                if(count>chars)
                    offset=(int)((ms/360u)%(Uint32)(count+chars));
                for(int j=0;j<chars && j<63;++j){
                    int k=(offset+j)%(count+chars);
                    visible[j]=k<count?shown[k]:' ';
                    visible[j+1]=0;
                }
                SDL_Rect clip_title={start,title_box.y+6,area,22};
                SDL_RenderSetClipRect(r,&clip_title);
                button_label(r,start+chars*3,title_box.y+14,visible);
                SDL_RenderSetClipRect(r,NULL);
            }
        }
    }
    /* Draw the button frame at its shifted position. */
    if(empty.h>=34){
        SDL_SetRenderDrawColor(r,105,93,132,255);
        draw_rounded_frame(r,empty,10);
        SDL_SetRenderDrawColor(r,51,46,67,255);
        SDL_Rect controls_inset={empty.x+3,empty.y+3,empty.w-6,empty.h-6};
        draw_rounded_frame(r,controls_inset,8);
    }
    paint_control_buttons(r,empty,ms,repeat,shuffle,skip,mic,recording);
    /* Dedicated progress strip. It stays at zero until playback and track
     * duration exist; monitor-only audio has no seekable timeline. */
    SDL_Rect progress={outer.x,sliders.y-6-32,outer.w,32};
    if(progress.y>=empty.y+empty.h+3){
        SDL_SetRenderDrawColor(r,105,93,132,255);
        draw_rounded_frame(r,progress,9);
        SDL_SetRenderDrawColor(r,51,46,67,255);
        SDL_Rect progress_inner={progress.x+3,progress.y+3,
                                  progress.w-6,progress.h-6};
        draw_rounded_frame(r,progress_inner,7);
        int railx=progress.x+20;
        int railw=progress.w-40;
        int raily=progress.y+16;
        SDL_SetRenderDrawColor(r,52,48,68,255);
        SDL_Rect rail={railx,raily-2,railw,5};
        SDL_RenderFillRect(r,&rail);
        SDL_SetRenderDrawColor(r,77,70,105,255);
        SDL_RenderDrawLine(r,railx,raily-2,railx+railw-1,raily-2);
        /* Actual playback progress or user drag preview. */
        int hx=railx+(railw-1)*clamp100(position)/100;
        SDL_SetRenderDrawColor(r,32,201,151,255);
        SDL_Rect filled={railx,raily-2,hx-railx,5};
        if(filled.w>0)SDL_RenderFillRect(r,&filled);
        /* Emerald PLAY triangle as the draggable thumb. */
        SDL_SetRenderDrawColor(r,32,201,151,55);
        SDL_Rect halo={hx-12,raily-13,24,26};
        SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(r,&halo);
        SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r,17,32,34,255);
        SDL_Rect thumb={hx-10,raily-10,20,20};
        SDL_RenderFillRect(r,&thumb);
        SDL_SetRenderDrawColor(r,32,201,151,255);
        draw_rounded_frame(r,thumb,5);
        for(int j=0;j<11;++j) {
            int span=5-(j>5?j-5:5-j);
            SDL_RenderDrawLine(r,hx-3,raily-5+j,hx-3+span,raily-5+j);
        }
    }
    side_meter(r,lm,gain,ms);
    side_meter(r,rm,vol,ms);
    small_label(r,lm.x-7,lm.y+lm.h+8,"SAUTI");
    small_label(r,rm.x-9,rm.y+rm.h+8,"SYSTEM");
    SDL_Rect left={sliders.x+11,sliders.y+8,sliders.w/2-19,sliders.h-16};
    SDL_Rect right={sliders.x+sliders.w/2+8,sliders.y+8,sliders.w/2-19,sliders.h-16};
    slider_paint(r,left,"SAUTI",gain,ms);
    slider_paint(r,right,"SYSTEM",vol,ms);
}
static int track_progress_position(int x,int y,int w,int h){
    if(w<650||h<550)return -1;
    SDL_Rect outer,buttons,sliders,lm,rm;
    panel_geometry(w,h,&outer,&buttons,&sliders,&lm,&rm);
    SDL_Rect progress={outer.x,sliders.y-38,outer.w,32};
    if(y<progress.y||y>=progress.y+progress.h)return -1;
    int start=progress.x+20, length=progress.w-41;
    if(length<=0)return -1;
    return clamp100((x-start)*100/length);
}
static int slider_position(int x,int y,int w,int h,int *side){
    if(w<650||h<550)return -1;
    SDL_Rect outer,empty,sliders,lm,rm;
    panel_geometry(w,h,&outer,&empty,&sliders,&lm,&rm);
    if(sliders.h<40||y<sliders.y||y>=sliders.y+sliders.h)return -1;
    *side=x<sliders.x+sliders.w/2?0:1;
    int boxx=sliders.x+(*side?sliders.w/2+8:11);
    int boxw=sliders.w/2-19;
    int start=boxx+12,width=boxw-24;
    if(width<10||x<start-8||x>start+width+8)return -1;
    return clamp100((x-start)*100/width);
}
#ifdef SAUTI_HAS_PIPEWIRE
static int read_output_volume(int fallback){
    FILE *f=popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null","r");
    if(!f)return fallback;
    char line[96];
    double v=0;
    int result=fallback;
    if(fgets(line,sizeof(line),f)&&sscanf(line,"Volume: %lf",&v)==1)
        result=clamp100((int)(v*100+0.5));
    (void)pclose(f);
    return result;
}
static void write_output_volume(int value){
    char cmd[120];
    snprintf(cmd,sizeof(cmd),"wpctl set-volume @DEFAULT_AUDIO_SINK@ %d%%",clamp100(value));
    if (system(cmd) != 0) {
        fprintf(stderr,"Sauti: wpctl could not set the system output volume\n");
    }
}
#endif


/* Separate, read-only Sauti disk browser. No external dialog dependency.
 * Navigation: double click a directory; BACKSPACE goes to parent; wheel scrolls.
 * Clicking a file selects it, but never executes or modifies it. */
enum { BROWSER_ITEMS=384, BROWSER_ROW=25 };
typedef struct {
    char name[256];
    int directory;
} BrowserEntry;
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    char path[PATH_MAX];
    char default_path[PATH_MAX];
    char selected_track[PATH_MAX];
    int default_changed;
    char queue[24][PATH_MAX];
    int queue_count;
    SDL_Window *save_window;
    SDL_Renderer *save_renderer;
    char save_name[80];
    char saved_names[40][80];
    int saved_count;
    char active_playlist[80];
    int queue_selected;
    int list_scroll[2];
    BrowserEntry entries[BROWSER_ITEMS];
    int count,scroll,selected;
    Uint32 last_click;
    int last_index;
} SautiBrowser;
static int browser_order(const void *ap,const void *bp) {
    const BrowserEntry *a=ap,*b=bp;
    if(a->directory!=b->directory)return b->directory-a->directory;
    return strcasecmp(a->name,b->name);
}
static void browser_scan(SautiBrowser *b) {
    b->count=0;b->scroll=0;b->selected=-1;b->last_index=-1;
    DIR *dir=opendir(b->path);
    if(!dir)return;
    struct dirent *ent;
    while((ent=readdir(dir))!=NULL && b->count<BROWSER_ITEMS){
        if(!strcmp(ent->d_name,".")||!strcmp(ent->d_name,".."))continue;
        BrowserEntry *entry=&b->entries[b->count];
        int n=snprintf(entry->name,sizeof(entry->name),"%s",ent->d_name);
        if(n<0||(size_t)n>=sizeof(entry->name))continue;
        char full[PATH_MAX];
        n=snprintf(full,sizeof(full),"%s/%s",b->path,ent->d_name);
        if(n<0||(size_t)n>=sizeof(full))continue;
        struct stat st;
        if(stat(full,&st)!=0)continue;
        entry->directory=S_ISDIR(st.st_mode);
        ++b->count;
    }
    closedir(dir);
    qsort(b->entries,(size_t)b->count,sizeof(b->entries[0]),browser_order);
    /* Artificial parent entry always comes first, never in filesystem root. */
    if(strcmp(b->path,"/")!=0 && b->count<BROWSER_ITEMS){
        memmove(&b->entries[1],&b->entries[0],
                (size_t)b->count*sizeof(b->entries[0]));
        snprintf(b->entries[0].name,sizeof(b->entries[0].name),"..");
        b->entries[0].directory=1;
        ++b->count;
    }
}
static void browser_parent(SautiBrowser *b) {
    char *slash=strrchr(b->path,'/');
    if(!slash)return;
    if(slash==b->path) b->path[1]='\0';
    else *slash='\0';
    browser_scan(b);
}
static void browser_enter(SautiBrowser *b,int idx) {
    if(idx<0||idx>=b->count||!b->entries[idx].directory)return;
    if(strcmp(b->entries[idx].name,"..")==0){browser_parent(b);return;}
    char next[PATH_MAX];
    int n=snprintf(next,sizeof(next),"%s%s%s",b->path,
        !strcmp(b->path,"/")?"":"/",b->entries[idx].name);
    if(n<0||(size_t)n>=sizeof(next))return;
    memcpy(b->path,next,(size_t)n+1);
    browser_scan(b);
}
static void browser_close(SautiBrowser *b) {
    if(b->renderer)SDL_DestroyRenderer(b->renderer);
    if(b->window)SDL_DestroyWindow(b->window);
    b->window=NULL;b->renderer=NULL;
}
static int browser_is_audio_file(const char *name);
/* Keep the user's preferred starting directory across Sauti launches. */
static void browser_config_path(char *out,size_t cap){
    const char *xdg=getenv("XDG_CONFIG_HOME");
    const char *home=getenv("HOME");
    if(xdg && *xdg)snprintf(out,cap,"%s/sayari-sauti",xdg);
    else snprintf(out,cap,"%s/.config/sayari-sauti",home&&*home?home:"/tmp");
}
/* Shared with Qt preferences: one unmodified SDL key name per action.
 * Reload on keypress so preference changes apply while the player is running. */
static int sauti_shortcut_action(SDL_Keycode key){
    static const char *ids[]={"play_stop","previous","next","pause","mute","repeat","shuffle"};
    static const char *defaults[]={"Space","Left","Right","P","M","R","H"};
    char current[7][32];
    for(int i=0;i<7;i++)snprintf(current[i],sizeof(current[i]),"%s",defaults[i]);
    char dir[PATH_MAX],path[PATH_MAX];
    browser_config_path(dir,sizeof(dir));
    int n=snprintf(path,sizeof(path),"%s/shortcuts.ini",dir);
    if(n>0&&(size_t)n<sizeof(path)){
        FILE *f=fopen(path,"r");
        if(f){
            char line[160];
            while(fgets(line,sizeof(line),f)){
                char *eq=strchr(line,'=');
                if(!eq)continue;
                *eq=0;
                char *value=eq+1;
                value[strcspn(value,"\r\n")]=0;
                for(int i=0;i<7;i++)if(strcmp(line,ids[i])==0&&*value){
                    SDL_Keycode candidate=SDL_GetKeyFromName(value);
                    if(candidate!=SDLK_UNKNOWN)
                        snprintf(current[i],sizeof(current[i]),"%s",value);
                }
            }
            fclose(f);
        }
    }
    for(int i=0;i<7;i++)
        if(SDL_GetKeyFromName(current[i])==key)return i;
    return -1;
}
static void browser_save_default(SautiBrowser *b){
    char dir[PATH_MAX],file[PATH_MAX];
    browser_config_path(dir,sizeof(dir));
    if(mkdir(dir,0700)!=0 && errno!=EEXIST)return;
    int n=snprintf(file,sizeof(file),"%s/default-folder",dir);
    if(n<0||(size_t)n>=sizeof(file))return;
    FILE *fp=fopen(file,"w");
    if(!fp)return;
    fprintf(fp,"%s\n",b->path);
    fclose(fp);
    snprintf(b->default_path,sizeof(b->default_path),"%s",b->path);
    b->default_changed=1;
}
static void browser_load_default(SautiBrowser *b){
    const char *home=getenv("HOME");
    snprintf(b->default_path,sizeof(b->default_path),"%s",
             home&&*home?home:"/");
    char dir[PATH_MAX],file[PATH_MAX];
    browser_config_path(dir,sizeof(dir));
    int n=snprintf(file,sizeof(file),"%s/default-folder",dir);
    if(n<0||(size_t)n>=sizeof(file))return;
    FILE *fp=fopen(file,"r");
    if(!fp)return;
    char candidate[PATH_MAX];
    if(fgets(candidate,sizeof(candidate),fp)){
        candidate[strcspn(candidate,"\r\n")]=0;
        struct stat st;
        if(candidate[0]=='/' && stat(candidate,&st)==0 && S_ISDIR(st.st_mode))
            snprintf(b->default_path,sizeof(b->default_path),"%s",candidate);
    }
    fclose(fp);
}
static int browser_confirm_track(SautiBrowser *b){
    if(b->selected<0||b->selected>=b->count)return 0;
    BrowserEntry *entry=&b->entries[b->selected];
    if(entry->directory||!browser_is_audio_file(entry->name))return 0;
    int n=snprintf(b->selected_track,sizeof(b->selected_track),"%s%s%s",
        b->path,strcmp(b->path,"/")==0?"":"/",entry->name);
    if(n<0||(size_t)n>=sizeof(b->selected_track))return 0;
    struct stat st;
    if(stat(b->selected_track,&st)!=0 || !S_ISREG(st.st_mode))return 0;
    fprintf(stderr,"Sauti: wybrano utwor: %s\n",
            b->selected_track);
    return 1;
}
static int browser_can_add(const SautiBrowser *b){
    if(b->queue_count>=24||b->selected<0||b->selected>=b->count)return 0;
    const BrowserEntry *e=&b->entries[b->selected];
    if(e->directory||!browser_is_audio_file(e->name))return 0;
    char path[PATH_MAX];
    int n=snprintf(path,sizeof(path),"%s%s%s",b->path,
                   strcmp(b->path,"/")==0?"":"/",e->name);
    if(n<0||(size_t)n>=sizeof(path))return 0;
    struct stat st;
    return stat(path,&st)==0&&S_ISREG(st.st_mode);
}
static void browser_add(SautiBrowser *b){
    if(!browser_can_add(b))return;
    if(b->selected<0||b->selected>=b->count)return;
    BrowserEntry *entry=&b->entries[b->selected];
    if(entry->directory||!browser_is_audio_file(entry->name))return;
    char path[PATH_MAX];
    int n=snprintf(path,sizeof(path),"%s%s%s",b->path,
                   strcmp(b->path,"/")==0?"":"/",entry->name);
    if(n<0||(size_t)n>=sizeof(path))return;
    struct stat st;
    if(stat(path,&st)!=0||!S_ISREG(st.st_mode))return;
    (void)sauti_queue_add(&b->queue[0][0],PATH_MAX,&b->queue_count,24,path);
}
/* SAVE writes the pending ADD queue as a simple UTF-8 list of paths.
 * Do not alter the currently playing file or the in-memory queue. */
static void browser_text(SDL_Renderer *r,int x,int y,const char *str,int max_chars);
static void browser_refresh_playlists(SautiBrowser *b){
    b->saved_count=0;
    char dir[PATH_MAX],path[PATH_MAX];
    browser_config_path(dir,sizeof(dir));
    if(snprintf(path,sizeof(path),"%s/playlists",dir)>=(int)sizeof(path))return;
    DIR *d=opendir(path);
    if(!d)return;
    struct dirent *ent;
    while((ent=readdir(d))!=NULL && b->saved_count<40){
        char playlist_name[80];
        if(!sauti_library_name(ent->d_name,playlist_name,sizeof(playlist_name)))continue;
        char full[PATH_MAX];
        if(snprintf(full,sizeof(full),"%s/%s",path,ent->d_name)>=(int)sizeof(full))continue;
        struct stat st;
        if(stat(full,&st)!=0||!S_ISREG(st.st_mode))continue;
        snprintf(b->saved_names[b->saved_count++],80,"%s",playlist_name);
    }
    closedir(d);
}
static int browser_valid_playlist_track(const char *path,void *ctx){
    (void)ctx;
    struct stat st;
    return stat(path,&st)==0 && S_ISREG(st.st_mode);
}
static void browser_load_playlist(SautiBrowser *b,int index){
    if(index<0||index>=b->saved_count)return;
    char dir[PATH_MAX],file[PATH_MAX];
    browser_config_path(dir,sizeof(dir));
    if(snprintf(file,sizeof(file),"%s/playlists/%s.m3u",dir,b->saved_names[index])>=(int)sizeof(file))return;
    FILE *fp=fopen(file,"r");
    if(!fp){perror("Sauti playlist");return;}
    b->queue_count=0;
    b->queue_selected=-1;
    b->list_scroll[1]=0;
    snprintf(b->active_playlist,sizeof(b->active_playlist),"%s",b->saved_names[index]);
    (void)sauti_playlist_read(fp,&b->queue[0][0],PATH_MAX,
        &b->queue_count,24,browser_valid_playlist_track,NULL);
    fclose(fp);
    /* Persist the chosen playlist for the next Sauti launch. */
    char config[PATH_MAX],last[PATH_MAX];
    browser_config_path(config,sizeof(config));
    if(snprintf(last,sizeof(last),"%s/last-playlist",config)<(int)sizeof(last)){
        FILE *out=fopen(last,"w");
        if(out){
            fprintf(out,"%s\n",b->active_playlist);
            fclose(out);
        }
    }
}
static void browser_restore_playlist(SautiBrowser *b){
    browser_refresh_playlists(b);
    char config[PATH_MAX],last[PATH_MAX],name[80];
    browser_config_path(config,sizeof(config));
    if(snprintf(last,sizeof(last),"%s/last-playlist",config)>=(int)sizeof(last))return;
    FILE *fp=fopen(last,"r");
    if(!fp)return;
    if(!fgets(name,sizeof(name),fp)){fclose(fp);return;}
    fclose(fp);
    name[strcspn(name,"\r\n")]=0;
    int index=sauti_library_find(&b->saved_names[0][0],80,b->saved_count,name);
    if(index>=0)browser_load_playlist(b,index);
}
static void browser_save_close(SautiBrowser *b){
    if(b->save_renderer)SDL_DestroyRenderer(b->save_renderer);
    if(b->save_window)SDL_DestroyWindow(b->save_window);
    b->save_renderer=NULL;b->save_window=NULL;
    SDL_StopTextInput();
}
static void browser_save_open(SautiBrowser *b){
    if(b->save_window){SDL_RaiseWindow(b->save_window);return;}
    b->save_name[0]=0;
    b->save_window=SDL_CreateWindow("Sauti - Save Playlist",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,390,165,0);
    if(!b->save_window)return;
    b->save_renderer=SDL_CreateRenderer(b->save_window,-1,SDL_RENDERER_SOFTWARE);
    if(!b->save_renderer){browser_save_close(b);return;}
    SDL_RaiseWindow(b->save_window);
    SDL_SetWindowInputFocus(b->save_window);
    SDL_StartTextInput();
}
static int browser_save_queue(SautiBrowser *b){
    if(!b->save_name[0]||b->queue_count==0)return 0;
    char dir[PATH_MAX],folder[PATH_MAX],file[PATH_MAX],temporary[PATH_MAX];
    browser_config_path(dir,sizeof(dir));
    if(mkdir(dir,0700)!=0&&errno!=EEXIST)return 0;
    if(snprintf(folder,sizeof(folder),"%s/playlists",dir)>=(int)sizeof(folder))return 0;
    if(mkdir(folder,0700)!=0&&errno!=EEXIST)return 0;
    if(snprintf(file,sizeof(file),"%s/%s.m3u",folder,b->save_name)>=(int)sizeof(file))return 0;
    if(snprintf(temporary,sizeof(temporary),"%s/.%s.tmp",folder,b->save_name)>=(int)sizeof(temporary))return 0;
    /* Never silently overwrite an existing named playlist. */
    if(access(file,F_OK)==0){fprintf(stderr,"Sauti: playlist already exists: %s\n",file);return 0;}
    FILE *fp=fopen(temporary,"w");
    if(!fp)return 0;
    int ok=sauti_playlist_write(fp,&b->queue[0][0],PATH_MAX,b->queue_count);
    if(fclose(fp)!=0)ok=0;
    if(ok&&rename(temporary,file)==0){
        browser_refresh_playlists(b);
        fprintf(stderr,"Sauti: playlist saved: %s\n",file);
        return 1;
    }
    (void)remove(temporary);
    return 0;
}
static void browser_save_paint(SautiBrowser *b){
    if(!b->save_renderer)return;
    SDL_Renderer *r=b->save_renderer;
    SDL_SetRenderDrawColor(r,19,18,27,255);SDL_RenderClear(r);
    SDL_SetRenderDrawColor(r,173,153,214,255);
    browser_text(r,24,23,"PLAYLIST NAME",20);
    SDL_Rect field={20,53,350,42};
    SDL_SetRenderDrawColor(r,109,95,137,255);draw_rounded_frame(r,field,7);
    SDL_SetRenderDrawColor(r,35,31,49,255);
    SDL_Rect entry={field.x+2,field.y+2,field.w-4,field.h-4};
    SDL_RenderFillRect(r,&entry);
    SDL_SetRenderDrawColor(r,229,224,246,255);
    browser_text(r,30,69,b->save_name,36);
    if(SDL_GetTicks()%1000<550){
        int cursor_x=30+(int)strlen(b->save_name)*8;
        if(cursor_x>358)cursor_x=358;
        SDL_RenderDrawLine(r,cursor_x,66,cursor_x,85);
    }
    SDL_Rect cancel={25,113,157,34},ok={208,113,157,34};
    int mx=-1,my=-1;
    if(SDL_GetMouseFocus()==b->save_window)SDL_GetMouseState(&mx,&my);
    int hc=mx>=cancel.x&&mx<cancel.x+cancel.w&&my>=cancel.y&&my<cancel.y+cancel.h;
    int ho=mx>=ok.x&&mx<ok.x+ok.w&&my>=ok.y&&my<ok.y+ok.h;
    SDL_SetRenderDrawColor(r,hc?156:92,hc?143:82,hc?187:115,255);
    SDL_RenderFillRect(r,&cancel);
    draw_rounded_frame(r,cancel,6);
    SDL_SetRenderDrawColor(r,ho?50:32,ho?232:201,ho?183:151,255);
    SDL_RenderFillRect(r,&ok);
    draw_rounded_frame(r,ok,6);
    SDL_SetRenderDrawColor(r,238,236,251,255);
    browser_text(r,75,125,"CANCEL",12);
    SDL_SetRenderDrawColor(r,13,34,31,255);
    browser_text(r,277,125,"OK",12);
    SDL_RenderPresent(r);
}
static void browser_save_event(SautiBrowser *b,const SDL_Event *e){
    if(e->type==SDL_WINDOWEVENT&&e->window.event==SDL_WINDOWEVENT_CLOSE){browser_save_close(b);return;}
    if(e->type==SDL_TEXTINPUT){
        (void)sauti_library_append_name(b->save_name,sizeof(b->save_name),e->text.text);
    }
    if(e->type==SDL_KEYDOWN){
        if(e->key.keysym.sym==SDLK_ESCAPE){browser_save_close(b);return;}
        if(e->key.keysym.sym==SDLK_BACKSPACE){
            sauti_library_backspace(b->save_name);
        }
        if(e->key.keysym.sym==SDLK_RETURN&&browser_save_queue(b))browser_save_close(b);
    }
    if(e->type==SDL_MOUSEBUTTONDOWN&&e->button.button==SDL_BUTTON_LEFT&&
       e->button.y>=113&&e->button.y<147){
        if(e->button.x>=25&&e->button.x<182)browser_save_close(b);
        else if(e->button.x>=208&&e->button.x<365&&browser_save_queue(b))
            browser_save_close(b);
    }
}
/* Monochrome-alpha line-art of a tall speaker, used independently by
 * PLAYLISTS (left) and QUEUE (right). Drawn before list text. */
/* Asset loader for our fixed-size RGBA PNG speaker masks. The PNGs use
 * uncompressed DEFLATE blocks and filter 0; no SDL2_image dependency. */
static uint32_t sauti_png_u32(const unsigned char *p){
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|(uint32_t)p[3];
}
static SDL_Texture *sauti_speaker_image(SDL_Renderer *r,int side,int light){
    static SDL_Texture *cache[4]={0};
    static SDL_Renderer *owner=NULL;
    static unsigned char attempted[4]={0};
    if(owner!=r){for(int i=0;i<4;++i){if(cache[i])SDL_DestroyTexture(cache[i]);cache[i]=NULL;attempted[i]=0;}owner=r;}
    int idx=side*2+light;
    if(attempted[idx])return cache[idx];
    attempted[idx]=1;
    char name[160];
    snprintf(name,sizeof(name),"assets/speaker_%s_%s.png",
             side?"right":"left",light?"dark":"light");
    FILE *fp=fopen(name,"rb");
    if(!fp){
        char alt[192];
        snprintf(alt,sizeof(alt),"apps/sayari-sauti/%s",name);
        fp=fopen(alt,"rb");
    }
    if(!fp)return NULL;
    if(fseek(fp,0,SEEK_END)!=0){fclose(fp);return NULL;}
    long size=ftell(fp);
    if(size<100||size>500000){fclose(fp);return NULL;}
    rewind(fp);
    unsigned char *src=malloc((size_t)size);
    if(!src){fclose(fp);return NULL;}
    if(fread(src,1,(size_t)size,fp)!=(size_t)size){free(src);fclose(fp);return NULL;}
    fclose(fp);
    const unsigned char sig[8]={137,80,78,71,13,10,26,10};
    if(memcmp(src,sig,8)!=0){free(src);return NULL;}
    size_t off=8,zpos=0,zlen=0;
    const unsigned char *compressed=NULL;
    int w=0,h=0,valid=1;
    while(off+12<=(size_t)size){
        uint32_t n=sauti_png_u32(src+off);
        if(n>(size_t)size-off-12){valid=0;break;}
        unsigned char *p=src+off+8;
        if(memcmp(src+off+4,"IHDR",4)==0){
            if(n<13){valid=0;break;}
            w=(int)sauti_png_u32(p);h=(int)sauti_png_u32(p+4);
            if(w!=96||h!=240||p[8]!=8||p[9]!=6){valid=0;break;}
        }else if(memcmp(src+off+4,"IDAT",4)==0){
            if(compressed){valid=0;break;}
            compressed=p;zlen=n;
        }
        off+=(size_t)n+12;
    }
    const size_t row=1u+96u*4u,total=240u*row;
    unsigned char *raw=malloc(total);
    if(!valid||!compressed||zlen<8||!raw){free(src);free(raw);return NULL;}
    /* zlib header, followed by byte-aligned stored blocks (BTYPE=00). */
    zpos=2;size_t decoded=0;int finished=0;
    while(zpos+5<=zlen&&!finished){
        unsigned char flags=compressed[zpos++];
        if((flags&6u)!=0){valid=0;break;}
        finished=(flags&1u)!=0;
        unsigned n=(unsigned)compressed[zpos]|((unsigned)compressed[zpos+1]<<8);
        unsigned inv=(unsigned)compressed[zpos+2]|((unsigned)compressed[zpos+3]<<8);
        zpos+=4;
        if(((n^inv)&65535u)!=65535u||n>total-decoded||n>zlen-zpos){valid=0;break;}
        memcpy(raw+decoded,compressed+zpos,n);
        decoded+=n;zpos+=n;
    }
    if(!valid||!finished||decoded!=total){free(raw);free(src);return NULL;}
    uint32_t *pixels=malloc(96u*240u*sizeof(*pixels));
    if(!pixels){free(raw);free(src);return NULL;}
    for(int y=0;y<240;++y){
        const unsigned char *p=raw+(size_t)y*row;
        if(*p++!=0){valid=0;break;}
        for(int x=0;x<96;++x){
            pixels[y*96+x]=((uint32_t)p[3]<<24)|((uint32_t)p[0]<<16)|
                            ((uint32_t)p[1]<<8)|(uint32_t)p[2];
            p+=4;
        }
    }
    if(valid){
        cache[idx]=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STATIC,96,240);
        if(cache[idx]){
            SDL_UpdateTexture(cache[idx],NULL,pixels,96*4);
            SDL_SetTextureBlendMode(cache[idx],SDL_BLENDMODE_BLEND);
        }
    }
    free(pixels);free(raw);free(src);
    return cache[idx];
}
static void sauti_upper_speaker_sketch(SDL_Renderer *r,SDL_Rect rect,int right){
    if(rect.w<72||rect.h<140)return;
    SDL_BlendMode previous;
    SDL_GetRenderDrawBlendMode(r,&previous);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    const int light=sauti_is_light_bg(sauti_theme_bg);
    /* Dark line art on light themes, pale line art on dark themes. */
    const Uint8 shade=light?30:167, alpha=light?65:29;
    SDL_SetRenderDrawColor(r,shade,shade,light?48:199,alpha);
    SDL_Rect shell={rect.x+7,rect.y+5,rect.w-14,rect.h-10};
    SDL_RenderDrawRect(r,&shell);
    SDL_Rect inner={shell.x+4,shell.y+4,shell.w-8,shell.h-8};
    SDL_RenderDrawRect(r,&inner);
    int cx=rect.x+rect.w/2;
    /* Place drivers in the lower 80 percent, leaving playlist text readable. */
    int top=rect.y+rect.h*47/100;
    int rad=rect.w/2-15;
    if(rad>rect.h/6)rad=rect.h/6;
    if(rad<11)rad=11;
    for(int d=0;d<3;++d){
        int cy=top+d*(rect.h/5);
        int rr=d==0?rad/2:(d==1?rad*3/4:rad);
        SDL_SetRenderDrawColor(r,shade,shade,light?48:199,(Uint8)(alpha+(d==2?15:0)));
        sauti_speaker_circle(r,cx,cy,rr);
        if(rr>6)sauti_speaker_circle(r,cx,cy,rr-4);
        if(rr>13)sauti_speaker_circle(r,cx,cy,rr/3);
    }
    SDL_SetRenderDrawColor(r,shade,shade,light?48:199,alpha);
    for(int i=0;i<4;++i){
        int sx= i%2?inner.x+inner.w-5:inner.x+5;
        int sy= i/2?inner.y+inner.h-5:inner.y+5;
        SDL_RenderDrawPoint(r,sx,sy);
    }
    (void)right; /* independent sides retain identical low-contrast design */
    SDL_SetRenderDrawBlendMode(r,previous);
}
static void browser_upper_panels(SDL_Renderer *r,int w,int h,SautiBrowser *b,
                                  const char *playing,int active,int unpaused,
                                  const float *left,const float *right){
    if(w<650||h<550)return;
    /* Smooth a small bass/mid energy envelope, not an artificial time pulse. */
    static float envelope=0.f;
    float energy=0.f;
    if(unpaused && left && right){
        for(int j=0;j<8;++j){
            float l=left[j],rr=right[j];
            if(l>0.f)energy+=l;
            if(rr>0.f)energy+=rr;
        }
        energy/=16.f;
        if(energy>1.f)energy=1.f;
    }
    envelope+=((energy>envelope)?0.23f:0.075f)*(energy-envelope);
    if(envelope<0.f)envelope=0.f;
    float pulse=unpaused?envelope:0.f;
    SDL_Rect outer,empty,sliders,lm,rm;
    panel_geometry(w,h,&outer,&empty,&sliders,&lm,&rm);
    SDL_Rect title_box={outer.x,outer.y+outer.h+6,outer.w,34};
    SDL_Rect bays[2]={{16,12,lm.x-28,title_box.y-24},
        {rm.x+rm.w+12,12,w-16-(rm.x+rm.w+12),title_box.y-24}};
    for(int k=0;k<2;++k){
        SDL_Rect box=bays[k];
        if(box.w<70||box.h<70)continue;
        SDL_Rect artwork={box.x+4,box.y+27,box.w-8,box.h-31};
        SDL_Texture *speaker=sauti_speaker_image(r,k,sauti_is_light_bg(sauti_theme_bg));
        if(speaker){
            /* Speaker graphics are watermarks, not foreground controls.
             * Keep playlist and queue text visually dominant. */
            SDL_SetTextureAlphaMod(speaker,90);
            SDL_RenderCopy(r,speaker,NULL,&artwork);
        }
        else sauti_upper_speaker_sketch(r,artwork,k==1);
        button_label(r,box.x+box.w/2,box.y+15,k==0?"PLAYLISTS":"QUEUE");
        int show_current=k==1&&playing&&playing[0]&&active;
        int count=k==0?b->saved_count:b->queue_count+show_current;
        int rows=(box.h-36)/22;
        if(rows<1)continue;
        int max_scroll=count>rows?count-rows:0;
        if(b->list_scroll[k]>max_scroll)b->list_scroll[k]=max_scroll;
        int visible=count-b->list_scroll[k];
        if(visible>rows)visible=rows;
        for(int row=0;row<visible;++row){
            int i=row+b->list_scroll[k];
            int current=k==1&&show_current&&i==0;
            int index=k==1?i-show_current:i;
            const char *name=current?playing:
                (k==0?b->saved_names[i]:b->queue[index]);
            if(k==1){
                const char *slash=strrchr(name,'/');
                if(slash)name=slash+1;
            }
            int active_playlist=k==0&&b->active_playlist[0]&&
                strcmp(b->saved_names[i],b->active_playlist)==0;
            int selected_queue=k==1&&!current&&index==b->queue_selected;
            if(current||active_playlist||selected_queue){
                SDL_Rect selected={box.x+6,box.y+29+row*22,box.w-12,20};
                float glow=(current||(active_playlist&&active))?pulse:0.f;
                SDL_SetRenderDrawColor(r,(Uint8)(29+glow*14.f),
                    (Uint8)(82+glow*38.f),(Uint8)(72+glow*31.f),255);
                SDL_RenderFillRect(r,&selected);
                SDL_SetRenderDrawColor(r,(Uint8)(45+glow*19.f),
                    (Uint8)(227+glow*20.f),(Uint8)(176+glow*23.f),255);
                SDL_RenderDrawLine(r,selected.x,selected.y,
                                   selected.x,selected.y+selected.h-1);
            }
            int highlighted=current||(k==0 && b->active_playlist[0] &&
                strcmp(b->saved_names[i],b->active_playlist)==0)||
                (k==1&&!current&&index==b->queue_selected);
            if(sauti_is_light_bg(sauti_theme_bg) && !highlighted)
                SDL_SetRenderDrawColor(r,29,40,51,255);
            else SDL_SetRenderDrawColor(r,highlighted?235:130,
                                         highlighted?255:196,highlighted?245:197,255);
            browser_text(r,box.x+12,box.y+34+row*22,name,(box.w-18)/8);
        }
    }
}
/* Returns queue index on double click, -1 otherwise.
 * Right click removes a pending entry, never the playing track. */
static int browser_upper_click(SautiBrowser *b,int x,int y,int w,int h,
                               Uint8 button,Uint8 clicks,int playing){
    SDL_Rect outer,empty,sliders,lm,rm;
    panel_geometry(w,h,&outer,&empty,&sliders,&lm,&rm);
    SDL_Rect title_box={outer.x,outer.y+outer.h+6,outer.w,34};
    SDL_Rect bays[2]={{16,12,lm.x-28,title_box.y-24},
        {rm.x+rm.w+12,12,w-16-(rm.x+rm.w+12),title_box.y-24}};
    for(int k=0;k<2;++k){
        SDL_Rect box=bays[k];
        if(x<box.x||x>=box.x+box.w||y<box.y+29||y>=box.y+box.h)continue;
        int row=(y-box.y-29)/22;
        int i=row+b->list_scroll[k];
        if(k==0){
            if(button==SDL_BUTTON_LEFT&&i>=0&&i<b->saved_count)
                browser_load_playlist(b,i);
            return -1;
        }
        int index=i-(playing?1:0);
        if(index<0||index>=b->queue_count)return -1;
        if(button==SDL_BUTTON_RIGHT){
            (void)sauti_queue_remove(&b->queue[0][0],PATH_MAX,
                                     &b->queue_count,index);
            b->queue_selected=-1;
            return -1;
        }
        if(button==SDL_BUTTON_LEFT){
            b->queue_selected=index;
            if(clicks>=2)return index;
        }
        return -1;
    }
    return -1;
}
static void browser_upper_wheel(SautiBrowser *b,int x,int y,int w,int h,int delta,int playing){
    SDL_Rect outer,empty,sliders,lm,rm;
    panel_geometry(w,h,&outer,&empty,&sliders,&lm,&rm);
    SDL_Rect title_box={outer.x,outer.y+outer.h+6,outer.w,34};
    SDL_Rect bays[2]={{16,12,lm.x-28,title_box.y-24},
        {rm.x+rm.w+12,12,w-16-(rm.x+rm.w+12),title_box.y-24}};
    for(int k=0;k<2;++k){
        SDL_Rect box=bays[k];
        if(x<box.x||x>=box.x+box.w||y<box.y||y>=box.y+box.h)continue;
        int rows=(box.h-36)/22;
        if(rows<1)return;
        int count=k==0?b->saved_count:b->queue_count+playing;
        int max_scroll=count>rows?count-rows:0;
        int scroll=b->list_scroll[k]-delta;
        if(scroll<0)scroll=0;
        if(scroll>max_scroll)scroll=max_scroll;
        b->list_scroll[k]=scroll;
        return;
    }
}
static void browser_open(SautiBrowser *b) {
    browser_refresh_playlists(b);
    if(b->window){SDL_RaiseWindow(b->window);return;}
    browser_load_default(b);
    snprintf(b->path,sizeof(b->path),"%s",b->default_path);
    b->default_changed=0;
    browser_scan(b);
    b->window=SDL_CreateWindow("Sauti - przegladarka dysku",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,700,530,
        SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    if(!b->window)return;
    b->renderer=SDL_CreateRenderer(b->window,-1,SDL_RENDERER_SOFTWARE);
    if(!b->renderer)browser_close(b);
}
static void browser_text(SDL_Renderer *r,int x,int y,const char *str,int max_chars){
    /* Same minimalist pixel alphabet as panel labels; unsupported glyphs
     * are indicated by a dot. Full UTF-8 filename stays in window title. */
    static const unsigned char letter[26][7]={
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    static const unsigned char digit[10][7]={
        {14,19,21,21,25,17,14},{4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
    };
    for(int k=0;str[k]&&k<max_chars;++k,x+=8){
        unsigned char ch=(unsigned char)str[k];
        const unsigned char *g=NULL;
        if(ch>='a'&&ch<='z')g=letter[ch-'a'];
        else if(ch>='A'&&ch<='Z')g=letter[ch-'A'];
        else if(ch>='0'&&ch<='9')g=digit[ch-'0'];
        if(g) {
            for(int row=0;row<7;++row) {
                for(int col=0;col<5;++col) {
                    if(g[row]&(16>>col)) {
                        SDL_Rect pixel={x+col,y+row,1,1};
                        SDL_RenderFillRect(r,&pixel);
                    }
                }
            }
        } else if(ch!=' ') {
            SDL_RenderDrawPoint(r,x+2,y+6);
        }
    }
}
/* Geometry shared by mouse hit testing and painting. */
enum { BROWSER_LIST_TOP=132 };
/* Neon folder and document pictograms instead of diagnostic DIR/FILE labels. */
static int browser_is_audio_file(const char *name){
    const char *ext=strrchr(name,'.');
    if(!ext||!ext[1])return 0;
    ++ext;
    /* A file extension is only a candidate; the decoder registry verifies actual support.
     * Keep the list focused on formats we intend to support. */
    static const char *audio[]={
        "mid","midi",
        "mp3","wav","flac","ogg","oga","opus","m4a","aac","wv","avr","dvs","ym","dtm","sndh","snd"
    };
    for(size_t i=0;i<sizeof(audio)/sizeof(audio[0]);++i)
        if(strcasecmp(ext,audio[i])==0)return 1;
    return 0;
}
/* Two softly glowing, rounded musical notes in the spectrum palette.
 * Left ruby head is lower; right emerald head is higher. */
static void browser_audio_icon(SDL_Renderer *r,int x,int y,int selected){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,231,61,101,selected?62:34);
    SDL_Rect ruby_halo={x-3,y+4,14,13};
    SDL_RenderFillRect(r,&ruby_halo);
    SDL_SetRenderDrawColor(r,32,201,151,selected?67:38);
    SDL_Rect green_halo={x+6,y-3,14,14};
    SDL_RenderFillRect(r,&green_halo);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
    /* Rounded ruby note head and slender upward stem. */
    SDL_SetRenderDrawColor(r,231,61,101,255);
    SDL_RenderDrawLine(r,x+2,y+10,x+5,y+10);
    SDL_RenderDrawLine(r,x+1,y+11,x+6,y+11);
    SDL_RenderDrawLine(r,x+2,y+12,x+5,y+12);
    SDL_RenderDrawLine(r,x+6,y+3,x+6,y+10);
    SDL_RenderDrawLine(r,x+7,y+3,x+7,y+9);
    /* Emerald note a few pixels higher. */
    SDL_SetRenderDrawColor(r,32,201,151,255);
    SDL_RenderDrawLine(r,x+10,y+6,x+13,y+6);
    SDL_RenderDrawLine(r,x+9,y+7,x+14,y+7);
    SDL_RenderDrawLine(r,x+10,y+8,x+13,y+8);
    SDL_RenderDrawLine(r,x+14,y,x+14,y+6);
    SDL_RenderDrawLine(r,x+15,y,x+15,y+5);
    /* Fine highlights resemble the bright centers of Sauti LEDs. */
    SDL_SetRenderDrawColor(r,255,157,185,255);
    SDL_RenderDrawLine(r,x+3,y+10,x+5,y+10);
    SDL_SetRenderDrawColor(r,145,255,223,255);
    SDL_RenderDrawLine(r,x+11,y+6,x+13,y+6);
}
static void browser_entry_icon(SDL_Renderer *r,int x,int y,int directory,int audio,int selected){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    if(directory){
        SDL_SetRenderDrawColor(r,32,201,151,selected?65:35);
        SDL_Rect glow={x-3,y-3,23,19};
        SDL_RenderFillRect(r,&glow);
        SDL_SetRenderDrawColor(r,selected?82:32,selected?245:201,selected?195:151,255);
        SDL_Rect tab={x+2,y,7,4},body={x,y+3,17,11};
        SDL_RenderFillRect(r,&tab);
        SDL_RenderFillRect(r,&body);
        SDL_SetRenderDrawColor(r,16,16,22,255);
        SDL_RenderDrawPoint(r,x,y+3);
        SDL_RenderDrawPoint(r,x+16,y+3);
        SDL_RenderDrawPoint(r,x,y+13);
        SDL_RenderDrawPoint(r,x+16,y+13);
        SDL_SetRenderDrawColor(r,142,255,224,255);
        SDL_RenderDrawLine(r,x+2,y+7,x+14,y+7);
    }else if(audio){
        browser_audio_icon(r,x,y,selected);
    }else{
        SDL_SetRenderDrawColor(r,128,154,205,selected?60:28);
        SDL_Rect glow={x-3,y-3,21,20};
        SDL_RenderFillRect(r,&glow);
        SDL_SetRenderDrawColor(r,145,163,213,255);
        SDL_Rect sheet={x+2,y,13,15};
        SDL_RenderFillRect(r,&sheet);
        SDL_SetRenderDrawColor(r,27,27,40,255);
        SDL_Rect inside={x+4,y+3,9,10};
        SDL_RenderFillRect(r,&inside);
        SDL_SetRenderDrawColor(r,145,163,213,255);
        SDL_RenderDrawLine(r,x+5,y+6,x+11,y+6);
        SDL_RenderDrawLine(r,x+5,y+9,x+10,y+9);
    }
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
}
static void browser_paint(SautiBrowser *b) {
    if(!b->window||!b->renderer)return;
    int w=0,h=0;
    SDL_GetRendererOutputSize(b->renderer,&w,&h);
    if(w<240||h<180)return;
    char title[PATH_MAX+80];
    const char *selection=b->selected>=0&&b->selected<b->count?
        b->entries[b->selected].name:NULL;
    snprintf(title,sizeof(title),"Sauti - %s%s%s",b->path,
        selection?"  |  ":"",selection?selection:"");
    SDL_SetWindowTitle(b->window,title);
    SDL_SetRenderDrawColor(b->renderer,16,16,22,255);
    SDL_RenderClear(b->renderer);
    SDL_SetRenderDrawColor(b->renderer,112,96,140,255);
    SDL_Rect outer={12,12,w-24,h-24};
    draw_rounded_frame(b->renderer,outer,9);
    /* Full-width path bar; the ".." folder row replaces the old UP button. */
    SDL_Rect pathbar={23,23,w-46,36};
    SDL_SetRenderDrawColor(b->renderer,34,31,45,255);
    SDL_RenderFillRect(b->renderer,&pathbar);
    SDL_SetRenderDrawColor(b->renderer,103,92,126,255);
    draw_rounded_frame(b->renderer,pathbar,5);
    SDL_SetRenderDrawColor(b->renderer,203,189,224,255);
    browser_text(b->renderer,35,37,b->path,(w-70)/8);
    SDL_SetRenderDrawColor(b->renderer,114,107,143,255);
    browser_text(b->renderer,28,77,"FOLDERS AND FILES",22);
    SDL_SetRenderDrawColor(b->renderer,50,47,66,255);
    SDL_RenderDrawLine(b->renderer,25,113,w-25,113);
    int visible=(h-BROWSER_LIST_TOP-78)/BROWSER_ROW;
    if(visible<1)visible=1;
    /* The artificial '..' parent is pinned to row 0. Only real entries
     * participate in scrolling. Root '/' has no artificial parent. */
    int pinned=(b->count>0 && strcmp(b->entries[0].name,"..")==0);
    int scroll_rows=visible-pinned;
    if(scroll_rows<0)scroll_rows=0;
    int maxscroll=b->count-pinned-scroll_rows;
    if(maxscroll<0)maxscroll=0;
    if(b->scroll>maxscroll)b->scroll=maxscroll;
    for(int i=0;i<visible;++i){
        int idx;
        if(pinned && i==0) idx=0;
        else idx=pinned+(i-pinned)+b->scroll;
        if(idx<0||idx>=b->count)break;
        int y=BROWSER_LIST_TOP+i*BROWSER_ROW;
        SDL_Rect row={25,y,w-50,BROWSER_ROW-2};
        SDL_SetRenderDrawColor(b->renderer,idx==b->selected?43:(i%2?23:28),
            idx==b->selected?61:(i%2?22:26),
            idx==b->selected?73:(i%2?34:39),255);
        SDL_RenderFillRect(b->renderer,&row);
        browser_entry_icon(b->renderer,39,y+5,
            b->entries[idx].directory,
            !b->entries[idx].directory &&
                browser_is_audio_file(b->entries[idx].name),
            idx==b->selected);
        SDL_SetRenderDrawColor(b->renderer,202,191,224,255);
        browser_text(b->renderer,77,y+8,b->entries[idx].name,(w-108)/8);
    }
    /* Footer: DEFAULT / CANCEL / OK, with shared hit-test rectangles. */
    SDL_Rect footer={23,h-67,w-46,43};
    SDL_SetRenderDrawColor(b->renderer,29,27,40,255);
    SDL_RenderFillRect(b->renderer,&footer);
    SDL_SetRenderDrawColor(b->renderer,65,57,86,255);
    SDL_RenderDrawLine(b->renderer,23,h-75,w-23,h-75);
    const char *labels[5]={"DEFAULT","ADD","SAVE","CANCEL","OK"};
    for(int j=0;j<5;++j){
        int span=(w-60)/5;
        SDL_Rect btn={27+j*span,h-63,span-7,32};
        int mx=-1,my=-1;
        if(SDL_GetMouseFocus()==b->window)SDL_GetMouseState(&mx,&my);
        int enabled=j!=1||browser_can_add(b);
        int hover=enabled&&mx>=btn.x&&mx<btn.x+btn.w&&my>=btn.y&&my<btn.y+btn.h;
        SDL_SetRenderDrawColor(b->renderer,hover?58:(j==4?27:36),hover?75:(j==4?67:33),
                               hover?81:(j==4?58:49),255);
        SDL_RenderFillRect(b->renderer,&btn);
        SDL_SetRenderDrawColor(b->renderer,hover?205:(j==4?32:118),
                               hover?189:(j==4?201:104),hover?230:(j==4?151:145),255);
        draw_rounded_frame(b->renderer,btn,6);
        if(!enabled){
            SDL_SetRenderDrawColor(b->renderer,27,26,35,255);
            SDL_RenderFillRect(b->renderer,&btn);
            SDL_SetRenderDrawColor(b->renderer,65,60,77,255);
            draw_rounded_frame(b->renderer,btn,6);
        }
        SDL_SetRenderDrawColor(b->renderer,enabled?218:91,
                               enabled?211:87,enabled?237:103,255);
        browser_text(b->renderer,btn.x+(btn.w-(int)strlen(labels[j])*8)/2,
                     btn.y+13,labels[j],12);
    }
    SDL_RenderPresent(b->renderer);
}
static void browser_event(SautiBrowser *b,const SDL_Event *e){
    if(e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_CLOSE){
        browser_close(b);return;
    }
    if(e->type==SDL_KEYDOWN){
        if(e->key.keysym.sym==SDLK_ESCAPE){browser_close(b);return;}
        if(e->key.keysym.sym==SDLK_BACKSPACE){browser_parent(b);return;}
        if(e->key.keysym.sym==SDLK_RETURN){
            if(b->selected>=0 && b->selected<b->count){
                if(b->entries[b->selected].directory)browser_enter(b,b->selected);
                else if(browser_confirm_track(b))browser_close(b);
            }
            return;
        }
        if(e->key.keysym.sym==SDLK_UP && b->selected>0)--b->selected;
        if(e->key.keysym.sym==SDLK_DOWN && b->selected+1<b->count)++b->selected;
    }
    if(e->type==SDL_MOUSEWHEEL){
        b->scroll-=e->wheel.y*2;
        if(b->scroll<0)b->scroll=0;
    }
    if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT){
        int w=0,h=0;
        SDL_GetRendererOutputSize(b->renderer,&w,&h);
        int x=e->button.x,y=e->button.y;
        if(y>=h-63&&y<h-31&&x>=27){
            int span=(w-60)/5;
            if(span>0){
                int j=(x-27)/span;
                if(j>=0&&j<5 && x<27+j*span+span-7){
                    if(j==0)browser_save_default(b);
                    else if(j==1 && browser_can_add(b))browser_add(b);
                    else if(j==2)browser_save_open(b);
                    else if(j==3)browser_close(b);
                    else if(browser_confirm_track(b))browser_close(b);
                    return;
                }
            }
        }
        if(x<25||y<BROWSER_LIST_TOP||y>=h-78)return;
        int row=(y-BROWSER_LIST_TOP)/BROWSER_ROW;
        int pinned=(b->count>0 && strcmp(b->entries[0].name,"..")==0);
        int idx=(pinned && row==0)?0:row+b->scroll;
        int visible=(h-BROWSER_LIST_TOP-24)/BROWSER_ROW;
        if(row<0||row>=visible||idx<0||idx>=b->count)return;
        Uint32 now=SDL_GetTicks();
        if(idx==b->last_index && now-b->last_click<450)
            browser_enter(b,idx);
        else b->selected=idx;
        b->last_click=now;b->last_index=idx;
    }
}
static int control_button_hit(int x,int y,int w,int h) {
    if(w<650||h<550)return -1;
    SDL_Rect outer,empty,sliders,lm,rm;
    panel_geometry(w,h,&outer,&empty,&sliders,&lm,&rm);
    if(empty.w<390||empty.h<75)return -1;
    int bw=(empty.w-26-35)/6;
    int bh=(empty.h-18-6)/2;
    if(bh<25)return -1;
    for(int i=0;i<12;++i){
        int col=i%6,row=i/6;
        SDL_Rect button={empty.x+13+col*(bw+7),
                         empty.y+9+row*(bh+6),bw,bh};
        if(x>=button.x&&x<button.x+button.w&&
           y>=button.y&&y<button.y+button.h)return i;
    }
    return -1;
}

static int parse_args(int argc, char **argv, Options *out) {
#ifdef SAUTI_HAS_PIPEWIRE
    *out = (Options){ .frames=0, .source=SAUTI_MONITOR, .target=NULL,
                      .accelerated=0, .stereo_bars=1 };
#else
    *out = (Options){ .frames=0, .source=SAUTI_GENERATOR, .target=NULL,
                      .accelerated=0, .stereo_bars=0 };
#endif
    int explicit_view = 0;
    for (int i=1; i<argc; ++i) {
        if (strcmp(argv[i], "--accelerated") == 0) {
            out->accelerated = 1;
        } else if (strcmp(argv[i], "--software") == 0) {
            out->accelerated = 0;
        } else if (strcmp(argv[i], "--view") == 0 && i+1 < argc) {
            explicit_view = 1;
            const char *view = argv[++i];
            if (strcmp(view, "scope") == 0) out->stereo_bars = 0;
            else if (strcmp(view, "stereo-bars") == 0) out->stereo_bars = 1;
            else return -1;
        } else if (strcmp(argv[i], "--frames") == 0 && i+1 < argc) {
            char *end = NULL;
            const char *value = argv[++i];
            unsigned long n = strtoul(value, &end, 10);
            if (!*value || *end || n < 1 || n > 100000) return -1;
            out->frames = (int)n;
        } else if (strcmp(argv[i], "--source") == 0 && i+1 < argc) {
            const char *mode = argv[++i];
            if (strcmp(mode, "generator") == 0) out->source = SAUTI_GENERATOR;
            else if (strcmp(mode, "mic") == 0) out->source = SAUTI_MIC;
            else if (strcmp(mode, "monitor") == 0) out->source = SAUTI_MONITOR;
            else return -1;
        } else if (strcmp(argv[i], "--target") == 0 && i+1 < argc) {
            out->target = argv[++i];
            if (!*out->target) return -1;
        } else return -1;
    }
    if (out->source == SAUTI_GENERATOR && out->stereo_bars) {
        if (explicit_view) return -1;
        out->stereo_bars = 0;
    }
    if (out->source == SAUTI_GENERATOR && out->target) return -1;
    return 0;
}
static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [--frames N] [--source generator|mic|monitor] "
                    "[--view stereo-bars|scope] [--target PIPEWIRE_NODE] "
                    "[--software|--accelerated]\n", prog);
    fprintf(stderr, "Switch while running with M (mic) / O (output monitor).\n");
#ifdef SAUTI_HAS_PIPEWIRE
    fprintf(stderr, "Default: stereo-bars player with system monitor; video renderer: software.\\n");
#else
    fprintf(stderr, "Default: legacy generator scope (without PipeWire); renderer: software.\\n");
#endif
    fprintf(stderr, "Select the diagnostic display with --view scope.\\n"
                    "Use --target NODE for an explicit audio route.\\n");
}

/* Sauti in-process player: SDL_mixer streams MP3 through SDL audio.
 * No external processes, separate windows or shell commands. */
typedef struct {
    Mix_Music *music;
    SautiMp3SdlPlayer *portable_mp3;
    double duration;
    char loaded[PATH_MAX];
    int audio_ready;
    int muted;
} SautiPlayer;
static int player_init(SautiPlayer *p){
    if(p->audio_ready)return 0;
    if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0){
        fprintf(stderr,"Sauti: SDL audio: %s\n",SDL_GetError());
        return -1;
    }
    /* Optional SDL2_mixer decoders: one missing codec must not disable
     * otherwise working playback (including built-in WAV handling). */
    int requested=MIX_INIT_MP3|MIX_INIT_OGG|MIX_INIT_FLAC;
#if SDL_MIXER_VERSION_ATLEAST(2,6,0)
    requested|=MIX_INIT_OPUS;
#endif
    int ready=Mix_Init(requested);
    if((ready&requested)!=requested)
        fprintf(stderr,"Sauti: nie wszystkie dekodery audio sa dostepne "
                       "(gotowe: 0x%x, zadane: 0x%x): %s\n",
                ready,requested,Mix_GetError());
    if(Mix_OpenAudio(48000,AUDIO_S16SYS,2,2048)!=0){
        fprintf(stderr,"Sauti: wyjscie audio: %s\n",Mix_GetError());
        Mix_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return -1;
    }
    fprintf(stderr,"Sauti: dekodery muzyki SDL2_mixer (%d):",
            Mix_GetNumMusicDecoders());
    for(int i=0;i<Mix_GetNumMusicDecoders();++i)
        fprintf(stderr," %s",Mix_GetMusicDecoder(i));
    fprintf(stderr,"\n");
    p->audio_ready=1;
    Mix_VolumeMusic(p->muted?0:MIX_MAX_VOLUME);
    return 0;
}
static void player_mute(SautiPlayer *p){
    p->muted=!p->muted;
    sauti_mp3_sdl_mute(p->portable_mp3,p->muted);
    if(p->audio_ready)Mix_VolumeMusic(p->muted?0:MIX_MAX_VOLUME);
}
static void player_stop(SautiPlayer *p){
    sauti_mp3_sdl_stop(p->portable_mp3);
    if(p->audio_ready)Mix_HaltMusic();
}
static void player_unload(SautiPlayer *p){
    player_stop(p);
    if(p->music){Mix_FreeMusic(p->music);p->music=NULL;}
    sauti_mp3_sdl_destroy(p->portable_mp3);
    p->portable_mp3=NULL;
    p->loaded[0]=0;
    p->duration=0.0;
}
static int player_load(SautiPlayer *p,const char *path){
    if(!path||!path[0])return -1;
    player_unload(p);
    if(player_init(p)!=0)return -1;
    const char *ext=strrchr(path,'.');
    if(ext && (strcasecmp(ext,".mp3")==0 || strcasecmp(ext,".ogg")==0 || strcasecmp(ext,".oga")==0 || strcasecmp(ext,".opus")==0 || strcasecmp(ext,".wav")==0 || strcasecmp(ext,".flac")==0 || strcasecmp(ext,".wv")==0 || strcasecmp(ext,".mid")==0 || strcasecmp(ext,".midi")==0 || strcasecmp(ext,".avr")==0 || strcasecmp(ext,".dvs")==0 || strcasecmp(ext,".ym")==0 || strcasecmp(ext,".dtm")==0 || strcasecmp(ext,".sndh")==0 || strcasecmp(ext,".snd")==0)){
        p->portable_mp3=sauti_mp3_sdl_open(path);
        if(p->portable_mp3){
            p->duration=sauti_mp3_sdl_duration(p->portable_mp3);
            sauti_mp3_sdl_mute(p->portable_mp3,p->muted);
            snprintf(p->loaded,sizeof(p->loaded),"%s",path);
            fprintf(stderr,"Sauti: portable decoder -> SDL PCM: %s\n",path);
            return 0;
        }
        fprintf(stderr,"Sauti: portable decoder unavailable, SDL2_mixer fallback: %s\n",path);
    }
    Mix_Music *music=Mix_LoadMUS(path);
    if(!music){
        fprintf(stderr,"Sauti: nie mozna odtworzyc pliku audio %s: %s\n",path,Mix_GetError());
        return -1;
    }
    p->music=music;
    p->duration=Mix_MusicDuration(music);
    if(p->duration<0.0)p->duration=0.0;
    snprintf(p->loaded,sizeof(p->loaded),"%s",path);
    return 0;
}
static void player_play(SautiPlayer *p){
    if(p->portable_mp3){sauti_mp3_sdl_play(p->portable_mp3);return;}
    if(!p->music)return;
    if(Mix_PausedMusic()){Mix_ResumeMusic();return;}
    if(!Mix_PlayingMusic() && Mix_PlayMusic(p->music,0)!=0)
        fprintf(stderr,"Sauti: nie mozna odtworzyc: %s\n",Mix_GetError());
}
static void player_pause(SautiPlayer *p){
    if(p->portable_mp3){sauti_mp3_sdl_pause(p->portable_mp3);return;}
    if(p->music&&Mix_PlayingMusic()&&!Mix_PausedMusic())Mix_PauseMusic();
}
static int player_progress(const SautiPlayer *p){
    if(p->portable_mp3)return sauti_playback_percent(sauti_mp3_sdl_position(p->portable_mp3),p->duration);
    if(!p->music||p->duration<=0.0)return 0;
    double pos=Mix_GetMusicPosition(p->music);
    return sauti_playback_percent(pos,p->duration);
}
/* Shuffle plays a random readable MP3 in the loaded track directory.
 * Reservoir sampling needs no playlist allocation and avoids choosing
 * the currently loaded song when another candidate exists. */
static int player_shuffle_path(const char *current,char *out,size_t cap){
    if(!current||!current[0]||cap<2)return 0;
    const char *slash=strrchr(current,'/');
    if(!slash)return 0;
    char folder[PATH_MAX];
    size_t len=(size_t)(slash-current);
    if(len==0)len=1;
    if(len>=sizeof(folder))return 0;
    memcpy(folder,current,len);folder[len]=0;
    DIR *dir=opendir(folder);
    if(!dir)return 0;
    unsigned count=0;
    struct dirent *entry;
    while((entry=readdir(dir))!=NULL){
        const char *ext=strrchr(entry->d_name,'.');
        if(!ext||strcasecmp(ext,".mp3")!=0)continue;
        char path[PATH_MAX];
        int n=snprintf(path,sizeof(path),"%s%s%s",folder,
                       strcmp(folder,"/")==0?"":"/",entry->d_name);
        if(n<0||(size_t)n>=sizeof(path)||strcmp(path,current)==0)continue;
        struct stat st;
        if(stat(path,&st)!=0||!S_ISREG(st.st_mode))continue;
        ++count;
        if((unsigned)rand()%count==0)
            snprintf(out,cap,"%s",path);
    }
    closedir(dir);
    return count>0;
}
/* Sequential PREV/NEXT among regular MP3 files in the track directory.
 * Lexical order with wraparound, and no persistent playlist allocation. */
static int player_neighbor_path(const char *current,int forward,char *out,size_t cap){
    if(!current||!current[0]||cap<2)return 0;
    const char *slash=strrchr(current,'/');
    if(!slash)return 0;
    char folder[PATH_MAX];
    size_t len=(size_t)(slash-current);
    if(len==0)len=1;
    if(len>=sizeof(folder))return 0;
    memcpy(folder,current,len);folder[len]=0;
    const char *current_name=slash+1;
    DIR *dir=opendir(folder);
    if(!dir)return 0;
    char best[PATH_MAX]={0},wrap[PATH_MAX]={0};
    char best_name[256]={0},wrap_name[256]={0};
    struct dirent *entry;
    while((entry=readdir(dir))!=NULL){
        const char *ext=strrchr(entry->d_name,'.');
        if(!ext||strcasecmp(ext,".mp3")!=0)continue;
        char path[PATH_MAX];
        int n=snprintf(path,sizeof(path),"%s%s%s",folder,
                       strcmp(folder,"/")==0?"":"/",entry->d_name);
        if(n<0||(size_t)n>=sizeof(path)||strcmp(path,current)==0)continue;
        struct stat st;
        if(stat(path,&st)!=0||!S_ISREG(st.st_mode))continue;
        int vs_cur=strcasecmp(entry->d_name,current_name);
        if(forward){
            if(vs_cur>0 && (!best[0]||strcasecmp(entry->d_name,best_name)<0)){
                snprintf(best,sizeof(best),"%s",path);
                snprintf(best_name,sizeof(best_name),"%s",entry->d_name);
            }
            if(!wrap[0]||strcasecmp(entry->d_name,wrap_name)<0){
                snprintf(wrap,sizeof(wrap),"%s",path);
                snprintf(wrap_name,sizeof(wrap_name),"%s",entry->d_name);
            }
        }else{
            if(vs_cur<0 && (!best[0]||strcasecmp(entry->d_name,best_name)>0)){
                snprintf(best,sizeof(best),"%s",path);
                snprintf(best_name,sizeof(best_name),"%s",entry->d_name);
            }
            if(!wrap[0]||strcasecmp(entry->d_name,wrap_name)>0){
                snprintf(wrap,sizeof(wrap),"%s",path);
                snprintf(wrap_name,sizeof(wrap_name),"%s",entry->d_name);
            }
        }
    }
    closedir(dir);
    const char *pick=best[0]?best:wrap;
    if(!pick[0])return 0;
    snprintf(out,cap,"%s",pick);
    return 1;
}
static int player_has_track(const SautiPlayer *p){return p->music || p->portable_mp3;}
static int player_is_playing(const SautiPlayer *p){
    if(p->portable_mp3)return sauti_mp3_sdl_playing(p->portable_mp3);
    return p->music && p->audio_ready && Mix_PlayingMusic();
}
static int player_is_paused(const SautiPlayer *p){
    if(p->portable_mp3)return sauti_mp3_sdl_paused(p->portable_mp3);
    return p->music && p->audio_ready && Mix_PausedMusic();
}
static double player_position(const SautiPlayer *p){
    return p->portable_mp3?sauti_mp3_sdl_position(p->portable_mp3):
        (p->music?Mix_GetMusicPosition(p->music):0.0);
}
static int player_skip(SautiPlayer *p,SautiBrowser *browser,int forward){
    const char *cur=p->loaded[0]?p->loaded:browser->selected_track;
    char chosen[PATH_MAX]={0};
    if(!player_neighbor_path(cur,forward,chosen,sizeof(chosen)))return 0;
    int keep=player_is_playing(p) && !player_is_paused(p);
    if(player_load(p,chosen)!=0)return 0;
    snprintf(browser->selected_track,sizeof(browser->selected_track),"%s",chosen);
    if(keep)player_play(p);
    return 1;
}
static void player_time(double seconds,char *out,size_t cap){
    unsigned long t=seconds>0.0?(unsigned long)seconds:0;
    snprintf(out,cap,"%lu:%02lu",t/60,t%60);
}
/* SDL2_mixer is the Linux playback adapter; application calls shared ABI. */
static int mixer_load(void *c,const char *path){return player_load(c,path);}
static void mixer_play(void *c){player_play(c);}
static void mixer_pause(void *c){player_pause(c);}
static void mixer_stop(void *c){player_stop(c);}
static void mixer_seek(void *c,double seconds){
    SautiPlayer *p=c;
    if(p->portable_mp3){sauti_mp3_sdl_seek(p->portable_mp3,seconds);return;}
    if(p->music && p->duration>0.0 &&
       Mix_SetMusicPosition(seconds)!=0)
        fprintf(stderr,"Sauti: przewijanie: %s\n",Mix_GetError());
}
static void mixer_mute(void *c,int muted){
    SautiPlayer *p=c;
    if(p->muted!=!!muted)player_mute(p);
}
static int mixer_playing(const void *c){
    const SautiPlayer *p=c;
    return player_is_playing(p);
}
static int mixer_paused(const void *c){
    const SautiPlayer *p=c;
    return player_is_paused(p);
}
static double mixer_position(const void *c){
    const SautiPlayer *p=c;
    return player_position(p);
}
static double mixer_duration(const void *c){
    const SautiPlayer *p=c;
    return p->duration;
}
static const SautiPlaybackOps sauti_sdl_mixer_ops={
    mixer_load,mixer_play,mixer_pause,mixer_stop,mixer_seek,
    mixer_mute,mixer_playing,mixer_paused,mixer_position,mixer_duration
};
static void player_close(SautiPlayer *p){
    player_unload(p);
    if(p->audio_ready){
        Mix_CloseAudio();
        Mix_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        p->audio_ready=0;
    }
}
#ifdef SAUTI_HAS_PIPEWIRE
/* UI thread only. The PipeWire real-time callback never writes to disk. */
static int sauti_record_drain(SautiRecordRing *ring,SautiWav *wav){
    float lr[2048],left[1024],right[1024];
    uint32_t count;
    while((count=sauti_record_pop(ring,lr,1024))!=0){
        for(uint32_t i=0;i<count;++i){
            left[i]=lr[2*i];
            right[i]=lr[2*i+1];
        }
        if(!sauti_wav_append(wav,left,right,count))return 0;
    }
    return !sauti_record_overflow(ring);
}
static void sauti_record_end(SautiAudioBackend *capture,SautiRecordRing *ring,SautiWav *wav){
    if(!wav->file)return;
    sauti_audio_record_enable(capture,0);
    int ok=sauti_record_drain(ring,wav);
    if(!sauti_wav_finish(wav)||!ok)
        fprintf(stderr,"Sauti REC: recording incomplete or write failed.\n");
    else fprintf(stderr,"Sauti REC: WAV finalized.\n");
}
static int sauti_record_begin(SautiAudioBackend *capture,SautiRecordRing *ring,SautiWav *wav){
    char dir[PATH_MAX],file[PATH_MAX];
    browser_config_path(dir,sizeof(dir));
    if(mkdir(dir,0700)!=0 && errno!=EEXIST)return 0;
    int n=snprintf(file,sizeof(file),"%s/recording-%lu.wav",dir,
                   (unsigned long)SDL_GetTicks64());
    if(n<0||(size_t)n>=sizeof(file))return 0;
    uint32_t rate=sauti_audio_sample_rate(capture);
    if(!rate){
        fprintf(stderr,"Sauti REC: PipeWire stereo format not negotiated.\n");
        return 0;
    }
    FILE *fp=fopen(file,"wb");
    if(!fp)return 0;
    if(!sauti_wav_begin(wav,fp,rate)){
        fclose(fp);
        wav->file=NULL;
        (void)remove(file);
        return 0;
    }
    sauti_audio_record_enable(capture,0);
    sauti_record_reset(ring);
    sauti_audio_record_enable(capture,1);
    fprintf(stderr,"Sauti REC: %s\n",file);
    return 1;
}
#endif
int main(int argc, char **argv) {
    Options opt;
    if (parse_args(argc, argv, &opt) != 0) { usage(argv[0]); return 2; }
    fprintf(stderr, "Sauti startup: source=%s view=%s renderer=%s\n",
            opt.source == SAUTI_GENERATOR ? "generator" :
            opt.source == SAUTI_MONITOR ? "monitor" : "mic",
            opt.stereo_bars ? "stereo-bars" : "scope",
            opt.accelerated ? "accelerated" : "software");
#ifndef SAUTI_HAS_PIPEWIRE
    if (opt.source != SAUTI_GENERATOR) {
        fprintf(stderr, "Audio capture is not enabled: build 'make window-pipewire'.\n");
        return 2;
    }
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("Sayari Sauti - ASM audio scope",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 640,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    /* Software is the conservative default for Wayland hybrid-GPU systems.
     * Accelerated rendering is explicit opt-in while display flicker is investigated.
     * A compositor may still use GPU internally; this does not disable it.
     */
    Uint32 flags = opt.accelerated ?
        (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : SDL_RENDERER_SOFTWARE;
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, flags);
    if (!renderer && opt.accelerated) {
        fprintf(stderr, "Accelerated SDL renderer unavailable; trying software.\n");
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_RendererInfo renderer_info;
    if (SDL_GetRendererInfo(renderer, &renderer_info) == 0)
        fprintf(stderr, "Sauti: SDL renderer=%s, flags=0x%x\n",
                renderer_info.name, renderer_info.flags);

    SDL_Texture *texture = NULL;
    uint32_t *pixels = NULL;
    SautiFrameScratch *scratch = calloc(1, sizeof(*scratch));
    SautiAudioRing *ring = NULL;
    SautiStereoRing *stereo_ring = NULL;
    SautiPeakHold peaks = {0};
    SautiBrowser browser = {0};
    SautiVisualPicker picker = {0};
    SautiSettings settings = {0};
    settings.background=5;
    settings.frame_color=5;
    sauti_settings_load(&settings);
    sauti_theme_bg=settings.background;
    sauti_theme_frames=settings.frame_color;
    sauti_theme_renderer=renderer;
    SautiPlayer player = {0};
    SautiPlaybackBackend playback={&player,&sauti_sdl_mixer_ops};
#ifdef SAUTI_HAS_PIPEWIRE
    SautiRecordRing *record_ring=calloc(1,sizeof(*record_ring));
    SautiWav recording={0};
#endif
    browser.selected=-1; browser.last_index=-1;
    browser.queue_selected=-1;
    browser_restore_playlist(&browser);
    int gain_level=50, volume_level=50, dragging=-1;
    int track_position=0, track_dragging=0;
    SautiAppState app={0};
    int skip_flash=0;
    Uint32 skip_flash_until=0;
    srand((unsigned)SDL_GetTicks());
#ifdef SAUTI_HAS_PIPEWIRE
    Uint32 last_volume_read=0;
#endif
    SautiFrameScratch *right_scratch = opt.stereo_bars ? calloc(1, sizeof(*right_scratch)) : NULL;
    float *left_history = opt.stereo_bars ? calloc(SAUTI_FFT_SAMPLES, sizeof(float)) : NULL;
    float *right_history = opt.stereo_bars ? calloc(SAUTI_FFT_SAMPLES, sizeof(float)) : NULL;
#ifdef SAUTI_HAS_PIPEWIRE
    SautiAudioBackend backend={0};
    SautiAudioBackend *capture=&backend;
#endif
    int width = 0, height = 0, running = 1, frames = 0, failed = !scratch ||
        (opt.stereo_bars && (!right_scratch || !left_history || !right_history));
#ifdef SAUTI_HAS_PIPEWIRE
    int last_capture_state = INT_MIN;
#endif

    if (!failed && opt.source != SAUTI_GENERATOR) {
        if (opt.stereo_bars) stereo_ring = calloc(1, sizeof(*stereo_ring));
        else ring = calloc(1, sizeof(*ring));
        if (!ring && !stereo_ring) failed = 1;
#ifdef SAUTI_HAS_PIPEWIRE
        if (!failed) {
            sauti_audio_open(capture,&sauti_pipewire_ops,stereo_ring, ring,record_ring,
                opt.source == SAUTI_MONITOR, opt.target);
            if (!capture->handle) {
                fprintf(stderr, "Cannot initialize PipeWire capture.\n");
                failed = 1;
            }
        }
#endif
    }

    while (running && !failed) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            Uint32 browser_id=browser.window?SDL_GetWindowID(browser.window):0;
            Uint32 save_id=browser.save_window?SDL_GetWindowID(browser.save_window):0;
            Uint32 picker_id=picker.window?SDL_GetWindowID(picker.window):0;
            Uint32 settings_id=settings.window?SDL_GetWindowID(settings.window):0;
            Uint32 target_id=0;
            if(event.type==SDL_WINDOWEVENT)target_id=event.window.windowID;
            else if(event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP)
                target_id=event.button.windowID;
            else if(event.type==SDL_MOUSEMOTION)target_id=event.motion.windowID;
            else if(event.type==SDL_MOUSEWHEEL)target_id=event.wheel.windowID;
            else if(event.type==SDL_KEYDOWN||event.type==SDL_KEYUP)
                target_id=event.key.windowID;
            else if(event.type==SDL_TEXTINPUT)
                target_id=event.text.windowID;
            else if(event.type==SDL_TEXTEDITING)
                target_id=event.edit.windowID;
            if(save_id && target_id==save_id){browser_save_event(&browser,&event);continue;}
            if(settings_id && target_id==settings_id){
                if(event.type==SDL_WINDOWEVENT &&
                   event.window.event==SDL_WINDOWEVENT_CLOSE)
                    sauti_settings_close(&settings);
                else if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE)
                    sauti_settings_close(&settings);
                else if(event.type==SDL_MOUSEBUTTONDOWN &&
                        event.button.button==SDL_BUTTON_LEFT){
                    int x=event.button.x,y=event.button.y;
                    int selected_tab=sauti_settings_tab_hit(x,y);
                    if(selected_tab>=0){
                        const int pages[5]={5,1,2,3,4};
                        settings.page=pages[selected_tab];
                    }else if(settings.page==1){
                        if(y>=214&&y<260){
                            for(int i=0;i<3;i++)if(x>=157+i*126&&x<273+i*126){
                                settings.accent=i;sauti_settings_save(&settings);break;
                            }
                        }else if(y>=335&&y<374){
                            for(int i=0;i<8;i++){
                                int dx=x-(80+i*62),dy=y-355;
                                if(dx*dx+dy*dy<=19*19){
                                    settings.background=i;sauti_theme_bg=i;
                                    sauti_settings_save(&settings);break;
                                }
                            }
                        }else if(y>=478&&y<517){
                            for(int i=0;i<8;i++){
                                int dx=x-(80+i*62),dy=y-497;
                                if(dx*dx+dy*dy<=19*19){
                                    settings.frame_color=i;sauti_theme_frames=i;
                                    sauti_settings_save(&settings);break;
                                }
                            }
                        }
                    }else if(settings.page==2){
                        if(x>=57&&x<523&&y>=278&&y<325)browser_open(&browser);
                        else if(x>=57&&x<523&&y>=340&&y<387)browser_save_default(&browser);
                    }else if(settings.page==3){
                        if(x>=57&&x<523){
                            if(y>=229&&y<280){settings.language=0;sauti_settings_save(&settings);}
                            else if(y>=299&&y<350){settings.language=1;sauti_settings_save(&settings);}
                        }
                    }else if(settings.page==4 && x>=57&&x<523){
                        if(y>=229&&y<280)sauti_app_toggle(&app,SAUTI_TOGGLE_REPEAT);
                        else if(y>=299&&y<350)sauti_app_toggle(&app,SAUTI_TOGGLE_SHUFFLE);
                    }
                }
                continue;
            }
            if(picker_id && target_id==picker_id){
                if(event.type==SDL_WINDOWEVENT &&
                   event.window.event==SDL_WINDOWEVENT_CLOSE)
                    sauti_picker_close(&picker);
                else if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE)
                    sauti_picker_close(&picker);
                else if(event.type==SDL_MOUSEBUTTONDOWN &&
                        event.button.button==SDL_BUTTON_LEFT){
                    int picked=sauti_picker_choice(event.button.x,event.button.y);
                    if(picked>=0){sauti_visual_style=picked;sauti_picker_close(&picker);}
                }
                continue;
            }
            if(browser_id && target_id==browser_id) {
                browser_event(&browser,&event);
                continue;
            }
            int control_hit=-1;
            if(event.type==SDL_KEYDOWN && !event.key.repeat &&
               event.key.keysym.mod==KMOD_NONE){
                int action=sauti_shortcut_action(event.key.keysym.sym);
                if(action==0)
                    control_hit=player_is_playing(&player)?8:2; /* Space: play / stop */
                else if(action==1)control_hit=1;
                else if(action==2)control_hit=4;
                else if(action==3)control_hit=3;
                else if(action==4)control_hit=7;
                else if(action==5)control_hit=6;
                else if(action==6)control_hit=10;
            }
            if(opt.stereo_bars && event.type==SDL_MOUSEBUTTONDOWN &&
               event.button.button==SDL_BUTTON_LEFT)
                control_hit=control_button_hit(event.button.x,event.button.y,
                                               width,height);
            if(opt.stereo_bars && event.type==SDL_MOUSEBUTTONDOWN &&
               event.button.button==SDL_BUTTON_LEFT){
                SDL_Rect left=sauti_visual_bay(width,height,0);
                SDL_Rect right=sauti_visual_bay(width,height,1);
                int x=event.button.x,y=event.button.y;
                if((left.w>0 && x>=left.x && x<left.x+left.w &&
                    y>=left.y && y<left.y+left.h) ||
                   (right.w>0 && x>=right.x && x<right.x+right.w &&
                    y>=right.y && y<right.y+right.h))
                    sauti_picker_open(&picker);
            }
            if(control_hit==9)browser_open(&browser);
#ifdef SAUTI_HAS_PIPEWIRE
            if(control_hit==5 && record_ring && opt.stereo_bars){
                if(recording.file)sauti_record_end(capture,record_ring,&recording);
                else if(capture->handle && sauti_audio_status(capture)==3){
                    if(!sauti_record_begin(capture,record_ring,&recording))
                        fprintf(stderr,"Sauti REC: unable to create WAV.\n");
                }else fprintf(stderr,"Sauti REC: active PipeWire stereo capture required.\n");
            }
#endif
            if(opt.stereo_bars && event.type==SDL_MOUSEWHEEL){
                int mx=0,my=0;
                SDL_GetMouseState(&mx,&my);
                browser_upper_wheel(&browser,mx,my,width,height,event.wheel.y,
                    player_is_playing(&player));
            }
            if(opt.stereo_bars && event.type==SDL_MOUSEBUTTONDOWN){
                int idx=browser_upper_click(&browser,event.button.x,event.button.y,
                    width,height,event.button.button,event.button.clicks,
                    player_is_playing(&player));
                if(idx>=0){
                    char chosen[PATH_MAX];
                    snprintf(chosen,sizeof(chosen),"%s",browser.queue[idx]);
                    if(sauti_playback_load(&playback,chosen)==0){
                        snprintf(browser.selected_track,sizeof(browser.selected_track),
                                 "%s",chosen);
                        (void)sauti_queue_remove(&browser.queue[0][0],PATH_MAX,
                                                 &browser.queue_count,idx);
                        browser.queue_selected=-1;
                        sauti_playback_play(&playback);
                    }
                }
            }
            if(control_hit==11){
                /* External Qt preferences only owns UI; audio/DSP remains here.
                 * Legacy SDL preferences remain the fallback without Qt6. */
                const char *qt_settings="./build/sauti_settings_qt";
                if(access(qt_settings,X_OK)==0){
                    pid_t child=fork();
                    if(child==0){
                        pid_t detached=fork();
                        if(detached==0){
                            execl(qt_settings,qt_settings,(char*)NULL);
                            _exit(127);
                        }
                        _exit(detached<0?127:0);
                    }
                    if(child<0)sauti_settings_open(&settings);
                    else {
                        int status=0;
                        if(waitpid(child,&status,0)<0 || !WIFEXITED(status) ||
                           WEXITSTATUS(status)!=0)
                            sauti_settings_open(&settings);
                    }
                }else sauti_settings_open(&settings);
            }
            /* PLAY toggles in-process music playback; STOP halts it. */
            SautiTransportAction transport=sauti_transport_command(
                control_hit,player_has_track(&player),
                player_is_playing(&player),player_is_paused(&player));
            if(transport==SAUTI_ACTION_PLAY||transport==SAUTI_ACTION_RESUME)
                sauti_playback_play(&playback);
            else if(transport==SAUTI_ACTION_PAUSE)sauti_playback_pause(&playback);
            if(control_hit==7)sauti_playback_mute(&playback,!player.muted);
            if(control_hit==6)sauti_app_toggle(&app,SAUTI_TOGGLE_REPEAT);
            if(control_hit==10)sauti_app_toggle(&app,SAUTI_TOGGLE_SHUFFLE);
            if(transport==SAUTI_ACTION_STOP){sauti_playback_stop(&playback);sauti_app_stopped(&app);}
            if(control_hit==1||control_hit==4){
                if(player_skip(&player,&browser,control_hit==4)){
                    skip_flash=control_hit;
                    skip_flash_until=SDL_GetTicks()+240;
                }
            }
            if(opt.stereo_bars && (event.type==SDL_MOUSEBUTTONDOWN ||
                                   event.type==SDL_MOUSEMOTION)){
                int held=event.type==SDL_MOUSEMOTION?
                     ((event.motion.state&SDL_BUTTON_LMASK)!=0):
                     (event.button.button==SDL_BUTTON_LEFT);
                if(held){
                    int x=event.type==SDL_MOUSEMOTION?event.motion.x:event.button.x;
                    int y=event.type==SDL_MOUSEMOTION?event.motion.y:event.button.y;
                    int track=track_progress_position(x,y,width,height);
                    if(track>=0 && (event.type==SDL_MOUSEBUTTONDOWN ||
                                    track_dragging)) {
                        track_position=track;
                        track_dragging=1;
                    }
                    int which=0;
                    int value=slider_position(x,y,width,height,&which);
                    if(value>=0&&(event.type==SDL_MOUSEBUTTONDOWN||dragging==which)){
                        dragging=which;
                        if(which==0)gain_level=value;
                        else {
                            volume_level=value;
#ifdef SAUTI_HAS_PIPEWIRE
                            write_output_volume(value);
#endif
                        }
                    }
                }
            }
            if(event.type==SDL_MOUSEBUTTONUP){
                if(track_dragging)sauti_playback_seek(&playback,
                    sauti_playback_seek_seconds(track_position,sauti_playback_duration(&playback)));
                dragging=-1;track_dragging=0;
            }
#ifdef SAUTI_HAS_PIPEWIRE
            enum SautiSource requested = opt.source;
            if(control_hit==0)requested=opt.source==SAUTI_MIC?
                SAUTI_MONITOR:SAUTI_MIC;
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_m) requested = SAUTI_MIC;
                if (event.key.keysym.sym == SDLK_o) requested = SAUTI_MONITOR;
            }

            if (requested != SAUTI_GENERATOR &&
                (requested != opt.source || !capture->handle)) {
                if(recording.file)sauti_record_end(capture,record_ring,&recording);
                sauti_audio_close(capture);
                
                if (opt.stereo_bars) {
                    if (!stereo_ring) stereo_ring = calloc(1, sizeof(*stereo_ring));
                    if (!stereo_ring) { failed = 1; break; }
                    memset(stereo_ring, 0, sizeof(*stereo_ring));
                    memset(&peaks, 0, sizeof(peaks));
                    memset(right_scratch, 0, sizeof(*right_scratch));
                    memset(left_history, 0, SAUTI_FFT_SAMPLES * sizeof(float));
                    memset(right_history, 0, SAUTI_FFT_SAMPLES * sizeof(float));
                } else {
                    if (!ring) ring = calloc(1, sizeof(*ring));
                    if (!ring) { failed = 1; break; }
                    memset(ring, 0, sizeof(*ring));
                }
                memset(scratch, 0, sizeof(*scratch));
                opt.source = requested;
                last_capture_state = INT_MIN;
                sauti_audio_open(capture,&sauti_pipewire_ops,stereo_ring, ring,record_ring,
                    opt.source == SAUTI_MONITOR, NULL);
                if (!capture->handle)
                    fprintf(stderr, "Sauti: capture source unavailable. Press M/O to retry.\n");
            }
#endif
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
                running = 0;
        }
        if(browser.selected_track[0] &&
           strcmp(player.loaded,browser.selected_track)!=0){
            /* Selecting OK loads the track; PLAY starts playback. */
            if(sauti_playback_load(&playback,browser.selected_track)!=0)
                browser.selected_track[0]=0;
        }
        sauti_mp3_sdl_pump(player.portable_mp3);
        /* Detect natural playback completion, not manual STOP or pause. */
        int now_playing=player_is_playing(&player);
        SautiAfterTrack next=sauti_transport_finished(app.was_playing,now_playing,
            player_has_track(&player),app.repeat,browser.queue_count,app.shuffle);
        if(next!=SAUTI_AFTER_NONE){
            if(next==SAUTI_AFTER_REPEAT){
                sauti_playback_play(&playback);
            }else if(next==SAUTI_AFTER_QUEUE){
                char chosen[PATH_MAX];
                if(sauti_queue_take(&browser.queue[0][0],PATH_MAX,
                                    &browser.queue_count,0,chosen,sizeof(chosen)) &&
                   sauti_playback_load(&playback,chosen)==0){
                    snprintf(browser.selected_track,sizeof(browser.selected_track),
                             "%s",chosen);
                    sauti_playback_play(&playback);
                }
            }else if(next==SAUTI_AFTER_SHUFFLE){
                char chosen[PATH_MAX]={0};
                if(player_shuffle_path(player.loaded,chosen,sizeof(chosen))){
                    if(sauti_playback_load(&playback,chosen)==0){
                        snprintf(browser.selected_track,sizeof(browser.selected_track),
                                 "%s",chosen);
                        sauti_playback_play(&playback);
                    }
                }
            }
        }
        sauti_app_update_playback(&app,player_is_playing(&player));
        if(!track_dragging)track_position=player_progress(&player);
#ifdef SAUTI_HAS_PIPEWIRE
        if(recording.file){
            if(sauti_audio_sample_rate(capture)!=recording.sample_rate){
                fprintf(stderr,"Sauti REC: capture format changed; stopping recording.\n");
                sauti_record_end(capture,record_ring,&recording);
            }else if(!sauti_record_drain(record_ring,&recording)){
                fprintf(stderr,"Sauti REC: buffer overflow or disk error.\n");
                sauti_record_end(capture,record_ring,&recording);
            }
        }
#endif
        if (!running) break;
#ifdef SAUTI_HAS_PIPEWIRE
        if (capture->handle) {
            int state = sauti_audio_status(capture);
            if (state != last_capture_state) {
                fprintf(stderr, "Sauti: PipeWire capture state=%d (%s)\n",
                        state, state == 3 ? "streaming" :
                        state == 2 ? "paused" :
                        state == 1 ? "connecting" :
                        state == 0 ? "unconnected" : "error");
                last_capture_state = state;
            }
            if (state < 0) {
                fprintf(stderr, "PipeWire stream entered ERROR state.\n");
                failed = 1;
                break;
            }
        }
#endif
        int w = 0, h = 0;
        if (SDL_GetRendererOutputSize(renderer, &w, &h) != 0) {
            failed = 1; break;
        }
        if (w < 128 || h < 128) {
            SDL_Delay(16);
            continue;
        }
        if (w > 4096 || h > 4096) {
            fprintf(stderr, "Maximum canvas is 4096x4096 (%dx%d requested).\n", w, h);
            failed = 1; break;
        }
        if (w != width || h != height) {
            size_t count = (size_t)w * (size_t)h;
            if (count > SIZE_MAX / sizeof(*pixels) || count > (size_t)INT_MAX / 4u) {
                failed = 1; break;
            }
            uint32_t *next = malloc(count * sizeof(*pixels));
            SDL_Texture *next_tex = SDL_CreateTexture(renderer,
                SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
            if (!next || !next_tex) {
                free(next);
                if (next_tex) SDL_DestroyTexture(next_tex);
                failed = 1; break;
            }
            free(pixels);
            if (texture) SDL_DestroyTexture(texture);
            pixels = next; texture = next_tex;
            width = w; height = h;
        }
        int rc;
        if (opt.source == SAUTI_GENERATOR)
            rc = sauti_render_demo_frame(pixels, (uint32_t)width,
                                         (uint32_t)height, scratch);
        else if (opt.stereo_bars && stereo_ring) {
#ifdef SAUTI_HAS_PIPEWIRE
            if (!capture->handle || sauti_audio_status(capture) != 3) {
                memset(pixels, 0, (size_t)width * height * sizeof(*pixels));
                rc = 0;
            } else
#endif
            {
            (void)sauti_stereo_ring_drain_f32(stereo_ring, left_history,
                                                  right_history, SAUTI_FFT_SAMPLES);
            rc = sauti_analyze_stereo_bands_f32(left_history, right_history,
                                                scratch, right_scratch);
            if (rc == 0) {
                uint32_t now_ms = (uint32_t)SDL_GetTicks();
                for(unsigned k=0;k<SAUTI_LOG_BANDS;++k){
                    float factor=(float)gain_level/50.0f;
                    float lv=scratch->smooth_bands[k]*factor;
                    float rv=right_scratch->smooth_bands[k]*factor;
                    scratch->smooth_bands[k]=lv>1.0f?1.0f:lv;
                    right_scratch->smooth_bands[k]=rv>1.0f?1.0f:rv;
                }
                sauti_peak_hold_update(&peaks, scratch->smooth_bands,
                                       right_scratch->smooth_bands, now_ms);
                sauti_render_stereo_bars_argb32(pixels,
                (uint32_t)width, (uint32_t)height,
                scratch->smooth_bands, right_scratch->smooth_bands,
                now_ms);
                sauti_draw_peak_hold_argb32(pixels, (uint32_t)width,
                                            (uint32_t)height, &peaks);
            }
            }
        } else if (
#ifdef SAUTI_HAS_PIPEWIRE
            capture->handle &&
#endif
            ring) {
            /* Mono sliding window update and history are entirely NASM. */
            (void)sauti_ring_drain_window_f32(ring, scratch->samples, SAUTI_FFT_SAMPLES);
            rc = sauti_render_audio_frame(pixels, (uint32_t)width,
                                          (uint32_t)height, scratch);
        } else {
            memset(pixels, 0, (size_t)width * height * sizeof(*pixels));
            rc = 0;
        }
        if(rc==0 && opt.stereo_bars)
            sauti_background_apply(pixels,width,height,settings.background);
        if (rc != 0 ||
            SDL_UpdateTexture(texture, NULL, pixels, width * (int)sizeof(*pixels)) != 0 ||
            SDL_RenderClear(renderer) != 0 ||
            SDL_RenderCopy(renderer, texture, NULL, NULL) != 0) {
            fprintf(stderr, "Sauti render error: %s\n", SDL_GetError());
            failed = 1; break;
        }
        if(opt.stereo_bars){
            Uint32 now=SDL_GetTicks();
#ifdef SAUTI_HAS_PIPEWIRE
            if(now-last_volume_read>=500){
                volume_level=read_output_volume(volume_level);
                last_volume_read=now;
            }
#endif
            paint_stereo_frames(renderer,width,height);
            paint_audio_controls(renderer,width,height,gain_level,volume_level,track_position,now,
                scratch?scratch->smooth_bands:NULL,
                right_scratch?right_scratch->smooth_bands:NULL,
                player.loaded,app.repeat,app.shuffle,
                (SDL_GetTicks()<skip_flash_until)?skip_flash:0,
                opt.source==SAUTI_MIC,
#ifdef SAUTI_HAS_PIPEWIRE
                recording.file!=NULL
#else
                0
#endif
                );
            browser_upper_panels(renderer,width,height,&browser,player.loaded,
                player_is_playing(&player),
                player_is_playing(&player) && !player_is_paused(&player),
                scratch?scratch->smooth_bands:NULL,
                right_scratch?right_scratch->smooth_bands:NULL);
        }
        SDL_RenderPresent(renderer);
        if(browser.selected_track[0]){
            const char *name=strrchr(browser.selected_track,'/');
            char title[400];
            char pos_text[32],len_text[32];
            double elapsed=player_position(&player);
            player_time(elapsed,pos_text,sizeof(pos_text));
            player_time(player.duration,len_text,sizeof(len_text));
            snprintf(title,sizeof(title),"Sayari Sauti - %.285s [%s / %s]",
                     name?name+1:browser.selected_track,pos_text,len_text);
            SDL_SetWindowTitle(window,title);
        }
        browser_paint(&browser);
        browser_save_paint(&browser);
        sauti_settings_paint(&settings,app.repeat,app.shuffle);
        sauti_picker_paint(&picker,SDL_GetTicks(),
            scratch?scratch->smooth_bands:NULL,
            right_scratch?right_scratch->smooth_bands:NULL);
        ++frames;
        if (opt.frames && frames >= opt.frames) break;
        SDL_Delay(16);
    }
#ifdef SAUTI_HAS_PIPEWIRE
    if(recording.file)sauti_record_end(capture,record_ring,&recording);
    sauti_audio_close(capture);
    free(record_ring);
#endif
    player_close(&player);
    sauti_picker_close(&picker);
    sauti_settings_close(&settings);
    browser_save_close(&browser);
    browser_close(&browser);
    free(ring);
    free(stereo_ring);
    free(right_scratch);
    free(left_history);
    free(right_history);
    if (texture) SDL_DestroyTexture(texture);
    free(pixels);
    free(scratch);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (failed) return 1;
    printf("Sayari Sauti: rendered %d frames (%s)\n", frames,
           opt.source == SAUTI_GENERATOR ? "generator" :
           opt.source == SAUTI_MONITOR ? "monitor" : "mic");
    return 0;
}
