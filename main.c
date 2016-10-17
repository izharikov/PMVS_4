#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#define MIN(a,b) (((a)<(b))?(a):(b))
#define FILE_SIZE 1024 * 1024 * 300
#define BLOCK_SIZE 4096
#define PATH_SIZE 300

#define CONTAINER_NAME "/home/igor/FUSE/container"
#define PATH_CONTAINER_NAME "/home/igor/FUSE/path-container"

struct info
{
    char data[BLOCK_SIZE];
    int next_block;
    int size;
};

struct path_info
{
    char path[PATH_SIZE];
    int size;
    int first_block;
    int end_block;
    int cur_block;
    int offset;
};

int get_first_empty_block()
{
    FILE *file = fopen(PATH_CONTAINER_NAME, "r+");
    int begin;
    fread(&begin, sizeof(int), 1, file);
    fclose(file);
    return begin;
}

struct path_info* find_path_info(const char *path)
{
    FILE *file = fopen(PATH_CONTAINER_NAME, "r+");
    fseek(file, sizeof(int), SEEK_SET);
    struct path_info *pi = (struct path_info*) malloc(sizeof(struct path_info));
    while (fread(pi, sizeof(struct path_info), 1, file) != 0) {
        if (strcmp(pi->path, path) == 0) {
            fclose(file);
            return pi;
        }
    }
    fclose(file);
    return NULL;
}

void rewrite_path_record(const char *path, struct path_info x)
{
    FILE *file = fopen(PATH_CONTAINER_NAME, "r+");
    fseek(file, sizeof(int), SEEK_SET);
    int cur = sizeof(int);
    struct path_info pi;
    while (fread(&pi, sizeof(struct path_info), 1, file) != 0) {
        if (strcmp(path, pi.path) == 0) {
            fseek(file, cur, SEEK_SET);
            fwrite(&x, sizeof(struct path_info), 1, file);
            fflush(file);
            fclose(file);
            return;
        }
        cur += sizeof(struct path_info);
    }
    fclose(file);
}

static int do_read( const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi )
{
    memset(buffer, 0, sizeof(char) * strlen(buffer));
    struct path_info *pi = find_path_info(path);
    FILE *file_container = fopen(CONTAINER_NAME, "r+");
    struct info block;
    char data[size];
    size_t cur_size = 0;
    size_t global_size = 0;
    if (offset > 0)
        global_size = offset;
    while (global_size != pi->size && cur_size != size) {
        fseek(file_container, pi->cur_block, SEEK_SET);
        fread(&block, sizeof(struct info), 1, file_container);

        memcpy(buffer + cur_size, block.data, block.size);
        cur_size += block.size - pi->offset;
        global_size += block.size - pi->offset;
        pi->cur_block = block.next_block;
    }
    if (offset + size >= pi->size)
        pi->cur_block = pi->first_block;
    rewrite_path_record(path, *pi);
    fclose(file_container);
    return size;
}

int write_buf(int begin, const char *buf, size_t size)
{
    FILE *file_container = fopen(CONTAINER_NAME, "r+");
    struct info block;
    fseek(file_container, begin, SEEK_SET);
    int written_bytes = 0;
    while (written_bytes != size) {
        fread(&block, sizeof(struct info), 1, file_container);
        int count = MIN(size - written_bytes, BLOCK_SIZE - block.size);
        memcpy(block.data, buf + written_bytes, count);
        written_bytes += count;
        block.size = count;
        fseek(file_container, begin, SEEK_SET);
        fwrite(&block, sizeof(struct info), 1, file_container);
        begin = block.next_block;
    }
    fclose(file_container);
    return block.next_block;
}

void update_first_empty_block(int new_block)
{
    FILE *file = fopen(PATH_CONTAINER_NAME, "r+");
    fwrite(&new_block, sizeof(int), 1, file);
    fclose(file);
}

