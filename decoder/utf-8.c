#include "font_decoder.h"
#include "debug_manager.h"

static struct font_decoder utf8_font_decoder;

static int utf8_font_init(struct font_decoder *decoder)
{
    struct font_render *tmp;
    tmp = get_font_render_by_name("freetype");
    if(tmp && tmp->is_enable){
        add_render_for_font_decoder(decoder,tmp);
    }
    decoder->default_render = tmp;
    return 0;
}

static int utf8_is_support(const char *buf)
{
    return 0;
}

static int utf8_set_font_size(unsigned int font_size)
{
    int ret;
    struct decoder_render *decoder_render;
    if(utf8_font_decoder.renders){
        decoder_render = utf8_font_decoder.renders;
        while(decoder_render){
            if(decoder_render->render->set_font_size){
                ret = decoder_render->render->set_font_size(font_size);
                if(ret){
                    DP_ERR("%s:set font size failed!\n",__func__);
                    return -1;
                }
            }
            decoder_render = decoder_render->next;
        }
        return 0;
    }else{
        DP_WARNING("%s:set font size failed,has no render!\n",__func__);
        return -1;
    }
}

/* 
 * @description:获得一个字节中前导1的个数
 */
static int lead_one_count_in_byte(char buf)
{
    int count = 0;

    while(buf & 0x80){
        count++;
        buf <<= 1;
    }

    return count;
}

/* 
 * @description:获得字符编码,返回该字符所占的内存长度
 */
static int utf8_get_code_from_buf(const char *buf,unsigned int len,unsigned int *code)
{
    int count,temp_count;
    unsigned int char_code = 0;

    if(!((*buf) & 0x80)){
        /* 此为ascii码,直接返回 */
        *code = (unsigned int)*buf;
        return 1;
    }

    count = lead_one_count_in_byte(*buf);
    temp_count = count;

    if(1 == count || count > len){
        DP_ERR("%s:invalid argument!\n",__func__);
        return -1;
    }

    char_code |= (*buf & ((1 << (8 - count)) - 1)) << ((count - 1) * 6);
    count--;
    buf++;
    while(count){
        char_code |= ((*buf) & 0x3f) << ((count - 1) * 6);
        count--;
        buf++;
    }

    *code = char_code;
    return temp_count;
}


static int utf8_get_bitmap_from_buf(const char *buf,unsigned int len,struct font_bitmap *font_bitmap)
{
    unsigned int char_code;
    struct font_render *font_render = utf8_font_decoder.default_render;

    utf8_get_code_from_buf(buf,len,&char_code);
    // DP_INFO("char_code:%d\n",char_code);
    font_render->get_char_bitmap(char_code,font_bitmap);
    
    return font_render->get_char_bitmap(char_code,font_bitmap);
}

static int utf8_get_bitmap_from_code(unsigned int code,struct font_bitmap *font_bitmap)
{
    /* to do */
    return 0;
}

static struct font_decoder utf8_font_decoder = {
    .name = "utf-8",
    .init = utf8_font_init,
    .is_support = utf8_is_support,
    .set_font_size = utf8_set_font_size,
    .get_code_from_buf = utf8_get_code_from_buf,
    .get_bitmap_from_buf = utf8_get_bitmap_from_buf,
    .get_bitmap_from_code = utf8_get_bitmap_from_code,
};


int utf8_decoder_init(void)
{
    return register_font_decoder(&utf8_font_decoder);
}
