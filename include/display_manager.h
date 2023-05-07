#ifndef __DISPLAY_MANAGER
#define __DISPLAY_MANAGER

struct pixel_data
{
    unsigned int width;
    unsigned int height;
    unsigned int bpp;
    unsigned int line_bytes;
    unsigned int total_bytes;
    unsigned char *buf;
    /* 指向一个指针数组，每个数组成员指向一行图像数据，另一种数据访问方式 */
    unsigned char **rows_buf;
    /* 以行指针数组的方式指定一块存储空间 */
    unsigned int in_rows:1;
    unsigned int has_alpha:1;
    /* 用于在浏览文件时显示预览图，表示此块内存区域内的预览图是否已完全生成 */
    unsigned int preview_completed:1;
};

/* 表示在显示空间中的一个指定区域 */
struct display_region
{
    struct pixel_data *data;
    int x_pos;
    int y_pos;
    int width;
    int height;
    /* 是否已分配数据 */
    unsigned int allocated:1;
};

struct display_struct
{
    const char *name;
    int xres;
    int yres;
    unsigned int bpp;
    unsigned int line_bytes;
    unsigned int total_bytes;
    unsigned char *buf;
    struct display_struct *next;
    /* 很直接，将一段数据输入缓存,不做任何处理 */
    int (*flush_buf)(struct display_struct *,const unsigned char *,int);
    /* 将整块显存清为某种颜色 */
    int (*clear_buf)(struct display_struct *,int);
    /* 将某个区域清为某种颜色 */
    int (*clear_buf_region)(struct display_struct *,struct display_region *);
    /* 只更改显存中的指定区域，当然可以全部更改，会自动转换bpp*/
    int (*merge_region)(struct display_struct *,struct display_region *);
    int (*init)(struct display_struct *);
    int (*exit)(struct display_struct *);
    unsigned int is_enable:1;
};

int register_display_struct(struct display_struct *);
int unregister_display_struct(struct display_struct *);
struct display_struct *get_display_by_name(const char *);
/* 打印当前已注册的调试器的信息 */
void show_display_struct(void);
struct display_struct *get_default_display(void);
void set_default_display(struct display_struct *);
int show_pixel_data_in_default_display(int ,int ,struct pixel_data *);

int lcd_init(void);
int display_init(void);

#endif // !__DISPLAY_MANAGER