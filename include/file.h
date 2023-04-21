#ifndef __FILE_H
#define __FILE_H

enum file_type
{
    FILETYPE_DIR = 0,
    FILETYPE_REG
};

/* 这里是所能识别的几种文件类型 */
enum filetype_file
{
    FILETYPE_FILE_BMP = 1,      //这里为什么要搞成1,为了方便组织图标数据数组
    FILETYPE_FILE_JPEG,
    FILETYPE_FILE_PNG,
    FILETYPE_FILE_GIF,
    FILETYPE_FILE_TXT,
    FILETYPE_FILE_OTHER,
    FILETYPE_FILE_MAX,
};

/* 表示一个目录项 */
struct dir_entry
{
    char name[256];
    char type;
    char file_type;
};

// int is_reg_file(const char*);
// /* 常规目录是指除/sbin等几个特殊目录以外的其他的目录 */
// int is_reg_dir(const char *);
// int is_dir(const char *);
int get_dir_contents(const char *,struct dir_entry ***,unsigned int*);
int get_pic_dir_contents(const char *file_full_name,struct dir_entry ***pic_contents,unsigned int *pic_nums,int *cur_pic_index,char **cur_dir);
void free_dir_contents(struct dir_entry **dir_contents,unsigned int dirent_nums);
int get_file_type(const char *path,const char *name);

#endif