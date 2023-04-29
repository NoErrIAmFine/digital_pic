#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "page_manager.h"
#include "debug_manager.h"
#include "picfmt_manager.h"
#include "pic_operation.h"
#include "input_manager.h"
#include "render.h"

static struct page_struct interval_page;
static int interval;                    /* 表示间隔 */
static int font_size;                   /* 间隔数字的大小 */

/* 用于给区域编号的枚举 */
enum region_info{
    REGION_INCREASE = 0,
    REGION_TIME,    
    REGION_DECREASE,
    REGION_RETURN,  
    REGION_SAVE,
    REGION_NUMS,
};    

/* 以下是本页面要用到的图标信息 */
enum icon_info{
    ICON_INCREASE = 0,
    ICON_TIME_BACKGROUND,
    ICON_DECREASE,
    ICON_RETURN,
    ICON_SAVE,
    ICON_NUMS,
};

/* 图标文件名字符串数组 */
static const char *icon_file_names[] = {
    [ICON_INCREASE]         = "interval_inc.png",
    [ICON_TIME_BACKGROUND]  = "interval_time.png",
    [ICON_DECREASE]         = "interval_dec.png",
    [ICON_RETURN]           = "interval_return.png",
    [ICON_SAVE]             = "interval_save.png", 
};

/* 图标对应的区域，数组下标表示图标编号，下标对应的数组项表示该图标对应的区域,用于缩放图标 */
static const int icon_region_links[] = {
    [ICON_INCREASE]         = REGION_INCREASE,
    [ICON_TIME_BACKGROUND]  = REGION_TIME,
    [ICON_DECREASE]         = REGION_DECREASE,
    [ICON_RETURN]           = REGION_RETURN,
    [ICON_SAVE]             = REGION_SAVE,
};

static struct pixel_data icon_pixel_datas[ICON_NUMS];

/* 比较两个timeval的差值，以ms为单位 */
static int timeval_diff_ms(struct timeval * tv0, struct timeval * tv1)
{
    int time1, time2;
   
    time1 = tv0->tv_sec * 1000 + tv0->tv_usec / 1000;
    time2 = tv1->tv_sec * 1000 + tv1->tv_usec / 1000;

    time1 = time1 - time2;
    if (time1 < 0)
        time1 = -time1;
    return time1;
}

static int destory_icon_pixel_datas(struct page_struct *interval_page)
{
    int i;

    if(!interval_page->icon_prepared)
        return 0;

    for(i = 0 ; i < ICON_NUMS ; i++){
        if(icon_pixel_datas[i].buf)
            free(icon_pixel_datas[i].buf);
    }
    memset(icon_pixel_datas,0,sizeof(icon_pixel_datas));
    interval_page->icon_prepared = 0;
    return 0;
}

