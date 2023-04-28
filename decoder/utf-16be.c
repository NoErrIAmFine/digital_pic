#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include "font_decoder.h"
#include "debug_manager.h"

static struct font_decoder utf16be_font_decoder;

static int utf16be_font_init(struct font_decoder *decoder)
{
    struct font_render *tmp;
    tmp = get_font_render_by_name("freetype");
    if(tmp && tmp->is_enable){
        add_render_for_font_decoder(decoder,tmp);
    }
    decoder->default_render = tmp;
    return 0;
}

static int utf16be_set_font_size(unsigned int font_size)
{
    int ret;
    struct decoder_render *decoder_render;
    if(utf16be_font_decoder.renders){
        decoder_render = utf16be_font_decoder.renders;
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

static int utf16be_is_support(const char *file_name)
{
    int fd;
    const char utf16le_bom[] = {0xfe,0xff,0};
    char file_buf[4];

    if((fd = open(file_name,O_RDONLY)) < 0){
        perror("open text file failed!\n");
        return errno;
    }

    if(4 != read(fd,file_buf,4)){
        perror("write text file failed!\n");
        return errno;
    }
    close(fd);

    if(!strncmp(utf16le_bom,file_buf,2)){
        return 1;
    }else{
        return 0;
    }
}

/* 
 * @description:获得字符编码,返回该字符所占的内存长度
 */
static int utf16be_get_code_from_buf(const char *buf,unsigned int len,unsigned int *code)
{
     unsigned short temp,temp1;
   
    if(len >=2){
        temp = buf[1] | (buf[0] << 8);
        if((temp & 0xfc00) == 0xd800){
            /*引导代理*/
            temp1 = buf[3] | (buf[2] << 8);
            if((temp1 & 0xfc00) == 0xdc00){
                *code = (((temp & 0x03ff) << 10) | (temp1 & 0x03ff)) + 0x10000;
                return 4;
            }
        }else if((temp & 0xfc00) == 0xdc00){
            /* 尾随代理,这种情况不应该出现的 */
            temp1 =(*(buf - 1)) | ((*(buf - 2)) << 8);
            if((temp1 & 0xfc00) == 0xd800){
                *code = (((temp1 & 0x03ff) << 10) | (temp & 0x03ff)) + 0x10000;
                return 2;
            }
        }else{
            *code = temp;
            return 2;
        }
    }
    return -1;
}


static int utf16be_get_bitmap_from_buf(const char *buf,unsigned int len,struct font_bitmap *font_bitmap)
{
    unsigned int char_code;
    struct font_render *font_render = utf16be_font_decoder.default_render;

    utf16be_get_code_from_buf(buf,len,&char_code);
    // DP_INFO("char_code:%d\n",char_code);
    font_render->get_char_bitmap(char_code,font_bitmap);
    
    return font_render->get_char_bitmap(char_code,font_bitmap);
}

static int utf16be_get_bitmap_from_code(unsigned int code,struct font_bitmap *font_bitmap)
{
    /* to do */
    return 0;
}

static int utf16be_get_header_len(const char *file_buf)
{
    const char utf16be_bom[] = {0xfe,0xff,0};
    if(!strncmp(utf16be_bom,file_buf,2)){
        return 2;
    }else{
        return 0;
    }
}

static struct font_decoder utf16be_font_decoder = {
    .name                   = "utf-16be",
    .init                   = utf16be_font_init,
    .is_support             = utf16be_is_support,
    .set_font_size          = utf16be_set_font_size,
    .get_code_from_buf      = utf16be_get_code_from_buf,
    .get_bitmap_from_buf    = utf16be_get_bitmap_from_buf,
    .get_bitmap_from_code   = utf16be_get_bitmap_from_code,
    .get_header_len         = utf16be_get_header_len,
};


int utf16be_decoder_init(void)
{
    return register_font_decoder(&utf16be_font_decoder);
}
