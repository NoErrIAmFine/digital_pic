#include <stdbool.h>
#include <ft2build.h>

#include FT_FREETYPE_H
#include FT_GLYPH_H

#include "config.h"
#include "font_render.h"
#include "font_decoder.h"
#include "picfmt_manager.h"
#include "debug_manager.h"
#include "pic_operation.h"
#include "render.h"

/* 按理说,下面两个函数应该与具体的字体渲染模块尽量无关的,但实际上,这两个函数与freetype紧密相关,
 * 目前我还想不到什么好办法使这两个模块尽量解耦,或许该把它们合成一个?
 */
/* 
 * @param:code_type - 编码类型，如果为空则尝试进行简单的自动检测
 */
int get_char_bitamp_from_buf(const char *buf,unsigned int len,const char *code_type,struct pixel_data *pixel_data,unsigned int font_align,unsigned int color,unsigned int font_size)
{
    int ret;
    unsigned int code;
    unsigned int width,height;
    unsigned int start_x,start_y;
    unsigned int i,j;
    unsigned char *dst_buf;
    struct font_decoder *font_decoder;
    struct font_bitmap font_bitmap;
    FT_GlyphSlot slot;

    font_decoder = get_font_decoder_by_name(code_type);
    
    font_decoder->set_font_size(font_size);

    font_bitmap.pen_x = 0;
    font_bitmap.pen_y = 0;

    ret = font_decoder->get_bitmap_from_buf(buf,len,&font_bitmap);

    if(ret){
        DP_ERR("%s:get bitamp from buf failed!\n",__func__);
        return ret;
    }

    slot = (FT_GlyphSlot)font_bitmap.private_data;
    width = font_bitmap.width;
    height = font_bitmap.rows;

    DP_INFO("width:%d,height:%d\n",width,height);
    DP_INFO("pixel_data->width:%d,pixel_data->height:%d\n",pixel_data->width,pixel_data->height);
    /* 如果宽度或高度不够,直接退出 */
    if(height > pixel_data->height || width > pixel_data->width){
        DP_ERR("%s:bitmap insufficient height\n",__func__);
        return -1;
    }

    switch(font_align){
        case FONT_ALIGN_HORIZONTAL_CENTER:
            start_x = (pixel_data->width - width) / 2;
            start_y = (pixel_data->height - height) / 2;
            break;
        case FONT_ALIGN_RIGHT:
            start_x = pixel_data->width - slot->advance.x;
            start_y = (pixel_data->height - height) / 2;
            break;
        case FONT_ALIGN_LEFT:
        default:
            start_x = 0;
            start_y = (pixel_data->height - height) / 2;
            break;
    }

    /* 暂只处理16bpp */
    if(16 == pixel_data->bpp){
        /* 按数据不同的储存方式作不同处理 */
        int y_min,x_min;
        y_min = pixel_data->height - start_y - slot->bitmap_top;
        if(y_min < 0){
            y_min = 0;
        }
        x_min = start_x + slot->bitmap_left;
        if(x_min < 0){
            x_min = 0;
        }

        if(pixel_data->in_rows){
            if(1 == font_bitmap.bpp){
                char temp_byte;
                int k,bit;

                for(i = 0; i < height; i++){
                    dst_buf = pixel_data->rows_buf[y_min++] + x_min * pixel_data->bpp / 8;
                    k = (i) * font_bitmap.pitch;
                    temp_byte = font_bitmap.buffer[k++];
                    for(j = 0, bit = 7 ; j < width ; j++ , bit--){
                        if(-1 == bit){
                            bit = 7;
                            temp_byte = font_bitmap.buffer[k++];
                        }
                        if(temp_byte & 0x80){
                            *(unsigned short *)dst_buf = (unsigned short)color;
                        }else{
                            // *(unsigned short *)dst_buf = (unsigned short)BACKGROUND_COLOR;
                        }
                        temp_byte <<= 1;
                        dst_buf += 2;
                    }
                }
            }else if(8 == font_bitmap.bpp){
                int k;
                for(i = y_min ; i < height -start_y ; i++){
                    dst_buf = pixel_data->rows_buf[i] + x_min * pixel_data->bpp / 8;
                    k = (i - y_min) * font_bitmap.pitch;
                    for(j = x_min ; j < width ; j++){
                        if(font_bitmap.buffer[k++]){
                            *(unsigned short *)dst_buf = (unsigned short)color;
                        }else{
                            *(unsigned short *)dst_buf = (unsigned short)BACKGROUND_COLOR;
                        }
                        dst_buf += 2;
                    }
                }
            }
            
        }else{
             if(1 == font_bitmap.bpp){
                char temp_byte,bit;
                int k;
                for(i = y_min; i < height -start_y ; i++){
                    dst_buf = pixel_data->buf + pixel_data->line_bytes * y_min + x_min * pixel_data->bpp / 8;
                    k = (i - y_min) * font_bitmap.pitch;
                    for(j = x_min , bit = 7 ; j < width ; j++ , bit--){
                        if(-1 == bit){
                            bit = 7;
                        }
                        if(7 == bit){
                            temp_byte = font_bitmap.buffer[k++];
                        }
                        if(bit & 0x80){
                            *(unsigned short *)dst_buf = (unsigned short)color;
                        }else{
                            *(unsigned short *)dst_buf = (unsigned short)BACKGROUND_COLOR;
                        }
                        bit <<= 1;
                        dst_buf += 2;
                    }
                }
            }else if(8 == font_bitmap.bpp){
                int k;
                for(i = y_min ; i < height -start_y ; i++){
                    dst_buf = pixel_data->buf + pixel_data->line_bytes * y_min + x_min * pixel_data->bpp / 8;
                    k = (i - y_min) * font_bitmap.pitch;
                    for(j = x_min ; j < width ; j++){
                        if(font_bitmap.buffer[k++]){
                            *(unsigned short *)dst_buf = (unsigned short)color;
                        }else{
                            *(unsigned short *)dst_buf = (unsigned short)BACKGROUND_COLOR;
                        }
                        dst_buf += 2;
                    }
                }
            }
            
        }
    }
    return 0;
}


