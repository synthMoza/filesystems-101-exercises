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

/*
	ntfs_pathname_to_inode returns ENOENT even if it was directory, so rewrite (a bit) to return ENOTDIR if it was a directory
*/
ntfs_inode* my_ntfs_pathname_to_inode(ntfs_volume *vol, ntfs_inode *parent, const char *pathname)
{
	u64 inum;
	int len, err = 0;
	char *p, *q;
	ntfs_inode *ni;
	ntfs_inode *result = NULL;
	ntfschar *unicode = NULL;
	char *ascii = NULL;

	if (!vol || !pathname) {
		errno = EINVAL;
		return NULL;
	}
	
	ntfs_log_trace("path: '%s'\n", pathname);
	
	ascii = strdup(pathname);
	if (!ascii) {
		ntfs_log_error("Out of memory.\n");
		err = ENOMEM;
		goto out;
	}

	p = ascii;
	/* Remove leading /'s. */
	while (p && *p && *p == PATH_SEP)
		p++;

	if (parent) {
		ni = parent;
	} else {

		ni = ntfs_inode_open(vol, FILE_root);
		if (!ni) {
			ntfs_log_debug("Couldn't open the inode of the root "
					"directory.\n");
			err = EIO;
			result = (ntfs_inode*)NULL;
			goto out;
		}
	}

	while (p && *p) {
		/* Find the end of the first token. */
		q = strchr(p, PATH_SEP);
		if (q != NULL) {
			*q = '\0';
		}
		len = ntfs_mbstoucs(p, &unicode);
		if (len < 0) {
			ntfs_log_perror("Could not convert filename to Unicode:"
					" '%s'", p);
			err = errno;
			goto close;
		} else if (len > NTFS_MAX_NAME_LEN) {
			err = ENAMETOOLONG;
			goto close;
		}
		inum = ntfs_inode_lookup_by_name(ni, unicode, len);

		if (inum == (u64) -1) {
			ntfs_log_debug("Couldn't find name '%s' in pathname "
					"'%s'.\n", p, pathname);
			// if it was a directory, we will find another PATH_SEP
			q = strchr(p, PATH_SEP);
			if (q)
				err = ENOTDIR;
			else
				err = ENOENT;
			goto close;
		}

		if (ni != parent)
			if (ntfs_inode_close(ni)) {
				err = errno;
				goto out;
			}

		inum = MREF(inum);
		ni = ntfs_inode_open(vol, inum);
		if (!ni) {
			ntfs_log_debug("Cannot open inode %llu: %s.\n",
					(unsigned long long)inum, p);
			err = EIO;
			goto close;
		}
	
		free(unicode);
		unicode = NULL;

		if (q) *q++ = PATH_SEP; /* JPA */
		p = q;
		while (p && *p && *p == PATH_SEP)
			p++;
	}

	result = ni;
	ni = NULL;
close:
	if (ni && (ni != parent))
		if (ntfs_inode_close(ni) && !err)
			err = errno;
out:
	free(ascii);
	free(unicode);
	if (err)
		errno = err;
	return result;
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
	inode = my_ntfs_pathname_to_inode(vol, NULL, path);
	if (!inode)
	{
		r = -errno;
		goto main_out;
	}

	// copy inode data to out fd
	ATTR_TYPES attrType = AT_DATA;
	attr = ntfs_attr_open(inode, attrType, NULL, 0);
	if (!attr)
	{
		r = -1;
		goto main_out;
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
			goto main_out;
		}

		if (!bytesRead)
			break; // end

		written = write(out, buffer, bytesRead);
		if (written != bytesRead)
		{
			r = -errno;
			goto main_out;
		}
		
		offset += bytesRead;
	}

main_out:
	ntfs_attr_close(attr);
	ntfs_inode_close(inode);
	ntfs_umount(vol, FALSE);

	return r;
}
