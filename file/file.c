#include "file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

#include "debug_manager.h"
#include "picfmt_manager.h"
#include "font_decoder.h"

static const char *special_dirs[] = {"sbin", "bin", "usr", "lib", "proc", "tmp", "dev", "sys", NULL};

int is_reg_file(const char *path,const char *name)
{
    char full_name[256];
    struct stat f_stat;
    int ret;
    
    ret = snprintf(full_name,256,"%s/%s",path,name);
    if(ret < 0)
        return ret;

    if(!stat(full_name,&f_stat) && S_ISREG(f_stat.st_mode)){
        return 1;
    }else{
        return 0;
    }
}

/* 常规目录是指除/sbin等几个特殊目录以外的其他的目录 */
int is_reg_dir(const char *path,const char *name)
{
    int i,ret;
    const char *temp_dir;
    struct stat f_stat;
    char full_name[256];

    ret = snprintf(full_name,256,"%s/%s",path,name);
    if(ret < 0)
        return ret;

    temp_dir = special_dirs[0];
    i = 0;

    while(temp_dir){
        /* 如果文件名与列表中的某项相同,则直接返回0 */
        if(!strcmp(temp_dir,name)){
            return 0;
        }
        temp_dir = special_dirs[++i];
    }

    /* 在判断是否为目录 */
    if(!stat(full_name,&f_stat) && S_ISDIR(f_stat.st_mode)){
        return 1;
    }else{
        return 0;
    }
}

int is_dir(const char *path,const char *name)
{
    char full_name[256];
    struct stat f_stat;
    int ret = 0;

    ret = snprintf(full_name,256,"%s/%s",path,name);
    if(ret < 0)
        return ret;

    if(!stat(full_name,&f_stat) && S_ISDIR(f_stat.st_mode)){
        return 1;
    }else{
        return 0;
    }
}

/* 获取某指定目录下的内容, 目录项数量保存到dir_nums*/
int get_dir_contents(const char *dir_name,struct dir_entry ***dir_contents,unsigned int *dir_nums)
{
    struct dirent **orig_dirents;
    struct dir_entry **my_dir_entrys;
    unsigned int nums;
    int i,my_dir_index;
    nums = scandir(dir_name,&orig_dirents,NULL,alphasort);

    if(nums < 0){
        DP_ERR("%s:scandir error!\n",__func__);
        return nums;
    }
    
    /* 为自己的目录信息数组分配空间,忽略.和..这两项 */
    my_dir_entrys = malloc((nums - 2) * sizeof(struct dir_entry));
    if(!my_dir_entrys){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;;
    }

    /* 第一次遍历原始目录项数组,找出目录并保存 */
    my_dir_index = 0;
    for(i = 0 ; i < nums ; i++){
        /* 忽略.和..这两项 */
        if(!strcmp(orig_dirents[i]->d_name,".") || !strcmp(orig_dirents[i]->d_name,"..")){
            free(orig_dirents[i]);
            orig_dirents[i] = NULL;
            continue;
        }
        if(is_dir(dir_name,orig_dirents[i]->d_name)){
            /* 为自己的信息数组分配空间 */
            my_dir_entrys[my_dir_index] = malloc(sizeof(struct dir_entry));
            if(!my_dir_entrys[my_dir_index]){
                DP_ERR("%s:malloc failed!\n",__func__);
                return -ENOMEM;
            }
            memset(my_dir_entrys[my_dir_index],0,sizeof(struct dir_entry));
            /* 将信息复制到自己的数组中去 */
            strncpy(my_dir_entrys[my_dir_index]->name,orig_dirents[i]->d_name,256);
            my_dir_entrys[my_dir_index]->name[255] = '\0';
            my_dir_entrys[my_dir_index]->type = FILETYPE_DIR;
            my_dir_index++;
            /* 释放scandir函数分配的内存 */
            free(orig_dirents[i]);
            orig_dirents[i] = NULL;
        }
    }

    /* 再次遍历原始目录项数组,找出文件并保存 */
    for(i = 0 ; i < nums ; i++){
        if(!orig_dirents[i])
            continue;

        if(is_reg_file(dir_name,orig_dirents[i]->d_name)){
            /* 为自己的信息数组分配空间 */
            my_dir_entrys[my_dir_index] = malloc(sizeof(struct dir_entry));
            if(!my_dir_entrys[my_dir_index]){
                DP_ERR("%s:malloc failed!\n",__func__);
                return -ENOMEM;
            }
            memset(my_dir_entrys[my_dir_index],0,sizeof(struct dir_entry));
            /* 将信息复制到自己的数组中去 */
            strncpy(my_dir_entrys[my_dir_index]->name,orig_dirents[i]->d_name,256);
            my_dir_entrys[my_dir_index]->name[255] = '\0';
            my_dir_entrys[my_dir_index]->type = FILETYPE_REG;
            my_dir_index++;

            /* 释放scandir函数分配的内存 */
            free(orig_dirents[i]);
            orig_dirents[i] = NULL;
        }
    }
    /* 返回数据 */
    *dir_nums = my_dir_index;
    *dir_contents = my_dir_entrys;

    /* 检查一遍scandir函数分配的空间是否还有没有释放的 */
    for(i = 0 ; i < nums ; i++){
        if(orig_dirents[i]){
            free(orig_dirents[i]);
        }
    }
    free(orig_dirents);

    return 0;
}

