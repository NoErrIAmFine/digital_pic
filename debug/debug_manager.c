#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "debug_manager.h"

static struct debuger_struct *debuger_list;
static unsigned int debug_level =4;
static unsigned int max_debug_level = 7;

int register_debuger(struct debuger_struct *debuger)
{
    struct debuger_struct *temp;
    if(!debuger_list){
        debuger_list = debuger;
        debuger->next = NULL;
        return 0;
    }else{
        temp = debuger_list;
        /* 不允许注册同名的debuger */
        if(!strcmp(temp->name,debuger->name)){
            printf("%s:debuger is existed!\n",__func__);
            return -1;
        }
        while(temp->next){
            if(!strcmp(temp->next->name,debuger->name)){
                printf("%s:debuger is existed!\n",__func__);
                return -1;
            }
            temp = temp->next;
        }
        temp->next = debuger;
        debuger->next = NULL;
        return 0;
    }
}

/* 将指定的debuger从全局链表移除 */
int unregister_debuger(struct debuger_struct *debuger)
{
    struct debuger_struct **tmp;
    if(!debuger_list){
        printf("%s:has no exist debuger!\n",__func__);
        return -1;
    }else{
        tmp = &debuger_list;
        while(*tmp){
            /* 找到则移除,同时调用退出函数*/
            if((*tmp) == debuger){
                if((*tmp)->exit)
                    (*tmp)->exit();
                *tmp = (*tmp)->next;
                return 0;
            }
            tmp = &(*tmp)->next;
        }
        printf("%s:unregistered debuger!\n",__func__);
        return -1;
    }
}

struct debuger_struct *get_debuger_by_name(const char *name)
{
    struct debuger_struct *tmp = debuger_list;

    while(tmp){
        if(!strcmp(name,tmp->name)){
            return tmp;
        }
        tmp = tmp->next;
    }
    printf("%s:can't found debuger!\n",__func__);
    return NULL;
}

/* 打印当前已注册的调试器的信息 */
void show_debuger(void)
{
    int i = 1;
    struct debuger_struct *tmp = debuger_list;

    while(tmp){
        printf("no %d : %s\n",i++,tmp->name);
        tmp = tmp->next;
    }
}

int set_debug_level(unsigned int level)
{
    if(0 <= level && level <= max_debug_level){
        debug_level = level;
        return 0;
    }
    printf("%s:invalid argument!\n",__func__);
    return -1;
}

/* 使能或禁用某个调试器
 * 输入参数为以下格式字符串：name=1	—— 使能调试器 ；name=0 —— 禁用调试器；name为调试器名字 */
int set_debug_channel(const char *str)
{
    unsigned int enable;
    char *buf_end;
    struct debuger_struct *debuger;
    char buf[20];

    strcpy(buf,str);
    
    buf_end = strchr(buf,'=');
    if(!buf_end){
        printf("%s:invalid argument!\n",__func__);
        return -1;
    }
    *buf_end = '\0';
    enable = *(buf_end + 1) - '0';
    
    debuger = get_debuger_by_name(buf);
    if(!debuger){
        printf("%s:unregistered debuger!\n",__func__);
        return -1;
    }

    /* 使能 */
    if(enable){
        if(debuger->is_initialized){
            if(debuger->enable){
                debuger->enable();
            }
            debuger->is_enable = 1;
            return 0;
        }else{
            if(debuger->init)
                debuger->init();
            if(debuger->enable)
                debuger->enable();
            debuger->is_initialized = 1;
            debuger->is_enable = 1;
            return 0;
        }
    }else{
        /* 禁止 */
        if(debuger->disable){
            debuger->disable();
        }
        debuger->is_enable = 0;
        return 0;
    }
}

int debug_print(const char *fmt,...)
{
    va_list varg;
    struct debuger_struct *tmp;
    unsigned int level;
    int buf_len;
    char str_buf[1000];

    tmp = debuger_list;
 
    /* 识别前面特定的字符串 */
    if(fmt[0] == '<' && fmt[2] == '>'){
        level = fmt[1] - '0';
        fmt+=3;
        /* 打印级别在允许的范围之内则打印 */
        if(level < 0 || level > max_debug_level){
            level = DEFAULT_DBGLEVEL;
        }
        if(level >= 0 && level <= debug_level){
             /* 先处理变参，将字符串格式化后存到一临时缓存中 */
            va_start(varg,fmt);
            buf_len = vsprintf(str_buf,fmt,varg);
            va_end(varg);
            str_buf[buf_len] = '\0';
            while(tmp){
                if(tmp->is_enable && tmp->print){
                    tmp->print(str_buf);
                }
                tmp = tmp->next;
            }
            return 0;
        }
        return 0;
    }

    /* 如果没有识别到指定打印级别的字符串，直接打印 */
    /* 先处理变参，将字符串格式化后存到一临时缓存中 */
    va_start(varg,fmt);
    buf_len = vsprintf(str_buf,fmt,varg);
    va_end(varg);
    str_buf[buf_len] = '0';
    while(tmp){
        if(tmp->is_enable && tmp->print){
            tmp->print(str_buf);
        }
        tmp = tmp->next;
    }
    return 0;
    
}

int init_debuger_channel(struct debuger_struct *debuger)
{
    if(!debuger->is_initialized){
        if(debuger->init){
            debuger->init();
            debuger->is_initialized = 1;
            return 0;
        }else{
            /* 没有初始化函数，直接置相应位即可 */
            debuger->is_initialized = 1;
            return 0;
        }
    }
    return 0;
}

/* 调用各初始化函数即可 */
int debug_init(void)
{
    int ret;
    
    if((ret = stdout_init())){
        return ret;
    }  

    return 0;
}