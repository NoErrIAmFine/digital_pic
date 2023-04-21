#ifndef __INPUT_MANAGER_H
#define __INPUT_MANAGER_H

#include <sys/time.h>

#define INPUT_TYPE_STDIN 1
#define INPUT_TYPE_TOUCHSCREEN 2
#define INPUT_TYPE_MOUSE 3

struct my_input_event
{
    int type;           /* 事件类型 */
    int x_pos;
    int y_pos;
    int key_value;      /* 比如如果是按键事件的话，此值表示按键值 */
    int presssure;      /* 对于触摸屏，0表示未按下，1表示按下 */
    int slot_id;        /* 多点触摸屏触点id */
    struct timeval time;/* 事件发生时间 */
};

struct input_device
{
    const char *name;
    struct input_device *next;
    int (*init)(void);
    void (*exit)(void);
    int (*get_input_event)(struct my_input_event *);
};

int register_input_device(struct input_device *);
int unregister_input_device(struct input_device *);
void show_input_device(void);
struct input_device *get_input_device_by_name(const char *);
void report_input_event(struct my_input_event*);
void get_input_event(struct my_input_event*);

int touchscreen_init(void);
int input_init(void);

#endif // !__INPUT_MANAGER_H