/* 从一个完整的文件名中获取目录的名字 */
char *get_dir_name(const char *file_full_name)
{
    char *buf_end,*buf_start,*dir_name;
    unsigned int n = strlen(file_full_name);
    char buf[n + 1];

    strcpy(buf,file_full_name);
    buf_start = buf;
    buf[n] = '\0';

    /* 如果不是绝对路径，直接退出 */
    if(buf_start[0] != '/'){
        return (void *)(-1);
    }   

    // "//xxx" 这样的根目录也是合法的,这里是为了删除前面多余的斜线
    if(buf_start[0] == '/' && buf_start[1] == '/'){
        buf_start++;       
    }

    /* 解析出文件所出的目录 */
    buf_end = strrchr(buf_start,'/');

    if(buf_end == buf_start){  
        // '/xxx',处于根目录下的文件       
        dir_name = malloc(2);
        dir_name[0] = '/';
        dir_name[1] = '\0';
    }else{
        *buf_end = '\0';
        dir_name = malloc(strlen(buf_start) + 1);
        strcpy(dir_name,buf_start);
    }

    return dir_name;
}

/* 从一个完整的文件名中获取文件的名字 */
char *get_file_name(const char *file_full_name)
{
    char *buf_end,*buf_start,*file_name;
    unsigned int n = strlen(file_full_name);
    char buf[n + 1];

    strcpy(buf,file_full_name);
    buf_start = buf;
    buf[n] = '\0';

    /* 如果不是绝对路径，直接退出 */
    if(buf_start[0] != '/'){
        return (void *)(-1);
    }   

    // "//xxx" 这样的根目录也是合法的,这里是为了删除前面多余的斜线
    if(buf_start[0] == '/' && buf_start[1] == '/'){
        buf_start++;       
    }

    /* 找到最后一个’/‘所在的位置*/
    buf_end = strrchr(buf_start,'/');

    buf_end++;

    file_name = malloc(strlen(buf_end) + 1);
    strcpy(file_name,buf_end);

    return file_name;
}