/* 在此函数中将会计算好页面的布局情况 */
static int interval_page_init(void)
{
    struct display_struct *default_display = get_default_display();
    struct page_layout *page_layout = &interval_page.page_layout;
    struct page_region *regions = page_layout->regions;
    int width = default_display->xres;
    int height = default_display->yres;
    int unit_distance,x_cursor,y_cursor;
    int i;

    page_layout->width  = width;
    page_layout->height = height;
    interval_page.page_mem.bpp     = default_display->bpp;
    interval_page.page_mem.width   = width;
    interval_page.page_mem.height  = height;
    interval_page.page_mem.line_bytes  = interval_page.page_mem.width * interval_page.page_mem.bpp / 8;
    interval_page.page_mem.total_bytes = interval_page.page_mem.line_bytes * interval_page.page_mem.height;

    /* 分横屏和竖屏两种情况进行处理,此页面的region结构体为静态分配 */
    if(width >= height){
        /* 横屏 */
        /*   
        *    ----------------------
        *                           unit_distance / 2
        *          inc              unit_distance     
        *         time              3 / 2 * unit_distance 
        *          dec              unit_distance    
        *                           unit_distance / 2
        *    save     return        unit_distance
        *                           unit_distance / 2
        *    ----------------------
        */
        unit_distance = height / 6;
        y_cursor = unit_distance / 2;
        x_cursor = (width - unit_distance * 9 / 2) / 2;
        /* inc */
        regions[REGION_INCREASE].x_pos = (width - unit_distance) / 2;
        regions[REGION_INCREASE].y_pos = y_cursor ;
        regions[REGION_INCREASE].height = unit_distance;
        regions[REGION_INCREASE].width = unit_distance;
        /* time */
        regions[REGION_TIME].x_pos = x_cursor;
        regions[REGION_TIME].y_pos = y_cursor + unit_distance;
        regions[REGION_TIME].height = unit_distance * 3 / 2 ;
        regions[REGION_TIME].width = (unit_distance * 3 / 2 ) * 3;
        font_size = regions[REGION_TIME].height - 20;
        /* dec */
        regions[REGION_DECREASE].x_pos = (width - unit_distance) / 2;
        regions[REGION_DECREASE].y_pos = y_cursor + (unit_distance * 5 / 2);
        regions[REGION_DECREASE].height = unit_distance;
        regions[REGION_DECREASE].width = unit_distance;
        /* save */
        x_cursor = 2 * (width - (4 * unit_distance)) / 5;
        y_cursor = unit_distance * 9 / 2;
        regions[REGION_SAVE].x_pos = x_cursor ;
        regions[REGION_SAVE].y_pos = y_cursor;
        regions[REGION_SAVE].height = unit_distance;
        regions[REGION_SAVE].width = unit_distance * 2;
        /* return */
        regions[REGION_RETURN].x_pos = x_cursor + 2 * unit_distance + x_cursor / 2;;
        regions[REGION_RETURN].y_pos = y_cursor;
        regions[REGION_RETURN].height = unit_distance;
        regions[REGION_RETURN].width = unit_distance * 2;
        for(i = 0 ; i < 5 ; i++){
            printf("regions[i].x_pos:%d,regions[i].y_pos:%d\n",regions[i].x_pos,regions[i].y_pos);
            printf("regions[i].height:%d,regions[i].width:%d\n",regions[i].height,regions[i].width);
        }
    }else{
        /* 横屏 */
        /* 暂不考虑 */ 
    }
    interval_page.already_layout = 1;
    return 0;
}

static void interval_page_exit(void)
{
    int i;

    /* 释放图标数据 */
    destory_icon_pixel_datas(&interval_page);

    /* 取消区域的内存映射,以免误操作 */
    unmap_regions_to_page_mem(&interval_page);
    interval_page.page_mem.buf  = NULL;
    interval_page.allocated     = 0;
    interval_page.share_fbmem   = 0;

    return ;
}

static void show_interval(int interval)
{
    struct display_struct *default_display = get_default_display();
    struct page_region *regions = interval_page.page_layout.regions;
    char interval_char[4];

    snprintf(interval_char,20,"%02d",interval);
    interval_char[3] = '\0';       //保险措施
    clear_pixel_data(regions[REGION_TIME].pixel_data,BACKGROUND_COLOR); /* 清理 */
    merge_pixel_data(regions[REGION_TIME].pixel_data,&icon_pixel_datas[ICON_TIME_BACKGROUND]);
    get_string_bitamp_from_buf(interval_char,20,"utf-8",regions[REGION_TIME].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xbef0,font_size);
    printf("interval_char:%s\n",interval_char);
    flush_page_region(&regions[REGION_TIME],default_display);
}

