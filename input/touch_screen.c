#include <tslib.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#include "input_manager.h"
#include "debug_manager.h"

static struct input_device touchscreen_input_device;

static struct tsdev *ts;
static unsigned int max_slots;
static struct ts_sample_mt *mt_events_ptr;

static void *touchscreen_thread_func(void *data)
{   
    int i;
    int ret;
    struct my_input_event event;
    while(1){
        ret = touchscreen_input_device.get_input_event(&event);
        /* 此函数会处理好竞争条件，无需在此函数处理 */
        if(!ret){
            report_input_event(&event);
        }
    }   
    return NULL;
}

static int touchscreen_input_device_init(void)
{
    int ret;
    struct input_absinfo slot;
    pthread_t pthread_id;

    /* 打开触摸屏设备，参数为NULL表示到配置文件中查找设备名称 */
    ts = ts_setup(NULL,0);
    if(!ts){
        DP_ERR("%s:ts_setup failed!\n",__func__);
        return -1;
    }

    /* 获取触摸屏支持的最大触点数 */
    ret = ioctl(ts_fd(ts),EVIOCGABS(ABS_MT_SLOT),&slot);
    if(ret < 0){
        DP_ERR("%s:ioctl get abs mt slot failed!\n",__func__);
        return -1;
    }

    max_slots = slot.maximum - slot.minimum + 1;
    DP_INFO("touchscreen max slots:%d\n",max_slots);

    mt_events_ptr = malloc(max_slots * sizeof(struct ts_sample_mt));
    if(!mt_events_ptr){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }

    /* 创建线程 */
    ret = pthread_create(&pthread_id,NULL,touchscreen_thread_func,NULL);
    if(ret){
        DP_ERR("%s:can't create thread for touchscreen,error no is:%d!\n",__func__,ret);
        return ret;
    }
    /* 分离线程 */
    pthread_detach(pthread_id);
    return 0;
}

static void touchscreen_input_device_exit(void)
{
    /* 释放资源 */
    ts_close(ts);
    free(mt_events_ptr);
}

static int touchscreen_get_input_event(struct my_input_event *event)
{
    int ret;
    static int i = 0;

read_mt:
    if(0 == i){
        ts_read_mt(ts,&mt_events_ptr,max_slots,1);
        if(ret < 0){
            DP_ERR("%s:ts_read_mt error!,error no is %d\n",__func__,ret);
            ts_close(ts);
            free(mt_events_ptr);
            return ret;
        }
    }

    for( ; i < max_slots ; ){
        if(mt_events_ptr[i].valid){     //说明数据有效
            event->type = INPUT_TYPE_TOUCHSCREEN;
            event->x_pos = mt_events_ptr[i].x;
            event->y_pos = mt_events_ptr[i].y;
            event->slot_id = mt_events_ptr[i].slot;
            event->presssure = mt_events_ptr[i].pressure;
            event->time = mt_events_ptr[i].tv;
            i++;
            return 0; 
        }else{
            i++;
            if(i == max_slots){
                i = 0;
                goto read_mt;
            }    
            continue;
        }
    }
    
    return 0;
}

static struct input_device touchscreen_input_device = {
    .name = "touchscreen",
    .init = touchscreen_input_device_init,
    .exit = touchscreen_input_device_exit,
    .get_input_event = touchscreen_get_input_event,
};

int touchscreen_init(void)
{
    return register_input_device(&touchscreen_input_device);
}