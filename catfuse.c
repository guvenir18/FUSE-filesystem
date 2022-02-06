// İlke Anıl Güvenir - 150180042
// Furkan Pala - 150180109

#define FUSE_USE_VERSION 26
#define MAX_TYPE 100

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/xattr.h>
#include <dirent.h>
#include <unistd.h>
#include <magic.h>
#include <fuse.h>

char *root_path;
struct magic_set *magic;

char *get_type(char *path)
{
    const char *magic_out = magic_file(magic, path);
    int begin, end;
    for (unsigned int i = 0; i < strlen(magic_out); i++)
    {
        if (magic_out[i] == '/')
        {
            begin = i + 1;
        }
        if (magic_out[i] == ';')
        {
            end = i;
        }
    }

    int n = end - begin;
    char *type = malloc(sizeof(char) * n + 1);
    strncpy(type, magic_out + begin, n);
    type[n] = '\0';

    return type;
}

static char *translate_path(const char *path)
{
    char *rPath = malloc(sizeof(char) * (strlen(path) + strlen(root_path) + 1));

    strcpy(rPath, root_path);
    if (rPath[strlen(rPath) - 1] == '/')
    {
        rPath[strlen(rPath) - 1] = '\0';
    }
    strcat(rPath, path);

    return rPath;
}

static char *get_file_path(const char *path)
{
    char *path_copy = strdup(path);
    char *token1 = strtok(path_copy, "/");
    (void)token1;
    char *token2 = strtok(NULL, "/");
    char *token3 = malloc(sizeof(char) * strlen(token2) + 1);
    token3[0] = '/';
    strcpy(token3 + 1, token2);
    free(path_copy);

    return token3;
}

static int type_exists(const char *type, char **unique_types, int *length)
{
    for (int i = 0; i < *length; i++)
    {
        if (strcmp(type, unique_types[i]) == 0)
        {
            return 0;
        }
    }

    return 1;
}

static void add_type(const char *type, char **unique_types, int *length)
{
    unique_types[(*length)++] = strdup(type);
}

static void free_types(char **unique_types, int length)
{
    for (int i = 0; i < length; i++)
    {
        free(unique_types[i]);
    }
}

static int count_slashes(const char *path)
{
    int i = 0;
    int n_slash = 0;
    while (path[i] != '\0')
    {
        if (path[i] == '/')
        {
            n_slash++;
        }
        i++;
    }

    return n_slash;
}

static int catfuse_getattr(const char *path, struct stat *st_data)
{
    char *upath = translate_path("/");
    int res;

    // if path == "/"
    if (strcmp(path, "/") == 0)
    {
        res = lstat(upath, st_data);
        if (res == -1)
        {
            free(upath);
            return -errno;
        }
    }
    // if path == "/plain/file.txt"
    else if (count_slashes(path) == 2)
    {
        char *file_path = get_file_path(path);
        upath = translate_path(file_path);
        res = lstat(upath, st_data);
        free(file_path);
        if (res == -1)
        {
            free(upath);
            return -errno;
        }
    }
    // if path == "/plain"
    else
    {
        st_data->st_mode = S_IFDIR | 0755;
    }

    free(upath);

    return 0;
}

static int catfuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    DIR *dp;
    struct dirent *de;
    int res;
    char *unique_types[MAX_TYPE];
    int length = 0;

    (void)offset;
    (void)fi;

    char *upath = translate_path("/");

    dp = opendir(upath);
    if (dp == NULL)
    {
        free(upath);
        res = -errno;
        return res;
    }
    while ((de = readdir(dp)) != NULL)
    {
        char *full_path, *type;
        full_path = malloc(sizeof(char) * (strlen(upath) + strlen(de->d_name) + 1));
        strcpy(full_path, upath);
        strcat(full_path, de->d_name);
        type = get_type(full_path);

        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;

        // Do not put . and .. inside "directory"
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
        {
            if (filler(buf, de->d_name, &st, 0))
            {
                free(type);
                free(full_path);
                break;
            }
        }
        // List files as their type dirs
        else if (strcmp(path, "/") == 0)
        {
            // Check if the type exists
            if (type_exists(type, unique_types, &length) != 0)
            {
                add_type(type, unique_types, &length);
                if (filler(buf, type, &st, 0))
                {
                    free(type);
                    free(full_path);
                    break;
                }
            }
        }
        // Get type from path
        // List files if file.type == type
        else
        {
            if (strcmp(type, path + 1) == 0)
            {
                if (filler(buf, de->d_name, &st, 0))
                {
                    free(type);
                    free(full_path);
                    break;
                }
            }
        }

        free(type);
        free(full_path);
    }

    free_types(unique_types, length);
    free(upath);

    closedir(dp);

    return 0;
}

