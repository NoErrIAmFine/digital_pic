#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#include "config.h"
#include "page_manager.h"
#include "debug_manager.h"
#include "picfmt_manager.h"
#include "pic_operation.h"
#include "input_manager.h"
#include "render.h"
#include "config.h"
#include "file.h"

static struct page_struct view_pic_page;
static struct view_pic_private view_pic_priv;

enum region_info{     
    REGION_MENU_RETURN = 0,   
    REGION_MENU_START_AUTOPLAY,   
    REGION_MENU_PRE_PIC,     
    REGION_MENU_NEXT_PIC,    
    REGION_MENU_UNFOLD,      
    REGION_MAIN_PIC,         
    REGION_MENU_ZOOM_IN,     
    REGION_MENU_ZOOM_OUT,    
    REGION_MENU_LEFT_ROTATE, 
    REGION_MENU_RIGHT_ROTATE,
    REGION_MENU_PIC_RESET, 
    REGION_MENU_HV_SCREEN,
    REGION_NUMS,  
};

/* 本页面用到的图标信息 */
enum icon_info{       
    ICON_MENU_RETURN = 0, 
    ICON_MENU_START_AUTOPLAY,   
    ICON_MENU_PRE_PIC,      
    ICON_MENU_NEXT_PIC,     
    ICON_MENU_UNFOLD, 
    ICON_MENU_FOLD,      
    ICON_MENU_ZOOM_IN,      
    ICON_MENU_ZOOM_OUT,     
    ICON_MENU_LEFT_ROTATE,  
    ICON_MENU_RIGHT_ROTATE, 
    ICON_MENU_PIC_RESET, 
    ICON_MENU_HV_SCREEN,
    ICON_LOAD_ERR,        
    ICON_NUMS, 
};

/* 图标名称数组 */
static const char *icon_file_names[ICON_NUMS] = {
    [ICON_MENU_RETURN]          = "return.png",
    [ICON_MENU_START_AUTOPLAY]  = "start_autoplay.png",
    [ICON_MENU_PRE_PIC]         = "pre_pic.png",
    [ICON_MENU_NEXT_PIC]        = "next_pic.png",
    [ICON_MENU_UNFOLD]          = "unfold.png",
    [ICON_MENU_ZOOM_IN]         = "zoom_in.png",
    [ICON_MENU_ZOOM_OUT]        = "zoom_out.png",
    [ICON_MENU_LEFT_ROTATE]     = "left_rotate.png",
    [ICON_MENU_RIGHT_ROTATE]    = "right_rotate.png",
    [ICON_MENU_PIC_RESET]       = "pic_reset.png",
    [ICON_MENU_FOLD]            = "fold.png",
    [ICON_MENU_HV_SCREEN]       = "hv_screen.png",
    [ICON_LOAD_ERR]             = "load_err.png",
};
 
/* 图标对应的区域，数组下标表示图标编号，下标对应的数组项表示该图标对应的区域，用于图标缩放 */
static const unsigned int icon_region_links[] = {
    [ICON_MENU_RETURN]          = REGION_MENU_RETURN,
    [ICON_MENU_START_AUTOPLAY]  = REGION_MENU_START_AUTOPLAY,
    [ICON_MENU_PRE_PIC]         = REGION_MENU_PRE_PIC,
    [ICON_MENU_NEXT_PIC]        = REGION_MENU_NEXT_PIC,
    [ICON_MENU_UNFOLD]          = REGION_MENU_UNFOLD,
    [ICON_MENU_ZOOM_IN]         = REGION_MENU_ZOOM_IN,
    [ICON_MENU_ZOOM_OUT]        = REGION_MENU_ZOOM_OUT,
    [ICON_MENU_LEFT_ROTATE]     = REGION_MENU_LEFT_ROTATE,
    [ICON_MENU_RIGHT_ROTATE]    = REGION_MENU_RIGHT_ROTATE,
    [ICON_MENU_PIC_RESET]       = REGION_MENU_PIC_RESET,
    [ICON_MENU_FOLD]            = REGION_MENU_UNFOLD,
    [ICON_MENU_HV_SCREEN]       = REGION_MENU_HV_SCREEN,
    [ICON_LOAD_ERR]             = REGION_MAIN_PIC,   
};

/* 图标数据数组 */
static struct pixel_data icon_pixel_datas[ICON_NUMS];

#define ZOOM_RATE (0.9)             //放大缩小的比例系数
#define MIN_DRAG_DISTANCE (4 * 4)   //能够响应的最小的拖动距离

static short min_drag_distance = MIN_DRAG_DISTANCE;

/* 表示展开菜单的状态，是展开还是折叠状态 */
static int menu_unfolded = 0;

/* 文件目录相关的几个全局变量 */
static char *cur_dir;                           //当前所在目录
static struct dir_entry **cur_dir_pic_contents; //当前目录下有那些图片
static unsigned int cur_dir_pic_nums;           //当前浏览的目录下所含的图片数量
static int cur_pic_index;                       //当前正浏览的图片在目录中的索引
static char *cur_gif_file;                      //当前正显示的gif图片
static bool pic_dir_generated = 0;              //是否生成了目录信息   
static bool pic_dir_completed = 0;              //表示图片目录信息是否完整，目录信息是随着图片浏览逐渐生成的，当目录信息完全生成时置此位                         
/* 当从“浏览页面”进入此页面时，文件目录信息可以使用“浏览页面”的 */
static struct dir_entry **cur_dir_contents;     //当前目录下的内容，包含所有文件，不只图片
static unsigned int cur_dir_nums;               //当前浏览的目录下所含的文件数量
static int cur_file_index;                      //当前查看的图片在目录下的索引
static int initial_file_index;                  //首次进入此页面时，被选中图片对应的文件索引
static bool is_from_browse;                     //是否是从“浏览页面”进入此页面的

/* 缓存的图片,这里缓存的是原始数据,大小,bpp可能不同 */
static unsigned int pic_caches_generated = 0;
static struct pic_cache *pic_caches[3];
static struct pic_cache **const cur_pic_data = &pic_caches[1];  //当前的图片数据总是在第二个缓存中

/* 如果由于某些原因加载图片出错，则展示该默认图片 */
static struct pixel_data default_err_pic;

static int view_pic_page_calc_layout(struct page_struct *page)
{
    struct page_layout *layout;
    struct page_region *regions;
    unsigned int width,height;
    unsigned int x_cursor,y_cursor,unit_distance;
    unsigned int unfold_region_interval;
    unsigned int unfold_region_width;
    int i;

    width = page->page_layout.width;
    height = page->page_layout.height;
    layout = &page->page_layout;
    layout->region_num = REGION_NUMS;

    /* 动态分配region数组所占用的空间 */
    regions = malloc(layout->region_num * sizeof(struct page_region));
    if(!regions){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }

    layout->regions = regions;
    
    /* 一些与位置无关的成员在外面先填充了 */
    for(i = 0 ; i < 11 ; i++){
        regions[i].index = i;
        if(i > 5){
            /* 这几个区域level为1(从0开始),因为它是在其他区域之上的 */
            regions->level = 1;
        }else{
            regions[i].level = 0;
        }
        regions[i].owner_page = page;
    }
    regions[i].index = i;
    regions[i].level = 0;

    if(width >= height){
        /* 横屏 */
        /*	 iYres/5
		 *	  ----------------------------------		  
		 *    home          
		 *    go back
		 *    pre_pic
		 *    next_pic
		 *    unfold/fold zoom_in zoom_out left_rotate right_rotate reset_pic    
		 *	  ----------------------------------
		 */
        unit_distance = height / 5;
        y_cursor = 0;
        x_cursor = 0;

        /* "home" 、"goback"、 "pre_pic"、"next_pic"、"unfold" 区域*/
        for(i = 0 ; i < 5 ; i++){
            regions[i].x_pos    = x_cursor;
            regions[i].y_pos    = y_cursor + i * unit_distance;
            regions[i].height   = unit_distance;
            regions[i].width    = unit_distance;
        }
        
        /* 图片显示区域 */
        regions[REGION_MAIN_PIC].x_pos    = x_cursor + unit_distance;
        regions[REGION_MAIN_PIC].y_pos    = 0;
        regions[REGION_MAIN_PIC].height   = height ;
        regions[REGION_MAIN_PIC].width    = width - unit_distance;

        unfold_region_interval = 10;
        unfold_region_width = unit_distance * 2 / 3;
        x_cursor = unit_distance + unfold_region_interval;
        y_cursor = unit_distance * 4 + (unit_distance - unfold_region_width) / 2;

        /* zoom_in,zoom_out,left_rotate,right_rotate,pic_reset */
        for(i = 6 ; i < REGION_NUMS ; i++){
            regions[i].x_pos    = x_cursor;
            regions[i].y_pos    = y_cursor;
            x_cursor += unfold_region_interval + unfold_region_width;
            regions[i].height   = unfold_region_width;
            regions[i].width    = unfold_region_width ;
        }
    }else{
        /* 竖屏 */
        /*	 iXres/4
		 *	  --------------------------------------
		 *	        
		 *                                     pic_reset
		 *                                     right_rotate
		 *                                     left_rotate
		 *                                     zoom_out
		 *                                     zoom_in
		 *    home  goback  pre_pic  next_pic  unfold/fold
		 *	  -------------------------------------
		 */
       /* 思路是一样的,时间不够,就不写了 */
    }

    /* 设置相应标志位 */
    page->already_layout = 1;

    return 0;
}

