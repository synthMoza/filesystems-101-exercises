#include "ext2_access.h"

struct ext2_access
{
    int m_img; // filesystem img descriptor
    struct ext2_super_block* m_superBlock;
};

// Helper defines
// ----------------------------------------------------------
#define NAME_MAX 255
// ----------------------------------------------------------


// Static helper functions
// ----------------------------------------------------------

static int ReadSuperBlock(int img, struct ext2_super_block *superBlock)
{
	if (!superBlock)
		return -1;

	// seek to the actual start of the zero block
	off_t offset = lseek(img, SUPERBLOCK_OFFSET, SEEK_SET);
	RETURN_IF_FALSE(offset == SUPERBLOCK_OFFSET);

	// read super block from the zero block
	ssize_t readSize = read(img, superBlock, sizeof(*superBlock));
	RETURN_IF_FALSE(readSize == sizeof(superBlock));

	return 0;
}

static int GetGroupDesc(struct ext2_access* access, unsigned blockGroupNumber, struct ext2_group_desc* groupDesc)
{
    /*
		blockSize = 1024: [boot] 1024, superBlock [1024], groupDesc [...]
		blockSize > 1024: [boot] 1024, superBlock [1024], free space [...], groupDesc [...] alligned to next block
	*/

    unsigned blockSize = GetBlockSize(access);
	unsigned groupDescBlock = (blockSize > 1024) ? 1 : 2;
	off_t offset = lseek(access->m_img, groupDescBlock * blockSize + blockGroupNumber * sizeof(struct ext2_group_desc), SEEK_SET);
	RETURN_IF_FALSE((unsigned)offset == groupDescBlock * blockSize + blockGroupNumber * sizeof(struct ext2_group_desc));

	ssize_t readSize = read(access->m_img, groupDesc, sizeof(*groupDesc));
	RETURN_IF_FALSE(readSize == sizeof(groupDesc));

	return 0;
}

static int ReadBlock(struct ext2_access* access, unsigned blockNumber, char* blockBuffer)
{
    // seek to this block
    unsigned blockSize = GetBlockSize(access);

	off_t offset = lseek(access->m_img, blockNumber * blockSize, SEEK_SET);
	RETURN_IF_FALSE(offset == blockNumber * blockSize)

	// read block into memory
	ssize_t readSize = read(access->m_img, blockBuffer, blockSize);
	RETURN_IF_FALSE(readSize == blockSize);

	return 0;
}

union IndirectBlock
{
	const char *rawBuffer;
	int32_t *idArray;
};

static int IterateIndirectBlock(struct ext2_access* access, char* blockBuffer, block_visitor visitor, void* data)
{
    union IndirectBlock indirectBlock;

	indirectBlock.rawBuffer = blockBuffer;
    unsigned blockSize = GetBlockSize(access);

	char *blockBufferIndirect = (char *) malloc(blockSize);
	for (unsigned j = 0; j < blockSize / 4; ++j)
	{
		if (indirectBlock.idArray[j] == 0)
			break; // terminate

		RETURN_IF_FAIL(ReadBlock(access, indirectBlock.idArray[j], blockBufferIndirect));
		RETURN_IF_FAIL(visitor(access, blockBufferIndirect, data));
	}

	free(blockBufferIndirect);
	return 0;
}

static int IterateDIndirectBlock(struct ext2_access* access, char* blockBuffer, block_visitor visitor, void* data)
{
    union IndirectBlock indirectBlock;

	indirectBlock.rawBuffer = blockBuffer;
    unsigned blockSize = GetBlockSize(access);

	char *blockBufferIndirect = (char *) malloc(blockSize);
	for (unsigned j = 0; j < blockSize / 4; ++j)
	{
		if (indirectBlock.idArray[j] == 0)
			break; // terminate

		RETURN_IF_FAIL(ReadBlock(access, indirectBlock.idArray[j], blockBufferIndirect));
		RETURN_IF_FAIL(IterateIndirectBlock(access, blockBufferIndirect, visitor, data));
	}

	free(blockBufferIndirect);
	return 0;
}

static int IterateTIndirectBlock(struct ext2_access* access, char* blockBuffer, block_visitor visitor, void* data)
{
    union IndirectBlock indirectBlock;

	indirectBlock.rawBuffer = blockBuffer;
    unsigned blockSize = GetBlockSize(access);

	char *blockBufferIndirect = (char *) malloc(blockSize);
	for (unsigned j = 0; j < blockSize / 4; ++j)
	{
		if (indirectBlock.idArray[j] == 0)
			break; // terminate

		RETURN_IF_FAIL(ReadBlock(access, indirectBlock.idArray[j], blockBufferIndirect));
		RETURN_IF_FAIL(IterateDIndirectBlock(access, blockBufferIndirect, visitor, data));
	}

	free(blockBufferIndirect);
	return 0;
}

struct DirToFindEntryByNameStruct
{
    const char* name;
    unsigned length;
    int out;
};

static int VisitDirToFindEntryByName(struct ext2_access* access, char* block, void* data)
{
    struct DirToFindEntryByNameStruct* fileData = (struct DirToFindEntryByNameStruct*) data;

    // get first dir entry
	struct ext2_dir_entry_2 *dirEntry = (struct ext2_dir_entry_2 *) block;
	unsigned leftToRead = GetBlockSize(access);

	// traverse to all dir entries
	while (leftToRead && dirEntry->inode != 0)
	{
        if (strncmp(dirEntry->name, fileData->name, fileData->length) == 0 &&
            dirEntry->name_len == fileData->length)
        {
            // found!
            fileData->out = dirEntry->inode;
            break;
        }

		// move to the nexty directory
		block += dirEntry->rec_len;
		leftToRead -= dirEntry->rec_len;

		dirEntry = (struct ext2_dir_entry_2 *) block;
	}

	return 0;
}

