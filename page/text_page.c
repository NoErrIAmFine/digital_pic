#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "list.h"
#include "page_manager.h"
#include "debug_manager.h"
#include "picfmt_manager.h"
#include "pic_operation.h"
#include "input_manager.h"
#include "font_decoder.h"
#include "render.h"

static struct page_struct text_page;

/* 用于表示区域信息的枚举 */
enum region_info{
    REGION_MAIN_TEXT,               /* 显示文本的主体区域 */
    REGION_BOTTOM_INFO,             /* 底部信息区域，显示进度、时间、电量等 */
    REGION_MENU_TOTAL,              /* 菜单底部总体区域 */
    REGION_MENU_CATALOG,            /* 目录菜单 */
    REGION_MENU_CATALOG_ICON,       /* 目录菜单对应的图标 */
    REGION_MENU_CATALOG_TEXT,       /* 目录菜单对应的文字 */
    REGION_MENU_PROGRESS,           /* 进度菜单 */
    REGION_MENU_PROGRESS_ICON,      /* 进度菜单对应的图标 */
    REGION_MENU_PROGRESS_TEXT,      /* 进度菜单对应的文字 */
    REGION_MENU_FORMAT,             /* 版式菜单 */
    REGION_MENU_FORMAT_ICON,        /* 版式菜单对应的图标 */
    REGION_MENU_FORMAT_TEXT,        /* 版式菜单对应的文字 */
    REGION_FORMAT_TOTAL,            /* 格式菜单总体区域 */
    REGION_FORMAT_SELECT1,          /* 格式菜单子菜单1选择区域,此处为"字体"子菜单 */
    REGION_FORMAT_SELECT2,          /* 格式菜单子菜单2选择区域,此处为"间距样式"子菜单 */
    REGION_FORMAT2_ROW1_TEXT1,      /* 格式菜单子菜单2第一行 */
    REGION_FORMAT2_ROW1_ICON1,
    REGION_FORMAT2_ROW1_SCALE, 
    REGION_FORMAT2_ROW1_ICON2,
    REGION_FORMAT2_ROW1_TEXT2,   
    REGION_FORMAT2_ROW2_TEXT1,      /* 格式菜单子菜单2第二行 */
    REGION_FORMAT2_ROW2_ICON1,
    REGION_FORMAT2_ROW2_SCALE, 
    REGION_FORMAT2_ROW2_ICON2,
    REGION_FORMAT2_ROW2_TEXT2,
    REGION_FORMAT2_ROW3_TEXT1,      /* 格式菜单子菜单2第三行 */
    REGION_FORMAT2_ROW3_ICON1,
    REGION_FORMAT2_ROW3_SCALE, 
    REGION_FORMAT2_ROW3_ICON2,
    REGION_FORMAT2_ROW3_TEXT2,
    REGION_FORMAT2_ROW4_TEXT1,      /* 格式菜单子菜单2第四行 */
    REGION_FORMAT2_ROW4_ICON1,
    REGION_FORMAT2_ROW4_SCALE, 
    REGION_FORMAT2_ROW4_ICON2,
    REGION_FORMAT2_ROW4_TEXT2,  
    REGION_NUMS,
};

/* 一些区域大小的宏定义 */
#define REGION_BOTTOM_INFO_HEIGHT       20
#define REGION_MENU_VERTICAL_PADDING    5
#define REGION_MENU_TEXT_HEIGHT         20
// #define REGION_MENU_TEXT_HEIGHT         20

/* 以下是本页面要用到的图标信息 */
enum icon_info{
    ICON_CATALOG = 0,
    ICON_PROGRESS,
    ICON_FORMAT,
    ICON_FORMAT_PLUS,
    ICON_FORMAT_SUB,
    ICON_NUMS,
};
/* 图标文件名字符串数组 */
static const char *icon_file_names[] = {
    [ICON_CATALOG]      = "text_catalog.png",
    [ICON_PROGRESS]     = "text_progress.png",
    [ICON_FORMAT]       = "text_format.png",
    [ICON_FORMAT_PLUS]  = "text_plus.png",
    [ICON_FORMAT_SUB]   = "text_sub.png",
};
/* 图标对应的区域，数组下标表示图标编号，下标对应的数组项表示该图标对应的区域 */
static const int icon_region_links[] = {
    [ICON_CATALOG]      = REGION_MENU_CATALOG_ICON,
    [ICON_PROGRESS]     = REGION_MENU_PROGRESS_ICON,
    [ICON_FORMAT]       = REGION_MENU_FORMAT_ICON,
    [ICON_FORMAT_PLUS]  = REGION_FORMAT2_ROW1_ICON2,
    [ICON_FORMAT_SUB]   = REGION_FORMAT2_ROW1_ICON1,
};
static struct pixel_data icon_pixel_datas[ICON_NUMS];

/* 一些描述文字版式的全局变量 */
#define MAX_VPADDING            30
#define MAX_HPADDING            30
#define MAX_LINE_SPACING        30
#define MAX_SEGMENT_SPACING     30
static int font_size    = 24;
static int font_color;
static char *font_file;
static int vertical_padding     = 20;
static int min_text_width       = 200;
static int horizontal_padding   = 20;
static int min_text_height      = 200;
static int line_spacing         = 0;
static int segment_spacing      = 0;

/* 当前正查看的文件相关信息 */
/* 用于追踪每个页面所对应的在文件中的位置，这样在点上一页和下一页时才知道显示哪些内容；
 * 比如点击前一页，要往前显示多少内容才刚好能填满前一页呢，这不提前记录是不容易知道的;
 * 如果点击目录跳转的更复杂，我现在想不通 */
struct page_pos
{
    int index;                              /* 当前项对应的索引 */
    const unsigned char *start_pos;         /* 当前页对应的文件起始位置 */   
    const unsigned char *end_pos;           /* 当前页对应的文件结束位置 */  
    struct list_head list;                  /* 从内核中扣出来的一个链表结构 */
}; 
static unsigned char *cur_file;             /* 当前文件名 */
static const unsigned char *cur_file_pos;   /* 当前显示页面的第一个字符对应在文件中的位置 */
static const unsigned char *file_start;     /* 文件起始指针（不含文件头） */
static const unsigned char *file_end;       /* 文件结束指针 */
static int file_size;                       /* 文件大小（不含文件头） */
static bool file_info_generated = 0;        /* 是否已获取到文件信息 */
static struct font_decoder *cur_decoder;    /* 当前用的字符解码器 */
#define CACHE_COUNT 5
static struct pixel_data *text_caches[CACHE_COUNT];/* 缓存5个页面，前后各两张 */
/* 当前正被显示的缓存 */
static struct pixel_data **const cur_text_cache = &text_caches[2]; 
static bool text_caches_generated = 0;
/* 用于追踪文件位置 */
static struct list_head page_pos_head = LIST_HEAD_INIT(page_pos_head);
static struct page_pos *cur_page_pos;

static bool menu_show_status = 0;           /* 说明当前正在展示菜单 */
static int format_select_status = 0;        /* 表示当前选中了格式菜单的哪个子菜单，取值为1和2 */

