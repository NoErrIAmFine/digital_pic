#include <linux/fb.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "display_manager.h"
#include "debug_manager.h"

/* 此函数主要两个功能，1.读取屏幕数据并记录，2.为屏幕映射一段内存 */
static int lcd_display_init(struct display_struct *display)
{
    int fd;
    int ret;
    struct fb_var_screeninfo vinfo;
    fd = open(DEFAULT_LCD_DEVICE,O_RDWR);
    if(fd < 0){
        DP_ERR("open lcd device failed!\n");
        return -1;
    }
    ret = ioctl(fd,FBIOGET_VSCREENINFO,&vinfo);
    if(ret){
        DP_ERR("ioctl:get var screen info failed!\n");
        return -1;
    }
    display->xres = vinfo.xres;
    display->yres = vinfo.yres;
    display->bpp  = vinfo.bits_per_pixel;
    display->line_bytes = display->xres * display->bpp / 8;
    display->total_bytes = display->line_bytes * display->yres;
    display->buf = mmap(NULL,display->total_bytes,PROT_READ | PROT_WRITE ,MAP_SHARED,fd,0);
    if(display->buf == MAP_FAILED){
        DP_ERR("mmap failed");
        return -1;
    }

    close(fd);
    return 0;

}

static int lcd_exit(struct display_struct *display)
{
    /* 除了取消映射，似乎也没有其他事可作了 */
    return munmap(display->buf,display->total_bytes);
}

static int lcd_flush_buf(struct display_struct *display,const unsigned char *buf,int len)
{
    if(len <= display->total_bytes){
        memcpy(display->buf,buf,len);
        return 0;
    }
    return -1;
}   
/* 将整个屏幕清为某种颜色 */
static int lcd_clear_buf(struct display_struct *display,int color)
{
    unsigned short lcd_color;
    unsigned short *lcd_buf;
    int i,j;

    lcd_buf = (unsigned short *)display->buf;
    lcd_color = (unsigned short)color;

    for(i = 0 ; i < display->xres ; i++){
        for(j = 0 ; j < display->yres ; j++){
            *lcd_buf = lcd_color;
            lcd_buf++;
        }
    }
    return 0;
}

/* 将某个区域清为某种颜色 */
static int lcd_clear_buf_region(struct display_struct *display,struct display_region *region)
{
    return 0;
}

