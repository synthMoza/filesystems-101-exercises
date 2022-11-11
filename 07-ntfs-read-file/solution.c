#include <solution.h>

#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#define __timespec_defined // dont know why, but if there is no such define, it can not be copmpiled

#include <ntfs-3g/types.h>
#include <ntfs-3g/attrib.h>
#include <ntfs-3g/volume.h>
#include <ntfs-3g/dir.h>

int GetFileNameByFd(int fd, char* buffer)
{
	if (!buffer)
		return -1;
	
	char pathBuffer[PATH_MAX] = {0};
	sprintf(pathBuffer, "/proc/self/fd/%d", fd);

	ssize_t size = readlink(pathBuffer, buffer, PATH_MAX);
	if (size < 0)
		return size;
	
	buffer[size] = '\0';
	return 0;
}

int dump_file(int img, const char *path, int out)
{
	// mount image
	// maybe it can be done much more effectively

	// get device/file name
	char fileName[PATH_MAX];
	if (GetFileNameByFd(img, fileName) < 0)
		return -1; // cant read file name

	ntfs_volume *vol = NULL;

	// char pathBuffer[PATH_MAX] = {0};
	// sprintf(pathBuffer, "/proc/%d/fd/%d", getpid(), img);

	vol = ntfs_mount(fileName, NTFS_MNT_RDONLY);
	if (!vol)
		return -1;

	// find inode by path
	ntfs_inode *inode = NULL;
	inode = ntfs_pathname_to_inode(vol, NULL, path);
	if (!inode)
	{
		ntfs_umount(vol, FALSE);
		return -errno;
	}

	// copy inode data to out fd
	const unsigned bufferSize = 4096;
	char buffer[bufferSize];

	// allocate buffer here, free outselves
	ntfschar* attrName = (ntfschar*) malloc(PATH_MAX * sizeof(char));
	ATTR_TYPES attrType = AT_DATA;
	int attrLen = ntfs_mbstoucs("DATA", &attrName);
	if (attrLen < 0)
	{
		free(attrName);
		ntfs_inode_close(inode);
		ntfs_umount(vol, FALSE);
		return -1; // cant get attribute name
	}

	ntfs_attr* attr = ntfs_attr_open(inode, attrType, attrName, attrLen);
	if (!attr)
	{
		// free(attrName);
		// ntfs_inode_close(inode);
		// ntfs_umount(vol, FALSE);
		return -1; // cant read data atribute
	}

	u32 blockSize = 0;
	if (inode->mft_no < 2)
		blockSize = vol->mft_record_size;

	s64 offset = 0;
	s64 bytesRead = 0, written = 0;
	while (1) 
	{
		if (blockSize > 0) 
		{
			bytesRead = ntfs_attr_mst_pread(attr, offset, 1, blockSize, buffer);
			if (bytesRead > 0)
				bytesRead *= blockSize;
		} 
		else 
		{
			bytesRead = ntfs_attr_pread(attr, offset, bufferSize, buffer);
		}

		if (bytesRead < 0) 
		{
			free(attrName);
			ntfs_inode_close(inode);
			ntfs_umount(vol, FALSE);
			ntfs_attr_close(attr);
			return -errno;
		}

		if (!bytesRead)
			break; // end

		written = write(out, buffer, bytesRead);
		if (written != bytesRead)
		{
			free(attrName);
			ntfs_inode_close(inode);
			ntfs_umount(vol, FALSE);
			ntfs_attr_close(attr);
			return -errno;
		}
		
		offset += bytesRead;
	}

	free(attrName);
	ntfs_attr_close(attr);
	ntfs_inode_close(inode);
	ntfs_umount(vol, FALSE);

	return 0;
}
