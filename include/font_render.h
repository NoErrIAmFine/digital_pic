#ifndef __FONT_RENDER_H
#define __FONT_RENDER_H

struct font_bitmap
{
    unsigned int    rows;
    unsigned int    width;
    unsigned int    bpp;
    int             pen_x;
    int             pen_y;
    /* 相当于位图中每行的字节数 */
    int             pitch;
    unsigned char*  buffer;
    void            *private_data;
    unsigned int previous_index;
    unsigned int use_kerning:1;
};

struct font_render
{
    const char *name;
    /* 现在所用的字体文件名称 */
    const char *font_file;
    unsigned int font_size;
    struct font_render *next;
    int (*init)(void);
    void (*exit)(void);
    int (*set_font_size)(unsigned int);
    int (*set_font_file)(const char *);
    int (*get_char_bitmap)(unsigned int,struct font_bitmap *);
    int (*get_char_glyph)(unsigned int,struct font_bitmap *);
    int use_count;
    unsigned int is_enable:1;
};


int register_font_render(struct font_render*);
int unregister_font_render(struct font_render*);
void show_font_render(void);
struct font_render *get_font_render_by_name(const char*);

int freetype_render_init(void);
int font_render_init(void);

#endif // !__FONT_RENDER_H