/* 将某一区域数据到屏幕的显存中，处理bpp之间的转换 */
static int lcd_merge_region(struct display_struct *display,struct display_region *region)
{
    int width,height;
    int line_bytes;
    int i,j;
    unsigned char *src_buf,*dst_buf;
    // DP_INFO("begin merge display region!\n");

    /* 屏幕bpp为16，处理24和32到16的bpp转换 */
    if((display->bpp != region->data->bpp) && (region->data->bpp != 24) && (region->data->bpp != 32)){
        /* 这个函数不处理bpp之间的转换 */
        DP_ERR("%s:unsupported bpp!\n",__func__);
        return -1;
    }

    /* 检查显示区域是否在屏幕范围内 */
    if((region->x_pos >= display->xres) || (region->y_pos >= display->yres)){
        DP_ERR("invalid display region!\n");
        return -1;
    }
    if((region->x_pos + region->data->width - 1) >= display->xres){
        width = display->xres - region->x_pos;
    }else{
        width = region->data->width;
    }

    if((region->y_pos + region->data->height - 1) >= display->yres){
        height = display->yres - region->y_pos;
    }else{
        height = region->data->height;
    }

    dst_buf = display->buf + region->y_pos * display->line_bytes + region->x_pos * display->bpp / 8;
    src_buf = region->data->buf;

    /* 根据bpp的不同作不同处理 */
    if(display->bpp == region->data->bpp){
        /* 根据数据不同的储存方式作不同处理 */
        if(region->data->in_rows){
            unsigned char **row_pointers = region->data->rows_buf;
            line_bytes = width * display->bpp / 8;
            for(i = 0 ; i < height ; i++){
                src_buf = row_pointers[i];
                memcpy(dst_buf,src_buf,line_bytes);
                dst_buf += display->line_bytes;
            }
        }else{
            line_bytes = width * display->bpp / 8;
            for(i = 0 ; i < height ; i++){
                memcpy(dst_buf,src_buf,line_bytes);
                dst_buf += display->line_bytes;
                src_buf += region->data->line_bytes;
            }
        }
    }else if(24 == region->data->bpp){
        /* 要输入缓存的数据格式bpp为24 */
        /* 数据按行存储 */
        if(region->data->in_rows){
            unsigned short red,green,blue,color;
            unsigned char **row_pointers = region->data->rows_buf;
            for(i = 0 ; i < height ; i++){
                src_buf = row_pointers[i];
                for(j = 0 ; j < width ; j++){
                    red   = src_buf[j * 3] >> 3;
                    green = src_buf[j * 3 + 1] >> 2;
                    blue  = src_buf[j * 3 + 2] >> 3;
                    color = (red << 11) | (green << 5) | blue;
                    *(unsigned short *)(dst_buf + 2 * j) = color;
                }
                dst_buf += display->line_bytes;
            }
        }else{
            /* 数据整块存储 */
            unsigned short red,green,blue,color;
            for(i = 0 ; i < height ; i++){
                for(j = 0 ; j < width ; j++){
                    red   = src_buf[j * 3] >> 3;
                    green = src_buf[j * 3 + 1] >> 2;
                    blue  = src_buf[j * 3 + 2] >> 3;
                    color = (red << 11) | (green << 5) | blue;
                    *(unsigned short *)(dst_buf + 2 * j) = color;
                }
                src_buf += region->data->line_bytes;
                dst_buf += display->line_bytes;
            }
        }
    }else if(32 == region->data->bpp){
        /* 要刷入显存的数据格式bpp为32 */

        /* bpp为32却不含alpha通道，直接退出 */
        if(!region->data->has_alpha){
            DP_WARNING("%s:bpp is 32 ,but don't have alpha channel!\n",__func__);
            return -1;
        }
        /* 数据按行存储 */
        if(region->data->in_rows){
            unsigned short red,green,blue,color;
            unsigned char **row_pointers = region->data->rows_buf;
            for(i = 0 ; i < height ; i++){
                src_buf = row_pointers[i];
                for(j = 0 ; j < width ; j++){
                    red   = src_buf[j * 4 + 1] >> 3;
                    green = src_buf[j * 4 + 2] >> 2;
                    blue  = src_buf[j * 4 + 3] >> 3;
                    color = (red << 11) | (green << 5) | blue;
                    *(unsigned short *)(dst_buf + 2 * j) = color;
                }
                dst_buf += display->line_bytes;
            }
        }else{
            /* 数据整块存储 */
            unsigned short red,green,blue,color;
            for(i = 0 ; i < height ; i++){
                for(j = 0 ; j < width ; j++){
                    red   = src_buf[j * 4 + 1] >> 3;
                    green = src_buf[j * 4 + 2] >> 2;
                    blue  = src_buf[j * 4 + 3] >> 3;
                    color = (red << 11) | (green << 5) | blue;
                    *(unsigned short *)(dst_buf + 2 * j) = color;
                }
                src_buf += region->data->line_bytes;
                dst_buf += display->line_bytes;
            }
        }
    }
    return 0;
}



static struct display_struct lcd_display = {
    .name = "lcd",
    .init = lcd_display_init,
    .exit = lcd_exit,
    .flush_buf  = lcd_flush_buf,
    .clear_buf  = lcd_clear_buf,
    .clear_buf_region = lcd_clear_buf_region,
    .merge_region     = lcd_merge_region,
    .is_enable = 1,
};

int lcd_init(void)
{
    int ret;
    if((ret = register_display_struct(&lcd_display))){
        return ret;
    }
    
    /* 将此设备设为默认设备 */
    set_default_display(&lcd_display);
    return 0;
}