static void compute_string_bbox(FT_BBox *abbox,FT_Glyph *glyphs,FT_Vector *pos,unsigned int num_glyphs)
{
    unsigned int n;
    FT_BBox  bbox;
    FT_BBox  glyph_bbox;
    /* initialize string bbox to "empty" values */
    bbox.xMin = bbox.yMin =  32000;
    bbox.xMax = bbox.yMax = -32000;
    /* for each glyph image, compute its bounding box, */
    /* translate it, and grow the string bbox          */
    for (n = 0; n < num_glyphs ; n++ )
    {
        FT_Glyph_Get_CBox( glyphs[n], ft_glyph_bbox_pixels, &glyph_bbox );
        glyph_bbox.xMin += pos[n].x;
        glyph_bbox.xMax += pos[n].x;
        glyph_bbox.yMin += pos[n].y;
        glyph_bbox.yMax += pos[n].y;
        if ( glyph_bbox.xMin < bbox.xMin )
        bbox.xMin = glyph_bbox.xMin;
        if ( glyph_bbox.yMin < bbox.yMin )
        bbox.yMin = glyph_bbox.yMin;
        if ( glyph_bbox.xMax > bbox.xMax )
        bbox.xMax = glyph_bbox.xMax;
        if ( glyph_bbox.yMax > bbox.yMax )
        bbox.yMax = glyph_bbox.yMax;
    }
    /* check that we really grew the string bbox */
    if ( bbox.xMin > bbox.xMax )
    {
        bbox.xMin = 0;
        bbox.yMin = 0;
        bbox.xMax = 0;
        bbox.yMax = 0;
    }
    /* return string bbox */
    *abbox = bbox;
}