static int catfuse_access(const char *path, int mode)
{
    int res;

    // if path == "/plain/file.txt"
    if (count_slashes(path) == 2)
    {
        char *file_path = get_file_path(path);
        char *upath = translate_path(file_path);
        res = access(upath, mode);
        free(file_path);
        free(upath);

        if (res == -1)
        {
            free(upath);
            return -errno;
        }

        return 0;
    }
    else if (strcmp(path, "/") == 0)
    {
        char *upath = translate_path("/");
        res = access(upath, mode);
        free(upath);

        if (res == -1)
        {
            free(upath);
            return -errno;
        }

        return 0;
    }

    return 0;
}

static int catfuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *finfo)
{
    int fd;
    int res;
    char *file_path = get_file_path(path);
    char *upath = translate_path(file_path);

    (void)finfo;

    fd = open(upath, O_RDONLY);

    free(file_path);
    free(upath);

    if (fd == -1)
    {
        res = -errno;
        return res;
    }
    res = pread(fd, buf, size, offset);

    if (res == -1)
    {
        res = -errno;
    }
    close(fd);

    return res;
}

static int catfuse_open(const char *path, struct fuse_file_info *finfo)
{
    int res;

    int flags = finfo->flags;

    if ((flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_CREAT) || (flags & O_EXCL) || (flags & O_TRUNC) || (flags & O_APPEND))
    {
        return -EROFS;
    }

    char *file_path = get_file_path(path);
    char *upath = translate_path(file_path);

    res = open(upath, flags);

    free(upath);
    free(file_path);
    if (res == -1)
    {
        return -errno;
    }
    close(res);
    return 0;
}

static int catfuse_release(const char *path, struct fuse_file_info *finfo)
{
    (void)path;
    (void)finfo;
    return 0;
}

static int catfuse_rename(const char *from, const char *to)
{
    (void)from;
    (void)to;
    return -EROFS;
}

static int catfuse_rmdir(const char *path)
{
    (void)path;
    return -EROFS;
}

static int catfuse_mknod(const char *path, mode_t mode, dev_t rdev)
{
    (void)path;
    (void)mode;
    (void)rdev;
    return -EROFS;
}

static int catfuse_mkdir(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    return -EROFS;
}

static int catfuse_unlink(const char *path)
{
    char *file_path = get_file_path(path);
    char *upath = translate_path(file_path);

    int res;
    res = unlink(upath);
    if (res == -1)
    {
        free(upath);
        free(file_path);
        return -errno;
    }
    free(upath);
    free(file_path);

    return 0;
}

static struct fuse_operations catfuse_oper = {
    .getattr = catfuse_getattr,
    .readdir = catfuse_readdir,
    .access = catfuse_access,
    .read = catfuse_read,
    .open = catfuse_open,
    .release = catfuse_release,
    .unlink = catfuse_unlink,
    .rmdir = catfuse_rmdir,
    .mkdir = catfuse_mkdir,
    .rename = catfuse_rename,
    .mknod = catfuse_mknod,
};

static int catfuse_parse_opt(void *data, const char *arg, int key,
                             struct fuse_args *outargs)
{
    (void)data;
    (void)outargs;

    switch (key)
    {
    case FUSE_OPT_KEY_NONOPT:
        if (root_path == 0)
        {
            root_path = strdup(arg);
            return 0;
        }
        else
        {
            return 1;
        }
    case FUSE_OPT_KEY_OPT:
        return 1;
    default:
        exit(1);
    }
    return 1;
}

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    int res;

    res = fuse_opt_parse(&args, &root_path, NULL, catfuse_parse_opt);
    if (res != 0)
    {
        fprintf(stderr, "Invalid arguments\n");
        exit(1);
    }
    if (root_path == 0)
    {
        fprintf(stderr, "Missing root path\n");
        exit(1);
    }

    magic = magic_open(MAGIC_MIME | MAGIC_CHECK);
    magic_load(magic, NULL);

    fuse_main(args.argc, args.argv, &catfuse_oper, NULL);

    return 0;
}