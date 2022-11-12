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
	ntfs_attr* attr = NULL;
	ntfs_volume *vol = NULL;
	ntfs_inode *inode = NULL;

	const unsigned bufferSize = 4096;
	char buffer[bufferSize];

	int r = 0;

	// mount image
	// maybe it can be done much more effectively

	// get device/file name
	char fileName[PATH_MAX];
	if (GetFileNameByFd(img, fileName) < 0)
		return -1; // cant read file name

	vol = ntfs_mount(fileName, NTFS_MNT_RDONLY);
	if (!vol)
		return -1;

	// find inode by path
	inode = ntfs_pathname_to_inode(vol, NULL, path);
	if (!inode)
	{
		r = -errno;
		goto out;
	}

	// copy inode data to out fd
	ATTR_TYPES attrType = AT_DATA;
	attr = ntfs_attr_open(inode, attrType, NULL, 0);
	if (!attr)
	{
		r = -1;
		goto out;
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
			r = -errno;
			goto out;
		}

		if (!bytesRead)
			break; // end

		written = write(out, buffer, bytesRead);
		if (written != bytesRead)
		{
			r = -errno;
			goto out;
		}
		
		offset += bytesRead;
	}

out:
	ntfs_attr_close(attr);
	ntfs_inode_close(inode);
	ntfs_umount(vol, FALSE);

	return r;
}