static int draw_bitmap_in_pixel_data(struct pixel_data *pixel_data,FT_Bitmap *bitmap,int pos_x,int pos_y,unsigned int color)
{
    unsigned char *dst_buf;
    unsigned char *src_buf;
    unsigned int width,height;
    unsigned int i,j,k;
    /* 检查一些边界条件,x方向允许截断,但y方向不允许 */
    if(pos_x < 0) pos_x = 0;
    if(pos_x >= pixel_data->width) pos_x = pixel_data->width - 1;
    if(pos_y < 0 || pos_y >= pixel_data->height){
        DP_WARNING("%s:bitmap out of range!\n",__func__);
        return 0;
    }

    // DP_WARNING("pixel_data->width:%d,pixel_data->height:%d!\n",pixel_data->width,pixel_data->height);
    // DP_WARNING("pos_x:%d,pos_y:%d!\n",pos_x,pos_y);
    // DP_WARNING("bitmap->width:%d,bitmap->rows:%d!\n",bitmap->width,bitmap->rows);

    width = bitmap->width;
    height = bitmap->rows;
    if(pos_x + width >= pixel_data->width){
        width = pixel_data->width - pos_x;
    }

    if(16 == pixel_data->bpp){
        unsigned int temp;
        char bit,temp_byte;
        temp = pos_x * pixel_data->bpp / 8;
        for(i = 0 ; i < height ; i++){
            /* 按数据的不同的储存方式作不同处理 */
            if(pixel_data->in_rows){        //按行存储
                dst_buf = pixel_data->rows_buf[pos_y + i] + temp;
            }else{                          //整块存储
                dst_buf = pixel_data->buf + (pos_y + i) * pixel_data->line_bytes + temp;
            }
            src_buf = bitmap->buffer + bitmap->pitch * i;
            temp_byte = *src_buf++;
            for(j = 0 ,bit = 7; j < width ; j++ ,bit--){
                if(bit == 0){
                    bit = 7;
                    temp_byte = *src_buf++;
                }
                if(temp_byte & 0x80){
                    *(unsigned short *)dst_buf = (unsigned short)FOREGROUND_COLOR;
                }else{
                    // *(unsigned short *)dst_buf = (unsigned short)BACKGROUND_COLOR;
                }
                temp_byte <<= 1;
                dst_buf += 2;
            }
        }   
    }   
    return 0;
}

/* 
 * @param:code_type - 编码类型，如果为空则尝试进行简单的自动检测
 * @param:len - 字符缓存长度，如果为0由此函数尝试获取
 */
int get_string_bitamp_from_buf(const char *buf,unsigned int len,const char *code_type,
struct pixel_data *pixel_data,unsigned int font_align,unsigned int color,unsigned int font_size)
{
    int char_len,ret;
    unsigned int code;
    unsigned int string_width,string_height;
    int start_x,start_y; 
    struct font_decoder *font_decoder;
    struct font_render *font_render;
    struct font_bitmap font_bitmap;
    FT_BBox bbox;
    FT_GlyphSlot slot;
#define MAX_STRING_SIZE 80
    FT_Glyph glyphs[MAX_STRING_SIZE];
    FT_Vector pos[MAX_STRING_SIZE];
    unsigned num_glyphs,n;

    font_bitmap.pen_x = 0;
    font_bitmap.pen_y = 0;
    font_bitmap.previous_index = 0;
    num_glyphs = 0;
    pos[num_glyphs].x = 0;
    pos[num_glyphs].y = 0;

    if(!len){
        len = strlen(buf);
    }
    
    if(font_size > pixel_data->height){
        DP_ERR("%s:invalid argument!\n",__func__);
        return -1;
    }
    
    /* 根据提供的参数获取解码器 */
    if(code_type){
        font_decoder = get_font_decoder_by_name(code_type);
    }else{
        //to-do-list
    }
    
    font_decoder->set_font_size(font_size);
    if(font_decoder->default_render){
        font_render = font_decoder->default_render;
    }else{
        return -1;
    }

