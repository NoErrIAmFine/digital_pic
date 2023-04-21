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
        regions[0].x_pos = x_cursor;
        regions[0].y_pos = y_cursor;
        regions[0].height = unit_distance;
        regions[0].width = unit_distance;

        regions[1].x_pos = x_cursor + unit_distance;
        regions[1].y_pos = y_cursor;
        regions[1].height = unit_distance;
        regions[1].width = unit_distance * 3;

        /* "连播模式"图标和文本 */
        y_cursor += unit_distance * 3 / 2;
        regions[2].x_pos = x_cursor;
        regions[2].y_pos = y_cursor;
        regions[2].height = unit_distance;
        regions[2].width = unit_distance;

        regions[3].x_pos = x_cursor + unit_distance;
        regions[3].y_pos = y_cursor;
        regions[3].height = unit_distance;
        regions[3].width = unit_distance * 3;

        /* "设置"图标和文本 */
         y_cursor += unit_distance * 3 / 2;
        regions[4].x_pos = x_cursor;
        regions[4].y_pos = y_cursor;
        regions[4].height = unit_distance;
        regions[4].width = unit_distance;

        regions[5].x_pos = x_cursor + unit_distance;
        regions[5].y_pos = y_cursor;
        regions[5].height = unit_distance;
        regions[5].width = unit_distance * 3;
    }else{
        /* 横屏 */
        unit_distance = width / 6;
        y_cursor = (height - unit_distance * 4) / 2;
        x_cursor = (width - unit_distance * 4) / 2;
        /* "浏览模式"图标和文本 */
        regions[0].x_pos = x_cursor;
        regions[0].y_pos = y_cursor;
        regions[0].height = unit_distance;
        regions[0].width = unit_distance;

        regions[1].x_pos = x_cursor + unit_distance;
        regions[1].y_pos = y_cursor;
        regions[1].height = unit_distance;
        regions[1].width = unit_distance * 3;

        /* "连播模式"图标和文本 */
        y_cursor += unit_distance * 2;
        regions[2].x_pos = x_cursor;
        regions[2].y_pos = y_cursor;
        regions[2].height = unit_distance;
        regions[2].width = unit_distance;

        regions[3].x_pos = x_cursor + unit_distance;
        regions[3].y_pos = y_cursor;
        regions[3].height = unit_distance;
        regions[3].width = unit_distance * 3;

        /* "设置"图标和文本 */
         y_cursor += unit_distance * 2;
        regions[4].x_pos = x_cursor;
        regions[4].y_pos = y_cursor;
        regions[4].height = unit_distance;
        regions[4].width = unit_distance;

        regions[5].x_pos = x_cursor + unit_distance;
        regions[5].y_pos = y_cursor;
        regions[5].height = unit_distance;
        regions[5].width = unit_distance * 3;
    }
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

