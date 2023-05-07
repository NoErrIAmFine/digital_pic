#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <gif_lib.h>
// #include <linux/time.h>

#include "config.h"
#include "page_manager.h"
#include "display_manager.h"
#include "debug_manager.h"
#include "file.h"
#include "input_manager.h"
#include "render.h"
#include "pic_operation.h"
#include "picfmt_manager.h"

/* 自动连播页面 */

#define GIF_CONTROL_EXT_SIZE 0x4
#define GIF_CONTROL_EXT_CODE 0xf9

/* 用于保存要连播文件的文件信息 */
struct file_info 
{
    char *file_name;
    int file_type;
};

/* 用于gif图像的缓存结构 */
struct gif_frame_datas
{
    struct gif_frame_data *head;
    struct gif_frame_data *tail;
};

/* 当前页面要用到的缓存结构 */
union auto_page_cache
{
    struct pixel_data pixel_data;
    struct gif_frame_datas gif_datas;
};

static struct autoplay_private auto_priv;
static struct page_struct auto_page;

static pthread_t autoplay_thread_id;
static pthread_mutex_t autoplay_thread_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t autoplay_thread_cond = PTHREAD_COND_INITIALIZER;
static int autoplay_thread_exit = 0;

/* 当前连播的文件信息 */
/* 当前要连播的目录，可选择多个 */
#define MAX_DIR_COUNT 10
static char **cur_dirs;
static int cur_dir_nums;
/* 已经预读到哪个目录以及该目录下的哪个文件，注意会预读若干文件名，这两个索引是指向已经预读的文件之后的 */
static int cur_dir_index;
static int file_index_in_dir;
/* 当前预读的文件信息数组 */
#define MAX_FILE_COUNT 10
static struct file_info cur_files[MAX_FILE_COUNT];
static int cur_file_index;
static int cur_file_nums;

/* 下一张图片的缓存信息 */
static union auto_page_cache pic_cache;
static bool is_cache_generated = 0;
static bool is_gif;

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

    /* 将cur_dirs初始化为指向私有结构中的数组 */
    cur_dirs = auto_priv.autoplay_dirs;
    
    return 0;
}

