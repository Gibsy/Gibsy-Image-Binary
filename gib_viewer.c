#define GIB_IMPLEMENTATION
#include "gib.h"

#include <SDL2/SDL.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W   900
#define WIN_H   650
#define BAR_H   44
#define BOT_H   52

#define C_BG       0x141416ff
#define C_BAR      0x222224ff
#define C_DIV      0x333336ff
#define C_TEXT     0xf2f2f4ff
#define C_MUTED    0x78787eff
#define C_BTN      0x38383cff
#define C_BTN_HOV  0x505056ff
#define C_BTN_BRD  0x555558ff
#define C_GREEN    0x32d74bff
#define C_GREEN_BG 0x0c2216ff
#define C_RED      0xff3b30ff
#define C_RED_BG   0x2a0c0cff

static uint8_t    *cur_pixels = NULL;
static int         cur_w = 0, cur_h = 0;
static SDL_Texture*cur_tex    = NULL;
static char        cur_path[1024] = "";
static char        cur_name[256]  = "";

static char   flash_msg[160] = "";
static Uint32 flash_until    = 0;
static int    flash_ok       = 1;
static int    btn_hov        = 0;

/*  drawing helpers  */

static void set_col(SDL_Renderer*r, Uint32 rgba){
    SDL_SetRenderDrawColor(r,(rgba>>24)&0xff,(rgba>>16)&0xff,(rgba>>8)&0xff,rgba&0xff);
}
static void fill_rect(SDL_Renderer*r,int x,int y,int w,int h,Uint32 c){
    set_col(r,c);SDL_Rect rc={x,y,w,h};SDL_RenderFillRect(r,&rc);
}
static void draw_border(SDL_Renderer*r,int x,int y,int w,int h,Uint32 c){
    set_col(r,c);SDL_Rect rc={x,y,w,h};SDL_RenderDrawRect(r,&rc);
}

/*  5x7 bitmap font  */

static const Uint8 F57[][5]={
{0,0,0,0,0},{0,0,95,0,0},{0,7,0,7,0},{20,127,20,127,20},
{36,42,127,42,18},{35,19,8,100,98},{54,73,85,34,80},{0,5,3,0,0},
{0,28,34,65,0},{0,65,34,28,0},{20,8,62,8,20},{8,8,62,8,8},
{0,80,48,0,0},{8,8,8,8,8},{0,96,96,0,0},{32,16,8,4,2},
{62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},
{24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},
{54,73,73,73,54},{6,73,73,41,30},{0,54,54,0,0},{0,86,54,0,0},
{8,20,34,65,0},{20,20,20,20,20},{0,65,34,20,8},{2,1,81,9,6},
{50,73,121,65,62},{126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},
{127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},
{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},
{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},
{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},
{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},
{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67},{0,127,65,65,0},
{2,4,8,16,32},{0,65,65,127,0},{4,2,1,2,4},{64,64,64,64,64},
{0,1,2,4,0},{32,84,84,84,120},{127,72,68,68,56},{56,68,68,68,32},
{56,68,68,72,127},{56,84,84,84,24},{8,126,9,1,2},{12,82,82,82,62},
{127,8,4,4,120},{0,68,125,64,0},{32,64,68,61,0},{127,16,40,68,0},
{0,65,127,64,0},{124,4,24,4,120},{124,8,4,4,120},{56,68,68,68,56},
{124,20,20,20,8},{8,20,20,24,124},{124,8,4,4,8},{72,84,84,84,32},
{4,63,68,64,32},{60,64,64,32,124},{28,32,64,32,28},{60,64,48,64,60},
{68,40,16,40,68},{12,80,80,80,60},{68,100,84,76,68}
};

static void draw_char(SDL_Renderer*r,int x,int y,char c,int sc,Uint32 col){
    int i=(unsigned char)c-32;
    if(i<0||i>=(int)(sizeof(F57)/5))return;
    set_col(r,col);
    for(int cx=0;cx<5;cx++){
        Uint8 b=F57[i][cx];
        for(int ry=0;ry<7;ry++)
            if(b&(1<<ry)){SDL_Rect rc={x+cx*sc,y+ry*sc,sc,sc};SDL_RenderFillRect(r,&rc);}
    }
}
static void draw_str(SDL_Renderer*r,int x,int y,const char*s,int sc,Uint32 col){
    while(*s){draw_char(r,x,y,*s,sc,col);x+=6*sc;s++;}
}
static int str_w(const char*s,int sc){return(int)strlen(s)*6*sc;}

