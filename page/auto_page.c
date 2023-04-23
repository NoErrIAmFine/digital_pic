#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
// #include <linux/time.h>

#include "page_manager.h"
#include "display_manager.h"
#include "debug_manager.h"
#include "file.h"
#include "input_manager.h"
#include "render.h"
#include "pic_operation.h"

/* 自动连播页面 */

struct file_info 
{
    char *file_name;
    int file_type;
};

/* 此页面的私有结构，用于从其他页面接收连播目录和连播间隔信息 */
struct autoplay_private
{
    unsigned int autoplay_interval;
    const char **autoplay_dirs;
};

#define FILE_COUNT 10

static struct page_struct auto_page;

static pthread_t autoplay_thread_id;
static pthread_mutex_t autoplay_thread_mutex;
static int autoplay_thread_exit = 0;

/* 当前连播的文件信息 */
/* 当前要连播的目录，可选择多个 */
static const char **cur_dirs = NULL;
// static const char *cur_dir = NULL;
/* 已经预读到哪个目录以及该目录下的哪个文件，注意会预读若干文件名，这两个索引是指向已经预读的文件之后的 */
static int cur_dir_index;
static int file_index_in_dir;
/* 当前预读的文件信息数组 */
static struct file_info cur_files[FILE_COUNT];
static int cur_file_index;
static int cur_file_nums;

/* 图片缓存信息，缓存两张就行了：当前和下一张，而且不用保留原图，因为不会放大缩小之类的操作 */
static struct pixel_data pic_caches[2];

/* 初始化函数，在页面注册时调用，主要功能计算页面的布局，本页面的布局非常简单，只有一个区域*/
static int auto_page_init(void)
{
    int ret;
    struct display_struct *default_display = get_default_display();
    struct page_layout *page_layout = &auto_page.page_layout;
    struct page_region *regions;
    int width = default_display->xres;
    int height = default_display->yres;

    page_layout->width  = width;
    page_layout->height = height;

    /* 如果已经计算过布局，则直接退出 */
    if(auto_page.already_layout){
        return 0;
    }

    /* 此页面只含一个区域 */
    regions = malloc(sizeof(struct page_region));
    if(!regions){
        DP_ERR("%s:malloc failed\n",__func__);
        return -ENOMEM;
    }

    regions->name   = "main_region";
    regions->index  = 0;
    regions->level  = 0;
    regions->x_pos  = 0;
    regions->y_pos  = 0;
    regions->width  = width;
    regions->height = height;
    regions->page_layout = page_layout;
    page_layout->regions = regions;
    page_layout->region_num = 1;

    auto_page.already_layout = 1;
    return 0;
}

static void auto_page_exit(void)
{
    if(auto_page.allocated){
        free(auto_page.page_mem.buf);
        auto_page.allocated = 0;
    }
    if(auto_page.region_mapped){
        unmap_regions_to_page_mem(&auto_page);
    }
}

static int get_autoplay_file_infos(const char **cur_dirs,int *cur_dir_index,int *file_index_in_dir,
                                   struct file_info *cur_file_infos,int *cur_file_num)
{
    int ret;
    int file_type;
    const char *cur_dir;
    struct dirent **origin_dirents;
    int origin_dir_num,i,j;
    int file_num = 0;
    int round    = 0;
    int old_dir_index  = *cur_dir_index;
    int old_file_index = *file_index_in_dir;

    while(1){
        j = 0;
        cur_dir = cur_dirs[*cur_dir_index];
        origin_dir_num = scandir(cur_dir,&origin_dirents,NULL,alphasort);
        if(origin_dir_num < 0){
            DP_ERR("%s:scandir failed!\n",__func__);
            return origin_dir_num;
        }
        for(i = 0 ; i < origin_dir_num ; i++){
            if(is_reg_file(cur_dir,origin_dirents[i]->d_name)){
                /* 如果查找已经回绕，检查是否又回到了第一个开始查找 */
                if(round && (old_dir_index == *cur_dir_index) && (old_file_index == *file_index_in_dir)){
                    ret = 0;
                    goto free_origin_dirents;
                }
                file_type = get_file_type(cur_dir,origin_dirents[i]->d_name);
                if((file_type == FILETYPE_FILE_BMP) || (file_type == FILETYPE_FILE_JPEG) ||
                (file_type == FILETYPE_FILE_PNG) || (file_type == FILETYPE_FILE_GIF)){
                    /* 从该目录的下指定索引处的图片开始查找，因为前面的已经播放过了 */
                    if(j < *file_index_in_dir){
                        j++;
                        continue;
                    }
                    j++;
                    cur_file_infos[file_num].file_type = file_type;
                    if(cur_file_infos[file_num].file_name){
                        free(cur_file_infos[file_num].file_name);
                    }
                    cur_file_infos[file_num].file_name = malloc(strlen(cur_dir) + 2 + strlen(origin_dirents[i]->d_name));
                    if(!cur_file_infos[file_num].file_name){
                        DP_ERR("%s:malloc failed!\n",__func__);
                        ret = -ENOMEM;
                        goto free_origin_dirents;
                    }
                    sprintf(cur_file_infos[file_num].file_name,"%s/%s",cur_dir,origin_dirents[i]->d_name);
                    file_num++;
                    if(file_num >= FILE_COUNT){
                        *cur_file_num = file_num;
                        *file_index_in_dir = j;
                        ret = 0;
                        goto free_origin_dirents;
                    }
                }
            }
        }

        for(i = 0 ; i < origin_dir_num ; i++){
            free(origin_dirents[i]);
        }
        free(origin_dirents);

        /* 如果找完第一个目录未找到10个图片文件，且指定了多个目录，尝试读取其他目录 */
        if(cur_dirs[(*cur_dir_index) + 1]){
            (void)*cur_dir_index++;
            *file_index_in_dir = 0;
            continue;
        }else{
            /* 回绕第一个目录继续找 */
            *cur_dir_index = 0;
            *file_index_in_dir = 0;
            round = 1;      /* 标志位 */
            continue;
        }
    }
free_origin_dirents:
    for(i = 0 ; i < origin_dir_num ; i++){
        free(origin_dirents[i]);
    }
    free(origin_dirents);
    return ret;
}

