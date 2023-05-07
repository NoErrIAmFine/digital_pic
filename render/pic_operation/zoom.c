#include "debug_manager.h"
#include "pic_operation.h"
#include "render.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* @description : 缩放图像，且只能缩小
 * @param : dst_data - 缩放后的图像，必须在此参数内传入要缩放到的大小信息；可以在此参数指定bpp，支持的bpp有16，24，32，
 * 如果不指定(bpp为0），则与源数据bpp相同；此参数可含指向已分配内存的指针，如果该指针为空，该函数会负责分配内存
 * @param : src_data - 原始数据，支持的bpp有16，24，32
 * @return : 0 - 成功 ； 其他值 - 失败
 */
int pic_zoom(struct pixel_data *dst_data,struct pixel_data *src_data)
{
    int i,j;
    int src_width,src_height;
    int dst_width,dst_height;
    unsigned char *src_line_buf,*dst_line_buf;
    float scale_x,scale_y;
    int bytes_per_pixel,src_y;
    int scale_x_table[dst_data->width];
    unsigned char src_red,src_green,src_blue;
    unsigned char dst_red,dst_green,dst_blue;
    unsigned char red,green,blue;
    unsigned int color,orig_color;
    unsigned short color_16;
    unsigned char alpha;

    /* 检查一些条件 */ 
    if(!dst_data->width || !dst_data->height || dst_data->width > src_data->width || dst_data->height > src_data->height){
        DP_WARNING("%s:err!\n",__func__);
        return -1;
    }
    /* bpp只支持16、24、32的转换，更多的情况暂不考虑 */
    if((dst_data->bpp != 16 && dst_data->bpp != 24 && dst_data->bpp != 32 && dst_data->bpp != 0) || \
       (src_data->bpp != 16 && src_data->bpp != 24 && src_data->bpp != 32)){
        DP_WARNING("%s:imcompatibel bpp,zoomed bpp %d,src bpp %d!\n",__func__,dst_data->bpp,dst_data->bpp);
        return -1;
    }
    
    dst_width   = dst_data->width;
    dst_height  = dst_data->height;
    src_width   = src_data->width;
    src_height  = src_data->height;

    /* 检查内存，如果分配了内存，检查大小是否符合要求 */
    if(dst_data->buf || dst_data->in_rows){
        if((dst_data->width * dst_data->bpp / 8) >= dst_data->line_bytes || \
           ((dst_data->width * dst_data->bpp / 8) * dst_data->height) >= dst_data->total_bytes){
            //内存过小，重新分配内存
            dst_data->buf = NULL;
            dst_data->rows_buf = NULL;
            dst_data->in_rows = 0;
        }
    }
    
    if(!dst_data->buf && !dst_data->in_rows){
        /* 确定目标区域的内存，如果未分配则分配 */
        dst_data->rows_buf = NULL;
        dst_data->line_bytes = dst_data->width * dst_data->bpp / 8;
        dst_data->total_bytes = dst_data->line_bytes * dst_data->height;
        if(NULL == (dst_data->buf = malloc(dst_data->total_bytes))){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        clear_pixel_data(dst_data,BACKGROUND_COLOR);
    }

    /* 开始缩放操作 */
    scale_x = (float)src_width / dst_width;
    for(i = 0 ; i < dst_width ; i++){
        scale_x_table[i] = i * scale_x;
    }

    scale_y = (float)src_height / dst_height;
    bytes_per_pixel = dst_data->bpp / 8;
    
    for(i = 0 ; i < dst_height ; i++){
        src_y = i * scale_y;
        if(src_data->in_rows){
            src_line_buf = src_data->rows_buf[src_y];
        }else{
            src_line_buf = src_data->buf + src_data->line_bytes * src_y;
        }
        if(dst_data->in_rows){
            dst_line_buf = dst_data->rows_buf[i];
        }else{
            dst_line_buf = dst_data->buf + dst_data->line_bytes * i;
        }
        
        /* 因为兼容几种bpp，这里写的又臭又长 */
        switch(dst_data->bpp){
        case 16:
            switch(src_data->bpp){
            case 16:
                for(j = 0 ; j < dst_width ; j++){
                    *(unsigned short *)dst_line_buf = *(unsigned short *)(src_line_buf + (2 * scale_x_table[j]));
                    dst_line_buf += 2;
                }
                break;
            case 24:
                for(j = 0 ; j < dst_width ; j++){
                    // printf("scale_x_table[j]-%d\n",scale_x_table[j]);
                    src_red   = (src_line_buf[3 * scale_x_table[j]] >> 3);
                    src_green = (src_line_buf[3 * scale_x_table[j] + 1] >> 2);
                    src_blue  = (src_line_buf[3 * scale_x_table[j] + 2] >> 3);
                    color_16 = (src_red << 11 | src_green << 5 | src_blue);
                    *(unsigned short *)dst_line_buf = color_16;
                    // *(unsigned short *)dst_line_buf = 0xbb;
                    dst_line_buf += 2;
                }
                break;
            case 32:
                for(j = 0 ; j < dst_width ; j++){
                    alpha     = src_line_buf[4 * scale_x_table[j]];
                    src_red   = src_line_buf[4 * scale_x_table[j] + 1] >> 3;
                    src_green = src_line_buf[4 * scale_x_table[j] + 2] >> 2;
                    src_blue  = src_line_buf[4 * scale_x_table[j] + 3] >> 3;
                    color_16 = *(unsigned short *)dst_line_buf;
                    dst_red   = (color_16 >> 11) & 0x1f;
                    dst_green = (color_16 >> 5) & 0x3f;
                    dst_blue  = color_16 & 0x1f;
                    /* 根据透明度合并颜色,公式:显示颜色 = 源像素颜色 X alpha / 255 + 背景颜色 X (255 - alpha) / 255 */
                    red   = (src_red * alpha) / 255 + (dst_red * (255 - alpha)) / 255;
                    green = (src_green * alpha) / 255 + (dst_green * (255 - alpha)) / 255;
                    blue  = (src_blue * alpha) / 255 + (dst_blue * (255 - alpha)) / 255;
                    color_16 = (red << 11) | (green << 5) | blue;
                    *(unsigned short *)dst_line_buf = color_16;
                    // *(unsigned short *)dst_line_buf = 0xff;
                    dst_line_buf += 2;
                }
                break;
            }
            break;
        case 24:
            switch(src_data->bpp){
            case 16:
                for(j = 0 ; j < dst_width ; j++){
                    /* 取出各颜色分量 */
                    color_16  = *(unsigned short *)(src_line_buf + 2 * scale_x_table[j]);
                    src_red   = (color_16 >> 11) << 3;
                    src_green = ((color_16 >> 5) & 0x3f) << 2;
                    src_blue  = (color_16 & 0x1f) << 3;
                    
                    dst_line_buf[j * 3]     = src_red;
                    dst_line_buf[j * 3 + 1] = src_green;
                    dst_line_buf[j * 3 + 2] = src_blue;
                    dst_line_buf += 3;
                }
                break;
            case 24:
                for(j = 0 ; j < dst_width ; j++){
                    memcpy(dst_line_buf,(src_line_buf + 3 * scale_x_table[j]),3);
                    dst_line_buf += 3;
                }
                break;
            case 32:
                for(j = 0 ; j < dst_width ; j++){
                    alpha     = src_line_buf[scale_x_table[j] * 4];
                    src_red   = src_line_buf[scale_x_table[j] * 4 + 1];
                    src_green = src_line_buf[scale_x_table[j] * 4 + 2];
                    src_blue  = src_line_buf[scale_x_table[j] * 4 + 3];
                    dst_red   = dst_line_buf[j * 3] ;
                    dst_green = dst_line_buf[j * 3 + 1];
                    dst_blue  = dst_line_buf[j * 3 + 2];
                    
                    /* 根据透明度合并颜色,公式:显示颜色 = 源像素颜色 X alpha / 255 + 背景颜色 X (255 - alpha) / 255 */
                    red   = (src_red * alpha) / 255 + (dst_red * (255 - alpha)) / 255;
                    green = (src_green * alpha) / 255 + (dst_green * (255 - alpha)) / 255;
                    blue  = (src_blue * alpha) / 255 + (dst_blue * (255 - alpha)) / 255;
                    color = (red << 11) | (green << 5) | blue;
                    dst_line_buf[j * 3]     = red;
                    dst_line_buf[j * 3 + 1] = green;
                    dst_line_buf[j * 3 + 2] = blue;
                }
                break;
            }
            break;
        case 32:
            switch(src_data->bpp){
            case 16:
                for(j = 0 ; j < dst_width ; j++){
                    /* 取出各颜色分量 */
                    color_16  = *(unsigned short *)(src_line_buf + (2 * scale_x_table[j]));
                    src_red   = (color_16 >> 11) << 3;
                    src_green = ((color_16 >> 5) & 0x3f) << 2;
                    src_blue  = (color_16 & 0x1f) << 3;
                    
                    dst_line_buf[j * 4] = 0xff;             //默认不透明
                    dst_line_buf[j * 4 + 1] = src_red;
                    dst_line_buf[j * 4 + 2] = src_green;
                    dst_line_buf[j * 4 + 2] = src_blue;
                }
                break;
            case 24:
                for(j = 0 ; j < dst_width ; j++){
                    src_red     = src_line_buf[3 * scale_x_table[j] + 0] >> 3;
                    src_green   = src_line_buf[3 * scale_x_table[j] + 1] >> 2;
                    src_blue    = src_line_buf[3 * scale_x_table[j] + 2] >> 3;
                    dst_line_buf[j * 4]     = 0;
                    dst_line_buf[j * 4 + 1] = src_red;
                    dst_line_buf[j * 4 + 2] = src_green;
                    dst_line_buf[j * 4 + 2] = src_blue;
                }
                break;
            case 32:
                for(j = 0 ; j < dst_width ; j++){
                    *(unsigned int *)dst_line_buf = *(unsigned int *)(src_line_buf + (4 * scale_x_table[j]));
                    dst_line_buf += 4;
                }
                break;
            }
            break;
        }
    }
    return 0;
}

/* 至少要指定目的图像的长宽 */
int pic_zoom_with_same_bpp_and_rotate(struct pixel_data *dst_data,struct pixel_data *src_data,int rotate)
{
    float x_scale,y_scale;
    unsigned char *dst_line_buf,*src_line_buf,*src_buf;
    unsigned int width,height;
    unsigned int src_width,src_height,src_line_bytes;
    unsigned int y_src,i,j,k,bytes_per_pixel;
    unsigned int x_scale_table[dst_data->width];
    unsigned int temp;
    
    /* 判断条件 */
    if(dst_data->height > src_data->height || dst_data->width > src_data->width || \
       ((dst_data->bpp != 0) && (dst_data->bpp != src_data->bpp))){
        DP_ERR("%s:invalid argument!\n",__func__);
        return -1;
    }
    if(rotate != 0 && rotate != 90 && rotate != 180 && rotate != 270){
        DP_ERR("%s:invalid argument!\n",__func__);
        return -1;
    }
    
    /* 如果需要的话，为目的图像分配内存 */
    if(!dst_data->buf && !dst_data->rows_buf){
        dst_data->bpp = src_data->bpp;
        dst_data->line_bytes = dst_data->width * dst_data->bpp / 8;
        dst_data->total_bytes = dst_data->line_bytes * dst_data->height;
        dst_data->buf = malloc(dst_data->total_bytes);
        if(!dst_data->buf){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
    }
    
    bytes_per_pixel = dst_data->bpp / 8;
    width = dst_data->width;
    height = dst_data->height;
    src_width = src_data->width;
    src_height = src_data->height;
    src_line_bytes = src_data->line_bytes;
    
    /* 计算x方向上的比例 */
    x_scale = (float)src_data->width / dst_data->width;
    for(i = 0 ; i < width ; i++){
        x_scale_table[i] = i * x_scale;
    }
    
    y_scale = (float)src_data->height / dst_data->height;
    if(0 == rotate){
        for(i = 0 ; i < height ; i++){
            y_src = i * y_scale;
            if(src_data->in_rows){
                src_line_buf = src_data->rows_buf[y_src];
            }else{
                src_line_buf = src_data->buf + src_data->line_bytes * y_src;
            }
            if(dst_data->in_rows){
                dst_line_buf = dst_data->rows_buf[i];
            }else{
                dst_line_buf = dst_data->buf + dst_data->line_bytes * i;
            }
            for(j = 0 ; j < width ; j++){
                switch (bytes_per_pixel){
                    case 1:
                        *dst_line_buf = *(src_line_buf + (1 * x_scale_table[j]));
                        dst_line_buf++;
                        break;
                    case 2:
                        *(unsigned short *)dst_line_buf = *(unsigned short *)(src_line_buf + (2 * x_scale_table[j]));
                        dst_line_buf += 2;
                        break;
                    case 4:
                        *(unsigned int *)dst_line_buf = *(unsigned int *)(src_line_buf + (4 * x_scale_table[j]));
                        dst_line_buf += 4;
                        break;
                    default:
                        temp = bytes_per_pixel * x_scale_table[j];
                        for(k = 0 ; k < bytes_per_pixel ; k++){
                            *(dst_line_buf + k) = *(src_line_buf + temp + k);
                        }
                        dst_line_buf += bytes_per_pixel;
                        break;
                }
                
            }
        }
    }else if(180 == rotate){
        for(i = 0 ; i < height ; i++){
            y_src = i * y_scale;
            if(src_data->in_rows){
                src_line_buf = src_data->rows_buf[src_height - y_src - 1];
            }else{
                src_line_buf = src_data->buf + src_data->line_bytes * (src_height - y_src - 1);
            }
            if(dst_data->in_rows){
                dst_line_buf = dst_data->rows_buf[i];
            }else{
                dst_line_buf = dst_data->buf + dst_data->line_bytes * i;
            }
            for(j = 0 ; j < width ; j++){
                switch (bytes_per_pixel){
                    case 1:
                        *dst_line_buf = *(src_line_buf + (src_width - x_scale_table[j] - 1));
                        dst_line_buf++;
                        break;
                    case 2:
                        *(unsigned short *)dst_line_buf = *(unsigned short *)(src_line_buf + (2 * (src_width - x_scale_table[j] - 1)));
                        dst_line_buf += 2;
                        break;
                    case 4:
                        *(unsigned int *)dst_line_buf = *(unsigned int *)(src_line_buf + (4 * (src_width - x_scale_table[j] - 1)));
                        dst_line_buf += 4;
                        break;
                    default:
                        temp = bytes_per_pixel * (src_width - x_scale_table[j] - 1);
                        for(k = 0 ; k < bytes_per_pixel ; k++){
                            *(dst_line_buf + k) = *(src_line_buf + temp + k);
                        }
                        dst_line_buf += bytes_per_pixel;
                        break;
                }
            }
        }
    }else if(90 == rotate){
        /* 这种情况，比例得重新算 */
        x_scale = (float)src_data->height / dst_data->width;
        for(i = 0 ; i < width ; i++){
            x_scale_table[i] = i * x_scale;
        }
        y_scale = (float)src_data->width / dst_data->height;

        for(i = 0 ; i < height ; i++){
            y_src = i * y_scale;
            if(src_data->in_rows){
                src_line_buf = src_data->rows_buf[src_height - 1] + y_src * bytes_per_pixel;
            }else{
                src_line_buf = src_data->buf + src_data->line_bytes * (src_height  - 1) + y_src * bytes_per_pixel;
            }
            if(dst_data->in_rows){
                dst_line_buf = dst_data->rows_buf[i];
            }else{
                dst_line_buf = dst_data->buf + dst_data->line_bytes * i;
            }
            for(j = 0 ; j < width ; j++){
                switch (bytes_per_pixel){
                    case 1:
                        *dst_line_buf = *(src_line_buf - x_scale_table[j] * src_line_bytes);
                        dst_line_buf++;
                        break;
                    case 2:
                        *(unsigned short *)dst_line_buf = *(unsigned short *)(src_line_buf - x_scale_table[j] * src_line_bytes);
                        dst_line_buf += 2;
                        break;
                    case 4:
                        *(unsigned int *)dst_line_buf = *(unsigned int *)(src_line_buf - x_scale_table[j] * src_line_bytes);
                        dst_line_buf += 4;
                        break;
                    default:
                        temp = x_scale_table[j] * src_line_bytes;
                        for(k = 0 ; k < bytes_per_pixel ; k++){
                            *(dst_line_buf + k) = *(src_line_buf - temp + k);
                        }
                        dst_line_buf += bytes_per_pixel;
                        break;
                }
            }
        }
    }else if(270 == rotate){
        /* 这种情况，比例得重新算 */
        x_scale = (float)src_data->height / dst_data->width;
        for(i = 0 ; i < width ; i++){
            x_scale_table[i] = i * x_scale;
        }
        y_scale = (float)src_data->width / dst_data->height;

        for(i = 0 ; i < height ; i++){
            y_src = i * y_scale;
            if(src_data->in_rows){
                src_line_buf = src_data->rows_buf[0] + (src_width - 1 - y_src) * bytes_per_pixel;
            }else{
                src_line_buf = src_data->buf + (src_width - 1 - y_src) * bytes_per_pixel;
            }
            if(dst_data->in_rows){
                dst_line_buf = dst_data->rows_buf[i];
            }else{
                dst_line_buf = dst_data->buf + dst_data->line_bytes * i;
            }
            for(j = 0 ; j < width ; j++){
                switch (bytes_per_pixel){
                    case 1:
                        *dst_line_buf = *(src_line_buf + x_scale_table[j] * src_line_bytes);
                        dst_line_buf++;
                        break;
                    case 2:
                        *(unsigned short *)dst_line_buf = *(unsigned short *)(src_line_buf + x_scale_table[j] * src_line_bytes);
                        dst_line_buf += 2;
                        break;
                    case 4:
                        *(unsigned int *)dst_line_buf = *(unsigned int *)(src_line_buf + x_scale_table[j] * src_line_bytes);
                        dst_line_buf += 4;
                        break;
                    default:
                        temp = x_scale_table[j] * src_line_bytes;
                        for(k = 0 ; k < bytes_per_pixel ; k++){
                            *(dst_line_buf + k) = *(src_line_buf + temp + k);
                        }
                        dst_line_buf += bytes_per_pixel;
                        break;
                }
            }
        }
    }
    
    return 0;
}

/* 至少要指定目的图像的长宽 */
int pic_zoom_with_same_bpp(struct pixel_data *dst_data,struct pixel_data *src_data)
{
    float x_scale,y_scale;
    unsigned char *dst_line_buf,*src_line_buf,*src_buf;
    unsigned int width,height;
    unsigned int y_src,i,j,k,bytes_per_pixel;
    unsigned int x_scale_table[dst_data->width];
    unsigned int temp;

    /* 判断条件,如果要求的目的图像比原图像还大，则不缩放，直接复制一份原图像 */
    if(dst_data->width > src_data->width || dst_data->height > src_data->height){
        *dst_data = *src_data;
        if(NULL == (dst_data = malloc(dst_data->total_bytes))){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        memcpy(dst_data->buf,src_data->buf,dst_data->total_bytes);
        return 0;
    } 
    /* 此函数只支持相同的bpp */
    if((dst_data->bpp != 0) && (dst_data->bpp != src_data->bpp)){
        DP_ERR("%s:invalid argument!\n",__func__);
        return -1;
    }
    /* 如果需要的话，为目的图像分配内存 */
    if(!dst_data->buf && !dst_data->rows_buf){
        dst_data->bpp = src_data->bpp;
        dst_data->line_bytes  = dst_data->width * dst_data->bpp / 8;
        dst_data->total_bytes = dst_data->line_bytes * dst_data->height;
        dst_data->buf = malloc(dst_data->total_bytes);
        if(!dst_data->buf){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
    }else{
        return -1;
    }

    dst_data->has_alpha = src_data->has_alpha;

    bytes_per_pixel = dst_data->bpp / 8;
    width = dst_data->width;
    height = dst_data->height;
    /* 计算x方向上的比例 */
    x_scale = (float)src_data->width / dst_data->width;
    for(i = 0 ; i < width ; i++){
        x_scale_table[i] = i * x_scale;
    }
    y_scale = (float)src_data->height / dst_data->height;
    for(i = 0 ; i < height ; i++){
        y_src = i * y_scale;
        if(src_data->in_rows){
            src_line_buf = src_data->rows_buf[y_src];
        }else{
            src_line_buf = src_data->buf + src_data->line_bytes * y_src;
        }
        if(dst_data->in_rows){
            dst_line_buf = src_data->rows_buf[i];
        }else{
            dst_line_buf = dst_data->buf + dst_data->line_bytes * i;
        }
        for(j = 0 ; j < width ; j++){
            switch (bytes_per_pixel){
                case 1:
                    *dst_line_buf = *(src_line_buf + (1 * x_scale_table[j]));
                    dst_line_buf++;
                    break;
                case 2:
                    *(unsigned short *)dst_line_buf = *(unsigned short *)(src_line_buf + (2 * x_scale_table[j]));
                    dst_line_buf += 2;
                    break;
                case 4:
                    *(unsigned int *)dst_line_buf = *(unsigned int *)(src_line_buf + (4 * x_scale_table[j]));
                    dst_line_buf += 4;
                    break;
                default:
                    temp = bytes_per_pixel * x_scale_table[j];
                    for(k = 0 ; k < bytes_per_pixel ; k++){
                        *(dst_line_buf + k) = *(src_line_buf + temp + k);
                    }
                    dst_line_buf += bytes_per_pixel;
                    break;
            }
         
        }
    }
    return 0;
}

/* 此函数与pic_zoom_in_rows的区别是，pic_zoom_in_rows直接将缩放后的数据覆盖到zoomed_data所指向的内存区域中，
 * 而 pic_zoom_and_merge_in_rows 是将缩放后的数据合并到zoomed_data所指向的区域中；
 * 如果图像有透明度的属性，这种做法是必要的
 */
int pic_zoom_and_merge(struct pixel_data *raw_data,struct pixel_data *zoomed_data)
{
    int i,j;
    unsigned short src_red,src_green,src_blue;
    unsigned short dst_red,dst_green,dst_blue;
    unsigned short red,green,blue;
    unsigned int color,orig_color;
    unsigned char alpha;
    int zoomed_width   = zoomed_data->width;
    int zoomed_height  = zoomed_data->height;
    int src_line_bytes = raw_data->line_bytes;
    unsigned char **src_rows_buf = raw_data->rows_buf;
    unsigned char **dst_rows_buf = zoomed_data->rows_buf;
    unsigned char *src_buf = raw_data->buf;
    unsigned char *dst_buf = zoomed_data->buf;
    unsigned char *dst_line_buf,*src_line_buf;
    float scale_y,scale_x;
    unsigned int scale_x_table[zoomed_width];

    /* 一些特殊情况不处理 */ 
    if(zoomed_data->width >= raw_data->width || zoomed_data->height >=raw_data->height){
        DP_WARNING("%s:err!\n",__func__);
        return -1;
    }
    /* 原始数据的bpp必须为32，且有alpha通道 */
    if(zoomed_data->bpp != 16 || raw_data->bpp != 32 || !raw_data->has_alpha){
        DP_WARNING("%s:imcompatible bpp!\n",__func__);
        return -1;
    }
    /* 计算x方向上的比例系数 */
    scale_x = (float)(raw_data->width) / zoomed_data->width;
    for(i = 0 ; i < zoomed_width ; i++){
        scale_x_table[i] = scale_x * i;
    }
    scale_y = (float)(raw_data->height) / zoomed_data->height;
    for(i = 0 ; i < zoomed_height ; i++){
        unsigned int src_y = (unsigned int )(i * scale_y);
        
        /* 根据数据不同储存方式作不同处理 */
        if(raw_data->in_rows){
            src_line_buf = src_rows_buf[src_y];
        }else{
            src_line_buf = src_buf + src_y * raw_data->line_bytes;
        }

        if(zoomed_data->in_rows){
            dst_line_buf = dst_rows_buf[i];
        }else{
            dst_line_buf = dst_buf + i *zoomed_data->line_bytes;
        }
        
        for(j = 0 ; j < zoomed_width ; j++){
            /* 颜色格式为ARGB */
            int temp = scale_x_table[j] * 4;
            alpha = src_line_buf[temp];
            
            /* !! 在合成颜色之前,将原图数据位数转换至与屏幕相符是必要的!!!*/
            src_red   = (src_line_buf[temp + 1] >> 3);
            src_green = (src_line_buf[temp + 2] >> 2);
            src_blue  = (src_line_buf[temp + 3] >> 3);

            orig_color = *(unsigned short *)dst_line_buf;

            dst_red   = (orig_color >> 11) & 0x1f;
            dst_green = (orig_color >> 5) & 0x3f;
            dst_blue  = orig_color & 0x1f;

            /* 显示颜色 = 源像素颜色 X alpha / 255 + 背景颜色 X (255 - alpha) / 255 */
            /* 255 和 256差不多，开始有个想法，为加快速度，用左移8位代替除255 */
            /* 但事实证明上述方法不可取，即使像素值只差1，还是可以看出来的 */
            red     = ((src_red * alpha) / 255) + ((dst_red * (255 - alpha)) / 255);
            green   = ((src_green * alpha) / 255) + ((dst_green * (255 - alpha)) / 255);
            blue    = ((src_blue * alpha) / 255) + ((dst_blue * (255 - alpha)) / 255);
            
            color = (red << 11 | green << 5 | blue) & 0xffff;
            *(unsigned short *)dst_line_buf = (unsigned short)color;
            // *(unsigned short *)dst_line_buf = 0xf800;
            dst_line_buf += 2;
        }
    }
    return 0;
}

