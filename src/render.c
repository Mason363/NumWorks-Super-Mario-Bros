/* SPDX-License-Identifier: GPL-3.0-only */
#include "game.h"
#include "palette.h"
#include <string.h>
#define RGB(r,g,b) (((r)>>3)<<11|((g)>>2)<<5|((b)>>3))
#define WHITE RGB(255,254,255)
#define NAVY RGB(13,19,31)
#define GOLD RGB(247,186,63)
#define GREY RGB(180,192,207)
#define FOCUS RGB(54,68,91)
/* The NES picture's rows 8 to 231, as a television showed them, fill the screen's first 224 rows between two
 * borders; the 16 rows under it hold the key line. Everything is drawn in NES coordinates, 8 rows at a time:
 * a strip covers rows top to top+7 and columns clip_l to clip_r-1 (the picture's 256, or the whole width). */
#define PICTURE_TOP 8
#define PICTURE_BOTTOM 232
static uint16_t strip[320*8],palette[32];
static int top,clip_l=32,clip_r=288;
static bool invalid=true;
void sm_invalidate(void) {invalid=true;}
static void pixel(int x,int y,uint16_t c) {
  if(x>=clip_l&&x<clip_r&&y>=top&&y<top+8)strip[(y-top)*(clip_r-clip_l)+x-clip_l]=c;
}
static void rect(int x,int y,int w,int h,uint16_t c) {
  int l=x<clip_l?clip_l:x,r=x+w>clip_r?clip_r:x+w,t=y<top?top:y,b=y+h>top+8?top+8:y+h;
  for(int yy=t;yy<b;++yy)for(int xx=l;xx<r;++xx)strip[(yy-top)*(clip_r-clip_l)+xx-clip_l]=c;
}
static void frame(int x,int y,int w,int h,int t,uint16_t c) {
  rect(x,y,w,t,c);rect(x,y+h-t,w,t,c);rect(x,y,t,h,c);rect(x+w-t,y,t,h,c);
}
/* One 8x8 pattern: colours 1 to 3 from the palette, or all from `override`, or from `fixed`. */
static void pattern(unsigned idx,int x,int y,unsigned pal,bool flipx,bool flipy,uint16_t override,
                      const uint16_t *fixed) {
  if(y>=top+8||y+8<=top||x>=clip_r||x+8<=clip_l)return;
  const uint8_t *p=smb_patterns+(idx&511)*16;
  int start=top>y?top-y:0,end=top+8-y;if(end>8)end=8;
  for(int yy=start;yy<end;++yy) {
    int row=flipy?7-yy:yy;unsigned lo=p[row],hi=p[row+8];
    for(int xx=0;xx<8;++xx) {
      unsigned bit=flipx?xx:7-xx,v=((lo>>bit)&1)|(((hi>>bit)&1)<<1);
      if(v)pixel(x+xx,y+yy,fixed?fixed[v]:override?override:palette[(pal&7)*4+v]);
    }
  }
}
static void tile(unsigned idx,int x,int y,unsigned pal,bool flipx,bool flipy,uint16_t override) {
  pattern(idx,x,y,pal,flipx,flipy,override,0);
}
/* The game's own letters: digits, A to Z, - ! and . (others are blanks) */
static void glyph(int x,int y,char c,uint16_t color) {
  unsigned ch=(unsigned char)c;
  if(ch>='a'&&ch<='z')ch-=32;
  unsigned idx=ch>='0'&&ch<='9'?ch-'0':ch>='A'&&ch<='Z'?ch-'A'+10:36;
  if(ch=='-')idx=40;
  if(ch=='!')idx=43;
  if(ch=='.')idx=175;
  tile(256+idx,x,y,0,false,false,color);
}
static void text(int x,int y,const char *s,uint16_t c) {for(;*s;++s,x+=8)glyph(x,y,*s,c);}
static void centered(int y,const char *s,uint16_t c) {text(160-(int)strlen(s)*4,y,s,c);}
static void sprites(const sm_game *g,bool behind) {
  for(int i=63;i>=0;--i) {
    const uint8_t *p=g->sprites+i*4;unsigned a=p[2];
    if(((a&32)!=0)!=behind)continue;
    tile(p[1],32+p[3],1+p[0],4+(a&3),(a&64)!=0,(a&128)!=0,0);
  }
}
static void background(const sm_game *g) {
  for(int yy=top;yy<top+8;yy+=8) {
    unsigned ytile=(unsigned)yy/8;
    unsigned scroll=yy<32?0:g->state.ppu.scroll_x;
    unsigned start=scroll&255,bank=(scroll>>8)&1;
    int sx=32-(int)(start&7);
    for(unsigned col=start/8;sx<288;++col,sx+=8) {
      unsigned nt=bank^((col/32)&1),x=col&31;
      const uint8_t *p=g->state.ppuram+nt*1024;
      unsigned a=p[960+(ytile/4)*8+x/4];a>>=((ytile/2)&1)*4+((x/2)&1)*2;
      tile(256+p[ytile*32+x],sx,yy,a&3,false,false,0);
    }
  }
}
/* the title screen's mushroom points at the chosen line */
static const uint16_t mushroom[4]={0,RGB(0xfe,0xcc,0xc5),RGB(0x99,0x4e,0x00),RGB(0,0,0)};
static void choice(int y,const char *label,unsigned id,unsigned selected) {
  bool on=id==selected;
  if(on)rect(76,y-5,168,18,FOCUS),frame(76,y-5,168,18,1,GOLD),pattern(256+0xce,88,y,0,false,false,0,mushroom);
  text(108,y,label,on?WHITE:GREY);
}
static void panel(int x,int y,int w,int h) {rect(x,y,w,h,NAVY);rect(x,y,w,3,GOLD);}
static void overlay(unsigned view,unsigned selected,bool failed) {
  if(view==SM_VIEW_PAUSE) {
    panel(64,52,192,140);centered(66,"PAUSED",WHITE);
    choice(92,"RESUME",0,selected);choice(114,"NEW GAME",1,selected);
    choice(136,"CONTROLS",2,selected);choice(158,"QUIT",3,selected);
    if(failed)centered(178,"SAVE FAILED",GOLD);
  } else if(view==SM_VIEW_GUIDE) {
    static const char *const keys[][2]={{"ARROWS","MOVE AND DUCK"},{"OK","JUMP AND SWIM"},{"SHIFT","RUN AND FIRE"},
      {"EXE","START AND PAUSE"},{"ALPHA","1 OR 2 PLAYERS"},{"BACK","MENU"},{"HOME","SAVE AND QUIT"}};
    panel(47,46,226,148);centered(60,"CONTROLS",WHITE);
    for(int i=0;i<7;++i)text(60,82+i*15,keys[i][0],WHITE),text(124,82+i*15,keys[i][1],GREY);
  } else if(view==SM_VIEW_RESTART||view==SM_VIEW_QUIT||view==SM_VIEW_FAULT) {
    static const char *const lines[][2]={{"NEW GAME","THIS GAME WILL BE LOST"},{"QUIT","YOUR GAME IS SAVED"},
      {"GAME INTERRUPTED","YOUR LAST SAVE IS KEPT"}};
    unsigned i=view==SM_VIEW_RESTART?0:view==SM_VIEW_QUIT?1:2;
    panel(56,90,208,60);centered(104,lines[i][0],i==2?GOLD:WHITE);centered(126,lines[i][1],GREY);
  } else {
    panel(47,82,226,76);centered(96,"SAVE FAILED",GOLD);
    centered(118,"THE STORAGE MAY BE FULL",GREY);centered(136,"BACK QUITS WITHOUT SAVING",GREY);
  }
}
/* The keys that matter now: groups three spaces apart, each a key (white) and what it does. */
static const char *keyline(const sm_game *g,unsigned view) {
  const uint8_t *ram=g->state.rammem;
  switch(view) {
  case SM_VIEW_GAME:
    if(ram[0x770]==0)return ram[0x7fc]?"EXE START   SHIFT WORLD   ALPHA PLAYERS":"EXE START   ALPHA 1 OR 2 PLAYERS";
    if(ram[0x770]==2&&ram[0x75f]==7)return "SHIFT BUTTON B";
    if(ram[0x776]&1)return "EXE RESUME   BACK MENU";
    return "OK JUMP   SHIFT RUN   BACK MENU";
  case SM_VIEW_PAUSE:return "OK SELECT   BACK RESUME";
  case SM_VIEW_GUIDE:return "OK CONTINUE";
  case SM_VIEW_RESTART:return "OK START OVER   BACK CANCEL";
  case SM_VIEW_QUIT:return "OK QUIT   BACK CANCEL";
  case SM_VIEW_FAULT:return "OK CONTINUE   HOME QUIT";
  default:return "OK TRY AGAIN   1 RESUME   BACK QUIT";
  }
}
static void draw_keys(const char *s,int y) {
  int x=160-(int)strlen(s)*4;bool key=true;
  for(const char *p=s;*p;++p,x+=8) {
    if(*p==' ') {if(p[1]==' ')key=true;else if(p[-1]!=' ')key=false;continue;}
    glyph(x,y,*p,key?WHITE:GREY);
  }
}
static unsigned old_view=99,old_selected;
static bool old_failed;
static uint32_t old_ticks=UINT32_MAX;
void sm_render(const sm_game *g,unsigned view,unsigned selected,bool failed,sm_push push) {
  if(!invalid&&old_view==view&&old_selected==selected&&old_failed==failed&&old_ticks==g->ticks)return;
  for(unsigned i=0;i<32;++i) {
    unsigned c=g->state.ppuram[0x800+i]&63;
    palette[i]=(uint16_t)RGB(PALETTE_BUILTIN[c][0],PALETTE_BUILTIN[c][1],PALETTE_BUILTIN[c][2]);
  }
  if(invalid) { /* the borders beside the picture */
    for(unsigned i=0;i<32*8;++i)strip[i]=NAVY;
    for(int y=0;y<PICTURE_BOTTOM-PICTURE_TOP;y+=8)push(0,y,32,8,strip),push(288,y,32,8,strip);
  }
  clip_l=32;clip_r=288;
  for(top=PICTURE_TOP;top<PICTURE_BOTTOM;top+=8) {
    rect(32,top,256,8,palette[16]);
    if(g->state.ppu.screen_on) {sprites(g,true);background(g);sprites(g,false);}
    /* A small adaptation credit outside the original scene on the title. */
    if(g->state.rammem[0x770]==0&&g->state.rammem[0x772]>=3) {
      rect(104,196,112,11,palette[16]);centered(197,"BY BOBYDELUXE",WHITE);
    }
    if(view!=SM_VIEW_GAME)overlay(view,selected,failed);
    push(32,top-PICTURE_TOP,256,8,strip);
  }
  clip_l=0;clip_r=320;
  const char *keys=keyline(g,view);
  for(top=PICTURE_BOTTOM;top<PICTURE_BOTTOM+16;top+=8) {
    rect(0,top,320,8,NAVY);draw_keys(keys,PICTURE_BOTTOM+4);
    push(0,top-PICTURE_TOP,320,8,strip);
  }
  clip_l=32;clip_r=288;
  old_view=view;old_selected=selected;old_failed=failed;old_ticks=g->ticks;invalid=false;
}
