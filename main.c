#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "debug_manager.h"
#include "display_manager.h"
#include "picfmt_manager.h"
#include "page_manager.h"
#include "font_decoder.h"
#include "font_render.h"
#include "input_manager.h"

int main(int argc,char *argv[])
{
    struct page_struct *page;
    struct page_param page_param;

    /* 先初始化 */
    debug_init();
    set_debug_level(7);

    font_render_init();
    font_decoder_init();
    show_font_render();
    display_init();
    
    picfmt_parser_init();
    input_init();
    page_init();

    page = get_page_by_name("main_page");

    page->run(&page_param);
    return 0;
}

