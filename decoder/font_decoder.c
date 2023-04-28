#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "font_decoder.h"
#include "debug_manager.h"

static struct font_decoder *font_decoder_list;

int add_render_for_font_decoder(struct font_decoder *decoder,struct font_render *render)
{   
    struct decoder_render *temp;

    if(!decoder->renders){
        temp = malloc(sizeof(*temp));
        if(!temp){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        decoder->renders = temp;
        temp->next = NULL;
        temp->render = render;
        render->use_count++;
        return 0;
    }else{
        temp = decoder->renders;
        if(temp->render == render){
            DP_WARNING("%s:repeated render for decoder!\n");
            return -1;
        }
        while(temp->next){
            if(temp->next->render == render){
                DP_WARNING("%s:repeated render for decoder!\n");
                return -1;
            }
            temp = temp->next;
        }
        temp->next = malloc(sizeof(*temp));
        if(!temp){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        temp->next->next = NULL;
        temp->next->render = render;
        render->use_count++;
        return 0;
    }
}

int register_font_decoder(struct font_decoder *decoder)
{
    struct font_decoder *temp;
    if(!font_decoder_list){
        font_decoder_list = decoder;
        decoder->next = NULL;
        if(decoder->init){
            decoder->init(decoder);
        }
        return 0;
    }else{
        temp = font_decoder_list;
        /* 不允许同名 */
        if(!strcmp(temp->name,decoder->name)){
            DP_WARNING("%s:font decoder is existed!\n",__func__);
            return -1;
        }
        while(temp->next){
            if(!strcmp(temp->next->name,decoder->name)){
                DP_WARNING("%s:font decoder is existed!\n",__func__);
                return -1;
            }
            temp = temp->next;
        }
        temp->next = decoder;
        decoder->next = NULL;
        if(decoder->init){
            decoder->init(decoder);
        }
        return 0;
    }
}

int unregister_font_decoder(struct font_decoder *decoder)
{
    struct font_decoder **tmp;
    if(!font_decoder_list){
        DP_WARNING("%s:has no exist font decoder!\n",__func__);
        return -1;
    }else{
        tmp = &font_decoder_list;
        while(*tmp){
            /* 找到则移除*/
            if((*tmp) == decoder){
                /* 同时移除挂载在其中的font_render */
                struct decoder_render *tmp1,*tmp2;
                tmp1 = decoder->renders;
                while(tmp1){
                    tmp2 = tmp1->next;
                    tmp1->render->use_count--;
                    free(tmp1);
                    tmp1 = tmp2;
                }
                decoder->renders = NULL;
                *tmp = (*tmp)->next;
                return 0;
            }
            tmp = &(*tmp)->next;
        }
        DP_WARNING("%s:can't found registered font decoder!\n",__func__);
        return -1;
    }
}

void show_font_deconder(void)
{
    int i = 1;
    struct font_decoder *tmp = font_decoder_list;

    while(tmp){
        DP_EMERG("number:%d ; font decoder name:%s\n",i++,tmp->name);
        tmp = tmp->next;
    }
}

struct font_decoder *get_font_decoder_by_name(const char *name)
{
    struct font_decoder *tmp = font_decoder_list;

    while(tmp){
        if(!strcmp(name,tmp->name)){
            return tmp;
        }
        tmp = tmp->next;
    }
    DP_WARNING("%s:can't found font decoder!\n",__func__);
    return NULL;
}

int font_decoder_init(void)
{
    int ret;

    if((ret = ascii_decoder_init())){
        DP_ERR("%s:ascii_init failed!\n",__func__);
        return ret;
    }

    if((ret = utf8_decoder_init())){
        DP_ERR("%s:utf8_init failed!\n",__func__);
        return ret;
    }

    if((ret = utf16be_decoder_init())){
        DP_ERR("%s:utf16be_init failed!\n",__func__);
        return ret;
    }

    if((ret = utf16le_decoder_init())){
        DP_ERR("%s:utf16le_init failed!\n",__func__);
        return ret;
    }
    
    return 0;
}

/* 根据文件内容，尝试获取一个解码器 */
struct font_decoder *get_font_decoder_for_file(const char *file_name)
{
    struct font_decoder *temp;

    /* 遍历font_decoder链表，调用上面的函数 */
    temp = font_decoder_list;
    while(temp){
        if(temp->is_support){
            if(temp->is_support(file_name)){
                return temp;
            }
        }
        temp = temp->next;
    }
    return NULL;
}