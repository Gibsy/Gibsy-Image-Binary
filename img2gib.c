#define GIB_IMPLEMENTATION
#include "gib.h"

#include <png.h>
#include <jpeglib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  helpers  */

static int ends_with(const char*s,const char*suffix){
    size_t sl=strlen(s),tl=strlen(suffix);
    if(sl<tl)return 0;
    const char*p=s+sl-tl;
    while(*p&&*suffix){
        char a=*p,b=*suffix;
        if(a>='A'&&a<='Z')a+=32;
        if(b>='A'&&b<='Z')b+=32;
        if(a!=b)return 0;
        p++;suffix++;
    }
    return 1;
}

/*  PNG loader  */

static uint8_t*load_png(const char*path,int*w,int*h){
    FILE*fp=fopen(path,"rb");
    if(!fp){fprintf(stderr,"Cannot open: %s\n",path);return NULL;}
    png_structp png=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
    png_infop info=png_create_info_struct(png);
    if(setjmp(png_jmpbuf(png))){fclose(fp);return NULL;}
    png_init_io(png,fp);
    png_read_info(png,info);
    *w=(int)png_get_image_width(png,info);
    *h=(int)png_get_image_height(png,info);
    png_byte ct=png_get_color_type(png,info);
    png_byte bd=png_get_bit_depth(png,info);
    if(bd==16)png_set_strip_16(png);
    if(ct==PNG_COLOR_TYPE_PALETTE)png_set_palette_to_rgb(png);
    if(ct==PNG_COLOR_TYPE_GRAY||ct==PNG_COLOR_TYPE_GRAY_ALPHA)png_set_gray_to_rgb(png);
    if(ct&PNG_COLOR_MASK_ALPHA)png_set_strip_alpha(png);
    png_read_update_info(png,info);
    size_t rb=png_get_rowbytes(png,info);
    uint8_t*px=(uint8_t*)malloc((*h)*rb);
    png_bytep*rows=(png_bytep*)malloc((*h)*sizeof(png_bytep));
    for(int i=0;i<*h;i++)rows[i]=px+i*rb;
    png_read_image(png,rows);
    png_destroy_read_struct(&png,&info,NULL);
    fclose(fp);free(rows);
    return px;
}

/*  JPG loader  */

static uint8_t*load_jpg(const char*path,int*w,int*h){
    FILE*fp=fopen(path,"rb");
    if(!fp){fprintf(stderr,"Cannot open: %s\n",path);return NULL;}
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err=jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo,fp);
    jpeg_read_header(&cinfo,TRUE);
    cinfo.out_color_space=JCS_RGB;
    jpeg_start_decompress(&cinfo);
    *w=(int)cinfo.output_width;
    *h=(int)cinfo.output_height;
    int row_stride=(*w)*3;
    uint8_t*px=(uint8_t*)malloc((*h)*row_stride);
    while((int)cinfo.output_scanline<*h){
        uint8_t*row=px+cinfo.output_scanline*row_stride;
        jpeg_read_scanlines(&cinfo,&row,1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    return px;
}

static uint8_t*load_image(const char*path,int*w,int*h){
    if(ends_with(path,".jpg")||ends_with(path,".jpeg"))return load_jpg(path,w,h);
    if(ends_with(path,".png"))return load_png(path,w,h);
    uint8_t*px=load_png(path,w,h);
    if(px)return px;
    return load_jpg(path,w,h);
}

static void make_out_path(const char*in,char*out,size_t sz){
    snprintf(out,sz,"%s",in);
    char*dot=strrchr(out,'.');
    if(dot&&!strchr(dot,'/')&&!strchr(dot,'\\'))*dot=0;
    strncat(out,".gib",sz-strlen(out)-1);
}

/*  quality ask table  */

static int ask_quality(const char*name){
    printf("\n");
    printf("  File: %s\n\n",name);
    printf("  Choose quality:\n\n");
    printf("    0  - lossless\n");
    printf("         largest file size\n\n");
    printf("    3  - default\n");
    printf("         good balance\n\n");
    printf("    7  - maximum compression\n");
    printf("         smaller file, slight loss\n\n");
    printf("  Enter 0-10 and press Enter: ");
    fflush(stdout);
    char buf[32];
    if(!fgets(buf,sizeof(buf),stdin))return 3;
    int q=atoi(buf);
    if(q<0)q=0;
    if(q>10)q=10;
    return q;
}

/*  main  */

int main(int argc,char**argv){
    if(argc<2){
        fprintf(stderr,
            "Usage:\n"
            "  Drop a PNG or JPG file onto img2gib.exe\n"
            "  Or: img2gib.exe input.png [output.gib] [quality 0-10]\n");
#ifdef _WIN32
        printf("\nPress Enter to exit..."); fflush(stdout); getchar();
#endif
        return 1;
    }

    char in_path[1024], out_path[1024]="";
    int  quality=-1;

    snprintf(in_path,sizeof(in_path),"%s",argv[1]);

    if(argc>=3){
        char*end; long v=strtol(argv[2],&end,10);
        if(*end==0) quality=(int)v;
        else        snprintf(out_path,sizeof(out_path),"%s",argv[2]);
    }
    if(argc>=4) quality=atoi(argv[3]);

    if(out_path[0]==0) make_out_path(in_path,out_path,sizeof(out_path));

    const char*base=in_path;
    const char*sl=strrchr(in_path,'/');
    const char*bs=strrchr(in_path,'\\');
    if(sl) base=sl+1;
    if(bs&&bs+1>base) base=bs+1;

    if(quality<0) quality=ask_quality(base);

    printf("\n  Loading...\n");
    int w,h;
    uint8_t*pixels=load_image(in_path,&w,&h);
    if(!pixels){
        fprintf(stderr,"  Error: could not read image file\n");
#ifdef _WIN32
        printf("\nPress Enter to exit..."); fflush(stdout); getchar();
#endif
        return 1;
    }

    printf("  Encoding %dx%d...\n",w,h);
    size_t gib_size;
    uint8_t*gib=gib_encode(pixels,w,h,quality,&gib_size);
    free(pixels);

    FILE*fp=fopen(out_path,"wb");
    if(!fp){
        fprintf(stderr,"  Error: cannot write %s\n",out_path);
        free(gib);
#ifdef _WIN32
        printf("\nPress Enter to exit..."); fflush(stdout); getchar();
#endif
        return 1;
    }
    fwrite(gib,1,gib_size,fp); fclose(fp); free(gib);

    long raw=(long)w*h*3;
    double pct=(double)gib_size/raw*100.0;
    const char*qname=quality==0?"lossless":quality<=3?"good":"high compression";

    printf("\n");
    printf("  Size:    %d x %d px\n",w,h);
    printf("  Raw:     %.0f KB\n",raw/1024.0);
    printf("  GIB:     %.0f KB  (%.1f%%)\n",gib_size/1024.0,pct);
    printf("  Quality: %d  (%s)\n",quality,qname);
    printf("  Output:  %s\n",out_path);
    printf("  Done!\n");

#ifdef _WIN32
    printf("\nPress Enter to exit..."); fflush(stdout); getchar();
#endif
    return 0;
}