#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
// #include <linux/time.h>

#include "config.h"
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

static struct page_struct auto_page;

static pthread_t autoplay_thread_id;
static pthread_mutex_t autoplay_thread_mutex;
static int autoplay_thread_exit = 0;

/* 当前连播的文件信息 */
/* 当前要连播的目录，可选择多个 */
static char **cur_dirs = NULL;
static int cur_dir_nums;
/* 已经预读到哪个目录以及该目录下的哪个文件，注意会预读若干文件名，这两个索引是指向已经预读的文件之后的 */
static int cur_dir_index;
static int file_index_in_dir;
/* 当前预读的文件信息数组 */
#define FILE_COUNT 10
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

    regions->index  = 0;
    regions->level  = 0;
    regions->x_pos  = 0;
    regions->y_pos  = 0;
    regions->width  = width;
    regions->height = height;
    regions->owner_page = &auto_page;
    page_layout->regions = regions;
    page_layout->region_num = 1;

    auto_page.already_layout = 1;
    return 0;
}

static void auto_page_exit(void)
{
    int i;
    struct autoplay_private *auto_priv = auto_page.private_data;

    if(auto_page.allocated && (!auto_page.share_fbmem)){
        free(auto_page.page_mem.buf);
        auto_page.allocated = 0;
    }
    if(auto_page.region_mapped){
        unmap_regions_to_page_mem(&auto_page);
    }
    /* 清理缓存的文件名和文件夹名 */
    for(i = 0 ; i < FILE_COUNT ; i++){
        if(cur_files[i].file_name){
            free(cur_files[i].file_name);
        }
        if(auto_priv->autoplay_dirs[i]){
            free(auto_priv->autoplay_dirs[i]);
        }
    }
    memset(auto_priv,0,sizeof(*auto_priv));
    memset(cur_files,0,sizeof(cur_files));
    cur_file_index = 0;
    cur_file_nums = 0;
    cur_dirs = NULL;
    cur_dir_index = 0;
    cur_dir_nums = 0;
    /* 清理缓存的图片数据 */
    if(pic_caches[0].buf)
        free(pic_caches[0].buf);
    if(pic_caches[1].buf)
        free(pic_caches[1].buf);
    memset(pic_caches,0,sizeof(pic_caches));
}

static int get_autoplay_file_infos(char **cur_dirs,int *cur_dir_index,int *file_index_in_dir,
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
            if(origin_dirents[i])
                free(origin_dirents[i]);
        }
        if(origin_dirents){
            free(origin_dirents);
            origin_dirents = NULL;
        }
        
        /* 如果找完第一个目录未找到10个图片文件，且指定了多个目录，尝试读取其他目录 */
        if(++(*cur_dir_index) < cur_dir_nums){
            (void)(*cur_dir_index)++;
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
        if(origin_dirents[i])
            free(origin_dirents[i]);
    }
    if(origin_dirents){
        free(origin_dirents);
        origin_dirents = NULL;
    }
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
    time_t pre_time,cur_time;
    struct autoplay_private *auto_priv;
    int interval;
    struct page_region *region = &auto_page.page_layout.regions[0];
    if(!cur_dirs){
        /* 数据没准备好，直接等退出把 */
        goto wait_exit;
    }
    
    auto_priv = auto_page.private_data;
    interval = auto_priv->autoplay_interval;    /* 连播间隔，单位为秒 */

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
            clear_pixel_data(&auto_page.page_mem,BACKGROUND_COLOR);
            merge_pixel_data_in_center(&auto_page.page_mem,&pic_caches[0]);
            /* 记录此时的时间 */
            pre_time = time(NULL);
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
            /* 缩放到能在屏幕上显示的大小 */
            resize_pic_pixel_data(&pic_caches[1],region->pixel_data->width,region->pixel_data->height);

            /* 随眠到指定时间 */
            cur_time = time(NULL);
            if(cur_time >= (pre_time + interval)){
                /* 已经超时了，不用睡眠了，直接返回 */
                continue;
            }else{
                sleep(pre_time + interval - cur_time);
            }

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
            clear_pixel_data(&auto_page.page_mem,BACKGROUND_COLOR);
            merge_pixel_data_in_center(&auto_page.page_mem,&pic_caches[0]);
            /* 记录此时的时间 */
            pre_time = time(NULL);
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
            cur_time = time(NULL);
            if(cur_time >= (pre_time + interval)){
                /* 已经超时了，不用睡眠了，直接返回 */
                continue;
            }else{
                sleep(pre_time + interval - cur_time);
            }
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
    struct autoplay_private *auto_priv = auto_page.private_data;
    
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
        auto_page.share_fbmem          = 1;
    }

    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!auto_page.region_mapped){
        ret = remap_regions_to_page_mem(&auto_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return ret;
        }
    }

    /* 获取要连播的图片文件信息数组 */
    if(!auto_priv->autoplay_dir_num){
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
    }else{
        cur_dir_index = 0;
        cur_dirs = auto_priv->autoplay_dirs;
        cur_dir_nums = auto_priv->autoplay_dir_num;printf("%s-cur_dir_nums%d\n",__func__,cur_dir_nums);
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
            /* 进入图片浏览页面 */
            next_page = get_page_by_name("view_pic_page");
            param.id = calc_page_id("autoplay_page");
            param.private_data = cur_files[cur_file_index].file_name;
            cur_files[cur_file_index].file_name = NULL;
            /* 清理该页面用到的资源 */
            auto_page_exit();
            /* 进入“查看图片”页面 */
            next_page->run(&param);
            return 0;
        }
    }
    
}

static struct autoplay_private auto_priv = {
    .autoplay_interval = 3,             /* 默认间隔设为3s */
    .autoplay_dirs = {0},
    .autoplay_dir_num = 0,
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