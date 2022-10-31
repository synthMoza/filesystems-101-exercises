#include <solution.h>

#include <sys/types.h>
#include <unistd.h>

#include <ext2fs/ext2fs.h>

// list of defines
#define BASE_OFFSET 1024 // zero block group offset
#define BLOCK_OFFSET(block, blockSize) (BASE_OFFSET + block * blockSize) // get offset of the certain block

// list of macros
#define RETURN_IF_FAIL(res)		\
	if (res < 0)				\
		return -errno;			\

#define RETURN_IF_FALSE(res)	\
	if (!(res))					\
		return -errno;			\

int dump_file(int img, int inode_nr, int out)
{
	// seek to the actual start of the zero block
	off_t offset = lseek(img, BASE_OFFSET, SEEK_SET);
	RETURN_IF_FALSE(offset == BASE_OFFSET);

	// read super block from the zero block
	struct ext2_super_block superBlock = {};
	ssize_t readSize = read(img, &superBlock, sizeof(superBlock));
	RETURN_IF_FALSE(readSize == sizeof(superBlock));

	// calculate size of block group
	unsigned blockSize = 1024 << superBlock.s_log_block_size;

	// calculate block group number where inode is located
	int blockGroupNumber = (inode_nr - 1) / superBlock.s_inodes_per_group;
	int index = (inode_nr - 1) % superBlock.s_inodes_per_group;

	// seek to this block and needed header inside it
	// unsigned int groupCount = 1 + (superBlock.s_blocks_count - 1) / superBlock.s_blocks_per_group;
	// unsigned int descrListSize = groupCount * sizeof(struct ext2_group_desc);
	offset = lseek(img, BASE_OFFSET + sizeof(superBlock) + blockGroupNumber * sizeof(struct ext2_group_desc), SEEK_SET);
	RETURN_IF_FALSE((unsigned) offset == BLOCK_OFFSET(blockGroupNumber, blockSize) + sizeof(superBlock) + blockGroupNumber * sizeof(struct ext2_group_desc));

	// read block groups header
	struct ext2_group_desc groupDesc;
	readSize = read(img, &groupDesc, sizeof(groupDesc));
	RETURN_IF_FALSE(readSize == sizeof(groupDesc));

	// read inode struct
	offset = lseek(img, BLOCK_OFFSET(groupDesc.bg_inode_table, blockSize) + index * sizeof(struct ext2_inode), SEEK_SET);
	RETURN_IF_FALSE((unsigned) offset == BLOCK_OFFSET(groupDesc.bg_inode_table, blockSize) + index * sizeof(struct ext2_inode));

	struct ext2_inode inodeStruct;
	readSize = read(img, &inodeStruct, sizeof(inodeStruct));
	RETURN_IF_FALSE(readSize == sizeof(inodeStruct));

	char* blockBuffer = (char*) malloc(blockSize);
	unsigned currentSize = inodeStruct.i_size;
	ssize_t writeSize = 0;
	
	for (unsigned i = 0; i < EXT2_N_BLOCKS && currentSize > 0; ++i)
	{
		// seek to this block
		offset = lseek(img, BLOCK_OFFSET(inodeStruct.i_block[i], blockSize), SEEK_SET);
		RETURN_IF_FALSE(offset == BLOCK_OFFSET(inodeStruct.i_block[i], blockSize));

		// read block into memory
		readSize = read(img, blockBuffer, blockSize);
		RETURN_IF_FALSE(readSize == blockSize);

		// direct blocks
		if (i < EXT2_NDIR_BLOCKS)
		{
			// write to out
			if (currentSize > blockSize)
			{
				writeSize = write(out, blockBuffer, blockSize);
				RETURN_IF_FALSE(writeSize == blockSize);
				currentSize -= blockSize;
			}
			else
			{
				writeSize = write(out, blockBuffer, currentSize);
				RETURN_IF_FALSE(writeSize == currentSize);
				currentSize = 0;
				break; // done
			}
		}
		else if (i == EXT2_IND_BLOCK)
		{
			// first indirect
			union IndirectBlock
			{
				char* rawBuffer;
				int32_t* idArray; 
			} indirectBlock;

			indirectBlock.rawBuffer = blockBuffer;
			char* blockBufferIndirect = (char*) malloc(blockSize);
			for (unsigned j = 0; j < blockSize / 4; ++j)
			{
				// seek to this block
				offset = lseek(img, BLOCK_OFFSET(indirectBlock.idArray[j], blockSize), SEEK_SET);
				RETURN_IF_FALSE(offset == BLOCK_OFFSET(indirectBlock.idArray[j], blockSize));

				// read block into memory
				readSize = read(img, blockBufferIndirect, blockSize);
				RETURN_IF_FALSE(readSize == blockSize);

				// write to out
				if (currentSize > blockSize)
				{
					writeSize = write(out, blockBufferIndirect, blockSize);
					RETURN_IF_FALSE(writeSize == blockSize);
					currentSize -= blockSize;
				}
				else
				{
					writeSize = write(out, blockBufferIndirect, currentSize);
					RETURN_IF_FALSE(writeSize == currentSize);
					currentSize = 0;
					break; // done
				}
			}

			free(blockBufferIndirect);
		}
		else
		{
			return -1; // unsupported
		}
	}

	free(blockBuffer);
	return 0;
}
