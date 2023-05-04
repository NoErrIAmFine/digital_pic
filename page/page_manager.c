#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "input_manager.h"
#include "page_manager.h"
#include "debug_manager.h"
#include "picfmt_manager.h"
#include "pic_operation.h"
// #include "file.h"

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

    //调用各页面的初始化函数
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

    if((ret = setting_init())){
        return ret;
    }

    if((ret = interval_init())){
        return ret;
    }

    if((ret = text_init())){
        return ret;
    }

    return 0;
}

void page_exit(void)
{

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
    
    /* 如果页面是和显存共享同一块内存，那还刷个毛，直接退出 */
    if(region->owner_page->share_fbmem)
        return 0;
    
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
          DP_INFO("layout->width:%d,layout->height:%d\n",layout->width,layout->height);
          DP_INFO("region.id:%d\n",region->index);
          DP_INFO("region->x_pos:%d,region->y_pos:%d,region->width:%d,region->height:%d\n",region->x_pos,region->y_pos,region->width,region->height);
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
    if(page->region_mapped){
        return 0;
    }
    printf("page.name:%s-layout->region_num:%d\n",page->name,layout->region_num);
    for(i = 0 ; i < layout->region_num ; i++){
        ret = remap_region_to_page_mem(page,&regions[i]);
        if(ret){
            DP_ERR("%s:remap_region_to_page_mem error!\n",__func__);
            return ret;
        }
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
        return 0;
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

/* @description : 调整 src_data 图像的大小，使其刚好能放入到 dst_data 中,并复制到 dst_data 中 ；
 * @parma : dst_data - 存放最终的图像，必须指定宽高；支持的bpp：16、24、32；
 * @param : src_data - 源图像，对其进行缩放；支持的bpp：16、24、32 */
int resize_pic_pixel_data(struct pixel_data *dst_data,struct pixel_data *src_data)
{
    int i,j;
    int resized_width,resized_height;
    int src_width,src_height;
    int dst_width,dst_height;
    int start_x,start_y,src_y;
    int bytes_per_pixel;
    unsigned char *src_line_buf,*dst_line_buf;
    float scale,scale_x,scale_y;
    int scale_x_table[dst_data->width];
    unsigned char src_red,src_green,src_blue;
    unsigned char dst_red,dst_green,dst_blue;
    unsigned char red,green,blue;
    unsigned int color,orig_color;
    unsigned short color_16;
    unsigned char alpha;
    int temp;

    if(!dst_data->width || !dst_data->height || !src_data->buf){
        DP_ERR("%s:invalied argument!\n",__func__);
        return -1;
    }
    
    if(!dst_data->buf && !dst_data->rows_buf){
        dst_data->bpp = src_data->bpp;
        dst_data->line_bytes = dst_data->width * dst_data->bpp / 8;
        dst_data->total_bytes = dst_data->line_bytes * dst_data->height;
        if(NULL == (dst_data->buf = malloc(dst_data->total_bytes))){
            DP_ERR("%s:malloc failed!\n",__func__);
            return -ENOMEM;
        }
    }
    
    src_width = src_data->width;
    src_height = src_data->height;
    dst_width = dst_data->width;
    dst_height = dst_data->height;
    scale = (float)src_height / src_width;
    
    /* 确定缩放后的图片大小 */
    if(dst_width >= src_width && dst_height >= src_height){
        /* 图片可完全显示，保留原有尺寸 */
        resized_width = src_width;
        resized_height = src_height;
    }else if(dst_width < src_width && dst_height < src_height){
        /* 先将宽度缩至允许的最大值 */
        resized_width = dst_width;
        resized_height = scale * resized_width;
        if(resized_height > dst_height){
            /* 还要继续缩小 */
            resized_height = dst_height;
            resized_width = resized_height / scale;
        }
    }else if(dst_width < src_width){
        resized_width = dst_width;
        resized_height = resized_width * scale;
    }else if(dst_height < src_height){
        resized_height = dst_height;
        resized_width = resized_height / scale;
    }

    /* 确定调整后的图像在目标图像中的起始位置 */
    start_x = (dst_width - resized_width) / 2;
    start_y = (dst_height - resized_height) / 2;
    
    /* 开始缩放操作 */
    scale_x = (float)src_width / resized_width;
    for(i = 0 ; i < resized_width ; i++){
        scale_x_table[i] = i * scale_x;
    }
    scale_y = (float)src_height / resized_height;
    bytes_per_pixel = dst_data->bpp / 8;
    temp = bytes_per_pixel * start_x;
    // printf("start_x-%d,start_y-%d\n",start_x,start_y);
    // printf("resized_width-%d,resized_height-%d\n",resized_width,resized_height);
    // printf("dst_width-%d,dst_height-%d\n",dst_width,dst_height);
    // printf("src_width-%d,src_height-%d\n",src_width,src_height);
    for(i = 0 ; i < resized_height ; i++){
        src_y = i * scale_y;
        if(src_data->rows_buf){
            src_line_buf = src_data->rows_buf[src_y];
        }else{
            src_line_buf = src_data->buf + src_data->line_bytes * src_y;
        }
        if(dst_data->rows_buf){
            dst_line_buf = dst_data->rows_buf[i + start_y] + temp;
        }else{
            dst_line_buf = dst_data->buf + dst_data->line_bytes * (i + start_y) + temp;
        }
        
        /* 因为兼容几种bpp，这里写的又臭又长 */
        switch(dst_data->bpp){
        case 16:
            switch(src_data->bpp){
            case 16:
                for(j = 0 ; j < resized_width ; j++){
                    *(unsigned short *)dst_line_buf = *(unsigned short *)(src_line_buf + (2 * scale_x_table[j]));
                    dst_line_buf += 2;
                }
                break;
            case 24:
                for(j = 0 ; j < resized_width ; j++){
                    // printf("scale_x_table[j]-%d\n",scale_x_table[j]);
                    src_red   = (src_line_buf[3 * scale_x_table[j]] >> 3);
                    src_green = (src_line_buf[3 * scale_x_table[j] + 1] >> 2);
                    src_blue  = (src_line_buf[3 * scale_x_table[j] + 2] >> 3);
                    color_16 = (src_red << 11 | src_green << 5 | src_blue);
                    *(unsigned short *)dst_line_buf = color_16;
                    // *(unsigned short *)dst_line_buf = 0xbb;
                    dst_line_buf += 2;
                }
                break;
            case 32:
                for(j = 0 ; j < resized_width ; j++){
                    alpha     = src_line_buf[4 * scale_x_table[j]];
                    src_red   = src_line_buf[4 * scale_x_table[j] + 1] >> 3;
                    src_green = src_line_buf[4 * scale_x_table[j] + 2] >> 2;
                    src_blue  = src_line_buf[4 * scale_x_table[j] + 3] >> 3;
                    color_16 = *(unsigned short *)dst_line_buf;
                    dst_red   = (color_16 >> 11) & 0x1f;
                    dst_green = (color_16 >> 5) & 0x3f;
                    dst_blue  = color_16 & 0x1f;
                    /* 根据透明度合并颜色,公式:显示颜色 = 源像素颜色 X alpha / 255 + 背景颜色 X (255 - alpha) / 255 */
                    red   = (src_red * alpha) / 255 + (dst_red * (255 - alpha)) / 255;
                    green = (src_green * alpha) / 255 + (dst_green * (255 - alpha)) / 255;
                    blue  = (src_blue * alpha) / 255 + (dst_blue * (255 - alpha)) / 255;
                    color_16 = (red << 11) | (green << 5) | blue;
                    *(unsigned short *)dst_line_buf = color_16;
                    // *(unsigned short *)dst_line_buf = 0xff;
                    dst_line_buf += 2;
                }
                break;
            }
            break;
        case 24:
            switch(src_data->bpp){
            case 16:
                for(j = 0 ; j < resized_width ; j++){
                    /* 取出各颜色分量 */
                    color_16  = *(unsigned short *)(src_line_buf + 2 * scale_x_table[j]);
                    src_red   = (color_16 >> 11) << 3;
                    src_green = ((color_16 >> 5) & 0x3f) << 2;
                    src_blue  = (color_16 & 0x1f) << 3;
                    
                    dst_line_buf[j * 3]     = src_red;
                    dst_line_buf[j * 3 + 1] = src_green;
                    dst_line_buf[j * 3 + 2] = src_blue;
                    dst_line_buf += 3;
                }
                break;
            case 24:
                for(j = 0 ; j < resized_width ; j++){
                    memcpy(dst_line_buf,(src_line_buf + 3 * scale_x_table[j]),3);
                    dst_line_buf += 3;
                }
                break;
            case 32:
                for(j = 0 ; j < resized_width ; j++){
                    alpha     = src_line_buf[scale_x_table[j] * 4];
                    src_red   = src_line_buf[scale_x_table[j] * 4 + 1];
                    src_green = src_line_buf[scale_x_table[j] * 4 + 2];
                    src_blue  = src_line_buf[scale_x_table[j] * 4 + 3];
                    dst_red   = dst_line_buf[j * 3] ;
                    dst_green = dst_line_buf[j * 3 + 1];
                    dst_blue  = dst_line_buf[j * 3 + 2];
                    
                    /* 根据透明度合并颜色,公式:显示颜色 = 源像素颜色 X alpha / 255 + 背景颜色 X (255 - alpha) / 255 */
                    red   = (src_red * alpha) / 255 + (dst_red * (255 - alpha)) / 255;
                    green = (src_green * alpha) / 255 + (dst_green * (255 - alpha)) / 255;
                    blue  = (src_blue * alpha) / 255 + (dst_blue * (255 - alpha)) / 255;
                    color = (red << 11) | (green << 5) | blue;
                    dst_line_buf[j * 3]     = red;
                    dst_line_buf[j * 3 + 1] = green;
                    dst_line_buf[j * 3 + 2] = blue;
                }
                break;
            }
            break;
        case 32:
            switch(src_data->bpp){
            case 16:
                for(j = 0 ; j < resized_width ; j++){
                    /* 取出各颜色分量 */
                    color_16  = *(unsigned short *)(src_line_buf + (2 * scale_x_table[j]));
                    src_red   = (color_16 >> 11) << 3;
                    src_green = ((color_16 >> 5) & 0x3f) << 2;
                    src_blue  = (color_16 & 0x1f) << 3;
                    
                    dst_line_buf[j * 4] = 0xff;             //默认不透明
                    dst_line_buf[j * 4 + 1] = src_red;
                    dst_line_buf[j * 4 + 2] = src_green;
                    dst_line_buf[j * 4 + 2] = src_blue;
                }
                break;
            case 24:
                for(j = 0 ; j < resized_width ; j++){
                    src_red     = src_line_buf[3 * scale_x_table[j] + 0] >> 3;
                    src_green   = src_line_buf[3 * scale_x_table[j] + 1] >> 2;
                    src_blue    = src_line_buf[3 * scale_x_table[j] + 2] >> 3;
                    dst_line_buf[j * 4]     = 0;
                    dst_line_buf[j * 4 + 1] = src_red;
                    dst_line_buf[j * 4 + 2] = src_green;
                    dst_line_buf[j * 4 + 2] = src_blue;
                }
                break;
            case 32:
                for(j = 0 ; j < resized_width ; j++){
                    *(unsigned int *)dst_line_buf = *(unsigned int *)(src_line_buf + (4 * scale_x_table[j]));
                    dst_line_buf += 4;
                }
                break;
            }
            break;
        }
    }

    return 0;
}

/* 准备图标数据，只支持png格式文件，将图标原样读出，不进行缩放 */
static int get_icon_pixel_datas(struct pixel_data *icon_datas,const char **icon_names,int icon_num)
{
    int i,j,ret;
    struct picfmt_parser *png_parser = get_parser_by_name("png");
    const char file_path[] = DEFAULT_ICON_FILE_PATH;
    char file_full_path[100];

    for(i = 0 ; i < icon_num ; i++){
        char *file_name;
        int file_name_malloc = 0;
       
        /* 如果没有指定文件，直接跳过 */
        if(!icon_names[i]){
            continue;
        }
        /* 构造文件名，为了预防文件名过长导致出错,虽然这发生的概率极小 */
        if((strlen(file_path) + strlen(icon_names[i]) + 1) > 99){
            file_name = malloc(strlen(file_path) + strlen(icon_names[i]) + 2);
            if(!file_name){
                DP_ERR("%s:malloc failed!\n");
                goto free_icon_data;
            }
            sprintf(file_name,"%s/%s",file_path,icon_names[i]);
            file_name_malloc = 1;
        }else{
            sprintf(file_full_path,"%s/%s",file_path,icon_names[i]);
        }
    
        memset(&icon_datas[i],0,sizeof(struct pixel_data));
        if(file_name_malloc){
            ret = png_parser->get_pixel_data_in_rows(file_name,&icon_datas[i]);
        }else{
            ret = png_parser->get_pixel_data_in_rows(file_full_path,&icon_datas[i]);
        } 
        if(ret){
            // if(ret == -2){
            //     //to-do 此种错误是可修复的 
            // }
            DP_ERR("%s:get icon pixel data failed!\n",__func__);
            goto free_icon_data;
        } 
        
        if(file_name_malloc){
            free(file_name);
        }
    }
    return 0; 

free_icon_data:
    for(i-- ; i > 0 ; i--){
        if(icon_datas[i].buf){
            free(icon_datas[i].buf);
        }else if(icon_datas[i].rows_buf){
            free(icon_datas[i].rows_buf);
        }
        memset(&icon_datas[i],0,sizeof(struct pixel_data));
    }
    return ret;
}

/* 为一个页面准备好图标数据 */
int prepare_icon_pixel_datas(struct page_struct *page,struct pixel_data *icon_datas,
                                    const char **icon_names,const unsigned int icon_region_links[],int icon_num)
{
    int i,ret;
    struct page_region *regions = page->page_layout.regions;
    struct pixel_data temp;

    if(page->icon_prepared){
        return 0;
    }
    if(!page->already_layout){
        DP_ERR("%s:page has not calculate layout!\n",__func__);
        return -1;
    }

    /* 获取初始数据 */
    ret = get_icon_pixel_datas(icon_datas,icon_names,icon_num);
    if(ret){
        DP_ERR("%s:get_icon_pixel_datas failed\n",__func__);
        return ret;
    }

    /* 缩放至合适大小 */
    for(i = 0 ; i < icon_num ; i++){
        memset(&temp,0,sizeof(struct pixel_data));
        if(icon_region_links[i] & (1 << 31)){
            /* 如果最高位为1，表示此值为实际的长宽值，而非对应的区域编号,长宽分别用12位表示 */
            temp.width  = ((icon_region_links[i] & 0xfff000) >> 12);
            temp.height = (icon_region_links[i] & 0xfff);
        }else{
            temp.width  = regions[icon_region_links[i]].width;
            temp.height = regions[icon_region_links[i]].height;
        }
        
        ret = pic_zoom_with_same_bpp(&temp,&icon_datas[i]);
        if(ret){
            DP_ERR("%s:pic_zoom_with_same_bpp failed\n",__func__);
            return ret;
        }
        free(icon_datas[i].buf);
        icon_datas[i] = temp;
    }

    page->icon_prepared = 1;
    return 0;
}

void destroy_icon_pixel_datas(struct page_struct *page,struct pixel_data *icon_datas,int icon_num)
{
    int i;
    if(page->icon_prepared){
        for(i = 0 ; i < icon_num ; i++){
            if(icon_datas[i].buf)
                free(icon_datas[i].buf);
            memset(&icon_datas[i],0,sizeof(struct pixel_data));
        }
    }
    page->icon_prepared = 0;
}

int invert_region(struct pixel_data *pixel_data)
{   
    unsigned short *line_buf;
    unsigned int width,height;
    unsigned int i,j;
    /* 暂只处理16bpp */
    if(16 == pixel_data->bpp){
        width = pixel_data->width;
        height = pixel_data->height;
        for(i = 0 ; i < height ; i++){
            /* 根据数据的不同储存方式获取行起始处指针 */
            if(pixel_data->in_rows){
                line_buf = (unsigned short *)pixel_data->rows_buf[i];
            }else{
                line_buf = (unsigned short *)pixel_data->buf + pixel_data->line_bytes * i;
            }
            for(j = 0 ; j < width ; j++){
                *line_buf = ~(*line_buf);
                line_buf++;
            }
        }
        return 0;
    }else{
        DP_INFO("%s:unsupported bpp!\n",__func__);
        return -1;
    }
}

/* @description : 对指定区域施加某种效果表示该区域被按下，比如反转该区域的颜色
 * @param : region - 被按下的区域
 * @param : press - 1表示按下，0表示松开
 * @param : pattern - 用于表示按下状态的样式，比如反转颜色、变灰、加个框等，现在该参数无效 */
int press_region(struct page_region *region,int press,int pattern)
{   
    if(press){
        if(region->pressed){
            return 0;       //如果已被按下，什么也不用做
        }
        region->pressed = 1;
        invert_region(region->pixel_data);
    }else{
        if(!region->pressed){
            return 0;       //如果已是松开状态，什么也不用做
        }
        region->pressed = 0;
        invert_region(region->pixel_data);
    }
    return 0;
}