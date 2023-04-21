#include <stdio.h>
#include <string.h>

#include "display_manager.h"
#include "debug_manager.h"

static struct display_struct *display_list = NULL;
static struct display_struct *default_display = NULL;

int register_display_struct(struct display_struct *disp)
{
    struct display_struct *temp;
    if(!display_list){
        display_list = disp;
        disp->next = NULL;
        /* 特别注意，如果有初始化函数要记得调用初始化函数 */
        if(disp->init){
            disp->init(disp);
        }
        
        return 0;
    }else{
        temp = display_list;
        /* 不允许注册同名的debuger */
        if(!strcmp(temp->name,display_list->name)){
            printf("%s:display device is existed!\n",__func__);
            return -1;
        }
        while(temp->next){
            if(!strcmp(temp->next->name,disp->name)){
                printf("%s:picfmt parser is existed!\n",__func__);
                return -1;
            }
            temp = temp->next;
        }
        temp->next = disp;
        disp->next = NULL;
        /* 特别注意，如果有初始化函数要记得调用初始化函数 */
        if(disp->init){
            disp->init(disp);
        }
        return 0;
    }
}

int unregister_display_struct(struct display_struct *disp)
{
    struct display_struct **tmp;
    if(!display_list){
        printf("%s:has no exist picfmt parser!\n",__func__);
        return -1;
    }else{
        tmp = &display_list;
        while(*tmp){
            /* 找到则移除，并调用退出函数*/
            if((*tmp) == disp){
                if((*tmp)->exit)
                    (*tmp)->exit(*tmp);
                *tmp = (*tmp)->next;
                return 0;
            }
            tmp = &(*tmp)->next;
        }
        return -1;
    }
}
struct display_struct *get_display_by_name(const char *name)
{
    struct display_struct *tmp = display_list;

    while(tmp){
        if(!strcmp(name,tmp->name)){
            return tmp;
        }
        tmp = tmp->next;
    }
    printf("%s:can't found display device!\n",__func__);
    return NULL;
}
/* 打印当前已注册的调试器的信息 */
void show_display_struct(void)
{
    int i = 1;
    struct display_struct *tmp = display_list;

    while(tmp){
        printf("number:%d ; display device name:%s\n",i++,tmp->name);
        tmp = tmp->next;
    }
}

struct display_struct *get_default_display(void)
{
    return default_display;
}

void set_default_display(struct display_struct *disp)
{
    if(disp){
        default_display = disp;
    }
}

int show_pixel_data_in_default_display(int x_pos,int y_pos,struct pixel_data *pixel_data)
{
    struct display_region region;
    struct display_struct *display = get_default_display();

    region.x_pos = x_pos;
    region.y_pos = y_pos;
    region.data = pixel_data;
    display->merge_region(display,&region);
    return 0;
}

int display_init(void)
{
    int ret;
    if((ret = lcd_init())){
        return ret;
    }

    return 0;
}