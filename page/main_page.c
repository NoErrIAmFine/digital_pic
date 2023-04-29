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

static struct page_struct main_page;

/* 用于给区域编号的枚举 */
enum region_info{
    REGION_BROWSE_ICON = 0, 
    REGION_BROWSE_TEXT,  
    REGION_AUTOPLAY_ICON,
    REGION_AUTOPLAY_TEXT,
    REGION_SETTING_ICON, 
    REGION_SETTING_TEXT, 
    REGION_NUMS,
};

/* 以下是本页面要用到的图标信息 */
enum icon_info{
    ICON_BROWSE_MODE = 0,
    ICON_AUTO_PLAY_MODE,
    ICON_SETTING,
    ICON_NUMS,
};

/* 图标文件名字符串数组 */
static const char *icon_file_names[] = {
    [ICON_BROWSE_MODE]      = "browse_mode.png",
    [ICON_AUTO_PLAY_MODE]   = "periodic_mode.png",
    [ICON_SETTING]          = "setting.png",
};

/* 图标对应的区域，数组下标表示图标编号，下标对应的数组项表示该图标对应的区域,用于缩放图标 */
static const int icon_region_links[] = {
    [ICON_BROWSE_MODE]      = REGION_BROWSE_ICON,
    [ICON_AUTO_PLAY_MODE]   = REGION_AUTOPLAY_ICON,
    [ICON_SETTING]          = REGION_SETTING_ICON,
};
static struct pixel_data icon_pixel_datas[ICON_NUMS];