/* 在此函数中将会计算好页面的布局情况 */
static int view_pic_page_init(void)
{
    int ret;
    struct display_struct *default_display = get_default_display();
    struct page_layout *page_layout = &view_pic_page.page_layout;
    struct page_region *regions = page_layout->regions;
    int width = default_display->xres;
    int height = default_display->yres;

    page_layout->width  = width;
    page_layout->height = height;
    view_pic_page.page_mem.bpp     = default_display->bpp;
    view_pic_page.page_mem.width   = width;
    view_pic_page.page_mem.height  = height;
    view_pic_page.page_mem.line_bytes  = view_pic_page.page_mem.width * view_pic_page.page_mem.bpp / 8;
    view_pic_page.page_mem.total_bytes = view_pic_page.page_mem.line_bytes * view_pic_page.page_mem.height;

    /* 计算布局 */
    ret = view_pic_page_calc_layout(&view_pic_page);
    if(ret){
        DP_ERR("%s:view_pic_page_calc_layout failed!\n",__func__);
        return ret;
    }

    return 0;
}

/* 将图片重置为能在屏幕上显示的大小，注意：这个函数只能对保留有原有数据的缓存使用,save_orig参数是说明是否要保留原始数据 */
static int reset_pic_cache_size(struct pic_cache *pic_cache,bool save_orig)
{
    float scale;
    int ret;
    unsigned int display_width,display_height;
    unsigned int zoomed_width,zoomed_height;
    unsigned int orig_width,orig_height;
    struct pixel_data *pixel_data;

    if(!pic_cache->orig_data.buf){
        return 0;              //没有原始图像数据，直接退出
    }

    orig_width = pic_cache->orig_width;
    orig_height = pic_cache->orig_height;
    pixel_data = &pic_cache->data;

    /* 获取显示区域的长宽 */
    scale = (float)orig_height / orig_width;
    display_width = view_pic_page.page_layout.regions[REGION_MAIN_PIC].width;
    display_height = view_pic_page.page_layout.regions[REGION_MAIN_PIC].height;

    /* 确定缩放后的图片大小 */
    if(display_width >= orig_width && display_height >= orig_height){
        zoomed_width = orig_width;
        zoomed_height = orig_height;
    }else if(display_width < orig_width && display_height < orig_height){
        /* 先将宽度缩至允许的最大值 */
        zoomed_width = display_width;
        zoomed_height = scale * zoomed_width;
        if(zoomed_height > display_height){
            /* 还要继续缩小 */
            zoomed_height = display_height;
            zoomed_width = zoomed_height / scale;
        }
    }else if(display_width < orig_width){
        zoomed_width = display_width;
        zoomed_height = zoomed_width * scale;
    }else if(display_height < orig_height){
        zoomed_height = display_height;
        zoomed_width = zoomed_height / scale;
    }

    /* 记录图像在虚拟显示空间中的左上角座标 */
    pic_cache->virtual_x = (display_width - zoomed_width) / 2;
    pic_cache->virtual_y = (display_height - zoomed_height) / 2;

    /* 检查当前缓存的数据大小是否符合要求，要是符合在这里就可以退出了，否则从原始数据中复制一份过去 */
    if(!(!pic_cache->angle && pixel_data->buf && zoomed_width == pixel_data->width && \
    zoomed_height == pixel_data->height)){
        if(pixel_data->buf){
            /* 释放原有数据 */
            free(pixel_data->buf);
        }
        memset(pixel_data,0,sizeof(struct pixel_data));
        pixel_data->width = zoomed_width;
        pixel_data->height = zoomed_height;
        pic_cache->angle = 0;
        ret = pic_zoom_with_same_bpp(pixel_data,&pic_cache->orig_data);
        if(ret < 0){
            DP_ERR("%s:pic_zoom_with_same_bpp failed!\n",__func__);
            return ret;
        }
        
    }
    /* 如果需要释放原有数据 */
    if(!save_orig){
        pixel_data = &pic_cache->orig_data;
        if(pixel_data->buf)
            free(pixel_data->buf);
        memset(pixel_data,0,sizeof(struct pixel_data));
    }
    return 0;
}

