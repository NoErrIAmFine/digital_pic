#ifndef __PAGE_MANAGER_H
#define __PAGE_MANAGER_H

#include "display_manager.h"
#include "input_manager.h"

#define PAGE_REGION(_index,_level,_name,_file_name) {   \
    .index = _index,                                    \
    .level = _level,                                    \
    .name = _name,                                      \
    .file_name = _file_name,                            \
}

struct page_region
{
    unsigned int x_pos;
    unsigned int y_pos;
    int width;
    int height;
    int index;
    /* 该区域的层次，区域是可以重叠的，在上面的层次越高 */
    int level;
    /* 为该区域取一个名字 */
    const char *name;
    /* 若该区域对应一个资源文件，file_name 为文件名 */
    const char *file_name;
    /* 该区域所对应的数据所在的内存位置，不是必需的 */
    struct pixel_data *pixel_data;
    /* 该区域所属的页布局 */
    struct page_layout *page_layout;
    void *private_data;
    /* 是否已经分配了内存 */
    unsigned int allocated:1;
    /* 是否被按下 */
    unsigned int pressed:1;
    /* 是否被显示 */
    unsigned int displayed:1;
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
    /* 是否已分配内存 */
    unsigned int allocated:1;
    /* 是否已计算好布局 */
    unsigned int already_layout:1;
    /* 区域已映射到内存 */
    unsigned int region_mapped:1;
    /* 是否已准备好图标数据 */
    unsigned int icon_prepared:1;
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

/*
 * @description : 将页面中的某块区域输入屏幕缓存中
 */
int flush_page_region(struct page_region*,struct display_struct *);
unsigned int calc_page_id(const char *);
int remap_regions_to_page_mem(struct page_struct *page);
int unmap_regions_to_page_mem(struct page_struct *page);

int main_init(void);
int browse_init(void);
int view_pic_init(void);
int page_init(void);

#endif // !__PAGE_MANAGER_H