/* 在此函数中将会计算好页面的布局情况 */
static int main_page_init(void)
{
    struct display_struct *default_display = get_default_display();
    struct page_layout *page_layout = &main_page.page_layout;
    struct page_region *regions = page_layout->regions;
    int width = default_display->xres;
    int height = default_display->yres;
    int unit_distance,x_cursor,y_cursor;

    page_layout->width  = width;
    page_layout->height = height;
    main_page.page_mem.bpp     = default_display->bpp;
    main_page.page_mem.width   = width;
    main_page.page_mem.height  = height;
    main_page.page_mem.line_bytes  = main_page.page_mem.width * main_page.page_mem.bpp / 8;
    main_page.page_mem.total_bytes = main_page.page_mem.line_bytes * main_page.page_mem.height;

    /* 分横屏和竖屏两种情况进行处理 */
    if(width >= height){
        /* 横屏 */
        /*   ----------------------
	     *                                              1/2 * unit_distance
	     *    browse_mode.bmp   文本（浏览模式）           unit_distance
	     *                                              1/2 * unit_distance
	     *    continue_mod.bmp  文本（连播模式）           unit_distance
	     *                                              1/2 * unit_distance
	     *      setting.bmp     文本（设置）              unit_distance
	     *                                              1/2 * unit_distance
	     *    ----------------------
	     */
        unit_distance = height / 5;
        y_cursor = unit_distance / 2;
        x_cursor = (width - unit_distance * 4) / 2;
        /* "浏览模式"图标和文本 */
        regions[REGION_BROWSE_ICON].x_pos = x_cursor;
        regions[REGION_BROWSE_ICON].y_pos = y_cursor;
        regions[REGION_BROWSE_ICON].height = unit_distance;
        regions[REGION_BROWSE_ICON].width = unit_distance;

        regions[REGION_BROWSE_TEXT].x_pos = x_cursor + unit_distance;
        regions[REGION_BROWSE_TEXT].y_pos = y_cursor;
        regions[REGION_BROWSE_TEXT].height = unit_distance;
        regions[REGION_BROWSE_TEXT].width = unit_distance * 3;

        /* "连播模式"图标和文本 */
        y_cursor += unit_distance * 3 / 2;
        regions[REGION_AUTOPLAY_ICON].x_pos = x_cursor;
        regions[REGION_AUTOPLAY_ICON].y_pos = y_cursor;
        regions[REGION_AUTOPLAY_ICON].height = unit_distance;
        regions[REGION_AUTOPLAY_ICON].width = unit_distance;

        regions[REGION_AUTOPLAY_TEXT].x_pos = x_cursor + unit_distance;
        regions[REGION_AUTOPLAY_TEXT].y_pos = y_cursor;
        regions[REGION_AUTOPLAY_TEXT].height = unit_distance;
        regions[REGION_AUTOPLAY_TEXT].width = unit_distance * 3;

        /* "设置"图标和文本 */
         y_cursor += unit_distance * 3 / 2;
        regions[REGION_SETTING_ICON].x_pos = x_cursor;
        regions[REGION_SETTING_ICON].y_pos = y_cursor;
        regions[REGION_SETTING_ICON].height = unit_distance;
        regions[REGION_SETTING_ICON].width = unit_distance;

        regions[REGION_SETTING_TEXT].x_pos = x_cursor + unit_distance;
        regions[REGION_SETTING_TEXT].y_pos = y_cursor;
        regions[REGION_SETTING_TEXT].height = unit_distance;
        regions[REGION_SETTING_TEXT].width = unit_distance * 3;
    }else{
        /* 横屏 */
        unit_distance = width / 6;
        y_cursor = (height - unit_distance * 4) / 2;
        x_cursor = (width - unit_distance * 4) / 2;
        /* "浏览模式"图标和文本 */
        regions[REGION_BROWSE_ICON].x_pos = x_cursor;
        regions[REGION_BROWSE_ICON].y_pos = y_cursor;
        regions[REGION_BROWSE_ICON].height = unit_distance;
        regions[REGION_BROWSE_ICON].width = unit_distance;

        regions[REGION_BROWSE_TEXT].x_pos = x_cursor + unit_distance;
        regions[REGION_BROWSE_TEXT].y_pos = y_cursor;
        regions[REGION_BROWSE_TEXT].height = unit_distance;
        regions[REGION_BROWSE_TEXT].width = unit_distance * 3;

        /* "连播模式"图标和文本 */
        y_cursor += unit_distance * 2;
        regions[REGION_AUTOPLAY_ICON].x_pos = x_cursor;
        regions[REGION_AUTOPLAY_ICON].y_pos = y_cursor;
        regions[REGION_AUTOPLAY_ICON].height = unit_distance;
        regions[REGION_AUTOPLAY_ICON].width = unit_distance;

        regions[REGION_AUTOPLAY_TEXT].x_pos = x_cursor + unit_distance;
        regions[REGION_AUTOPLAY_TEXT].y_pos = y_cursor;
        regions[REGION_AUTOPLAY_TEXT].height = unit_distance;
        regions[REGION_AUTOPLAY_TEXT].width = unit_distance * 3;

        /* "设置"图标和文本 */
         y_cursor += unit_distance * 2;
        regions[REGION_SETTING_ICON].x_pos = x_cursor;
        regions[REGION_SETTING_ICON].y_pos = y_cursor;
        regions[REGION_SETTING_ICON].height = unit_distance;
        regions[REGION_SETTING_ICON].width = unit_distance;

        regions[REGION_SETTING_TEXT].x_pos = x_cursor + unit_distance;
        regions[REGION_SETTING_TEXT].y_pos = y_cursor;
        regions[REGION_SETTING_TEXT].height = unit_distance;
        regions[REGION_SETTING_TEXT].width = unit_distance * 3;
    }
    main_page.already_layout = 1;
    return 0;
}

static void main_page_exit(void)
{
    /* 还没弄清楚这个函数具体做什么 */
    return ;
}

static int remap_region_to_page_mem(struct page_region *region,struct page_struct *page)
{
    struct pixel_data *region_data  = region->pixel_data;
    struct pixel_data *page_data    = &page->page_mem;
    struct page_layout *layout      = &page->page_layout;
    unsigned char *page_buf = page_data->buf;
    int i;

    /* 只处理region完全在page范围内的情况 */
    if(region->x_pos >= layout->width || region->y_pos >= layout->height || \
      (region->x_pos + region->width) >= layout->width || (region->y_pos + region->height) >= layout->height){
          DP_ERR("%s:invalid region!\n");
          return -1;
      }

    region_data->bpp    = page_data->bpp;
    region_data->width  = region->width;
    region_data->height = region->height;
    region_data->line_bytes     = region_data->width * region_data->bpp / 8;
    region_data->total_bytes    = region_data->line_bytes * region_data->height;
    region_data->rows_buf = malloc(sizeof(char *) * region->height);
    if(!region_data->rows_buf){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -1;
    }
    
    page_buf += (page_data->line_bytes * region->y_pos + region->x_pos * page_data->bpp / 8);
    for(i = 0 ; i < region->height ; i++){
        region_data->rows_buf[i] = page_buf;
        page_buf += page_data->line_bytes;
    }
    region_data->in_rows = 1;
    return 0;
}

