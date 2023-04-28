#ifndef __RENDER_H
#define __RENDER_H

#include "display_manager.h"
#include "font_decoder.h"

#define FONT_ALIGN_LEFT 0x01
#define FONT_ALIGN_HORIZONTAL_CENTER 0x02
#define FONT_ALIGN_RIGHT 0x04

/* 
 * @param:code_type - 编码类型，如果为空则尝试进行简单的自动检测
 */
int get_char_bitamp_from_buf(const char *buf,unsigned int len,const char *code_type,
struct pixel_data *pixel_data,unsigned int font_align,unsigned int color,unsigned int font_size);


/* 
 * @param:code_type - 编码类型，如果为空则尝试进行简单的自动检测
 */
int get_string_bitamp_from_buf(const char *buf,unsigned int len,const char *code_type,
struct pixel_data *pixel_data,unsigned int font_align,unsigned int color,unsigned int font_size);
int fill_text_one_line(struct pixel_data *pixel_data,const char *file_buf,int len,struct font_decoder *decoder);
int clear_pixel_data(struct pixel_data *dst_data,unsigned int color);
int copy_pixel_data(struct pixel_data *dst_data,struct pixel_data *src_data);
int merge_pixel_data_in_center(struct pixel_data *dst_data,struct pixel_data *src_data);
int merge_pixel_data(struct pixel_data *dst_data,struct pixel_data *src_data);

#endif // !__RENDER_H