    /* 将字符串对应的所有字形读出来 */
    while(len > 0){
        char_len = font_decoder->get_code_from_buf(buf,len,&code);
        if(char_len < 0){
            DP_ERR("%s:get char code failed!\n",__func__);
            return -1;
        }
        if(font_bitmap.pen_x > pixel_data->width){
            /* 已超出最大显示区域，退出 */
            num_glyphs--;
            FT_Done_Glyph(glyphs[num_glyphs]);
            break;
        }
        pos[num_glyphs].x = font_bitmap.pen_x;
        pos[num_glyphs].y = font_bitmap.pen_y;

        font_render->get_char_glyph(code,&font_bitmap);

        glyphs[num_glyphs] = (FT_Glyph)font_bitmap.private_data;

        num_glyphs++;
        if(num_glyphs > MAX_STRING_SIZE){
            /* 已达最大字符长度，退出 */
            break;
        }

        buf += char_len;
        len -= char_len;
    }
    
    /*  计算最小边界框 */
    compute_string_bbox(&bbox,glyphs,pos,num_glyphs);
    if(!(bbox.xMin || bbox.xMax || bbox.yMin || bbox.yMax)){
        DP_ERR("%s:compute_string_bbox failed!\n",__func__);
        return -1;
    }
    string_width = bbox.xMax - bbox.xMin;
    string_height = bbox.yMax - bbox.yMin;

    /* 如果字符高度超出，则直接返回,如果宽度超出的话直接截断即可 */
    if(string_height > pixel_data->height){
        DP_ERR("%s:string height is out fof range!\n",__func__);
        /* 这个错误是可修复的 */
        return -2;
    }
    
    /* 计算起点 */
    switch(font_align){
        case FONT_ALIGN_LEFT:
            start_x = 0;
            start_y = (((signed int)pixel_data->height - (signed int)string_height) / 2);
            start_y = start_y + bbox.yMax;
            break;
        case FONT_ALIGN_RIGHT:
            start_x = ((signed int)pixel_data->width - (signed int)string_width);
            start_y = ((signed int)pixel_data->height - (signed int)string_height) / 2;
            start_y = start_y + bbox.yMax;
            break;
        case FONT_ALIGN_HORIZONTAL_CENTER:
        default:
            start_x = ((signed int)pixel_data->width - (signed int)string_width) / 2;
            start_y = ((signed int)pixel_data->height - (signed int)string_height) / 2;
            start_y = start_y + bbox.yMax;
            break;
    }
    
    /* 挨个描绘 */
    for(n = 0 ; n < num_glyphs ; n++){
        FT_Glyph image;
        // FT_Vector pen;
        image = glyphs[n];
        // pen.x = start_x + pos[n].x;
        // pen.y = start_y + pos[n].y;
        ret = FT_Glyph_To_Bitmap(&image,FT_RENDER_MODE_MONO,NULL,0);
        if(!ret){
            FT_BitmapGlyph bit = (FT_BitmapGlyph)image;
            draw_bitmap_in_pixel_data(pixel_data,&bit->bitmap,start_x + pos[n].x,start_y - bit->top,color);
            FT_Done_Glyph(image);
        }
    }

    return 0;
}

int clear_pixel_data(struct pixel_data *pixel_data,unsigned int color)
{
    unsigned int width,height;
    unsigned int i,j;
    unsigned char *dst_buf;

    width = pixel_data->width;
    height = pixel_data->height;

    /* 暂只处理16bpp */
    if(16 == pixel_data->bpp){
        for(i = 0 ; i < height ; i++){
            /* 按数据的不同的储存方式作不同处理 */
            if(pixel_data->in_rows){        //按行存储
                dst_buf = pixel_data->rows_buf[i] ;
            }else{                          //整块存储
                dst_buf = pixel_data->buf + i * pixel_data->line_bytes;
            }
            for(j = 0 ; j < width ; j++){
                *(unsigned short *)dst_buf = (unsigned short)color;
                dst_buf += 2;
            }
        }   
    }
    return 0;
}

