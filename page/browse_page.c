#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>

#include "config.h"
#include "page_manager.h"
#include "debug_manager.h"
#include "picfmt_manager.h"
#include "pic_operation.h"
#include "input_manager.h"
#include "render.h"
#include "config.h"
#include "file.h"

static struct page_struct browse_page;

/* 定义文件图标以及文件名称字符串的大小,图标是一个正方体, "图标+名字"也是一个正方体
 *   --------
 *   |  图  |
 *   |  标  |
 * ------------
 * |   名字   |
 * ------------
 */
#define FILE_ICON_WIDTH     80
#define FILE_ICON_HEIGHT FILE_ICON_WIDTH
#define FILE_NAME_HEIGHT    20
#define FILE_NAME_WIDTH (FILE_NAME_HEIGHT + FILE_ICON_HEIGHT)
#define FILE_ALL_WIDTH FILE_NAME_WIDTH
#define FILE_ALL_HEIGHT FILE_ALL_WIDTH
#define DIR_SELECT_ICON_WIDTH 30

#define REGION_MENU_HOME        0
#define REGION_MENU_GOBACK      1
#define REGION_MENU_SELECT      2
#define REGION_MENU_PRE_PAGE    3
#define REGION_MENU_NEXT_PAGE   4
#define REGION_MAIN_FILE_PATH   5
#define REGION_MAIN_FILE_PAGES  6
#define REGION_MAIN_FILE_DIR    7          //专门用于刷新显存
#define REGION_FILE_DIR_BASE    8

#define REGION_FILE_PATH_HEIGHT   40
#define REGION_FILE_PAGES_HEIGHT  30

/* 以下是本页面要用到的图标信息 */
enum icon_info{
    ICON_MENU_HOME = 0,
    ICON_MENU_GOBACK,
    ICON_MENU_SELECT,
    ICON_MENU_PRE_PAGE,
    ICON_MENU_NEXT_PAGE,
    ICON_SAVE_AUTOPLAY_DIR,
    ICON_START_AUTOPLAY,
    ICON_FILETYPE_DIR,     
    ICON_FILETYPE_FILE_BMP, 
    ICON_FILETYPE_FILE_JPEG,
    ICON_FILETYPE_FILE_PNG,  
    ICON_FILETYPE_FILE_GIF,
    ICON_FILETYPE_FILE_TXT,
    ICON_FILETYPE_FILE_OTHER,
    ICON_DIR_SELECTED,
    ICON_DIR_UNSELECTED,
    ICON_DOT_SELECTED,
    ICON_DOT_UNSELECTED,
    ICON_NUMS, 
};

/* 图标文件名字符串数组 */
static const char *icon_file_names[] = {
    [ICON_MENU_HOME]            = "home.png",
    [ICON_MENU_GOBACK]          = "goback.png",
    [ICON_MENU_SELECT]          = "select.png",
    [ICON_MENU_PRE_PAGE]        = "pre_page.png",
    [ICON_MENU_NEXT_PAGE]       = "next_page.png",
    [ICON_FILETYPE_DIR]         = "folder.png",
    [ICON_FILETYPE_FILE_BMP]    = "bmp.png",
    [ICON_FILETYPE_FILE_JPEG]   = "jpeg.png",
    [ICON_FILETYPE_FILE_PNG]    = "png.png",
    [ICON_FILETYPE_FILE_GIF]    = "gif.png",
    [ICON_FILETYPE_FILE_TXT]    = "txt.png",
    [ICON_FILETYPE_FILE_OTHER]  = "other.png",
    [ICON_DIR_SELECTED]         = "dir_selected.png",
    [ICON_DIR_UNSELECTED]       = "dir_unselected.png",
    [ICON_DOT_SELECTED]         = "dot_selected.png",
    [ICON_DOT_UNSELECTED]       = "dot_unselected.png",
    [ICON_SAVE_AUTOPLAY_DIR]    = "save_autoplay_dir.png",
    [ICON_START_AUTOPLAY]       = "start_autoplay.png",
};

static struct pixel_data icon_pixel_datas[ICON_NUMS];

/* 与文件夹选择相关的几个全局变量 */
#define MAX_SELECTED_DIR 10
static bool dir_select_status = 0;                  //标志位
static int max_selected_dir = MAX_SELECTED_DIR;     //最多选择10个文件夹
static char *selected_dirs[MAX_SELECTED_DIR]; //当前选中的文件夹
static int selected_dir_num = 0;                    //当前选中的文件夹的个数

/* 文件目录相关的几个全局变量 */
static const char *const root_dir = DEFAULT_DIR;    //默认的根路径
static char *cur_dir;                               //在初始化函数中设置
static struct dir_entry **cur_dir_contents;
static unsigned int cur_dirent_nums;
static int start_file_index = 0;
static unsigned int file_cols,file_rows,files_per_page;
static bool dir_contents_generated = 0;

