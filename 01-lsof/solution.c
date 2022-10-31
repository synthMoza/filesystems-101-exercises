#include <solution.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>

static int IsNumber(const char* string, int* number)
{
	if (!string)
		return -1; // null input string

	char* endptr = NULL;
	int input = 0;

	input = strtoimax(string, &endptr, 10);
	if (endptr == string || *endptr != '\0')
		return -1;
	if (errno == ERANGE && (input == INT_MAX || input == INT_MIN || input == 0))
		return -1;
	
	if (number)
		*number = input;
	
	return 0;
}

void HandleFile(const char* fileName)
{
	// open dir with opened file descrs '/proc/<pid>/fd'
	char path[PATH_MAX];
	sprintf(path, "/proc/%s/fd", fileName);

	// open dir with fd's
	DIR* dir = opendir(path);
	if (!dir)
	{
		report_error(path, errno);
		return ;
	}

	// open dir with fd's as descr (to use readlinkat method)
	int dirFd = open(path, O_RDONLY);
	if (dirFd < 0)
	{
		report_error(path, errno);

		closedir(dir);
		return ;
	}

	// read each descriptor in the directory
	struct dirent* dirent = NULL;
	while ((dirent = readdir(dir)) != NULL)
	{
		if (IsNumber(dirent->d_name, NULL) < 0)
			continue; // not a process

		// read link to the actual path
		char buff[PATH_MAX] = {};
		ssize_t size = readlinkat(dirFd, dirent->d_name, buff, PATH_MAX);
		if (size < 0)
		{
			int code = errno;
			sprintf(path, "/proc/%s/fd/%s", fileName, dirent->d_name);
			report_error(path, code);

			closedir(dir);
			close(dirFd);
			return ;
		}

		// check if it is the file
		struct stat statbuf = {};
		if (stat(buff, &statbuf) < 0)
			continue;
		
		report_file(buff);
	}

	closedir(dir);
	close(dirFd);
}

void lsof(void)
{
	// Open "/proc/" dir and list all files
	DIR* procDir = opendir("/proc");
	if (!procDir)
	{
		report_error("/proc", errno);
		return ;
	}

	struct dirent* dirent = NULL;
	while ((dirent = readdir(procDir)) != NULL)
	{
		if (IsNumber(dirent->d_name, NULL) < 0)
			continue; // not a process
		
		HandleFile(dirent->d_name);
	}
	
	closedir(procDir);
}
