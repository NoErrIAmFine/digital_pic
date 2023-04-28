#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <gif_lib.h>
#include <pthread.h>

#include "picfmt_manager.h"
#include "debug_manager.h"
#include "page_manager.h"

/* view_pic 页面用到的私有结构 */
struct view_pic_private
{
    char **cur_file_name;
    struct pic_cache *pic_cache;
    int (*fill_main_pic_area)(struct page_struct *page)
};

/* 一帧数据 */
struct gif_frame_data
{
    struct pixel_data data;
    struct gif_frame_data *next;
    int interval;                       /* 播放下一帧数据的间隔 */
};

/* 每个线程的管理数据结构，内含gif的各帧数据及间隔 */
struct gif_thread_data
{
    char *gif_file;                     /* 当前正打开的gif文件 */
    struct gif_frame_data *frame_data;  /* gif各帧数据 */
};

/* 线程数据结构，含3个线程 */
struct thread_pool
{
#define THREAD_NUMS 3
    pthread_t tids[THREAD_NUMS];
    struct gif_thread_data thread_datas[THREAD_NUMS];
    char *new_task[THREAD_NUMS];                        /* 准备播放的gif文件名 */
    char *exit_task[THREAD_NUMS];                       /* 退出播放的gif文件名 */
    int idle_thread;                                    /* 空闲线程数 */
    pthread_mutex_t pool_mutex;
    pthread_cond_t thread_cond;                         /* 空闲的线程在此等待 */
    pthread_cond_t task_cond;                           /* 无空闲线程时，任务线程在此等待线程空闲 */
};

static struct thread_pool *thread_pool;
// extern pthread_mutex_t cur_file_mutex;

/* 用于同步的全局互斥量，在往页面内存写数据前要先获得此互斥量 */
// pthread_mutex_t gif_mutex = PTHREAD_MUTEX_INITIALIZER;
// pthread_mutex_t gif_exit_mutex = PTHREAD_MUTEX_INITIALIZER;

