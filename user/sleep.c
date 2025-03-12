#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


//  如果用户忘记传入参数了, 报错并退出. 不然就parse输入的参数为int, 直接调用系统函数sleep.
int main(int argc, char **argv){
    if(argc < 2)
        printf("Usage: sleep <ticks\n>");
    
    int ret = atoi(argv[1]);
    sleep(ret);
    exit(0);
}