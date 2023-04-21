#include <stdio.h>
#include <string.h>

#include "picfmt_manager.h"

static struct picfmt_parser *picfmt_parser_list;

int register_picfmt_parser(struct picfmt_parser *parser)
{
    struct picfmt_parser *temp;
    if(!picfmt_parser_list){
        picfmt_parser_list = parser;
        parser->next = NULL;
        return 0;
    }else{
        temp = picfmt_parser_list;
        /* 不允许同名 */
        if(!strcmp(temp->name,parser->name)){
            printf("%s:picfmt parser is existed!\n",__func__);
            return -1;
        }
        while(temp->next){
            if(!strcmp(temp->next->name,parser->name)){
                printf("%s:picfmt parser is existed!\n",__func__);
                return -1;
            }
            temp = temp->next;
        }
        temp->next = parser;
        parser->next = NULL;
        return 0;
    }
}

/* 将指定的debuger从全局链表移除 */
int unregister_picfmt_parser(struct picfmt_parser *parser)
{
    struct picfmt_parser **tmp;
    if(!picfmt_parser_list){
        printf("%s:has no exist picfmt parser!\n",__func__);
        return -1;
    }else{
        tmp = &picfmt_parser_list;
        while(*tmp){
            /* 找到则移除*/
            if((*tmp) == parser){
                *tmp = (*tmp)->next;
                return 0;
            }
            tmp = &(*tmp)->next;
        }
        printf("%s:unregistered picfmt parser!\n",__func__);
        return -1;
    }
}

struct picfmt_parser *get_parser_by_name(const char *name)
{
    struct picfmt_parser *tmp = picfmt_parser_list;
    if(!name)
        return NULL;
        
    while(tmp){
        if(!strcmp(name,tmp->name)){
            return tmp;
        }
        tmp = tmp->next;
    }
    printf("%s:can't found picfmt parser!\n",__func__);
    return NULL;
}

/* 打印当前已注册的图片解析器的信息 */
void show_picfmt_parser(void)
{
    int i = 1;
    struct picfmt_parser *tmp = picfmt_parser_list;

    while(tmp){
        printf("number:%d ; picfmt parser name:%s\n",i++,tmp->name);
        tmp = tmp->next;
    }
}

int picfmt_parser_init(void)
{
    int ret;
    if((ret = png_init()))
        return ret;
    
    if((ret = jpeg_init()))
        return ret;

    if((ret = bmp_init()))
        return ret;

    if((ret = gif_init()))
        return ret;

    return 0;
}