static int do_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct path_info *pi = find_path_info(path);
    int block_for_write;
    if (pi) {
        block_for_write = write_buf(pi->end_block, buf, size);

        struct path_info new_pi;
        strcpy(new_pi.path, path);
        new_pi.first_block = pi->first_block;
        new_pi.end_block = block_for_write;
        new_pi.size = pi->size + size;
        new_pi.cur_block = new_pi.first_block;
        new_pi.offset = 0;

        rewrite_path_record(path, new_pi);

        update_first_empty_block(block_for_write);

        return size;
    }
}

static int do_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi) 
{
	(void) offset;
	(void) fi;
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	FILE *file_path_container = fopen(PATH_CONTAINER_NAME, "r+");
	fseek(file_path_container, sizeof(int), SEEK_SET);
	struct path_info pi;
	while (fread(&pi, sizeof(struct path_info), 1, file_path_container) != 0)
		filler(buf, pi.path + 1, NULL, 0);
	fclose(file_path_container);
	return 0;
}

static int do_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }
    struct path_info *pi = find_path_info(path);
    if (pi) {
        stbuf->st_mode = S_IFREG | 0777;
        stbuf->st_nlink = 1;
        stbuf->st_size = pi->size;
        return 0;
    }
    return -ENOENT;
}

static int do_mknod(const char *path, mode_t mode, dev_t rdev)
{
    FILE *file_path_container = fopen(PATH_CONTAINER_NAME, "r+");

    struct path_info pi;
    strcpy(pi.path, path);
    pi.size = 0;

    fread(&pi.first_block, sizeof(int), 1, file_path_container);

    fseek(file_path_container, 0, SEEK_END);
    pi.end_block = pi.first_block;
    pi.cur_block = pi.first_block;
    pi.offset = 0;

    fwrite(&pi, sizeof(struct path_info), 1, file_path_container);
    fflush(file_path_container);

    FILE *file_container = fopen(CONTAINER_NAME, "r+");
    fseek(file_container, pi.first_block, SEEK_SET);

    struct info block;
    fread(&block, sizeof(struct info), 1, file_container);

    fseek(file_path_container, 0, SEEK_SET);
    fwrite(&block.next_block, sizeof(int), 1, file_path_container);
    fflush(file_path_container);
    fclose(file_path_container);
    fclose(file_container);
}

int get_size()
{
    FILE *file = fopen(PATH_CONTAINER_NAME, "r+");
    fseek(file, sizeof(int), SEEK_SET);
    struct path_info pi;
    int cnt = 0;
    while (fread(&pi, sizeof(struct path_info), 1, file) != 0)
        cnt ++;
    fclose(file);
    return cnt;
}

void remove_path(const char *path)
{
    FILE *file = fopen(PATH_CONTAINER_NAME, "r+");
    int begin;
    fread(&begin, sizeof(int), 1, file);
    fseek(file, sizeof(int), SEEK_SET);
    int size = get_size();
    struct path_info pi_array[size - 1];
    struct path_info pi;
    int i = 0;
    while (fread(&pi, sizeof(struct path_info), 1, file) != 0) {
        if (strcmp(pi.path, path) != 0)
            pi_array[i] = pi;
    }
    fseek(file, 0, SEEK_SET);
    fclose(file);
    file = fopen(PATH_CONTAINER_NAME, "w+");
    fwrite(&begin, sizeof(int), 1, file);
    for (int i = 0; i < size - 1; i++)
        fwrite(&pi_array[i], sizeof(struct path_info), 1, file);
    fclose(file);
}

static int do_unlink(const char *path)
{
    remove_path(path);
    return 0;
}

static int do_open(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

static int do_getxattr(const char* path, const char* name, char* value, size_t size)
{
    return 0;
}

static int do_listxattr(const char* path, char* list, size_t size)
{
    return 0;
}

static int do_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    return 0;
}

static int do_utimens(const char *path, const struct timespec ts[2])
{
    return 0;
}

static struct fuse_operations operations =
{
    .open = do_open,
    .write = do_write,
    .readdir = do_readdir,
    .getattr = do_getattr,
    .mknod = do_mknod,
    .setxattr = do_setxattr,
    .utimens = do_utimens,
    .listxattr = do_listxattr,
    .getxattr = do_getxattr,
    .read = do_read,
    .unlink = do_unlink
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &operations, NULL);
}
