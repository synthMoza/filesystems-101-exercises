#include <solution.h>

#include <ext2_access.h>

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>

// global variables
// ====================
static struct ext2_access* g_access = NULL;
// ====================

// helper functions and structs
// ====================

static int ext2_fuse_inode_to_stbuf(ino_t inodeNum, struct ext2_inode* inode, struct stat* stbuf)
{
	if (!inode || !stbuf)
		return -EINVAL;

	stbuf->st_ino = inodeNum;
	stbuf->st_mode = inode->i_mode;
	stbuf->st_nlink = inode->i_links_count;
	stbuf->st_uid = inode->i_uid;
	stbuf->st_gid = inode->i_gid;
	
	stbuf->st_size = inode->i_size;
	stbuf->st_blksize = GetBlockSize(g_access);
	stbuf->st_blocks = inode->i_blocks;
	stbuf->st_atime = inode->i_atime;
	stbuf->st_mtime = inode->i_mtime;
	stbuf->st_ctime = inode->i_ctime;

	return 0;
}

struct DirToListEntriesStruct
{
    void* buf;
    fuse_fill_dir_t filler;
};

static int VisitDirToListEntries(struct ext2_access* access, block_type_t blockType, char* block, size_t blockSize, void* data)
{
    (void) blockType;
    (void) blockSize; // ignore these parameters

    struct DirToListEntriesStruct *fillerData = (struct DirToListEntriesStruct*) data;

    // get first dir entry
    struct ext2_dir_entry_2 *dirEntry = (struct ext2_dir_entry_2 *)block;
    unsigned leftToRead = GetBlockSize(access);

    // traverse to all dir entries
    while (leftToRead && dirEntry->inode != 0)
    {
		char pathBuffer[PATH_MAX] = {0};
		memcpy(pathBuffer, dirEntry->name, dirEntry->name_len);

		struct stat stbuf;
		struct ext2_inode inode;
		if (GetInodeStruct(g_access, dirEntry->inode, &inode) < 0)
			return -1;
		
		ext2_fuse_inode_to_stbuf(dirEntry->inode, &inode, &stbuf);
		fillerData->filler(fillerData->buf, pathBuffer, &stbuf, 0, 0);

        // move to the nexty directory
        block += dirEntry->rec_len;
        leftToRead -= dirEntry->rec_len;

        dirEntry = (struct ext2_dir_entry_2*) block;
    }

    return 0;
}

struct ReadBlockVisitorStruct
{
	char* buf;
	off_t offset;
	size_t size;
	size_t read;
};

int ReadBlockVisitor(struct ext2_access* access, block_type_t blockType, char* block, size_t blockSize, void* data)
{
	(void) access; // unused
	(void) blockType; // ignore sparse files

	struct ReadBlockVisitorStruct* readData = (struct ReadBlockVisitorStruct*) data; 

	if (readData->offset > 0)
	{
		if ((size_t) readData->offset >= blockSize)
		{
			// do not read yet
			readData->offset -= blockSize;
		}
		else
		{
			// read less than a block
			size_t toRead = blockSize - readData->offset;
			if (readData->size < toRead)
				toRead = readData->size; // do not have to read this block till the end
			
			memcpy((void*) readData->buf + readData->read, (void*) block + readData->offset, toRead);
			
			readData->size -= toRead;
			readData->read += toRead;
			readData->offset = 0;
		}
	}
	else if (readData->offset == 0)
	{ 
		// no offset
		size_t toRead = (readData->size < blockSize) ? readData->size : blockSize;
		memcpy((void*) readData->buf + readData->read, (void*) block, toRead);
		readData->size -= toRead;
		readData->read += toRead;
	}
	else
	{
		return -EINVAL; // can't read from negative offset
	}

	return 0;
}

// ====================

// fuse operations
// ====================

static void* ext2_fuse_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	(void) conn;
	(void) cfg;

	cfg->kernel_cache = 1;
	cfg->uid = getuid();
	cfg->gid = getgid();
	cfg->umask = ~S_IRUSR;

	return NULL;
}

