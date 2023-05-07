#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "picfmt_manager.h"
#include "debug_manager.h"

static int png_get_pixel_data_in_rows(const char *file_name,struct pixel_data *pixel_data)
{
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    FILE *fp;
    unsigned int width,height,bit_depth,color_type,channels,row_bytes;
    unsigned int bpp;
    png_bytepp row_pointers = NULL;
    int i,j;
    
    /* 打开文件 */
    fp = fopen(file_name,"rb");
    if(!fp){
        DP_ERR("%s:fopen failed!\n",__func__);
        return -1;
    }
    
    /* 分配和初始化png_ptr 和 info_ptr */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
    if(!png_ptr){
        DP_ERR("failed to create png_struct!\n");
        return -1;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if(!info_ptr){
        png_destroy_read_struct(&png_ptr,NULL,NULL);
        DP_ERR("failed to create png_info!\n");
        return -1;
    }

    /* 设置错误返回点 */
    if(setjmp(png_jmpbuf(png_ptr))){
        png_destroy_read_struct(&png_ptr,&info_ptr,NULL);
        DP_ERR("libpng internal error!\n");
        return -1;
    }
    
    /* 指定数据源 */
    png_init_io(png_ptr,fp);
    
    /* 读取png文件信息 */
    png_read_info(png_ptr,info_ptr);
    
    width       = png_get_image_width(png_ptr,info_ptr);
    height      = png_get_image_height(png_ptr,info_ptr);
    color_type  = png_get_color_type(png_ptr,info_ptr);

    if(color_type == PNG_COLOR_TYPE_PALETTE){
        DP_WARNING("unsupported png color type!\n");
        png_destroy_read_struct(&png_ptr,&info_ptr,NULL);
        return -1;
    }
    
    if (color_type == PNG_COLOR_TYPE_RGB_ALPHA)
        png_set_swap_alpha(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    bit_depth   = png_get_bit_depth(png_ptr,info_ptr);
    channels    = png_get_channels(png_ptr,info_ptr);
    row_bytes   = png_get_rowbytes(png_ptr,info_ptr);

    if (bit_depth == 16){
        png_set_scale_16(png_ptr);
        //特别注意，通道深度修剪后要重新获取bit_depth
        png_read_update_info(png_ptr, info_ptr);
        bit_depth   = png_get_bit_depth(png_ptr,info_ptr);
        row_bytes   = png_get_rowbytes(png_ptr,info_ptr);
    }
    
    /* 到这里，应该只有两种情况了，24位的rgb和32位的argb,如果不是则退出 */
    bpp = channels * bit_depth;
    if(bpp != 24 && bpp != 32){
        /* bpp不对劲，释放内存，并返回错误 */
        DP_ERR("%s:invalid bpp!\n",__func__);
        return -1;
    }
    /* 为传进来的pixel_data分配内存,按行分配，最好还是让此函数来分配内存 */
    if(!pixel_data->buf && !pixel_data->in_rows){
        pixel_data->rows_buf = malloc(height * sizeof(unsigned char *));
        if(!pixel_data->rows_buf){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        row_pointers = pixel_data->rows_buf;

        for(i = 0 ; i < height ; i++){
            row_pointers[i] = malloc(row_bytes);
            if(!row_pointers[i]){
                DP_ERR("%s:malloc failed!\n",__func__);
                return -ENOMEM;
            }
        }
        pixel_data->width = width;
        pixel_data->height = height;
        pixel_data->in_rows = 1;
        if(channels == 4){
            pixel_data->has_alpha = 1;
        }
        pixel_data->bpp = bpp;
        pixel_data->line_bytes = row_bytes;
        pixel_data->total_bytes = pixel_data->line_bytes * pixel_data->height;
    }else if(pixel_data->in_rows){
        /* 已经分配过内存，还是需要检查一下内存是否足够 */
        if(pixel_data->height < height || pixel_data->line_bytes < (width * bpp / 8)){
            DP_WARNING("%s:already alloc mem but don't enough\n",__func__);
            /* 此错误可修复 */
            return -2;
        }
        row_pointers = pixel_data->rows_buf;
    }else{
        //内存必须由此函数分配
        return -1;
    }
    
    png_read_image(png_ptr,row_pointers);
    
    png_read_end(png_ptr,info_ptr);
    
    /* 释放资源 */
    png_destroy_read_struct(&png_ptr,&info_ptr,NULL);
    
    fclose(fp);
    return 0;
}

static int png_get_pixel_data(const char *file_name,struct pixel_data *pixel_data)
{
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    FILE *fp;
    unsigned int width,height,bit_depth,color_type,channels,row_bytes;
    unsigned int bpp;
    png_bytepp row_pointers = NULL;
    int i,j;
    
    /* 打开文件 */
    fp = fopen(file_name,"rb");
    if(!fp){
        DP_ERR("%s:fopen failed!\n",__func__);
        return -1;
    }
    
    /* 分配和初始化png_ptr 和 info_ptr */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
    if(!png_ptr){
        DP_ERR("failed to create png_struct!\n");
        return -1;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if(!info_ptr){
        png_destroy_read_struct(&png_ptr,NULL,NULL);
        DP_ERR("failed to create png_info!\n");
        return -1;
    }

    /* 设置错误返回点 */
    if(setjmp(png_jmpbuf(png_ptr))){
        png_destroy_read_struct(&png_ptr,&info_ptr,NULL);
        DP_ERR("libpng internal error!\n");
        return -1;
    }
    
    /* 指定数据源 */
    png_init_io(png_ptr,fp);
    
    /* 读取png文件信息 */
    png_read_info(png_ptr,info_ptr);
    
    width       = png_get_image_width(png_ptr,info_ptr);
    height      = png_get_image_height(png_ptr,info_ptr);
    color_type  = png_get_color_type(png_ptr,info_ptr);
    
    if(color_type == PNG_COLOR_TYPE_PALETTE){
        png_set_palette_to_rgb(png_ptr);
    }
    
    if (color_type == PNG_COLOR_TYPE_RGB_ALPHA)
        png_set_swap_alpha(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    channels    = png_get_channels(png_ptr,info_ptr);
    row_bytes   = png_get_rowbytes(png_ptr,info_ptr);
    bit_depth   = png_get_bit_depth(png_ptr,info_ptr);
    
    if (bit_depth == 16){
        png_set_scale_16(png_ptr);      //特别注意，通道深度修剪后要重新获取bit_depth       
        png_read_update_info(png_ptr, info_ptr);
        bit_depth   = png_get_bit_depth(png_ptr,info_ptr);
        row_bytes   = png_get_rowbytes(png_ptr,info_ptr);
    }
   
    /* 到这里，应该只有两种情况了，24位的rgb和32位的argb,如果不是则退出 */
    bpp = channels * bit_depth; 
    if(bpp != 24 && bpp != 32){
        /* bpp不对劲，释放内存，并返回错误 */
        DP_ERR("%s:invalid bpp!\n",__func__);
        return -1;
    }
    /* 为传进来的pixel_data分配内存,整块分配，同时需要一个辅助的行指针数组,最好还是让此函数来分配内存 */
    if(!pixel_data->buf && !pixel_data->in_rows){
        pixel_data->width = width;
        pixel_data->height = height;
        if(channels == 4){
            pixel_data->has_alpha = 1;
        }else{
            pixel_data->has_alpha = 0;
        }
        pixel_data->bpp = bpp;
        pixel_data->line_bytes = row_bytes;
        pixel_data->total_bytes = pixel_data->line_bytes * pixel_data->height;
        pixel_data->buf = malloc(pixel_data->total_bytes);
        if(!pixel_data->buf){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }

        row_pointers = malloc(height * sizeof(unsigned char *));
        if(!row_pointers){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        /* 设置行指针数组使其指向合适位置 */
        for(i = 0 ; i < height ; i++){
            row_pointers[i] = pixel_data->buf + i * pixel_data->line_bytes;
        }
    }else if(pixel_data->in_rows){
        /* 内存必须是整块分配的 */
        return -2;
    }else{
        //内存必须由此函数分配
        return -1;
    }
    
    png_read_image(png_ptr,row_pointers);
    
    png_read_end(png_ptr,info_ptr);
    
    /* 释放资源 */
    png_destroy_read_struct(&png_ptr,&info_ptr,NULL);
    free(row_pointers);
    fclose(fp);
    return 0;
}

static int png_free_pixel_data(struct pixel_data *pixel_data)
{
    return 0;
}

static int is_support_png(const char *file_name)
{
    unsigned char png_sig[8];
    int is_png;
    FILE *fp = fopen(file_name, "rb");
    if (!fp){
        DP_ERR("%s:open failed!\n");
        return -1;
    }
    fread(png_sig, 1, 8, fp);
    is_png = !png_sig_cmp(png_sig, 0, 8);
    if (!is_png){
        return 0;
    }
    fclose(fp);
    return 1;
}

static struct picfmt_parser png_parser = {
    .name = "png",
    .get_pixel_data_in_rows = png_get_pixel_data_in_rows,
    .get_pixel_data = png_get_pixel_data,
    .free_pixel_data = png_free_pixel_data,
    .is_support = is_support_png,
    .is_enable = 1,
};

int png_init(void)
{
    return register_picfmt_parser(&png_parser);
}