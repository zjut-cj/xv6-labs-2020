// Xv5 操作系统的用户空间头文件，提供基本的系统调用功能，如 fork(),pipe()m,read(),write()等
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// pingpong是利用系统调用函数fork和pipe在父进程和子进程前交换一个字节
// 父进程发送“ping”（字节 a）给子进程。
// 子进程接收“ping”，然后回复“pong”（同样是字节 a）给父进程
int main(int argc,char **argv){
    char buf[] = {'a'};
    int pid;
    // pp2c：父进程->子进程的通信    pc2p：子进程->父进程的通信
    int pp2c[2],cc2p[2];
    pipe(pp2c);     // pp2c[0] 读端    pp2c[1] 写端
    pipe(cc2p);     // pc2p[0] 读端    pc2p[1] 写端

    int ret = fork();

    if(ret == 0){
         // 子进程
         pid = getpid();
         close(pp2c[1]);
         close(cc2p[0]);
         read(pp2c[0],buf,1);    // 子进程从父进程读取数据,子进程要等待父进程数据才能进行下一步
         printf("%d: received ping\n",pid);
         write(cc2p[1],buf,1);   // 子进程写入数据
    }else{
        // 父进程
        pid = getpid();
        close(pp2c[0]);     // 关闭父进程的读端
        close(cc2p[1]);     // 关闭子进程的写端
        write(pp2c[1],buf,1);   // 父进程写入数据
        read(cc2p[0],buf,1);    // 父进程从子进程读取数组
        printf("%d: received pong\n",pid);
    }

    exit(0);    // 终止当前进程

}