// 实验要求:从标准输入读取参数，并将这些参数传递给指定的程序执行

// 通过维护一个滑动窗户buffer读取命令
// 如果buf = [......],通过 read 读取进来 6 个字节，buf = [a b \n c d \n ....]
// 需要进行如下操作：
// 1、找到第一个'\n'的下标，用 xv6 提供的 strchr 函数得到下标为2
// 2、把 0~1 下标的 byte 转移到另一个buffer 作为额外参数
// 3、执行 fork + exec + wait 组合去执行真正的程序,使用额外参数
// 4、更新滑动窗口,将 0~2 下标的元素移除,把 3~9 的 byte已到队头,buffer = [c d \n]
// 上述操作一直循环直到 filr decriptor 0 已经关闭 && buffer 里的所有读入的 bytes 都处理完了

#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"

#define buf_size 512
int main(int argc, char* argv[]){
    char buf[buf_size + 1] ={0};      // 定义滑动窗口大小
    uint occupy = 0;
    char *xargv[MAXARG] = {0};      // MAXARG 是宏定义,在 param.h 文件中.指定一个进程在执行时所能接收的最大命令行参数
    int stdin_end = 0;
    
    // 将 argv 除程序名外的所有参数赋值给 xargv 数组
    for(int i = 1; i < argc;i++){
        xargv[i-1] = argv[i];
    }

    // 循环读取
    while(!(stdin_end && occupy == 0)){
        // 从标准输入读取数据
        if(!stdin_end){
            int remain_size = buf_size - occupy;    // 剩余可以读入的数据量
            int read_bytes = read(0, buf + occupy, remain_size);  // 实际读入的数据量,读取到 buf 缓冲区中
            // 读取报错
            if(read_bytes < 0){
                fprintf(2, "xargs:read return -1 error \n");
            }
            // 读取完就关闭输入,并将读取结束变量置为 1
            if(read_bytes == 0){
                close(0);
                stdin_end = 1;
            }
            occupy += read_bytes;
        }

        char *line_end = strchr(buf,'\n');  // 找隔断符,查找缓冲区第一行的结束位置,即'\n' 字符的位置
        while(line_end){
            char xbuf[buf_size + 1] = {0};      // 存储当前读取的行
            memcpy(xbuf, buf, line_end - buf);    // 将该行参数复制到 xbuf 中
            xargv[argc-1] = xbuf;
            int ret = fork();
            if(ret == 0){
                // 子进程：调用 exec 执行指定命令,并将命令参数传递给它,如果传递失败，打印错误并退出
                if(!stdin_end){
                    close(0);
                }

                if(exec(argv[1],xargv) < 0){
                    fprintf(2,"xargs: exec fails with -1\n");
                    exit(1);
                }
            }else{
                // 父进程
                // 更新滑动窗口
                memmove(buf, line_end + 1, occupy - (line_end - buf) - 1);    // -1是因为把 '\n'排除在外
                occupy = occupy-(line_end - buf + 1);            // 更新 bytes 个数，'+1'是因为要把'\n'算在内
                memset(buf + occupy, 0 , buf_size-occupy);      // 缓冲区未使用的部分进行清 0 ，确保旧数据不会影响.
                int pid;
                wait(&pid);     // 等待子进程结束
                line_end = strchr(buf,'\n');
            }
        }

    }
    exit(0);
}