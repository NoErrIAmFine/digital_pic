#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "picfmt_manager.h"
#include "debug_manager.h"

static int gif_get_pixel_data(const char *gif_file,struct pixel_data *pixel_data)
{
    return 0;
}

static int gif_free_pixel_data(struct pixel_data *pixel_data)
{
    return 0;
}

static int is_support_gif(const char *buf)
{
    return 0;
}

static struct picfmt_parser gif_parser = {
    .name = "gif",
    .get_pixel_data = gif_get_pixel_data,
    .free_pixel_data = gif_free_pixel_data,
    .is_support = is_support_gif,
    .is_enable = 1,
};

int gif_init(void)
{
    return register_picfmt_parser(&gif_parser);
}