static int resize_pic_pixel_data(struct pixel_data *data,int region_width,int region_height)
{
    int ret;
    float scale;
    unsigned int zoomed_width,zoomed_height;
    unsigned int orig_width,orig_height;
    struct pixel_data temp_data;

    if(!data->buf){
        return 0;              //没有原始图像数据，直接退出
    }

    orig_width = data->width;
    orig_height = data->height;
    scale = (float)orig_height / orig_width;
    memset(&temp_data,0,sizeof(struct pixel_data));

    /* 确定缩放后的图片大小 */
    if(region_width >= orig_width && region_height >= orig_height){
        /* 图片可完全显示，直接退出即可 */
        return 0;
    }else if(region_width < orig_width && region_height < orig_height){
        /* 先将宽度缩至允许的最大值 */
        zoomed_width = region_width;
        zoomed_height = scale * zoomed_width;
        if(zoomed_height > region_height){
            /* 还要继续缩小 */
            zoomed_height = region_height;
            zoomed_width = zoomed_height / scale;
        }
    }else if(region_width < orig_width){
        zoomed_width = region_width;
        zoomed_height = zoomed_width * scale;
    }else if(orig_height < orig_height){
        zoomed_height = orig_height;
        zoomed_width = zoomed_height / scale;
    }

    temp_data.width = zoomed_width;
    temp_data.height = zoomed_height;

    ret = pic_zoom_with_same_bpp(&temp_data,data);
    if(ret){
        DP_ERR("%s:pic_zoom_with_same_bpp failed！\n",__func__);
        return ret;
    }
    free(data->buf);
    *data = temp_data;
    return 0;
}

void *autoplay_thread_func(void *data)
{
    int exit;
    int ret;
    int retries;
    struct pixel_data resized_pixel_data;
    struct timespec pre_time;
    struct page_region *region = &auto_page.page_layout.regions[0];
    if(!cur_dirs){
        /* 数据没准备好，直接等退出把 */
        goto wait_exit;
    }
    while(1){
        /* 先判断是否要退出 */
        pthread_mutex_lock(&autoplay_thread_mutex);
        exit = autoplay_thread_exit;
        pthread_mutex_unlock(&autoplay_thread_mutex);
        if(exit)
            return NULL;

        /* 下一张图片的缓存准备好了吗，准备好了直接显示并更新缓存 */
        retries = 0;
        if(pic_caches[1].buf){
            free(pic_caches[0].buf);
            pic_caches[0] = pic_caches[1];
            memset(&pic_caches[1],0,sizeof(struct pixel_data));
            /* 显示图片 */
            merge_pixel_data_in_center(&auto_page.page_mem,&pic_caches[0]);
            /* 记录此时的时间 */
            clock_gettime(CLOCK_MONOTONIC,&pre_time);
            /* 更新缓存 */
retry1:
            if((cur_file_index += 1) >= cur_file_nums){
                cur_file_index = 0;
                cur_file_nums  = 0;
                get_autoplay_file_infos(cur_dirs,&cur_dir_index,&file_index_in_dir,cur_files,&cur_file_nums);
            }
            memset(&pic_caches[1],0,sizeof(struct pixel_data));
            ret = get_pic_pixel_data(cur_files[cur_file_index].file_name,cur_files[cur_file_index].file_type,&pic_caches[1]);
            if(ret && (retries < 5)){
                /* 如果失败读取下一张，最多尝试5次 */
                retries++;
                goto retry1;
            }
            /* 缩放到合适大小，能在屏幕上显示 */
            resize_pic_pixel_data(&pic_caches[1],region->pixel_data->width,region->pixel_data->height);
            /* 随眠到指定时间 */
        }else{
            if(cur_file_nums == 1 && pic_caches[0].buf){
                /* 只有一张图，直接等退出把 */
                goto wait_exit;
            }
            /* 此时为初始状态 */
retry2:
            memset(&pic_caches[0],0,sizeof(struct pixel_data));
            ret = get_pic_pixel_data(cur_files[cur_file_index].file_name,cur_files[cur_file_index].file_type,&pic_caches[0]);
            if (ret && (retries < 5)){
                if((cur_file_index += 1) >= cur_file_nums){
                    cur_file_index = 0;
                    cur_file_nums  = 0;
                    get_autoplay_file_infos(cur_dirs,&cur_dir_index,&file_index_in_dir,cur_files,&cur_file_nums);
                }
                retries++;
                goto retry2;
            }
            resize_pic_pixel_data(&pic_caches[0],region->pixel_data->width,region->pixel_data->height);
            /* 显示图片 */
            merge_pixel_data_in_center(&auto_page.page_mem,&pic_caches[0]);
            /* 记录此时的时间 */
            clock_gettime(CLOCK_MONOTONIC,&pre_time);
            /* 缓存下一张图 */
retry3:
            if((cur_file_index += 1) >= cur_file_nums){
                cur_file_index = 0;
                cur_file_nums  = 0;
                get_autoplay_file_infos(cur_dirs,&cur_dir_index,&file_index_in_dir,cur_files,&cur_file_nums);
            }
            memset(&pic_caches[1],0,sizeof(struct pixel_data));
            ret = get_pic_pixel_data(cur_files[cur_file_index].file_name,cur_files[cur_file_index].file_type,&pic_caches[1]);
            if(ret && (retries < 5)){
                /* 如果失败读取下一张，最多尝试5次 */
                retries++;
                goto retry3;
            }
            resize_pic_pixel_data(&pic_caches[1],region->pixel_data->width,region->pixel_data->height);

            /* 睡眠 */
        }
    }

wait_exit:
    while(1){
        pthread_mutex_lock(&autoplay_thread_mutex);
        exit = autoplay_thread_exit;
        pthread_mutex_unlock(&autoplay_thread_mutex);
        if(exit)
            return NULL;
    } 
}

