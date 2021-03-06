//
// Created by 薛祥清 on 2017/4/1.
//


#include "Hdog.h"

/**
 * 获取进程的pid
 * @param process 进程名(包名)
 * @return
 */
int Hdog::getProcessPid(const char *process) {
    char cmd[MAX_BUFF];
    char buff[MAX_BUFF];
    char targetProcess[MAX_NAME_LEN];
    int targetPid = 0;

    //通过ps查找
    sprintf(cmd, "ps | grep %s", process);
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        printf("* Exec popen failed {%d, %s}\n", errno, strerror(errno));
        return 0;
    }
    while (fgets(buff, sizeof(buff), fp) != NULL) {
        //printf("Read buff: %s", buff);
        uint32_t pid = 0;
        sscanf(buff, "%*s\t%d  %*d\t%*d %*d %*x %*x %*c %s", &pid, targetProcess);
        //printf("Read pid:%d, process:%s\n", pid, targetProcess);
        if (strcmp(targetProcess, process) == 0) {
            targetPid = pid;
            break;
        }
    }
    fclose(fp);
    fp = NULL;
    if (targetPid > 0) {
        return targetPid;
    }

    //通过cmdline查找
    char fileName[MAX_NAME_LEN];
    DIR *dirProc;
    if ((dirProc = opendir("/proc")) == NULL) {
        printf("* Exec opendir failed {%d, %s}\n", errno, strerror(errno));
        return 0;
    }
    struct dirent *dirent;
    while ((dirent = readdir(dirProc)) != NULL) {
        sprintf(fileName, "/proc/%s/cmdline", dirent->d_name);
        fp = fopen(fileName, "r");
        if (fp == NULL) {
            continue;
        }
        fscanf(fp, "%s", targetProcess);
        fclose(fp);
        fp = NULL;

        if (strcmp(targetProcess, process) == 0) {
            targetPid = (uint32_t) atoi(dirent->d_name);
            break;
        }
    }
    closedir(dirProc);
    return targetPid;
}

/**
 * 获取进程的子线程
 * @param targetPid 目标进程
 * @return
 */
int Hdog::getSubPid(int targetPid) {
    char taskDirName[MAX_NAME_LEN];
    sprintf(taskDirName, "/proc/%d/task/", targetPid);
    DIR *rootDir = opendir(taskDirName);
    if (rootDir == NULL) {
        printf("* Open dir %s failed {%d, %s}\n", taskDirName, errno, strerror(errno));
        return 0;
    }

    struct dirent *dirent = NULL;
    struct dirent *lastDirent = NULL;
    while ((dirent = readdir(rootDir)) != NULL) {
        lastDirent = dirent;
    }
    if (lastDirent == NULL) {
        printf("* Last dirent is null\n");
        return 0;
    }
    closedir(rootDir);
    return atoi(lastDirent->d_name);
}

/**
 * 要附加进程pid
 * @param pid
 * @return
 */
int Hdog::attachPid(int pid) {
    char memName[MAX_NAME_LEN];
    sprintf(memName, "/proc/%d/mem", pid);
    long ret = ptrace(PTRACE_ATTACH, pid, NULL, NULL);
    if (ret != 0) {
        printf("* Attach %d failed {%d, %s}\n", pid, errno, strerror(errno));
        return 0;
    } else {
        int memFp = open(memName, O_RDONLY);
        if (memFp == 0) {
            printf("* Open %s failed: %d, %s\n", memName, errno, strerror(errno));
        }
        return memFp;
    }
}

int Hdog::dumpMems(int clonePid, int memFp, const char *dumpedPath) {
    printf("> Scanning dex ********************\n");

    char mapsName[MAX_NAME_LEN];
    sprintf(mapsName, "/proc/%d/maps", clonePid);
    FILE *mapsFp = fopen(mapsName, "r");
    if (mapsFp == NULL) {
        printf("* Open %s failed: %d, %s\n", mapsName, errno, strerror(errno));
        return 0;
    }

    int dexNum = 1;
    uint64_t start, end;
    char memLine[MAX_BUFF];
    char memName[MAX_NAME_LEN];
    char preMemName[MAX_NAME_LEN];
    MemRegion *memRegion = (MemRegion *) malloc(sizeof(MemRegion));
    while (fgets(memLine, sizeof(memLine), mapsFp) != NULL) {
        memName[0] = '\0'; //重置为空
        int rv = sscanf(memLine, "%llx-%llx %*s %*s %*s %*s %s\n", &start, &end, memName);
        if (rv < 2) {
            printf("* Scanf failed: %d, %s\n", errno, strerror(errno));
            continue;
        } else {
            if (strcmp(preMemName, memName) == 0 && strcmp(memName, "") != 0) continue; //忽略同名的分割区段
            strcpy(preMemName, memName);
            //printf("%llx-%llx %s\n", start, end, memName);
            memRegion->start = start;
            memRegion->end = end;
            memRegion->len = end - start;
            strcpy(memRegion->name, memName);
            dexNum = seekDex(memFp, memRegion, dumpedPath, dexNum);
        }
    }
    free(memRegion);
    fclose(mapsFp);
    printf("> Scanning end ********************\n \n");
}

