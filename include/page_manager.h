#ifndef __PAGE_MANAGER_H
#define __PAGE_MANAGER_H

#include "display_manager.h"
#include "input_manager.h"

#define PAGE_REGION(_index,_level,_page) {    \
    .index      = _index,                           \
    .level      = _level,                           \
    .owner_page = _page                             \
}

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

struct page_region
{
    unsigned int x_pos;
    unsigned int y_pos;
    int width;
    int height;
    int index;
    /* 该区域的层次，区域是可以重叠的，在上面的层次越高 */
    int level;
    /* 该区域所对应的数据所在的内存位置，不是必需的 */
    struct pixel_data *pixel_data;
    /* 该区域所属的页面 */
    struct page_struct *owner_page;
    void *private_data;
    /* 是否已经分配了内存 */
    unsigned int allocated:1;
    /* 是否被按下 */
    unsigned int pressed:1;
    /* 是否可见，默认为可见 */
    unsigned int invisible:1;
    /* 是否被选中 */
    unsigned int selected;
};

struct page_layout
{
    int width;
    int height;
    struct page_region *regions;
    int region_num;
};

struct page_param
{
    unsigned int id;
    void *private_data;
};

/* 用于在页面之间传递数据 */
struct page_struct 
{
    const char *name;
    /* 唯一id */
    unsigned int id;
    struct page_struct *next;
    struct page_layout page_layout;
    /* 代表该页的内容在内存中的位置 */
    struct pixel_data page_mem;
    int (*init)(void);
    void (*exit)(void);
    int (*run)(struct page_param*);
    void *private_data;
    /* 是否已分配内存 */
    unsigned int allocated:1;
    /* 是否与显存共享一块内存 */
    unsigned int share_fbmem:1;
    /* 是否已计算好布局 */
    unsigned int already_layout:1;
    /* 区域已映射到内存 */
    unsigned int region_mapped:1;
    /* 是否已准备好图标数据 */
    unsigned int icon_prepared:1;
};

/* 用于 view pic 页面，表示一个图片缓存 */
struct pic_cache
{
    short virtual_x;        //图片在虚拟显示空间中的右上角座标，这个空间是可以超出显示屏的
    short virtual_y;
    short orig_width;       //图片原始宽度
    short orig_height;      //图片原始高度
    short angle;            //缓存中图片的角度，可能的取值:0,90,180,270
    struct pixel_data data;
    void *orig_data;
    unsigned int has_data:1;        //标志位，说明缓存中是否有数据
    unsigned int has_orig_data:1;   //标志位，表明缓存中是否含有原始数据
    unsigned int is_gif:1;          //是否是一张gif图？
};

/* view pic 页面用到的私有结构 */
struct view_pic_private
{
    char **cur_gif_file;
    pthread_mutex_t page_mem_mutex;
    struct pic_cache **pic_cache;
    pthread_mutex_t gif_cache_mutex;
    int (*fill_main_pic_area)(struct page_struct *page);
};

/* 专用于autoplay页面，用于从其他页面接收连播目录和连播间隔信息 */
struct autoplay_private
{
#define MAX_AUTOPLAY_DIRS 10
    char *autoplay_dirs[MAX_AUTOPLAY_DIRS];
    unsigned long autoplay_dir_num;
    unsigned long autoplay_interval;
};

int register_page_struct(struct page_struct *);
int unregister_page_struct(struct page_struct *);
struct page_struct *get_page_by_name(const char *);
void show_page_struct(void);

/* 
 * @description : 获取页面中的输入事件
 * @param : event - 返回事件结构
 * @return : 发生点击事件的区域所对应的索引编号
 */
int get_input_event_for_page(struct page_struct*,struct my_input_event*);

/* 根据图片文件名读入相应图片的原始数据,此函数负责分配内存,此函数不负责缩放 */
int get_pic_pixel_data(const char *file_name,char file_type,struct pixel_data *pixel_data);

/*
 * @description : 将页面中的某块区域输入屏幕缓存中
 */
int flush_page_region(struct page_region*,struct display_struct *);
unsigned int calc_page_id(const char *);
int remap_regions_to_page_mem(struct page_struct *page);
int unmap_regions_to_page_mem(struct page_struct *page);
int prepare_icon_pixel_datas(struct page_struct *page,struct pixel_data *icon_datas,
                                    const char **icon_names,const int icon_region_links[],int icon_num);
void destroy_icon_pixel_datas(struct page_struct *page,struct pixel_data *icon_datas,int icon_num);
int invert_region(struct pixel_data *);
int press_region(struct page_region *region,int press,int pattern);

int main_init(void);
int browse_init(void);
int view_pic_init(void);
int autoplay_init(void);
int page_init(void);
int setting_init(void);
int interval_init(void);
int text_init(void);

#endif // !__PAGE_MANAGER_H