/*  gib loading  */

static uint8_t*read_file(const char*path,size_t*sz){
    FILE*fp=fopen(path,"rb");if(!fp)return NULL;
    fseek(fp,0,SEEK_END);*sz=(size_t)ftell(fp);fseek(fp,0,SEEK_SET);
    uint8_t*buf=(uint8_t*)malloc(*sz);
    if(fread(buf,1,*sz,fp)!=*sz){free(buf);fclose(fp);return NULL;}
    fclose(fp);return buf;
}

static void load_image(SDL_Renderer*ren, const char*path){
    if(cur_tex){SDL_DestroyTexture(cur_tex);cur_tex=NULL;}
    if(cur_pixels){free(cur_pixels);cur_pixels=NULL;}

    size_t sz;
    uint8_t*data=read_file(path,&sz);
    if(!data){
        snprintf(flash_msg,sizeof(flash_msg),"Cannot open file");
        flash_ok=0;flash_until=SDL_GetTicks()+3000;return;
    }
    cur_pixels=gib_decode(data,sz,&cur_w,&cur_h);
    free(data);
    if(!cur_pixels){
        snprintf(flash_msg,sizeof(flash_msg),"Invalid .gib file");
        flash_ok=0;flash_until=SDL_GetTicks()+3000;return;
    }

    snprintf(cur_path,sizeof(cur_path),"%s",path);
    const char*base=path;
    const char*sl=strrchr(path,'/');
    const char*bs=strrchr(path,'\\');
    if(sl&&sl>=base)base=sl+1;
    if(bs&&bs>=base)base=bs+1;
    snprintf(cur_name,sizeof(cur_name),"%s",base);

    cur_tex=SDL_CreateTexture(ren,SDL_PIXELFORMAT_RGB24,SDL_TEXTUREACCESS_STATIC,cur_w,cur_h);
    SDL_UpdateTexture(cur_tex,NULL,cur_pixels,cur_w*3);
}

/*  png saving  */

static void do_save(void){
    if(!cur_pixels||cur_path[0]==0)return;

    char out[1030];
    snprintf(out,sizeof(out),"%s",cur_path);
    char*dot=strrchr(out,'.');
    if(dot&&!strchr(dot,'/')&&!strchr(dot,'\\'))*dot=0;
    strncat(out,".png",sizeof(out)-strlen(out)-1);

    FILE*fp=fopen(out,"wb");
    if(!fp){
        snprintf(flash_msg,sizeof(flash_msg),"Save failed!");
        flash_ok=0;flash_until=SDL_GetTicks()+2500;return;
    }

    png_structp png=png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
    png_infop info=png_create_info_struct(png);
    png_init_io(png,fp);
    png_set_IHDR(png,info,cur_w,cur_h,8,PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png,info);
    for(int i=0;i<cur_h;i++)png_write_row(png,cur_pixels+i*cur_w*3);
    png_write_end(png,NULL);
    png_destroy_write_struct(&png,&info);
    fclose(fp);

    const char*base=out;
    const char*sl=strrchr(out,'/');
    const char*bs=strrchr(out,'\\');
    if(sl)base=sl+1;
    if(bs&&bs+1>base)base=bs+1;
    snprintf(flash_msg,sizeof(flash_msg),"Saved: %s",base);
    flash_ok=1;flash_until=SDL_GetTicks()+3000;
}

static SDL_Rect save_btn(int ww,int wh){
    int bw=160,bh=30;
    return (SDL_Rect){(ww-bw)/2,(wh-BOT_H)+(BOT_H-bh)/2,bw,bh};
}

/*  main  */