/* 此函数填充此页面布局内的内容 */
static int main_page_fill_layout(struct page_struct *main_page)
{
    int i,ret;
    int region_num = main_page->page_layout.region_num;
    struct page_region *regions = main_page->page_layout.regions;
    struct pixel_data temp;
    struct display_struct *default_display = get_default_display();
    
    if(!main_page->already_layout){
        main_page_init();
    }

    if(!main_page->allocated){
        /* 直接将 auto page 对应的内存映射到显存上，省的多一道复制 */
        main_page->page_mem.bpp         = default_display->bpp;
        main_page->page_mem.width       = default_display->xres;
        main_page->page_mem.height      = default_display->yres;
        main_page->page_mem.line_bytes  = main_page->page_mem.width * main_page->page_mem.bpp / 8;
        main_page->page_mem.total_bytes = main_page->page_mem.line_bytes * main_page->page_mem.height; 
        main_page->page_mem.buf         = default_display->buf;
        main_page->allocated            = 1;
        main_page->share_fbmem          = 1;
    }
    clear_pixel_data(&main_page->page_mem,BACKGROUND_COLOR);

    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!main_page->region_mapped){
        ret = remap_regions_to_page_mem(main_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return ret;
        }
    }

    /* 准备图标数据 */
    if(!main_page->icon_prepared){
        ret = prepare_icon_pixel_datas(main_page,icon_pixel_datas,icon_file_names,icon_region_links,ICON_NUMS);
        if(ret){
            DP_ERR("%s:prepare_icon_pixel_datas failed!\n",__func__);
            return ret;
        }
    }

    /* 挨个填充各个区域 */ 
    /* 先填充三个图标 */
    ret = clear_pixel_data(regions[REGION_BROWSE_ICON].pixel_data,BACKGROUND_COLOR);
    ret |= merge_pixel_data(regions[REGION_BROWSE_ICON].pixel_data,&icon_pixel_datas[ICON_BROWSE_MODE]);
    if(ret){
        DP_ERR("%s:fill icon region failed!\n",__func__);
        return ret;
    }

    ret = clear_pixel_data(regions[REGION_AUTOPLAY_ICON].pixel_data,BACKGROUND_COLOR);
    ret |= merge_pixel_data(regions[REGION_AUTOPLAY_ICON].pixel_data,&icon_pixel_datas[ICON_AUTO_PLAY_MODE]);
    if(ret){
        DP_ERR("%s:fill icon region failed!\n",__func__);
        return ret;
    }

    ret = clear_pixel_data(regions[REGION_SETTING_ICON].pixel_data,BACKGROUND_COLOR);
    ret |= merge_pixel_data(regions[REGION_SETTING_ICON].pixel_data,&icon_pixel_datas[ICON_SETTING]);
    if(ret){
        DP_ERR("%s:fill icon region failed!\n",__func__);
        return ret;
    }
    
    /* 再填充三段文本 */
    /* 浏览目录;连播模式;设置 */
    get_string_bitamp_from_buf("浏览目录",0,"utf-8",regions[REGION_BROWSE_TEXT].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xff00,60);
    get_string_bitamp_from_buf("连播模式",0,"utf-8",regions[REGION_AUTOPLAY_TEXT].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xff00,60);
    get_string_bitamp_from_buf("设    置",0,"utf-8",regions[REGION_SETTING_TEXT].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xff00,60);

    return 0;
}