/* 只做最简单的数据复制,两个区域的大小必须完全相同,不负责转换和缩放 */
int copy_pixel_data(struct pixel_data *dst_data,struct pixel_data *src_data)
{
    unsigned char *dst_line_buf,*src_line_buf;
    unsigned int height;
    unsigned int i;

    /* 如果这两者大小和bpp有任何不符,直接退出把 */
    if(dst_data->width != src_data->width || dst_data->height != src_data->height || \
       dst_data->bpp != src_data->bpp){
           return -1;
    }

    height = dst_data->height;

    for(i = 0 ; i < height ; i++){
        if(dst_data->in_rows){
            dst_line_buf = dst_data->rows_buf[i];
        }else{
            dst_line_buf = dst_data->buf + dst_data->line_bytes * i;
        }
        if(src_data->in_rows){
            src_line_buf = src_data->rows_buf[i];
        }else{
            src_line_buf = src_data->buf + src_data->line_bytes * i;
        }
        memcpy(dst_line_buf,src_line_buf,dst_data->line_bytes);
    }
    return 0;
}

/* 两个区域大小不相同,但源图像必须能完整地放入目标图像内,支持alpha通道 */
int merge_pixel_data_in_center(struct pixel_data *dst_data,struct pixel_data *src_data)
{
    unsigned int i,j;
    unsigned int dst_start_x,dst_start_y;
    unsigned int src_width,src_height;
    unsigned int dst_width,dst_height;
    unsigned char *src_line_buf,*dst_line_buf;
    unsigned char src_red,src_green,src_blue,alpha;
    unsigned char dst_red,dst_green,dst_blue;
    unsigned char red,green,blue;
    unsigned short color;

    src_width  = src_data->width;
    src_height = src_data->height;
    dst_width  = dst_data->width;
    dst_height = dst_data->height;

    /* 目的数据的bpp只能为16,源数据的bpp可以为16,24,32,其他情况视为错误 */
    if(dst_width < src_width || dst_height < src_height || dst_data->bpp != 16 || \
       (src_data->bpp != 16 && src_data->bpp != 24 && src_data->bpp != 32)){
        DP_ERR("%s:invalid argument!\n",__func__);
        return -1;
    }
    dst_start_x = (dst_width - src_width) / 2;
    dst_start_y = (dst_height - src_height) / 2;

    for(i = 0 ; i < src_height ; i++){
        /* 构造行指针 */
        if(src_data->in_rows){
            src_line_buf = src_data->rows_buf[i];
        }else{
            src_line_buf = src_data->buf + i * src_data->line_bytes;
        }
        if(dst_data->in_rows){
            dst_line_buf = dst_data->rows_buf[i + dst_start_y] + dst_start_x * 2;
        }else{
            dst_line_buf = dst_data->buf + (i + dst_start_y) * dst_data->line_bytes + dst_start_x * 2;
        }
        /* 根据源数据不同bpp作不同处理 */
        switch(src_data->bpp){
            case 16:
                memcpy(dst_line_buf,src_line_buf,src_data->line_bytes);
                break;
            case 24:
                for(j = 0 ; j < src_width ; j++){
                    /* 取出各颜色分量 */
                    red   = src_line_buf[j * 3] >> 3;
                    green = src_line_buf[j * 3 + 1] >> 2;
                    blue  = src_line_buf[j * 3 + 2] >> 3;
                    color = (red << 11) | (green << 5) | blue;

                    *(unsigned short *)dst_line_buf = color;
                    dst_line_buf += 2;
                }
                break;
            case 32:
                for(j = 0 ; j < src_width ; j++){
                    /* 取出各颜色分量 */
                    alpha     = src_line_buf[j * 4];
                    src_red   = src_line_buf[j * 4 + 1] >> 3;
                    src_green = src_line_buf[j * 4 + 2] >> 2;
                    src_blue  = src_line_buf[j * 4 + 3] >> 3;

                    color = *(unsigned short *)dst_line_buf;
                    dst_red   = (color >> 11) & 0x1f;
                    dst_green = (color >> 5) & 0x3f;
                    dst_blue  = color & 0x1f;

                    /* 根据透明度合并颜色,公式:显示颜色 = 源像素颜色 X alpha / 255 + 背景颜色 X (255 - alpha) / 255 */
                    red   = (src_red * alpha) / 255 + (dst_red * (255 - alpha)) / 255;
                    green = (src_green * alpha) / 255 + (dst_green * (255 - alpha)) / 255;
                    blue  = (src_blue * alpha) / 255 + (dst_blue * (255 - alpha)) / 255;
                    color = (red << 11) | (green << 5) | blue;

                    *(unsigned short *)dst_line_buf = color;
                    // *(unsigned short *)dst_line_buf = 0xff;
                    dst_line_buf += 2;
                }
                break;
            default:
                return -1;
        }
    }
    return 0;
}