int main(int argc,char**argv){
    if(argc<2){fprintf(stderr,"Drop a .gib file onto gib_viewer.exe\n");return 1;}

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window*win=SDL_CreateWindow("GIB Viewer",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,WIN_W,WIN_H,
        SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer*ren=SDL_CreateRenderer(win,-1,
        SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);

    load_image(ren,argv[1]);

    int running=1;
    SDL_Event ev;
    while(running){
        int mx,my;SDL_GetMouseState(&mx,&my);
        int ww,wh;SDL_GetWindowSize(win,&ww,&wh);
        SDL_Rect sb=save_btn(ww,wh);
        btn_hov=(mx>=sb.x&&mx<sb.x+sb.w&&my>=sb.y&&my<sb.y+sb.h)?1:0;

        while(SDL_PollEvent(&ev)){
            if(ev.type==SDL_QUIT)running=0;
            if(ev.type==SDL_KEYDOWN){
                if(ev.key.keysym.sym==SDLK_ESCAPE||ev.key.keysym.sym==SDLK_q)running=0;
                if(ev.key.keysym.sym==SDLK_s)do_save();
            }
            if(ev.type==SDL_MOUSEBUTTONDOWN&&ev.button.button==SDL_BUTTON_LEFT){
                SDL_Rect sb2=save_btn(ww,wh);
                int bx=ev.button.x,by=ev.button.y;
                if(bx>=sb2.x&&bx<sb2.x+sb2.w&&by>=sb2.y&&by<sb2.y+sb2.h)
                    do_save();
            }
        }

        set_col(ren,C_BG);SDL_RenderClear(ren);

        fill_rect(ren,0,0,ww,BAR_H,C_BAR);
        fill_rect(ren,0,BAR_H-1,ww,1,C_DIV);
        if(cur_name[0]){
            int tw=str_w(cur_name,2);
            int tx=(ww-tw)/2;if(tx<8)tx=8;
            draw_str(ren,tx,BAR_H/2-7,cur_name,2,C_TEXT);
            char info[32];snprintf(info,sizeof(info),"%dx%d",cur_w,cur_h);
            draw_str(ren,ww-str_w(info,1)-10,BAR_H/2-3,info,1,C_MUTED);
        }

        if(cur_tex){
            int vx=0,vy=BAR_H,vw=ww,vh=wh-BAR_H-BOT_H;
            float sx=(float)vw/cur_w,sy=(float)vh/cur_h;
            float s=sx<sy?sx:sy;
            int dw=(int)(cur_w*s),dh=(int)(cur_h*s);
            SDL_Rect dst={vx+(vw-dw)/2,vy+(vh-dh)/2,dw,dh};
            SDL_RenderCopy(ren,cur_tex,NULL,&dst);
        } else {
            const char*m="Drop a .gib file onto gib_viewer.exe";
            draw_str(ren,(ww-str_w(m,1))/2,wh/2-3,m,1,C_MUTED);
        }

        fill_rect(ren,0,wh-BOT_H,ww,BOT_H,C_BAR);
        fill_rect(ren,0,wh-BOT_H,ww,1,C_DIV);
        {
            SDL_Rect b=save_btn(ww,wh);
            fill_rect(ren,b.x,b.y,b.w,b.h,btn_hov?C_BTN_HOV:C_BTN);
            draw_border(ren,b.x,b.y,b.w,b.h,btn_hov?0x8888aaff:C_BTN_BRD);
            const char*lbl="Save as PNG  [S]";
            int tw=str_w(lbl,1);
            draw_str(ren,b.x+(b.w-tw)/2,b.y+(b.h-7)/2,lbl,1,C_TEXT);
        }

        if(SDL_GetTicks()<flash_until){
            Uint32 bc=flash_ok?C_GREEN_BG:C_RED_BG;
            Uint32 tc=flash_ok?C_GREEN:C_RED;
            int fw=str_w(flash_msg,1)+24,fh=24;
            int fx=ww-fw-14,fy=wh-BOT_H-fh-8;
            fill_rect(ren,fx,fy,fw,fh,bc);
            draw_border(ren,fx,fy,fw,fh,tc);
            draw_str(ren,fx+12,fy+(fh-7)/2,flash_msg,1,tc);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(8);
    }

    if(cur_tex)SDL_DestroyTexture(cur_tex);
    if(cur_pixels)free(cur_pixels);
    SDL_DestroyRenderer(ren);SDL_DestroyWindow(win);SDL_Quit();
    return 0;
}