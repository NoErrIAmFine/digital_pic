#include "font_decoder.h"


static int ascii_font_init(struct font_decoder *decoder)
{
    struct font_render *tmp;
    tmp = get_font_render_by_name("freetype");
    if(tmp && tmp->is_enable){
        add_render_for_font_decoder(decoder,tmp);
    }
    return 0;
}

static int ascii_is_support(const char *buf)
{
    /* 暂只简单处理,如果字符第七位为0,则认为是ascii */
    if((*buf) & 0x80){
        return 0;
    }
    return 1;
}

static int ascii_get_code_from_buf(const char *buf,unsigned int len,unsigned int *code)
{
    *code = (unsigned int)(*buf);

    return 0;
}

static int ascii_get_bitmap_from_buf(const char *buf,unsigned int len,struct font_bitmap *font_bitmap)
{
    /* to do */
    return 0;
}

static int ascii_get_bitmap_from_code(unsigned int code,struct font_bitmap *font_bitmap)
{
    /* to do */
    return 0;
}

static struct font_decoder ascii_font_decoder = {
    .name = "ascii",
    .init = ascii_font_init,
    .is_support = ascii_is_support,
    .get_code_from_buf = ascii_get_code_from_buf,
    .get_bitmap_from_code = ascii_get_bitmap_from_code,
    .get_bitmap_from_buf = ascii_get_bitmap_from_buf,
};

int ascii_decoder_init(void)
{
    return register_font_decoder(&ascii_font_decoder);
}
