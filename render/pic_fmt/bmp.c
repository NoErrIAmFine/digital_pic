#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "picfmt_manager.h"
#include "debug_manager.h"

struct bmp_bitmap_file_header               /* bmfh */
{ 
	unsigned short type; 
	unsigned long  size;
	unsigned short reserved1;
	unsigned short reserved2;
	unsigned long  off_bits;
}__attribute__((packed));

struct bmp_bitmap_info_header               /* bmih */
{ 
	unsigned long  size;
	unsigned long  width;
	unsigned long  height;
	unsigned short planes;
	unsigned short bit_count;
	unsigned long  compression;
	unsigned long  size_image;
	unsigned long  x_pels_per_meter;
	unsigned long  y_pels_per_meter;
	unsigned long  clr_used;
	unsigned long  clr_important;
}__attribute__((packed));

static int bmp_get_pixel_data(const char *file_name,struct pixel_data *pixel_data)
{
    struct bmp_bitmap_file_header *bmp_file_header;
	struct bmp_bitmap_info_header *bmp_info_header;

    int ret,y;
    int fd;
    unsigned char *file_buf;
    unsigned char *src_data;
	unsigned char *dst_data;
	unsigned short width,height,bpp;	
	unsigned short line_bytes_align;
	unsigned short line_bytes_real;
    struct stat bmp_stat;

    /* 打开文件 */
    fd = open(file_name,O_RDONLY);
    if(fd < 0){
        DP_ERR("%s:open failed!\n",__func__);
        return errno;
    }

    /* 分配文件缓存，并读入 */
    if((ret = fstat(fd,&bmp_stat)) < 0){
        return errno;
    }

    file_buf = malloc(bmp_stat.st_size);
    if(!file_buf){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    if((ret = read(fd,file_buf,bmp_stat.st_size)) != bmp_stat.st_size){
        DP_ERR("%s:read failed!\n",__func__);
        free(file_buf);
        close(fd);
        return errno;
    }

    close(fd);

    bmp_file_header = (struct bmp_bitmap_file_header *)file_buf;
    bmp_info_header = (struct bmp_bitmap_info_header *)(file_buf + sizeof(struct bmp_bitmap_file_header));

	width = bmp_info_header->width;
	height = bmp_info_header->height;
	bpp = bmp_info_header->bit_count;

	if (bpp != 24){
		DP_WARNING("%s:invalid bpp,iBMPBpp = %d\n", __func__,bpp);
		return -1;
	}

	pixel_data->width  = width;
	pixel_data->height = height;
	pixel_data->bpp = bpp;
	pixel_data->line_bytes    = width * pixel_data->bpp / 8;
    pixel_data->total_bytes   = pixel_data->height * pixel_data->line_bytes;
    if(pixel_data->buf){
        free(pixel_data->buf);
    }
	pixel_data->buf = malloc(pixel_data->total_bytes);
	if (!pixel_data->buf){
		DP_ERR("%s:malloc failed!\n",__func__);
        free(file_buf);
        return -ENOMEM;
	}

	line_bytes_real  = width * bpp / 8;
	line_bytes_align = (line_bytes_real + 3) & ~0x3;   /* 向4取整 */
		
	src_data = file_buf + bmp_file_header->off_bits;
	src_data = src_data + (height - 1) * line_bytes_align;

	dst_data = pixel_data->buf;
	
	for (y = 0; y < height; y++){		
		memcpy(dst_data, src_data, line_bytes_real);
		src_data  -= line_bytes_align;
		dst_data += pixel_data->line_bytes;
	}

    /* 释放文件所占的缓存 */
    free(file_buf);

    return 0;
}

static int bmp_free_pixel_data(struct pixel_data *pixel_data)
{
    return 0;
}

static int is_support_bmp(const char *file_name)
{
    int fd,ret;
    char buf[8];

    /* 打开文件 */
    fd = open(file_name,O_RDONLY);
    if(fd < 0){
        DP_ERR("%s:open failed!\n",__func__);
        return errno;
    }

    if((ret = read(fd,buf,8)) != 8){
        DP_ERR("%s:read failed!\n",__func__);
        close(fd);
        return errno;
    }

    close(fd);
    
	if (buf[0] != 0x42 || buf[1] != 0x4d)
		return 0;
	else
		return 1;
}

static struct picfmt_parser bmp_parser = {
    .name = "bmp",
    .get_pixel_data = bmp_get_pixel_data,
    .free_pixel_data = bmp_free_pixel_data,
    .is_support = is_support_bmp,
    .is_enable = 1,
};

int bmp_init(void)
{
    return register_picfmt_parser(&bmp_parser);
}
