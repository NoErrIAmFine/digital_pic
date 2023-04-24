#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "input_manager.h"
#include "page_manager.h"
#include "debug_manager.h"
#include "picfmt_manager.h"
#include "file.h"

/* 这个数组是为了方便查找picfmt_parser的 */
const char *parser_names[] = {
    [FILETYPE_FILE_BMP]     = "bmp",
    [FILETYPE_FILE_JPEG]    = "jpeg",
    [FILETYPE_FILE_PNG]     = "png",
    [FILETYPE_FILE_GIF]     = "gif"         
};

static struct page_struct *page_list;

int register_page_struct(struct page_struct *page)
{
    struct page_struct *temp;
    if(!page_list){
        page_list = page;
        page->next = NULL;
        if(page->init){
            page->init();
        }
        page->id = calc_page_id(page->name);
        return 0;
    }else{
        temp = page_list;
        /* 不允许同名 */
        if(!strcmp(temp->name,page->name)){
            printf("%s:page struct is existed!\n",__func__);
            return -1;
        }
        while(temp->next){
            if(!strcmp(temp->next->name,page->name)){
                printf("%s:page struct  is existed!\n",__func__);
                return -1;
            }
            temp = temp->next;
        }
        temp->next = page;
        page->next = NULL;
        if(page->init){
            page->init();
        }
        page->id = calc_page_id(page->name);
        return 0;
    }
}

int unregister_page_struct(struct page_struct *page)
{
    struct page_struct **tmp;
    if(!page_list){
        printf("%s:has no exist page struct!\n",__func__);
        return -1;
    }else{
        tmp = &page_list;
        while(*tmp){
            /* 找到则移除*/
            if((*tmp) == page){
                *tmp = (*tmp)->next;
                if((*tmp)->exit){
                    (*tmp)->exit();
                }
                return 0;
            }
            tmp = &(*tmp)->next;
        }
        printf("%s:unregistered page struct!\n",__func__);
        return -1;
    }
}

struct page_struct *get_page_by_name(const char *name)
{
    struct page_struct *tmp = page_list;

    while(tmp){
        if(!strcmp(name,tmp->name)){
            return tmp;
        }
        tmp = tmp->next;
    }
    printf("%s:can't found page!\n",__func__);
    return NULL;
}

void show_page_struct(void)
{
    int i = 1;
    struct page_struct *tmp = page_list;

    while(tmp){
        printf("number:%d ; page name:%s\n",i++,tmp->name);
        tmp = tmp->next;
    }
}

int page_init(void)
{
    int ret;

    if((ret = main_init())){
        return ret;
    }
    
    if((ret = browse_init())){
        return ret;
    }

    if((ret = view_pic_init())){
        return ret;
    }
    
    if((ret = autoplay_init())){
        return ret;
    }
    return 0;
}

/* 
 * @description : 获取页面中的输入事件
 * @param : event - 返回事件结构
 * @return : 发生点击事件的区域所对应的索引编号
 */
int get_input_event_for_page(struct page_struct *page,struct my_input_event *event)
{
    struct page_region *region;
    unsigned int i,num_regions;
    /* 先获取事件 */
    get_input_event(event);

    /* 判断事件发生在哪个区域内，获取其编号 */
    region = page->page_layout.regions;
    num_regions = page->page_layout.region_num;
    /* 逆序查找 */
    region += (num_regions - 1);
    for(i = 0 ; i < num_regions ; i++){
        /* 如果区域标为不可见，直接退出 */
        if(region->invisible){
            region--;
            continue;
        }
        // DP_INFO("region->x_pos:%d,region->y_pos:%d,region->index:%d\n",region->x_pos,region->y_pos,region->index);
        if(region->x_pos <= event->x_pos && region->x_pos + region->width > event->x_pos && \
           region->y_pos <= event->y_pos && region->y_pos + region->height > event->y_pos){
               return region->index;
        }
        region--;
    }
    return -1;
}

/*
 * @description : 将页面中的某块区域输入屏幕缓存中
 */
int flush_page_region(struct page_region *region,struct display_struct *display)
{
    int ret;
    struct display_region display_region;

    display_region.x_pos   = region->x_pos;
    display_region.y_pos   = region->y_pos;
    display_region.width   = region->pixel_data->width;
    display_region.height  = region->pixel_data->height;
    display_region.data    = region->pixel_data;

    ret = display->merge_region(display,&display_region);
    if(ret < 0){
        DP_ERR("%s:merge region to display failed!\n",__func__);
        return ret;
    }
    return 0;
}