static int gif_get_pixel_data(const char *file_name,struct pixel_data *pixel_data)
{
    char *cur_file_name;
    GifFileType *gif_file;
    GifRowType *screen_buffer,gif_row_buf;
    ColorMapObject *color_map;
    GifColorType *color_map_entry;
	GifByteType *extension = NULL;
	GifRecordType recoder_type = UNDEFINED_RECORD_TYPE;
    int interlaced_offset[] = {0,4,2,1};  // The way Interlaced image should
	int interlaced_jumps[] = {8,8,4,2};   // be read - offsets and jumps...
    int err,i,j;
    int row_size,row,col,width,height;
    unsigned char *rgb_line_buf;
    struct page_struct *view_pic_page = get_page_by_name("view_pic_page");
    struct view_pic_private *view_pic_priv = view_pic_page->private_data;

    /* 获取 view_pic_page 页当前正显示的图片名字，以决定如何显示*/
    // cur_file_name = malloc(strlen(*(char **)view_pic_page->private_data) + 1);
    // if(!cur_file_name){
    //     DP_ERR("%s:malloc failed!\n",__func__);
    //     return -ENOMEM;
    // }
    // pthread_mutex_lock(&cur_file_mutex);
    // strcpy(cur_file_name,*view_pic_priv->cur_file_name);
    // pthread_mutex_unlock(&cur_file_mutex);

    /* 获取gif文件数据 */
    if ((gif_file = DGifOpenFileName(file_name,&err)) == NULL) {
        DP_ERR("%s:open gif file failed!\n",__func__);
        return err;
    }
    /* 给屏幕分配内存 */
    if ((screen_buffer = (GifRowType *)malloc(gif_file->SHeight * sizeof(GifRowType *))) == NULL)
        DP_ERR("%s:malloc failed.\n",__func__);
    
    /* 以背景色填充屏幕 */
    row_size = gif_file->SWidth * sizeof(GifPixelType);             /* Size in bytes one row.*/
    if((screen_buffer[0] = (GifRowType) malloc(row_size)) == NULL)  /* First row. */
       DP_ERR("%s:malloc failed.\n",__func__);
    
    for (i = 0; i < gif_file->SWidth; i++)                          /* Set its color to BackGround. */
        screen_buffer[0][i] = gif_file->SBackGroundColor;
    for (i = 1; i < gif_file->SHeight; i++) {
        /* Allocate the other rows, and set their color to background too: */
        if ((screen_buffer[i] = (GifRowType) malloc(row_size)) == NULL)
            DP_ERR("%s:malloc failed.\n",__func__);
        
        memcpy(screen_buffer[i], screen_buffer[0], row_size);
    }

    /* 获取第一帧图像 */
	do
	{
		if(DGifGetRecordType(gif_file,&recoder_type)==GIF_ERROR)
            break;
		
		switch(recoder_type)
		{
		case IMAGE_DESC_RECORD_TYPE:
			if(DGifGetImageDesc(gif_file)==GIF_ERROR)break;
            row     = gif_file->Image.Top;
			col     = gif_file->Image.Left;
			width   = gif_file->Image.Width;
			height  = gif_file->Image.Height;
            if(col + width > gif_file->SWidth || row + height > gif_file->SHeight){
                DP_ERR("%s:gif %s image 1 not confined to screen dimension\n",__func__,file_name);
                err = -1;
                goto release_screen_buffer;
            }
			if(gif_file->Image.Interlace){
				for (i = 0; i < 4; i++){
                    for (j = row + interlaced_offset[i]; j < row + height; j += interlaced_jumps[i]) {
                        if (DGifGetLine(gif_file, &screen_buffer[j][col], width) == GIF_ERROR) {
                            DP_ERR("%s:gif get line failed\n",__func__);
                            err = -1;
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
			if(color_map==NULL){
                DP_ERR("%s:Gif Image does not have a color_map\n",__func__);
                err = -1;
				goto release_screen_buffer;
			}

			/* 将数据转换为ARGB数据，保留透明度属性，即bpp为32 */
            if(pixel_data->buf)
                free(pixel_data->buf);
            memset(pixel_data,0,sizeof(pixel_data));
            pixel_data->width = gif_file->SWidth;
            pixel_data->height = gif_file->SHeight;
            pixel_data->bpp = 32;
            pixel_data->has_alpha = 1;
            pixel_data->line_bytes = pixel_data->width * pixel_data->bpp / 2;
            pixel_data->total_bytes = pixel_data->line_bytes * pixel_data->height;
            if((pixel_data->buf = malloc(pixel_data->total_bytes)) == NULL){
                DP_ERR("%s:malloc failed\n",__func__);
                err = -ENOMEM;
				goto release_screen_buffer;
            }
            for(i = 0 ; i < gif_file->SHeight ; i++){
                gif_row_buf = screen_buffer[i];
                rgb_line_buf = pixel_data->total_bytes + i * pixel_data->line_bytes;
                for(j = 0 ; j < gif_file->SWidth ; j++){
                    color_map_entry = &color_map->Colors[gif_row_buf[j]];
                    *rgb_line_buf++ = 0xff;
                    *rgb_line_buf++ = color_map_entry->Blue;
                    *rgb_line_buf++ = color_map_entry->Green;
                    *rgb_line_buf++ = color_map_entry->Red;  
                }
            }
			goto exit_loop;
		case EXTENSION_RECORD_TYPE:
			/* 跳过文件中的所有扩展块*/
			if(DGifGetExtension(gif_file,&err,&extension)==GIF_ERROR)break;
			while(extension!=NULL){
				if(DGifGetExtensionNext(gif_file, &extension) == GIF_ERROR)break;
			}
			break;
			
		case TERMINATE_RECORD_TYPE:
			break;
		default:
				break;
		}
	}while(recoder_type != TERMINATE_RECORD_TYPE);

exit_loop:
    /* 如果当前获取的文件正是现在正在查看的，启动一个线程以更新动画 */
    cur_file_name = view_pic_priv->cur_file_name;
    if(!strcmp(cur_file_name,gif_file)){
        /* 最后检测一次看当前文件是否已被放入退出列表 */
        for(i = 0 ; i < 3 ; i++){

        }

    }
    /* 如果当前获取的文件不是现在正在查看,那么现在可以直接返回了 */

release_screen_buffer:
    for(i = 0 ; i < gif_file->SHeight ; i++){
       free(screen_buffer[i]);
    }
    free(screen_buffer);
    DGifCloseFile(gif_file,&err);

    return err;
}

static int gif_free_pixel_data(struct pixel_data *pixel_data)
{
    return 0;
}

static int is_support_gif(const char *file_name)
{
    GifFileType *gif_file;
    int err;

    gif_file = DGifOpenFileName(file_name,&err);

    /* 只有成功打开才视作是 gif 文件，否则一律认为不是 gif 文件 */
    if(gif_file){
        DGifCloseFile(gif_file,&err);
        return 1;
    }else{
        DGifCloseFile(gif_file,&err);
        return 0;
    }
    
}

static void *gif_thread_func(void *data)
{
    
}

static int gif_picfmt_init(void)
{
    int i,ret;

    /* 初始化线程池 */
    if((thread_pool = malloc(sizeof(thread_pool))) == NULL){
        DP_ERR("%s:malloc failed\n",__func__);
        return -ENOMEM;
    }
    memset(thread_pool,0,sizeof(thread_pool));

    /* 创建线程 */
    for(i = 0 ; i < THREAD_NUMS ; i++){
        if(ret = pthread_create(&thread_pool->tids[i],NULL,gif_thread_func,NULL)){
            DP_ERR("%s:create thread failed!\n",__func__);
            goto err;
        }
    }
    /* 初始化同步量 */
    if(pthread_cond_init(&thread_pool->task_cond,NULL) || pthread_cond_init(&thread_pool->thread_cond,NULL) ||
       pthread_mutex_init(&thread_pool->pool_mutex,NULL)){
        DP_ERR("%s:cond or mutex init failed!\n",__func__);
        ret = -1;
        goto err;
    }
    return 0;
err:
    free(thread_pool);
    return ret; 
}

static struct picfmt_parser gif_parser = {
    .name = "gif",
    .init               = gif_picfmt_init,
    .get_pixel_data     = gif_get_pixel_data,
    .free_pixel_data    = gif_free_pixel_data,
    .is_support         = is_support_gif,
    .is_enable = 1,
};

int gif_init(void)
{
    return register_picfmt_parser(&gif_parser);
}