/* 此函数填充此页面布局内的内容 */
static int interval_page_fill_layout(struct page_struct *interval_page)
{
    int i,ret;
    int region_num = interval_page->page_layout.region_num;
    struct page_region *regions = interval_page->page_layout.regions;
    struct display_struct *default_display = get_default_display();
    struct page_struct *auto_page;
    struct autoplay_private *auto_priv;

    if(!interval_page->already_layout){
        interval_page_init();
    }

    if(!interval_page->allocated){
        /* 直接将 auto page 对应的内存映射到显存上，省的多一道复制 */
        interval_page->page_mem.bpp         = default_display->bpp;
        interval_page->page_mem.width       = default_display->xres;
        interval_page->page_mem.height      = default_display->yres;
        interval_page->page_mem.line_bytes  = interval_page->page_mem.width * interval_page->page_mem.bpp / 8;
        interval_page->page_mem.total_bytes = interval_page->page_mem.line_bytes * interval_page->page_mem.height; 
        interval_page->page_mem.buf         = default_display->buf;
        interval_page->allocated            = 1;
        interval_page->share_fbmem          = 1;
    }

    /* 清理或填充一个背景 */
    clear_pixel_data(&interval_page->page_mem,BACKGROUND_COLOR);

    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!interval_page->region_mapped){
        ret = remap_regions_to_page_mem(interval_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return ret;
        }
    }

    /* 准备图标数据 */
    if(!interval_page->icon_prepared){
        ret = prepare_icon_pixel_datas(interval_page,icon_pixel_datas,icon_file_names,icon_region_links,ICON_NUMS);
        if(ret){
            DP_ERR("%s:prepare_icon_pixel_datas failed!\n",__func__);
            return ret;
        }
    }

    /* 挨个填充各个区域 */ 
    /* 填充各图标 */
    for(i = 0 ; i < 5 ;i++){
        ret = merge_pixel_data(regions[i].pixel_data,&icon_pixel_datas[i]);
        if(ret){
            DP_ERR("%s:fill region failed!\n",__func__);
            return ret;
        }
    }
    /* 填充间隔值 */
    /* 先到连播页面获取当前默认间隔值 */
    auto_page = get_page_by_name("autoplay_page");
    if(!auto_page){
        DP_ERR("%s:get autoplay page failed!\n",__func__);
        return -1;
    }
    auto_priv = auto_page->private_data;
    interval = auto_priv->autoplay_interval;
    show_interval(interval);
    return 0;
}

static void __interval_adj(struct my_input_event *event,int inc)
{
    #define MAX_INTERVAL 99                 /* 最大间隔值为 100 s */
    static int fast = 0;                    /* 长按两秒后进入快速计数，每50ms增加一次 */
    static int press = 0;                   /* 处于被按下状态 */
    static struct timeval pre_time = {0};   /* 前一次被按下的时间 */

    if(event->presssure){
        if(press){
            if(fast){
                if(timeval_diff_ms(&event->time,&pre_time) >= 50){
                    if(inc){    //增加
                        if((interval += 1) > 99)
                        interval = 99;
                    }else{      //减小
                        if((interval -= 1) < 0)
                        interval = 0;
                    }
                    /* 将数字显示到屏幕上 */
                    show_interval(interval);
                    pre_time = event->time;
                }
            }else{
                /* 长按2s秒进入快速模式 */
                if(timeval_diff_ms(&event->time,&pre_time) >= 2000){
                    fast = 1;
                    if(inc){    //增加
                        if((interval += 1) > 99)
                        interval = 99;
                    }else{      //减小
                        if((interval -= 1) < 0)
                        interval = 0;
                    }
                    show_interval(interval);
                    pre_time = event->time;
                }
            }
        }else{
            press = 1;
            pre_time = event->time;
        }
    }else{
        if(press){
            fast = 0;
            press = 0;
            pre_time.tv_sec = 0;
            pre_time.tv_usec = 0;
            if(!fast){
                if(inc){    //增加
                    if((interval += 1) > 99)
                    interval = 99;
                }else{      //减小
                    if((interval -= 1) < 0)
                    interval = 0;
                }
                show_interval(interval);
            }   
        }
    }
}

static void increase_cb_func(struct my_input_event *event)
{
    __interval_adj(event,1);
}
 
static void decrease_cb_func(struct my_input_event *event) 
{
    __interval_adj(event,0);
}

static void save_cb_func(void) 
{
    struct page_struct *auto_page;
    struct autoplay_private *auto_priv;

    auto_page = get_page_by_name("autoplay_page");
    if(!auto_page){
        DP_ERR("%s:get auto page failed!\n",__func__);
        return ;
    }
    auto_priv = auto_page->private_data;
    auto_priv->autoplay_interval = interval;
}

