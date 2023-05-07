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
#include "list.h"
#include "file.h"

static struct page_struct browse_page;

/* 用于缓存预览图 */
struct preview_cache
{
    struct list_head list;          //用于构建链表
    struct pixel_data data;         //用于保存实际缓存数据
    unsigned short file_index;      //该缓存对应的文件索引
    unsigned short err;             //表示数据获取是否出错
};

/* 用于给区域编号的枚举 */
enum region_info{
    REGION_MENU_RETURN = 0,
    REGION_MENU_ADJUST_ICON,      
    REGION_MENU_SELECT,      
    REGION_MENU_PRE_PAGE,    
    REGION_MENU_NEXT_PAGE, 
    REGION_MAIN,                        //主体区域，包括下面三个  
    REGION_MAIN_FILE_PATH,   
    REGION_MAIN_FILE_PAGES,  
    REGION_MAIN_FILE_DIR,              //专门用于刷新显存
    REGION_FILE_DIR_BASE,    
};
#define REGION_FILE_PATH_HEIGHT   40
#define REGION_FILE_PAGES_HEIGHT  40

/* 以下是本页面要用到的图标信息 */
enum icon_info{
    ICON_MENU_RETURN = 0,
    ICON_MENU_ADJUST_ICON,
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
    ICON_NUM_0,
    ICON_NUM_1,
    ICON_NUM_2,
    ICON_NUM_3,
    ICON_NUM_4,
    ICON_NUM_5,
    ICON_NUM_6,
    ICON_NUM_7,
    ICON_NUM_8,
    ICON_NUM_9,
    ICON_NUMS, 
};

/* 图标文件名字符串数组 */
static const char *icon_file_names[] = {
    [ICON_MENU_RETURN]          = "return.png",
    [ICON_MENU_ADJUST_ICON]     = "adjust_icon.png",
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
    [ICON_SAVE_AUTOPLAY_DIR]    = "save_autoplay_dir.png",
    [ICON_START_AUTOPLAY]       = "start_autoplay.png",
    [ICON_NUM_0]                = "num_0.png",
    [ICON_NUM_1]                = "num_1.png",
    [ICON_NUM_2]                = "num_2.png",
    [ICON_NUM_3]                = "num_3.png",
    [ICON_NUM_4]                = "num_4.png",
    [ICON_NUM_5]                = "num_5.png",
    [ICON_NUM_6]                = "num_6.png",
    [ICON_NUM_7]                = "num_7.png",
    [ICON_NUM_8]                = "num_8.png",
    [ICON_NUM_9]                = "num_9.png",
};

/* 图标对应的区域，数组下标表示图标编号，下标对应的数组项表示该图标对应的区域,用于缩放图标 */
static unsigned int icon_region_links[] = {
    [ICON_MENU_RETURN]          = REGION_MENU_RETURN,
    [ICON_MENU_ADJUST_ICON]     = REGION_MENU_ADJUST_ICON,
    [ICON_MENU_SELECT]          = REGION_MENU_SELECT,
    [ICON_MENU_PRE_PAGE]        = REGION_MENU_PRE_PAGE,
    [ICON_MENU_NEXT_PAGE]       = REGION_MENU_NEXT_PAGE,
    [ICON_FILETYPE_DIR]         = REGION_FILE_DIR_BASE, 
    [ICON_FILETYPE_FILE_BMP]    = REGION_FILE_DIR_BASE,
    [ICON_FILETYPE_FILE_JPEG]   = REGION_FILE_DIR_BASE,
    [ICON_FILETYPE_FILE_PNG]    = REGION_FILE_DIR_BASE,
    [ICON_FILETYPE_FILE_GIF]    = REGION_FILE_DIR_BASE,
    [ICON_FILETYPE_FILE_TXT]    = REGION_FILE_DIR_BASE,
    [ICON_FILETYPE_FILE_OTHER]  = REGION_FILE_DIR_BASE,
    [ICON_DIR_SELECTED]         = 0,                    /* 这几个需要在运行时才能确定 */
    [ICON_DIR_UNSELECTED]       = 0,
    [ICON_SAVE_AUTOPLAY_DIR]    = REGION_MENU_RETURN,
    [ICON_START_AUTOPLAY]       = REGION_MENU_RETURN,
    [ICON_NUM_0]                = 0,                    /* 这几个需要在运行时才能确定 */
    [ICON_NUM_1]                = 0,
    [ICON_NUM_2]                = 0,
    [ICON_NUM_3]                = 0,
    [ICON_NUM_4]                = 0,
    [ICON_NUM_5]                = 0,
    [ICON_NUM_6]                = 0,
    [ICON_NUM_7]                = 0,
    [ICON_NUM_8]                = 0,
    [ICON_NUM_9]                = 0,
};
static struct pixel_data icon_pixel_datas[ICON_NUMS];

/* 定义文件图标以及文件名称字符串的大小,图标是一个正方体, "图标+名字"也是一个正方体
 *   --------
 *   |  图  |
 *   |  标  |
 * ------------
 * |   名字   |
 * ------------
 */
#define FILE_ICON_WIDTH     100
#define FILE_ICON_HEIGHT FILE_ICON_WIDTH
#define FILE_NAME_HEIGHT    (FILE_ICON_WIDTH / 5)
#define FILE_NAME_WIDTH (FILE_NAME_HEIGHT + FILE_ICON_HEIGHT)
#define FILE_ALL_WIDTH FILE_NAME_WIDTH
#define FILE_ALL_HEIGHT FILE_ALL_WIDTH
#define DIR_SELECT_ICON_WIDTH 40
static unsigned short file_icon_width = FILE_ICON_WIDTH;       /* 文件图标宽度，之所以设为变量是因为该值是可变的 */
static unsigned short file_icon_height = FILE_ICON_HEIGHT; 
static unsigned short file_name_height = FILE_NAME_HEIGHT; 
static unsigned short file_name_width = (FILE_ICON_HEIGHT + FILE_NAME_HEIGHT); 
static unsigned short file_all_width = (FILE_ICON_HEIGHT + FILE_NAME_HEIGHT);
static unsigned short file_all_height = (FILE_ICON_HEIGHT + FILE_NAME_HEIGHT);
/* 文件图标的最大尺寸，缓存中预览图就是按此大小缓存的，注意预览图也是图标 */
static unsigned short file_icon_max_width;

/* 与文件夹选择相关的几个全局变量(用于连播) */
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
static int start_file_index = 0;                    //当前页的起始文件索引
static unsigned int file_cols,file_rows,files_per_page;
static bool dir_contents_generated = 0;

/* 页数信息（用于显示底部页码） */
#define PAGE_NUM_HEIGHT 30
static int page_num_height;         //页码高度；对于页码宽度，以及总共能显示多少个页码，这都是动态生成的
static int page_num_padding = 40;   //页码边距 
static int page_num_interval = 40;  //页码间距

/* 页面缓存信息，为了更好的响应 */
#define PAGE_CACHE_COUNT 7          //页面缓存的数量，最多缓存7个
static struct pixel_data *page_caches[PAGE_CACHE_COUNT];
static struct pixel_data ** const cur_page_cache = &page_caches[PAGE_CACHE_COUNT / 2];

/* 用于生成预览图的一些变量 */
#define SZ_16M (16 * 1024 * 1024)
static pthread_t preview_thread_id;
static pthread_mutex_t preview_thread_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t preview_thread_cond = PTHREAD_COND_INITIALIZER;
//用于构建预览图缓存链表;
static struct list_head preview_cache_list = LIST_HEAD_INIT(preview_cache_list);         
static int preview_cache_size;                          //当前缓存的数据的大小
static int preview_max_size = SZ_16M;                   //缓存最大16M
static bool preview_pause = 0;                          //是否暂停更新预览图，当进入图片浏览页面时置此位