/* 给定一图片,找出与该文件同属一个目录下的其他图片文件,将这些信息存入一数组 */
int get_pic_dir_contents(const char *file_full_name,struct dir_entry ***pic_contents,unsigned int *pic_nums,int *cur_pic_index,char **cur_dir)
{
    unsigned int n = strlen(file_full_name);
    char *file_name,*file_path;
    char *buf_end;
    struct dirent **orig_dir_contents;
    struct dir_entry **pic_dir_contents;
    unsigned int orig_dir_nums;
    unsigned int founded_pic_nums;
    unsigned int i;
    int ret;
    char file_type_num;
    char buf[n + 1];

    if(*file_full_name == '/' && *(file_full_name + 1) == '/'){
        file_full_name++;       // "//xxx" 这样的根目录也是合法的,这里是为了删除前面多余的斜线
    }
    strcpy(buf,file_full_name);
    buf[n] = '\0';

    /* 先解析出文件所出的目录 */
    buf_end = strrchr(buf,'/');
    if(!buf_end || buf_end == &buf[n - 1]){
        /* 文件名中不含'/',或'/'在最后(说明这是个目录)视为非法,直接退出 */
        return -1;
    }
    if(buf_end == buf){         // '/xxx',处于根目录下的文件
        file_path = "/";
        file_name = buf_end + 1;
    }else{
        *buf_end = '\0';
        file_path = buf;
        file_name = buf_end + 1;
    }

    /* 保存当前目录 */
    if(*cur_dir){
        free(*cur_dir);
    }
    *cur_dir = malloc(strlen(file_path + 1));
    if(!(*cur_dir)){
        DP_ERR("%s:malloc failed!\n",__func__);
        return -ENOMEM;
    }
    strcpy(*cur_dir,file_path);
    
    /* 读出原始目录信息 */
    orig_dir_nums = scandir(file_path,&orig_dir_contents,NULL,alphasort);
    if(orig_dir_nums < 0){
        DP_ERR("%s:scandir failed",__func__);
        ret = errno;
        goto free_cur_dir;
    }
    
    pic_dir_contents = malloc(orig_dir_nums * sizeof(struct dir_entry*));
    if(!pic_dir_contents){
        DP_ERR("%s:malloc failed!\n",__func__);
        ret = -ENOMEM;
        goto free_cur_dir;
    }

    founded_pic_nums = 0;  
    /* 遍历初始目录信息数组,找出其中的文件 */
    for(i = 0 ; i < orig_dir_nums ; i++){
        if(is_reg_file(file_path,orig_dir_contents[i]->d_name)){
            file_type_num = get_file_type(file_path,orig_dir_contents[i]->d_name);
            if(file_type_num == FILETYPE_FILE_BMP || file_type_num == FILETYPE_FILE_JPEG || \
               file_type_num == FILETYPE_FILE_PNG || file_type_num == FILETYPE_FILE_GIF){
                /* 能到这里,说明才是一个符合条件的文件 */
                pic_dir_contents[founded_pic_nums] = malloc(sizeof(struct dir_entry));
                if(!pic_dir_contents[founded_pic_nums]){
                    DP_ERR("%s:malloc failed!\n",__func__);
                    ret = -ENOMEM;
                    goto free_pic_contents;
                }
                strcpy(pic_dir_contents[founded_pic_nums]->name,orig_dir_contents[i]->d_name);
                pic_dir_contents[founded_pic_nums]->name[255] = '\0';
                pic_dir_contents[founded_pic_nums]->type = FILETYPE_REG;
                pic_dir_contents[founded_pic_nums]->file_type = file_type_num;
                founded_pic_nums++;

                /* 释放原有目录信息数组的空间 */
                free(orig_dir_contents[i]);
                orig_dir_contents[i] = NULL;
                continue;
            }
        }
         /* 释放原有目录信息数组的空间 */
        free(orig_dir_contents[i]);
        orig_dir_contents[i] = NULL;
    }
    /* 释放scandir函数分配的空间 */
    free(orig_dir_contents);
    /* 返回找到的图片的项数 */
    *pic_nums = founded_pic_nums;

    /* 将pic_dir_contents中多余的项置为NULL */
    for(i = founded_pic_nums ; i < orig_dir_nums ; i++){
        pic_dir_contents[i] = NULL;
    }

    /* 返回目录项数组 */
    *pic_contents = pic_dir_contents;

    /* 最后一件事,找到当前正被打开的文件在目录项数组中的索引 */
    for(i = 0 ; i < founded_pic_nums ; i++){
        if(!strcmp(pic_dir_contents[i]->name,file_name)){
            *cur_pic_index = i;
            break;
        }
    }
    /* 如果没有找到当前正被打开的文件,这很奇怪,直接返回出错把 */
    if(i == founded_pic_nums){
        ret = -1;
        goto free_pic_contents;
    }
    return 0;

free_pic_contents:
    for(i = 0 ; i < orig_dir_nums ; i++){
        if(orig_dir_contents[i]){
            free(orig_dir_contents[i]);
        }
    }
    if(orig_dir_contents){
        free(orig_dir_contents);
    }
    for(founded_pic_nums-- ; founded_pic_nums >= 0 ; founded_pic_nums--){
        free(pic_dir_contents[founded_pic_nums]);
    }
    free(pic_dir_contents);
    *pic_contents = NULL;
    *pic_nums = 0;
    *cur_pic_index = -1;
    
free_cur_dir:
    *cur_dir = NULL;
    free(*cur_dir);
    return ret;
}

void free_dir_contents(struct dir_entry **dir_contents,unsigned int dirent_nums)
{
    int i;

    for(i = 0 ; i < dirent_nums ; i++){
        if(dir_contents[i]){
            free(dir_contents[i]);
        }
    }

    free(dir_contents);
}

int get_file_type(const char *path,const char *name)
{
    int ret = 0;
    int i,fd;
    struct picfmt_parser *parser;
    struct font_decoder *decoder;
    char buf[4];
    const char *decoder_names[] = {
        "ascii","utf-8","utf-16be","utf-16le",
    };

    const char *parser_names[] = {
        [FILETYPE_FILE_BMP] = "bmp",
        [FILETYPE_FILE_JPEG] = "jpeg",
        [FILETYPE_FILE_PNG] = "png",
        [FILETYPE_FILE_GIF] = "gif"         //至少,得保证此枚举对应的整数是最大的
    };
    char full_name[256];

    if(!path || !name){
        return -1;
    }

    ret = snprintf(full_name,256,"%s/%s",path,name);
    if(ret < 0)
        return ret;
    
    /* 先判断是不是某种格式的图片 */
    for(i = 1 ; i <= FILETYPE_FILE_GIF; i++){
        parser = get_parser_by_name(parser_names[i]);
        if(parser && parser->is_support){
            if(parser->is_support(full_name)){
                return i;
            }
        }
    }

    /* 再判断是不是文本文件 */
    for(i = 0 ; i < 4; i++){
        decoder = get_font_decoder_by_name(decoder_names[i]);
        if(decoder && decoder->is_support){
            if(decoder->is_support(full_name)){
                return FILETYPE_FILE_TXT;
            }
        }
    }
    
    return FILETYPE_FILE_OTHER;
}