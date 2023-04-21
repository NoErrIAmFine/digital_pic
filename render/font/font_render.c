#include <stdio.h>
#include <string.h>

#include "font_render.h"
#include "debug_manager.h"

static struct font_render *font_render_list;

int register_font_render(struct font_render *render)
{
    int ret;
    struct font_render *temp;
    if(!font_render_list){
        font_render_list = render;
        render->next = NULL;
        if(render->init){
            if((ret = render->init())){
                DP_ERR("register_font_render failed!\n");
                return ret;
            }
        }
        return 0;
    }else{
        temp = font_render_list;
        /* 不允许同名 */
        if(!strcmp(temp->name,render->name)){
            printf("%s:font render is existed!\n",__func__);
            return -1;
        }
        while(temp->next){
            if(!strcmp(temp->next->name,render->name)){
                printf("%s:font render is existed!\n",__func__);
                return -1;
            }
            temp = temp->next;
        }
        temp->next = render;
        render->next = NULL;
        if(render->init){
            if((ret = render->init())){
                DP_ERR("register_font_render failed!\n");
                return ret;
            }
        }
        return 0;
    }
}

int unregister_font_render(struct font_render *render)
{
    struct font_render **tmp;
    if(!font_render_list){
        printf("%s:has no exist font render!\n",__func__);
        return -1;
    }else{
        tmp = &font_render_list;
        while(*tmp){
            /* 找到则移除*/
            if((*tmp) == render){
                if(render->exit){
                    render->exit();
                }
                *tmp = (*tmp)->next;
                return 0;
            }
            tmp = &(*tmp)->next;
        }
        printf("%s:can't found registered font render!\n",__func__);
        return -1;
    }
}

void show_font_render(void)
{
    int i = 1;
    struct font_render *tmp = font_render_list;

    while(tmp){
        printf("number:%d ; font render name:%s\n",i++,tmp->name);
        tmp = tmp->next;
    }
}

struct font_render *get_font_render_by_name(const char *name)
{
    struct font_render *tmp = font_render_list;
    while(tmp){
        if(!strcmp(name,tmp->name)){
            return tmp;
        }
        tmp = tmp->next;
    }
    
    printf("%s:can't found font render!\n",__func__);
    return NULL;
}

int font_render_init(void)
{
    int ret;

    if((ret = freetype_render_init())){
        DP_ERR("%s:freetype render init failed!\n",__func__);
        return ret;
    }

    return -1;
}