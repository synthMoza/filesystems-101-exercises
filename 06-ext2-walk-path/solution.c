#include <solution.h>

#include <sys/types.h>
#include <unistd.h>

#include <ext2_access.h>

struct visitor_data
{
	int out;
	unsigned currentSize;
};

int BlockVisitor(struct ext2_access* access, char* block, void* data)
{
	struct visitor_data* visitor_data = (struct visitor_data*) data;
	unsigned blockSize = GetBlockSize(access);

	// write to out
	if (visitor_data->currentSize > blockSize)
	{
		ssize_t writeSize = write(visitor_data->out, block, blockSize);
		RETURN_IF_FALSE(writeSize == blockSize);
		visitor_data->currentSize -= blockSize;
	}
	else
	{
		ssize_t writeSize = write(visitor_data->out, block, visitor_data->currentSize);
		RETURN_IF_FALSE(writeSize == visitor_data->currentSize);
		visitor_data->currentSize = 0;
	}

	return 0;
}

int dump_file(int img, const char *path, int out)
{
	int res = 0;
	int inode_nr = 0;
	struct ext2_access* access = Create(img);

	res = GetInodeNumberByPath(access, path, &inode_nr);
	if (res < 0)
		return res;

	struct ext2_inode inode = {};
	res = GetInodeStruct(access, inode_nr, &inode);
	if (res < 0)
		return res;

	struct visitor_data data = {
		.currentSize = inode.i_size,
		.out = out
	};

	res = IterateFileBlocksByInode(access, inode_nr, BlockVisitor, (void*) &data);
	if (res < 0)
		return res;

	Destroy(access);
	return 0;
}
