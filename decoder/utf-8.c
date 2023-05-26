#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

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

static int utf8_is_support(const char *file_name)
{
    int i,j,fd;
    int lead_one;
    const char utf8_bom[] = {0xef,0xbb,0xbf,0};
    char file_buf[100];
    char *file_pos;

    if((fd = open(file_name,O_RDONLY)) < 0){
        perror("open text file failed!\n");
        return errno;
    }

    if(100 != read(fd,file_buf,100)){
        perror("write text file failed!\n");
        return errno;
    }
    close(fd);
    return 1;
    if(!strncmp(utf8_bom,file_buf,3)){
        return 1;
    }else{
        /* 如果出现字节序标记当然可以直接确定，BOM对于utf8不是必需的 */
        /* 这里尝试检测前100个字节，看它是否符合utf8的模式 */
        file_pos = file_buf;
        for(i = 0 ; i < 100 ; i++){
            if(!((*file_pos) & 0x80)){
                /* 以0开始，说明是一个兼容的ascii */
                file_pos++;
                continue;
            }else if((*file_pos & 0xc0) == 0x80){
                /* 后续字节，直接跳过 */
                file_pos++;
                continue;
            }else{
                /* 找到了一个首字节，看后续字节是否符合要求 */
                lead_one = lead_one_count_in_byte(*file_pos);
                for(j = 1 ; j <= lead_one ; j++){
                    if((file_pos[j] & 0xc0) != 0x80)
                        return 0;
                }
                i += lead_one;
                file_pos += (1 + lead_one);
            }
        }
        return 1;
    }
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

/* 返回文件头长度，utf-8不一定有 */
static int utf8_get_header_len(const char *file_buf)
{
    const char utf8_bom[] = {0xef,0xbb,0xbf,0};
    if(!strncmp(utf8_bom,file_buf,3)){
        return 3;
    }else{
        return 0;
    }
}

static struct font_decoder utf8_font_decoder = {
    .name                   = "utf-8",
    .init                   = utf8_font_init,
    .is_support             = utf8_is_support,
    .set_font_size          = utf8_set_font_size,
    .get_code_from_buf      = utf8_get_code_from_buf,
    .get_bitmap_from_buf    = utf8_get_bitmap_from_buf,
    .get_bitmap_from_code   = utf8_get_bitmap_from_code,
    .get_header_len         = utf8_get_header_len,
};


int utf8_decoder_init(void)
{
    return register_font_decoder(&utf8_font_decoder);
}
