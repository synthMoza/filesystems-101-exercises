#include <solution.h>

#include <sys/types.h>
#include <unistd.h>

#include <ext2fs/ext2fs.h>

// list of defines
#define BASE_OFFSET 1024 // zero block group offset

// cant find this define in ext2fs.h
#ifndef EXT2_S_IFDIR
#define EXT2_S_IFDIR 0x4000
#endif

// list of macros
#define RETURN_IF_FAIL(res) \
	if (res < 0)            \
		return -errno;

#define RETURN_IF_FALSE(res) \
	if (!(res))              \
		return -errno;

int readSuperBlock(int img, struct ext2_super_block *superBlock)
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

int readGroupDesc(int img, unsigned blockSize, unsigned blockGroupNumber, struct ext2_group_desc *groupDesc)
{
	/*
		blockSize = 1024: [boot] 1024, superBlock [1024], groupDesc [...]
		blockSize > 1024: [boot] 1024, superBlock [1024], free space [...], groupDesc [...] alligned to next block
	*/
	unsigned groupDescBlock = (blockSize > 1024) ? 1 : 2;
	off_t offset = lseek(img, groupDescBlock * blockSize + blockGroupNumber * sizeof(struct ext2_group_desc), SEEK_SET);
	RETURN_IF_FALSE((unsigned)offset == groupDescBlock * blockSize + blockGroupNumber * sizeof(struct ext2_group_desc));

	ssize_t readSize = read(img, groupDesc, sizeof(*groupDesc));
	RETURN_IF_FALSE(readSize == sizeof(groupDesc));

	return 0;
}

int readInode(int img, unsigned blockSize, unsigned index, struct ext2_group_desc *groupDesc, struct ext2_inode *inodeStruct, size_t inodeSize)
{
	off_t offset = lseek(img, groupDesc->bg_inode_table * blockSize + index * inodeSize, SEEK_SET);
	RETURN_IF_FALSE((unsigned)offset == groupDesc->bg_inode_table * blockSize + index * inodeSize);

	ssize_t readSize = read(img, inodeStruct, sizeof(*inodeStruct));
	RETURN_IF_FALSE(readSize == sizeof(*inodeStruct));

	return 0;
}

int readBlock(int img, unsigned blockNumber, unsigned blockSize, char *blockBuffer)
{
	// seek to this block
	off_t offset = lseek(img, blockNumber * blockSize, SEEK_SET);
	RETURN_IF_FALSE(offset == blockNumber * blockSize)

	// read block into memory
	ssize_t readSize = read(img, blockBuffer, blockSize);
	RETURN_IF_FALSE(readSize == blockSize);

	return 0;
}

char getFileType(unsigned char fileType)
{
	switch (fileType)
	{
	case EXT2_FT_REG_FILE:
		return 'f';
	case EXT2_FT_DIR:
		return 'd';
	default:
		return -1; // unsupported
	}
}

int readDirBlock(unsigned blockSize, const char *blockBuffer)
{
	// get first dir entry
	struct ext2_dir_entry_2 *dirEntry = (struct ext2_dir_entry_2 *) blockBuffer;
	unsigned offset = 0;

	char fileName[EXT2_NAME_LEN + 1] = {};

	// traverse to all dir entries
	while (dirEntry->inode != 0)
	{
		memcpy(fileName, dirEntry->name, dirEntry->name_len);
		fileName[dirEntry->name_len] = '\0';

		if (strcmp(fileName, ".") != 0 && strcmp(fileName, "..") != 0)
			report_file(dirEntry->inode, getFileType(dirEntry->file_type), fileName);

		// move to the nexty directory
		blockBuffer += dirEntry->rec_len;
		offset += dirEntry->rec_len;

		if (offset >= blockSize)
			break;
		dirEntry = (struct ext2_dir_entry_2 *)blockBuffer;
	}

	return 0;
}

union IndirectBlock
{
	const char *rawBuffer;
	int32_t *idArray;
};

int readDirBlockIndirect(unsigned blockSize, const char *blockBuffer)
{
	union IndirectBlock indirectBlock;

	indirectBlock.rawBuffer = blockBuffer;
	char *blockBufferIndirect = (char *)malloc(blockSize);

	for (unsigned j = 0; j < blockSize / 4; ++j)
	{
		if (indirectBlock.idArray[j] == 0)
			break; // terminate

		RETURN_IF_FAIL(readDirBlock(blockSize, blockBufferIndirect));
	}

	free(blockBufferIndirect);
	return 0;
}

int readDirBlockDIndirect(unsigned blockSize, const char *blockBuffer)
{
	union IndirectBlock indirectBlock;

	indirectBlock.rawBuffer = blockBuffer;
	char *blockBufferIndirect = (char *)malloc(blockSize);

	for (unsigned j = 0; j < blockSize / 4; ++j)
	{
		if (indirectBlock.idArray[j] == 0)
			break; // terminate

		RETURN_IF_FAIL(readDirBlockIndirect(blockSize, blockBufferIndirect));
	}

	free(blockBufferIndirect);
	return 0;
}

int readDirBlockTIndirect(unsigned blockSize, const char *blockBuffer)
{
	union IndirectBlock indirectBlock;

	indirectBlock.rawBuffer = blockBuffer;
	char *blockBufferIndirect = (char *)malloc(blockSize);

	for (unsigned j = 0; j < blockSize / 4; ++j)
	{
		if (indirectBlock.idArray[j] == 0)
			break; // terminate

		RETURN_IF_FAIL(readDirBlockDIndirect(blockSize, blockBufferIndirect));
	}

	free(blockBufferIndirect);
	return 0;
}

int readDirectory(int img, unsigned blockSize, const struct ext2_inode *inodeStruct)
{
	char *blockBuffer = (char *)malloc(blockSize);

	for (int i = 0; i < EXT2_NDIR_BLOCKS; ++i)
	{
		if (inodeStruct->i_block[i] == 0)
			break; // terminate

		// read current block
		RETURN_IF_FAIL(readBlock(img, inodeStruct->i_block[i], blockSize, blockBuffer));

		// direct blocks
		if (i < EXT2_NDIR_BLOCKS)
		{
			RETURN_IF_FAIL(readDirBlock(blockSize, blockBuffer));
		}
		else if (i == EXT2_IND_BLOCK)
		{
			RETURN_IF_FAIL(readDirBlockIndirect(blockSize, blockBuffer));
		}
		else if (i == EXT2_DIND_BLOCK)
		{
			RETURN_IF_FAIL(readDirBlockDIndirect(blockSize, blockBuffer));
		}
		else if (i == EXT2_TIND_BLOCK)
		{
			RETURN_IF_FAIL(readDirBlockTIndirect(blockSize, blockBuffer));
		}
		else
		{
			free(blockBuffer);
			return -1; // unknown block number
		}
	}

	free(blockBuffer);
	return 0;
}

int dump_dir(int img, int inode_nr)
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

	if ((inodeStruct.i_mode & EXT2_S_IFDIR) == 0)
		return -1; // not a directory

	RETURN_IF_FAIL(readDirectory(img, blockSize, &inodeStruct));

	return 0;
}