static int GetInodeByNameInDir(struct ext2_access* access, int srcInode, const char* fileName, unsigned length)
{
    struct DirToFindEntryByNameStruct dirData = {
        .name = fileName,
        .length = length,
        .out = 0,
    };
    
    RETURN_IF_FAIL(IterateFileBlocksByInode(access, srcInode, VisitDirToFindEntryByName, (void*) &dirData));
    return dirData.out;
}

// ----------------------------------------------------------

struct ext2_access* Create(int img)
{
    struct ext2_access* access = (struct ext2_access*) malloc(sizeof(*access));
    if (!access)
        return NULL;

    access->m_img = img;
    access->m_superBlock = (struct ext2_super_block*) malloc(sizeof(*access->m_superBlock));
    if (!access->m_superBlock)
    {
        free(access);
        return NULL;
    }

    if (ReadSuperBlock(img, access->m_superBlock) < 0)
        return NULL;

    if (access->m_superBlock->s_magic != EXT2_SUPER_MAGIC)
    {
        Destroy(access);
        return NULL; // not ext2
    }

    return access;
}

unsigned GetBlockSize(struct ext2_access* access)
{
    return 1024 << (access->m_superBlock->s_log_block_size);
}

int GetInodeNumberByPath(struct ext2_access* access, const char* path, int* inode_nr)
{
    int srcInode = EXT2_ROOT_INO;
    const char* pathPointer = path + 1;
    
    while (*pathPointer != '\0')
    {
        const char* newPointer = strchr(pathPointer, '/');
        if (newPointer)
        {
            // directory
            char fileName[NAME_MAX] = {};
            strncpy(fileName, pathPointer, newPointer - pathPointer);
            
            fileName[newPointer - pathPointer] = '\0';
            srcInode = GetInodeByNameInDir(access, srcInode, fileName, newPointer - pathPointer);
            if (srcInode == 0)
            {
                // couldn't find entry
                return -1;
            }
        }
        else
        {
            // target file/dir
            char fileName[NAME_MAX] = {};
            unsigned length = strlen(pathPointer);
            strncpy(fileName, pathPointer, length);
            
            fileName[length] = '\0';
            srcInode = GetInodeByNameInDir(access, srcInode, fileName, length);
            if (srcInode == 0)
            {
                // couldn't find entry
                return -1;
            }
            else
            {
                // found target file
                *inode_nr = srcInode;
                break;
            }
        }

        pathPointer = newPointer + 1;
    } 

    return 0;
}

int GetInodeStruct(struct ext2_access* access, int inode_nr, struct ext2_inode* inode)
{
    // calculate block group number where inode is located
	unsigned blockGroupNumber = (inode_nr - 1) / access->m_superBlock->s_inodes_per_group;
	unsigned index = (inode_nr - 1) % access->m_superBlock->s_inodes_per_group;

    // get corresponding group desc
    struct ext2_group_desc groupDesc = {};
	RETURN_IF_FAIL(GetGroupDesc(access, blockGroupNumber, &groupDesc));

    // read inode struct
    unsigned blockSize = GetBlockSize(access);
    unsigned inodeSize = access->m_superBlock->s_inode_size;

    off_t offset = lseek(access->m_img, groupDesc.bg_inode_table * blockSize + index * inodeSize, SEEK_SET);
	RETURN_IF_FALSE((unsigned) offset == groupDesc.bg_inode_table * blockSize + index * inodeSize);

	ssize_t readSize = read(access->m_img, inode, sizeof(*inode));
	RETURN_IF_FALSE(readSize == sizeof(*inode));

	return 0;
}

int IterateFileBlocksByInode(struct ext2_access* access, int inode_nr, block_visitor visitor, void* data)
{
    if (!access)
        return -1;

    struct ext2_inode inode;
    RETURN_IF_FAIL(GetInodeStruct(access, inode_nr, &inode));

    char *blockBuffer = (char *)malloc(GetBlockSize(access));
	for (int i = 0; i < EXT2_N_BLOCKS; ++i)
	{
		if (inode.i_block[i] == 0)
			break; // terminate

		// read current block
		RETURN_IF_FAIL(ReadBlock(access, inode.i_block[i], blockBuffer));

		// direct blocks
		if (i < EXT2_NDIR_BLOCKS)
		{
			RETURN_IF_FAIL(visitor(access, blockBuffer, data));
		}
		else if (i == EXT2_IND_BLOCK)
		{
			RETURN_IF_FAIL(IterateIndirectBlock(access, blockBuffer, visitor, data));
		}
		else if (i == EXT2_DIND_BLOCK)
		{
			RETURN_IF_FAIL(IterateDIndirectBlock(access, blockBuffer, visitor, data));
		}
		else if (i == EXT2_TIND_BLOCK)
		{
			RETURN_IF_FAIL(IterateTIndirectBlock(access, blockBuffer, visitor, data));
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

void Destroy(struct ext2_access* access)
{
    if (!access)
        return;

    if (access->m_superBlock)
        free(access->m_superBlock);
    
    free(access);
}
