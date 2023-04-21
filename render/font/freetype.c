#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H 

#include "config.h"
#include "font_render.h"
#include "debug_manager.h"

static struct font_render freetype_font_render;

static FT_Library g_library;
static FT_Face g_face;
static FT_GlyphSlot g_slot;

static const char default_font_file[] = DEFAULT_FONT_FILE_PATH "/" "DENGB.TTF";
static const unsigned int default_font_size = 16;


static int freetype_font_init(void)
{
    int ret;

    ret = FT_Init_FreeType(&g_library);
    if(ret){
        DP_ERR("%s:FT_Library init failed!\n",__func__);
        return ret;
    }

    ret = FT_New_Face(g_library,default_font_file,0,&g_face);
    if(FT_Err_Unknown_File_Format == ret){
        DP_ERR("%s:Unknown_File_Format!\n",__func__);
        return ret;
    }else if(ret){
        DP_ERR("%s:create new face failed!\n",__func__);
        return ret;
    }
    freetype_font_render.font_file = default_font_file;

    FT_Set_Pixel_Sizes(g_face,default_font_size,0);
    freetype_font_render.font_size = default_font_size;

    g_slot = g_face->glyph;

    freetype_font_render.is_enable = 1;
    return 0;
}

static void freetype_font_exit(void)
{
    FT_Done_Face(g_face);
    FT_Done_FreeType(g_library);
}

static int freetype_set_font_size(unsigned int font_size)
{
    if(freetype_font_render.is_enable){
        FT_Set_Pixel_Sizes(g_face,font_size,0);
        freetype_font_render.font_size = font_size;
        return 0;
    }else{
        return -1;
    }
}

static int freetype_set_font_file(const char *font_file)
{
    if(freetype_font_render.is_enable){
        FT_Done_Face(g_face);
        
        FT_New_Face(g_library,font_file,0,&g_face);
        FT_Set_Pixel_Sizes(g_face,freetype_font_render.font_size,0);
        g_slot = g_face->glyph;
        
        return 0;
    }else{
        return -1;
    }
}

static int freetype_get_bitmap(unsigned int code,struct font_bitmap *bitmap)
{
    int ret;
    unsigned int glyph_index;
    int use_kerning = FT_HAS_KERNING(g_face);
    int previous_index = bitmap->previous_index;

    if(freetype_font_render.is_enable){

        glyph_index = FT_Get_Char_Index(g_face,code);

        if(use_kerning && previous_index && glyph_index){
            FT_Vector delta;
            FT_Get_Kerning(g_face,previous_index,glyph_index,FT_KERNING_DEFAULT,&delta);
            bitmap->pen_x += delta.x;
        }
        ret = FT_Load_Glyph(g_face,glyph_index,FT_LOAD_RENDER | FT_LOAD_MONOCHROME);
        if(ret){
            DP_ERR("%s:FT_Load_Glyph failed,errno :%d\n",__func__,ret);
            return ret;
        }

        bitmap->width   = g_slot->bitmap.width;
        bitmap->rows    = g_slot->bitmap.rows;
        bitmap->bpp     = 1;
        bitmap->buffer  = g_slot->bitmap.buffer;
        bitmap->pitch   = g_slot->bitmap.pitch;
        bitmap->private_data = g_slot;
        
        return 0;
    }   
    return -1;
}

static int freetype_get_glyph(unsigned int code,struct font_bitmap *bitmap)
{
    int ret;
    unsigned int glyph_index;
    FT_Glyph glyph;

    if(freetype_font_render.is_enable){
        glyph_index = FT_Get_Char_Index(g_face,code);
        /* 处理字距调整 */
        if(bitmap->use_kerning && bitmap->previous_index && glyph_index){
            FT_Vector  delta;
            FT_Get_Kerning(g_face, bitmap->previous_index, glyph_index, FT_KERNING_DEFAULT, &delta);
            bitmap->pen_x += delta.x >> 6;
        }
        ret = FT_Load_Glyph(g_face,glyph_index,FT_LOAD_DEFAULT);
        if(ret){
            DP_ERR("%s:FT_Load_Glyph failed\n",__func__);
            return ret;
        }

        bitmap->pen_x += g_slot->advance.x >> 6;
        /* 将字形作为私有数据返回 */
        ret = FT_Get_Glyph(g_slot,&glyph);
        bitmap->private_data = glyph;
        bitmap->previous_index = glyph_index;
        return 0;
    }
    return -1;
}

static struct font_render freetype_font_render = {
    .name = "freetype",
    .init = freetype_font_init,
    .exit = freetype_font_exit,
    .set_font_file = freetype_set_font_file,
    .set_font_size = freetype_set_font_size,
    .get_char_bitmap = freetype_get_bitmap,
    .get_char_glyph = freetype_get_glyph,
};

int freetype_render_init(void)
{
    return register_font_render(&freetype_font_render);
}