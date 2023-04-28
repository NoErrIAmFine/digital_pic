#ifndef __FONT_DECODER_H
#define __FONT_DECODER_H

#include "font_render.h"

struct decoder_render
{
    struct decoder_render *next;
    struct font_render *render;
};

struct font_decoder
{
    const char *name;
    struct font_decoder *next;
    struct font_render *default_render;
    struct decoder_render *renders;
    int (*init)(struct font_decoder *);
    int (*is_support)(const char *file_name);
    int (*set_font_size)(unsigned int font_size);
    int (*set_font_file)(const char *font_file);
    int (*get_code_from_buf)(const char *buf,unsigned int len,unsigned int *code);
    int (*get_bitmap_from_buf)(const char *buf,unsigned int len,struct font_bitmap *bitmap);
    int (*get_bitmap_from_code)(unsigned int code,struct font_bitmap* bitmap); 
    int (*get_header_len)(const char *file_buf);
};

int register_font_decoder(struct font_decoder*);
int unregister_font_decoder(struct font_decoder*);
void show_font_deconder(void);
struct font_decoder *get_font_decoder_by_name(const char*);
int add_render_for_font_decoder(struct font_decoder *,struct font_render *);
/* 根据文件内容，尝试获取一个解码器 */
struct font_decoder *get_font_decoder_for_file(const char *file_buf);

int ascii_decoder_init(void);
int utf8_decoder_init(void);
int utf16be_decoder_init(void);
int utf16le_decoder_init(void);

int font_decoder_init(void);


#endif // !__FONT_DECODER_H
