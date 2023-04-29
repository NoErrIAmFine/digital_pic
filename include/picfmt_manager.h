#ifndef __PICFMT_MANAGER_H
#define __PICFMT_MANAGER_H

#include <pthread.h>
#include <gif_lib.h>
#include "display_manager.h"
#include "page_manager.h"

struct picfmt_parser
{
    const char *name;
    struct picfmt_parser *next;
    int (*init)(void);
    void (*exit)(void);
    int (*is_support)(const char *);
    /* 我觉得没必要再传个文件名进去重新打开 */
    int (*get_pixel_data)(const char *,struct pixel_data *);
    /* 所获得的数据是按行存储的 */
    int (*get_pixel_data_in_rows)(const char *,struct pixel_data *);
    int (*free_pixel_data)(struct pixel_data *);
    unsigned int is_enable:1;
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
    char *file_name;                        /* 当前正打开的gif文件 */
    // struct pic_cache *pic_cache;         /* 当前打开文件对应的缓存 */
    struct gif_frame_data *frame_data;      /* gif各帧数据 */
    struct gif_frame_data *frame_data_tail; /* gif各帧数据 */
    GifRowType *screen_buf;                 /* 屏幕缓存区 */
    GifFileType *gif_file;                  /* gif lib 结构句柄 */
    unsigned int submitted:1;               /* 表示是否提交了任务 */
    unsigned int processsing:1;             /* 表示是否正在处理任务 */
};

/* 线程数据结构，含3个线程 */
struct gif_thread_pool
{
#define THREAD_NUMS 3
    pthread_t tids[THREAD_NUMS];
    struct gif_thread_data thread_datas[THREAD_NUMS];   /* 线程数据 */
    char *exit_task[THREAD_NUMS];                       /* 退出播放的gif文件名 */
    int idle_thread;                                    /* 空闲线程数 */
    pthread_mutex_t pool_mutex;
    pthread_cond_t thread_cond;                         /* 空闲的线程在此等待 */
    pthread_cond_t task_cond;                           /* 无空闲线程时，任务线程在此等待线程空闲 */
};



int register_picfmt_parser(struct picfmt_parser *);
int unregister_picfmt_parser(struct picfmt_parser *);
struct picfmt_parser *get_parser_by_name(const char *);
/* 打印当前已注册的图片解析器的信息 */
void show_picfmt_parser(void);

int png_init(void);
int jpeg_init(void);
int bmp_init(void);
int gif_init(void);

int picfmt_parser_init(void);

#endif // !__PICFMT_MANAGER_H