static int prepare_icon_pixel_datas(struct page_struct *text_page)
{
    int i,ret;
    struct page_region *regions = text_page->page_layout.regions;
    struct pixel_data temp;

    if(text_page->icon_prepared){
        return 0;
    }

    /* 获取初始数据 */
    ret = get_icon_pixel_datas(icon_pixel_datas,icon_file_names,ICON_NUMS);
    if(ret){
        DP_ERR("%s:get_icon_pixel_datas failed\n",__func__);
        return ret;
    }

    /* 缩放至合适大小 */
    for(i = 0 ; i < ICON_NUMS ; i++){
        memset(&temp,0,sizeof(struct pixel_data));
        temp.width  = regions[icon_region_links[i]].width;
        temp.height = regions[icon_region_links[i]].height;
        ret = pic_zoom_with_same_bpp(&temp,&icon_pixel_datas[i]);
        if(ret){
            DP_ERR("%s:pic_zoom_with_same_bpp failed\n",__func__);
            return ret;
        }
        free(icon_pixel_datas[i].buf);
        icon_pixel_datas[i] = temp;
    }

    text_page->icon_prepared = 1;
    return 0;
}

/* 计算格式菜单的布局 */
static int calc_format_menu_layout(struct page_struct *text_page)
{
    struct display_struct *default_display = get_default_display();
    struct page_region *regions = text_page->page_layout.regions;
    unsigned int height1,height2;   /* 各行的高度 */
    unsigned int x_cursor,y_cursor;
    unsigned int unit_distance;
    unsigned int width = default_display->xres;
    unsigned int height = default_display->yres;
    int i;

    height2 = regions[REGION_MENU_TOTAL].height * 4 / 5;
    height1 = height2 * 4 / 5;
    x_cursor = 0;
    y_cursor = height - regions[REGION_MENU_TOTAL].height - height1 - height2 * 4;

    /* "格式菜单"总体 */
    regions[REGION_FORMAT_TOTAL].x_pos = x_cursor;
    regions[REGION_FORMAT_TOTAL].y_pos = y_cursor;
    regions[REGION_FORMAT_TOTAL].width = width;
    regions[REGION_FORMAT_TOTAL].height = height1 + height2 * 4;
    regions[REGION_FORMAT_TOTAL].invisible = 1;

    /* 两个选择区域,点击该区域选择相应的子菜单 */
    regions[REGION_FORMAT_SELECT1].x_pos = x_cursor;
    regions[REGION_FORMAT_SELECT1].y_pos = y_cursor;
    regions[REGION_FORMAT_SELECT1].width = width / 2;
    regions[REGION_FORMAT_SELECT1].height = height1;
    regions[REGION_FORMAT_SELECT1].invisible = 1;

    regions[REGION_FORMAT_SELECT2].x_pos = x_cursor + width / 2;
    regions[REGION_FORMAT_SELECT2].y_pos = y_cursor;
    regions[REGION_FORMAT_SELECT2].width = width / 2;
    regions[REGION_FORMAT_SELECT2].height = height1;
    regions[REGION_FORMAT_SELECT2].invisible = 1;

    /* 子菜单"间距样式" */
    unit_distance = width / 24;
    y_cursor += height1;
    for(i = 0 ; i < 4 ; i++){
        regions[REGION_FORMAT2_ROW1_TEXT1 + i * 5].x_pos = x_cursor + unit_distance / 2;
        regions[REGION_FORMAT2_ROW1_TEXT1 + i * 5].y_pos = y_cursor + 5;
        regions[REGION_FORMAT2_ROW1_TEXT1 + i * 5].width = 4 * unit_distance;
        regions[REGION_FORMAT2_ROW1_TEXT1 + i * 5].height = height2 - 10;
        regions[REGION_FORMAT2_ROW1_TEXT1 + i * 5].invisible = 1;

        regions[REGION_FORMAT2_ROW1_ICON1 + i * 5].x_pos = x_cursor + unit_distance * 5;
        regions[REGION_FORMAT2_ROW1_ICON1 + i * 5].y_pos = y_cursor;
        regions[REGION_FORMAT2_ROW1_ICON1 + i * 5].width = ((2 * unit_distance) < height2) ? 2 * unit_distance : height2;
        regions[REGION_FORMAT2_ROW1_ICON1 + i * 5].height = regions[REGION_FORMAT2_ROW1_ICON1 + i * 5].width;
        regions[REGION_FORMAT2_ROW1_ICON1 + i * 5].invisible = 1;

        regions[REGION_FORMAT2_ROW1_SCALE + i * 5].x_pos = x_cursor + unit_distance * 7;
        regions[REGION_FORMAT2_ROW1_SCALE + i * 5].y_pos = y_cursor;
        regions[REGION_FORMAT2_ROW1_SCALE + i * 5].width = 12 * unit_distance;
        regions[REGION_FORMAT2_ROW1_SCALE + i * 5].height = height2;
        regions[REGION_FORMAT2_ROW1_SCALE + i * 5].invisible = 1;

        regions[REGION_FORMAT2_ROW1_ICON2 + i * 5].x_pos = x_cursor + unit_distance * 19;
        regions[REGION_FORMAT2_ROW1_ICON2 + i * 5].y_pos = y_cursor;
        regions[REGION_FORMAT2_ROW1_ICON2 + i * 5].width = ((2 * unit_distance) < height2) ? 2 * unit_distance : height2;
        regions[REGION_FORMAT2_ROW1_ICON2 + i * 5].height = regions[REGION_FORMAT2_ROW1_ICON2 + i * 5].width;
        regions[REGION_FORMAT2_ROW1_ICON2 + i * 5].invisible = 1;
 
        regions[REGION_FORMAT2_ROW1_TEXT2 + i * 5].x_pos = x_cursor + unit_distance * 22;
        regions[REGION_FORMAT2_ROW1_TEXT2 + i * 5].y_pos = y_cursor + 5;
        regions[REGION_FORMAT2_ROW1_TEXT2 + i * 5].width = unit_distance;
        regions[REGION_FORMAT2_ROW1_TEXT2 + i * 5].height = height2 - 10;
        regions[REGION_FORMAT2_ROW1_TEXT2 + i * 5].invisible = 1;
        y_cursor += height2;
    }

    /* 子菜单1 */
    //to_do ..
    return 0;
}

static int calc_catalog_menu_layout(struct page_struct *text_page)
{
    return 0;
}

static int calc_progress_menu_layout(struct page_struct *text_page)
{
    return 0;
}