static int main_page_fill_layout(struct page_struct *main_page)
{
    /* 此函数填充布局内的内容 */
    FILE *png_file;
    int i,ret;
    int region_num = main_page->page_layout.region_num;
    struct page_region *regions = main_page->page_layout.regions;
    struct pixel_data pixel_data;
    struct picfmt_parser *png_parser = get_parser_by_name("png");
    char *file_name;
    char file_path[] = DEFAULT_ICON_FILE_PATH;
    char file_full_path[100];

    /* 如果想加个整体的背景，应该最先加进去 */
    //。。。

    /* 挨个填充 */
    /* 为每个region分配一个pixel_data，以指示该区域的显示数据应放置到哪里 */
    for(i = 0 ; i < region_num ; i++){
        regions[i].pixel_data = malloc(sizeof(struct pixel_data));
        if(!regions[i].pixel_data){
            DP_ERR("%s:malloc failed\n",__func__);
            return -1;
        }
        /* 将每个region对应的内存位置直接映射到该page对应的内存中的相应位置 */
        ret = remap_region_to_page_mem(&regions[i],main_page);
        if(ret){
            DP_ERR("%s:remap region mem failed!\n",__func__);
            return -1;
        }
    }
    
    /* 先填充三个图标 */
    memset(&pixel_data,0,sizeof(pixel_data));
    for(i = 0 ; i < 3 ; i++){
        int file_name_malloc = 0;
        if((strlen(file_path) + strlen(regions[i * 2].file_name) + 1) > 99){
            file_name = malloc(strlen(file_path) + strlen(regions[i * 2].file_name) + 2);
            if(!file_name){
                DP_ERR("%s:malloc failed!\n");
                return -ENOMEM;
            }
            sprintf(file_name,"%s/%s",file_path,regions[i * 2].file_name);
            file_name_malloc = 1;
        }else{
            sprintf(file_full_path,"%s/%s",file_path,regions[i * 2].file_name);
        }
        
        if(file_name_malloc){
            png_parser->get_pixel_data_in_rows(file_name,&pixel_data);
        }else{
            png_parser->get_pixel_data_in_rows(file_full_path,&pixel_data);
        }
        
        ret = pic_zoom_and_merge(&pixel_data,regions[i * 2].pixel_data);

        if(ret){
            DP_ERR("%s:pic_zoom_and_merge_in_rows failed!\n",__func__);
            return -1;
        }
        if(file_name_malloc){
            free(file_name);
        }
    }
    /* 释放由png解析函数分配的内存 */
    for(i = 0 ; i < pixel_data.height ; i++){
        free(pixel_data.rows_buf[i]);
    }
    free(pixel_data.rows_buf);
    
    /* 再填充三段文本 */
    /* 浏览目录;连播模式;设置 */
    get_string_bitamp_from_buf("浏览目录",0,"utf-8",regions[1].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xff00,60);
    get_string_bitamp_from_buf("连播模式",0,"utf-8",regions[3].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xff00,60);
    get_string_bitamp_from_buf("设    置",0,"utf-8",regions[5].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xff00,60);

    return 0;
}

static int main_page_run(struct page_param *pre_page_param)
{
    int ret;
    int pre_region_index = -1;
    int region_index;
    int slot_id = -1;
    int pressure = 0;
    struct display_struct *default_display;
    struct page_region *regions;
    struct page_struct *next_page;
    struct page_param page_param;

    /* 主要功能：分配内存；解析要显示的数据；while循环检测输入*/
    if(!main_page.allocated){
        main_page.page_mem.buf = malloc(main_page.page_mem.total_bytes);
        if(!main_page.page_mem.buf){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -1;
        }
        main_page.allocated = 1;
    }
    
    default_display = get_default_display();
    // default_display->clear_buf(default_display,0xffff);
    clear_pixel_data(&main_page.page_mem,0xffff);
    ret = main_page_fill_layout(&main_page);
    if(ret){
        DP_ERR("%s:main_page_fill_layout failed!\n",__func__);
        return -1;
    }   
    
    // default_display = get_default_display();
    
    default_display->flush_buf(default_display,main_page.page_mem.buf,main_page.page_mem.total_bytes);
    
    regions = main_page.page_layout.regions;
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
                            DP_ERR("%s:enter browose page failed!\n",__func__);
                            break;
                        }
                        next_page->run(&page_param);
                        /* 返回后重新渲染此页 */
                        default_display->flush_buf(default_display,main_page.page_mem.buf,main_page.page_mem.total_bytes);
                        break;
                    case 2:
                    case 3:
                        //连播模式
                        next_page = get_page_by_name("periodic_page");
                        if(!next_page){
                            DP_ERR("%s:enter browose page failed!\n",__func__);
                            break;
                        }
                        next_page->run(&page_param);
                        /* 返回后重新渲染此页 */
                        default_display->flush_buf(default_display,main_page.page_mem.buf,main_page.page_mem.total_bytes);
                        break;
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
    PAGE_REGION(0,0,"browse_mode_icon","browse_mode.png"),
    PAGE_REGION(1,0,"browse_mode_text",0),
    PAGE_REGION(2,0,"periodic_mode_icon","periodic_mode.png"),
    PAGE_REGION(3,0,"periodic_mode_text",0),
    PAGE_REGION(4,0,"setting_mode_icon","setting.png"),
    PAGE_REGION(5,0,"setting_mode_text",0),
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