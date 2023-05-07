#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "picfmt_manager.h"
#include "display_manager.h"
#include "debug_manager.h"
#include "render.h"
#include "file.h"

static struct picfmt_parser *picfmt_parser_list;

/* 这个数组是为了方便查找picfmt_parser的 */
static const char *parser_names[] = {
    [FILETYPE_FILE_BMP]     = "bmp",
    [FILETYPE_FILE_JPEG]    = "jpeg",
    [FILETYPE_FILE_PNG]     = "png",
    [FILETYPE_FILE_GIF]     = "gif",         
};

struct pixel_data load_err_img;
const char *load_err_img_name = DEFAULT_ICON_FILE_PATH "/" "load_err.png";

int register_picfmt_parser(struct picfmt_parser *parser)
{
    int ret;

    struct picfmt_parser *temp;
    if(!picfmt_parser_list){
        picfmt_parser_list = parser;
        parser->next = NULL;
        if(parser->init)
            ret = parser->init();
        return ret;
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
        if(parser->init)
            ret = parser->init();
        return ret;
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
    struct picfmt_parser *parser;

    if((ret = png_init()))
        return ret;
    
    if((ret = jpeg_init()))
        return ret;

    if((ret = bmp_init()))
        return ret;

    if((ret = gif_init()))
        return ret;

    /* 如果只是一张图片获取失败不应该直接退出，给它返回一张专门表示错误的图片就好了 */
    memset(&load_err_img,0,sizeof(struct pixel_data));
    ret = get_pic_pixel_data(load_err_img_name,FILETYPE_FILE_PNG,&load_err_img);
    if(ret){
        DP_ERR("%s:load 'err image' failed!\n",__func__);
        return -1;
    }

    return 0;
}

/* 根据图片文件名读入相应图片的数据,此函数负责分配内存,此函数不负责缩放 */
int get_pic_pixel_data(const char *pic_file,char file_type,struct pixel_data *pixel_data)
{
    int ret;
    struct picfmt_parser *parser;
    
    parser = get_parser_by_name(parser_names[(int)file_type]);
    if(!parser){
        DP_ERR("%s:get_parser_by_name failed!\n",__func__);
        return -1;
    }
    
    ret = parser->get_pixel_data(pic_file,pixel_data);
    if(ret){
        DP_ERR("%s:pic parser get_pixel_data failed!\n",__func__);

        /* 如果没获取到，就给它一张默认的表示错误的图片吧 */
        *pixel_data = load_err_img;
        if(NULL == (pixel_data->buf = malloc(pixel_data->total_bytes))){
            /* 到这里真的是无药可救了 */
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        copy_pixel_data(pixel_data,&load_err_img);
        return 0;
    }
    
    return 0;
}