/*
 * @description : 一个简单的从页面名字计算页面唯一id的函数
 */
unsigned int calc_page_id(const char *name)
{
    unsigned int sum = 0;
    int len = strlen(name);
    /* 取一个最大长度，防止意外情况 */
    if(len > 50){
        len = 50;
    }
    while(len){
        sum += name[--len];
    }
    return sum;
}

static int remap_region_to_page_mem(struct page_struct *page,struct page_region *region)
{
    struct pixel_data *region_data;
    struct pixel_data *page_data = &page->page_mem;
    struct page_layout *layout = &page->page_layout;
    unsigned char *page_buf = page->page_mem.buf;
    int i;
    // DP_INFO("region->x_pos:%d,region->y_pos:%d,region->width:%d,region->height:%d\n",region->x_pos,region->y_pos,region->width,region->height);
    /* 只处理region完全在page范围内的情况 */
    if(region->x_pos >= layout->width || region->y_pos >= layout->height || \
      (region->x_pos + region->width) > layout->width || (region->y_pos + region->height) > layout->height){
          DP_ERR("%s:invalid region!\n",__func__);
          return -1;
    }

    region_data = region->pixel_data;

    if(!region_data){
        region_data = malloc(sizeof(struct pixel_data));
        if(!region_data){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
    }
    region->pixel_data = region_data;

    region_data->bpp    = page_data->bpp;
    region_data->width  = region->width;
    region_data->height = region->height;
    region_data->line_bytes     = region_data->width * region_data->bpp / 8;
    region_data->total_bytes    = region_data->line_bytes * region_data->height;
    region_data->rows_buf = malloc(sizeof(char *) * region->height);
    if(!region_data->rows_buf){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    
    page_buf += (page_data->line_bytes * region->y_pos + region->x_pos * page_data->bpp / 8);
    for(i = 0 ; i < region->height ; i++){
        region_data->rows_buf[i] = page_buf;
        page_buf += page_data->line_bytes;
    }
    region_data->in_rows = 1;
    return 0;
}

int remap_regions_to_page_mem(struct page_struct *page)
{
    struct pixel_data *page_data    = &page->page_mem;
    struct page_layout *layout      = &page->page_layout;
    struct page_region *regions = layout->regions;
    int i,ret;
    
    /* 如果页面还没分配内存那还映射个毛 */
    if(!page->allocated){
        DP_WARNING("%s:map unallocated page mem!\n",__func__);
        return -1;
    }
    
    for(i = 0 ; i < layout->region_num ; i++){
        ret = remap_region_to_page_mem(page,&regions[i]);
        if(ret){
            DP_ERR("%s:remap_region_to_page_mem error!\n",__func__);
            return ret;
        }
        
        regions[i].page_layout = layout;
    }
    page->region_mapped = 1;

    return 0;
}

int unmap_regions_to_page_mem(struct page_struct *page)
{
    int i;
    struct page_region *regions = page->page_layout.regions;
    int region_num = page->page_layout.region_num;

    if(!page->already_layout || !page->region_mapped){
        return -1;
    }

    /* 删除映射 */
    for(i = 0 ; i < region_num ; i++){
        if(regions[i].pixel_data->in_rows){
            free(regions[i].pixel_data->rows_buf);
        }
        free(regions[i].pixel_data);
        regions[i].pixel_data = NULL;
    }
    page->region_mapped = 0;
    return 0 ;
}

/* 根据图片文件名读入相应图片的数据,此函数负责分配内存,此函数不负责缩放 */
int get_pic_pixel_data(const char *pic_file,char file_type,struct pixel_data *pixel_data)
{
    int ret;
    struct picfmt_parser *parser;

    parser = get_parser_by_name(parser_names[(int)file_type]);
    if(!parser){
        DP_ERR("%s:get_parser_by_name failed!\n",__func__);
        return -1;
    }
   
    ret = parser->get_pixel_data(pic_file,pixel_data);
    if(ret){
        DP_ERR("%s:pic parser get_pixel_data failed!\n",__func__);
        /* 如果没获取到，就给它一张默认的表示错误的图片吧 */

        return -1;
    }
    
    return 0;
}