/* 主要功能：映射内存；解析要显示的数据；while循环检测输入*/
static int interval_page_run(struct page_param *pre_page_param)
{
    int ret;
    int pre_region_index = -1;
    int region_index;
    int slot_id = -1;
    int pressure = 0;
    struct display_struct *default_display = get_default_display();
    struct page_region *regions = interval_page.page_layout.regions;
    struct page_struct *next_page;
    struct page_param page_param;

    if(!interval_page.already_layout){
        interval_page_init();
    }

    if(!interval_page.allocated){
        /* 直接将 auto page 对应的内存映射到显存上，省的多一道复制 */
        interval_page.page_mem.bpp         = default_display->bpp;
        interval_page.page_mem.width       = default_display->xres;
        interval_page.page_mem.height      = default_display->yres;
        interval_page.page_mem.line_bytes  = interval_page.page_mem.width * interval_page.page_mem.bpp / 8;
        interval_page.page_mem.total_bytes = interval_page.page_mem.line_bytes * interval_page.page_mem.height; 
        interval_page.page_mem.buf         = default_display->buf;
        interval_page.allocated            = 1;
        interval_page.share_fbmem          = 1;
    }

    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!interval_page.region_mapped){
        ret = remap_regions_to_page_mem(&interval_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return ret;
        }
    }

    /* 准备图标数据 */
    if(!interval_page.icon_prepared){
        ret = prepare_icon_pixel_datas(&interval_page,icon_pixel_datas,icon_file_names,icon_region_links,ICON_NUMS);
        if(ret){
            DP_ERR("%s:prepare_icon_pixel_datas failed!\n",__func__);
            return ret;
        }
    }
    
    /* 填充页面 */
    ret = interval_page_fill_layout(&interval_page);
    if(ret){
        DP_ERR("%s:interval_page_fill_layout failed!\n",__func__);
        return ret;
    }   
    
    /* 因为页面与显存共享一块内存，所以不用刷新 */
    
    /* 检测输入事件的循环 */
    while(1){
        struct my_input_event event;
        region_index = get_input_event_for_page(&interval_page,&event);
        printf("%s-region_index:%d\n",__func__,region_index);
        /* 指定区域外的点击事件不响应 */
        if(region_index < 0 || region_index == REGION_TIME){
            if(event.presssure){
                continue;
            }else{
                if(pre_region_index >= 0){
                    press_region(&regions[pre_region_index],0,0);
                    flush_page_region(&regions[pre_region_index],default_display);
                    pre_region_index = -1;
                    pressure = 0;
                    slot_id = -1;
                }
                continue;
            }
        }

        /* 触摸屏是支持多触点的，但在这里只响应一个触点 */
        if(slot_id >= 0 && event.slot_id != slot_id){
            continue;
        }

        if(event.presssure){        //按下
            if(!pressure){          //如果之前未按下
                pre_region_index = region_index;
                pressure = 1;
                slot_id = event.slot_id;
                /* 反转按下区域的颜色 */
                press_region(&regions[region_index],1,0);
                flush_page_region(&regions[region_index],default_display);
            }
            /* 对于增加减小按钮，按下和松开时间都传给专用函数处理 */
            if(region_index == REGION_INCREASE){
                increase_cb_func(&event);
            }else if(region_index == REGION_DECREASE){
                decrease_cb_func(&event);
            }
        }else{                     //松开
            /* 按下和松开的是同一个区域，这是一次有效的点击 */
            if(pre_region_index == region_index){
                page_param.id = interval_page.id;
                switch(region_index){
                    case REGION_INCREASE:
                        increase_cb_func(&event);
                        break;
                    case REGION_DECREASE:   
                        decrease_cb_func(&event);             
                        break;
                    case REGION_SAVE:
                        save_cb_func();
                        break;
                    case REGION_RETURN:
                        interval_page_exit();       /* 释放资源 */
                        return 0;
                    default:
                        continue;  
                }
                press_region(&regions[region_index],0,0);
                flush_page_region(&regions[region_index],default_display);
            }else if(pre_region_index >= 0){
                press_region(&regions[pre_region_index],0,0);
                flush_page_region(&regions[pre_region_index],default_display);
            }
            pre_region_index = -1;
            pressure = 0;
            slot_id = -1;
        }
    }
    return 0;
}

static struct page_region interval_page_regions[] = {
    PAGE_REGION(REGION_INCREASE,0,&interval_page),
    PAGE_REGION(REGION_TIME,0,&interval_page),
    PAGE_REGION(REGION_DECREASE,0,&interval_page),
    PAGE_REGION(REGION_RETURN,0,&interval_page),
    PAGE_REGION(REGION_SAVE,0,&interval_page),
};

static struct page_struct interval_page = {
    .name = "interval_page",
    .page_layout = {
        .regions = interval_page_regions,
        .region_num = sizeof(interval_page_regions) / sizeof(struct page_region),
    },
    .init = interval_page_init,
    .exit = interval_page_exit,
    .run  = interval_page_run,
    .allocated = 0,
};

int interval_init(void)
{
    return register_page_struct(&interval_page);
}