/* 主要功能：分配内存；解析要显示的数据；while循环检测输入*/
static int main_page_run(struct page_param *pre_page_param)
{
    int ret;
    int pre_region_index = -1;
    int region_index;
    int slot_id = -1;
    int pressure = 0;
    struct display_struct *default_display = get_default_display();
    struct page_region *regions = main_page.page_layout.regions;
    struct page_struct *next_page;
    struct page_param page_param;

    if(!main_page.already_layout){
        main_page_init();
    }

    if(!main_page.allocated){
        /* 直接将 auto page 对应的内存映射到显存上，省的多一道复制 */
        main_page.page_mem.bpp         = default_display->bpp;
        main_page.page_mem.width       = default_display->xres;
        main_page.page_mem.height      = default_display->yres;
        main_page.page_mem.line_bytes  = main_page.page_mem.width * main_page.page_mem.bpp / 8;
        main_page.page_mem.total_bytes = main_page.page_mem.line_bytes * main_page.page_mem.height; 
        main_page.page_mem.buf         = default_display->buf;
        main_page.allocated            = 1;
        main_page.share_fbmem          = 1;
    }

    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!main_page.region_mapped){
        ret = remap_regions_to_page_mem(&main_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return ret;
        }
    }

    /* 准备图标数据 */
    if(!main_page.icon_prepared){
        ret = prepare_icon_pixel_datas(&main_page,icon_pixel_datas,icon_file_names,icon_region_links,ICON_NUMS);
        if(ret){
            DP_ERR("%s:prepare_icon_pixel_datas failed!\n",__func__);
            return ret;
        }
    }
    
    /* 填充页面 */
    ret = main_page_fill_layout(&main_page);
    if(ret){
        DP_ERR("%s:main_page_fill_layout failed!\n",__func__);
        return ret;
    }   
    
    /* 因为页面与显存共享一块内存，所以不用刷新 */
    
    /* 检测输入事件的循环 */
    while(1){
        struct my_input_event event;
        region_index = get_input_event_for_page(&main_page,&event);

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
                page_param.id = main_page.id;
                switch(region_index){
                    case 0:
                    case 1:
                        //浏览目录
                        next_page = get_page_by_name("browse_page");
                        if(!next_page){
                            DP_ERR("%s:enter browse page failed!\n",__func__);
                            break;
                        }
                        next_page->run(&page_param);
                        /* 返回后重新渲染此页 */
                        ret = main_page_fill_layout(&main_page);
                        if(ret){
                            DP_ERR("%s:main_page_fill_layout failed!\n",__func__);
                            return ret;
                        } 
                        continue;
                    case 2:
                    case 3:
                        //连播模式
                        next_page = get_page_by_name("autoplay_page");
                        if(!next_page){
                            DP_ERR("%s:enter browse page failed!\n",__func__);
                            break;
                        }
                        next_page->run(&page_param);
                        /* 返回后重新渲染此页 */
                         ret = main_page_fill_layout(&main_page);
                        if(ret){
                            DP_ERR("%s:main_page_fill_layout failed!\n",__func__);
                            return ret;
                        } 
                        continue;
                    case 4:
                    case 5:
                        //设置
                        next_page = get_page_by_name("setting_page");
                        if(!next_page){
                            DP_ERR("%s:enter browose page failed!\n",__func__);
                            break;
                        }
                        next_page->run(&page_param);
                        /* 返回后重新渲染此页 */
                        default_display->flush_buf(default_display,main_page.page_mem.buf,main_page.page_mem.total_bytes);
                        break;
                }
                invert_region(regions[pre_region_index].pixel_data);
                flush_page_region(&regions[pre_region_index],default_display);
            }else{
                invert_region(regions[pre_region_index].pixel_data);
                flush_page_region(&regions[pre_region_index],default_display);
            }
            pressure = 0;
            slot_id = -1;
        }
    }
    return 0;
}

static struct page_region main_page_regions[] = {
    PAGE_REGION(0,0,&main_page),
    PAGE_REGION(1,0,&main_page),
    PAGE_REGION(2,0,&main_page),
    PAGE_REGION(3,0,&main_page),
    PAGE_REGION(4,0,&main_page),
    PAGE_REGION(5,0,&main_page),
};

static struct page_struct main_page = {
    .name = "main_page",
    .page_layout = {
        .regions = main_page_regions,
        .region_num = 6,
    },
    .init = main_page_init,
    .exit = main_page_exit,
    .run  = main_page_run,
    .allocated = 0,
};

int main_init(void)
{
    return register_page_struct(&main_page);
}