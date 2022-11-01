#include <solution.h>

#include <sys/types.h>
#include <unistd.h>

#include <ext2fs/ext2fs.h>

// list of defines
#define BASE_OFFSET 1024 // zero block group offset

// list of macros
#define RETURN_IF_FAIL(res)		\
	if (res < 0)				\
		return -errno;			\

#define RETURN_IF_FALSE(res)	\
	if (!(res))					\
		return -errno;			\

int readSuperBlock(int img, struct ext2_super_block* superBlock)
{
	if (!superBlock)
		return -1;

	// seek to the actual start of the zero block
	off_t offset = lseek(img, BASE_OFFSET, SEEK_SET);
	RETURN_IF_FALSE(offset == BASE_OFFSET);

	// read super block from the zero block
	ssize_t readSize = read(img, superBlock, sizeof(*superBlock));
	RETURN_IF_FALSE(readSize == sizeof(superBlock));

	return 0;
}

int readGroupDesc(int img, unsigned blockSize, unsigned blockGroupNumber, struct ext2_group_desc* groupDesc)
{
	/*
		blockSize = 1024: [boot] 1024, superBlock [1024], groupDesc [...]
		blockSize > 1024: [boot] 1024, superBlock [1024], free space [...], groupDesc [...] alligned to next block 
	*/
	unsigned groupDescBlock = (blockSize > 1024) ? 1 : 2;
	off_t offset = lseek(img, groupDescBlock * blockSize + blockGroupNumber * sizeof(struct ext2_group_desc), SEEK_SET);
	RETURN_IF_FALSE((unsigned) offset == groupDescBlock * blockSize + blockGroupNumber * sizeof(struct ext2_group_desc));	
	
	ssize_t readSize = read(img, groupDesc, sizeof(*groupDesc));
	RETURN_IF_FALSE(readSize == sizeof(groupDesc));

	return 0;
}

int readInode(int img, unsigned blockSize, unsigned index, struct ext2_group_desc* groupDesc, struct ext2_inode* inodeStruct, size_t inodeSize)
{
	off_t offset = lseek(img, groupDesc->bg_inode_table * blockSize + index * inodeSize, SEEK_SET);
	RETURN_IF_FALSE((unsigned) offset == groupDesc->bg_inode_table * blockSize + index * inodeSize);

	ssize_t readSize = read(img, inodeStruct, sizeof(*inodeStruct));
	RETURN_IF_FALSE(readSize == sizeof(*inodeStruct));

	return 0;
}

int readBlock(int img, unsigned blockNumber, unsigned blockSize, char* blockBuffer)
{
	// seek to this block
	off_t offset = lseek(img, blockNumber * blockSize, SEEK_SET);
	RETURN_IF_FALSE(offset == blockNumber *  blockSize)

	// read block into memory
	ssize_t readSize = read(img, blockBuffer, blockSize);
	RETURN_IF_FALSE(readSize == blockSize);

	return 0;
}

int readAndCopy(int img, unsigned blockNumber, unsigned blockSize, char* blockBuffer, int out, unsigned* currentSize)
{
	RETURN_IF_FAIL(readBlock(img, blockNumber, blockSize, blockBuffer));
	
	// write to out
	if (*currentSize > blockSize)
	{
		ssize_t writeSize = write(out, blockBuffer, blockSize);
		RETURN_IF_FALSE(writeSize == blockSize);
		*currentSize -= blockSize;
	}
	else
	{
		ssize_t writeSize = write(out, blockBuffer, *currentSize);
		RETURN_IF_FALSE(writeSize == *currentSize);
		*currentSize = 0;
	}

	return 0;
}

int readAndCopyIndirect(int img, unsigned blockNumber, unsigned blockSize, char* blockBuffer, int out, unsigned* currentSize)
{
	RETURN_IF_FAIL(readBlock(img, blockNumber, blockSize, blockBuffer));

	union IndirectBlock
	{
		char* rawBuffer;
		int32_t* idArray; 
	} indirectBlock;

	indirectBlock.rawBuffer = blockBuffer;
	char* blockBufferIndirect = (char*) malloc(blockSize);

	for (unsigned j = 0; j < blockSize / 4 && *currentSize > 0; ++j)
	{
		if (indirectBlock.idArray[j] == 0)
			break; // terminate
		
		RETURN_IF_FAIL(readAndCopy(img, indirectBlock.idArray[j], blockSize, blockBufferIndirect, out, currentSize));
	}

	free(blockBufferIndirect);
	return 0;
}

int readAndCopyDIndirect(int img, unsigned blockNumber, unsigned blockSize, char* blockBuffer, int out, unsigned* currentSize)
{
	RETURN_IF_FAIL(readBlock(img, blockNumber, blockSize, blockBuffer));

	union IndirectBlock
	{
		char* rawBuffer;
		int32_t* idArray; 
	} indirectBlock;

	indirectBlock.rawBuffer = blockBuffer;
	char* blockBufferIndirect = (char*) malloc(blockSize);

	for (unsigned j = 0; j < blockSize / 4 && *currentSize > 0; ++j)
	{
		if (indirectBlock.idArray[j] == 0)
			break; // terminate
		
		RETURN_IF_FAIL(readAndCopyIndirect(img, indirectBlock.idArray[j], blockSize, blockBufferIndirect, out, currentSize));
	}

	free(blockBufferIndirect);
	return 0;
}

int copyByInodeToFile(int img, unsigned blockSize, struct ext2_inode* inodeStruct, int out)
{
	char* blockBuffer = (char*) malloc(blockSize);
	unsigned currentSize = inodeStruct->i_size;
	
	for (unsigned i = 0; i < EXT2_N_BLOCKS && currentSize > 0; ++i)
	{
		if (inodeStruct->i_block[i] == 0)
			break; // terminate

		// direct blocks
		if (i < EXT2_NDIR_BLOCKS)
		{
			RETURN_IF_FAIL(readAndCopy(img, inodeStruct->i_block[i], blockSize, blockBuffer, out, &currentSize));
		}
		else if (i == EXT2_IND_BLOCK)
		{
			RETURN_IF_FAIL(readAndCopyIndirect(img, inodeStruct->i_block[i], blockSize, blockBuffer, out, &currentSize));
		}
		else if (i == EXT2_DIND_BLOCK)
		{
			RETURN_IF_FAIL(readAndCopyDIndirect(img, inodeStruct->i_block[i], blockSize, blockBuffer, out, &currentSize));
		}
		else
		{
			free(blockBuffer);

			return -1; // unsupported
		}
	}

	free(blockBuffer);

	return 0;
}

int dump_file(int img, int inode_nr, int out)
{
	struct ext2_super_block superBlock = {};
	RETURN_IF_FAIL(readSuperBlock(img, &superBlock));

	unsigned blockSize = 1024 << superBlock.s_log_block_size;

	// calculate block group number where inode is located
	unsigned blockGroupNumber = (inode_nr - 1) / superBlock.s_inodes_per_group;
	unsigned index = (inode_nr - 1) % superBlock.s_inodes_per_group;

	struct ext2_group_desc groupDesc;
	RETURN_IF_FAIL(readGroupDesc(img, blockSize, blockGroupNumber, &groupDesc));

	struct ext2_inode inodeStruct;
	RETURN_IF_FAIL(readInode(img, blockSize, index, &groupDesc, &inodeStruct, superBlock.s_inode_size));

	RETURN_IF_FAIL(copyByInodeToFile(img, blockSize, &inodeStruct, out));
	return 0;
}