/* 原图像必须具有透明度,且为32bpp */
int merge_pixel_data(struct pixel_data *dst_data,struct pixel_data *src_data)
{
    unsigned char *dst_line_buf,*src_line_buf;
    unsigned int width,height;
    unsigned int i,j;
    unsigned char src_red,src_green,src_blue,alpha;
    unsigned char dst_red,dst_green,dst_blue;
    unsigned char red,green,blue;
    unsigned short color;

    /* 如果条件任何不符,直接退出把 */
    if(dst_data->width != src_data->width || dst_data->height != src_data->height || \
       (dst_data->bpp != 16 && src_data->bpp != 32)){
           return -1;
    }

    height = dst_data->height;
    width = dst_data->width;
    for(i = 0 ; i < height ; i++){
        if(dst_data->in_rows){
            dst_line_buf = dst_data->rows_buf[i];
        }else{
            dst_line_buf = dst_data->buf + dst_data->line_bytes * i;
        }
        if(src_data->in_rows){
            src_line_buf = src_data->rows_buf[i];
        }else{
            src_line_buf = src_data->buf + src_data->line_bytes * i;
        }
        for(j = 0 ; j < width ; j++){
            /* 取出各颜色分量 */
            alpha     = src_line_buf[j * 4];
            src_red   = src_line_buf[j * 4 + 1] >> 3;
            src_green = src_line_buf[j * 4 + 2] >> 2;
            src_blue  = src_line_buf[j * 4 + 3] >> 3;

            color = *(unsigned short *)dst_line_buf;
            dst_red   = (color >> 11) & 0x1f;
            dst_green = (color >> 5) & 0x3f;
            dst_blue  = color & 0x1f;

            /* 根据透明度合并颜色,公式:显示颜色 = 源像素颜色 X alpha / 255 + 背景颜色 X (255 - alpha) / 255 */
            red   = (src_red * alpha) / 255 + (dst_red * (255 - alpha)) / 255;
            green = (src_green * alpha) / 255 + (dst_green * (255 - alpha)) / 255;
            blue  = (src_blue * alpha) / 255 + (dst_blue * (255 - alpha)) / 255;
            color = (red << 11) | (green << 5) | blue;

            *(unsigned short *)dst_line_buf = color;
            // *(unsigned short *)dst_line_buf = 0xff;
            dst_line_buf += 2;
        }
    }
    return 0;
}

int invert_region(struct pixel_data *pixel_data)
{   
    unsigned short *line_buf;
    unsigned int width,height;
    unsigned int i,j;
    /* 暂只处理16bpp */
    if(16 == pixel_data->bpp){
        width = pixel_data->width;
        height = pixel_data->height;
        for(i = 0 ; i < height ; i++){
            /* 根据数据的不同储存方式获取行起始处指针 */
            if(pixel_data->in_rows){
                line_buf = (unsigned short *)pixel_data->rows_buf[i];
            }else{
                line_buf = (unsigned short *)pixel_data->buf + pixel_data->line_bytes * i;
            }
            for(j = 0 ; j < width ; j++){
                *line_buf = ~(*line_buf);
                line_buf++;
            }
        }
        return 0;
    }else{
        DP_INFO("%s:unsupported bpp!\n",__func__);
        return -1;
    }

}