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

static struct page_struct setting_page;

/* 区域的宏定义 */
enum region_info{
    REGION_SELECT_DIR = 0,
    REGION_SET_INTERVAL,
    REGION_RETURN,
    REGION_NUMS,    
};

/* 以下是本页面要用到的图标信息 */
enum icon_info{
    ICON_SELECT_DIR = 0,
    ICON_SET_INTERVAL,
    ICON_RETURN,
    ICON_NUMS,
};

/* 图标文件名字符串数组 */
static const char *icon_file_names[] = {
    [ICON_SELECT_DIR]     = "setting_select_dir.png",
    [ICON_SET_INTERVAL]   = "setting_set_interval.png",
    [ICON_RETURN]         = "setting_return.png",
};

/* 图标对应的区域，数组下标表示图标编号，下标对应的数组项表示该图标对应的区域,用于缩放图标 */
static const int icon_region_links[] = {
    [ICON_SELECT_DIR]     = REGION_SELECT_DIR,
    [ICON_SET_INTERVAL]   = REGION_SET_INTERVAL,
    [ICON_RETURN]         = REGION_RETURN,
};

static struct pixel_data icon_pixel_datas[ICON_NUMS];

/* 在此函数中将会计算好页面的布局情况 */
static int setting_page_init(void)
{
    struct display_struct *default_display = get_default_display();
    struct page_layout *page_layout = &setting_page.page_layout;
    struct page_region *regions = page_layout->regions;
    int width = default_display->xres;
    int height = default_display->yres;
    int unit_distance,x_cursor,y_cursor;
    int i;

    page_layout->width  = width;
    page_layout->height = height;
    printf("page_layout->width:%d,page_layout->height:%d\n",page_layout->width,page_layout->height);
    setting_page.page_mem.bpp     = default_display->bpp;
    setting_page.page_mem.width   = width;
    setting_page.page_mem.height  = height;
    setting_page.page_mem.line_bytes  = setting_page.page_mem.width * setting_page.page_mem.bpp / 8;
    setting_page.page_mem.total_bytes = setting_page.page_mem.line_bytes * setting_page.page_mem.height;

    /* 分横屏和竖屏两种情况进行处理,此页面的region结构体为静态分配 */
    if(width >= height){
        /* 横屏 */
        /*   ----------------------
	     *                                          1/2 * unit_distance
	     *    选择文件夹                              unit_distance
	     *                                          1/2 * unit_distance
	     *     设置间隔                               文本（连播模式）           unit_distance
	     *                                          1/2 * unit_distance
	     *      返回                                 unit_distance
	     *                                          1/2 * unit_distance
	     *    ----------------------
	     */
        unit_distance = height / 5;
        y_cursor = unit_distance / 2;
        x_cursor = (width - unit_distance * 3) / 2;

        for(i = 0 ; i < 3 ; i++){
            regions[i].x_pos = x_cursor;
            regions[i].y_pos = y_cursor + i * (unit_distance * 3 / 2);
            regions[i].height = unit_distance;
            regions[i].width = unit_distance * 3;
        }
    }else{
        /* 横屏 */
        unit_distance = width / 5;
        y_cursor = (height - unit_distance * 4) / 2;
        x_cursor = (width - unit_distance * 3) / 2;

        for(i = 0 ; i < 3 ; i++){
            regions[i].x_pos = x_cursor;
            regions[i].y_pos = y_cursor + i * (unit_distance * 3 / 2);
            regions[i].height = unit_distance;
            regions[i].width = unit_distance * 3;
        }
    }
    setting_page.already_layout = 1;
    return 0;
}

static void setting_page_exit(void)
{
    /* 还没弄清楚这个函数具体做什么 */
    return ;
}

/* 此函数填充此页面布局内的内容 */
static int setting_page_fill_layout(struct page_struct *setting_page)
{
    int i,ret;
    int region_num = setting_page->page_layout.region_num;
    struct page_region *regions = setting_page->page_layout.regions;
    struct display_struct *default_display = get_default_display();
    
    if(!setting_page->already_layout){
        setting_page_init();
    }

    if(!setting_page->allocated){
        /* 直接将 auto page 对应的内存映射到显存上，省的多一道复制 */
        setting_page->page_mem.bpp         = default_display->bpp;
        setting_page->page_mem.width       = default_display->xres;
        setting_page->page_mem.height      = default_display->yres;
        setting_page->page_mem.line_bytes  = setting_page->page_mem.width * setting_page->page_mem.bpp / 8;
        setting_page->page_mem.total_bytes = setting_page->page_mem.line_bytes * setting_page->page_mem.height; 
        setting_page->page_mem.buf         = default_display->buf;
        setting_page->allocated            = 1;
        setting_page->share_fbmem          = 1;
    }

    /* 清理或填充一个背景 */
    clear_pixel_data(&setting_page->page_mem,BACKGROUND_COLOR);

    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!setting_page->region_mapped){
        ret = remap_regions_to_page_mem(setting_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return ret;
        }
    }

    /* 准备图标数据 */
    if(!setting_page->icon_prepared){
        ret = prepare_icon_pixel_datas(setting_page,icon_pixel_datas,icon_file_names,icon_region_links,ICON_NUMS);
        if(ret){
            DP_ERR("%s:prepare_icon_pixel_datas failed!\n",__func__);
            return ret;
        }
    }

    /* 挨个填充各个区域 */ 
    /* 先填充三个图标 */
    for(i = 0 ; i < 3 ;i++){
        ret = merge_pixel_data(regions[i].pixel_data,&icon_pixel_datas[i]);
        if(ret){
            DP_ERR("%s:fill region failed!\n",__func__);
            return ret;
        }
    }
    return 0;
}