static int browse_page_calc_file_layout(struct page_struct *page)
{
    struct page_layout *layout;
    struct page_region *regions;
    unsigned int width,height;
    unsigned int file_all_width,file_name_height,file_icon_width;
    unsigned int preview_icon_width;
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
        y_start = layout->regions[REGION_MAIN_FILE_PATH].height;
        width = layout->width - x_start;
        height = layout->height - layout->regions[REGION_MAIN_FILE_PAGES].height - layout->regions[REGION_MAIN_FILE_PATH].height;
        
    }

    file_icon_max_width = (height - 30) / 2;        //文件图标的最大尺寸

    file_all_width = FILE_ALL_WIDTH;
    file_name_height = FILE_NAME_HEIGHT;
    file_icon_width = FILE_ICON_WIDTH;
    /* 显示预览图时的图标大小 */
    preview_icon_width = file_icon_width / 3;

    /* 确定每页显示多少个文件,每个图标之间间隔要大于10px */
    cols = (width - 10) / (file_all_width + 10);
    rows = (height - 10) / (file_all_width + 10);
    num_files = cols * rows;
    /* 将这几个变量存入全局变量中 */
    file_rows = rows;
    file_cols = cols;
    files_per_page = num_files;

    /* 为region数组分配空间,注意每个文件需要五个page_region：
     * 一个表示图标，一个表示名字,一个表示预览时的图标，一个将包含整个整体专门用于点击事件，一个位于文件图标右上角专门用于选择 */
    regions = malloc((layout->region_num + num_files * 5) * sizeof(struct page_region));
    if(!regions){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    /* 将已存在的菜单布局复制过来，并释放原有空间 */
    memcpy(regions,layout->regions,sizeof(struct page_region) * layout->region_num);
    free(layout->regions);
    layout->regions = regions;
    layout->region_num = num_files * 5 + layout->region_num;

    /* 开始计算页面布局 */
    x_delta = (width - cols * file_all_width) / (cols + 1);
    y_delta = (height - rows * file_all_width) / (rows + 1);
    x_cursor = x_start + x_delta;
    y_cursor = y_start + y_delta;
    if(layout->width >= layout->height){            /* 暂只考虑横屏的情况 */
        /* 此区域代表整个主体区域，包含文件路径、文件文件夹浏览、页码这几个区域 */
        regions[REGION_MAIN].x_pos  = x_start;
        regions[REGION_MAIN].y_pos  = 0;
        regions[REGION_MAIN].width  = width;
        regions[REGION_MAIN].height = layout->height;
        regions[REGION_MAIN].index  = REGION_MAIN;
        regions[REGION_MAIN].level  = 0;

        /* 此区域代表整个文件显示区域 */
        regions[REGION_MAIN_FILE_DIR].x_pos = x_start;
        regions[REGION_MAIN_FILE_DIR].y_pos = y_start;
        regions[REGION_MAIN_FILE_DIR].width = width;
        regions[REGION_MAIN_FILE_DIR].height = height;
        regions[REGION_MAIN_FILE_DIR].index = REGION_MAIN_FILE_DIR;
        regions[REGION_MAIN_FILE_DIR].level = 0;

        unsigned int temp = (file_all_width - file_icon_width) / 2;
        k = REGION_FILE_DIR_BASE;
        region_offset = k + num_files * 3;
        region_offset1 = k + num_files * 4;
        for(i = 0 ; i < rows ; i++){
            for(j = 0 ; j < cols ; j++){
                /* 文件夹选择区域(右上角的选择框) */
                regions[region_offset1].x_pos  = x_cursor + file_all_width - DIR_SELECT_ICON_WIDTH + 10;
                regions[region_offset1].y_pos  = y_cursor - 10;
                regions[region_offset1].width  = DIR_SELECT_ICON_WIDTH;
                regions[region_offset1].height = DIR_SELECT_ICON_WIDTH;
                regions[region_offset1].index  = region_offset1;
                regions[region_offset1].level  = 2;
                regions[region_offset1].invisible = 1;
                region_offset1++;

                /* 用于响应点击事件的区域,注意level为1 */
                regions[region_offset].x_pos  = x_cursor;
                regions[region_offset].y_pos  = y_cursor;
                regions[region_offset].width  = file_all_width;
                regions[region_offset].height = file_all_width;
                regions[region_offset].index  = region_offset;
                regions[region_offset].level  = 1;
                region_offset++;

                /* 文件图标区域 */
                regions[k].x_pos = x_cursor + temp;
                regions[k].y_pos = y_cursor;
                regions[k].width = file_icon_width;
                regions[k].height = file_icon_width;
                regions[k].index = k;
                regions[k].level = 0;
                regions[k].invisible = 1;
                k++;

                /* 当显示预览图时的文件图标 */
                regions[k].x_pos = x_cursor + temp + (file_icon_width - preview_icon_width);
                regions[k].y_pos = y_cursor + (file_icon_width - preview_icon_width);
                regions[k].width = preview_icon_width;
                regions[k].height = preview_icon_width;
                regions[k].index = k;
                regions[k].level = 0;
                regions[k].invisible = 1;
                k++;

                /* 文件名区域 */
                regions[k].x_pos = x_cursor;
                regions[k].y_pos = y_cursor + file_icon_width;
                regions[k].width = file_all_width;
                regions[k].height = file_name_height;
                regions[k].index = k;
                regions[k].level = 0;
                regions[k].invisible = 1;
                k++;

                x_cursor += (file_all_width + x_delta);
            }
            x_cursor = x_start + x_delta;
            y_cursor += (file_all_width + y_delta);
        }
    }
    
    /* 用于确定图标大小 */
    icon_region_links[ICON_DIR_SELECTED] = REGION_FILE_DIR_BASE + files_per_page * 4;
    icon_region_links[ICON_DIR_UNSELECTED] = REGION_FILE_DIR_BASE + files_per_page * 4;
    return 0;
}

