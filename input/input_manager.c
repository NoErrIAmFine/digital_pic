#include <pthread.h>
#include <string.h>

#include "input_manager.h"
#include "debug_manager.h"

static struct input_device *input_device_list;

/* 整个输入模块全局的事件结构，各个输入设备的事件都会被记入到该结构中，然后被其他模块获取 */
static struct my_input_event my_input_event;
static pthread_mutex_t my_input_event_mutex= PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t my_input_event_cond  = PTHREAD_COND_INITIALIZER;

int register_input_device(struct input_device *dev)
{
    struct input_device *temp;
    int ret;
    if(!input_device_list){
        input_device_list = dev;
        dev->next = NULL;
        if(dev->init){
            ret = dev->init();
            if(ret){
                DP_ERR("%s:input init failed!\n",__func__);
                return ret;
            }
        }
        return 0;
    }else{
        temp = input_device_list;
        /* 不允许同名 */
        if(!strcmp(temp->name,dev->name)){
            DP_WARNING("%s:input device is existed!\n",__func__);
            return -1;
        }
        while(temp->next){
            if(!strcmp(temp->next->name,dev->name)){
                DP_WARNING("%s:font decoder is existed!\n",__func__);
                return -1;
            }
            temp = temp->next;
        }
        temp->next = dev;
        dev->next = NULL;
        if(dev->init){
            ret = dev->init();
            if(ret){
                DP_ERR("%s:input init failed!\n",__func__);
                return ret;
            }
        }
        return 0;
    }
}

int unregister_input_device(struct input_device *dev)
{
    struct input_device **tmp;
    if(!input_device_list){
        DP_WARNING("%s:has no exist input device!\n",__func__);
        return -1;
    }else{
        tmp = &input_device_list;
        while(*tmp){
            /* 找到则移除*/
            if((*tmp) == dev){
                /* 如果有退出函数则调用之 */
                if(dev->exit){
                    dev->exit();
                }
                *tmp = (*tmp)->next;
                return 0;
            }
            tmp = &(*tmp)->next;
        }
        DP_WARNING("%s:can't found registered input device!\n",__func__);
        return -1;
    }
}

void show_input_device(void)
{
    int i = 1;
    struct input_device *tmp = input_device_list;

    while(tmp){
        DP_EMERG("number:%d ; input device name:%s\n",i++,tmp->name);
        tmp = tmp->next;
    }
}

struct input_device *get_input_device_by_name(const char *name)
{
    struct input_device *tmp = input_device_list;

    while(tmp){
        if(!strcmp(name,tmp->name)){
            return tmp;
        }
        tmp = tmp->next;
    }
    DP_WARNING("%s:can't found input device!\n",__func__);
    return NULL;
}

void report_input_event(struct my_input_event *event)
{
    pthread_mutex_lock(&my_input_event_mutex);
    my_input_event = *event;
    pthread_cond_signal(&my_input_event_cond);
    pthread_mutex_unlock(&my_input_event_mutex);
}

void get_input_event(struct my_input_event *event)
{
    pthread_mutex_lock(&my_input_event_mutex);
    pthread_cond_wait(&my_input_event_cond,&my_input_event_mutex);
    *event = my_input_event;
    pthread_mutex_unlock(&my_input_event_mutex);
}

int input_init(void)
{
    int ret;

    ret = touchscreen_init();
    if(ret){
        DP_ERR("%s:touchscreen_init failed!\n",__func__);
        return ret;
    }
    return 0;
}