static void auto_page_exit(void)
{
    int i;
    struct autoplay_private *auto_priv = auto_page.private_data;
    struct gif_frame_data *frame_data,*frame_temp;
    if(auto_page.allocated && (!auto_page.share_fbmem)){
        free(auto_page.page_mem.buf);
        auto_page.allocated = 0;
    }
    if(auto_page.region_mapped){
        unmap_regions_to_page_mem(&auto_page);
    }
    /* 清理缓存的文件名和文件夹名 */
    for(i = 0 ; i < MAX_FILE_COUNT ; i++){
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
    if(is_gif){
        frame_data = pic_cache.gif_datas.head;
        while(frame_data){
            if(frame_data->data.buf)
                free(frame_data->data.buf);
            frame_temp = frame_data->next;
            free(frame_data);
            frame_data = frame_temp;
        } 
    }else{
        if(pic_cache.pixel_data.buf)
            free(pic_cache.pixel_data.buf);
    }
    is_cache_generated = 0;
    memset(&pic_cache,0,sizeof(pic_cache));
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
                    if(j <= *file_index_in_dir){
                        j++;
                        continue;
                    }
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
                    if(file_num >= MAX_FILE_COUNT){
                        *cur_file_num = file_num;
                        *file_index_in_dir = j;
                        ret = 0;
                        goto free_origin_dirents;
                    }
                    j++;
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
        if(((*cur_dir_index) + 1) < cur_dir_nums){
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

void *autoplay_thread_func(void *data)
{
    int exit;
    int ret;
    int next_file_index;
    struct pixel_data resized_pixel_data,temp_data;
    time_t pre_time,cur_time;
    struct autoplay_private *auto_priv;
    int interval;
    bool cache_update_index = 0;          //表示缓存是否更新了文件索引信息
    struct page_region *region = &auto_page.page_layout.regions[0];
    struct display_struct *display = get_default_display();
    
    /* 与gif相关的变量 */
    GifFileType *gif_file;
    GifRowType *screen_buffer,gif_row_buf;
    ColorMapObject *color_map;
    GifColorType *color_map_entry;
	GifByteType *extension = NULL,*extension_temp = NULL;;
    GifByteType trans_color = -1;
    int ext_code,delay_ms = 0;
	GifRecordType recoder_type = UNDEFINED_RECORD_TYPE;
    struct gif_frame_data *frame_data,*frame_temp;
    int interlaced_offset[] = {0,4,2,1};  // The way Interlaced image should
	int interlaced_jumps[] = {8,8,4,2};   // be read - offsets and jumps...
    int i,j;
    int row_size,row,col,width,height;
    unsigned char *rgb_line_buf;

    if(!cur_dirs){
        /* 数据没准备好，直接等退出把 */
        goto wait_exit;
    }

    pthread_detach(pthread_self());

    auto_priv = auto_page.private_data;
    interval = auto_priv->autoplay_interval;    /* 连播间隔，单位为秒 */

    while(1){
        /* 先判断是否要退出 */
        pthread_mutex_lock(&autoplay_thread_mutex);
        exit = autoplay_thread_exit;
        pthread_mutex_unlock(&autoplay_thread_mutex);
        if(exit)
            return NULL;
        printf("cur_file_index-%d\n",cur_file_index);
        /* 如果有下一张图片的缓存的话，则直接显示 */
        if(is_cache_generated){
            /* 先更新文件索引信息 */
            if((cur_file_index += 1) >= cur_file_nums){printf("%s-%d\n",__func__,__LINE__);
                cur_file_index = 0;
                cur_file_nums  = 0;
                ret = get_autoplay_file_infos(cur_dirs,&cur_dir_index,&file_index_in_dir,cur_files,&cur_file_nums);
                if (ret) {
                    DP_ERR("%s:get_autoplay_file_infos failed!\n",__func__);
                    goto wait_exit;
                }
            }else if(cache_update_index && cur_file_index){
                cache_update_index = 0;
            }
            /* 先将缓存的图片显示出来 */
            if(is_gif){
                frame_data = pic_cache.gif_datas.head;
                clear_pixel_data(&auto_page.page_mem,BACKGROUND_COLOR);
                while(frame_data){
                    delay_ms = frame_data->delay_ms;
                    pthread_mutex_lock(&autoplay_thread_mutex);
                    if(!autoplay_thread_exit){
                        ret = adapt_pic_pixel_data(&auto_page.page_mem,&frame_data->data);
                        if(ret){
                            DP_WARNING("%s:adapt_pic_pixel_data failed!\n",__func__);
                        }
                        flush_page_region(region,display);
                    }else{
                        break;
                    }
                    pthread_mutex_unlock(&autoplay_thread_mutex);
                    usleep(delay_ms * 1000);
                    frame_data = frame_data->next;
                }
                /* 记录此刻的时间 */
                pre_time = time(NULL);

                /* 释放原有缓存 */
                frame_data = pic_cache.gif_datas.head;
                while(frame_data){
                    if(frame_data->data.buf)
                        free(frame_data->data.buf);
                    frame_temp = frame_data->next;
                    free(frame_data);
                    frame_data = frame_temp;
                }  
                pthread_mutex_lock(&autoplay_thread_mutex);
                if(!autoplay_thread_exit){
                    is_cache_generated = 0;
                    return NULL;
                } 
                pthread_mutex_unlock(&autoplay_thread_mutex);
            }else{printf("%s-%d\n",__func__,__LINE__);
                pthread_mutex_lock(&autoplay_thread_mutex);
                if(!autoplay_thread_exit){
                    clear_pixel_data(&auto_page.page_mem,BACKGROUND_COLOR);
                    ret = adapt_pic_pixel_data(&auto_page.page_mem,&pic_cache.pixel_data);
                    if(ret){
                        DP_WARNING("%s:adapt_pic_pixel_data failed!\n",__func__);
                    }
                    flush_page_region(region,display);
                }else{
                    /* 释放原有缓存 */
                    if(pic_cache.pixel_data.buf)
                        free(pic_cache.pixel_data.buf);
                    is_cache_generated = 0;
                    pthread_mutex_unlock(&autoplay_thread_mutex);
                    return NULL;
                }
                pthread_mutex_unlock(&autoplay_thread_mutex);
                /* 记录此刻的时间 */
                pre_time = time(NULL);   
            }

generate_next_cache:  
            memset(&pic_cache,0,sizeof(pic_cache));
            /* 获取下一张图片的索引 */
            if((next_file_index = cur_file_index + 1) >= cur_file_nums){printf("%s-%d\n",__func__,__LINE__);
                cur_file_index = 0;
                next_file_index = 0;
                cache_update_index = 1;
                ret = get_autoplay_file_infos(cur_dirs,&cur_dir_index,&file_index_in_dir,cur_files,&cur_file_nums);
                if (ret) {
                    DP_ERR("%s:get_autoplay_file_infos failed!\n",__func__);
                    goto wait_exit;
                }
            }

            /* 获取下一张图片的数据 */
            if(cur_files[next_file_index].file_type == FILETYPE_FILE_GIF){
                is_gif = 1;
                if ((gif_file = DGifOpenFileName(cur_files[next_file_index].file_name,&ret)) == NULL) {
                    DP_ERR("%s:open gif file failed!\n",__func__);
                    goto wait_exit;
                }
                printf("%s-%d\n",__func__,__LINE__);
                /* 给屏幕分配内存 */
                ret = -ENOMEM;
                if ((screen_buffer = (GifRowType *)malloc(gif_file->SHeight * sizeof(GifRowType *))) == NULL){
                    DP_ERR("%s:malloc failed.\n",__func__);
                    DGifCloseFile(gif_file,&ret);
                    goto wait_exit;
                }
     
                /* 以背景色填充屏幕 */
                row_size = gif_file->SWidth * sizeof(GifPixelType);                 /* Size in bytes one row.*/
                if((screen_buffer[0] = (GifRowType) malloc(row_size)) == NULL){     /* First row. */
                    DP_ERR("%s:malloc failed.\n",__func__);
                    DGifCloseFile(gif_file,&ret);
                    free(screen_buffer);
                    goto wait_exit;
                }
                
                for (i = 0; i < gif_file->SWidth; i++)                              /* Set its color to BackGround. */
                    screen_buffer[0][i] = gif_file->SBackGroundColor;
                for (i = 1; i < gif_file->SHeight; i++) {
                    /* Allocate the other rows, and set their color to background too: */
                    if ((screen_buffer[i] = (GifRowType) malloc(row_size)) == NULL){
                        DP_ERR("%s:malloc failed.\n",__func__);
                        DGifCloseFile(gif_file,&ret);
                        for( ; --i >=0 ; ){
                            free(screen_buffer[i]);
                        }
                        free(screen_buffer);
                        goto wait_exit;
                    }
                    memcpy(screen_buffer[i], screen_buffer[0], row_size);
                }
    
                /* 获取第一帧图像 */
                do{
                    if(DGifGetRecordType(gif_file,&recoder_type)==GIF_ERROR)
                        break;
                    
                    switch(recoder_type){
                    case IMAGE_DESC_RECORD_TYPE:
                        if(DGifGetImageDesc(gif_file)==GIF_ERROR){
                            ret = -1;
                            continue;
                        }
                        row     = gif_file->Image.Top;
                        col     = gif_file->Image.Left;
                        width   = gif_file->Image.Width;
                        height  = gif_file->Image.Height;
                        if(col + width > gif_file->SWidth || row + height > gif_file->SHeight){
                            DP_ERR("%s:gif %s image 1 not confined to screen dimension\n",__func__,cur_files[next_file_index].file_name);
                            ret = -1;
                            goto release_screen_buffer;
                        }
                        if(gif_file->Image.Interlace){
                            for (i = 0; i < 4; i++){
                                for (j = row + interlaced_offset[i]; j < row + height; j += interlaced_jumps[i]) {
                                    if (DGifGetLine(gif_file, &screen_buffer[j][col], width) == GIF_ERROR) {
                                        DP_ERR("%s:gif get line failed\n",__func__);
                                        ret = -1;
                                        goto release_screen_buffer;
                                    }
                                }
                            }
                        }else{
                            for(i = 0 ; i < height ; i++){
                                DGifGetLine(gif_file,&screen_buffer[row++][col],width);
                            }
                        }
                        color_map = (gif_file->Image.ColorMap ? gif_file->Image.ColorMap : gif_file->SColorMap);
                        if(color_map == NULL){
                            DP_ERR("%s:Gif Image does not have a color_map\n",__func__);
                            ret = -1;
                            goto release_screen_buffer;
                        }
                        
                        /* 将数据转换为RGB数据,bpp为24 */
                        if(NULL == (frame_data = malloc(sizeof(struct gif_frame_data)))){
                            DP_ERR("%s:malloc failed!\n",__func__);
                            ret = -ENOMEM;
                            goto release_screen_buffer;
                        }
                        memset(frame_data,0,sizeof(struct gif_frame_data));
                        frame_data->delay_ms = delay_ms;
                        delay_ms = 0;
                        frame_data->data.width = gif_file->SWidth;
                        frame_data->data.height = gif_file->SHeight;
                        frame_data->data.bpp = 24;
                        frame_data->data.line_bytes = frame_data->data.width * frame_data->data.bpp / 8;
                        frame_data->data.total_bytes = frame_data->data.line_bytes * frame_data->data.height;
                        if((frame_data->data.buf = malloc(frame_data->data.total_bytes)) == NULL){
                            DP_ERR("%s:malloc failed\n",__func__);
                            ret = -ENOMEM;
                            free(frame_data);
                            goto release_screen_buffer;
                        }
                        
                        for(i = 0 ; i < gif_file->SHeight ; i++){
                            gif_row_buf = screen_buffer[i];
                            rgb_line_buf = frame_data->data.buf + i * frame_data->data.line_bytes;
                            for(j = 0 ; j < gif_file->SWidth ; j++){
                                if( trans_color != -1 && trans_color == gif_row_buf[j] ) {
                                    rgb_line_buf += 3;
                                    continue;
                                }
                                color_map_entry = &color_map->Colors[gif_row_buf[j]];
                                // *rgb_line_buf++ = 0xff;
                                *rgb_line_buf++ = color_map_entry->Red;
                                *rgb_line_buf++ = color_map_entry->Green;
                                *rgb_line_buf++ = color_map_entry->Blue;            
                            }
                        }
                        
                        /* 将数据记录到缓存中 */
                        if(!pic_cache.gif_datas.head){
                            pic_cache.gif_datas.head = frame_data;
                            pic_cache.gif_datas.tail = frame_data;
                        }else{
                            pic_cache.gif_datas.tail->next = frame_data;
                            pic_cache.gif_datas.tail = frame_data;
                        }
                        break;
                    case EXTENSION_RECORD_TYPE:
                        /* 扩展块*/
                        if(DGifGetExtension(gif_file,&ext_code,&extension)==GIF_ERROR)
                            break;
                        while(extension != NULL){
                            extension_temp = extension;
                            if(DGifGetExtensionNext(gif_file, &extension) == GIF_ERROR)
                                break;
                        }
                        extension = extension_temp;
                        if( ext_code == GIF_CONTROL_EXT_CODE && extension[0] == GIF_CONTROL_EXT_SIZE) {
                            delay_ms = (extension[3] << 8 | extension[2]) * 10;
                        }
                        
                        /* handle transparent color */
                        if( (extension[1] & 1) == 1 ) {
                            trans_color = extension[4];
                        }else{
                            trans_color = -1;
                        }
                        break;
                    case TERMINATE_RECORD_TYPE:
                        break;
                    default:
                        break;
                    }
                }while(recoder_type != TERMINATE_RECORD_TYPE);
            }else{printf("%s-%d\n",__func__,__LINE__);
                is_gif = 0;
                ret = get_pic_pixel_data(cur_files[next_file_index].file_name,cur_files[next_file_index].file_type,&pic_cache.pixel_data);
                if(ret){
                   DP_ERR("%s:get_pic_pixel_data failed!\n",__func__);
                }
            }
            is_cache_generated = 1;

            /* 睡眠到指定时间 */
            cur_time = time(NULL);
            if(cur_time >= (pre_time + interval)){
                /* 已经超时了，不用睡眠了，直接返回 */
                continue;
            }else{
                sleep(pre_time + interval - cur_time);
            }
        }else{printf("%s-%d\n",__func__,__LINE__);
            /* 此时没有缓存，直接读取并显示当前图像 */
            if(cur_files[cur_file_index].file_type == FILETYPE_FILE_GIF){
                if ((gif_file = DGifOpenFileName(cur_files[next_file_index].file_name,&ret)) == NULL) {
                    DP_ERR("%s:open gif file failed!\n",__func__);
                    goto wait_exit;
                }
                printf("%s-%d\n",__func__,__LINE__);
                /* 给屏幕分配内存 */
                ret = -ENOMEM;
                if ((screen_buffer = (GifRowType *)malloc(gif_file->SHeight * sizeof(GifRowType *))) == NULL){
                    DP_ERR("%s:malloc failed.\n",__func__);
                    DGifCloseFile(gif_file,&ret);
                    goto wait_exit;
                }
                printf("%s-%d\n",__func__,__LINE__);
                /* 以背景色填充屏幕 */
                row_size = gif_file->SWidth * sizeof(GifPixelType);                 /* Size in bytes one row.*/
                if((screen_buffer[0] = (GifRowType) malloc(row_size)) == NULL){     /* First row. */
                    DP_ERR("%s:malloc failed.\n",__func__);
                    DGifCloseFile(gif_file,&ret);
                    free(screen_buffer);
                    goto wait_exit;
                }
                printf("%s-%d\n",__func__,__LINE__);
                for (i = 0; i < gif_file->SWidth; i++)                              /* Set its color to BackGround. */
                    screen_buffer[0][i] = gif_file->SBackGroundColor;
                for (i = 1; i < gif_file->SHeight; i++) {
                    /* Allocate the other rows, and set their color to background too: */
                    if ((screen_buffer[i] = (GifRowType) malloc(row_size)) == NULL){
                        DP_ERR("%s:malloc failed.\n",__func__);
                        DGifCloseFile(gif_file,&ret);
                        for( ; --i >=0 ; ){
                            free(screen_buffer[i]);
                        }
                        free(screen_buffer);
                        goto wait_exit;
                    }
                    memcpy(screen_buffer[i], screen_buffer[0], row_size);
                }
                printf("%s-%d\n",__func__,__LINE__);
                memset(&temp_data,0,sizeof(struct pixel_data));
                temp_data.width = gif_file->SWidth;
                temp_data.height = gif_file->SHeight;
                temp_data.bpp = 24;
                temp_data.line_bytes = temp_data.width * temp_data.bpp / 8;
                temp_data.total_bytes = temp_data.line_bytes * temp_data.height;
                if(NULL == (temp_data.buf = malloc(temp_data.total_bytes))){
                    DP_ERR("%s:malloc failed.\n",__func__);
                    goto release_screen_buffer;
                }
                clear_pixel_data(&auto_page.page_mem,BACKGROUND_COLOR);
                /* 获取第一帧图像 */
                do{
                    if(DGifGetRecordType(gif_file,&recoder_type)==GIF_ERROR)
                        break;
                    
                    switch(recoder_type){
                    case IMAGE_DESC_RECORD_TYPE:
                        if(DGifGetImageDesc(gif_file)==GIF_ERROR){
                            ret = -1;
                            continue;
                        }
                        row     = gif_file->Image.Top;
                        col     = gif_file->Image.Left;
                        width   = gif_file->Image.Width;
                        height  = gif_file->Image.Height;
                        if(col + width > gif_file->SWidth || row + height > gif_file->SHeight){
                            DP_ERR("%s:gif %s image 1 not confined to screen dimension\n",__func__,cur_files[next_file_index].file_name);
                            ret = -1;
                            goto release_screen_buffer;
                        }
                        if(gif_file->Image.Interlace){
                            for (i = 0; i < 4; i++){
                                for (j = row + interlaced_offset[i]; j < row + height; j += interlaced_jumps[i]) {
                                    if (DGifGetLine(gif_file, &screen_buffer[j][col], width) == GIF_ERROR) {
                                        DP_ERR("%s:gif get line failed\n",__func__);
                                        ret = -1;
                                        goto release_screen_buffer;
                                    }
                                }
                            }
                        }else{
                            for(i = 0 ; i < height ; i++){
                                DGifGetLine(gif_file,&screen_buffer[row++][col],width);
                            }
                        }
                        color_map = (gif_file->Image.ColorMap ? gif_file->Image.ColorMap : gif_file->SColorMap);
                        if(color_map == NULL){
                            DP_ERR("%s:Gif Image does not have a color_map\n",__func__);
                            ret = -1;
                            goto release_screen_buffer;
                        }
                        
                        /* 将数据转换为RGB数据,bpp为24 */
                        for(i = 0 ; i < gif_file->SHeight ; i++){
                            gif_row_buf = screen_buffer[i];
                            rgb_line_buf = temp_data.buf + i * temp_data.line_bytes;
                            for(j = 0 ; j < gif_file->SWidth ; j++){
                                if( trans_color != -1 && trans_color == gif_row_buf[j] ) {
                                    rgb_line_buf += 3;
                                    continue;
                                }
                                color_map_entry = &color_map->Colors[gif_row_buf[j]];
                                // *rgb_line_buf++ = 0xff;
                                *rgb_line_buf++ = color_map_entry->Red;
                                *rgb_line_buf++ = color_map_entry->Green;
                                *rgb_line_buf++ = color_map_entry->Blue;            
                            }
                        }
                        pthread_mutex_lock(&autoplay_thread_mutex);
                        if(!autoplay_thread_exit){
                            ret = adapt_pic_pixel_data(&auto_page.page_mem,&temp_data);
                            if(ret){
                                DP_WARNING("%s:adapt_pic_pixel_data failed!\n",__func__);
                            }
                            flush_page_region(region,display);
                        }else{
                            for(i = 0 ; i < gif_file->SHeight ; i++){
                                free(screen_buffer[i]);
                            }
                            free(screen_buffer);
                            DGifCloseFile(gif_file,&ret);
                            free(temp_data.buf);
                            pthread_mutex_unlock(&autoplay_thread_mutex);
                            return NULL;
                        }
                        pthread_mutex_unlock(&autoplay_thread_mutex);
                        
                        /* 睡眠 */
                        usleep(delay_ms * 1000);
                        break;
                    case EXTENSION_RECORD_TYPE:
                        /* 扩展块*/
                        if(DGifGetExtension(gif_file,&ext_code,&extension)==GIF_ERROR)
                            break;
                        while(extension != NULL){
                            extension_temp = extension;
                            if(DGifGetExtensionNext(gif_file, &extension) == GIF_ERROR)
                                break;
                        }
                        extension = extension_temp;
                        if( ext_code == GIF_CONTROL_EXT_CODE && extension[0] == GIF_CONTROL_EXT_SIZE) {
                            delay_ms = (extension[3] << 8 | extension[2]) * 10;
                        }
                        
                        /* handle transparent color */
                        if( (extension[1] & 1) == 1 ) {
                            trans_color = extension[4];
                        }else{
                            trans_color = -1;
                        }
                        break;
                    case TERMINATE_RECORD_TYPE:
                        break;
                    default:
                        break;
                    }
                }while(recoder_type != TERMINATE_RECORD_TYPE);
                /* 释放资源 */
                for(i = 0 ; i < gif_file->SHeight ; i++){
                    free(screen_buffer[i]);
                }printf("%s-%d\n",__func__,__LINE__);
                free(screen_buffer);
                DGifCloseFile(gif_file,&ret);
                free(temp_data.buf);
            }else{printf("%s-%d\n",__func__,__LINE__);
                memset(&temp_data,0,sizeof(struct pixel_data));
                get_pic_pixel_data(cur_files[cur_file_index].file_name,cur_files[cur_file_index].file_type,&temp_data);
                pthread_mutex_lock(&autoplay_thread_mutex);
                if(!autoplay_thread_exit){
                    adapt_pic_pixel_data(&auto_page.page_mem,&temp_data);
                    flush_page_region(region,display);
                    free(temp_data.buf);
                }else{
                    pthread_mutex_unlock(&autoplay_thread_mutex);
                    free(temp_data.buf);
                    return NULL;
                }
                pthread_mutex_unlock(&autoplay_thread_mutex);
            }
            
            /* 开始缓存下一张图的数据 */
            pre_time = time(NULL);
            goto generate_next_cache;
        }
    }

release_screen_buffer:
    for(i = 0 ; i < gif_file->SHeight ; i++){
       free(screen_buffer[i]);
    }
    free(screen_buffer);
    DGifCloseFile(gif_file,&ret);
wait_exit:
    while(1){
        pthread_mutex_lock(&autoplay_thread_mutex);
        exit = autoplay_thread_exit;
        if(exit)
            return NULL;
        else
            pthread_cond_wait(&autoplay_thread_cond,&autoplay_thread_mutex);
        pthread_mutex_unlock(&autoplay_thread_mutex);
    } 
}

static int auto_page_run(struct page_param *pre_param)
{
    int ret,i;
    int region_index;
    struct display_struct *default_display = get_default_display();
    struct page_param param;
    struct page_struct *next_page;
    struct page_struct *view_pic_page = get_page_by_name("view_pic_page"); 
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
    printf("%s-%d\n",__func__,__LINE__);
    /* 将划分的显示区域映射到相应的页面对应的内存中 */
    if(!auto_page.region_mapped){
        ret = remap_regions_to_page_mem(&auto_page);
        if(ret){
            DP_ERR("%s:remap_regions_to_page_mem failed!\n",__func__);
            return ret;
        }
    }
    printf("%s-%d\n",__func__,__LINE__);
    /* 获取要连播的图片文件信息数组 */
    if(view_pic_page->id == pre_param->id){
        /* 如果前一个页面是“图片浏览页面”，目录信息已经存进来了，提取出来即可 */
        struct dir_entry **pic_dir_contents = (struct dir_entry **)auto_priv->autoplay_dirs[0];
        int pic_dir_nums = (int)auto_priv->autoplay_dirs[1];
        int cur_pic_index = (int)auto_priv->autoplay_dirs[2];
        char * cur_dir = auto_priv->autoplay_dirs[3];
        char * file_name;
        int cur_dir_len = strlen(cur_dir);
        int temp = cur_pic_index;
        /* 从当前图片开始，取出10张图的文件信息 */
        for(i = 0 ; i < MAX_FILE_COUNT ; i++){
            /* 生成文件名 */
            file_name = malloc(cur_dir_len + 2 + strlen(pic_dir_contents[cur_pic_index]->name));
            if(!file_name){
                DP_ERR("%s:malloc failed!\n",__func__);
                return -ENOMEM;
            }
            sprintf(file_name,"%s/%s",cur_dir,pic_dir_contents[cur_pic_index]->name);

            cur_files[i].file_name = file_name;
            cur_files[i].file_type = pic_dir_contents[cur_pic_index]->file_type; 

            if((cur_pic_index += 1) >= pic_dir_nums){
                cur_pic_index = 0;
            }
            if(cur_pic_index == temp){
                break;
            }
        }
        if(NULL == (cur_dirs[0] = malloc(strlen(cur_dir)))){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
        strcpy(cur_dirs[0],cur_dir);
        cur_dir_nums = 1;
        cur_dir_index = 0;
        file_index_in_dir = temp;
        cur_file_nums = pic_dir_nums;
        cur_file_index = temp;
    }else{
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
            cur_dir_nums = auto_priv->autoplay_dir_num;
        }
        ret = get_autoplay_file_infos(cur_dirs,&cur_dir_index,&file_index_in_dir,cur_files,&cur_file_nums);
        if(ret){
            DP_ERR("%s:get_autoplay_file_infos failed!\n",__func__);
            return -1;
        }
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
            pthread_cond_signal(&autoplay_thread_cond);
            pthread_mutex_unlock(&autoplay_thread_mutex);
            
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