int Hdog::seekDex(int memFp, MemRegion *memRegion, const char *dumpedPath, int dexNum) {
    char dexName[MAX_NAME_LEN];
    char dumpedName[MAX_NAME_LEN];
    char memName[MAX_NAME_LEN];
    if(strcmp(memRegion->name, "") == 0) {
        sprintf(dexName, "classes%d.dex", dexNum);
    }else{
        strcpy(memName, memRegion->name);
        char *token = strtok(memName, "/");
        while (token) {
            strcpy(dexName, token);
            token = strtok(NULL, "/");
        }
        if (dexName == NULL || strcmp(dexName, "") == 0) {
            sprintf(dexName, "classes%d.dex", dexNum);
        }
    }

    off64_t off = lseek64(memFp, memRegion->start, SEEK_SET);
    if (off == -1) {
        printf("* Lseek %d failed: %d, %s\n", memFp, errno, strerror(errno));
    } else {
        unsigned char *buffer = (unsigned char *) malloc(memRegion->len);
        ssize_t readLen = read(memFp, buffer, memRegion->len);
        if (strncmp((const char *) buffer, "dex\n035\0", 8) == 0) {
            //printf("MemInfo:%s, memLen:%ld, start:%llx, readLen:%ld\n", memRegion->name, memRegion->len, memRegion->start, readLen);
            DexHeader *dexHeader = (DexHeader *) malloc(sizeof(DexHeader));
            memcpy(dexHeader, buffer, sizeof(DexHeader));
            //printf("> Find dex %s, size:%x\n", memRegion->name, dexHeader->fileSize);
            sprintf(dumpedName, "%s/%s/%s", dumpedPath, "dex", dexName);
            dexNum = readMem(memFp, memRegion->start, dexHeader->fileSize, dumpedName, dexName, dexNum);
        }
        else if(strncmp((const char *) buffer, "dey\n036\0", 8) == 0) {
            if(strstr(memRegion->name, "system@framework") == NULL) { //忽略系统框架文件
                DexOptHeader *dexOptHeader = (DexOptHeader *) malloc(sizeof(DexOptHeader));
                memcpy(dexOptHeader, buffer, sizeof(DexOptHeader));
                uint32_t fileSize = dexOptHeader->optOffset + dexOptHeader->optLength;
                //printf("Find odex %s, size:%d\n", memRegion->name, fileSize);
                sprintf(dumpedName, "%s/%s/%s", dumpedPath, "dey", dexName);
                dexNum = readMem(memFp, memRegion->start, fileSize, dumpedName, dexName, dexNum);
            }
        }
        else{
            if (strstr(memRegion->name, ".dex") != NULL) {
                printf("* Ignore %s, %d %d %d %d,%d %d %d %d\n", memRegion->name, buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7]);
            }
        }
        free(buffer);
        buffer = NULL;
    }
    return dexNum;
}

int Hdog::readMem(int memFp, uint64_t start, uint32_t len, const char *dumpedName, const char *dexName, int dexNum){

    if (lseek64(memFp, start, SEEK_SET) != -1) {
        char *dexRaw = (char *) malloc(len * sizeof(char));
        ssize_t dexSize = read(memFp, dexRaw, len);
        //printf("xxxx %s, %ld, %d, %d, %s\n", dumpedName, odexSize, fileSize, errno, strerror(errno));
        if (writeMem(dexRaw, dexSize, dumpedName) == 1) {
            dexNum++;
            printf("> Dump success -> %s, len:%d\n", dexName, len);
        } else {
            printf("> Dump failed -> %s\n", dexName);
        }
        free(dexRaw);
        dexRaw = NULL;
    } else {
        printf("* Lseek %d failed: %d, %s\n", memFp, errno, strerror(errno));
    }
    return dexNum;
}

int Hdog::writeMem(const char *dexRaw, uint64_t size, const char *dumpedName) {
    int res = -1;
    FILE *fp = fopen(dumpedName, "wb");
    if (fwrite(dexRaw, sizeof(char), size, fp) == size) {
        res = 1;
    } else {
        printf("* Write %s failed: %d, %s\n", dumpedName, errno, strerror(errno));
    }
    fclose(fp);
    return res;
}


int main(int argc, char *argv[]) {
    int targetPid = 0;
    int attachPid = 0;
    int memFp;
    char dumpedPath[MAX_NAME_LEN];
    const char *packageName = argv[1]; //获取包名
    Hdog hdog;

    targetPid = hdog.getProcessPid(packageName);
    if (targetPid == 0) {
        printf("* Can`t find \"%s\"\n", packageName);
        return 0;
    } else {
        attachPid = hdog.getSubPid(targetPid);
        if (attachPid == 0) {
            printf("* Get sub pid failed\n");
            return 0;
        } else {
            printf("> Target pid:%d\n", targetPid);
            memFp = hdog.attachPid(attachPid);
            if (memFp == 0) {
                printf("* Attach %d failed\n", attachPid);
                return 0;
            } else {
                printf("> Attach pid:%d\n", attachPid);
                sprintf(dumpedPath, "%s%s", OUTPUT_PATH, packageName);
                hdog.dumpMems(attachPid, memFp, dumpedPath);

            }
        }
    }
}