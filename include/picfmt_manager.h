#ifndef __PICFMT_MANAGER_H
#define __PICFMT_MANAGER_H

#include "display_manager.h"

struct picfmt_parser
{
    const char *name;
    struct picfmt_parser *next;
    int (*init)(void);
    void (*exit)(void);
    int (*is_support)(const char *);
    /* 为什么要用FILE，而不是文件名？能获取数据，说明肯定已经打开过文件了，
     * 我觉得没必要再传个文件名进去重新打开 */
    int (*get_pixel_data)(const char *,struct pixel_data *);
    /* 所获得的数据是按行存储的 */
    int (*get_pixel_data_in_rows)(const char *,struct pixel_data *);
    int (*free_pixel_data)(struct pixel_data *);
    unsigned int is_enable:1;
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