/* 在此函数中将会计算好页面的布局情况 */
static int text_page_init(void)
{
    struct display_struct *default_display = get_default_display();
    struct page_layout *page_layout = &text_page.page_layout;
    struct page_region *regions = page_layout->regions;
    int width = default_display->xres;
    int height = default_display->yres;
    int unit_distance,x_cursor,y_cursor,x_delta;
    int i;

    if(text_page.already_layout)
        return 0;

    page_layout->width  = width;
    page_layout->height = height;
    text_page.page_mem.bpp     = default_display->bpp;
    text_page.page_mem.width   = width;
    text_page.page_mem.height  = height;
    text_page.page_mem.line_bytes  = text_page.page_mem.width * text_page.page_mem.bpp / 8;
    text_page.page_mem.total_bytes = text_page.page_mem.line_bytes * text_page.page_mem.height;

    /* 此页面的region结构体为静态分配 */
    /*   ----------------------
	 *    
	 *    
	 *          主体文字
	 *    
	 *    
	 *    目录      进度        版式
	 *          底部信息
	 *    ----------------------
	 */
    if(height > width){
        unit_distance = width / 8;
    }else{
        unit_distance = height / 8;
    }
    /* 主体文字 */
    regions[REGION_MAIN_TEXT].x_pos = 0;
    regions[REGION_MAIN_TEXT].y_pos = 0;
    regions[REGION_MAIN_TEXT].width = width;
    regions[REGION_MAIN_TEXT].height = height - REGION_BOTTOM_INFO_HEIGHT;
    /* 底部信息 */
    regions[REGION_BOTTOM_INFO].x_pos = 0;
    regions[REGION_BOTTOM_INFO].y_pos = height - REGION_BOTTOM_INFO_HEIGHT;
    regions[REGION_BOTTOM_INFO].width = width;
    regions[REGION_BOTTOM_INFO].height = REGION_BOTTOM_INFO_HEIGHT;

    /* 定义文件图标以及文件名称字符串的大小,图标是一个正方体, "图标+名字"也是一个正方体
     *   --------
     *   |  图  |
     *   |  标  |
     * ------------
     * |   文字   |
     * ------------
     */
    /* 菜单总体区域 */
    regions[REGION_MENU_TOTAL].x_pos = 0;
    regions[REGION_MENU_TOTAL].y_pos = height - unit_distance - 2 * REGION_MENU_VERTICAL_PADDING;
    regions[REGION_MENU_TOTAL].width = width;
    regions[REGION_MENU_TOTAL].height = unit_distance + 2 * REGION_MENU_VERTICAL_PADDING;

    y_cursor = regions[REGION_MENU_TOTAL].y_pos + REGION_MENU_VERTICAL_PADDING;
    x_delta = (width - 3 * unit_distance) / 6;
    x_cursor = x_delta;
    
    /* 挨个填充三个菜单区域 */
    for(i = 0 ; i < 3 ; i++){
        regions[REGION_MENU_CATALOG + 3 * i].x_pos = x_cursor + i * (2 * x_delta + unit_distance);
        regions[REGION_MENU_CATALOG + 3 * i].y_pos = y_cursor;
        regions[REGION_MENU_CATALOG + 3 * i].width = unit_distance;
        regions[REGION_MENU_CATALOG + 3 * i].height = unit_distance;
        regions[REGION_MENU_CATALOG + 3 * i].invisible = 1;

        regions[REGION_MENU_CATALOG_ICON + 3 * i].x_pos = x_cursor + (REGION_MENU_TEXT_HEIGHT / 2) + i * (2 * x_delta + unit_distance);
        regions[REGION_MENU_CATALOG_ICON + 3 * i].y_pos = y_cursor;
        regions[REGION_MENU_CATALOG_ICON + 3 * i].width = unit_distance - REGION_MENU_TEXT_HEIGHT;
        regions[REGION_MENU_CATALOG_ICON + 3 * i].height = unit_distance - REGION_MENU_TEXT_HEIGHT;
        regions[REGION_MENU_CATALOG_ICON + 3 * i].invisible = 1;

        regions[REGION_MENU_CATALOG_TEXT + 3 * i].x_pos = x_cursor + i * (2 * x_delta + unit_distance);
        regions[REGION_MENU_CATALOG_TEXT + 3 * i].y_pos = y_cursor + (unit_distance - REGION_MENU_TEXT_HEIGHT);
        regions[REGION_MENU_CATALOG_TEXT + 3 * i].width = unit_distance;
        regions[REGION_MENU_CATALOG_TEXT + 3 * i].height = REGION_MENU_TEXT_HEIGHT;
        regions[REGION_MENU_CATALOG_TEXT + 3 * i].invisible = 1;
    }
    
    /* 计算"版式"菜单布局 */
    calc_format_menu_layout(&text_page);
    /* 计算"目录"菜单布局 */
    calc_catalog_menu_layout(&text_page);
    /* 计算"进度"菜单布局 */
    calc_progress_menu_layout(&text_page);

    text_page.already_layout = 1;
    return 0;
}

static void text_page_exit(void)
{
    return ;
}

/* 解析要显示的文本文件，获取相应的文件信息，获取对应的解码器，将文本文件内容映射至用户空间 */
static int parse_text_file(const char *file_name)
{
    int fd;
    int ret;
    unsigned char *file_buf;
    struct stat stat;
    printf("file_name:%p\n",file_name);
    if((fd = open(file_name,O_RDONLY)) < 0){
        perror("open text file failed!\n");
        return errno;
    }
    
    if((ret = fstat(fd,&stat)) < 0){
        perror("fstat failed!\n");
        return errno;
    }
    
    /* 因为目前采用的方法是将整个文本文件全部读进来，所以文件不能太大，不能大于20m */
    if(stat.st_size > (20 * 1024 * 1024)){
        DP_WARNING("text file is too big!\n");
        return -E2BIG;
    }
    
    /* 映射文件 */
    file_buf = mmap(NULL,stat.st_size,PROT_READ,MAP_SHARED,fd,0);
    if(file_buf == MAP_FAILED){
        perror("mmap failed!\n");
        return errno;
    }
    
    /* 获取解码器 */
    cur_decoder = get_font_decoder_for_file(file_name);
    if(!cur_decoder){
        DP_ERR("%s:get font decoder failed!\n",__func__);
        return -1;
    };
    
    /* 获取当前类型文件信息头长度，使文件指针偏移到正式的内容上,当然不是每种文件都有这个 */
    if(cur_decoder->get_header_len){
        ret = cur_decoder->get_header_len((char *)file_buf);
    }
    printf("coder name :%s\n",cur_decoder->name);
    
    /* 将当前文件信息记录到全局变量中 */
    file_size = stat.st_size;
    file_end = file_buf + file_size;
    file_buf += ret;
    cur_file_pos = file_buf;
    file_start = file_buf;
    file_info_generated = 1;

    return 0;
}

/* 设置当前字体解码器对应的字体大小和字体文件 */
static int set_font_size_and_file(struct font_decoder *decoder,int font_size,const char *font_file)
{
    int ret;
    if(decoder->set_font_size)
        ret = decoder->set_font_size(font_size);
    if(decoder->set_font_file)
        ret|= decoder->set_font_file(font_file);
    return ret;
}

