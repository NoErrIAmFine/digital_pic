#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <gif_lib.h>
#include <pthread.h>
#include <unistd.h>

#include "picfmt_manager.h"
#include "pic_operation.h"
#include "debug_manager.h"
#include "page_manager.h"
#include "render.h"

#define GIF_CONTROL_EXT_SIZE 0x4
#define GIF_CONTROL_EXT_CODE 0xf9

static struct gif_thread_pool *thread_pool;
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
	GifByteType *extension = NULL,*extension_temp = NULL;;
    GifByteType trans_color = -1;
    int ext_code,delay_ms = 0;
	GifRecordType recoder_type = UNDEFINED_RECORD_TYPE;
    int interlaced_offset[] = {0,4,2,1};  // The way Interlaced image should
	int interlaced_jumps[] = {8,8,4,2};   // be read - offsets and jumps...
    int err,i,j;
    int row_size,row,col,width,height;
    unsigned char *rgb_line_buf;
    struct page_struct *view_pic_page = get_page_by_name("view_pic_page");
    struct view_pic_private *view_pic_priv = view_pic_page->private_data;
    struct gif_frame_data *frame_data;
    
    /* 获取gif文件数据 */
    if ((gif_file = DGifOpenFileName(file_name,&err)) == NULL) {
        DP_ERR("%s:open gif file failed!\n",__func__);
        return err;
    }
    
    /* 给屏幕分配内存 */
    err = -ENOMEM;
    if ((screen_buffer = (GifRowType *)malloc(gif_file->SHeight * sizeof(GifRowType *))) == NULL){
        DP_ERR("%s:malloc failed.\n",__func__);
        DGifCloseFile(gif_file,&err);
        return err;
    }
     
    /* 以背景色填充屏幕 */
    row_size = gif_file->SWidth * sizeof(GifPixelType);                 /* Size in bytes one row.*/
    if((screen_buffer[0] = (GifRowType) malloc(row_size)) == NULL){     /* First row. */
       DP_ERR("%s:malloc failed.\n",__func__);
       DGifCloseFile(gif_file,&err);
       free(screen_buffer);
       return err;
    }
    
    for (i = 0; i < gif_file->SWidth; i++)                              /* Set its color to BackGround. */
        screen_buffer[0][i] = gif_file->SBackGroundColor;
    for (i = 1; i < gif_file->SHeight; i++) {
        /* Allocate the other rows, and set their color to background too: */
        if ((screen_buffer[i] = (GifRowType) malloc(row_size)) == NULL){
            DP_ERR("%s:malloc failed.\n",__func__);
            DGifCloseFile(gif_file,&err);
            for( ; --i >=0 ; ){
                free(screen_buffer[i]);
            }
            free(screen_buffer);
            return err;
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
                err = -1;
                goto release_screen_buffer;
            }
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
			if(color_map == NULL){
                DP_ERR("%s:Gif Image does not have a color_map\n",__func__);
                err = -1;
				goto release_screen_buffer;
			}
            
			/* 将数据转换为RGB数据,bpp为24 */
            if(pixel_data->buf)
                free(pixel_data->buf);
            memset(pixel_data,0,sizeof(struct pixel_data));
            pixel_data->width = gif_file->SWidth;
            pixel_data->height = gif_file->SHeight;
            pixel_data->bpp = 24;
            pixel_data->line_bytes = pixel_data->width * pixel_data->bpp / 8;
            pixel_data->total_bytes = pixel_data->line_bytes * pixel_data->height;
            if((pixel_data->buf = malloc(pixel_data->total_bytes)) == NULL){
                DP_ERR("%s:malloc failed\n",__func__);
                err = -ENOMEM;
				goto release_screen_buffer;
            }
            for(i = 0 ; i < gif_file->SHeight ; i++){
                gif_row_buf = screen_buffer[i];
                rgb_line_buf = pixel_data->buf + i * pixel_data->line_bytes;
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
			goto exit_loop;
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

exit_loop:
    /* 将线程池数据结构返回,复用row_buf指针 */
    pixel_data->rows_buf = (unsigned char **)thread_pool;  
    /* 如果当前获取的文件正是现在正在查看的，启动一个线程以更新动画 */
    pthread_mutex_lock(&thread_pool->pool_mutex);
    if(*(view_pic_priv->cur_gif_file)){
        cur_file_name = *(view_pic_priv->cur_gif_file);
        /* 最后检测一次看当前文件打开文件是否为当前显示文件 */
        if(strcmp(cur_file_name,file_name)){
            pthread_mutex_unlock(&thread_pool->pool_mutex);
            goto release_screen_buffer;
        }
    }else{
        pthread_mutex_unlock(&thread_pool->pool_mutex);
        goto release_screen_buffer;
    }
     pthread_mutex_unlock(&thread_pool->pool_mutex);
    
    /* 将已经读出来的第一帧数据缓存到线程数据结构中 */
    if(NULL == (frame_data = malloc(sizeof(struct gif_frame_data)))){
        DP_ERR("%s:malloc failed!\n");
        goto release_screen_buffer;
    }
    /* rgb_line_buf 临时用的，名字无特殊含义 */
    if(NULL == (rgb_line_buf = malloc(pixel_data->total_bytes))){
        DP_ERR("%s:malloc failed!\n");
        free(frame_data);
        goto release_screen_buffer;
    }
    memset(frame_data,0,sizeof(struct gif_frame_data));
    frame_data->data = *pixel_data;
    frame_data->delay_ms = delay_ms;
    memcpy(rgb_line_buf,pixel_data->buf,pixel_data->total_bytes);
    frame_data->data.buf = rgb_line_buf;

submit:
    pthread_mutex_lock(&thread_pool->pool_mutex);
    if(thread_pool->idle_thread){
        /* 当前有空闲线程，不用等待，找到一个未提交任务的线程数据 */
        for(i = 0 ; i < THREAD_NUMS ; i++){
            if(!thread_pool->thread_datas[i].submitted){
                thread_pool->thread_datas[i].file_name = malloc(strlen(cur_file_name));
                strcpy(thread_pool->thread_datas[i].file_name,cur_file_name);
                thread_pool->thread_datas[i].gif_file = gif_file;
                thread_pool->thread_datas[i].screen_buf = screen_buffer;
                thread_pool->thread_datas[i].submitted = 1;
                thread_pool->thread_datas[i].frame_data = frame_data;
                thread_pool->thread_datas[i].frame_data_tail = frame_data;
                pthread_cond_signal(&thread_pool->thread_cond);
                break;
            }
        }
        pthread_mutex_unlock(&thread_pool->pool_mutex);
    }else{
        pthread_mutex_unlock(&thread_pool->pool_mutex);

        /* 当前没有空闲先线程，则进行等待 */
        pthread_mutex_lock(&thread_pool->task_mutex);
        thread_pool->task_wait = 1;
        pthread_cond_wait(&thread_pool->task_cond,&thread_pool->task_mutex);
        thread_pool->task_wait = 0;
        pthread_mutex_unlock(&thread_pool->task_mutex);
        goto submit;
    }
    return 0;

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
    struct gif_thread_pool *thread_pool = data;
    struct gif_thread_data *thread_data;
    struct gif_frame_data *frame_data,*frame_temp;
    GifFileType *gif_file;
    GifRowType *screen_buffer,gif_row_buf;
    ColorMapObject *color_map;
    GifColorType *color_map_entry;
	GifByteType *extension = NULL,*extension_temp = NULL;
    GifByteType trans_color = -1;
    int ext_code,delay_ms = 0;
	GifRecordType recoder_type = UNDEFINED_RECORD_TYPE;
    int interlaced_offset[] = {0,4,2,1};  // The way Interlaced image should
	int interlaced_jumps[] = {8,8,4,2};   // be read - offsets and jumps...
    int err,i,j;
    int task_index;
    int row_size,row,col,width,height;
    unsigned char *rgb_line_buf;
    struct page_struct *view_pic_page = get_page_by_name("view_pic_page");
    struct view_pic_private *view_pic_priv = view_pic_page->private_data;
    struct display_struct *display = get_default_display();
    struct pic_cache *pic_cache;
    int (*fill_pic_func)(struct page_struct *);

    pthread_detach(pthread_self());         /* 分离线程 */
    
    while(1){
        printf("进入线程：%d\n",(int)pthread_self());
        pthread_mutex_lock(&thread_pool->pool_mutex);
        /* 寻找一个已提交任务 */
refind:
        for(i = 0 ; i < THREAD_NUMS ; i++){
            if(thread_pool->thread_datas[i].submitted && !thread_pool->thread_datas[i].processsing){
                task_index = i;
                printf("线程：%d开始执行,task_index:%d\n",(int)pthread_self(),task_index);
                thread_pool->thread_datas[i].processsing = 1;
                thread_pool->idle_thread--;
                break;
            }
        }
        if(i == THREAD_NUMS){
            pthread_mutex_lock(&thread_pool->task_mutex);
            if(thread_pool->task_wait)
                pthread_cond_signal(&thread_pool->task_cond);
            pthread_mutex_unlock(&thread_pool->task_mutex);

            pthread_cond_wait(&thread_pool->thread_cond,&thread_pool->pool_mutex);
            goto refind;
        }
        pthread_mutex_unlock(&thread_pool->pool_mutex);

        /* 读取数据后续的帧 */
        thread_data = &thread_pool->thread_datas[task_index];
        /* 如果当前文件未打开过，先打开gif文件 */
        if(!thread_data->gif_file){
            /* 获取gif文件数据 */
            if ((gif_file = DGifOpenFileName(thread_data->file_name,&err)) == NULL) {
                DP_ERR("%s:open gif file failed!\n",__func__);
                goto exit;
            }
            /* 给屏幕分配内存 */
            if ((screen_buffer = (GifRowType *)malloc(gif_file->SHeight * sizeof(GifRowType *))) == NULL){
                DP_ERR("%s:malloc failed.\n",__func__);
                DGifCloseFile(gif_file,&err);
                goto exit;
            }
                
            /* 以背景色填充屏幕 */
            row_size = gif_file->SWidth * sizeof(GifPixelType);                 /* Size in bytes one row.*/
            if((screen_buffer[0] = (GifRowType) malloc(row_size)) == NULL){     /* First row. */
                DP_ERR("%s:malloc failed.\n",__func__);
                DGifCloseFile(gif_file,&err);
                free(screen_buffer);
                goto exit;
            }
            
            for (i = 0; i < gif_file->SWidth; i++)                          /* Set its color to BackGround. */
                screen_buffer[0][i] = gif_file->SBackGroundColor;
            for (i = 1; i < gif_file->SHeight; i++) {
                /* Allocate the other rows, and set their color to background too: */
                if ((screen_buffer[i] = (GifRowType) malloc(row_size)) == NULL){
                    DP_ERR("%s:malloc failed.\n",__func__);
                    DGifCloseFile(gif_file,&err);
                    for( ; --i >=0 ; ){
                        free(screen_buffer[i]);
                    }
                    free(screen_buffer);
                    goto exit;
                } 
                memcpy(screen_buffer[i], screen_buffer[0], row_size);
            }
        }else{
            gif_file = thread_data->gif_file;
            screen_buffer = thread_data->screen_buf;
        }
        
        pthread_mutex_lock(&view_pic_priv->gif_cache_mutex);
        pic_cache = *view_pic_priv->pic_cache;
        pthread_mutex_unlock(&view_pic_priv->gif_cache_mutex);
        
        /* 循环获取gif图像 */
        do{ 
            if(DGifGetRecordType(gif_file,&recoder_type)==GIF_ERROR){
                err = -1;
                goto release_screen_buffer;
            }
            
            switch(recoder_type){
            case IMAGE_DESC_RECORD_TYPE:
                if(DGifGetImageDesc(gif_file)==GIF_ERROR){
                    err = -1;
                    goto release_screen_buffer;
                }
                row     = gif_file->Image.Top;
                col     = gif_file->Image.Left;
                width   = gif_file->Image.Width;
                height  = gif_file->Image.Height;
                
                if(col + width > gif_file->SWidth || row + height > gif_file->SHeight){
                    DP_ERR("%s:gif %s image 1 not confined to screen dimension\n",__func__,thread_data->file_name);
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
                if(color_map == NULL){
                    DP_ERR("%s:Gif Image does not have a color_map\n",__func__);
                    err = -1;
                    goto release_screen_buffer;
                }

                /* 将数据转换为RGB数据并报存，bpp为24 */
                if(NULL == (frame_data = malloc(sizeof(struct gif_frame_data)))){
                    DP_ERR("%s:malloc failed!\n",__func__);
                    err = -ENOMEM;
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
                    err = -ENOMEM;
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
                
                /* 将数据记录到线程管理的数据链表中 */
                if(!thread_data->frame_data){
                    thread_data->frame_data = frame_data;
                    thread_data->frame_data_tail = frame_data;
                }else{
                    thread_data->frame_data_tail->next = frame_data;
                    thread_data->frame_data_tail = frame_data;
                }

                /* 显示数据，检查此线程处理的文件是否为当前正显示的文件，如果是则显示，如果不是则进入销毁阶段 */
                pthread_mutex_lock(&view_pic_priv->gif_mutex);
                if(*view_pic_priv->pic_cache && *view_pic_priv->cur_gif_file && !strcmp(*view_pic_priv->cur_gif_file,thread_data->file_name)){
                    /* 相同则显示，先按大小缩放 */
                    if(pic_cache->data.buf){
                        free(pic_cache->data.buf);
                        pic_cache->data.buf = NULL;
                        pic_cache->data.in_rows = 0;
                        pic_cache->data.rows_buf = NULL;
                    }
                     /* 如果有需要则延时 */
                    if(frame_data->delay_ms){
                        usleep(1000 * frame_data->delay_ms);
                    }
                    pic_zoom(&pic_cache->data,&frame_data->data);
                    // pic_zoom_with_same_bpp_and_rotate(&pic_cache->data,&frame_data->data,pic_cache->angle);
                    fill_pic_func = view_pic_priv->fill_main_pic_area;
                    if((err = fill_pic_func(view_pic_page))){
                        pthread_mutex_unlock(&view_pic_priv->gif_mutex);
                        goto release_frame_data;
                    }
                    flush_page_region(&view_pic_page->page_layout.regions[5],display);
                    pthread_mutex_unlock(&view_pic_priv->gif_mutex);
                }else{
                    /* 否则进入销毁过程 */
                    free(thread_data->file_name);
                    thread_data->file_name = NULL;
                    pthread_mutex_unlock(&view_pic_priv->gif_mutex);
                    goto release_frame_data;
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
        
        /* 如果运行到这里，说明图片已经播放过一遍了，此时数据已缓存，可以从缓存中循环读取数据了 */
        /* 关闭文件，释放屏幕内存 */
        for(i = 0 ; i < gif_file->SHeight ; i++){
            free(screen_buffer[i]);
        }
        free(screen_buffer);
        screen_buffer = NULL;
        DGifCloseFile(gif_file,&err);
        
        /* 在一个循环中循环播放已缓存的图片 */
        frame_data = thread_data->frame_data;
        
        while(frame_data){
            /* 显示数据，检查此线程处理的文件是否为当前正显示的文件，如果是则显示，如果不是则进入销毁阶段 */
            pthread_mutex_lock(&view_pic_priv->gif_mutex);
            if(*view_pic_priv->pic_cache && *view_pic_priv->cur_gif_file && !strcmp(*view_pic_priv->cur_gif_file,thread_data->file_name)){
                /* 相同则显示，先按大小缩放 */printf("%s-%d\n",__func__,__LINE__);
                if(pic_cache->data.buf){
                    free(pic_cache->data.buf);
                    pic_cache->data.buf = NULL;
                    pic_cache->data.in_rows = 0;
                    pic_cache->data.rows_buf = NULL;
                }printf("%s-%d\n",__func__,__LINE__);
                /* 如果有需要则延时 */
                if(frame_data->delay_ms){
                    usleep(1000 * frame_data->delay_ms);
                }
                pic_zoom(&pic_cache->data,&frame_data->data);
                fill_pic_func = view_pic_priv->fill_main_pic_area;
                if((err = fill_pic_func(view_pic_page))){
                    pthread_mutex_unlock(&view_pic_priv->gif_mutex);
                    goto release_frame_data;
                }printf("%s-%d\n",__func__,__LINE__);
                flush_page_region(&view_pic_page->page_layout.regions[5],display);
                pthread_mutex_unlock(&view_pic_priv->gif_mutex);
            }else{
                /* 否则进入销毁过程 */
                free(thread_data->file_name);
                thread_data->file_name = NULL;
                pthread_mutex_unlock(&view_pic_priv->gif_mutex);
                break;
            }
            printf("%s-%d\n",__func__,__LINE__);
            if(frame_data->next){
                frame_data = frame_data->next;
            }else{
                frame_data = thread_data->frame_data;
            }printf("%s-%d\n",__func__,__LINE__);
        }
        
release_frame_data:
        frame_data = thread_data->frame_data;
        while(frame_data){
            if(frame_data->data.buf)
                free(frame_data->data.buf);
            frame_temp = frame_data->next;
            free(frame_data);
            frame_data = frame_temp;
        } 
release_screen_buffer:
        if(screen_buffer){
            for(i = 0 ; i < gif_file->SHeight ; i++){
            if(screen_buffer[i])
                free(screen_buffer[i]);
            } 
            
            free(screen_buffer);
            DGifCloseFile(gif_file,&err);
        }   
exit:
        if(thread_data->file_name)
            free(thread_data->file_name);
        pthread_mutex_lock(&thread_pool->pool_mutex);
        memset(thread_data,0,sizeof(*thread_data));
        thread_pool->idle_thread++;
        pthread_mutex_unlock(&thread_pool->pool_mutex);
        printf("线程：%d重新开始循环\n",(int)pthread_self());
    }

    return NULL;
}

static int gif_picfmt_init(void)
{
    int i,ret;

    /* 初始化线程池 */
    if((thread_pool = malloc(sizeof(struct gif_thread_pool))) == NULL){
        DP_ERR("%s:malloc failed\n",__func__);
        return -ENOMEM;
    }
    memset(thread_pool,0,sizeof(struct gif_thread_pool));

    /* 创建线程 */
    for(i = 0 ; i < THREAD_NUMS ; i++){
        if((ret = pthread_create(&thread_pool->tids[i],NULL,gif_thread_func,thread_pool))){
            DP_ERR("%s:create thread failed!\n",__func__);
            goto err;
        }
    }
    thread_pool->idle_thread = THREAD_NUMS;
    /* 初始化同步量 */
    if(pthread_cond_init(&thread_pool->task_cond,NULL) || pthread_cond_init(&thread_pool->thread_cond,NULL) ||
       pthread_mutex_init(&thread_pool->pool_mutex,NULL) || pthread_mutex_init(&thread_pool->task_mutex,NULL)){
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