static int auto_page_run(struct page_param *pre_param)
{
    int ret;
    int region_index;
    struct display_struct *default_display = get_default_display();
    struct page_param param;
    struct page_struct *next_page;

    if(!auto_page.already_layout){
        auto_page_init();
    }
    if(!auto_page.allocated){
        /* 直接将 auto page 对应的内存映射到显存上，省的多一道复制 */
        auto_page.page_mem.bpp         = default_display->bpp;
        auto_page.page_mem.width       = default_display->xres;
        auto_page.page_mem.height      = default_display->yres;
        auto_page.page_mem.line_bytes  = auto_page.page_mem.width * auto_page.page_mem.bpp / 8;
        auto_page.page_mem.total_bytes = auto_page.page_mem.line_bytes * auto_page.page_mem.height; 
        auto_page.page_mem.buf         = default_display->buf;
        auto_page.allocated            = 1;
    }
    
    /* 注意，页面布局在注册该页面时，在初始化函数中已经计算好了 */

    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!auto_page.region_mapped){
        ret = remap_regions_to_page_mem(&auto_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return -1;
        }
        auto_page.region_mapped = 1;
    }

    /* 获取要连播的图片文件信息数组 */
    if(!cur_dirs){
        /* 如果还没有指定要连播的目录，则进入“浏览页面”选择 */
        next_page = get_page_by_name("browse_page");
        if(!next_page){
            DP_ERR("%s:get browse page failed!\n",__func__);
            return -1;
        }
        cur_dir_index = 0;
        param.id = auto_page.id;
        param.private_data = &cur_dirs;
        next_page->run(&param);
    }
    ret = get_autoplay_file_infos(cur_dirs,&cur_dir_index,&file_index_in_dir,cur_files,&cur_file_nums);
    if(ret){
        DP_ERR("%s:get_autoplay_file_infos failed!\n",__func__);
        return -1;
    }

    /* 图片文件信息获取到后，从第一张开始显示,启动一个线程来连续显示图片*/
    cur_file_index = 0;
    pthread_create(&autoplay_thread_id,NULL,autoplay_thread_func,NULL);

    /* 当前线程等待触摸屏输入, 先做简单点: 如果点击了触摸屏, 让线程退出，
     * 更进一步的可以点击后直接进入图片查看页面 */
    while (1){
        struct my_input_event event;
        region_index = get_input_event_for_page(&auto_page,&event);
        if(!region_index){
            pthread_mutex_lock(&autoplay_thread_mutex);
            autoplay_thread_exit = 1;                   /* 线程函数检测到这个变量为1后会退出 */
            pthread_mutex_unlock(&autoplay_thread_mutex);
            pthread_join(autoplay_thread_id, NULL);     /* 等待子线程退出 */
            return 0;
        }
    }
    
}

static struct autoplay_private auto_priv = {
    .autoplay_interval = 3,             /* 默认间隔设为3s */
};

static struct page_struct auto_page = {
    .name = "autoplay_page",
    .init = auto_page_init,
    .exit = auto_page_exit,
    .run  = auto_page_run,
    .allocated = 0,
    .private_data = &auto_priv,
};

int autoplay_init(void)
{
    return register_page_struct(&auto_page);
}