/* 填充一页文本,返已读取的字节数 */
static int fill_text_one_page(struct pixel_data *pixel_data,const char *file_buf,int len,struct font_decoder *decoder)
{
    int ret;
    int read_len;
    unsigned int code;
    int total_len = 0;
    int i,x,y;
    struct pixel_data temp;

    struct display_struct *display = get_default_display();
    
    /* 设置当前字体解码器对应的字体大小和字体文件 */
    ret = set_font_size_and_file(decoder,font_size,font_file);
    if(ret){
        DP_ERR("%s:set_font_size_and_file failed!\n",__func__);
        return ret;
    }

    /* 初始化这个临时变量 */
    memset(&temp,0,sizeof(struct pixel_data));
    temp.bpp = pixel_data->bpp;
    temp.width = pixel_data->width - (vertical_padding * 2);
    temp.line_bytes = temp.bpp * temp.width;
    /* 如何确定行宽和字体高度的关系？？？ */
    temp.height = font_size + 2;
    temp.rows_buf  = malloc(temp.height * sizeof(unsigned char *));
    if(!temp.rows_buf){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    temp.in_rows = 1;
    x = horizontal_padding;
    y = vertical_padding;

    while(len > 0){
        for(i = 0 ; i < temp.height ; i++){
            temp.rows_buf[i] = pixel_data->buf + (y + i) * pixel_data->line_bytes + x * temp.bpp / 8;
        }
        read_len = fill_text_one_line(&temp,file_buf,len,decoder);
        if(read_len < 0){
            DP_ERR("%s:fill_text_one_line failed!\n",__func__);
            return read_len;
        }
        // merge_pixel_data_in_center(&text_page.page_mem,&temp);
        file_buf += read_len;
        len -= read_len;
        total_len += read_len;
        /* 确定下一行的位置 */
        y += (temp.height + line_spacing);
        read_len = cur_decoder->get_code_from_buf(file_buf,len,&code);
        if(code == '\n'){
            y += segment_spacing;
            file_buf += read_len;
            len -= read_len;
            total_len += read_len;
            read_len = cur_decoder->get_code_from_buf(file_buf,len,&code);
            if(code == '\r'){
                file_buf += read_len;
                len -= read_len;
                total_len += read_len;
            }
        }
        if((y + temp.height) > (pixel_data->height - vertical_padding)){
            /* 已经到页面底部，直接退出 */
            return total_len;
        }
    }
    return total_len;
}

/* @description ： 为当前页面生成缓存,为了逻辑清晰点，此函数相当于初始化函数，调用此函数会使缓存处于初始状态 */
static int generate_text_caches(void)
{
    int i;
    int ret,read_len;
    int file_remain_size;
    const unsigned char *file_pos;
    struct page_pos *page_pos,*temp_pos;
    struct page_region *regions = text_page.page_layout.regions;

    /* 检查此函数的执行条件 */
    if(!cur_file || !file_info_generated || text_caches_generated){
        DP_ERR("%s:has no file!\n",__func__);
        return -1;
    }
    
    /* 如果有资源，全部释放,也许是多余的，但我总不放心 */
    for(i = 0 ; i < CACHE_COUNT ; i++){
        if(text_caches[i]){
            if(text_caches[i]->buf)
                free(text_caches[i]->buf);
            free(text_caches[i]);
            text_caches[i] = NULL;
        }      
    }
    
    /* 释放位置链表 */
    if(!list_empty(&page_pos_head)){
        temp_pos = NULL;
        list_for_each_entry(page_pos,&page_pos_head,list){
            if(temp_pos)
                free(temp_pos);
            temp_pos = page_pos;
        }
        page_pos_head.next = page_pos_head.prev = &page_pos_head;
    }
    
    /* 一切总算清静了，开始生成缓存 */
    file_pos = cur_file_pos;
    file_remain_size = file_size;
    for(i = 0 ; i < 3 ; i++){
        /* 分配资源 */
        text_caches[i + 2] = malloc(sizeof(struct pixel_data));
        page_pos = malloc(sizeof(struct page_pos));
        if(!text_caches[i + 2] || !page_pos){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        *text_caches[i + 2] = *regions[REGION_MAIN_TEXT].pixel_data;
        text_caches[i + 2]->rows_buf = 0;
        text_caches[i + 2]->in_rows = 0;
        text_caches[i + 2]->buf = malloc(text_caches[i + 2]->total_bytes);
        if(!text_caches[i + 2]->buf){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        clear_pixel_data(text_caches[i + 2],BACKGROUND_COLOR);

        read_len = fill_text_one_page(text_caches[i + 2],(char *)file_pos,file_remain_size,cur_decoder);
        if(read_len < 0){
            DP_ERR("%s:fill_text_one_page failed!\n",__func__);
            return read_len;
        }
        page_pos->index = i;
        page_pos->start_pos = file_pos;
        page_pos->end_pos = file_pos + read_len; 
        list_add_tail(&page_pos->list,&page_pos_head);
        file_remain_size -= read_len;
        file_pos += read_len;
    }
    cur_page_pos = list_entry(page_pos_head.next,struct page_pos,list);
    text_caches_generated = 1;
    return 0;
}

/* 此函数填充此页面布局内的内容 */
static int text_page_fill_layout(struct page_struct *text_page)
{
    int i,ret;
    int region_num = text_page->page_layout.region_num;
    struct page_region *regions = text_page->page_layout.regions;
    struct display_struct *default_display = get_default_display();
    
    if(!text_page->already_layout){
        text_page_init();
    }

    if(!text_page->allocated){
        /* 直接将 auto page 对应的内存映射到显存上，省的多一道复制 */
        text_page->page_mem.bpp         = default_display->bpp;
        text_page->page_mem.width       = default_display->xres;
        text_page->page_mem.height      = default_display->yres;
        text_page->page_mem.line_bytes  = text_page->page_mem.width * text_page->page_mem.bpp / 8;
        text_page->page_mem.total_bytes = text_page->page_mem.line_bytes * text_page->page_mem.height; 
        text_page->page_mem.buf         = default_display->buf;
        text_page->allocated            = 1;
        text_page->share_fbmem          = 1;
    }

    /* 清理或填充一个背景 */
    clear_pixel_data(&text_page->page_mem,BACKGROUND_COLOR);

    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!text_page->region_mapped){
        ret = remap_regions_to_page_mem(text_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return ret;
        }
    }

    /* 准备图标数据 */
    if(!text_page->icon_prepared){
        ret = prepare_icon_pixel_datas(text_page);
        if(ret){
            DP_ERR("%s:prepare_icon_pixel_datas failed!\n",__func__);
            return ret;
        }
    }

    /* 填充主体文字区域 */ 
    /* 先生成缓存 */
    if(!text_caches_generated){
        ret = generate_text_caches();
        if(ret){
            DP_ERR("%s:generate_text_caches failed!\n",__func__);
            return ret;
        }
    }
    /* 缓存生成后直接显示即可 */
    merge_pixel_data(regions[REGION_MAIN_TEXT].pixel_data,*cur_text_cache);
    flush_page_region(&regions[REGION_MAIN_TEXT],default_display);

    /* 填充底部信息区域 */

    return 0;
}

/* 填充总体菜单区域 */
static int fill_total_menu_area(struct page_struct *text_page)
{
    int i;
    int font_size;
    struct page_region *regions = text_page->page_layout.regions;

    /* 添加总体菜单背景 */
    clear_pixel_data(regions[REGION_MENU_TOTAL].pixel_data,0x9ace);
    /* 填写三个菜单的图标 */
    for(i = 0 ; i < 3 ; i++){
        merge_pixel_data(regions[REGION_MENU_CATALOG_ICON + 3 * i].pixel_data,&icon_pixel_datas[ICON_CATALOG + i]);
    }
    /* 填写三个菜单的文字 */
    font_size = regions[REGION_MENU_CATALOG_TEXT].height - 5;
    get_string_bitamp_from_buf("目录",0,"utf-8",regions[REGION_MENU_CATALOG_TEXT].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xbedf,font_size);
    get_string_bitamp_from_buf("进度",0,"utf-8",regions[REGION_MENU_PROGRESS_TEXT].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xbedf,font_size);
    get_string_bitamp_from_buf("版式",0,"utf-8",regions[REGION_MENU_FORMAT_TEXT].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xbedf,font_size);
    return 0;
}

/* 给一个区域上左右三边加上边框以示选中,默认选中第一个 */
static int format_add_selected_pattern(struct pixel_data *pixel_data,int color,int line_width)
{
    int i,j;
    unsigned int width = pixel_data->width;
    unsigned int height = pixel_data->height;
    unsigned char *line_buf;
    unsigned char red,green,blue;
    unsigned short color_16 = (unsigned short)color;
    printf("%s-%d\n",__func__,__LINE__);
    for(i = 0 ; i < height ; i++){
        if(pixel_data->in_rows){
            line_buf = pixel_data->rows_buf[i];
        }else{
            line_buf = pixel_data->buf + pixel_data->line_bytes * i;
        }
        switch(pixel_data->bpp){
        case 16:
            for(j = 0 ; j < width ; j++){
                if(i > line_width && j >= line_width && j <= (width - line_width)){
                    j += (width - 2 * line_width);
                    line_buf += 2 * (width - 2 * line_width);
                    // continue;
                }
                *(unsigned short *)line_buf = color_16;
                line_buf += 2;
            }
            break;
        case 24:
            for(j = 0 ; j < width ; j++){
                if(i > line_width && j >= line_width && j <= (width - line_width)){
                    j += (width - 2 * line_width);
                    line_buf += 3 * (width - 2 * line_width);
                    // continue;
                }
                /* 取出各颜色分量 */
                red     = (color >> 16) & 0xff;
                green   = (color >> 8) & 0xff;
                blue    = (color) & 0xff;
                line_buf[0] = red;
                line_buf[1] = green;
                line_buf[2] = blue;
                line_buf += 3;
            }
            break;
        case 32:
            for(j = 0 ; j < width ; j++){
                if(i > line_width && j >= line_width && j <= (width - line_width)){
                    j += (width - 2 * line_width);
                    line_buf += 4 * (width - 2 * line_width);
                    // continue;
                }
                *(unsigned int *)line_buf = color;
                line_buf += 4;
            }
            break;
        }
    }
    printf("%s-%d\n",__func__,__LINE__);
    return 0;
}

/* 画分隔各选项行的3条虚线 */
static int format_draw_dot_line(struct pixel_data *pixel_data,int color,int line_width)
{
#define DOT_LINE_INTERVAL (width / 120)
    int i,j,k;
    int row_height,y_cursor;
    unsigned int width = pixel_data->width;
    unsigned int height = pixel_data->height;
    int interval,reverse_bit;
    unsigned char red,green,blue;
    unsigned short color_16 = (unsigned short)color;
    unsigned char *line_buf;
    struct page_region *regions = text_page.page_layout.regions;
    
    y_cursor = regions[REGION_FORMAT_SELECT1].height + regions[REGION_FORMAT2_ROW1_SCALE].height - line_width;
    row_height = regions[REGION_FORMAT2_ROW1_SCALE].height;
    interval = DOT_LINE_INTERVAL;
    printf("%s-%d\n",__func__,__LINE__);
    for(i = 0 ; i < 3 ; i++){
        for(j = 0 ; j < line_width ; j++){
            reverse_bit = 0;
            interval = DOT_LINE_INTERVAL;
            if(pixel_data->in_rows){
                line_buf = pixel_data->rows_buf[y_cursor + i * row_height + j];
            }else{
                line_buf = pixel_data->buf + pixel_data->line_bytes * (y_cursor + i * row_height + j);
            }
            switch(pixel_data->bpp){
            case 16:
                for(k = 0 ; k < width ; k++){
                    if(reverse_bit){
                        *(unsigned short *)line_buf = color;
                    }
                    // printf("interval-%d\n",interval);
                    if(!interval--){
                        reverse_bit = ~reverse_bit;
                        interval = DOT_LINE_INTERVAL;   
                    }
                    line_buf += 2;
                }
                break;
            case 24:
                for(j = 0 ; j < width ; j++){
                    /* 取出各颜色分量 */
                    if(reverse_bit){
                        color = *(unsigned short *)line_buf;
                        red     = (color >> 16) & 0xff;
                        green   = (color >> 8) & 0xff;
                        blue    = (color) & 0xff;
                        line_buf[0] = red;
                        line_buf[1] = green;
                        line_buf[2] = blue;
                    }
                    if(interval--){
                        reverse_bit = ~reverse_bit;
                        interval = DOT_LINE_INTERVAL;
                    } 
                    line_buf += 3;
                }
                break;
            case 32:
                for(j = 0 ; j < width ; j++){
                    if(reverse_bit)
                        *(unsigned int *)line_buf = color_16;
                    if(interval--){
                        reverse_bit = ~reverse_bit;
                        interval = DOT_LINE_INTERVAL;
                    }
                    line_buf += 4;
                }
                break;
            }
        } 
    }
    return 0;
}

/* 又是一个又臭又长的函数,吐了 */
/* @description : 画圆角矩形刻度条,此处用的算法很蠢,但没办法,我暂时想不到更好的方法了
 * @cur_value : 该参数的当前值;
 * @range : 参数的最大值;
 * @width : 半圆矩形的宽度,取0会设为默认值
 * @height :  半圆矩形的高度,取0会设为默认值*/
static int format_draw_scale_bar(struct pixel_data *pixel_data,int width,int height,int line_width,int cur_value,int range)
{
#define LINE_COLOR 0xffff
#define FILL_COLOR 0xf800
#define UNFILL_COLOR 0xf800
    int i,j,k;
    int fill_length;
    int x_start,y_start;
    int r1,r2,temp;
    int x_temp,y_temp;
    int x1,x2,y1;
    int circle_r,circle_r2,circle_r1,circle_d;
    unsigned char *line_buf;
    int bytes_per_pixel = pixel_data->bpp / 8;

    if((width + line_width) > pixel_data->width || (height + line_width) > pixel_data->height){
        DP_ERR("%s:invalied argument!\n",__func__);
        return -1;
    }
    if(!height)
        height = pixel_data->height / 3;
    if(!width)
        width = pixel_data->width - 10 - line_width;
    printf("%s-%d\n",__func__,__LINE__);
    x_start = (pixel_data->width - width) / 2 - line_width / 2;
    y_start = (pixel_data->height - height) / 2 - line_width / 2;
    x1 = (pixel_data->width - width) / 2 + height / 2;
    x2 = pixel_data->width - x1;
    y1 = pixel_data->height / 2;
    /* 要填充的长度,用于表示变量的取值 */
    fill_length = (width - height) * cur_value / range;
    /* 因为线有宽度,r1,r2分别表示内外半径的平方 */
    r1 = (height / 2 - line_width / 2) * (height / 2 - line_width / 2);
    r2 = (height / 2 + line_width / 2) * (height / 2 + line_width / 2);
    printf("r1:%d,r2:%d\n",r1,r2);
    /* 画半圆矩形边框及填充内容 */ 
    x_temp = x_start + width + line_width;
    y_temp = y_start + height + line_width;
    for(i = y_start ; i < y_temp ; i++){
        if(pixel_data->in_rows){
            line_buf = pixel_data->rows_buf[i] ;
        }else{
            line_buf = pixel_data->buf + pixel_data->line_bytes * i;
        }
        line_buf += x_start * bytes_per_pixel;
        for(j = x_start ; j < x_temp ; j++){
           
            if(j < x1){       /* 半圆区域 */
                temp = (x1 - j) * (x1 - j) + (y1 - i) * (y1 - i);
            }else if(j > x2){
                temp = (j - x2) * (j - x2) + (y1 - i) * (y1 - i);
            }else{
                *(unsigned short *)line_buf = LINE_COLOR;
                line_buf += bytes_per_pixel;
                continue;
            }
            if(temp > r2){
                line_buf += pixel_data->bpp / 8;
                continue;   /* 区域外 */
            }else if(temp > r1){
                /* 线内,需填充 */
                *(unsigned short *)line_buf = LINE_COLOR;
                line_buf += bytes_per_pixel;
            }else{
                /* 内部区域,要么填充底色,要么填充填充色 */
                k = j + 2 * (x1 - j) + (x2 - x1);
                for( ; j <= k ; j++){
                    if(j < (fill_length + x1)){
                        /* 内部填充色 */
                        switch(pixel_data->bpp){
                        case 16:
                                *(unsigned short *)line_buf = LINE_COLOR;
                                line_buf += 2;
                            break;
                        case 24:
                            break;
                        case 32:
                            break;
                        }
                    }else{
                        /* 内部背景色 */
                        switch(pixel_data->bpp){
                        case 16:
                                *(unsigned short *)line_buf = UNFILL_COLOR;
                                line_buf += 2;
                            break;
                        case 24:
                            break;
                        case 32:
                            break;
                        }
                    }
                }
                continue;
            }
        }
    }

    printf("%s-%d\n",__func__,__LINE__);
    /* 画表示值所处位置的圆 */
    /* 圆半径 */
    circle_r = (height + (pixel_data->height - height) / 2) / 2;
    x_start = fill_length + x1 - circle_r - line_width / 2;
    y_start = y1 - circle_r - line_width / 2;
    x2 = fill_length + x1;
    circle_r1 = (circle_r - line_width / 2) * (circle_r - line_width / 2);
    circle_r2 = (circle_r + line_width / 2) * (circle_r + line_width / 2);
    circle_d = circle_r * 2;
    x_temp = x_start + circle_d + line_width;
    y_temp = y_start + circle_d + line_width;
    for(i = y_start ; i < y_temp ; i++){
        if(pixel_data->in_rows){
            line_buf = pixel_data->rows_buf[i] ;
        }else{
            line_buf = pixel_data->buf + pixel_data->line_bytes * i;
        }
        line_buf += x_start * bytes_per_pixel;
        for(j = x_start ; j < x_temp ; j++){
            temp = (j - x2) * (j - x2) + (i - y1) * (i - y1);
            if(temp > circle_r2){
                /* 圆圈外 */
                line_buf += bytes_per_pixel;
                continue;
            }else if(temp > circle_r1){
                /* 圆的边线上 */
                switch(pixel_data->bpp){
                case 16:
                        *(unsigned short *)line_buf = LINE_COLOR;
                        line_buf += bytes_per_pixel;
                    break;
                case 24:
                    break;
                case 32:
                    break;
                }
            }else{
                /* 圆内部 */
                switch(pixel_data->bpp){
                case 16:
                        *(unsigned short *)line_buf = FILL_COLOR;
                        line_buf += bytes_per_pixel;
                    break;
                case 24:
                    break;
                case 32:
                    break;
                }
            }
        }
    }
    return 0;
}

/* 填充 "格式菜单"子菜单1 */
static int fill_format1_menu_area(struct page_struct *text_page)
{
    return 0;
}

/* 填充 "格式菜单"子菜单2 */
static int fill_format2_menu_area(struct page_struct *text_page)
{   
    int i,ret;
    int font_size;
    char buf[8];
    struct page_region *regions = text_page->page_layout.regions;
    printf("%s-%d\n",__func__,__LINE__);
    /* 描画各选项行 */
    /* 图标都是一样的,先画图标 */
    for(i = 0 ; i < 4 ; i++){
        ret = merge_pixel_data(regions[REGION_FORMAT2_ROW1_ICON1 + i * 5].pixel_data,&icon_pixel_datas[ICON_FORMAT_SUB]);
        ret |= merge_pixel_data(regions[REGION_FORMAT2_ROW1_ICON2 + i * 5].pixel_data,&icon_pixel_datas[ICON_FORMAT_PLUS]);
        if(ret){
            DP_ERR("%s:merge_pixel_data failed!\n",__func__);
            return ret;
        }
    }printf("%s-%d\n",__func__,__LINE__);
    /* 文字 */
    font_size = regions[REGION_FORMAT2_ROW1_TEXT1].height - 20;
    ret = get_string_bitamp_from_buf("行间距",0,"utf-8",regions[REGION_FORMAT2_ROW1_TEXT1].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xbfed,font_size);
    snprintf(buf,2,"%02d",line_spacing);
    ret |= get_string_bitamp_from_buf(buf,0,"utf-8",regions[REGION_FORMAT2_ROW1_TEXT2].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xbfed,font_size);

    ret |= get_string_bitamp_from_buf("段间距",0,"utf-8",regions[REGION_FORMAT2_ROW2_TEXT1].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xbfed,font_size);
    snprintf(buf,2,"%02d",segment_spacing);
    ret |= get_string_bitamp_from_buf(buf,0,"utf-8",regions[REGION_FORMAT2_ROW2_TEXT2].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xbfed,font_size);

    ret |= get_string_bitamp_from_buf("上下边距",0,"utf-8",regions[REGION_FORMAT2_ROW3_TEXT1].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xbfed,font_size);
    snprintf(buf,4,"%02d",vertical_padding);
    ret |= get_string_bitamp_from_buf(buf,0,"utf-8",regions[REGION_FORMAT2_ROW3_TEXT2].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xbfed,font_size);

    ret |= get_string_bitamp_from_buf("左右边距",0,"utf-8",regions[REGION_FORMAT2_ROW4_TEXT1].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xbfed,font_size);
    snprintf(buf,4,"%02d",horizontal_padding);
    ret |= get_string_bitamp_from_buf(buf,0,"utf-8",regions[REGION_FORMAT2_ROW4_TEXT2].pixel_data,FONT_ALIGN_HORIZONTAL_CENTER,0xbfed,font_size);
    if(ret){
        DP_ERR("%s:fill font failed!\n",__func__);
        return ret;
    }printf("%s-%d\n",__func__,__LINE__);
    /* 描画中间的刻度条 */
    ret = format_draw_scale_bar(regions[REGION_FORMAT2_ROW1_SCALE].pixel_data,0,0,4,5,MAX_LINE_SPACING);
    ret |= format_draw_scale_bar(regions[REGION_FORMAT2_ROW2_SCALE].pixel_data,0,0,4,5,MAX_SEGMENT_SPACING);
    ret |= format_draw_scale_bar(regions[REGION_FORMAT2_ROW3_SCALE].pixel_data,0,0,4,5,MAX_VPADDING);
    ret |= format_draw_scale_bar(regions[REGION_FORMAT2_ROW4_SCALE].pixel_data,0,0,4,5,MAX_HPADDING);
    if(ret < 0){
        DP_ERR("%s:format_draw_scale_bar failed!\n",__func__);
        return ret;
    }printf("%s-%d\n",__func__,__LINE__);
    return 0;
}

/* 填充 "格式菜单" */
static int show_format_menu_area(struct page_struct *text_page)
{
    int i,ret;
    int font_size;
    struct page_region *regions = text_page->page_layout.regions;

    if(!text_page->region_mapped)
        return -1;
    
    /* 上一个底色 */
    clear_pixel_data(regions[REGION_FORMAT_TOTAL].pixel_data,0xeeee);
    printf("%s-%d\n",__func__,__LINE__);
    /* 给一个区域上左右三边加上边框以示选中,默认选中第一个 */
    ret = format_add_selected_pattern(regions[REGION_FORMAT_SELECT1].pixel_data,0xff,5);
    if(ret){
        DP_ERR("%s:format_add_selected_pattern failed!\n",__func__);
        return ret;
    }
    printf("%s-%d\n",__func__,__LINE__);
    /* 画各选项行之间的分割虚线 */
    ret = format_draw_dot_line(regions[REGION_FORMAT_TOTAL].pixel_data,0xf00f,2);
    if(ret){
        DP_ERR("%s:format_draw_dot_line failed!\n",__func__);
        return ret;
    }
    printf("%s-%d\n",__func__,__LINE__);
    /* 根据标志位决定显示哪一个子菜单 */
    printf("format_select_status：%d\n",format_select_status);
    if(format_select_status == 1){
        printf("%s-%d\n",__func__,__LINE__);
        ret = fill_format1_menu_area(text_page);
        if(ret){
            DP_ERR("%s:fill format1 menu area failed!\n",__func__);
            return ret;
        }
    }else if(format_select_status == 2){
        printf("%s-%d\n",__func__,__LINE__);
        ret = fill_format2_menu_area(text_page);
        if(ret){
            DP_ERR("%s:fill format2 menu area failed!\n",__func__);
            return ret;
        }
        /* 将相应的区域标为可见 */
        for(i = 0 ; i < 4 ; i++){
            regions[REGION_FORMAT2_ROW1_ICON1 + i * 5].invisible = 0;
            regions[REGION_FORMAT2_ROW1_SCALE + i * 5].invisible = 0;
            regions[REGION_FORMAT2_ROW1_ICON2 + i * 5].invisible = 0;
        }
    }
    printf("%s-%d\n",__func__,__LINE__);
    return 0;
}

/* 隐藏 "格式菜单" */
static int hidden_format_menu_area(struct page_struct *text_page)
{
    int i,ret;
    int font_size;
    struct page_region *regions = text_page->page_layout.regions;
    struct display_struct *display = get_default_display();

    /* 重新刷新主体文本和主体菜单 */
    merge_pixel_data(regions[REGION_MAIN_TEXT].pixel_data,*cur_text_cache);
    fill_total_menu_area(text_page);
    flush_page_region(&regions[REGION_MAIN_TEXT],display);
    flush_page_region(&regions[REGION_MENU_TOTAL],display);
    /* 将相应的区域标为不可见 */
    for(i = 0 ; i < 4 ; i++){
        regions[REGION_FORMAT2_ROW1_ICON1 + i * 5].invisible = 1;
        regions[REGION_FORMAT2_ROW1_SCALE + i * 5].invisible = 1;
        regions[REGION_FORMAT2_ROW1_ICON2 + i * 5].invisible = 1;
    }
    return 0;
}


static int show_menu(void)
{
    int i;
    struct page_region *regions = text_page.page_layout.regions;
    struct display_struct *display = get_default_display();
    if(menu_show_status){
        /* 隐藏菜单 */
        merge_pixel_data(regions[REGION_MAIN_TEXT].pixel_data,*cur_text_cache);
        clear_pixel_data(regions[REGION_BOTTOM_INFO].pixel_data,BACKGROUND_COLOR);
        flush_page_region(&regions[REGION_MAIN_TEXT],display);
        /* 将相应的区域标为不可见 */
        for(i = 0 ; i < 3 ; i++){
            regions[REGION_MENU_PROGRESS + i * 3].invisible = 1;
        }
        menu_show_status = 0;
        format_select_status = 0;
    }else{
        /* 显示菜单 */
        fill_total_menu_area(&text_page);
        flush_page_region(&regions[REGION_MENU_TOTAL],display);
        /* 将相应的区域标为可见 */
        for(i = 0 ; i < 3 ; i++){
            regions[REGION_MENU_PROGRESS + i * 3].invisible = 0;
        }
        menu_show_status = 1;
    }
    DP_INFO("menu evetn\n");
    return 0;
}

static int show_pre_page(void)
{
    int i;
    int fIOCN_FORMAT_PLUSile_size,read_len;
    const unsigned char *file_buf;
    struct page_region *regions = text_page.page_layout.regions;
    struct display_struct *display = get_default_display();
    struct pixel_data *temp = text_caches[4];
    struct page_pos *next_page_pos;

    /* 当前没有缓存了,直接退出 */
    if(!text_caches[1])
        return 0;
    for(i = (CACHE_COUNT - 1) ; i > 0 ; i--){
        text_caches[i] = text_caches[i - 1];
    }
    text_caches[0] = NULL;
    /* 显示当前页为最优先 */
    if(*cur_text_cache){
        merge_pixel_data(regions[REGION_MAIN_TEXT].pixel_data,*cur_text_cache);
        flush_page_region(&regions[REGION_MAIN_TEXT],display);
        cur_page_pos = container_of(cur_page_pos->list.prev,struct page_pos,list);
        cur_file_pos = cur_page_pos->start_pos;
    }
    /* 释放前面的页以及相关的文件位置结构 */
    if(temp){
        if(temp->buf)
            free(temp->buf);
        free(temp);
        next_page_pos = container_of(page_pos_head.prev,struct page_pos,list);
        if(next_page_pos){
            list_del(&next_page_pos->list);
            free(next_page_pos);
        }  
    }
    /* 这里与读取下一页的逻辑不同,读取上一页必须根据之前记录的文件位置信息 */
    if(cur_page_pos->list.prev != &page_pos_head){
        if(cur_page_pos->list.prev->prev != &page_pos_head){
            next_page_pos = container_of(cur_page_pos->list.prev->prev,struct page_pos,list);
        }else{
            return 0;
        }
         /*获取当前的文件位置和剩余文件大小 */
        file_buf = next_page_pos->start_pos;
        file_size = next_page_pos->end_pos - file_buf;
        /* 分配缓存空间 */
        temp = malloc(sizeof(struct pixel_data));
        if(!temp){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        *temp = *regions[REGION_MAIN_TEXT].pixel_data;
        temp->rows_buf = 0;
        temp->in_rows = 0;
        temp->buf = malloc(temp->total_bytes);
        if(!temp->buf){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        clear_pixel_data(temp,BACKGROUND_COLOR);
        /* 生成页面内容 */
        read_len = fill_text_one_page(temp,(char *)file_buf,file_size,cur_decoder);
        if(read_len < 0){
            DP_ERR("%s:fill_text_one_page failed!\n",__func__);
            return read_len;
        }
        text_caches[0] = temp;
    }
    return 0;
}

static int show_next_page(void)
{
    int i;
    int file_size,read_len;
    const unsigned char *file_buf;
    struct page_region *regions = text_page.page_layout.regions;
    struct display_struct *display = get_default_display();
    struct pixel_data *temp = text_caches[0];
    struct page_pos *next_page_pos,*new_page_pos;

    /* 当前没有缓存了,直接退出 */
    if(!text_caches[3])
        return 0;
    for(i = 0 ; i < (CACHE_COUNT - 1) ; i++){
        text_caches[i] = text_caches[i + 1];
    }
    text_caches[CACHE_COUNT - 1] = NULL;
    /* 显示当前页为最优先 */
    if(*cur_text_cache){
        merge_pixel_data(regions[REGION_MAIN_TEXT].pixel_data,*cur_text_cache);
        flush_page_region(&regions[REGION_MAIN_TEXT],display);
        cur_page_pos = container_of(cur_page_pos->list.next,struct page_pos,list);
        cur_file_pos = cur_page_pos->start_pos;
    }
    /* 释放前面的页 */
    if(temp){
        if(temp->buf)
            free(temp->buf);
        free(temp);
    }
    /* 尝试预读下一页 */
    next_page_pos = container_of(page_pos_head.prev,struct page_pos,list);
    if(next_page_pos != cur_page_pos){
        /*获取当前的文件位置和剩余文件大小 */
        file_buf = next_page_pos->end_pos;
        file_size = file_end - file_buf;
        /* 分配缓存空间 */
        temp = malloc(sizeof(struct pixel_data));
        if(!temp){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        *temp = *regions[REGION_MAIN_TEXT].pixel_data;
        temp->rows_buf = 0;
        temp->in_rows = 0;
        temp->buf = malloc(temp->total_bytes);
        if(!temp->buf){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        clear_pixel_data(temp,BACKGROUND_COLOR);
        /* 生成页面内容 */
        read_len = fill_text_one_page(temp,(char *)file_buf,file_size,cur_decoder);
        if(read_len < 0){
            DP_ERR("%s:fill_text_one_page failed!\n",__func__);
            return read_len;
        }
        text_caches[4] = temp;
        /* 记录文件位置 */
        new_page_pos = malloc(sizeof(struct page_pos));
        if(!new_page_pos){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        new_page_pos->start_pos = file_buf;
        new_page_pos->end_pos = file_buf + read_len;
        new_page_pos->index = next_page_pos->index + 1;
        list_add_tail(&new_page_pos->list,&page_pos_head);
    }
    return 0;
}

/* 点击"格式"菜单时的响应函数 */
static int format_menu_cb_func(void)
{
    int ret = 0;
    DP_INFO("enter :%s!\n",__func__);
    /* 显示时点击隐藏，隐藏时点击显示 */
    if(!format_select_status){
        format_select_status = 2;
        ret = show_format_menu_area(&text_page);  
    }else{
        ret = hidden_format_menu_area(&text_page);
        format_select_status = 0;
    }
    if(ret){
        DP_ERR("%s:err!\n");
    } 
    return 0;
}

/* 点击"目录"菜单时的响应函数 */
static int catalog_menu_cb_func(void)
{
    DP_ERR("catalog menu!\n");
    return 0;
}
/* 点击"进度"菜单时的响应函数 */
static int progress_menu_cb_func(void)
{
    DP_ERR("progress menu!\n");
    return 0;
}

/* 主体区域的触摸回调,主要三个功能:上一页,下一页,唤出菜单 */
static int main_text_cb_func(struct my_input_event *event)
{
    struct page_region *regions = text_page.page_layout.regions;
    int unit_width = regions[REGION_MAIN_TEXT].width / 3;
    int ret;

    if(event->x_pos < unit_width){
        /* 上一页 */
        ret = show_pre_page();
    }else if(event->x_pos < (unit_width * 2)){
        /* 显示菜单 */
        ret = show_menu();
    }else{
        /* 下一页 */
        ret = show_next_page();
    }
    if(ret){
        DP_ERR("%s:main_text_cb_func error!\n",__func__);
        return -1;
    }
    return 0;
}

/* 主要功能：映射内存；解析要显示的数据；while循环检测输入*/
static int text_page_run(struct page_param *pre_page_param)
{
    int ret;
    int pre_region_index = -1;
    int region_index;
    int slot_id = -1;
    int pressure = 0;
    struct display_struct *default_display = get_default_display();
    struct page_region *regions = text_page.page_layout.regions;
    struct page_struct *next_page;
    struct page_param page_param;

    if(!text_page.already_layout){
        text_page_init();
    }
    printf("enter-%s\n",__func__);
    if(!text_page.allocated){
        /* 直接将 auto page 对应的内存映射到显存上，省的多一道复制 */
        text_page.page_mem.bpp         = default_display->bpp;
        text_page.page_mem.width       = default_display->xres;
        text_page.page_mem.height      = default_display->yres;
        text_page.page_mem.line_bytes  = text_page.page_mem.width * text_page.page_mem.bpp / 8;
        text_page.page_mem.total_bytes = text_page.page_mem.line_bytes * text_page.page_mem.height; 
        text_page.page_mem.buf         = default_display->buf;
        text_page.allocated            = 1;
        text_page.share_fbmem          = 1;
    }
    printf("%s-%d\n",__func__,__LINE__);
    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!text_page.region_mapped){
        ret = remap_regions_to_page_mem(&text_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return ret;
        }
    }
    printf("%s-%d\n",__func__,__LINE__);
    /* 准备图标数据 */
    if(!text_page.icon_prepared){
        ret = prepare_icon_pixel_datas(&text_page);
        if(ret){
            DP_ERR("%s:prepare_icon_pixel_datas failed!\n",__func__);
            return ret;
        }
    }
    printf("%s-%d\n",__func__,__LINE__);
    /* 获取当前要打开的文本文件名 */
    if(pre_page_param->private_data){
        cur_file = pre_page_param->private_data;
    }
    printf("%s-%d\n",__func__,__LINE__);
    /* 获取文件信息 */
    ret = parse_text_file((char *)cur_file);
    if(ret){
        DP_ERR("%s:parse_text_file failed!\n",__func__);
        return ret;
    }
    printf("%s-%d\n",__func__,__LINE__);
    /* 获取缓存 */
    ret = generate_text_caches();
    if(ret){
        DP_ERR("%s:generate_text_caches failed!\n",__func__);
        return ret;
    }
    printf("%s-%d\n",__func__,__LINE__);
    /* 填充页面 */
    ret = text_page_fill_layout(&text_page);
    if(ret){
        DP_ERR("%s:text_page_fill_layout failed!\n",__func__);
        return ret;
    }   
    printf("%s-%d\n",__func__,__LINE__);
    /* 因为页面与显存共享一块内存，所以不用刷新 */
    
    /* 检测输入事件的循环 */
    while(1){
        struct my_input_event event;
        region_index = get_input_event_for_page(&text_page,&event);
        printf("%s-region_index:%d\n",__func__,region_index);
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
            }
        }else{                     //松开
            /* 按下和松开的是同一个区域，这是一次有效的点击 */
            if(pre_region_index == region_index){
                page_param.id = text_page.id;
                switch(region_index){
                    case REGION_MAIN_TEXT:
                        main_text_cb_func(&event);
                        continue;
                    case REGION_MENU_CATALOG:
                        catalog_menu_cb_func();
                        continue;
                    case REGION_MENU_PROGRESS:
                        progress_menu_cb_func();
                        continue;
                    case REGION_MENU_FORMAT:
                        format_menu_cb_func();
                        continue;
                    default:
                        continue;     
                }
                press_region(&regions[pre_region_index],0,0);
                flush_page_region(&regions[pre_region_index],default_display);
            }else{
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

static struct page_region text_page_regions[] = {
    PAGE_REGION(REGION_MAIN_TEXT,0,&text_page),
    PAGE_REGION(REGION_BOTTOM_INFO,1,&text_page),
    PAGE_REGION(REGION_MENU_TOTAL,1,&text_page),
    PAGE_REGION(REGION_MENU_CATALOG,1,&text_page),
    PAGE_REGION(REGION_MENU_CATALOG_ICON,1,&text_page),
    PAGE_REGION(REGION_MENU_CATALOG_TEXT,1,&text_page),
    PAGE_REGION(REGION_MENU_PROGRESS,1,&text_page),
    PAGE_REGION(REGION_MENU_PROGRESS_ICON,1,&text_page),
    PAGE_REGION(REGION_MENU_PROGRESS_TEXT,1,&text_page),
    PAGE_REGION(REGION_MENU_FORMAT,1,&text_page),
    PAGE_REGION(REGION_MENU_FORMAT_ICON,1,&text_page),
    PAGE_REGION(REGION_MENU_FORMAT_TEXT,1,&text_page),
    PAGE_REGION(REGION_FORMAT_TOTAL,1,&text_page),    
    PAGE_REGION(REGION_FORMAT_SELECT1,1,&text_page),    
    PAGE_REGION(REGION_FORMAT_SELECT2,1,&text_page),    
    PAGE_REGION(REGION_FORMAT2_ROW1_TEXT1,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW1_ICON1,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW1_SCALE,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW1_ICON2,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW1_TEXT2,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW2_TEXT1,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW2_ICON1,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW2_SCALE,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW2_ICON2,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW2_TEXT2,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW3_TEXT1,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW3_ICON1,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW3_SCALE,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW3_ICON2,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW3_TEXT2,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW4_TEXT1,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW4_ICON1,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW4_SCALE,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW4_ICON2,1,&text_page),
    PAGE_REGION(REGION_FORMAT2_ROW4_TEXT2,1,&text_page),
};

static struct page_struct text_page = {
    .name = "text_page",
    .page_layout = {
        .regions = text_page_regions,
        .region_num = sizeof(text_page_regions) / sizeof(struct page_region),
    },
    .init = text_page_init,
    .exit = text_page_exit,
    .run  = text_page_run,
    .allocated = 0,
};

int text_init(void)
{
    return register_page_struct(&text_page);
}