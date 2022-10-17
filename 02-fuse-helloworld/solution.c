#include <solution.h>

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>	

const char* filename = "hello";
char* content = NULL;

static void* helloworld_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	(void) conn;
	(void) cfg;

	cfg->kernel_cache = 1;

	content = (char*) malloc(64 * sizeof(char));
	if (!content)
		exit(-ENOMEM);

	return NULL;
}

static int helloworld_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	(void) fi;
	
	int res = 0;
	memset(stbuf, 0, sizeof(*stbuf));

	// two possible values - "/" or filename
	if (strcmp(path, "/") == 0)
	{
		stbuf->st_mode = S_IFDIR | 0775;
		stbuf->st_nlink = 2;
	}
	else if (strcmp(path + 1, filename) == 0)
	{
		stbuf->st_mode = S_IFREG | 0400;
		stbuf->st_nlink = 1;
		stbuf->st_size = 64; // it is OK to report the size of "hello" that does not match the content.
	}

	return res;
}

static int helloworld_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
				 struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
	(void) offset;
	(void) fi;
	(void) flags;

	if (strcmp(path, "/") != 0)
		return -ENOENT;
	
	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	filler(buf, filename, NULL, 0, 0);

	return 0;
}

static int helloworld_open(const char *path, struct fuse_file_info *fi)
{
	if (strcmp(path + 1, filename) != 0)
		return -ENOENT;

	int current_flag = fi->flags & O_ACCMODE;
	if (current_flag != O_RDONLY)
		return -EROFS;

	return 0;
}

static int helloworld_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	(void) fi;
	if(strcmp(path + 1, filename) != 0)
		return -ENOENT;

	struct fuse_context* ctx = fuse_get_context();
	sprintf(content, "hello, %d\n", ctx->pid);
	long len = (long) strlen(content);
	if (offset < len) {
		if (offset + ((long) size) > len)
			size = len - offset;
		memcpy(buf, content + offset, size);
	} 
	else
	{
		size = 0;
	}

	return size;
}

static void helloworld_destroy(void *private_data)
{
	(void) private_data;
	free(content);
}

static const struct fuse_operations hellofs_ops = {
	.init = helloworld_init,
	.getattr = helloworld_getattr,
	.readdir = helloworld_readdir,
	.open = helloworld_open,
	.read = helloworld_read,
	.destroy = helloworld_destroy
};

int helloworld(const char *mntp)
{
	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &hellofs_ops, NULL);
}
