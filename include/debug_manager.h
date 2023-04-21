#ifndef  __DEBUG_MANAGER
#define __DEBUG_MANAGER

#include "config.h"

/* 信息的调试级别,数值起小级别越高 */
#define	APP_EMERG	"<0>"	/* system is unusable			*/
#define	APP_ALERT	"<1>"	/* action must be taken immediately	*/
#define	APP_CRIT	"<2>"	/* critical conditions			*/
#define	APP_ERR	    "<3>"	/* error conditions			*/
#define	APP_WARNING	"<4>"	/* warning conditions			*/
#define	APP_NOTICE	"<5>"	/* normal but significant condition	*/
#define	APP_INFO	"<6>"	/* informational			*/
#define	APP_DEBUG	"<7>"	/* debug-level messages			*/

#define DEFAULT_DBGLEVEL 4

#define DP_EMERG(format,...) debug_print(APP_EMERG format,##__VA_ARGS__)
#define DP_ALERT(format,...) debug_print(APP_ALERT format,##__VA_ARGS__)
#define DP_CRIT(format,...) debug_print(APP_CRIT format,##__VA_ARGS__)
#define DP_ERR(format,...) debug_print(APP_ERR format,##__VA_ARGS__)
#define DP_WARNING(format,...) debug_print(APP_WARNING format,##__VA_ARGS__)
#define DP_NOTICE(format,...) debug_print(APP_NOTICE format,##__VA_ARGS__)
#define DP_INFO(format,...) debug_print(APP_INFO format,##__VA_ARGS__)
#define DP_DEBUG(format,...) debug_print(APP_DEBUG format,##__VA_ARGS__)

struct debuger_struct
{
    const char *name;
    struct debuger_struct *next;
    void (*init)(void);
    void (*enable)(void);
    void (*disable)(void);
    void (*exit)(void);
    int (*print)(const char *str);
    unsigned int is_enable:1;
    unsigned int is_initialized:1;
};

int register_debuger(struct debuger_struct *);
int unregister_debuger(struct debuger_struct *);
struct debuger_struct *get_debuger_by_name(const char *);
/* 打印当前已注册的调试器的信息 */
void show_debuger(void);
int set_debug_level(unsigned int);

/* 使能或禁用某个调试器
 * 输入参数为以下格式字符串：name=1	—— 使能调试器 ；name=0 —— 禁用调试器；name为调试器名字 */
int set_debug_channel(const char *);
int debug_print(const char *fmt,...);

int init_debuger_channel(struct debuger_struct *);

int stdout_init(void);
int stdout2_init(void);
int stdout3_init(void);

int debug_init(void);

#endif // ! __DEBUG_MANAGER