#ifndef EXT2_ACCESS_HEADER
#define EXT2_ACCESS_HEADER

#include <sys/types.h>
#include <unistd.h>

#include <ext2fs/ext2fs.h>

// list of macros
#define RETURN_IF_FAIL(res)     \
	if (res < 0)                \
		return -errno;

#define RETURN_IF_FALSE(res)    \
	if (!(res))                 \
		return -errno;

/*
    Struct that allows to easily access ext2 filesystem
*/
struct ext2_access;

/*
    Creates ext2_access struct, read needed initialize data from img
    @param img Ext2 filesystem img
*/
struct ext2_access* Create(int img);

/*
    Destroys the ext2_access structure and fress all the memory used
*/
void Destroy(struct ext2_access* access);

/*
    Get given filesystem's block size
*/
unsigned GetBlockSize(struct ext2_access* access);

/*
    Get raw inode struct by given inode number
*/
int GetInodeStruct(struct ext2_access* access, int inode_nr, struct ext2_inode* inode);

/*
    Visitor function for iterating over blocks 
*/
typedef int (*block_visitor)(struct ext2_access* access, char* block, void* data);

/*
    Iterate inode's file blocks (direct and indirect ones) using visitor function
*/
int IterateFileBlocksByInode(struct ext2_access* access, int inode_nr, block_visitor visitor, void* data);

/*
    Find inode number by given path to the file (absolute path)
*/
int GetInodeNumberByPath(struct ext2_access* access, const char* path, int* inode_nr);

#endif // #define EXT2_ACCESS_HEADER
 