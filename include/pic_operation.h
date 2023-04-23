#ifndef __PIC_OPERATION_H
#define __PIC_OPERATION_H

#include "display_manager.h"

int pic_zoom_in_rows(struct pixel_data *src_data,struct pixel_data *dst_data);
int pic_zoom_and_merge(struct pixel_data *src_data,struct pixel_data *dst_data);
int pic_zoom_with_same_bpp(struct pixel_data *dst_data,struct pixel_data *src_data);
int pic_zoom_with_same_bpp_and_rotate(struct pixel_data *src_data,struct pixel_data *dst_data,int rotate);
#endif // !__PIC_OPERATION_H