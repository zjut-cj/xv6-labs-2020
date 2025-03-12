#include "kernel/types.h"
#include "user/user.h"

// 用pipeline来实现Sieve质数算法

// 每个进程被分配一个质数,该进程会把该该质数打印在屏幕上
// 进程从左手边的进程不断接收数字,如果这个数字是自己质数的倍数,就过滤掉,不然就 fork 出一个新的进程,把这个数字分配并传给这个进程
// 每个进程都需要等待它的子进程结束后才可以退出,形成一条进程生命周期依赖链

void runprocess(int listenfd){
    int my_num = 0;
    int forked = 0;
    int passed_num = 0;
    int pipes[2];

    while(1){
        int read_bytes = read(listenfd,&passed_num,4);
        
        // 左邻居没有数据可以读取了
        if(read_bytes == 0){
            close(listenfd);    // 关左邻居的读端
            // 如果当前进程有子进程
            if(forked){
                close(pipes[1]);
                int child_pid;
                wait(&child_pid);
            }
            exit(0);    // 退出进程
        }

        // 如果当前进程是第一个进程
        if(my_num == 0){
            my_num = passed_num;
            printf("prime %d\n",my_num);
        }

        // 如果接收的数字不是自己数字的倍数,就进行传递
        if(passed_num % my_num != 0){
            // 如果当前进程没有子进程
            if(!forked){
                pipe(pipes);
                forked = 1;
                int ret = fork();
                if(ret == 0){
                    // 子进程
                    close(pipes[1]);    // 关闭管道写端
                    close(listenfd);    // 关闭上个管道的读端
                    runprocess(pipes[0]);   // 递归传递
                }else{
                    close(pipes[0]);
                }
            }
            // 传递数字给右邻居
            write(pipes[1],&passed_num,4);
        }
    }

}

int main(int argc,char *argv[]){
    int pipes[2];
    pipe(pipes);

    for(int i = 2; i <= 35;i++)
        write(pipes[1],&i,4);
    
    close(pipes[1]);
    runprocess(pipes[0]);
    exit(0);

}