static int ext2_fuse_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	(void) fi;

	int inodeNum = 0;
	int res = 0;
	memset(stbuf, 0, sizeof(*stbuf));

	if (GetInodeNumberByPath(g_access, path, &inodeNum) < 0)
		return -ENOENT;

	struct ext2_inode inode;
	if ((res = GetInodeStruct(g_access, inodeNum, &inode)))
		return res;
	
	// fil stat buf
	ext2_fuse_inode_to_stbuf(inodeNum, &inode, stbuf);

	return 0;
}

static int ext2_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
	(void) offset;
	(void) fi;
	(void) flags;

	// get this directory inode number
	int inode = 0;
	if (GetInodeNumberByPath(g_access, path, &inode) < 0)
		return -ENOENT;
	
	struct DirToListEntriesStruct dirData = {
		.buf = buf,
		.filler = filler,
	};

	int res = 0;
	if ((res = IterateFileBlocksByInode(g_access, inode, VisitDirToListEntries, (void *) &dirData)))
		return res;

	return 0;
}

static int ext2_fuse_open(const char *path, struct fuse_file_info *fi)
{
	// can only open for reading
	int current_flags = fi->flags & O_ACCMODE;
	if (current_flags != O_RDONLY)
		return -EROFS;
	
	// check for existence
	int inode = 0;
	if (GetInodeNumberByPath(g_access, path, &inode) < 0)
		return -ENOENT;
	
	return 0;
}

static int ext2_fuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void) fi;

	int inode = 0;
	if (GetInodeNumberByPath(g_access, path, &inode) < 0)
		return -ENOENT;
	
	struct ReadBlockVisitorStruct readData = {
		.buf = buf,
		.offset = offset,
		.size = size,
		.read = 0,
	};

	int res = 0;
	if ((res = IterateFileBlocksByInode(g_access, inode, ReadBlockVisitor, (void *) &readData)))
		return res;

	return (size - readData.size);
}

static int ext2_fuse_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void) path;
	(void) buf;
	(void) size;
	(void) offset;
	(void) fi;

	return -EROFS; // no write operations
}

static int ext2_fuse_truncate(const char* path, off_t offset, struct fuse_file_info *fi)
{
	(void) path;
	(void) offset;
	(void) fi;

	return -EROFS; // no write operations
}

static int ext2_fuse_create(const char* path, mode_t mode, struct fuse_file_info *fi)
{
	(void) path;
	(void) mode;
	(void) fi;

	return -EROFS; // no write operations
}

static int ext2_fuse_rename(const char* path, const char* new_name, unsigned int flags)
{
	(void) path;
	(void) new_name;
	(void) flags;

	return -EROFS; // no write operations
}

static int ext2_fuse_setxattr(const char* path, const char* name, const char* value, size_t size, int flags)
{
	(void) path;
	(void) name;
	(void) flags;
	(void) value;
	(void) size;
	(void) flags;

	return -EROFS; // no write operations
}
static int ext2_fuse_removexattr(const char* path, const char* name)
{
	(void) path;
	(void) name;

	return -EROFS; // no write operations
}

static int ext2_fuse_access(const char* path, int mode)
{
	(void) path;

	if ((mode & W_OK) != 0)
		return -EROFS;

	return 0; // no write operations
}

static int ext2_fuse_unlink(const char* path)
{
	(void) path;
	return -EROFS; // no write operations
}

static void ext2_fuse_destroy(void *private_data)
{
	(void) private_data;
	Destroy(g_access);
}

// ====================


static const struct fuse_operations ext2_ops = 
{
	.init = ext2_fuse_init,
	.getattr = ext2_fuse_getattr,
	.readdir = ext2_fuse_readdir,
	.open = ext2_fuse_open,
	.read = ext2_fuse_read,
	.write = ext2_fuse_write,
	.truncate = ext2_fuse_truncate,
	.create = ext2_fuse_create,
	.rename = ext2_fuse_rename,
	.setxattr = ext2_fuse_setxattr,
	.removexattr = ext2_fuse_removexattr,
	.access = ext2_fuse_access,
	.unlink = ext2_fuse_unlink,
	.destroy = ext2_fuse_destroy
};

int ext2fuse(int img, const char *mntp)
{
	g_access = Create(img);
	if (!g_access)
		return -1;

	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &ext2_ops, NULL);
}