static int browse_page_calc_menu_layout(struct page_struct *page)
{
    struct page_layout *layout;
    struct page_region *regions;
    unsigned int width,height;
    unsigned int x_cursor,y_cursor,unit_distance;
    unsigned int i;
    unsigned int temp;

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
        regions[REGION_MENU_RETURN].x_pos  = x_cursor;
        regions[REGION_MENU_RETURN].y_pos  = y_cursor;
        regions[REGION_MENU_RETURN].height = unit_distance;
        regions[REGION_MENU_RETURN].width  = unit_distance;
        
        /* "goback" */
        regions[REGION_MENU_ADJUST_ICON].x_pos  = x_cursor ;
        regions[REGION_MENU_ADJUST_ICON].y_pos  = y_cursor + unit_distance;
        regions[REGION_MENU_ADJUST_ICON].height = unit_distance;
        regions[REGION_MENU_ADJUST_ICON].width  = unit_distance;
        
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
        regions[REGION_MAIN_FILE_PATH].height = 40 ;
        regions[REGION_MAIN_FILE_PATH].width  = width - unit_distance ;
        
        /* 页数信息显示区域 */
        regions[REGION_MAIN_FILE_PAGES].x_pos  = unit_distance;
        regions[REGION_MAIN_FILE_PAGES].y_pos  = height - 40;
        regions[REGION_MAIN_FILE_PAGES].height = 40 ;
        regions[REGION_MAIN_FILE_PAGES].width  = width - unit_distance ;
        page_num_height = regions[REGION_MAIN_FILE_PAGES].height - 10;
        /* 确定相应数字图标的大小 */
        temp = page_num_height * 6 / 10;
        for(i = ICON_NUM_0 ; i <= ICON_NUM_9 ; i++){
            icon_region_links[i] = (1 << 31) | (((temp * 2 / 3) & 0xfff) << 12) | (temp & 0xfff);
            icon_region_links[i] = (1 << 31) | (((temp * 2 / 3)  & 0xfff) << 12) | (temp & 0xfff);
        }
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
        regions[REGION_MENU_RETURN].x_pos = x_cursor;
        regions[REGION_MENU_RETURN].y_pos = y_cursor;
        regions[REGION_MENU_RETURN].height = unit_distance;
        regions[REGION_MENU_RETURN].width = unit_distance;

        /* "goback" */
        regions[REGION_MENU_ADJUST_ICON].x_pos = x_cursor + unit_distance;
        regions[REGION_MENU_ADJUST_ICON].y_pos = y_cursor;
        regions[REGION_MENU_ADJUST_ICON].height = unit_distance;
        regions[REGION_MENU_ADJUST_ICON].width = unit_distance;

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

/* 在此函数中将会计算好页面的布局情况，浏览页面主要可分为两部分：1.菜单部分   2.文件浏览部分 */
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

/* 销毁预览图缓存 */
static void destroy_preview_caches(void)
{
    struct preview_cache *preview_cache,*temp;

    list_for_each_entry_safe(preview_cache,temp,&preview_cache_list,list){
        if(preview_cache->data.buf)
            free(preview_cache->data.buf);
        free(preview_cache);
    }

    preview_cache_size = 0;
    preview_cache_list.prev = &preview_cache_list;
    preview_cache_list.next = &preview_cache_list;
}

static int fill_menu_icon_area(struct page_struct *browse_page)
{
    int i,ret;
    struct page_region *regions = browse_page->page_layout.regions;

    if(!browse_page->already_layout){
        browse_page_calc_layout(browse_page);
    }else if(!browse_page->icon_prepared){
        prepare_icon_pixel_datas(browse_page,icon_pixel_datas,icon_file_names,icon_region_links,ICON_NUMS); 
    }else if(!browse_page->region_mapped){
        remap_regions_to_page_mem(browse_page);
    }

    for(i = 0 ; i <= REGION_MENU_NEXT_PAGE ; i++){
        /* 先清理 */
        clear_pixel_data(regions[i].pixel_data,BACKGROUND_COLOR);
        /* 将图像数据并入到相应区域映射到的内存中 */
        if(dir_select_status){
            if(i == 0){
                ret = merge_pixel_data(regions[i].pixel_data,&icon_pixel_datas[ICON_MENU_RETURN]);
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
static int fill_file_path_area(struct pixel_data *pixel_data)
{
    int font_size;
    int ret;
    // struct page_region *path_region = &browse_page.page_layout.regions[REGION_MAIN_FILE_PATH];

    font_size = pixel_data->height - 10;
    clear_pixel_data(pixel_data,BACKGROUND_COLOR);
    ret = get_string_bitamp_from_buf(cur_dir,0,"utf-8",pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xfd8,font_size);
    if(ret < 0){
        DP_ERR("%d:get_string_bitamp_from_buf failed!\n",__func__);
        return ret;
    }
    return 0 ;
}

/* 填充页数信息,此函数会被fill_file_dir_area调用 */
static int fill_pages_info_area(struct pixel_data *pixel_data,int start_file_index)
{
#define LINE_COLOR 0x4d4d       /* 页标圆圈线条颜色 */
#define LINE_WIDTH 3            /* 页标圆圈线条宽度*/
#define FILL_COLOR 0xd4d4       /* 被选中的页标填充的颜色*/
    int cur_page = (start_file_index / files_per_page) + 1;
    int total_pages =  (cur_dirent_nums - 1) / files_per_page + 1;

    struct page_region *pages_region = &browse_page.page_layout.regions[REGION_MAIN_FILE_PAGES];
    struct pixel_data *region_data = pixel_data;
    struct pixel_data temp_data;
    
    unsigned char page_num_widths[16] = {0};        /* 保存临时生成的页码宽度 */
    unsigned char page_num_digitals[16][16] = {{0}};  /* 页码对应的每一位数字，10进制 */
    short array_base_index = 8;
    short num_height,num_width;                       /* 每一位数字对应的宽高 */
    short num_count,num_start_x;
    short page_num_width;
    short region_width = region_data->width - page_num_padding * 2;
    short region_height = region_data->height;
    short cur_total_width = 0;                        /* 当前已生成的页码内容的总体宽度 */
    short start_x,start_y;
    short i,j,k;
    short x1,x2;
    float r1,r2,temp1;
    short temp,bytes_per_pixel;
    unsigned char *line_buf;
    
    num_height = page_num_height * 6 / 10;
    num_width = num_height * 2 / 3;
    num_start_x = page_num_height / 5;
    
    /* 清理该区域 */
    clear_pixel_data(region_data,BACKGROUND_COLOR);

    /* 先算出当前页码对应的显示位图的宽度及每位数字 */
    temp = cur_page;
    num_count = 0;
    do{
        page_num_digitals[array_base_index][1 + num_count++] = temp % 10;
    }while((temp /= 10) > 0);
    page_num_digitals[array_base_index][0] = num_count;   //第一位存储该页码要用多少个数字显示
    page_num_widths[array_base_index] = num_count * num_width + num_start_x * 2;
    if(page_num_widths[array_base_index] < page_num_height)
        page_num_widths[array_base_index] = page_num_height;
    cur_total_width = page_num_widths[array_base_index];
    
    
    /* 先算出能显示多少的页码，以及这个区域的总宽度 */
    for(i = 1 ; i < 9 ; i++){     
        /* 尝试以当前页为中心，依次左边右边各填一页，直到总体宽度超出 */
        if((temp = cur_page - i) > 0){
            num_count = 0;
            do{
                page_num_digitals[array_base_index - i][1 + num_count++] = temp % 10;
            }while((temp /= 10) > 0);
            page_num_digitals[array_base_index - i][0] = num_count;   //第一位存储该页码要用多少个数字显示
            page_num_widths[array_base_index - i] = num_count * num_width + num_start_x * 2;
            if(page_num_widths[array_base_index - i] < page_num_height)
                page_num_widths[array_base_index - i] = page_num_height;
            cur_total_width += (page_num_widths[array_base_index - i] + page_num_interval);

            if(cur_total_width >= region_width){
                cur_total_width -= (page_num_widths[array_base_index - i] + page_num_interval);
                page_num_widths[array_base_index - i] = 0;
                page_num_digitals[array_base_index - i][0] = 0;
                break;
            }
        }
            
        if((temp = cur_page + i) <= total_pages){
            num_count = 0;
            do{
                page_num_digitals[array_base_index + i][1 + num_count++] = temp % 10;
            }while((temp /= 10) > 0);
            
            page_num_digitals[array_base_index + i][0] = num_count;
            page_num_widths[array_base_index + i] = num_count * num_width + num_start_x * 2;
            if(page_num_widths[array_base_index + i] < page_num_height)
                page_num_widths[array_base_index + i] = page_num_height;
            cur_total_width += (page_num_widths[array_base_index + i] + page_num_interval);

            if(cur_total_width >= region_width){
                cur_total_width -= (page_num_widths[array_base_index + i] + page_num_interval);
                page_num_widths[array_base_index + i] = 0;
                page_num_digitals[array_base_index + i][0] = 0;
                break;
            }  
        }  
    }

    /* 计算开始描画的起点 */
    start_x = (region_data->width - cur_total_width) / 2;
    start_y = (region_data->height - page_num_height) / 2;
    
    /* 开始描画 */
    /* 映射区域内存 */
    memset(&temp_data,0,sizeof(struct pixel_data));
    temp_data.height = page_num_height;
    temp_data.bpp = pages_region->pixel_data->bpp;
    temp_data.in_rows = 1;
    bytes_per_pixel = temp_data.bpp / 8;
    if(NULL == (temp_data.rows_buf = malloc(sizeof(unsigned char *) * temp_data.height))){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    for(i = 0 ; i < temp_data.height ; i++){
        temp_data.rows_buf[i] = region_data->rows_buf[start_y + i]  + start_x * region_data->bpp / 8;
    }
    
    r2 = (float)(page_num_height / 2) * (page_num_height / 2);
    r1 = (float)(page_num_height / 2 - 3) * (page_num_height / 2 - 3);
    x1 = page_num_height / 2;
    for(i = 0 ; i < 16 ; i++){
        /* 被选中的页做单独处理 */
        if(0 == page_num_widths[i])
            continue;
        
        temp_data.width = page_num_widths[i];
        temp_data.line_bytes = temp_data.width * temp_data.bpp / 8;

        /* 画半圆矩形边框 */
        x2 = page_num_widths[i] - x1;
        page_num_width = page_num_widths[i];
        for(j = 0 ; j < page_num_height ; j++){
            line_buf = temp_data.rows_buf[j] ;
            for(k = 0 ; k < page_num_width ; k++){
                if(k < x1){       /* 半圆区域 */
                    temp1 = (float)(x1 - k) * (x1 - k) + (x1 - j) * (x1 - j);
                }else if(k > x2){
                    temp1 = (float)(k - x2) * (k - x2) + (x1 - j) * (x1 - j);
                }else{
                    *(unsigned short *)line_buf = LINE_COLOR;
                    line_buf += bytes_per_pixel;
                    continue;
                }
                if(temp1 > r2){      /* 区域外 */
                    line_buf += bytes_per_pixel;  
                }else if(temp1 >= r1){ /* 线内,需填充 */
                    *(unsigned short *)line_buf = LINE_COLOR;
                    line_buf += bytes_per_pixel;
                }else{
                    /* 内部区域,直接跳转到另一侧,如果是选中页则填充 */
                    if(__glibc_unlikely(i == array_base_index)){    //填充
                        temp = k + 2 * (x1 - k) + (x2 - x1);
                        for( ; k <= temp ; k++){
                            *(unsigned short *)line_buf = FILL_COLOR;
                            line_buf += bytes_per_pixel;
                        }
                    }else{  //跳转
                        line_buf += (2 * (x1 - k) + (x2 - x1) ) * bytes_per_pixel;
                        k += 2 * (x1 - k) + (x2 - x1) ; 
                    }
                }
            }
        }
        start_x += page_num_width + page_num_interval;
        for(j = 0 ; j < temp_data.height ; j++){
            temp_data.rows_buf[j] = region_data->rows_buf[start_y + j]  + start_x * region_data->bpp / 8;
        }
    }

    /* 依次填充数字 */
    start_y = (region_data->height - page_num_height) / 2 + (page_num_height - num_height) / 2;
    start_x = (region_data->width - cur_total_width) / 2 + num_start_x;
    temp_data.width = num_width;
    temp_data.height = num_height;
    temp_data.line_bytes = temp_data.width * bytes_per_pixel;
    for(i = 0 ; i < 16 ; i++){
        /* 被选中的页做单独处理 */
        if(0 == page_num_digitals[i][0])
            continue;

        num_count = page_num_digitals[i][0];
        
        if(num_count == 1){
            temp = start_x + (page_num_height - num_width - num_start_x * 2) / 2;
            for(j = 0 ; j < num_height ; j++){
                temp_data.rows_buf[j] = region_data->rows_buf[start_y + j]  + temp * region_data->bpp / 8;
            }
        }else{
            temp = start_x;
            for(j = 0 ; j < num_height ; j++){
                temp_data.rows_buf[j] = region_data->rows_buf[start_y + j]  + temp * region_data->bpp / 8;
            }
        }

        for( ; num_count-- ;){
            merge_pixel_data(&temp_data,&icon_pixel_datas[ICON_NUM_0 + page_num_digitals[i][num_count + 1]]);
            for(j = 0 ; j < temp_data.height ; j++){
                temp_data.rows_buf[j] += num_width * region_data->bpp / 8;
            }
        }
        start_x += page_num_widths[i] + page_num_interval;
        for(j = 0 ; j < temp_data.height ; j++){
            temp_data.rows_buf[j] = region_data->rows_buf[start_y + j]  + start_x * region_data->bpp / 8;
        }
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

    flush_page_region(&regions[REGION_MENU_RETURN],display);
    flush_page_region(&regions[REGION_MENU_ADJUST_ICON],display);
    flush_page_region(&regions[REGION_MENU_SELECT],display);
    flush_page_region(&regions[REGION_MENU_PRE_PAGE],display);
    flush_page_region(&regions[REGION_MENU_NEXT_PAGE],display);
}

/* 用于填充各区域 */
static int browse_page_fill_layout(struct page_struct *browse_page)
{
    int ret;

    /* 如果想加个整体的背景，应该最先加进去 */
    
    /* 准备各种图标数据 */
    if(!browse_page->icon_prepared){
        prepare_icon_pixel_datas(browse_page,icon_pixel_datas,icon_file_names,icon_region_links,ICON_NUMS);
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
    return 0;
}


/* @description : 根据指定的开始文件索引，填充主体区域，可用于实现缓存
 * @param : start_file_index - 要为其填充内容的某页第一个文件所对应的索引*/
static int fill_main_area(int start_file_index)
{
    int ret;
    int i,last_file_index;
    int base_region_index,base_icon_index;
    int font_size,file_type;
    struct page_region *regions = browse_page.page_layout.regions;
    struct page_region *main_region = &browse_page.page_layout.regions[REGION_MAIN];
    struct pixel_data temp_data;
    char *file_name;
    char file_name_malloc = 0;
    char file_full_path[100];

    /* 清理内容 */
    clear_pixel_data(main_region->pixel_data,BACKGROUND_COLOR);
    
    /* 计算几个填充内容时要用的变量 */
    base_region_index = REGION_FILE_DIR_BASE;
    base_icon_index = ICON_FILETYPE_DIR;
    font_size = regions[base_region_index + 2].height;
    
    /* 填充内容 */
    /* 主体的文件文件夹浏览区域 */
    /* 需要注意的一点,对于填充内容而言,一个目录项对应三个区域,一个图标,一个文字，一个可选的缩略图标 */
    for(i = 0 ; i < files_per_page ; i++){
        if((start_file_index + i) >= cur_dirent_nums){
            break;          /* 已经填充完了所有目录项,直接退出 */
        }
        /* 文件夹,对于目录来说比较简单 */
        if(FILETYPE_DIR == cur_dir_contents[start_file_index + i]->type){
            /* 填充图标 */
            merge_pixel_data(regions[base_region_index + 3 * i].pixel_data,&icon_pixel_datas[ICON_FILETYPE_DIR]);
            /* 如果当前处于目录选择状态，则填充相应图标 */
            if(dir_select_status){
                merge_pixel_data(regions[base_region_index + 4 * files_per_page + i].pixel_data,&icon_pixel_datas[ICON_DIR_UNSELECTED]);
            }
            /* 填充文字 */
            get_string_bitamp_from_buf(cur_dir_contents[start_file_index + i]->name,0,"utf-8",\
            regions[base_region_index + i * 3 + 2].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xff00,font_size);
        }else if(FILETYPE_REG == cur_dir_contents[start_file_index + i]->type){
            /* 获取文件类型 */
            if(!cur_dir_contents[start_file_index + i]->file_type){
                cur_dir_contents[start_file_index + i]->file_type = get_file_type(cur_dir,cur_dir_contents[start_file_index + i]->name);
            }
            file_type = cur_dir_contents[start_file_index + i]->file_type;
            /* 填充图标 */
            merge_pixel_data(regions[base_region_index + 3 * i].pixel_data,&icon_pixel_datas[base_icon_index + file_type]);
            
            /* 填充文字 */
            get_string_bitamp_from_buf(cur_dir_contents[start_file_index + i]->name,0,"utf-8",\
            regions[base_region_index + i * 3 + 2].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xff00,font_size);
        }    
    }

    /* 填充页面路径信息 */
    ret = fill_file_path_area(regions[REGION_MAIN_FILE_PATH].pixel_data);
    if(ret < 0){
        DP_ERR("%d:fill_file_path_area failed!\n",__func__);
        return ret;
    }

    /* 填充页码信息 */
    ret = fill_pages_info_area(regions[REGION_MAIN_FILE_PAGES].pixel_data,start_file_index);
    if(ret < 0){
        DP_ERR("%d:fill_pages_info_area failed!\n",__func__);
        return ret;
    }

    return 0;
}

/* 用于实现预览图的线程函数 */
static void *preview_thread_func(void *data)
{
    int i,ret;
    int base_region_index = REGION_FILE_DIR_BASE;
    int pre_start_file_index;
    int file_type;
    int new_index;
    char *pre_cur_dir = NULL;
    char *preview_file_name = NULL;
    struct display_struct *display = get_default_display();
    struct page_region *regions = browse_page.page_layout.regions;
    struct preview_cache *preview_cache,*temp_cache;
    struct pixel_data pixel_data;

    pthread_detach(pthread_self());
    
    /* 逻辑对我来说有点复杂的，现在作简单点，它是为当前浏览的图片文件读取预览图，而且读完后会继续往下读 */
    while(1){
retry:
        /* 保存当前目录和起始文件索引，以在后面更新显存时判断是否已经不是当前页了 */
        pthread_mutex_lock(&preview_thread_mutex);
        pre_start_file_index = start_file_index;
        if(pre_cur_dir){
            if(strcmp(cur_dir,pre_cur_dir)){
                destroy_preview_caches();
                free(pre_cur_dir);
                pre_cur_dir = NULL;
                pthread_mutex_unlock(&preview_thread_mutex);
                goto retry;
            }
            free(pre_cur_dir);
        }
        pre_cur_dir = malloc(strlen(cur_dir) + 1);
        if(!pre_cur_dir){
            DP_ERR("%s:malloc failed!\n",__func__);
            goto wait;
        }
        strcpy(pre_cur_dir,cur_dir);
        pthread_mutex_unlock(&preview_thread_mutex);
        
file_index_changed:
        /* 先完成当前页面的预览图（如果有图片文件的话） */
        for(i = 0 ; i < files_per_page ; i++){
            if(pre_start_file_index + i >= cur_dirent_nums)
                break;
            printf("%s-%d\n",__func__,__LINE__);
            /* 获取到要检查的文件的类型和名字 */
            pthread_mutex_lock(&preview_thread_mutex);
            if(strcmp(cur_dir,pre_cur_dir)){
                destroy_preview_caches();
                free(pre_cur_dir);
                pre_cur_dir = NULL;
                pthread_mutex_unlock(&preview_thread_mutex);
                goto retry;
            }
            printf("%s-%d\n",__func__,__LINE__);
            if(!cur_dir_contents[pre_start_file_index + i]->file_type && cur_dir_contents[pre_start_file_index + i]->type == FILETYPE_REG ){
                cur_dir_contents[pre_start_file_index + i]->file_type = get_file_type(cur_dir,cur_dir_contents[pre_start_file_index + i]->name);
            }
            file_type = cur_dir_contents[pre_start_file_index + i]->file_type;
            printf("%s-%d\n",__func__,__LINE__);
            /* 构造文件名 */
            if(preview_file_name){
                free(preview_file_name);
                preview_file_name = NULL;
            }
            printf("%s-%d\n",__func__,__LINE__);
            if(NULL == (preview_file_name = malloc(strlen(cur_dir) + 2 + strlen(cur_dir_contents[pre_start_file_index + i]->name)))){
                DP_ERR("%s:malloc failed!\n",__func__);
                destroy_preview_caches();
                free(pre_cur_dir);
                pre_cur_dir = NULL;
                goto wait;
            }
            sprintf(preview_file_name,"%s/%s",cur_dir,cur_dir_contents[pre_start_file_index + i]->name);
            // printf("%s-%d-preview_file_name:%s\n",__func__,__LINE__,preview_file_name);
            // printf("%s-%d-pre_start_file_index + i:%d\n",__func__,__LINE__,pre_start_file_index + i);
            pthread_mutex_unlock(&preview_thread_mutex);
            printf("%s-%d\n",__func__,__LINE__);
            if(file_type >= FILETYPE_FILE_BMP && file_type <= FILETYPE_FILE_GIF){               
                /* 尝试寻找当前页面第一张图片对应的缓存，如果没有则生成 */
                list_for_each_entry(preview_cache,&preview_cache_list,list){
                    if(preview_cache->file_index == (pre_start_file_index + i)){
                        break;
                    }
                } 
                /* 如果未找到缓存，则生成缓存 */
                if(&preview_cache->list == &preview_cache_list){
                    /* 先检查缓存大小是否已达上限，如果是，则释放部分缓存 */
                    if(preview_cache_size >= preview_max_size){
                        if(pre_start_file_index > list_entry(preview_cache_list.next,struct preview_cache,list)->file_index){
                            temp_cache = list_entry(preview_cache_list.next,struct preview_cache,list);
                            list_del(preview_cache_list.next);
                            if(temp_cache->data.buf){
                                free(temp_cache->data.buf);
                            } else if(temp_cache->data.rows_buf){
                                free(temp_cache->data.rows_buf);
                            }
                            free(temp_cache);
                            preview_cache_size -= temp_cache->data.total_bytes;
                            preview_cache_size -= sizeof(*temp_cache);
                        }else if(pre_start_file_index < list_entry(preview_cache_list.prev,struct preview_cache,list)->file_index){
                            temp_cache = list_entry(preview_cache_list.prev,struct preview_cache,list);
                            list_del(preview_cache_list.prev);

                            if(temp_cache->data.buf){
                                free(temp_cache->data.buf);
                            } else if(temp_cache->data.rows_buf){
                                free(temp_cache->data.rows_buf);
                            }
                            free(temp_cache);
                            preview_cache_size -= temp_cache->data.total_bytes;
                            preview_cache_size -= sizeof(*temp_cache);
                        }
                    }
                    printf("%s-%d\n",__func__,__LINE__);
                    /* 生成新缓存 */
                    /* 分配一个 preview_cache */
                    if(NULL == (preview_cache = malloc(sizeof(struct preview_cache)))){
                        DP_ERR("%s:malloc failed!\n",__func__);
                        goto wait;
                    }
                    memset(preview_cache,0,sizeof(struct preview_cache));
                    preview_cache->file_index = pre_start_file_index + i;
                    
                    /* 获取数据 */
                    memset(&pixel_data,0,sizeof(struct pixel_data));
                    ret = get_pic_pixel_data(preview_file_name,file_type,&pixel_data);
                    if(ret > 0){
                        preview_cache->err = 1;
                        DP_ERR("%s:get_pic_pixel_data failed!\n",__func__);
                    }else{
                        /* 将图像缩放至合适大小 */
                        preview_cache->data.bpp = display->bpp;
                        ret = resize_pic_pixel_data(&preview_cache->data,&pixel_data,file_icon_max_width,file_icon_max_width);
                        if(ret){
                            DP_ERR("%s:resize_pic_pixel_data failed!\n",__func__);
                            goto wait;
                        }
                        preview_cache_size += preview_cache->data.total_bytes;
                    }
                    printf("%s-%d\n",__func__,__LINE__);
                    /* 更新缓存大小数据，将preview_cache加入链表 */
                    preview_cache_size += sizeof(struct preview_cache);
                    if(list_empty(&preview_cache_list)){
                        list_add_tail(&preview_cache->list,&preview_cache_list);
                    }else{
                        list_for_each_entry(temp_cache,&preview_cache_list,list){
                            if(temp_cache->file_index > preview_cache->file_index){
                                list_add_tail(&preview_cache->list,temp_cache->list.prev);
                                break;
                                // 
                            }
                        }
                        /* 如果要加入的preview_cache对应的文件索引是当前最大的 */
                        if(&temp_cache->list == &preview_cache_list){
                            list_add_tail(&preview_cache->list,&preview_cache_list);
                        }
                    }
                    printf("%s-%d\n",__func__,__LINE__);
                    /* 释放原有数据，不得不说，为了读个缩略图，这个代价有点太大了 */
                    if(pixel_data.buf){
                        free(pixel_data.buf);
                    }else if(pixel_data.rows_buf){
                        for(i = 0 ; i < pixel_data.height ; i++){
                            if(pixel_data.rows_buf[i])
                                free(pixel_data.rows_buf[i]);
                        }
                        free(pixel_data.rows_buf);
                    }
                }   
                printf("%s-%d\n",__func__,__LINE__);
                /* 如果能够找到缓存则直接更新到显存上，每次都更新 */
                /* 因为获取完预览图后，可能已经点击了下一页，要检查是否还是当前目录及是否是当前页，且要用互斥量保护 */
                pthread_mutex_lock(&preview_thread_mutex);
                /* 如果当前目录已改变，销毁数据，重新进入循环 */
                if(strcmp(cur_dir,pre_cur_dir)){
                    destroy_preview_caches();
                    free(pre_cur_dir);
                    pre_cur_dir = NULL;
                    pthread_mutex_unlock(&preview_thread_mutex);
                    goto retry;
                }
                printf("%s-%d\n",__func__,__LINE__);
                /* 如果起始索引改变（页数改变），重新开始循环 */
                if(start_file_index != pre_start_file_index){
                    pre_start_file_index = start_file_index;
                    pthread_mutex_unlock(&preview_thread_mutex);
                    goto file_index_changed;
                }
                printf("%s-%d\n",__func__,__LINE__);
                /* 如果要进入其它页面，暂停标志位置位，则暂停获取 */
                if(preview_pause){
                    pthread_mutex_unlock(&preview_thread_mutex);
                    goto wait;
                }
                
                if(!preview_cache->err && !cur_dir_contents[i + pre_start_file_index]->preview_cached && !preview_pause){
                    clear_pixel_data(regions[base_region_index + 3 * i].pixel_data,BACKGROUND_COLOR);
                    /* 填充预览图 */
                    adapt_pic_pixel_data(regions[base_region_index + 3 * i].pixel_data,&preview_cache->data);
                    /* 填充预览图图标 */
                    adapt_pic_pixel_data(regions[base_region_index + 3 * i + 1].pixel_data,&icon_pixel_datas[ICON_FILETYPE_DIR + file_type]);
                }
                /* 设置相应标志位，表示预览图已生成 */
                cur_dir_contents[pre_start_file_index + i]->preview_cached = 1;

                flush_page_region(&regions[base_region_index + 3 * i],display);
                pthread_mutex_unlock(&preview_thread_mutex);
            }
        }

        /* 继续读取后面的缓存，直到全部读取完或者到达内存上限 */
        new_index = i + pre_start_file_index;
        while(new_index < cur_dirent_nums && preview_cache_size <= preview_max_size){
            pthread_mutex_lock(&preview_thread_mutex);
            /* 如果当前目录已改变，销毁数据，重新进入循环 */
            if(strcmp(cur_dir,pre_cur_dir)){
                destroy_preview_caches();
                free(pre_cur_dir);
                pre_cur_dir = NULL;
                pthread_mutex_unlock(&preview_thread_mutex);
                goto retry;
            }
            
            /* 如果起始索引改变（页数改变），重新开始循环 */
            if(start_file_index != pre_start_file_index){
                pre_start_file_index = start_file_index;
                pthread_mutex_unlock(&preview_thread_mutex);
                goto file_index_changed;
            }

            /* 如果要1进入其它页面，暂停标志位置位，则暂停获取 */
            if(preview_pause){
                pthread_mutex_unlock(&preview_thread_mutex);
                goto wait;
            }
            
            /* 构造文件名 */
            if(preview_file_name){
                free(preview_file_name);
                preview_file_name = NULL;
            }

            if(NULL == (preview_file_name = malloc(strlen(cur_dir) + 2 + strlen(cur_dir_contents[new_index]->name)))){
                DP_ERR("%s:malloc failed!\n",__func__);
                pthread_mutex_unlock(&preview_thread_mutex);
                goto wait;
            }
            sprintf(preview_file_name,"%s/%s",cur_dir,cur_dir_contents[new_index]->name);
            file_type = cur_dir_contents[new_index]->file_type;
            pthread_mutex_unlock(&preview_thread_mutex);

            /* 生成新缓存,分配一个 preview_cache */
            if(NULL == (preview_cache = malloc(sizeof(struct preview_cache)))){
                DP_ERR("%s:malloc failed!\n",__func__);
                goto wait;
            }
            memset(preview_cache,0,sizeof(struct preview_cache));
            preview_cache->file_index = new_index;
            
            /* 获取数据 */
            memset(&pixel_data,0,sizeof(struct pixel_data));
            ret = get_pic_pixel_data(preview_file_name,file_type,&pixel_data);
            if(ret > 0){
                preview_cache->err = 1;
                DP_ERR("%s:get_pic_pixel_data failed!\n",__func__);
            }else{
                /* 将图像缩放至合适大小 */
                preview_cache->data.bpp = display->bpp;
                ret = resize_pic_pixel_data(&preview_cache->data,&pixel_data,file_icon_max_width,file_icon_max_width);
                if(ret){
                    DP_ERR("%s:resize_pic_pixel_data failed!\n",__func__);
                    goto wait;
                }
                preview_cache_size += preview_cache->data.total_bytes;
            }

             /* 更新缓存大小数据，将preview_cache加入链表 */
            preview_cache_size += sizeof(struct preview_cache);
            if(list_empty(&preview_cache_list)){
                list_add_tail(&preview_cache->list,&preview_cache_list);
            }else{
                list_for_each_entry(temp_cache,&preview_cache_list,list){
                    if(temp_cache->file_index > preview_cache->file_index){
                        list_add_tail(&preview_cache->list,temp_cache->list.prev);
                        break;
                    }
                }
                /* 如果要加入的preview_cache对应的文件索引是当前最大的 */
                if(&temp_cache->list == &preview_cache_list){
                    list_add_tail(&preview_cache->list,&preview_cache_list);
                }
            }
            
            /* 释放原有数据，不得不说，为了读个缩略图，这个代价有点太大了 */
            if(pixel_data.buf){
                free(pixel_data.buf);
            }else if(pixel_data.rows_buf){
                for(i = 0 ; i < pixel_data.height ; i++){
                    if(pixel_data.rows_buf[i])
                        free(pixel_data.rows_buf[i]);
                }
                free(pixel_data.rows_buf);
            }
        }   
wait:
        /* 进入睡眠，在点击上一页、下一页、进入新目录时被唤醒 */
        pthread_mutex_lock(&preview_thread_mutex);
        pthread_cond_wait(&preview_thread_cond,&preview_thread_mutex);
        pthread_mutex_unlock(&preview_thread_mutex);
    }
    return NULL;
}

static void destroy_page_caches(void)
{
    int i;

    for(i = 0 ; i < PAGE_CACHE_COUNT ; i++){
        if(page_caches[i]){
            if(page_caches[i]->buf){
                free(page_caches[i]->buf);
            }
            free(page_caches[i]);
            page_caches[i] = NULL;
        }
    }
}

/* 为本页面生成页面缓存，此函数依赖全局的目录信息，所以调用前应先获取，
 * 其此函数会从第一页开始生成,且会释放原有缓存 */
static int generate_page_caches(void)
{
    int i;
    int ret;
    int start_index;
    int total_pages =  (cur_dirent_nums - 1) / files_per_page + 1;
    struct page_region *regions = browse_page.page_layout.regions;
    struct display_struct *display = get_default_display();

    /* 为了配合实现缓存，使用该函数必须要求页面有自己单独的缓存 */
    if(browse_page.share_fbmem){
        DP_ERR("%s:invalied argument!\n",__func__);
        return -1;
    }

    if(!dir_contents_generated){
        DP_WARNING("%s dir contents has not generated!\n",__func__);
        return -1;
    }

    /* 如果已有缓存，则先释放 */
    for(i = 0 ; i < PAGE_CACHE_COUNT ; i++){
        if(page_caches[i]){
            if(page_caches[i]->buf){
                free(page_caches[i]->buf);
            }
            free(page_caches[i]);
            page_caches[i] = NULL;
        }
    }

    /* 开始干活,先生成当前页的内容 */
    /* 因为填充内容需要知道各区域的分布情况，所以需要现在页面内存上填充内容，再复制到缓存中 */
    ret = fill_main_area(start_file_index);
    if(ret){
        DP_ERR("%s:generate_one_page_cache failed!\n",__func__);
        return ret;
    }
    flush_page_region(&regions[REGION_MAIN],display);

    /* 保存缓存 */
    /* 分配内存 */
    if(NULL == (*cur_page_cache = malloc(sizeof(struct pixel_data)))){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    **cur_page_cache = *regions[REGION_MAIN].pixel_data;
    (*cur_page_cache)->in_rows = 0;
    (*cur_page_cache)->rows_buf = 0;
    if(NULL == ((*cur_page_cache)->buf = malloc((*cur_page_cache)->total_bytes))){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    copy_pixel_data(*cur_page_cache,regions[REGION_MAIN].pixel_data);

    /* 生成其他两页的内容 */
    /* 后面一页,此时 start_file_index 应为 0 */
    start_index = start_file_index + files_per_page;
    if(start_index < cur_dirent_nums){
        /* 在页面内存上生成下一页的内容，注意，此时生成的内容不能显示到屏幕上 */
        ret = fill_main_area(start_index);
        if(ret){
            DP_ERR("%s:generate_one_page_cache failed!\n",__func__);
            return ret;
        }
        if(NULL == ((*(cur_page_cache + 1)) = malloc(sizeof(struct pixel_data)))){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        *(*(cur_page_cache + 1)) = *regions[REGION_MAIN].pixel_data;
        (*(cur_page_cache + 1))->in_rows = 0;
        (*(cur_page_cache + 1))->rows_buf = 0;
        if(NULL == ((*(cur_page_cache + 1))->buf = malloc((*(cur_page_cache + 1))->total_bytes))){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        copy_pixel_data((*(cur_page_cache + 1)),regions[REGION_MAIN].pixel_data);
    }

    /* 前面一页 */
    if(total_pages > 2){
        start_index = (total_pages - 1) * files_per_page;
        
        /* 在页面内存上生成下一页的内容，注意，此时生成的内容不能显示到屏幕上 */
        ret = fill_main_area(start_index);
        if(ret){
            DP_ERR("%s:generate_one_page_cache failed!\n",__func__);
            return ret;
        }
        if(NULL == ((*(cur_page_cache - 1)) = malloc(sizeof(struct pixel_data)))){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        *(*(cur_page_cache - 1)) = *regions[REGION_MAIN].pixel_data;
        (*(cur_page_cache - 1))->in_rows = 0;
        (*(cur_page_cache - 1))->rows_buf = 0;
        if(NULL == ((*(cur_page_cache - 1))->buf = malloc((*(cur_page_cache - 1))->total_bytes))){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        copy_pixel_data((*(cur_page_cache - 1)),regions[REGION_MAIN].pixel_data);
    }

    /* 恢复当前页面缓存 */
    copy_pixel_data(regions[REGION_MAIN].pixel_data,*cur_page_cache);
    return 0;
}

/* 调用这个函数,几乎会释放该页面占用的所有资源 */
static void browse_page_exit(void)
{
    /* 释放占用的内存 */
    if(browse_page.allocated && !browse_page.share_fbmem){
        free(browse_page.page_mem.buf);
    }

    /* 删除区域的内存映射 */
    unmap_regions_to_page_mem(&browse_page);

    /* 删除图标数据 */
    destroy_icon_pixel_datas(&browse_page,icon_pixel_datas,ICON_NUMS);

    /* 删除目录信息 */
    if(cur_dir_contents){
        free_dir_contents(cur_dir_contents,cur_dirent_nums);
        dir_contents_generated = 0;
        start_file_index = 0;
        cur_dirent_nums = 0;
        if(cur_dir){
            free(cur_dir);
        }
        cur_dir_contents = NULL;
    }

    /* 删除页面缓存 */
    destroy_page_caches();
    
    /* 删除预览图缓存 */
    destroy_preview_caches();
}

/* 点击"调整图标"菜单时的回调函数 */
static int adjust_icon_menu_cb_func(void)
{
    
    return 0;
}

/* 点击返回菜单时的回调函数,返回1时表示返回到主页面 */
static int return_menu_cb_func(void)
{   
    int ret;
    int n = strlen(cur_dir);
    char buf[n + 1];
    char *buf_start,*buf_end;
    struct display_struct *display = get_default_display();
    struct page_region *regions = browse_page.page_layout.regions;

    /* 如果当前为根目录,返回到主页面，并销毁数据 */
    if(!strcmp(cur_dir,"/") || !strcmp(cur_dir,"//")){
        free(cur_dir);
        browse_page_exit();
        return 1;
    }

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
    
    pthread_mutex_lock(&preview_thread_mutex);
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

    /* 释放预览图缓存 */
    destroy_preview_caches();

    /* 重新生成缓存 */
    generate_page_caches();

    pthread_cond_signal(&preview_thread_cond);
    pthread_mutex_unlock(&preview_thread_mutex);
    
    return 0;
}

/* 使能文件夹选择状态 */
static void enable_dir_select_status(struct page_struct *browse_page,bool enable)
{
    struct page_region *regions = browse_page->page_layout.regions;
    struct display_struct *display = get_default_display();
    int dir_select_region_base;
    int file_count;
    int i;

    if(!browse_page->already_layout){
        return ;
    }

    if((file_count = cur_dirent_nums - start_file_index) >= files_per_page){
        file_count = files_per_page;
    }

    dir_select_region_base = REGION_FILE_DIR_BASE + files_per_page * 4;
    if(enable){
        dir_select_status = 1;
        for(i = 0 ; i < file_count ; i++){printf("%s-%d\n",__func__,__LINE__);
            if(cur_dir_contents[start_file_index + i]->type == FILETYPE_DIR){
                regions[dir_select_region_base + i].invisible = 0;
                merge_pixel_data(regions[dir_select_region_base + i].pixel_data,&icon_pixel_datas[ICON_DIR_UNSELECTED]); 
            }
        }
    }else{
        dir_select_status = 0;
        for(i = 0 ; i < files_per_page ; i++){
            regions[dir_select_region_base + i].invisible = 1;
        }
        copy_pixel_data(regions[REGION_MAIN].pixel_data,*cur_page_cache);
    }

    flush_page_region(&regions[REGION_MAIN],display);
    fill_menu_icon_area(browse_page);
    flush_menu_icon_area(browse_page);
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
    *(unsigned long *)&auto_priv[max_selected_dir] = j;
    enable_dir_select_status(&browse_page,0);

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
    dir_index = start_file_index + region_index - REGION_FILE_DIR_BASE - files_per_page * 4;
    dir_name = malloc(strlen(cur_dir) + strlen(cur_dir_contents[dir_index]->name) + 2);
    if(!dir_name){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    sprintf(dir_name,"%s/%s",cur_dir,cur_dir_contents[dir_index]->name);
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
        if(selected_dir_num > max_selected_dir)    //超出上限，直接返回
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

/* @description : 实现上一页 、下一页菜单回调函数功能,不得不说，这是一个很关键的函数，重要的逻辑都在这里了
 * @param : next_page - 1 表示下一页，0 表示上一页 */
static int __pre_next_page(int next_page)
{
    int ret,i,j;
    int next_index,pre_index,release_index,new_index;
    int total_page = (cur_dirent_nums - 1) / files_per_page + 1;
    int cache_count = 0;
    int base_region_index = REGION_FILE_DIR_BASE;
    int offset_x,bytes_per_pixel;
    struct page_region *regions = browse_page.page_layout.regions;
    struct display_struct *display = get_default_display();
    struct pixel_data *temp;
    struct pixel_data temp_data;
    
    /* 将当前页面已生成的缩略图（如果有的话）复制到缓存中，使显示看上去更流畅些 */
    if(!(*cur_page_cache)->preview_completed){
        memset(&temp_data,0,sizeof(struct pixel_data));
        temp_data = *regions[base_region_index].pixel_data;
        temp_data.buf = 0;
        temp_data.rows_buf = malloc(temp_data.height * sizeof(unsigned char *));
        temp_data.in_rows = 1;
        offset_x = regions[REGION_MENU_NEXT_PAGE].width;
        bytes_per_pixel = temp_data.bpp / 8;
        for(i = 0 ; i < files_per_page ; i++){
            if((i + start_file_index) >= cur_dirent_nums)
                break;
            printf("%s-%d-cur_dir_contents[i + start_file_index]->preview_cached:%d\n",__func__,__LINE__,cur_dir_contents[i + start_file_index]->preview_cached);
            if(cur_dir_contents[i + start_file_index]->file_type >= FILETYPE_FILE_BMP && \
               cur_dir_contents[i + start_file_index]->file_type <= FILETYPE_FILE_GIF && \
               cur_dir_contents[i + start_file_index]->preview_cached){
                printf("%s-%d\n",__func__,__LINE__);
                for(j = 0 ; j < temp_data.height ; j++){
                    temp_data.rows_buf[j] = (*cur_page_cache)->buf + (*cur_page_cache)->line_bytes * (regions[base_region_index + 3 * i].y_pos + j) + \
                    (regions[base_region_index + 3 * i].x_pos - offset_x) * bytes_per_pixel;
                }
                ret = copy_pixel_data(&temp_data,regions[base_region_index + 3 * i].pixel_data);
                if(ret){
                    DP_WARNING("%s:copy_pixel_data failed!\n",__func__);
                }
            }
        }
        /* 检查当前页的预览图是否全部生成完毕，并置相应的标志位 */
        for(i = 0 ; i < files_per_page ; i++){
            if((i + start_file_index) >= cur_dirent_nums)
                break;
            
            if(cur_dir_contents[i + start_file_index]->file_type >= FILETYPE_FILE_BMP && \
               cur_dir_contents[i + start_file_index]->file_type <= FILETYPE_FILE_GIF && \
               !cur_dir_contents[i + start_file_index]->preview_cached){
                break;
            }
        }
        if(i == files_per_page || (i + start_file_index) == cur_dirent_nums){
            (*cur_page_cache)->preview_completed = 1;
        }
        if(temp_data.rows_buf){
            free(temp_data.rows_buf);
        }
    }
    
    /* 确定新的起始文件索引 */
    pthread_mutex_lock(&preview_thread_mutex);
    if(next_page){
        if((start_file_index + files_per_page) >= cur_dirent_nums){
            start_file_index = 0;
        }else{
            start_file_index += files_per_page;
        }
    }else{
        if(0 == start_file_index){       //当前显示的第一个文件,没有前一页了,跳到最后一页
            start_file_index = ((cur_dirent_nums - 1) / files_per_page) * files_per_page;
        }else if((start_file_index -= files_per_page) < 0){
            start_file_index = 0;
        }
    }
    
    /* 将当前页面内存更新到缓存中,并看情况生成新的缓存 */
    if(total_page <= PAGE_CACHE_COUNT){
        /* 总页数小于等于缓存页数时，当缓存全部读入后就无需释放或读入新的缓存了，只需将相应页面的缓存刷新到屏幕上就可以了 */
        do{
            if(next_page){
                temp = page_caches[0];
                for(i = 0 ; i < PAGE_CACHE_COUNT - 1 ; i++){
                    page_caches[i] = page_caches[i + 1];
                }
                page_caches[PAGE_CACHE_COUNT - 1] = temp;
            }else{
                temp = page_caches[PAGE_CACHE_COUNT - 1];
                for(i = PAGE_CACHE_COUNT - 1 ; i > 0 ; i--){
                    page_caches[i] = page_caches[i - 1];
                }
                page_caches[0] = temp;
            }
        }while(!(*cur_page_cache));
        
        /* 将更改后的内容刷新至显存 */
        copy_pixel_data(regions[REGION_MAIN].pixel_data,*cur_page_cache);
        flush_page_region(&regions[REGION_MAIN],display);
        
        pthread_cond_signal(&preview_thread_cond);
        pthread_mutex_unlock(&preview_thread_mutex);

        /* 判断是否要加载下一页的缓存 */
        cache_count = 0;
        for(i = 0 ; i < PAGE_CACHE_COUNT ; i++){
            if(page_caches[i])
                cache_count++;
        }
        /* 如果缓存数小于总页数，说明还要继续加载内存 */
        if(!(*(cur_page_cache + 1)) && cache_count != total_page){
            temp = malloc(sizeof(struct pixel_data));
            memset(temp,0,sizeof(struct pixel_data));
            *temp = **cur_page_cache;
            if(NULL == (temp->buf = malloc(temp->total_bytes))){
                DP_ERR("%s:malloc failed!\n",__func__);
                return -ENOMEM;
            }
            if(next_page){
                if((next_index = start_file_index + files_per_page) >= cur_dirent_nums){
                    next_index = 0;
                }
                ret = fill_main_area(next_index);
            }else{
                if((pre_index = start_file_index - files_per_page) < 0){
                    pre_index = ((cur_dirent_nums - 1) / files_per_page) * files_per_page;
                }
                ret = fill_main_area(pre_index);
            }
            if(ret){
                DP_ERR("%s:fill_main_area failed!\n",__func__);
                return ret;
            }

            /* 保存缓存 */
            copy_pixel_data(temp,regions[REGION_MAIN].pixel_data);
            copy_pixel_data(regions[REGION_MAIN].pixel_data,*cur_page_cache);
            *(cur_page_cache + 1) = temp;
        }
    }else{
        if(next_page){
            temp = page_caches[0];
            for(i = 0 ; i < PAGE_CACHE_COUNT - 1 ; i++){
                page_caches[i] = page_caches[i + 1];
            }
            page_caches[i] = NULL;
        }else{
            temp = page_caches[PAGE_CACHE_COUNT - 1];
            for(i = PAGE_CACHE_COUNT - 1 ; i > 0 ; i--){
                page_caches[i] = page_caches[i - 1];
            }
            page_caches[i] = NULL;
        }
        
        /* 将更改后的内容刷新至显存 */
        copy_pixel_data(regions[REGION_MAIN].pixel_data,*cur_page_cache);
        flush_page_region(&regions[REGION_MAIN],display);

        pthread_cond_signal(&preview_thread_cond);
        pthread_mutex_unlock(&preview_thread_mutex);

        /* 读取最新一页的缓存 */
        new_index = start_file_index;
        if(next_page){
            for(i = (PAGE_CACHE_COUNT / 2 + 1) ; i < PAGE_CACHE_COUNT ; i++){
                if(!page_caches[i]){
                    cache_count = i - (PAGE_CACHE_COUNT / 2);
                    break;
                }      
            }
            for(i = 0 ; i < cache_count ; i++){
                if((new_index += files_per_page) >= cur_dirent_nums){
                    new_index = 0;
                }
            }
        }else{
            for(i = (PAGE_CACHE_COUNT / 2 - 1) ; i >= 0 ; i--){
                if(!page_caches[i]){
                    cache_count = (PAGE_CACHE_COUNT / 2) - i;
                    break;
                }     
            }
            for(i = 0 ; i < cache_count ; i++){
                if((new_index -= files_per_page) < 0){
                    new_index = ((cur_dirent_nums - 1) / files_per_page) * files_per_page;
                }
            }
        }
        
        /* 设置要释放的缓存对应的目录项中的标志位 */
        if(temp){
            release_index = start_file_index;
            if(next_page){
                for(i = 0 ; i < (PAGE_CACHE_COUNT / 2) + 1 ; i++){
                    if((release_index -= files_per_page) < 0){
                        release_index = ((cur_dirent_nums - 1) / files_per_page) * files_per_page;
                    }
                }
            }else{
                for(i = 0 ; i < (PAGE_CACHE_COUNT / 2) + 1 ; i++){
                    if((release_index += files_per_page) >= cur_dirent_nums){
                        release_index = 0;
                    }
                }
            }
            printf("%s-%d-release_index:%d\n",__func__,__LINE__,release_index);
            for(i = 0 ; i < files_per_page ; i++){
                if((i + release_index)>= cur_dirent_nums)
                    break;
                cur_dir_contents[i + release_index]->preview_cached = 0;
            }
        }
        
        pthread_mutex_lock(&preview_thread_mutex);
        ret = fill_main_area(new_index);
        if(ret){
            DP_ERR("%s:fill_main_area failed!\n",__func__);
            return ret;
        }
        
        /* 复用第一个缓存,如果存在的话 */
        if(temp){
            temp->preview_completed = 0;
            copy_pixel_data(temp,regions[REGION_MAIN].pixel_data);
            copy_pixel_data(regions[REGION_MAIN].pixel_data,*cur_page_cache);
        }else{
            temp = malloc(sizeof(struct pixel_data));
            memset(temp,0,sizeof(struct pixel_data));
            *temp = **cur_page_cache;
            temp->preview_completed = 0;
            if(NULL == (temp->buf = malloc(temp->total_bytes))){
                DP_ERR("%s:malloc failed!\n",__func__);
                return -ENOMEM;
            }
            copy_pixel_data(temp,regions[REGION_MAIN].pixel_data);
            copy_pixel_data(regions[REGION_MAIN].pixel_data,*cur_page_cache);
        }
        pthread_mutex_unlock(&preview_thread_mutex);

        if(next_page){
            page_caches[cache_count + PAGE_CACHE_COUNT / 2] = temp;
        }else{
            page_caches[PAGE_CACHE_COUNT / 2 - cache_count] = temp;
        }
        
    }
    return 0;
}

/* 点击上一页菜单时的回调函数 */
static int prepage_menu_cb_func(void)
{
   return __pre_next_page(0);
}

/* 点击下一页菜单时的回调函数 */
static int nextpage_menu_cb_func(void)
{
    return __pre_next_page(1);
}

/* 点击文件目录区域时的回调函数 */
static int file_dir_area_cb_func(int region_index,void *data)
{
    unsigned int selected_file_index = *(unsigned int *)data;
    struct page_region *regions = browse_page.page_layout.regions;
    struct display_struct *default_display = get_default_display();
    struct page_struct *next_page;
    char *file_name = NULL;
    int ret;
    struct page_param page_param;

    /* 打开目录 */
    if(FILETYPE_DIR == cur_dir_contents[selected_file_index]->type){
        char *temp;
        invert_region(regions[region_index].pixel_data);

        /* 修改当前目录及文件索引 */
        temp = malloc(strlen(cur_dir) + 2 + strlen(cur_dir_contents[selected_file_index]->name));
        if(!temp){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        if(!strcmp(cur_dir,"/")){
            sprintf(temp,"/%s",cur_dir_contents[selected_file_index]->name);
        }else{
            sprintf(temp,"%s/%s",cur_dir,cur_dir_contents[selected_file_index]->name);
        }

        pthread_mutex_lock(&preview_thread_mutex);
        if(cur_dir)
            free(cur_dir);
        cur_dir = temp;

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
        ret = generate_page_caches();
        if(ret){
            DP_ERR("%s:generate_page_caches failed!\n",__func__);
            return -1;
        }
        pthread_cond_signal(&preview_thread_cond);
        pthread_mutex_unlock(&preview_thread_mutex);
    }else if(FILETYPE_REG == cur_dir_contents[selected_file_index]->type){
        /* 打开文件 */
        switch (cur_dir_contents[selected_file_index]->file_type){
            /* 目前只支持两大类文件:图片和文本 */
            case FILETYPE_FILE_BMP:
            case FILETYPE_FILE_JPEG:
            case FILETYPE_FILE_PNG:
            case FILETYPE_FILE_GIF:
                /* 构造页面之间传递的参数，将目录信息传递过去，以免重新获取 */
                page_param.id = browse_page.id;
                page_param.private_data = malloc(sizeof(unsigned long) * 4);
                ((unsigned long *)page_param.private_data)[0] = (unsigned long)cur_dir_contents;
                ((unsigned long *)page_param.private_data)[1] = (unsigned long)cur_dirent_nums;
                ((unsigned long *)page_param.private_data)[2] = (unsigned long)selected_file_index;
                ((unsigned long *)page_param.private_data)[3] = (unsigned long)cur_dir;
                /* 获取page_struct */
                next_page = get_page_by_name("view_pic_page");
                if(!next_page){
                    DP_ERR("%s:get view_pic_page failed\n",__func__);
                    return -1;
                }
                printf("%s-%d-cur_dir_contents[selected_file_index].name:%s\n",__func__,__LINE__,cur_dir_contents[selected_file_index]->name);
                break;
            case FILETYPE_FILE_TXT:
                /* 构造文本文件名 */
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
                next_page = get_page_by_name("text_page");
                if(!next_page){
                    DP_ERR("%s:get view_pic_page failed\n",__func__);
                    return -1;
                }
                break;
            default:
                break;
        }
        pthread_mutex_lock(&preview_thread_mutex);
        preview_pause = 1;
        pthread_mutex_unlock(&preview_thread_mutex);

        next_page->run(&page_param);
        if(file_name)
            free(file_name);
        /* 应该重新渲染该页,似乎不用重新填充页面,直接将页面内存的数据输入显示屏缓存就可以了 */
        default_display->flush_buf(default_display,browse_page.page_mem.buf,browse_page.page_mem.total_bytes);
        pthread_mutex_lock(&preview_thread_mutex);
        preview_pause = 0;
        pthread_mutex_unlock(&preview_thread_mutex);
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
    unsigned int dir_select_region_base = REGION_FILE_DIR_BASE + 4 * files_per_page;
    unsigned int pre_page_id = pre_param->id;
    struct display_struct *default_display = get_default_display();
    struct page_region *regions = browse_page.page_layout.regions;
    struct page_struct *next_page;
    struct page_param page_param;
    DP_ERR("enter:%s\n",__func__);
    /* 为该页面分配一块内存 */
    if(!browse_page.allocated){
        /* 直接将页面对应的内存映射到显存上，省的多一道复制 */
        browse_page.page_mem.bpp         = default_display->bpp;
        browse_page.page_mem.width       = default_display->xres;
        browse_page.page_mem.height      = default_display->yres;
        browse_page.page_mem.line_bytes  = browse_page.page_mem.width * browse_page.page_mem.bpp / 8;
        browse_page.page_mem.total_bytes = browse_page.page_mem.line_bytes * browse_page.page_mem.height; 
        browse_page.page_mem.buf         = malloc(browse_page.page_mem.total_bytes);
        if(!browse_page.page_mem.buf){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        browse_page.allocated            = 1;
       
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
    
    /* 获取目录结构,注意,每到重新运行该run函数,总是从根目录开始显示 */
    if(cur_dir_contents){
        free_dir_contents(cur_dir_contents,cur_dirent_nums);
        dir_contents_generated = 0;
        start_file_index = 0;
        cur_dirent_nums = 0;
        if(cur_dir){
            free(cur_dir);
        }
        cur_dir = malloc(strlen(root_dir) + 1);
        if(!cur_dir){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        } 
        strcpy(cur_dir,root_dir);
    }
    /* 注意，此函数不会获取文件类型 */
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
    
    /* 准备各种图标数据 */
    if(!browse_page.icon_prepared){
        prepare_icon_pixel_datas(&browse_page,icon_pixel_datas,icon_file_names,icon_region_links,ICON_NUMS);
        if(ret){
            DP_ERR("%s:prepare_icon_pixel_data failed!\n",__func__);
            return -1;
        }
    }

    /* 获取页面缓存 */
    generate_page_caches();

    /* 填充各区域 */
    ret = browse_page_fill_layout(&browse_page);
    if(ret){
        DP_ERR("%s:browse_page_fill_layout failed!\n",__func__);
        return -1;
    }   

    flush_menu_icon_area(&browse_page);

    /* 创建预览图显示线程 */
    pthread_create(&preview_thread_id,NULL,preview_thread_func,NULL);

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
        if(region_index < 0 || REGION_MAIN_FILE_PATH == region_index || REGION_MAIN == region_index ||
           REGION_MAIN_FILE_PAGES == region_index || REGION_MAIN_FILE_DIR == region_index){
            if(!event.presssure && (-1 != pre_region_index)){
                press_region(&regions[pre_region_index],0,0);
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
                if((cur_dirent_nums - start_file_index) < files_per_page){
                    if((region_index - (REGION_FILE_DIR_BASE + 3 * files_per_page)) > (cur_dirent_nums - start_file_index))
                        continue;
                }
                
                /* 反转按下区域的颜色 */ 
                if(region_index < dir_select_region_base){
                    press_region(&regions[region_index],1,0);
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
                if(region_index >= REGION_FILE_DIR_BASE && region_index < (REGION_FILE_DIR_BASE + 4 * files_per_page)){
                    selected_file_index = (region_index - REGION_FILE_DIR_BASE - 3 * files_per_page) + start_file_index;
                    printf("%s-%d-selected_file_index:%d\n",__func__,__LINE__,selected_file_index);
                    if(selected_file_index >= cur_dirent_nums){
                        continue;
                    }
                }

                switch (region_index){
                    case REGION_MENU_RETURN:              /* return */
                        ret = return_menu_cb_func();
                        if(ret)
                            return 0;
                        break;
                    case REGION_MENU_ADJUST_ICON:         /* adjust_icon/save_autoplay_dir */
                        if(dir_select_status){
                            save_autoplay_dir_menu_cb_func();
                            break;
                        }else{
                            return_menu_cb_func();
                        }       
                        break;
                    case REGION_MENU_SELECT:             /* select/start_autoplay */
                        if(dir_select_status){
                            ret = start_autoplay_menu_cb_func(pre_page_id);
                            /* ret 为0不用跳转；为1要跳转，且前一个页面是主页面，直接调用run函数；
                             * 为1要跳转，且前一个页面正是连播页面 ，直接返回； */
                            if(ret == 1){
                                next_page = get_page_by_name("autoplay_page");
                                page_param.id = browse_page.id;
                                next_page->run(&page_param);
                            }else if(ret == 2){
                                return 0;
                            }
                            break;
                        }else{
                            select_menu_cb_func();
                            break;
                        }
                        break;
                    case REGION_MENU_PRE_PAGE:             /* pre_page */
                        prepage_menu_cb_func();
                        break;
                    case REGION_MENU_NEXT_PAGE:             /* next_page */
                        nextpage_menu_cb_func();
                        break;
                    case REGION_MAIN_FILE_PATH:
                    case REGION_MAIN_FILE_PAGES:            /* 路径名显示区域和页数信息区域暂不响应 */
                        continue;
                    case REGION_MAIN_FILE_DIR:
                        continue;
                    default:            
                        if(region_index >= dir_select_region_base){
                            /* 文件夹选择区域 */
                            select_dir_cb_func(region_index);
                            break;
                        }else{          
                            /* 文件区域 */
                            file_dir_area_cb_func(region_index,&selected_file_index);
                        }
                        break;
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