static int browse_page_calc_file_layout(struct page_struct *page)
{
    struct page_layout *layout;
    struct page_region *regions;
    unsigned int width,height;
    unsigned int file_all_width,file_name_height,file_icon_width;
    unsigned int rows,cols,num_files;
    unsigned int x_cursor,y_cursor;
    unsigned int x_delta,y_delta;
    unsigned int x_start,y_start;
    unsigned int i,j,k,region_offset,region_offset1;

    layout = &page->page_layout;
    regions = layout->regions;
    if(!regions){
        return -1;
    }
    if(layout->width >= layout->height){
        /* 暂只考虑横屏的情况 */
        x_start = layout->regions[0].width;
        y_start = layout->regions[5].height;
        width = layout->width - x_start;
        height = layout->height - layout->regions[5].height - layout->regions[6].height;
        
    }
        
    file_all_width = FILE_ALL_WIDTH;
    file_name_height = FILE_NAME_HEIGHT;
    file_icon_width = FILE_ICON_WIDTH;

    /* 确定每页显示多少个文件,每个图标之间间隔要大于10px */
    cols = (width - 10) / (file_all_width + 10);
    rows = (height - 10) / (file_all_width + 10);
    num_files = cols * rows;
    /* 将这几个变量存入全局变量中 */
    file_rows = rows;
    file_cols = cols;
    files_per_page = num_files;

    /* 为region数组分配空间,注意每个文件需要四个page_region：
     * 一个表示图标，一个表示名字,一个将包含整个整体专门用于点击事件，一个位于文件图标右上角专门用于选择 */
    regions = malloc((layout->region_num + num_files * 4) * sizeof(struct page_region));
    if(!regions){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    /* 将已存在的菜单布局复制过来，并释放原有空间 */
    memcpy(regions,layout->regions,sizeof(struct page_region) * layout->region_num);
    free(layout->regions);
    layout->regions = regions;
    layout->region_num = num_files * 4 + layout->region_num;

    /* 开始计算页面布局 */
    x_delta = (width - cols * file_all_width) / (cols + 1);
    y_delta = (height - rows * file_all_width) / (rows + 1);
    x_cursor = x_start + x_delta;
    y_cursor = y_start + y_delta;
    if(layout->width >= layout->height){            /* 暂只考虑横屏的情况 */
        /* 此区域代表整个文件显示区域,专门用于刷新显存 */
        regions[REGION_MAIN_FILE_DIR].x_pos = x_start;
        regions[REGION_MAIN_FILE_DIR].y_pos = y_start;
        regions[REGION_MAIN_FILE_DIR].width = width;
        regions[REGION_MAIN_FILE_DIR].height = height;
        regions[REGION_MAIN_FILE_DIR].index = REGION_MAIN_FILE_DIR;
        regions[REGION_MAIN_FILE_DIR].level = 0;

        unsigned int temp = (file_all_width - file_icon_width) / 2;
        k = REGION_FILE_DIR_BASE;
        region_offset = k + num_files * 2;
        region_offset1 = k + num_files * 3;
        for(i = 0 ; i < rows ; i++){
            for(j = 0 ; j < cols ; j++){
                /* 文件选择区域 */
                regions[region_offset1].x_pos  = x_cursor + file_all_width - DIR_SELECT_ICON_WIDTH + 10;
                regions[region_offset1].y_pos  = y_cursor - 10;
                regions[region_offset1].width  = DIR_SELECT_ICON_WIDTH;
                regions[region_offset1].height = DIR_SELECT_ICON_WIDTH;
                regions[region_offset1].index  = region_offset1;
                regions[region_offset1].level  = 2;
                regions[region_offset1].name   = "file_select_region";
                region_offset1++;

                /* 用于响应点击事件的区域,注意level为1 */
                regions[region_offset].x_pos  = x_cursor;
                regions[region_offset].y_pos  = y_cursor;
                regions[region_offset].width  = file_all_width;
                regions[region_offset].height = file_all_width;
                regions[region_offset].index  = region_offset;
                regions[region_offset].level  = 1;
                regions[region_offset].name   = "input_region";
                region_offset++;

                /* 文件图标区域 */
                regions[k].x_pos = x_cursor + temp;
                regions[k].y_pos = y_cursor;
                regions[k].width = file_icon_width;
                regions[k].height = file_icon_width;
                regions[k].index = k;
                regions[k].level = 0;
                regions[k].name = "file_icon";
                k++;

                /* 文件名区域 */
                regions[k].x_pos = x_cursor;
                regions[k].y_pos = y_cursor + file_icon_width;
                regions[k].width = file_all_width;
                regions[k].height = file_name_height;
                regions[k].index = k;
                regions[k].level = 0;
                regions[k].name = "file_name";
                k++;

                x_cursor += (file_all_width + x_delta);
            }
            x_cursor = x_start + x_delta;
            y_cursor += (file_all_width + y_delta);
        }
    }
    
    return 0;
}

static int browse_page_calc_menu_layout(struct page_struct *page)
{
    struct page_layout *layout;
    struct page_region *regions;
    unsigned int width,height;
    unsigned int x_cursor,y_cursor,unit_distance;
    unsigned int i;

    width = page->page_layout.width;
    height = page->page_layout.height;
    layout = &page->page_layout;

    /* 动态分配region数组所占用的空间 */
    regions = malloc(REGION_FILE_DIR_BASE * sizeof(struct page_region));
    if(!regions){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }

    layout->regions = regions;
    layout->region_num = REGION_FILE_DIR_BASE;

    /* 一些与方向无关的成员在外面先填充了 */
    for(i = 0 ; i < REGION_FILE_DIR_BASE ; i++){
        regions[i].index    = i;
        regions[i].level    = 0;
    }

    if(width >= height){
        /* 横屏 */
        /*	 iYres/5
		 *	  ----------------------------------		  
		 *    home          |   当前所在路径  |
		 *    go back
		 *    select
		 *    pre_page
		 *    next_page     |   当前页数     |
		 *	  ----------------------------------
		 */
        unit_distance = height / 5;
        y_cursor = 0;
        x_cursor = 0;
        /* "home" */
        regions[REGION_MENU_HOME].x_pos  = x_cursor;
        regions[REGION_MENU_HOME].y_pos  = y_cursor;
        regions[REGION_MENU_HOME].height = unit_distance;
        regions[REGION_MENU_HOME].width  = unit_distance;
        
        /* "goback" */
        regions[REGION_MENU_GOBACK].x_pos  = x_cursor ;
        regions[REGION_MENU_GOBACK].y_pos  = y_cursor + unit_distance;
        regions[REGION_MENU_GOBACK].height = unit_distance;
        regions[REGION_MENU_GOBACK].width  = unit_distance;
        
        /* "select" */
        regions[REGION_MENU_SELECT].x_pos  = x_cursor;
        regions[REGION_MENU_SELECT].y_pos  = y_cursor + unit_distance * 2;
        regions[REGION_MENU_SELECT].height = unit_distance;
        regions[REGION_MENU_SELECT].width  = unit_distance;
        
        /* "pre_page" */
        regions[REGION_MENU_PRE_PAGE].x_pos  = x_cursor;
        regions[REGION_MENU_PRE_PAGE].y_pos  = y_cursor + unit_distance * 3;
        regions[REGION_MENU_PRE_PAGE].height = unit_distance;
        regions[REGION_MENU_PRE_PAGE].width  = unit_distance;
        
        /* "next_page" */
        regions[REGION_MENU_NEXT_PAGE].x_pos  = x_cursor;
        regions[REGION_MENU_NEXT_PAGE].y_pos  = y_cursor + unit_distance * 4;
        regions[REGION_MENU_NEXT_PAGE].height = unit_distance ;
        regions[REGION_MENU_NEXT_PAGE].width  = unit_distance;
        
        /* 文件路径显示区域 */
        regions[REGION_MAIN_FILE_PATH].x_pos  = unit_distance;
        regions[REGION_MAIN_FILE_PATH].y_pos  = 0;
        regions[REGION_MAIN_FILE_PATH].height = 30 ;
        regions[REGION_MAIN_FILE_PATH].width  = width - unit_distance ;
        
        /* 页数信息显示区域 */
        regions[REGION_MAIN_FILE_PAGES].x_pos  = unit_distance;
        regions[REGION_MAIN_FILE_PAGES].y_pos  = height - 30;
        regions[REGION_MAIN_FILE_PAGES].height = 30 ;
        regions[REGION_MAIN_FILE_PAGES].width  = width - unit_distance ;
        
    }else{
        /* 竖屏 */
        /*	 iXres/4
		 *	  ----------------------------------
		 *	        | 当前所在路径 |
		 *
		 *
		 *
		 *
		 *            | 页数信息 |
		 *    home  up  select  pre_page    next_page
		 *	  ----------------------------------
		 */
        unit_distance = height / 5;
        y_cursor = height - unit_distance;
        x_cursor = 0;
        /* "home" */
        regions[REGION_MENU_HOME].x_pos = x_cursor;
        regions[REGION_MENU_HOME].y_pos = y_cursor;
        regions[REGION_MENU_HOME].height = unit_distance;
        regions[REGION_MENU_HOME].width = unit_distance;

        /* "goback" */
        regions[REGION_MENU_GOBACK].x_pos = x_cursor + unit_distance;
        regions[REGION_MENU_GOBACK].y_pos = y_cursor;
        regions[REGION_MENU_GOBACK].height = unit_distance;
        regions[REGION_MENU_GOBACK].width = unit_distance;

        /* "select" */
        regions[REGION_MENU_SELECT].x_pos = x_cursor + unit_distance * 2;
        regions[REGION_MENU_SELECT].y_pos = y_cursor;
        regions[REGION_MENU_SELECT].height = unit_distance;
        regions[REGION_MENU_SELECT].width = unit_distance;

        /* "pre_page" */
        regions[REGION_MENU_PRE_PAGE].x_pos = x_cursor + unit_distance * 3;
        regions[REGION_MENU_PRE_PAGE].y_pos = y_cursor;
        regions[REGION_MENU_PRE_PAGE].height = unit_distance;
        regions[REGION_MENU_PRE_PAGE].width = unit_distance;

        /* "next_page" */
        regions[REGION_MENU_NEXT_PAGE].x_pos = x_cursor + unit_distance * 4;
        regions[REGION_MENU_NEXT_PAGE].y_pos = y_cursor;
        regions[REGION_MENU_NEXT_PAGE].height = unit_distance ;
        regions[REGION_MENU_NEXT_PAGE].width = unit_distance;
    }
    return 0;
}

static int browse_page_calc_layout(struct page_struct *page)
{
    int i,ret;

    if(page->already_layout){
        return -1;
    }
    /* 注意这两个函数先后顺序有要求 */
    ret = browse_page_calc_menu_layout(page);
    if(ret < 0){
        DP_ERR("%s:browse_page_calc_menu_layout failed!\n",__func__);
        return ret;
    }
    ret = browse_page_calc_file_layout(page);
    if(ret < 0){
        DP_ERR("%s:browse_page_calc_file_layout failed!\n",__func__);
        return ret;
    }

    for(i = 0 ; i < page->page_layout.region_num ; i++){
        page->page_layout.regions[i].owner_page = page;
    }
    browse_page.already_layout = 1;
    return 0;
}

/* 很遗憾，这几个准备图标的函数要求图标长宽必须相同，否则会出现什么我也不确定 */
static int prepare_icon_pixel_data(struct page_struct *browse_page)
{
    int i,j,ret;
    struct pixel_data pixel_data;
    struct page_region *regions = browse_page->page_layout.regions;
    struct picfmt_parser *png_parser = get_parser_by_name("png");
    const char file_path[] = DEFAULT_ICON_FILE_PATH;
    char file_full_path[100];
    unsigned int menu_icon_width;

    if(browse_page->icon_prepared){
        return -1;
    }

    menu_icon_width = regions[REGION_MENU_HOME].width;

    for(i = 0 ; i < ICON_NUMS ; i++){
        char *file_name;
        int file_name_malloc = 0;
       
        /* 如果没有指定文件，直接跳过 */
        if(!icon_file_names[i]){
            continue;
        }
        /* 构造文件名，为了预防文件名过长导致出错,虽然这发生的概率极小 */
        if((strlen(file_path) + strlen(icon_file_names[i]) + 1) > 99){
            file_name = malloc(strlen(file_path) + strlen(icon_file_names[i]) + 2);
            if(!file_name){
                DP_ERR("%s:malloc failed!\n");
                return -ENOMEM;
            }
            sprintf(file_name,"%s/%s",file_path,icon_file_names[i]);
            file_name_malloc = 1;
        }else{
            sprintf(file_full_path,"%s/%s",file_path,icon_file_names[i]);
        }
    
        memset(&pixel_data,0,sizeof(pixel_data));
        if(file_name_malloc){
            ret = png_parser->get_pixel_data_in_rows(file_name,&pixel_data);
        }else{
            ret = png_parser->get_pixel_data_in_rows(file_full_path,&pixel_data);
        } 
        if(ret){
            if(ret == -2){
                //to-do 此种错误是可修复的
            }
            DP_ERR("%s:png get_pixel_data_in_rows failed!\n",__func__);
            return -ENOMEM;
        } 
        
        /* 确定图标要缩放到的大小 */
        memset(&icon_pixel_datas[i],0,sizeof(struct pixel_data));
        if(i < ICON_FILETYPE_DIR){
            /* 菜单图标 */
            icon_pixel_datas[i].width  = menu_icon_width;
            icon_pixel_datas[i].height = menu_icon_width;
        }else if(i < ICON_DIR_SELECTED){
            /* 文件图标 */
            icon_pixel_datas[i].width = FILE_ICON_WIDTH;
            icon_pixel_datas[i].height = FILE_ICON_WIDTH;
        }else if(i < ICON_DOT_SELECTED){
            /* 文件选择图标 */
            icon_pixel_datas[i].width = DIR_SELECT_ICON_WIDTH;
            icon_pixel_datas[i].height = DIR_SELECT_ICON_WIDTH;
        }else{
            /* 圆点图标 */
            icon_pixel_datas[i].width = 20;
            icon_pixel_datas[i].height = 20;
        }

        /* 进行缩放 */
        ret = pic_zoom_with_same_bpp(&icon_pixel_datas[i],&pixel_data);
        if(ret){
            DP_ERR("%s:pic_zoom_with_same_bpp failed!\n",__func__);
            return -1;
        }
        
        if(file_name_malloc){
            free(file_name);
        }
        /* 释放由png解析函数分配的内存 */
        if(pixel_data.in_rows){
            for(j = 0 ; j < pixel_data.height ; j++){
                free(pixel_data.rows_buf[j]);
            }
            free(pixel_data.rows_buf);
        }
        if(pixel_data.buf){
            free(pixel_data.buf);
        }
    }
    browse_page->icon_prepared = 1;
    return 0;
}

/* 在此函数中将会计算好页面的布局情况，浏览页面主要可分为两部分：1.菜单部分   2.文件浏览部分
 */
static int browse_page_init(void)
{
    int ret;
    struct display_struct *default_display = get_default_display();
    struct page_layout *page_layout = &browse_page.page_layout;
    struct page_region *regions = page_layout->regions;
    int width = default_display->xres;
    int height = default_display->yres;

    page_layout->width  = width;
    page_layout->height = height;
    browse_page.page_mem.bpp     = default_display->bpp;
    browse_page.page_mem.width   = width;
    browse_page.page_mem.height  = height;
    browse_page.page_mem.line_bytes  = browse_page.page_mem.width * browse_page.page_mem.bpp / 8;
    browse_page.page_mem.total_bytes = browse_page.page_mem.line_bytes * browse_page.page_mem.height;

    /* 计算布局 */
    ret = browse_page_calc_layout(&browse_page);
    if(ret){
        DP_ERR("%s:browse_page_calc_layout failed!\n",__func__);
        return ret;
    }

    /* 初始化当前目录 */
    cur_dir = malloc(strlen(DEFAULT_DIR) + 1);
    if(!cur_dir){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    strcpy(cur_dir,DEFAULT_DIR);

    return 0;
}

/* 对应的销毁函数，可能用不上，但按理说应该是需要的 */
static void destroy_icon_pixel_data(void)
{
    int i;
    if(browse_page.icon_prepared){
        for(i = 0 ; i < ICON_NUMS ; i++){
            free(icon_pixel_datas[i].buf);
            memset(&icon_pixel_datas[i],0,sizeof(struct pixel_data));
        }
    }
    browse_page.icon_prepared = 0; 
}

/* 调用这个函数,几乎会释放该页面占用的所有资源 */
static void browse_page_exit(void)
{
    /* 释放占用的内存 */
    if(browse_page.allocated){
        free(browse_page.page_mem.buf);
    }

    /* 删除区域的内存映射 */
    unmap_regions_to_page_mem(&browse_page);

    /* 删除图标数据 */
    destroy_icon_pixel_data();

    /* 删除目录信息 */
    if(cur_dir_contents){
        free_dir_contents(cur_dir_contents,cur_dirent_nums);
        dir_contents_generated = 0;
        start_file_index = 0;
        cur_dirent_nums = 0;
        if(cur_dir){
            free(cur_dir);
        }
        cur_dir_contents = 0;
    }
}

static int fill_menu_icon_area(struct page_struct *browse_page)
{
    int i,ret;
    struct page_region *regions = browse_page->page_layout.regions;

    if(!browse_page->icon_prepared){
        prepare_icon_pixel_data(browse_page);
    }else if(!browse_page->already_layout){
        browse_page_calc_layout(browse_page);
    }else if(!browse_page->region_mapped){
        remap_regions_to_page_mem(browse_page);
    }

    for(i = 0 ; i <= REGION_MENU_NEXT_PAGE ; i++){
        /* 先清理 */
        clear_pixel_data(regions[i].pixel_data,BACKGROUND_COLOR);
        /* 将图像数据并入到相应区域映射到的内存中 */
        if(dir_select_status){
            if(i == 0){
                ret = merge_pixel_data(regions[i].pixel_data,&icon_pixel_datas[ICON_MENU_GOBACK]);
            }else if(i == 1){
                ret = merge_pixel_data(regions[i].pixel_data,&icon_pixel_datas[ICON_SAVE_AUTOPLAY_DIR]);
            }else if(i == 2){
                ret = merge_pixel_data(regions[i].pixel_data,&icon_pixel_datas[ICON_START_AUTOPLAY]);
            }else{
                ret = merge_pixel_data(regions[i].pixel_data,&icon_pixel_datas[i]);
            }
        }else{
            ret = merge_pixel_data(regions[i].pixel_data,&icon_pixel_datas[i]);
        }  
        if(ret){
            DP_ERR("%s:merge_pixel_data failed!\n",__func__);
            return ret;
        }
    }
    return 0;
}

/* 填充文件路径区域,此函数会被fill_file_dir_area调用 */
static int fill_file_path_area(struct page_struct *page)
{
    int font_size;
    int ret;
    struct page_region *path_region = &page->page_layout.regions[REGION_MAIN_FILE_PATH];

    font_size = path_region->pixel_data->height;
    clear_pixel_data(path_region->pixel_data,BACKGROUND_COLOR);
    ret = get_string_bitamp_from_buf(cur_dir,0,"utf-8",path_region->pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xfd8,font_size);
    if(ret < 0){
        DP_ERR("%d:get_string_bitamp_from_buf failed!\n",__func__);
        return ret;
    }
    return 0 ;
}

/* 填充页数信息,此函数会被fill_file_dir_area调用 */
static int fill_pages_info_area(struct page_struct *page)
{
    return 0;
}

/* 填充文件浏览区域 */
static int fill_file_dir_area(struct page_struct *browse_page)
{
    struct page_region *regions = browse_page->page_layout.regions;
    unsigned int region_nums = browse_page->page_layout.region_num;
    unsigned int base_region_index;
    unsigned int base_icon_index;
    unsigned int font_size;
    unsigned int i,ret;
    unsigned int file_type;
    
    if(!browse_page->icon_prepared){
        prepare_icon_pixel_data(browse_page);
    }else if(!dir_contents_generated){
        DP_WARNING("%s:dir contents has not generated!\n",__func__);
        return -1;
    }

    base_region_index = REGION_FILE_DIR_BASE;
    base_icon_index = ICON_FILETYPE_DIR;
    font_size = regions[base_region_index + 1].height;
   
    /* 需要注意的一点,对于填充内容而言,一个目录项对应两个区域,一个图标,一个文字 */
    for(i = 0 ; i < files_per_page ; i++){
        if((start_file_index + i) >= cur_dirent_nums){
            /* 已经填充完了所有目录项,直接退出 */
            break;
        }
        /* 文件夹 */
        if(FILETYPE_DIR == cur_dir_contents[start_file_index + i]->type){
            /* 填充图标 */
            merge_pixel_data(regions[base_region_index + 2 * i].pixel_data,&icon_pixel_datas[base_icon_index + FILETYPE_DIR]);
            /* 如果当前处于目录选择状态，则填充相应图标 */
            if(dir_select_status){
                merge_pixel_data(regions[base_region_index + 3 * files_per_page + i].pixel_data,&icon_pixel_datas[ICON_DIR_UNSELECTED]);
            }

            /* 填充文字 */
            get_string_bitamp_from_buf(cur_dir_contents[start_file_index + i]->name,0,"utf-8",\
            regions[base_region_index + i * 2 + 1].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xff00,font_size);
            
        }else if(FILETYPE_REG == cur_dir_contents[start_file_index + i]->type){
            /* 文件 */
            /* 判断文件类型信息已经获取过了,直接使用即可 */
            file_type = cur_dir_contents[start_file_index + i]->file_type;
            /* 填充图标 */
            merge_pixel_data(regions[base_region_index + 2 * i].pixel_data,&icon_pixel_datas[base_icon_index + file_type]);
            /* 填充文字 */
            get_string_bitamp_from_buf(cur_dir_contents[start_file_index + i]->name,0,"utf-8",\
            regions[base_region_index + i * 2 + 1].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xff00,font_size);
        }
        
    }
    /* 填充文件路径 */
    ret = fill_file_path_area(browse_page);
    if(ret < 0){
        DP_ERR("%d:fill_file_path_area failed!\n",__func__);
        return ret;
    }
    /* 填充页数信息区域 */
    ret = fill_pages_info_area(browse_page);
    if(ret < 0){
        DP_ERR("%d:fill_pages_info_area failed!\n",__func__);
        return ret;
    }
    return 0;
}

/* 一个专门用于刷新主体区域的函数 */
static void flush_file_dir_area(struct page_struct *page)
{
    struct page_region *regions = page->page_layout.regions;
    struct display_struct *display = get_default_display();

    flush_page_region(&regions[REGION_MAIN_FILE_DIR],display);
    flush_page_region(&regions[REGION_MAIN_FILE_PATH],display);
    flush_page_region(&regions[REGION_MAIN_FILE_PAGES],display);
}

/* 一个专门用于刷新菜单区域的函数 */
static void flush_menu_icon_area(struct page_struct *page)
{
    struct page_region *regions = page->page_layout.regions;
    struct display_struct *display = get_default_display();

    flush_page_region(&regions[REGION_MENU_HOME],display);
    flush_page_region(&regions[REGION_MENU_GOBACK],display);
    flush_page_region(&regions[REGION_MENU_SELECT],display);
    flush_page_region(&regions[REGION_MENU_PRE_PAGE],display);
    flush_page_region(&regions[REGION_MENU_NEXT_PAGE],display);
}

/* 使能文件夹选择状态 */
static void enable_dir_select_status(struct page_struct *browse_page,bool enable)
{
    struct page_region *regions = browse_page->page_layout.regions;
    int dir_select_region_base;
    int i;

    if(!browse_page->already_layout){
        return ;
    }

    dir_select_region_base = REGION_FILE_DIR_BASE + files_per_page * 3;
    if(enable){
        dir_select_status = 1;
        for(i = 0 ; i < files_per_page ; i++){
            regions[dir_select_region_base + i].invisible = 0;
        }
    }else{
        dir_select_status = 0;
        for(i = 0 ; i < files_per_page ; i++){
            regions[dir_select_region_base + i].invisible = 1;
        }
    }
    clear_pixel_data(regions[REGION_MAIN_FILE_DIR].pixel_data,BACKGROUND_COLOR);
    fill_file_dir_area(browse_page);
    flush_file_dir_area(browse_page);
    fill_menu_icon_area(browse_page);
    flush_menu_icon_area(browse_page);
}

/* 用于填充各区域 */
static int browse_page_fill_layout(struct page_struct *browse_page)
{
    int ret;

    /* 如果想加个整体的背景，应该最先加进去 */
    //...
    DP_ERR("enter:%s\n",__func__);
    
    /* 准备各种图标数据 */
    if(!browse_page->icon_prepared){
        ret = prepare_icon_pixel_data(browse_page);
        if(ret){
            DP_ERR("%s:prepare_icon_pixel_data failed!\n",__func__);
            return -1;
        }
    }
    
    /* 填充菜单图标数据 */
    ret = fill_menu_icon_area(browse_page);
    if(ret){
        DP_ERR("%s:prepare_file_icon_data failed!\n",__func__);
        return -1;
    }
    
    /* 填充文件浏览区域 */
    ret = fill_file_dir_area(browse_page);
    if(ret){
        DP_ERR("%s:fill_file_dir_area failed!\n",__func__);
        return -1;
    }
    return 0;
}

/* 点击家菜单时的回调函数 */
static int home_menu_cb_func(void)
{
    
    return 0;
}

/* 点击返回菜单时的回调函数 */
static int goback_menu_cb_func(void)
{   
    int ret;
    int n = strlen(cur_dir);
    char buf[n + 1];
    char *buf_start,*buf_end;
    struct display_struct *display = get_default_display();
    struct page_region *regions = browse_page.page_layout.regions;

    /* 如果当前为根目录,不用返回 */
    if(!strcmp(cur_dir,"/") || !strcmp(cur_dir,"//")){
        return -1;
    }
    DP_INFO("cur_dir:%s\n",cur_dir);
    /* 否则找出上级目录 */
    strcpy(buf,cur_dir);
    buf[n] = '\0';
    

    // "//xxx" 这样的根目录也是合法的,这里是为了删除前面多余的斜线方便处理
    buf_start = buf;
    if(*buf_start == '/' && *(buf_start + 1) == '/'){
        buf_start++;       
    }

    buf_end = strrchr(buf_start,'/');

    /* 为根目录 */
    if(buf_start == buf_end){
        free(cur_dir);
        cur_dir = malloc(2);
        if(!cur_dir){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        cur_dir[0] = '/';
        cur_dir[1] = '\0';
    }else if(!buf_end){
        return -1;
    }else{
        *buf_end = '\0';
        free(cur_dir);
        cur_dir = malloc(strlen(buf_start) + 1);
        if(!cur_dir){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        strcpy(cur_dir,buf_start);
    }
    
    /* 释放原有目录信息 */
    if(cur_dir_contents){
        free_dir_contents(cur_dir_contents,cur_dirent_nums);
        cur_dir_contents = NULL;
    }

    /* 重新获取目录信息 */
    ret = get_dir_contents(cur_dir,&cur_dir_contents,&cur_dirent_nums);
    if(ret){
        DP_ERR("%s:get_dir_contents failed!\n",__func__);
        cur_dir_contents = NULL;
        dir_contents_generated = 0;
        cur_dirent_nums = 0;
        start_file_index = -1;
        return ret;
    }
    start_file_index = 0;

    /* 重新填充文件显示区域 */
    clear_pixel_data(regions[REGION_MAIN_FILE_DIR].pixel_data,BACKGROUND_COLOR);
    ret = fill_file_dir_area(&browse_page);
    if(ret){
        DP_ERR("%s:fill_file_dir_area failed!\n",__func__);
        return ret;
    }

    /* 将更改后的内容刷新至显存 */
    flush_file_dir_area(&browse_page);
    return 0;
}

/* 点击选择连播文件夹菜单时的回调函数 */
static int select_menu_cb_func(void)
{
    int i;
    int ret;
    struct page_region *regions = browse_page.page_layout.regions;

    enable_dir_select_status(&browse_page,1);

    return 0;
}

/* 点击 “保存连播文件夹” 菜单时的回调函数 */
static int save_autoplay_dir_menu_cb_func(void)
{
    struct page_struct *auto_page = get_page_by_name("autoplay_page");
    char **auto_priv = auto_page->private_data;
    int i,j = 0;

    /* 将“连播页面”中的原有数据清除 */
    for(i = 0 ; i < max_selected_dir ; i++){
        if(auto_priv[i]){
            free(auto_priv[i]);
            auto_priv[i] = NULL;
        }
    }

    /* 将数据保存到“连播页面”中 */
    for(i = 0 ; i < max_selected_dir ; i++){
        if(selected_dirs[i]){
            auto_priv[j] = selected_dirs[i];
            DP_DEBUG("selected dir %d:%s\n",j,auto_priv[j]);
            j++;
            selected_dirs[i] = NULL;
        }
    }
    *(unsigned long *)&auto_priv[max_selected_dir] = j;printf("%s-%d\n",__func__,__LINE__);
    enable_dir_select_status(&browse_page,0);printf("%s-%d\n",__func__,__LINE__);

    return 0;
}

/* 点击 “开始连播” 菜单时的回调函数 */
static int start_autoplay_menu_cb_func(int pre_page_id)
{
    int ret;
    struct page_struct *auto_page;

    auto_page = get_page_by_name("autoplay_page");
    if(!auto_page){
        DP_ERR("%s:get \"autoplay_page\" failed!\n",__func__);
        return ret;
    }
    /* 如果当前一个文件夹也没选择，退出文件夹选择状态，继续留在“浏览页面” */
    if(!selected_dir_num){
        enable_dir_select_status(&browse_page,0);
        return 0;
    }else{
        /* 先保存数据 */
        save_autoplay_dir_menu_cb_func();
        /* 进入 autoplay page */
        if(pre_page_id == calc_page_id("main_page")){
            return 1;
        }else if(pre_page_id == calc_page_id("autoplay_page")){
            return 2;
        }
    }
    return -1;
}

/* 点击"文件夹选择区域"时的回调函数 */
static int select_dir_cb_func(int region_index)
{
    struct page_region *regions = browse_page.page_layout.regions;
    struct display_struct *display = get_default_display();
    unsigned int dir_index;
    char *dir_name;
    int i;

    /* 先构造出当前所选择文件夹的名称 */
    dir_index = start_file_index + region_index - REGION_FILE_DIR_BASE - files_per_page * 3;
    dir_name = malloc(strlen(cur_dir) + strlen(cur_dir_contents[dir_index]->name) + 2);
    if(!dir_name){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    sprintf(dir_name,"%s/%s",cur_dir,cur_dir_contents[dir_index]->name);
    DP_DEBUG("cur dir:%s,cur_name :%s\n",cur_dir,cur_dir_contents[dir_index]->name);
    DP_DEBUG("cur selected dir:%s\n",dir_name);
    if(regions[region_index].selected){
        /* 更新数据 */
        regions[region_index].selected = 0;
        for(i = 0 ; i < max_selected_dir ; i++){
            if(selected_dirs[i] && !strcmp(dir_name,selected_dirs[i])){
                free(selected_dirs[i]);
                selected_dirs[i] = NULL;
                selected_dir_num--;
            }
        }
        /* 更新图像 */
        clear_pixel_data(regions[region_index].pixel_data,BACKGROUND_COLOR);
        merge_pixel_data(regions[region_index].pixel_data,&icon_pixel_datas[ICON_DIR_UNSELECTED]);
        flush_page_region(&regions[region_index],display);
    }else{
        /* 更新数据 */
        if(selected_dir_num >= max_selected_dir)    //超出上限，直接返回
            return 0;
        /* 在全局数组找到第一个空闲项 */
        regions[region_index].selected = 1;
        for(i = 0 ; i < max_selected_dir ; i++){
            if(!selected_dirs[i])
                break;
        }
        selected_dirs[i] = dir_name;
        selected_dir_num++;
        /* 更新图像 */
        clear_pixel_data(regions[region_index].pixel_data,BACKGROUND_COLOR);
        merge_pixel_data(regions[region_index].pixel_data,&icon_pixel_datas[ICON_DIR_SELECTED]);
        flush_page_region(&regions[region_index],display);
    }

    return 0;
}

/* 点击上一页菜单时的回调函数 */
static int prepage_menu_cb_func(void)
{
    int ret;
    struct page_region *regions = browse_page.page_layout.regions;
    struct display_struct *display = get_default_display();

    /* 确定新的起始文件索引 */
    if(0 == start_file_index){       //当前显示的第一个文件,没有前一页了
        return 0;
    }else if((start_file_index -= files_per_page) < 0){
        start_file_index = 0;
    }
    
    /* 重新填充区域 */
    clear_pixel_data(regions[REGION_MAIN_FILE_DIR].pixel_data,BACKGROUND_COLOR);
    fill_file_dir_area(&browse_page);
    if(ret){
        DP_ERR("%s:fill_file_dir_area failed!\n",__func__);
        return ret;
    }

    /* 将更改后的内容刷新至显存 */
    flush_file_dir_area(&browse_page);
    return 0;
}

/* 点击下一页菜单时的回调函数 */
static int nextpage_menu_cb_func(void)
{
    int ret;
    struct page_region *regions = browse_page.page_layout.regions;
    struct display_struct *display = get_default_display();

    /* 确定新的起始文件索引 */
    if((start_file_index + files_per_page) >= cur_dirent_nums){
        return 0;
    }else{
        start_file_index += files_per_page;
    }

    /* 重新填充区域 */
    clear_pixel_data(regions[REGION_MAIN_FILE_DIR].pixel_data,BACKGROUND_COLOR);
    fill_file_dir_area(&browse_page);
    if(ret){
        DP_ERR("%s:fill_file_dir_area failed!\n",__func__);
        return ret;
    }

    /* 将更改后的内容刷新至显存 */
    flush_file_dir_area(&browse_page);
    return 0;
}

/* 点击文件目录区域时的回调函数 */
static int file_dir_area_cb_func(int region_index,void *data)
{
    unsigned int selected_file_index = *(unsigned int *)data;
    struct page_region *regions = browse_page.page_layout.regions;
    struct display_struct *default_display = get_default_display();
    struct page_struct *view_pic_page;
    char *file_name;
    int ret;
    struct page_param page_param;
    DP_ERR("enter :%s!\n",__func__);
    /* 打开目录 */
    if(FILETYPE_DIR == cur_dir_contents[selected_file_index]->type){
        invert_region(regions[region_index].pixel_data);
        flush_file_dir_area(&browse_page);
        /* 修改当前目录 */
        char *temp = cur_dir;
        cur_dir = malloc(strlen(cur_dir) + 2 + strlen(cur_dir_contents[selected_file_index]->name));
        if(!cur_dir){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        
        if(!strcmp(temp,"/")){
            sprintf(cur_dir,"/%s",cur_dir_contents[selected_file_index]->name);
        }else{
            sprintf(cur_dir,"%s/%s",temp,cur_dir_contents[selected_file_index]->name);
        }
        
        if(temp){
            free(temp);
        }
        /* 释放原有目录信息数组 */
        free_dir_contents(cur_dir_contents,cur_dirent_nums); 
        /* 重新获取新的当前目录的信息 */
        ret = get_dir_contents(cur_dir,&cur_dir_contents,&cur_dirent_nums);
        if(ret){
            DP_ERR("%s:get_dir_contents failed!\n",__func__);
            return -1;
        }
        start_file_index = 0;
        dir_contents_generated = 1;
        /* 重新填充文件显示区域,将文件显示区域清为白色 */
        clear_pixel_data(regions[REGION_MAIN_FILE_DIR].pixel_data,BACKGROUND_COLOR); 
        /* 填充各区域 */
        ret = fill_file_dir_area(&browse_page);
        if(ret){
            DP_ERR("%s:fill_file_dir_area failed!\n",__func__);
            return -1;
        }
        default_display->flush_buf(default_display,browse_page.page_mem.buf,browse_page.page_mem.total_bytes);
    }else if(FILETYPE_REG == cur_dir_contents[selected_file_index]->type){
        /* 打开文件 */
        switch (cur_dir_contents[selected_file_index]->file_type){
            /* 目前就支持两大类文件:图片和文本 */
            case FILETYPE_FILE_BMP:
            case FILETYPE_FILE_JPEG:
            case FILETYPE_FILE_PNG:
            case FILETYPE_FILE_GIF:
                /* 有个要求,既然能识别出该文件,那么就得能打开该文件,否则就别识别了 */
                /* 构造出文件的绝对路径名,然后把后面的事交给view_page吧 */
                file_name = malloc(strlen(cur_dir) + 2 + strlen(cur_dir_contents[selected_file_index]->name));
                if(!file_name){
                    DP_ERR("%s:malloc failed\n",__func__);
                    return -ENOMEM;
                }
                if(!strcmp(cur_dir,"/")){
                    sprintf(file_name,"/%s",cur_dir_contents[selected_file_index]->name);
                }else{
                    sprintf(file_name,"%s/%s",cur_dir,cur_dir_contents[selected_file_index]->name);
                }
                
                /* 构造页面之间传递的参数 */
                page_param.id = browse_page.id;
                page_param.private_data = file_name;
                /* 获取page_struct */
                view_pic_page = get_page_by_name("view_pic_page");
                if(!view_pic_page){
                    DP_ERR("%s:get view_pic_page failed\n",__func__);
                    return -1;
                }
                view_pic_page->run(&page_param);
                free(file_name);
                /* 应该重新渲染该页,似乎不用重新填充页面,直接将页面内存的数据输入显示屏缓存就可以了 */
                default_display->flush_buf(default_display,browse_page.page_mem.buf,browse_page.page_mem.total_bytes);
                break;
            case FILETYPE_FILE_TXT:
                break;
            default:
                break;
        }
    }
    return 0;
}

/* 主要功能：分配内存；解析要显示的数据；while循环检测输入*/
static int browse_page_run(struct page_param *pre_param)
{
    int ret;
    int pre_region_index = -1;
    int region_index;
    int slot_id = -1;
    int pressure = 0;
    unsigned int selected_file_index;
    unsigned int dir_select_region_base = REGION_FILE_DIR_BASE + 3 * files_per_page;
    unsigned int pre_page_id = pre_param->id;
    struct display_struct *default_display;
    struct page_region *regions;
    struct page_struct *next_page;
    struct page_param page_param;
    DP_ERR("enter:%s\n",__func__);
    /* 为该页面分配一块内存 */
    if(!browse_page.allocated){
        browse_page.page_mem.buf = malloc(browse_page.page_mem.total_bytes);
        if(!browse_page.page_mem.buf){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -1;
        }
        browse_page.allocated = 1;
    }
    
    /* 注意，页面布局在注册该页面时，在初始化函数中已经计算好了 */

    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!browse_page.region_mapped){
        ret = remap_regions_to_page_mem(&browse_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return -1;
        }
        browse_page.region_mapped = 1;
    }
    
    /* 将页面清为白色 */
    clear_pixel_data(&browse_page.page_mem,BACKGROUND_COLOR);

    /* 获取目录结构,注意,每到重新运行该run函数,总是从根目录开始显示 */
    if(cur_dir_contents){
        free_dir_contents(cur_dir_contents,cur_dirent_nums);
        dir_contents_generated = 0;
        start_file_index = 0;
        cur_dirent_nums = 0;
        if(cur_dir){
            free(cur_dir);
        }
        cur_dir = malloc(strlen(DEFAULT_DIR) + 1);
        if(!cur_dir){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        } 
        strcpy(cur_dir,DEFAULT_DIR);
    }
    ret = get_dir_contents(cur_dir,&cur_dir_contents,&cur_dirent_nums);
    if(ret){
        DP_ERR("%s:get_dir_contents failed!\n",__func__);
        return -1;
    }
    dir_contents_generated = 1;
    
    /* 如果是从连播页面进入此页面的，则设置相应的位 */
    if(pre_param->id == calc_page_id("autoplay_page")){
        enable_dir_select_status(&browse_page,1);
    }

    /* 填充各区域 */
    ret = browse_page_fill_layout(&browse_page);
    if(ret){
        DP_ERR("%s:browse_page_fill_layout failed!\n",__func__);
        return -1;
    }   

    default_display = get_default_display();
    default_display->flush_buf(default_display,browse_page.page_mem.buf,browse_page.page_mem.total_bytes);
    
    regions = browse_page.page_layout.regions;

    /* 检测输入事件的循环 */
    while(1){
        struct my_input_event event;
        region_index = get_input_event_for_page(&browse_page,&event);
        DP_ERR("region_index:%d!\n",region_index);
        /* 触摸屏支持多触点事件,但这里只响应单个触点 */
        if(-1 == slot_id){
            slot_id = event.slot_id;
        }else if(slot_id != event.slot_id){
            continue;
        }
        //只处理特定区域内的事件
        if(region_index < 0 || REGION_MAIN_FILE_PATH == region_index || 
           REGION_MAIN_FILE_PAGES == region_index || REGION_MAIN_FILE_DIR == region_index){
            if(!event.presssure && (-1 != pre_region_index)){
                invert_region(regions[pre_region_index].pixel_data);
                flush_page_region(&regions[pre_region_index],default_display);
            }
            pre_region_index = -1;
            pressure = 0;
            slot_id = -1;
            continue;           
        }
        
        if(event.presssure){                //按下
            if(!pressure && -1 == pre_region_index){     //还未曾有按钮按下   
                pre_region_index = region_index;
                pressure = 1;
                /* 反转按下区域的颜色 */
                if(region_index < dir_select_region_base){
                    invert_region(regions[region_index].pixel_data);
                    flush_page_region(&regions[region_index],default_display); 
                }     
            }
        }else{                  //松开
            if(!pressure) continue;
            /* 按下和松开的是同一个区域，这是一次有效的点击 */
            if(region_index == pre_region_index){
                page_param.id = browse_page.id;
                pressure = 0;
                slot_id = -1;
                pre_region_index = -1;

                /* 如果点击的是文件区域,先算出点击的文件是目录项数据数组中的第几个 */
                if(region_index >= REGION_FILE_DIR_BASE && region_index < (REGION_FILE_DIR_BASE + 3 *files_per_page)){
                    selected_file_index = (region_index - REGION_FILE_DIR_BASE - 2 * files_per_page) + start_file_index;
                    if(selected_file_index >= cur_dirent_nums){
                        continue;
                    }
                }

                switch (region_index){
                    case REGION_MENU_HOME:             /* home */
                        if(dir_select_status){
                            goback_menu_cb_func();
                        }else{
                            home_menu_cb_func();
                            return 0;
                        }
                        break;
                    case REGION_MENU_GOBACK:             /* goback */
                        if(dir_select_status){
                            save_autoplay_dir_menu_cb_func();
                            continue;
                        }else{
                            goback_menu_cb_func();
                        }       
                        break;
                    case REGION_MENU_SELECT:             /* select */
                        if(dir_select_status){
                            ret = start_autoplay_menu_cb_func(pre_page_id);
                            /* ret 为0不用跳转；为1要跳转，且前一个页面是主页面，直接调用run函数；
                             * 为1要跳转，且前一个页面正是 连播页面 ，直接返回； */
                            if(ret == 1){
                                next_page = get_page_by_name("autoplay_page");
                                page_param.id = browse_page.id;
                                next_page->run(&page_param);
                            }else if(ret == 2){
                                return 0;
                            }
                            continue;
                        }else{
                            select_menu_cb_func();
                            continue;
                        }
                        break;
                    case REGION_MENU_PRE_PAGE:             /* pre_page */
                        prepage_menu_cb_func();
                        break;
                    case REGION_MENU_NEXT_PAGE:             /* next_page */
                        nextpage_menu_cb_func();
                        break;
                    case REGION_MAIN_FILE_PATH:
                    case REGION_MAIN_FILE_PAGES:             /* 路径名显示区域和页数信息区域暂不响应 */
                        continue;
                    case REGION_MAIN_FILE_DIR:
                        continue;
                    default:            
                        if(region_index >= dir_select_region_base){
                            /* 文件夹选择区域 */
                            select_dir_cb_func(region_index);
                            continue;
                        }else{          
                            /* 文件区域 */
                            file_dir_area_cb_func(region_index,&selected_file_index);
                        }
                        continue;
                }
                invert_region(regions[region_index].pixel_data);
                flush_page_region(&regions[region_index],default_display);
            }else{
                invert_region(regions[pre_region_index].pixel_data);
                flush_page_region(&regions[pre_region_index],default_display);
            }
        }
    }   
    return 0;
}


static struct page_struct browse_page = {
    .name = "browse_page",
    .init = browse_page_init,
    .exit = browse_page_exit,
    .run  = browse_page_run,
    .allocated = 0,
};

int browse_init(void)
{
    return register_page_struct(&browse_page);
}