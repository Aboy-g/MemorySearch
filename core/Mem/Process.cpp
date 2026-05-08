#include "Process.hpp"
#include <dirent.h>
#include <sys/mman.h>   // for PROT_* constants
std::vector<ProcMap> Process::get_process_maps(int pid){
    std::vector<ProcMap> maps;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE* fp = fopen(path, "r");
    if (!fp) {
        return maps;  // 打开失败，返回空向量
    }

    char line[2048];  // 足够长的缓冲区
    while (fgets(line, sizeof(line), fp)) {
        // 跳过空行
        if (line[0] == '\0' || line[0] == '\n') continue;

        ProcMap map;
        // 解析格式: start-end perms offset dev inode pathname
        unsigned long long start, end, off;
        unsigned long inode;
        char perms[8] = {0};   // 权限字符，如 r-xp
        char dev[16] = {0};    // 设备号，如 08:01

        // 使用 %n 来获取已解析的字符位置，便于提取路径名
        int consumed = 0;
        int matched = sscanf(line, "%llx-%llx %7s %llx %15s %lu %n",
                             &start, &end, perms, &off, dev, &inode, &consumed);
        if (matched < 6) {
            // 某些行可能没有 inode（如特殊区域），尝试只解析前5个字段，路径名留空
            consumed = 0;
            matched = sscanf(line, "%llx-%llx %7s %llx %15s %n",
                             &start, &end, perms, &off, dev, &consumed);
            if (matched < 5) {
                continue;  // 格式严重错误，跳过
            }
            inode = 0;
            map.pathname.clear();
        } else {
            // 成功解析到 inode，提取路径名
            const char* path_start = line + consumed;
            // 跳过前导空格
            while (*path_start && isspace((unsigned char)*path_start)) path_start++;
            if (*path_start) {
                // 去除末尾换行符
                size_t len = strlen(path_start);
                if (len > 0 && path_start[len-1] == '\n') {
                    map.pathname.assign(path_start, len-1);
                } else {
                    map.pathname = path_start;
                }
            } else {
                map.pathname.clear();
            }
        }

        // 填充基本字段
        map.startAddress = start;
        map.endAddress = end;
        map.length = static_cast<size_t>(end - start);
        map.offset = off;
        map.dev = dev;
        map.inode = inode;

        // 解析权限字符串
        map.readable   = (perms[0] == 'r');
        map.writeable  = (perms[1] == 'w');
        map.executable = (perms[2] == 'x');
        char sharing = perms[3];
        map.is_private = (sharing == 'p');
        map.is_shared  = (sharing == 's');

        // 设置 protection 标志
        map.protection = 0;
        if (map.readable)   map.protection |= PROT_READ;
        if (map.writeable)  map.protection |= PROT_WRITE;
        if (map.executable) map.protection |= PROT_EXEC;

        // 便捷标志
        map.is_ro = (map.readable && !map.writeable && !map.executable);
        map.is_rw = (map.readable && map.writeable && !map.executable);
        map.is_rx = (map.readable && !map.writeable && map.executable);

        maps.push_back(std::move(map));
    }

    fclose(fp);
    return maps;
}

int Process::get_pid_by_name(const char *process_name)
{
    int id;
    pid_t pid = -1;
    DIR *dir;
    FILE *fp;
    char filename[32];
    char cmdline[256];

    struct dirent *entry;
    if (process_name == NULL) {
        return -1;
    }
    dir = opendir("/proc");
    if (dir == NULL) {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        id = atoi(entry->d_name);
        if (id != 0) {
            sprintf(filename, "/proc/%d/cmdline", id);
            fp = fopen(filename, "r");
            if (fp) {
                fgets(cmdline, sizeof(cmdline), fp);
                fclose(fp);

                if (strcmp(process_name, cmdline) == 0) {
                    /* process found */
                    pid = id;
                    break;
                }
            }
        }
    }

    closedir(dir);
    return pid;
}
