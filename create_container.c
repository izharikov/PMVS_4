#include <stdio.h>
#include <stdlib.h>
#define CONTAINER_NAME "/home/igor/FUSE/container"
#define PATH_CONTAINER_NAME "/home/igor/FUSE/path-container"
#define FILE_SIZE 1024 * 1024 * 300
#define BLOCK_SIZE 4096

struct info
{
    char data[BLOCK_SIZE];
    int next_block;
    int size;
};

void create_container()
{
    FILE *file_container = fopen(CONTAINER_NAME, "w+");
    int len = (FILE_SIZE) / (BLOCK_SIZE);
    for (int i = 1; i <= len; i++) {
        struct info block;
        block.size = 0;
        block.next_block = sizeof(struct info) * i;
        fwrite(&block, sizeof(struct info), 1, file_container);
    }
    fclose(file_container);
    FILE *file_path_container = fopen(PATH_CONTAINER_NAME, "w+");
    int begin = 0;
    fwrite(&begin, sizeof(int), 1, file_path_container);
    fclose(file_path_container);
}

int main(int argc, char *argv[])
{
    create_container();
    return 0;
}
