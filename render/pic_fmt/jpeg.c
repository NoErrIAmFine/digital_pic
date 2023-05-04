#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <jpeglib.h>
#include <setjmp.h>

#include "picfmt_manager.h"
#include "debug_manager.h"

struct my_error_mgr
{
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
};

static int jpeg_get_pixel_data(const char *file_name,struct pixel_data *pixel_data)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *fp;
    unsigned char *line_buf;

    fp = fopen(file_name,"rb");
    if(!fp){
        DP_ERR("%s:fopen failed!\n",__func__);
        return -1;
    }

    /* 绑定默认错误处理函数 */
    cinfo.err = jpeg_std_error(&jerr);

    /* 创建jpeg解码对象 */
    jpeg_create_decompress(&cinfo);

    /* 指定图像文件 */
    jpeg_stdio_src(&cinfo,fp);

    /* 读取图像信息 */
    jpeg_read_header(&cinfo,TRUE);

    /* 设置解码参数 */
    cinfo.out_color_space = JCS_RGB;
    cinfo.scale_num = 1;
    cinfo.scale_denom = 1;

    /* 开始解码图像 */
    jpeg_start_decompress(&cinfo);

    /* 为图像数据分配空间 */
    pixel_data->width = cinfo.output_width;
    pixel_data->height = cinfo.output_height;
    pixel_data->bpp = cinfo.output_components * 8;
    pixel_data->line_bytes = pixel_data->width * cinfo.output_components;
    pixel_data->total_bytes = pixel_data->line_bytes * pixel_data->height;
    if(!pixel_data->buf){
        pixel_data->buf = malloc(pixel_data->total_bytes);
        if(!pixel_data->buf){
            DP_ERR("malloc failed!\n");
            return -ENOMEM;
        }
    }
    line_buf = pixel_data->buf;
    /* 读取数据 */
    while(cinfo.output_scanline < pixel_data->height){
        jpeg_read_scanlines(&cinfo,&line_buf,1);
        line_buf +=pixel_data->line_bytes;
    }

    /* 完成解码 */
    jpeg_finish_decompress(&cinfo);

    /* 释放资源 */
    jpeg_destroy_decompress(&cinfo);
    
    fclose(fp);
    return 0;
}

static int jpeg_free_pixel_data(struct pixel_data *pixel_data)
{
    return 0;
}

static void MyErrorExit(j_common_ptr cinfo)
{
    static char errStr[JMSG_LENGTH_MAX];
    
	struct my_error_mgr *my_err = (struct my_error_mgr *)cinfo->err;

    /* Create the message */
    (*cinfo->err->format_message) (cinfo, errStr);
    DP_INFO("%s\n", errStr);

	longjmp(my_err->setjmp_buffer, 1);
}

static int is_support_jpeg(const char *file_name)
{
    FILE *fp;
    struct jpeg_decompress_struct tDInfo;
    /* 默认的错误处理函数是让程序退出
     * 我们参考libjpeg里的bmp.c编写自己的错误处理函数
     */ 
	struct my_error_mgr tJerr;
    int iRet;

    fp = fopen(file_name,"rb");
    if(!fp){
        DP_ERR("%s:fopen failed!\n",__func__);
        return -1;
    }

	// 分配和初始化一个decompression结构体
	tDInfo.err               = jpeg_std_error(&tJerr.pub);
	tJerr.pub.error_exit     = MyErrorExit;

	if(setjmp(tJerr.setjmp_buffer)){
		/* 如果程序能运行到这里, 表示JPEG解码出错 */
        jpeg_destroy_decompress(&tDInfo);
		return 0;
	}
	
	jpeg_create_decompress(&tDInfo);

	// 用jpeg_read_header获得jpg信息
	jpeg_stdio_src(&tDInfo, fp);

    iRet = jpeg_read_header(&tDInfo, TRUE);

	jpeg_abort_decompress(&tDInfo);

    /* 释放资源 */
    jpeg_destroy_decompress(&tDInfo);
    fclose(fp);
    return (iRet == JPEG_HEADER_OK);
}


// static int is_support_jpeg(const char *buf)
// {
//     static const unsigned char jpeg_sign[] = {0xff,0xd8,0xff};
//     int jpeg_sign_size = sizeof(jpeg_sign);

//     if(strlen(buf) < jpeg_sign_size){
//         return 0;
//     }

//     if(memcmp(jpeg_sign,buf,jpeg_sign_size)){
//         return 0;
//     }
//     return 1;
// }

static struct picfmt_parser jpeg_parser = {
    .name = "jpeg",
    .get_pixel_data = jpeg_get_pixel_data,
    .free_pixel_data = jpeg_free_pixel_data,
    .is_support = is_support_jpeg,
    .is_enable = 1,
};

int jpeg_init(void)
{
    return register_picfmt_parser(&jpeg_parser);
}