#include "debug_manager.h"
#include "pic_operation.h"

#include <errno.h>
#include <stdlib.h>

/* 缩放图像，且只能缩小，缩放后的数据是按行存储的 */
int pic_zoom_in_rows(struct pixel_data *raw_data,struct pixel_data *zoomed_data)
{
    int i,j;
    unsigned short red,green,blue,color;
    int zoomed_width   = zoomed_data->width;
    int zoomed_height  = zoomed_data->height;
    int src_line_bytes = raw_data->line_bytes;
    unsigned char *src_buf = raw_data->buf;
    unsigned char *dst_line_buf;
    unsigned char **dst_buf = zoomed_data->rows_buf;
    float scale_y,scale_x;
    unsigned int scale_x_table[zoomed_width];

    /* 一些特殊情况不处理 */ 
    if(zoomed_data->width >= raw_data->width || zoomed_data->height >=raw_data->height){
        DP_WARNING("%s:err!\n",__func__);
        return -1;
    }
    /* bpp只支持16到16、16到24的转换，更多的情况暂不考虑 */
    if(zoomed_data->bpp != 16 || (raw_data->bpp != 16 && raw_data->bpp != 24)){
        DP_WARNING("%s:imcompatibel bpp!\n",__func__);
        return -1;
    }
    
    /* 计算x方向上的比例系数 */
    scale_x = (float)(raw_data->width) / zoomed_data->width;
    for(i = 0 ; i < zoomed_width ; i++){
        scale_x_table[i] = scale_x * i;
    }
    
    scale_y = (float)(raw_data->height) / zoomed_data->height;
    for(i = 0 ; i < zoomed_height ; i++){
        
        dst_line_buf = dst_buf[i];
        src_buf += (((unsigned int)(scale_y * i)) * src_line_bytes);
        if(raw_data->bpp == 16){
            for(j = 0 ; j < zoomed_width ; j++){
                *(unsigned short *)dst_line_buf = *(unsigned short*)(src_buf + j * scale_x_table[j] * 2);
                dst_line_buf += 2;
            }
        }else if(raw_data->bpp == 24){
            for(j = 0 ; j < zoomed_width ; j++){
                int temp = j * scale_x_table[j];
                red   = src_buf[temp * 3] >> 3;
                green = src_buf[temp * 3 + 1] >> 2;
                blue  = src_buf[temp * 3 + 2] >> 3;
                color = red << 11 | green << 5 | blue;
                *(unsigned short *)dst_line_buf = color;
                dst_line_buf += 2;
            }
        }
    }
    return 0;
}

/* 至少要指定目的图像的长宽 */
int pic_zoom_with_same_bpp_and_rotate(struct pixel_data *src_data,struct pixel_data *dst_data,int rotate)
{
    float x_scale,y_scale;
    unsigned char *dst_line_buf,*src_line_buf,*src_buf;
    unsigned int width,height;
    unsigned int src_width,src_height,src_line_bytes;
    unsigned int y_src,i,j,k,bytes_per_pixel;
    unsigned int x_scale_table[dst_data->width];
    unsigned int temp;

    /* 判断条件 */
    if(dst_data->width > src_data->width || dst_data->height > src_data->height || \
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
int pic_zoom_with_same_bpp(struct pixel_data *src_data,struct pixel_data *dst_data)
{
    float x_scale,y_scale;
    unsigned char *dst_line_buf,*src_line_buf,*src_buf;
    unsigned int width,height;
    unsigned int y_src,i,j,k,bytes_per_pixel;
    unsigned int x_scale_table[dst_data->width];
    unsigned int temp;
    /* 判断条件 */
    if(dst_data->width > src_data->width || dst_data->height > src_data->height || \
       ((dst_data->bpp != 0) && (dst_data->bpp != src_data->bpp))){
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

