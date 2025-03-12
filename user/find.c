#include "kernel/types.h"   // 定义基本数据类型
#include "kernel/fcntl.h"   // 定义文件操作,比如O_RDONLY
#include "kernel/fs.h"      // 定义文件系统相关结构
#include "kernel/stat.h"    // 文件状态结构
#include "user/user.h"      // 系统

// 查找目录树种具有特定名称的所有文件
// 给定一个初始路径和目标文件名, 要不断递归的扫描找到所有子目录前匹配的文件全路径. 
// 实验手册里让我们模仿ls用户函数的实现来实现这个功能. 主要是要学一下文件系统的操作.

// 从完整的文件路径名中提取最后一级的目录分隔符
char *basename(char *pathname){
    char *prev = 0;
    char *curr = strchr(pathname,'/');  // strchr函数用于在一个字符串中查找第一次出现指定字符的位置
    while(curr != 0){
        prev = curr;
        curr = strchr(prev + 1, '/');
    }
    return prev;
}

void find(char *curr_path,char *target){
    char buf[512], *p;
    int fd;
    struct stat st; // 获取文件状态
    struct dirent de;

    // 文件打不开报错
    // 2 表示向标准错误输出写入错误信息
    if((fd = open(curr_path, O_RDONLY)) < 0){
        fprintf(2,"find: cannot open %s\n",curr_path);
        return ;
    }

    // 文件状态无法获取报错
    if(fstat(fd, &st) < 0){
        fprintf(2,"find: cannot stat %s\n",curr_path);
        close(fd);
        return;
    }

    // 文件状态的类型为文件名或者文件的不同处理方式
    switch (st.type){
        // 普通文件
        case T_FILE:{
            char *f_name = basename(curr_path);
            int match = 1;
            // 0 处是文件结尾位置,不存在或者文件名不匹配
            if(f_name == 0 || strcmp(f_name + 1, target) != 0)
                match = 0;
            if(match)
                printf("%s\n",curr_path);
            close(fd);  // 关闭文件描述符
            break;
        }
        // 目录
        case T_DIR:{
            memset(buf, 0, sizeof(buf));
            uint  curr_path_len = strlen(curr_path);    
            memcpy(buf, curr_path, curr_path_len);
            buf[curr_path_len] = '/';   // 将要进入子目录或附加文件名
            p = buf + curr_path_len + 1;    // p 指针指向 buf[curr_path_len + 1]
            while(read(fd, &de, sizeof(de)) == sizeof(de)){
                if(de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name,"..") == 0)
                    continue;
                memcpy(p, de.name, DIRSIZ);
                p[DIRSIZ] = 0;  // 确保新路径以 '\0' 结束，形成合法的 C 字符串
                find(buf,target);   // 对新构造的路径继续进行查找
            }
            close(fd);
            break;
        }    
    }
}

int main(int argc,char *argv[]){
    if(argc != 3){
        printf("usage: find [directory] [target filename]\n");
        exit(1);
    }
    find(argv[1],argv[2]);
    exit(0);
}