static int set_interval_cb_func(void)
{
    int ret;
    struct page_struct *next_page;
    struct page_param param;

    next_page = get_page_by_name("interval_page");
    if(!next_page){
        DP_ERR("%s:get page failed!\n",__func__);
        return -1;
    }

    param.id = setting_page.id;
    next_page->run(&param);

    /* 返回后重新刷新页面 */
    ret = setting_page_fill_layout(&setting_page);
    if(ret){
        DP_ERR("%s:setting_page_fill_layout failed!\n",__func__);
        return ret;
    } 
    return 0;
}
 
static void select_dir_cb_func(void) 
{
    struct page_struct *next_page;
    struct page_param param;

    next_page = get_page_by_name("browse_page");
    if(!next_page){
        DP_ERR("%s:get page failed!\n",__func__);
        return ;
    }

    param.id = setting_page.id;
    next_page->run(&param);
}

/* 主要功能：映射内存；解析要显示的数据；while循环检测输入*/
static int setting_page_run(struct page_param *pre_page_param)
{
    int ret;
    int pre_region_index = -1;
    int region_index;
    int slot_id = -1;
    int pressure = 0;
    struct display_struct *default_display = get_default_display();
    struct page_region *regions = setting_page.page_layout.regions;
    struct page_struct *next_page;
    struct page_param page_param;

    if(!setting_page.already_layout){
        setting_page_init();
    }

    if(!setting_page.allocated){
        /* 直接将 auto page 对应的内存映射到显存上，省的多一道复制 */
        setting_page.page_mem.bpp         = default_display->bpp;
        setting_page.page_mem.width       = default_display->xres;
        setting_page.page_mem.height      = default_display->yres;
        setting_page.page_mem.line_bytes  = setting_page.page_mem.width * setting_page.page_mem.bpp / 8;
        setting_page.page_mem.total_bytes = setting_page.page_mem.line_bytes * setting_page.page_mem.height; 
        setting_page.page_mem.buf         = default_display->buf;
        setting_page.allocated            = 1;
        setting_page.share_fbmem          = 1;
    }
    printf("%s-%d\n",__func__,__LINE__);
    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!setting_page.region_mapped){
        ret = remap_regions_to_page_mem(&setting_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return ret;
        }
    }
    printf("%s-%d\n",__func__,__LINE__);
    /* 准备图标数据 */
    if(!setting_page.icon_prepared){
        ret = prepare_icon_pixel_datas(&setting_page,icon_pixel_datas,icon_file_names,icon_region_links,ICON_NUMS);
        if(ret){
            DP_ERR("%s:prepare_icon_pixel_datas failed!\n",__func__);
            return ret;
        }
    }
    printf("%s-%d\n",__func__,__LINE__);
    /* 填充页面 */
    ret = setting_page_fill_layout(&setting_page);
    if(ret){
        DP_ERR("%s:setting_page_fill_layout failed!\n",__func__);
        return ret;
    }   
    printf("%s-%d\n",__func__,__LINE__);
    /* 因为页面与显存共享一块内存，所以不用刷新 */
    
    /* 检测输入事件的循环 */
    while(1){
        struct my_input_event event;
        region_index = get_input_event_for_page(&setting_page,&event);

        /* 指定区域外的点击事件不响应 */
        if(region_index < 0){
            continue;
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
                invert_region(regions[region_index].pixel_data);
                flush_page_region(&regions[region_index],default_display);
            }
        }else{                     //松开
            /* 按下和松开的是同一个区域，这是一次有效的点击 */
            if(pre_region_index == region_index){
                page_param.id = setting_page.id;
                switch(region_index){
                    case REGION_SELECT_DIR:
                        select_dir_cb_func();
                        continue;
                    case REGION_SET_INTERVAL:   
                        set_interval_cb_func();             
                        continue;
                    case REGION_RETURN:
                        return 0;
                        continue;
                    default:
                        continue;     
                }
                invert_region(regions[pre_region_index].pixel_data);
                flush_page_region(&regions[pre_region_index],default_display);
            }else{
                invert_region(regions[pre_region_index].pixel_data);
                flush_page_region(&regions[pre_region_index],default_display);
            }
            pre_region_index = -1;
            pressure = 0;
            slot_id = -1;
        }
    }
    return 0;
}

static struct page_region setting_page_regions[] = {
    PAGE_REGION(0,0,&setting_page),
    PAGE_REGION(1,0,&setting_page),
    PAGE_REGION(2,0,&setting_page),
};

static struct page_struct setting_page = {
    .name = "setting_page",
    .page_layout = {
        .regions = setting_page_regions,
        .region_num = sizeof(setting_page_regions) / sizeof(struct page_region),
    },
    .init = setting_page_init,
    .exit = setting_page_exit,
    .run  = setting_page_run,
    .allocated = 0,
};

int setting_init(void)
{
    return register_page_struct(&setting_page);
}