/* 给定当前目录信息数组的一个索引，将对应的文件读入并将大小设为初始值,save_orig表示是否保存原有数据 */
static int get_pic_cache_data(int pic_index,struct pic_cache **pic_cache,bool save_orig)
{
    int ret;
    int is_gif = 0;
    char *pic_file;
    struct pixel_data *pixel_data;
    struct pic_cache *temp_cache;
    
    /* 构造文件名 */
    pic_file = malloc(strlen(cur_dir) + 2 + strlen(cur_dir_pic_contents[pic_index]->name));
    if(!pic_file){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    sprintf(pic_file,"%s/%s",cur_dir,cur_dir_pic_contents[pic_index]->name);
    printf("%s-%d-pic_file:%s\n",__func__,__LINE__,pic_file);
    /* 分配一个struct pic_cache */
    temp_cache = malloc(sizeof(struct pic_cache));
    if(!temp_cache){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    memset(temp_cache,0,sizeof(struct pic_cache));

    pixel_data = &temp_cache->orig_data;
    
    if(!cur_dir_pic_contents[pic_index]->file_type)
        cur_dir_pic_contents[pic_index]->file_type = get_file_type(cur_dir,cur_dir_pic_contents[pic_index]->name);

    ret = get_pic_pixel_data(pic_file,cur_dir_pic_contents[pic_index]->file_type,pixel_data);
    if(ret > 0){
        DP_WARNING("%s:get_pic_pixel_data failed!\n",__func__);
    }
    free(pic_file);
    /* 判断是否为gif图片 */
    if(!ret && cur_dir_pic_contents[pic_index]->file_type == FILETYPE_FILE_GIF){
        is_gif = 1;
    }

    /* 填充pic_cache;  保存图像的原始长宽 */
    temp_cache->orig_width = pixel_data->width;
    temp_cache->orig_height = pixel_data->height;
    
    /* 将读入的图片重置为能在屏幕上显示的大小,如果图片能完全被显示,那么不做改变,否则会缩放至合适大小 */
    if(is_gif){
        temp_cache->is_gif = 1;
        temp_cache->gif_thread_pool = (struct gif_thread_pool *)temp_cache->orig_data.rows_buf;
    }

    ret = reset_pic_cache_size(temp_cache,save_orig);
    if(ret < 0){
        DP_ERR("%s:reset_pic_size failed!\n",__func__);
        return -1;
    }
    *pic_cache = temp_cache;        //返回数据
    return 0;
}

/* 读入三张图并缓存 */
static int generate_pic_caches(void)
{
    int i,ret;
    int pre_index,next_index;
    int is_gif = cur_dir_pic_contents[cur_pic_index]->file_type == FILETYPE_FILE_GIF;

    /* 做一些最基本的检查 */
    if(!cur_dir || !cur_dir_pic_contents){
        return -1;
    }

    /* 如果缓存已存在,先将其释放 */
    if(pic_caches_generated){
        for(i = 0 ; i < 3 ; i++){
            if(pic_caches[i]){
                if(pic_caches[i]->data.buf){
                    free(pic_caches[i]->data.buf);
                }
                if(pic_caches[i]->orig_data.buf){
                    free(pic_caches[i]->orig_data.buf);
                }
                free(pic_caches[i]);
            }   
        }
        pic_caches_generated = 0;
    }

    /* 生成前一张,当前,下一张这三张图片的缓存(如果有的话)*/
    /* 当前 */
    if(is_gif){
        pthread_mutex_lock(&view_pic_priv.gif_cache_mutex);
    }
    ret = get_pic_cache_data(cur_pic_index,&pic_caches[1],1);
    if(ret < 0){
        DP_ERR("%s:get_pic_cache_data failed!\n",__func__);
        return -1;
    }
    if(is_gif){
        pthread_mutex_unlock(&view_pic_priv.gif_cache_mutex);
    }

    /* 下一张 */
    if((next_index = cur_pic_index + 1) == cur_dir_pic_nums){
        next_index = 0;
    }
    if(next_index != cur_pic_index && cur_dir_pic_contents[next_index]){
        ret = get_pic_cache_data(next_index,&pic_caches[2],0);
        if(ret < 0){
            DP_ERR("%s:get_pic_cache_data failed!\n",__func__);
            return -1;
        }
    }

    /* 上一张 */
    if((pre_index = cur_pic_index - 1) < 0){
        pre_index = cur_dir_pic_nums - 1;
    }
    if(pre_index != cur_pic_index && pre_index != next_index && cur_dir_pic_contents[pre_index]){
        ret = get_pic_cache_data(pre_index,&pic_caches[0],0);
        if(ret < 0){
            DP_ERR("%s:get_pic_cache_data failed!\n",__func__);
            return -1;
        }
    }
    
    /* 设置相应标志位 */
    pic_caches_generated = 1;

    return 0;
}

/* 释放本页面对应的图像的缓存 */
static void destroy_pic_caches(void)
{
    int i;
    pthread_mutex_lock(&view_pic_priv.gif_mutex);
    if(pic_caches_generated){
        for(i = 0 ; i < 3 ; i++){
            if(pic_caches[i]){
                if(pic_caches[i]->data.buf)
                    free(pic_caches[i]->data.buf);
                if(pic_caches[i]->orig_data.buf)
                    free(pic_caches[i]->orig_data.buf);
                free(pic_caches[i]);
                pic_caches[i] = NULL;
            }
        }
        pic_caches_generated = 0;
    }
    pthread_mutex_unlock(&view_pic_priv.gif_mutex);
}

/* 退出函数 */
static void view_pic_page_exit(void)
{
    struct page_region *regions = view_pic_page.page_layout.regions;
    int i;
    /* 删除映射 */
    unmap_regions_to_page_mem(&view_pic_page);
    
    /* 释放页面占用的内存 */
    if(view_pic_page.allocated && !view_pic_page.share_fbmem){
        free(view_pic_page.page_mem.buf);
        view_pic_page.allocated = 0;
    }

    /* 释放图标数据 */ 
    destroy_icon_pixel_datas(&view_pic_page,icon_pixel_datas,ICON_NUMS);

    /* 释放图片缓存 */
    destroy_pic_caches();

    /* 释放目录信息 */
    free_dir_contents(cur_dir_pic_contents,cur_dir_pic_nums);
    if(!is_from_browse){
        free_dir_contents(cur_dir_contents,cur_dir_nums);
    }
    free(cur_dir);
    cur_dir = NULL;
    cur_dir_pic_contents = NULL;
    cur_dir_pic_nums = 0;
    cur_pic_index = 0;
    cur_gif_file = NULL;
    cur_dir_contents = NULL;
    cur_dir_nums = 0;
    cur_file_index = 0;
    initial_file_index = 0;
    pic_dir_generated = 0;
}

static int fill_menu_icon_area(struct page_struct *view_pic_page)
{
    int i,ret;
    struct page_region *regions = view_pic_page->page_layout.regions;

    if(!view_pic_page->icon_prepared)
        prepare_icon_pixel_datas(view_pic_page,icon_pixel_datas,icon_file_names,icon_region_links,ICON_NUMS);

    for(i = 0 ; i < REGION_MAIN_PIC ; i++){
        /* 没有数据直接跳过 */
        if(!icon_pixel_datas[i].buf){
            continue;
        }
        clear_pixel_data(regions[i].pixel_data,BACKGROUND_COLOR);
        ret = merge_pixel_data(regions[i].pixel_data,&icon_pixel_datas[i]);
        if(ret < 0){
            DP_ERR("%s:merge_pixel_data failed!\n",__func__);
            return ret;
        }
    }
    return 0;
}

static int fill_unfolded_menu_icon_area(struct page_struct *page)
{
    int i,ret;
    struct page_region *regions = page->page_layout.regions;
    
    for(i = 0 ; i <= (REGION_MENU_HV_SCREEN - REGION_MENU_ZOOM_IN) ; i++){
        /* 没有数据直接跳过 */
        if(!icon_pixel_datas[ICON_MENU_ZOOM_IN + i].buf){
            continue;
        }
        ret = merge_pixel_data(regions[REGION_MENU_ZOOM_IN + i].pixel_data,&icon_pixel_datas[ICON_MENU_ZOOM_IN + i]);
        if(ret < 0){
            DP_ERR("%s:merge_pixel_data failed!\n",__func__);
            return ret;
        }
    }
    clear_pixel_data(regions[REGION_MENU_UNFOLD].pixel_data,0xffff);
    merge_pixel_data(regions[REGION_MENU_UNFOLD].pixel_data,&icon_pixel_datas[ICON_MENU_FOLD]);
 
    return 0;
}

/* 其实这个函数要比想象中的复杂，它负责将图像缓存中的数据更新到页面对应的内存中，
 * 但关键的是，因为图像可以拖动，它要负责处理图片在显示区域中的位置问题，这是很麻烦的， */
static int fill_main_pic_area(struct page_struct *page)
{
    int i,j,ret;
    struct display_struct *default_display = get_default_display();
    struct page_region *main_pic_region = &page->page_layout.regions[REGION_MAIN_PIC];
    struct pic_cache *cur_pic = *(cur_pic_data);
    struct pixel_data *src_data,*dst_data;
    unsigned char *dst_line_buf,*src_line_buf;
    unsigned char src_red,src_green,src_blue,alpha;
    unsigned char dst_red,dst_green,dst_blue;
    unsigned char red,green,blue;
    unsigned char src_bpp;
    unsigned short color;
    short region_width,region_height;       //显示区域的整体宽高
    short x_disp,y_disp;                    //显示区域的左上角座标
    short disp_width,disp_height;           //实际显示图片区域的宽高
    short x_pic,y_pic;                      //图像中被显示的部分在图像内的左上角座标
    short x_vpic,y_vpic;                    //图像在虚拟空间中的左上角座标
    short pic_width,pic_height;             //图像的长宽

    dst_data = main_pic_region->pixel_data;
    src_data = &cur_pic->data;
    src_bpp = src_data->bpp;

    //如果一些条件不满足，快退出把，不要浪费时间了
    if(!page->region_mapped || !src_data->buf || dst_data->bpp != 16 || \
    (src_data->bpp != 16 && src_data->bpp != 24 && src_data->bpp != 32)){    
        printf("src_data->buf-%p\n",src_data->buf); 
        return -1;
    }
    
    /* 先清理该区域 */
    clear_pixel_data(dst_data,BACKGROUND_COLOR);

    /* 先解出显示相关的几个座标 */
    x_vpic = cur_pic->virtual_x;
    y_vpic = cur_pic->virtual_y;
    region_width  = main_pic_region->width;
    region_height = main_pic_region->height;
    pic_width = src_data->width;
    pic_height = src_data->height;
    /* 先解决x方向 */
    if(x_vpic < 0){
        if((x_vpic + pic_width - 1) < 0){
            return 0;           //图像已经超出显示区域了
        }else if((x_vpic + pic_width - 1) < region_width){
            x_disp = 0;
            disp_width = x_vpic + pic_width;
            x_pic = (-x_vpic);
        }else{
            x_disp = 0;
            disp_width = region_width;
            x_pic = (-x_vpic);
        }
    }else if(x_vpic < region_width){
        if((x_vpic + pic_width - 1) < region_width){
            x_pic = 0;
            disp_width = pic_width;
            x_disp = x_vpic;
        }else{
            x_pic = 0;
            disp_width = region_width - x_vpic;
            x_disp = x_vpic;
        }
    }else{
        return 0;           //图像已经超出显示区域了
    }
    /* 再解决y方向 */
    if(y_vpic < 0){
        if((y_vpic + pic_height - 1) < 0){
            return 0;           //图像已经超出显示区域了
        }else if((y_vpic + pic_height) < region_height){
            y_disp = 0;
            disp_height = y_vpic + pic_height - 1;
            y_pic = (-y_vpic);
        }else{
            y_disp = 0;
            disp_height = region_height;
            y_pic = (-y_vpic);
        }
    }else if(y_vpic < region_height){
        if((y_vpic + pic_height - 1) < region_height){
            y_pic = 0;
            disp_height = pic_height;
            y_disp = y_vpic;
        }else{
            y_pic = 0;
            disp_height = region_height - y_vpic;
            y_disp = y_vpic;
        }
    }else{
        return 0;           //图像已经超出显示区域了
    }

    /* 位置总算确定好了，开始描绘像素，希望能成功，阿弥陀佛 */
    /* 目标区域默认为16bpp，源数据区域可为16、24、32bpp，否则报错 */
    for(i = 0 ; i < disp_height ; i++){
        if(src_data->in_rows){
            src_line_buf = src_data->rows_buf[y_pic + i] + x_pic * src_bpp / 8;
        }else{
            src_line_buf = src_data->buf + src_data->line_bytes * (y_pic + i) + x_pic * src_bpp / 8;
        }
        if(dst_data->in_rows){
            dst_line_buf = dst_data->rows_buf[y_disp + i] + x_disp * 2;
        }else{
            dst_line_buf = dst_data->buf + dst_data->line_bytes * (y_disp + i) + x_disp * 2;
        }
        //根据不同bpp作不同处理
        switch(src_bpp){
            case 16:
                memcpy(dst_line_buf,src_line_buf,disp_width * 2);
                break;
            case 24:
                for(j = 0 ; j < disp_width ; j++){
                    /* 取出各颜色分量 */
                    red   = src_line_buf[j * 3] >> 3;
                    green = src_line_buf[j * 3 + 1] >> 2;
                    blue  = src_line_buf[j * 3 + 2] >> 3;
                    color = (red << 11) | (green << 5) | blue;

                    *(unsigned short *)dst_line_buf = color;
                    // *(unsigned short *)dst_line_buf = 0xff;
                    dst_line_buf += 2;
                }
                break;
            case 32:
                for(j = 0 ; j < disp_width ; j++){
                    /* 取出各颜色分量 */
                    alpha     = src_line_buf[j * 4];
                    src_red   = src_line_buf[j * 4 + 1] >> 3;
                    src_green = src_line_buf[j * 4 + 2] >> 2;
                    src_blue  = src_line_buf[j * 4 + 3] >> 3;

                    color = *(unsigned short *)dst_line_buf;
                    dst_red   = (color >> 11) & 0x1f;
                    dst_green = (color >> 5) & 0x3f;
                    dst_blue  = color & 0x1f;

                    /* 根据透明度合并颜色,公式:显示颜色 = 源像素颜色 X alpha / 255 + 背景颜色 X (255 - alpha) / 255 */
                    red   = (src_red * alpha) / 255 + (dst_red * (255 - alpha)) / 255;
                    green = (src_green * alpha) / 255 + (dst_green * (255 - alpha)) / 255;
                    blue  = (src_blue * alpha) / 255 + (dst_blue * (255 - alpha)) / 255;
                    color = (red << 11) | (green << 5) | blue;

                    *(unsigned short *)dst_line_buf = color;
                    // *(unsigned short *)dst_line_buf = 0xff;
                    dst_line_buf += 2;
                }
                break;
            default:
                return -1;
                break;
        }
    }

    /* 如果当前菜单是展开状态，将展开菜单填充上去 */
    if(menu_unfolded){
        ret = fill_unfolded_menu_icon_area(page);
        if(ret < 0){
            DP_ERR("%s:fill_unfolded_menu_icon_area failed!\n",__func__);
            return ret;
        }
    }
    
    return 0;
}

 /* 将主体显示区域中的变动更新到显存中 */
static void flush_main_pic_area(struct page_struct *page)
{
    struct display_struct *default_display = get_default_display();
    struct page_region *main_pic_region = &page->page_layout.regions[REGION_MAIN_PIC];

    flush_page_region(main_pic_region,default_display);
}

/* 一切准备好后,用于填充各区域 */
static int view_pic_page_fill_layout(struct page_struct *page)
{
    int ret;

    /* 如果想加个整体的背景，应该最先加进去 */
    //...
    DP_ERR("enter:%s\n",__func__);
    
    /* 检查菜单图标数据 */
    if(!page->icon_prepared){
        ret = prepare_icon_pixel_datas(page,icon_pixel_datas,icon_file_names,icon_region_links,ICON_NUMS);
        if(ret){
            DP_ERR("%s:prepare_icon_pixel_datas failed!\n",__func__);
            return -1;
        }
        page->icon_prepared = 1;
    }
    /* 填充菜单图标数据 */
    ret = fill_menu_icon_area(page);
    if(ret){
        DP_ERR("%s:fill_menu_icon_area failed!\n",__func__);
        return -1;
    }
    /* 填充主体图像显示区域 */
    pthread_mutex_lock(&view_pic_priv.gif_mutex);
    ret = fill_main_pic_area(page);
    if(ret){
        DP_ERR("%s:fill_main_pic_area failed!\n",__func__);
        return -1;
    }
    pthread_mutex_unlock(&view_pic_priv.gif_mutex);
    return 0;
}

/* 点击返回菜单时的回调函数,主要负责释放该函数分配的资源 */
static void return_menu_cb_func(void)
{
    /* 直接调用退出函数即可 */
    view_pic_page_exit();
}

/* 点击"自动播放"菜单时的回调函数 */
static int start_autoplay_cb_func(void)
{
    struct page_struct *autoplay_page = get_page_by_name("autoplay_page");
    struct autoplay_private *auto_priv = autoplay_page->private_data;
    struct page_param param;

    param.id = view_pic_page.id;

    
    /* 释放图片缓存 */
    destroy_pic_caches();
    
    /* 将当前的目录信息转存到自动连播页面中，以进行自动播放 */
    /* 分别将当前目录信息数组、数组项数、当前正浏览图片的索引保存到 view_pic 页面中 */
    auto_priv->autoplay_dirs[0] = (char *)cur_dir_pic_contents;
    auto_priv->autoplay_dirs[1] = (char *)cur_dir_pic_nums;
    auto_priv->autoplay_dirs[2] = (char *)cur_pic_index;
    auto_priv->autoplay_dirs[3] = (char *)cur_dir;
    autoplay_page->run(&param);

    return 0;
}

/* @description : 用于实现“上一张”、“下一张”回调函数功能的函数
 * @param - next_pic : 1 表示下一张，0 表示上一张  */
static int __pre_next_pic(int next_pic)
{
    struct pic_cache *temp;
    int i,j,ret;
    int is_gif = 0;
    int next_index,pre_index;
    char *gif_file;
    struct gif_thread_pool *thread_pool;
    struct dir_entry **new_dir_contents;

    /* 如果当前目录只有一张图，则什么也不做 */
    if(1 == cur_dir_pic_nums){
        return 0;
    }

    /* 更新文件索引 */
    if((next_index = cur_pic_index + 1) == cur_dir_pic_nums){
        next_index = 0;
    }
    while(!cur_dir_pic_contents[next_index]){
        if((++next_index) == cur_dir_pic_nums){
            next_index = 0;
        }
    }

    if((pre_index = cur_pic_index - 1) < 0){
        pre_index = cur_dir_pic_nums - 1;
    }
    while(!cur_dir_pic_contents[pre_index]){
        if((--pre_index) < 0){
            pre_index = cur_dir_pic_nums - 1;
        }
    }
    
    /* 更新索引 */
    if(next_pic){
        cur_pic_index = next_index;
    }else{
        cur_pic_index = pre_index;
    }
    
    /* 先判断要显示的下一张图是否为gif */
    if(cur_dir_pic_contents[cur_pic_index]->file_type == FILETYPE_FILE_GIF){
        is_gif = 1;
    }
    
    /* 只有两张图片的情况 */
    if(pre_index == next_index){
        if(is_gif){
            pthread_mutex_lock(&view_pic_priv.gif_mutex);
            if(!(gif_file = malloc(strlen(cur_dir) + 2 + strlen(cur_dir_pic_contents[cur_pic_index]->name)))){
                DP_ERR("%s:malloc failed!\n",__func__);
                return -ENOMEM;
            }
            sprintf(gif_file,"%s/%s",cur_dir,cur_dir_pic_contents[cur_pic_index]->name);
            if(cur_gif_file)
                free(cur_gif_file);
            cur_gif_file = gif_file; 
retry:      
            temp = pic_caches[1];
            pic_caches[1] = pic_caches[2];
            pic_caches[2] = pic_caches[0];
            pic_caches[0] = temp;
                
            if(!pic_caches[1])
                goto retry;

            fill_main_pic_area(&view_pic_page);
            flush_main_pic_area(&view_pic_page);
            pthread_mutex_unlock(&view_pic_priv.gif_mutex);
        }else{
            if(cur_gif_file){
                free(cur_gif_file);
                cur_gif_file = NULL;
            }
retry1:     
            temp = pic_caches[1];
            pic_caches[1] = pic_caches[2];
            pic_caches[2] = pic_caches[0];
            pic_caches[0] = temp;
                
            if(!pic_caches[1])
                goto retry1;
            fill_main_pic_area(&view_pic_page);
            flush_main_pic_area(&view_pic_page);
        }
        goto exit;
    }else{
        /* 当有三张及以上的图片 */
        /* 更新缓存，如果下一张是gif图，更新图片名字 */
        if(is_gif){
            pthread_mutex_lock(&view_pic_priv.gif_mutex);
            if(!(gif_file = malloc(strlen(cur_dir) + 2 +strlen(cur_dir_pic_contents[cur_pic_index]->name)))){
                DP_ERR("%s:malloc failed!\n",__func__);
                return -ENOMEM;
            }
            sprintf(gif_file,"%s/%s",cur_dir,cur_dir_pic_contents[cur_pic_index]->name);
            if(cur_gif_file)
                free(cur_gif_file);
            cur_gif_file = gif_file; 
            if(next_pic){
                temp = pic_caches[0];
                pic_caches[0] = pic_caches[1];
                pic_caches[1] = pic_caches[2];
                pic_caches[2] = NULL;
            }else{
                temp = pic_caches[2];
                pic_caches[2] = pic_caches[1];
                pic_caches[1] = pic_caches[0];
                pic_caches[0] = NULL;
            }
            
            /* 先显示图片，释放和缓存操作放在后面 */
            fill_main_pic_area(&view_pic_page);
            flush_main_pic_area(&view_pic_page);
            pthread_mutex_unlock(&view_pic_priv.gif_mutex);
        }else{
            if(cur_gif_file){
                pthread_mutex_lock(&view_pic_priv.gif_mutex);
                free(cur_gif_file);
                cur_gif_file = NULL;
                if(next_pic){
                    temp = pic_caches[0];
                    pic_caches[0] = pic_caches[1];
                    pic_caches[1] = pic_caches[2];
                    pic_caches[2] = NULL;
                }else{
                    temp = pic_caches[2];
                    pic_caches[2] = pic_caches[1];
                    pic_caches[1] = pic_caches[0];
                    pic_caches[0] = NULL;
                }
                /* 先显示图片，释放和缓存操作放在后面 */
                fill_main_pic_area(&view_pic_page);
                flush_main_pic_area(&view_pic_page);
                pthread_mutex_unlock(&view_pic_priv.gif_mutex);
            }else{
                if(next_pic){
                    temp = pic_caches[0];
                    pic_caches[0] = pic_caches[1];
                    pic_caches[1] = pic_caches[2];
                    pic_caches[2] = NULL;
                }else{
                    temp = pic_caches[2];
                    pic_caches[2] = pic_caches[1];
                    pic_caches[1] = pic_caches[0];
                    pic_caches[0] = NULL;
                }
                /* 先显示图片，释放和缓存操作放在后面 */
                fill_main_pic_area(&view_pic_page);
                flush_main_pic_area(&view_pic_page);
            }   
        }
    }
    
    /* 以某种方法启动新线程显示动图，此处原始数据指向线程池数据结构 */
    if(is_gif){
        thread_pool = (struct gif_thread_pool *)pic_caches[1]->gif_thread_pool;
retry3:    
        pthread_mutex_lock(&thread_pool->pool_mutex);
        if(thread_pool->idle_thread){
            for(i = 0 ; i < THREAD_NUMS ; i++){
                if(!thread_pool->thread_datas[i].submitted){
                    // thread_pool->thread_datas[i].file_name = cur_gif_file;
                    thread_pool->thread_datas[i].file_name = malloc(strlen(cur_gif_file));
                    strcpy(thread_pool->thread_datas[i].file_name,cur_gif_file);
                    thread_pool->thread_datas[i].submitted = 1;
                    break;
                }
            }
            pthread_cond_signal(&thread_pool->thread_cond);
            pthread_mutex_unlock(&thread_pool->pool_mutex);
        }else{
            pthread_mutex_unlock(&thread_pool->pool_mutex);
            pthread_mutex_lock(&thread_pool->task_mutex);
            thread_pool->task_wait = 1;
            pthread_cond_wait(&thread_pool->task_cond,&thread_pool->task_mutex);
            thread_pool->task_wait = 0;
            pthread_mutex_unlock(&thread_pool->task_mutex);
            goto retry3;
        }
    }
    
    if(next_pic){
        ret = reset_pic_cache_size(pic_caches[0],0);
    }else{
        ret = reset_pic_cache_size(pic_caches[2],0);
    }
    if(ret < 0){
        DP_ERR("%s:reset_pic_size failed!\n",__func__);
        return -1;
    }
     
    /* 获取要预读的下一张图片的索引 */
    i = cur_pic_index;
    if(next_pic){
        while(++i != cur_pic_index){
            if(i == cur_dir_nums && ((i = 0) == cur_pic_index))
                break;
            if(!cur_dir_contents[i]->file_type){
                cur_dir_contents[i]->file_type = get_file_type(cur_dir,cur_dir_contents[i]->name);
            }
            if(cur_dir_contents[i]->file_type >= FILETYPE_FILE_BMP && cur_dir_contents[i]->file_type <= FILETYPE_FILE_GIF){
                if(!cur_dir_pic_contents[i]){
                    if(NULL == (cur_dir_pic_contents[i] = malloc(sizeof(struct dir_entry)))){
                    DP_ERR("%s:malloc failed!\n",__func__);
                    return -ENOMEM;
                    }
                    memcpy(cur_dir_pic_contents[i],cur_dir_contents[i],sizeof(struct dir_entry));
                }
                break;
            }
        }
        next_index = i;
    }else{
        while(--i != cur_pic_index){
            if(i < 0 && ((i = cur_dir_nums - 1) == cur_pic_index))
                break;
            if(!cur_dir_contents[i]->file_type){
                cur_dir_contents[i]->file_type = get_file_type(cur_dir,cur_dir_contents[i]->name);
            }
            if(cur_dir_contents[i]->file_type >= FILETYPE_FILE_BMP && cur_dir_contents[i]->file_type <= FILETYPE_FILE_GIF){
                if(!cur_dir_pic_contents[i]){
                    if(NULL == (cur_dir_pic_contents[i] = malloc(sizeof(struct dir_entry)))){
                        DP_ERR("%s:malloc failed!\n",__func__);
                        return -ENOMEM;
                    }
                    memcpy(cur_dir_pic_contents[i],cur_dir_contents[i],sizeof(struct dir_entry));
                }
                break;
            }
        }
        pre_index = i;
    }
    
    /* 释放上一张的缓存 */
    if(next_index == pre_index){
        /* 只有三张图，无需读入新的缓存，轮换缓存指针即可 */
        if(next_pic){
            pic_caches[2] = temp;
        }else{
            pic_caches[0] = temp;
        }
        goto exit;
    }else{
        if(temp && temp->data.buf){
            free(temp->data.buf);
            free(temp);
        }
    }
    
    /* 读入下一张的缓存 */
    if(next_pic){
        ret = get_pic_cache_data(next_index,&pic_caches[2],0);
    }else{
        ret = get_pic_cache_data(pre_index,&pic_caches[0],0);
    }
    if(ret < 0){
        DP_ERR("%s:get_pic_cache_data failed!\n",__func__);
        return -ENOMEM;
    }
    
exit:
    /* 如果所有的图片目录信息都已经生成了，重新构建全局图片目录, */
    if(!pic_dir_completed){
        if(cur_pic_index == initial_file_index){
            cur_dir_pic_nums = 0;
            for(i = 0 ; i < cur_dir_nums ; i++){
                if(cur_dir_pic_contents[i])
                    cur_dir_pic_nums++;
            }
            if(cur_dir_pic_nums != cur_dir_nums){
                if(NULL == (new_dir_contents = malloc(sizeof(struct dir_entry *) * cur_dir_pic_nums))){
                    DP_WARNING("%s:malloc failed!\n",__func__);
                    cur_dir_pic_nums = cur_dir_nums;
                }else{
                    for(i = 0 ,j = 0 ; i < cur_dir_pic_nums ; i++){
                        if(cur_dir_pic_contents[i]){
                            new_dir_contents[j++] = cur_dir_pic_contents[i];
                        }
                    }
                    free(cur_dir_pic_contents);
                    cur_dir_pic_contents = new_dir_contents;
                    pic_dir_completed = 1;
                }    
            }
        }
    }
    
    return 0;
}

/* 点击"上一张"菜单时的回调函数 */
static int prepic_menu_cb_func(void)
{   
    return __pre_next_pic(0);
}

/* 点击"下一张"菜单时的回调函数 */
static int nextpic_menu_cb_func(void)
{
    return __pre_next_pic(1);
}

/* 点击"更多"菜单时的回调函数 */
static int unfold_menu_cb_func(void)
{
    int ret;
    struct page_region *regions = view_pic_page.page_layout.regions;
    struct display_struct *default_dsiplay = get_default_display();

    /* 如果当前是折叠状态 */
    if(!menu_unfolded){
        /* 将各个图标的数据复制到页面缓冲对应的位置中 */
        ret = fill_unfolded_menu_icon_area(&view_pic_page);
        if(ret < 0){
            DP_ERR("%s:fill_unfolded_menu_icon_area failed!\n",__func__);
            return ret;
        }
        /* 设置相应标志位 */
        menu_unfolded = 1;
        /* 将改动冲洗到缓存中 */
        flush_page_region(&regions[REGION_MENU_UNFOLD],default_dsiplay);
        flush_page_region(&regions[REGION_MENU_ZOOM_IN],default_dsiplay);
        flush_page_region(&regions[REGION_MENU_ZOOM_OUT],default_dsiplay);
        flush_page_region(&regions[REGION_MENU_LEFT_ROTATE],default_dsiplay);
        flush_page_region(&regions[REGION_MENU_RIGHT_ROTATE],default_dsiplay);
        flush_page_region(&regions[REGION_MENU_PIC_RESET],default_dsiplay);
    }else{
        menu_unfolded = 0;
        /* 如果已经是展开状态，重新显示主体图片，覆盖图标，并设置相应标志 */
        clear_pixel_data(regions[REGION_MENU_UNFOLD].pixel_data,0xffff);
        merge_pixel_data(regions[REGION_MENU_UNFOLD].pixel_data,&icon_pixel_datas[ICON_MENU_UNFOLD]);
        flush_page_region(&regions[REGION_MENU_UNFOLD],default_dsiplay);
        fill_main_pic_area(&view_pic_page);     //原有图像已被破坏，需要重新填充
        flush_main_pic_area(&view_pic_page);
        
    }
    return 0;
}

/* @description : 用于实现“放大”、“缩小”回调函数功能的函数
 * @param - zoomin : 1 表示放大，0 表示缩小  */
static int __zoom_in_out(int zoomin)
{
    static unsigned int min_width = 40;     //设置一个缩放的最小值
    static unsigned int min_height = 40;
    int ret;
    float scale;
    char *cur_file;
    unsigned int zoomed_width,zoomed_height;
    struct pic_cache *cur_pic = *cur_pic_data;
    struct pixel_data *pixel_data = &cur_pic->data;

    /* 先判断能不能缩小或放大 */
    if(zoomin){
        /* 先判断能不能放大，图像再怎么放大，也不会超过原图大小 */
        if((cur_pic->angle == 0 || cur_pic->angle == 180) && cur_pic->data.width >= cur_pic->orig_width){
            return 0;
        }else if((cur_pic->angle == 90 || cur_pic->angle == 270) && cur_pic->data.width >= cur_pic->orig_height){
            return 0;   //这是图片被旋转后再缩放的情况
        }
    }else{
        /* 小于指定值就不要再缩小了 */
        if(cur_pic->data.width <= min_width || cur_pic->data.height <= min_height){
            return 0;
        }
    }

    /* 先看看当前图片的原图数据是否存在，如果不存在则进行读取,即使是缩小也要求有原始数据，对于gif无需操作，因为线程会完成功能的 */
    if(!cur_pic->orig_data.buf && !cur_pic->is_gif){
        cur_file = malloc(strlen(cur_dir) + 2 + strlen(cur_dir_pic_contents[cur_pic_index]->name));
        if(!cur_file){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        sprintf(cur_file,"%s/%s",cur_dir,cur_dir_pic_contents[cur_pic_index]->name);
        
        memset(&cur_pic->orig_data,0,sizeof(struct pixel_data));
        ret = get_pic_pixel_data(cur_file,cur_dir_pic_contents[cur_pic_index]->file_type,&cur_pic->orig_data);
        if(ret < 0){
            DP_ERR("%s:get_pic_pixel_data failed!\n",__func__);
            return ret;
        }
        free(cur_file);
    }

    /* 计算缩放后的图像长宽及起始位置，缩放是以图片中心为原点的，而位置调整只需重新设置pic_cache中的虚拟座标，会有函数完成其他部分的 */
    scale = (float)(pixel_data->height) / pixel_data->width;
    if(zoomin){
        zoomed_width = pixel_data->width / ZOOM_RATE;
        zoomed_height = zoomed_width * scale;
        if(zoomed_width >= cur_pic->orig_width || zoomed_height >= cur_pic->orig_height){
            zoomed_width = cur_pic->orig_width;
            zoomed_height = cur_pic->orig_height;
        }
    }else{
        zoomed_width = pixel_data->width * ZOOM_RATE;
        zoomed_height = zoomed_width * scale; 
    }

    if(cur_pic->is_gif){
        /* 对于gif而言，改完这几个参数就可以直接退出了 */
        pthread_mutex_lock(&view_pic_priv.gif_mutex);
        if(zoomin){
            cur_pic->virtual_x -= ((zoomed_width - pixel_data->width) / 2);
            cur_pic->virtual_y -= ((zoomed_height - pixel_data->height) / 2);
        }else{
            cur_pic->virtual_x += ((pixel_data->width - zoomed_width) / 2);
            cur_pic->virtual_y += ((pixel_data->height - zoomed_height) / 2);
        }
         /* 释放原有数据 */
        if(pixel_data->buf){
            free(pixel_data->buf);
        }
        memset(pixel_data,0,sizeof(struct pixel_data));
        pixel_data->width = zoomed_width;
        pixel_data->height = zoomed_height;
        pthread_mutex_unlock(&view_pic_priv.gif_mutex);
        return 0;
    }else{
        if(zoomin){
            cur_pic->virtual_x -= ((zoomed_width - pixel_data->width) / 2);
            cur_pic->virtual_y -= ((zoomed_height - pixel_data->height) / 2);
        }else{
            cur_pic->virtual_x += ((pixel_data->width - zoomed_width) / 2);
            cur_pic->virtual_y += ((pixel_data->height - zoomed_height) / 2);
        }
         /* 释放原有数据 */
        if(pixel_data->buf){
            free(pixel_data->buf);
        }
        memset(pixel_data,0,sizeof(struct pixel_data));
        pixel_data->width = zoomed_width;
        pixel_data->height = zoomed_height;
    }
    
    /* 开始缩放 */
    ret = pic_zoom_with_same_bpp(pixel_data,&cur_pic->orig_data);
    if(ret < 0){
        DP_ERR("%s:pic_zoom_with_same_bpp failed!\n",__func__);
        free(pixel_data);
        return ret;
    }

    /* 将更改刷入缓存 */
    fill_main_pic_area(&view_pic_page);
    flush_main_pic_area(&view_pic_page);
    return 0;
}

/* 点击"放大"菜单时的回调函数 */
static int zoomin_menu_cb_func(void)
{
    return __zoom_in_out(1);
}

/* 点击"缩小"菜单时的回调函数 */
static int zoomout_menu_cb_func(void)
{
    return __zoom_in_out(0);
}

/* 更高级的实现，实现任意角度的旋转 */
/* @description : 用于实现“左旋”、“右旋”回调函数功能的
 * @param - left_rotate : 1 表示左旋，0 表示右旋  */
static int __left_right_rotate(int left_rotate)
{
    struct pic_cache *cur_pic = *cur_pic_data;
    struct pixel_data *pixel_data = &cur_pic->data;
    int ret,is_gif;
    unsigned short width,height;
    unsigned short rotated_width,rotated_height;
    unsigned short x_center,y_center;
    int angle = cur_pic->angle;

    /* 不管怎样，还是检查一下吧，内存问题真的怕 */
    if(!cur_pic->data.buf){
        return -1;
    }
    /* 这里就简单点考虑了，如果数据不是整块存储直接退出 */
    if(pixel_data->in_rows){
        return -1;
    }

    width = pixel_data->width;
    height = pixel_data->height;
    rotated_width = height;
    rotated_height = width;

    /* 修改虚拟起点，旋转是相对于图像中心进行的;计算好角度，然后直接调用函数就可以了 */
    x_center = (cur_pic->virtual_x + width) / 2;
    y_center = (cur_pic->virtual_y + height) / 2;
    if(left_rotate){
        if((angle -= 90) < 0)
            angle = 270;
    }else{ 
        if((angle += 90) >= 360)
            angle = 0;
    }

    if(is_gif){
        pthread_mutex_lock(&view_pic_priv.gif_mutex);
        cur_pic->virtual_x = x_center - rotated_width / 2;
        cur_pic->virtual_y = y_center -rotated_height / 2; 
        pixel_data->width = rotated_width;
        pixel_data->height = rotated_height;
        cur_pic->angle = angle;
        pthread_mutex_unlock(&view_pic_priv.gif_mutex);
        return 0;
    }

    cur_pic->virtual_x = x_center - rotated_width / 2;
    cur_pic->virtual_y = y_center -rotated_height / 2; 
    cur_pic->angle = angle;
    
    /* 开始变换 */
    if(pixel_data->buf)
        free(pixel_data->buf);
    pixel_data->width = rotated_width;
    pixel_data->height = rotated_height;

    ret = pic_zoom_with_same_bpp_and_rotate(pixel_data,&cur_pic->orig_data,angle);
    if(ret){
        DP_ERR("%s:pic_zoom_with_same_bpp_and_rotate failed!\n",__func__);
        return ret;
    }

    /* 将更改后的图像缓存刷入页面缓存，然后再刷入屏幕显存 */
    fill_main_pic_area(&view_pic_page);
    flush_main_pic_area(&view_pic_page);

    return 0;
}

/* 点击"左转"菜单时的回调函数 */
static int leftrotate_menu_cb_func(void)
{
   return __left_right_rotate(1);
}

/* 点击"右转"菜单时的回调函数 */
static int rightrotate_menu_cb_func(void)
{
    return __left_right_rotate(0);
}

/* 点击"重置"菜单时的回调函数 */
static int picreset_menu_cb_func(void)
{
    int ret;
    struct pic_cache *cur_pic = *cur_pic_data;

    if(cur_pic->is_gif){
        pthread_mutex_lock(&view_pic_priv.gif_mutex);
        ret = reset_pic_cache_size(cur_pic,1);
        pthread_mutex_unlock(&view_pic_priv.gif_mutex);
    }else{
        ret = reset_pic_cache_size(cur_pic,1);
    }
    
    if(ret < 0){
        DP_ERR("%s:reset_pic_cache_size failed!\n",__func__);
        return ret;
    }
    /* 将更改后的图像缓存刷入页面缓存，然后再刷入屏幕显存 */
    fill_main_pic_area(&view_pic_page);
    flush_main_pic_area(&view_pic_page);
    return 0;
}

/* 入口函数,主要功能：分配内存；解析要显示的数据；while循环检测输入*/
static int view_pic_page_run(struct page_param *pre_page_param)
{
    int ret;
    int i;
    short pre_region_index = -1;
    short region_index;
    short slot_id = -1;
    short pressure = 0;
    short x_pre_drag = 0;      /* 手指在屏幕上滑动时，前一个点的座标 */ 
    short y_pre_drag = 0;
    short x_offset,y_offset;
    unsigned short distance;
    struct display_struct *default_display = get_default_display();
    struct page_region *regions = view_pic_page.page_layout.regions;
    struct page_struct *temp_page;
    unsigned int pre_page_id = pre_page_param->id;
    DP_ERR("enter:%s\n",__func__);
    /* 为该页面分配一块内存 */
    if(!view_pic_page.allocated){
        /* 页面内存可以与显存使用同一块内存，但这样在显示gif图片时会有点问题 */
        view_pic_page.page_mem.bpp         = default_display->bpp;
        view_pic_page.page_mem.width       = default_display->xres;
        view_pic_page.page_mem.height      = default_display->yres;
        view_pic_page.page_mem.line_bytes  = view_pic_page.page_mem.width * view_pic_page.page_mem.bpp / 8;
        view_pic_page.page_mem.total_bytes = view_pic_page.page_mem.line_bytes * view_pic_page.page_mem.height; 
        // view_pic_page.page_mem.buf      = default_display->buf;
        view_pic_page.page_mem.buf         = malloc(view_pic_page.page_mem.total_bytes);
        if(!view_pic_page.page_mem.buf){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        view_pic_page.allocated            = 1;
        // view_pic_page.share_fbmem          = 1;
    }
    
    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!view_pic_page.region_mapped){
        ret = remap_regions_to_page_mem(&view_pic_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return -1;
        }
    }
    
    /* 获取目录信息，根据前一个页面的情况作不同处理 */
    if(!pic_dir_generated){
        /* 如果是从“浏览页面“进入此页面的，目录信息已经获取过了，直接提取出来 */
        if(pre_page_id == calc_page_id("browse_page")){
            is_from_browse = 1;
            cur_dir_contents = (struct dir_entry **)(((unsigned long *)pre_page_param->private_data)[0]);
            cur_dir_nums    = (unsigned int)(((unsigned long *)pre_page_param->private_data)[1]);
            cur_file_index  = (int)(((unsigned long *)pre_page_param->private_data)[2]);
            cur_dir = malloc(strlen((char *)(((unsigned long *)pre_page_param->private_data)[3])));
            strcpy(cur_dir,(char *)(((unsigned long *)pre_page_param->private_data)[3]));
        }else if(pre_page_id == calc_page_id("autoplay_page")){
            is_from_browse = 0;
            /* 根据传进来的文件名获取该文件所在目录下的图片信息，注意此时并不获取文件类型，因为如果现在获取的话，如果图片很多将很费时，在播放时动态获取 */
            char *temp;
            cur_dir = get_dir_name((char *)pre_page_param->private_data);
            temp = get_file_name((char *)pre_page_param->private_data);
            ret = get_dir_contents(cur_dir,&cur_dir_contents,&cur_dir_nums);
            if(ret < 0){
                DP_ERR("%s:get_pic_dir_contents failed!\n",__func__);
                return -1;
            }
            for(i = 0 ; i < cur_dir_nums ; i++){
                if(!strcmp(cur_dir_contents[i]->name,temp)){
                    i = cur_file_index;
                    break;
                }
            }
            free(temp);
        }

        free(pre_page_param->private_data);
        if(NULL == (cur_dir_pic_contents = malloc(sizeof(struct dir_entry *) * cur_dir_nums))){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        memset(cur_dir_pic_contents,0,sizeof(struct dir_entry *) * cur_dir_nums);
        cur_pic_index = cur_file_index;
        cur_dir_pic_nums = cur_dir_nums;
        if(NULL == (cur_dir_pic_contents[cur_pic_index] = malloc(sizeof(struct dir_entry)))){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        memcpy(cur_dir_pic_contents[cur_pic_index],cur_dir_contents[cur_file_index],sizeof(struct dir_entry));
        
        /* 至少要算出当前查看的文件前后两个图片文件，如果有的话 */
        i = cur_file_index;
        while(++i != cur_file_index){
            if(i == cur_dir_nums && ((i = 0) == cur_file_index))
                break;
            if(!cur_dir_contents[i]->file_type){
                cur_dir_contents[i]->file_type = get_file_type(cur_dir,cur_dir_contents[i]->name);
            }
            if(cur_dir_contents[i]->file_type >= FILETYPE_FILE_BMP && cur_dir_contents[i]->file_type <= FILETYPE_FILE_GIF){
                if(NULL == (cur_dir_pic_contents[i] = malloc(sizeof(struct dir_entry)))){
                    DP_ERR("%s:malloc failed!\n",__func__);
                    return -ENOMEM;
                }
                memcpy(cur_dir_pic_contents[i],cur_dir_contents[i],sizeof(struct dir_entry));
                break;
            }
        }
        
        
        i = cur_file_index;
        while(--i != cur_file_index){
            if(i < 0 && ((i = cur_dir_nums - 1) == cur_file_index))
                break;
            if(!cur_dir_contents[i]->file_type){
                cur_dir_contents[i]->file_type = get_file_type(cur_dir,cur_dir_contents[i]->name);
            }
            if(cur_dir_contents[i]->file_type >= FILETYPE_FILE_BMP && cur_dir_contents[i]->file_type <= FILETYPE_FILE_GIF){
                if(NULL == (cur_dir_pic_contents[i] = malloc(sizeof(struct dir_entry)))){
                    DP_ERR("%s:malloc failed!\n",__func__);
                    return -ENOMEM;
                }
                memcpy(cur_dir_pic_contents[i],cur_dir_contents[i],sizeof(struct dir_entry));
                break;
            }
        }
        pic_dir_generated = 1;
    }
    
    /* 如果当前点击的是gif图片，将文件名保存起来，此操作很关键，因为就是通过判断此文件名是否为空来决定是否启动gif线程 */
    if(cur_dir_pic_contents[cur_pic_index]->file_type == FILETYPE_FILE_GIF){
        if(!(cur_gif_file = malloc(strlen(cur_dir) + 2 + strlen(cur_dir_pic_contents[cur_pic_index]->name)))){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        sprintf(cur_gif_file,"%s/%s",cur_dir,cur_dir_pic_contents[cur_pic_index]->name);
        printf("%s-%d-cur_gif_file:%s\n",__func__,__LINE__,cur_gif_file);
    }
    
    /* 准备主体的图像数据缓存，该函数使用全局变量 */
    ret = generate_pic_caches(); 
    if(ret < 0){
        DP_ERR("%s:generate_pic_caches failed!\n",__func__);
        return -1;
    }
    
    /* 填充各区域 */
    ret = view_pic_page_fill_layout(&view_pic_page);
    if(ret){
        DP_ERR("%s:view_pic_page_fill_layout failed!\n",__func__);
        return -1;
    }   

    default_display = get_default_display();
    default_display->flush_buf(default_display,view_pic_page.page_mem.buf,view_pic_page.page_mem.total_bytes);

    /* 检测输入事件的循环 */
    while(1){
        struct my_input_event event;
        region_index = get_input_event_for_page(&view_pic_page,&event);
        DP_ERR("region_index:%d!\n",region_index);
        /* 触摸屏支持多触点事件,这里暂时只响应单个触点 */
        /* 后面希望能实现两指缩放功能 */
        if(-1 == slot_id){
            slot_id = event.slot_id;
        }else if(slot_id != event.slot_id){
            continue;
        }
        //只处理特定区域内的事件
        if(region_index < 0 || (!menu_unfolded && region_index >= REGION_MENU_ZOOM_IN && region_index <= REGION_MENU_PIC_RESET )){
            if(!event.presssure && (-1 != pre_region_index)){
                invert_region(regions[pre_region_index].pixel_data);
                flush_page_region(&regions[pre_region_index],default_display);
                pre_region_index = -1;
                pressure = 0;
                slot_id = -1;
            }
            continue;           
        }
        if(event.presssure){                //按下
            if(0 == pressure){     //还未曾有按钮按下   
                pre_region_index = region_index;DP_ERR("pre_region_index:%d!\n",pre_region_index);
                pressure = 1;
                x_pre_drag = 0;
                y_pre_drag = 0;
                 /* 反转按下区域的颜色 */
                if(region_index != 5){
                    invert_region(regions[region_index].pixel_data);
                    flush_page_region(&regions[region_index],default_display);  
                }  
            }
            if(REGION_MAIN_PIC == region_index){    //该需区域可响应连续的按下时间，也就是滑动
                if(!x_pre_drag && !y_pre_drag){     //首次按下
                    x_pre_drag = event.x_pos;
                    y_pre_drag = event.y_pos;
                }else{                              //后续的拖动点
                    /* 距离按平方算，节省时间 */
                    distance = (event.y_pos - y_pre_drag) * (event.y_pos - y_pre_drag) + \
                     (event.x_pos - x_pre_drag) * (event.x_pos - x_pre_drag);
                    if(distance > min_drag_distance){   /* 如果距离大于最小值，则进行响应 */
                        x_offset = event.x_pos - x_pre_drag;
                        y_offset = event.y_pos - y_pre_drag;
                        (*cur_pic_data)->virtual_x += x_offset;
                        (*cur_pic_data)->virtual_y += y_offset;
                        fill_main_pic_area(&view_pic_page);
                        flush_main_pic_area(&view_pic_page);
                        x_pre_drag = event.x_pos;
                        y_pre_drag = event.y_pos;
                    }
                }
            }
        }else{                  //松开
            if(!pressure) continue;
            if(region_index == REGION_MAIN_PIC){
                x_pre_drag = 0;
                y_pre_drag = 0;
            }
            /* 按下和松开的是同一个区域，这是一次有效的点击 */
            if(region_index == pre_region_index){
                if(region_index != 5){
                    invert_region(regions[region_index].pixel_data);
                    flush_page_region(&regions[region_index],default_display);  
                }
                switch (region_index){
                    case REGION_MENU_RETURN:             /* return */
                        return_menu_cb_func();
                        return 0;
                        break;
                    case REGION_MENU_START_AUTOPLAY:     /* autoplay */
                        start_autoplay_cb_func();
                        break;
                    case REGION_MENU_PRE_PIC:            /* pre_pic */
                        prepic_menu_cb_func();
                        break;
                    case REGION_MENU_NEXT_PIC:           /* next_pic */
                        nextpic_menu_cb_func();
                        break;
                    case REGION_MENU_UNFOLD:             /* unfolded */
                        unfold_menu_cb_func();
                        break;
                    case REGION_MENU_ZOOM_IN:
                        zoomin_menu_cb_func();
                        break;
                    case REGION_MENU_ZOOM_OUT: 
                        zoomout_menu_cb_func();           
                        break;
                    case REGION_MENU_LEFT_ROTATE:
                        leftrotate_menu_cb_func(); 
                        break;
                    case REGION_MENU_RIGHT_ROTATE:
                        rightrotate_menu_cb_func(); 
                        break;
                    case REGION_MENU_PIC_RESET:
                        picreset_menu_cb_func(); 
                        break;
                    case REGION_MAIN_PIC:           //主体图像显示区域
                        
                        break;
                    default:            /* 文件区域 */
                        
                        break;
                }
            }else{
                invert_region(regions[pre_region_index].pixel_data);
                flush_page_region(&regions[pre_region_index],default_display);
            }
            pressure = 0;
            slot_id = -1;
            pre_region_index = -1;
        }
    }   
    return 0;
}

static struct view_pic_private view_pic_priv = {
    .cur_gif_file = &cur_gif_file,
    .gif_mutex = PTHREAD_MUTEX_INITIALIZER,
    .pic_cache = &pic_caches[1],
    .gif_cache_mutex = PTHREAD_MUTEX_INITIALIZER,
    .fill_main_pic_area = fill_main_pic_area,
};

static struct page_struct view_pic_page = {
    .name = "view_pic_page",
    .init = view_pic_page_init,
    .exit = view_pic_page_exit,
    .run  = view_pic_page_run,
    .private_data = &view_pic_priv,
    .allocated = 0,
};

int view_pic_init(void)
{
    return register_page_struct(&view_pic_page);
}