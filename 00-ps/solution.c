#include <solution.h>
#include <error_code.h>
#include <process_info.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
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

#define PROC_DIR_PATH "/proc"

#define REPORT_RETURN_IF_NULL(res, path) \
	if (!res)                            \
	{                                    \
		report_error(path, errno);       \
		return ERR;                      \
	}

// ============================================================================

/*
	Checks if the given string is the valid integer number, returns true
	in case of success, otherwise returns false. Puts number in (@param number)
	if it is not NULL
*/
static result_t IsNumber(const char *string, int *number);

/*
	Handles file, checks for "PID" file, puts it in processInfo struct. Returns OK in case of success,
	otherwise appropriate error code.
*/
static result_t HandleFile(const struct dirent *dirent, process_info_t *processInfo);

/*
	Reads file into the given pointer, allocates needed space (not more than PATH_MAX). Returns OK in case of success,
	otherwise appropriate error code.
*/
static result_t ReadFile(int dirfd, const char *name, char **string, const char* pidName);

/*
	Put argv info into process_info_t struct. Returns OK in case of success,
	otherwise appropriate error code.
*/
static result_t GetArgv(int dirfd, process_info_t *processInfo, const char* name);

/*
	Put exe info into process_info_t struct. Returns OK in case of success,
	otherwise appropriate error code.
*/
static result_t GetExe(int dirFd, process_info_t *processInfo, const char* name);

/*
	Put envp info into process_info_t struct. Returns OK in case of success,
	otherwise appropriate error code.
*/
static result_t GetEnvp(int dirfd, process_info_t *processInfo, const char* name);

/*
	Reads array of strings from the given string. Returns array in case of success, otherwise returns NULL
*/
static char **ReadArray(char *string);
// ============================================================================

static result_t IsNumber(const char *string, int *number)
{
	if (!string)
		return WRONG_ARG; // null input string

	char *endptr = NULL;
	int input = 0;

	input = strtoimax(string, &endptr, 10);
	if (endptr == string || *endptr != '\0')
		return WRONG_ARG;
	if (errno == ERANGE && (input == INT_MAX || input == INT_MIN || input == 0))
		return OUT_OF_RANGE;

	if (number)
		*number = input;

	return true;
}

static result_t ReadFile(int dirfd, const char *name, char **string, const char* pidName)
{
	RETURN_IF_NULL(string);

	char filePath[PATH_MAX];
	sprintf(filePath, "%s/%s/%s", PROC_DIR_PATH, pidName, name);

	int fd = openat(dirfd, name, O_RDONLY);
	if (fd == -1)
	{
		report_error(filePath, errno);
		return IO_ERR;
	}

	struct rlimit lim;
	if (getrlimit(RLIMIT_STACK, &lim) == -1)
	{
		perror("Failed to get RLIMIT_STACK");
		close(fd);
		return ERR;
	}

	long argMax = sysconf(_SC_ARG_MAX); // ARG_MAX is defined in <limits.h>, too, but it is safer to get it runtime from sysconf
	*string = (char *)malloc(argMax * sizeof(char));
	if (!(*string))
	{
		perror("Failed to allocate space for array of strings");
		close(fd);
		return OUT_OF_MEM;
	}

	ssize_t size = read(fd, *string, argMax * sizeof(char));
	if (size == -1)
	{
		report_error(filePath, errno);

		free(*string);
		close(fd);
		return OUT_OF_MEM;
	}

	(*string)[size] = '\0';
	(*string)[size + 1] = '\0';

	close(fd);
	return OK;
}

static char **ReadArray(char *string)
{
	int argc = 0;

	// Count argc, then allocate array of pointers
	char *p = string;
	while (*p != '\0')
	{
		++argc;
		p = strchr(p, '\0') + 1;
	}

	// Fill argv "flag by flag"
	char **argv = (char **)malloc(sizeof(char *) * (argc + 1));
	if (!argv)
	{
		perror("Failed to allocate memory for argv\n");
		return NULL;
	}

	p = string;

	for (long i = 0; i < argc; ++i)
	{
		argv[i] = p;
		p = strchr(p, '\0') + 1;
	}

	argv[argc] = NULL;
	return argv;
}

static result_t GetArgv(int dirfd, process_info_t *processInfo, const char* name)
{
	char *string = NULL;
	const char *cmdPath = "cmdline";

	RETURN_IF_FAIL(ReadFile(dirfd, cmdPath, &string, name));
	processInfo->argv = ReadArray(string);

	REPORT_RETURN_IF_NULL(processInfo->argv, cmdPath);
	return OK;
}

static result_t GetEnvp(int dirfd, process_info_t *processInfo, const char* name)
{
	char *string = NULL;
	const char *envPath = "environ";

	RETURN_IF_FAIL(ReadFile(dirfd, envPath, &string, name));
	processInfo->envp = ReadArray(string);

	REPORT_RETURN_IF_NULL(processInfo->envp, envPath);
	return OK;
}

static result_t GetExe(int dirFd, process_info_t *processInfo, const char* name)
{
	char buff[PATH_MAX + 1];
	const char *exeLink = "exe";

	ssize_t len = readlinkat(dirFd, exeLink, buff, PATH_MAX);
	if (len == -1)
	{
		int saveErrno = errno;

		char filePath[PATH_MAX];
		sprintf(filePath, "%s/%s/%s", PROC_DIR_PATH, name, exeLink);

		report_error(filePath, saveErrno);
		return ERR;
	}
	buff[len] = '\0';

	processInfo->exe = (char *)malloc((len + 1) * sizeof(char));
	RETURN_IF_NULL(processInfo->exe);

	memcpy(processInfo->exe, buff, (len + 1) * sizeof(char));
	return OK;
}

static result_t HandleFile(const struct dirent *dirent, process_info_t *processInfo)
{
	// Open directory with process pid as it will be used later
	int procDirFd = open(PROC_DIR_PATH, O_RDONLY);
	if (procDirFd == -1)
	{
		report_error(PROC_DIR_PATH, errno);
		return ERR;
	}

	int currentProcDirFd = openat(procDirFd, dirent->d_name, O_RDONLY);
	if (currentProcDirFd == -1)
	{
		int saveErrno = errno;

		char filePath[PATH_MAX];
		sprintf(filePath, "%s/%s", PROC_DIR_PATH, dirent->d_name);
		report_error(filePath, saveErrno);

		close(procDirFd);
		return ERR;
	}

	if (!IS_OK(GetExe(currentProcDirFd, processInfo, dirent->d_name)))
	{
		close(currentProcDirFd);
		close(procDirFd);

		return ERR;
	}

	if (!IS_OK(GetArgv(currentProcDirFd, processInfo, dirent->d_name)))
	{
		close(currentProcDirFd);
		close(procDirFd);

		return ERR;
	}

	if (!IS_OK(GetEnvp(currentProcDirFd, processInfo, dirent->d_name)))
	{
		close(currentProcDirFd);
		close(procDirFd);

		return ERR;
	}

	close(procDirFd);
	close(currentProcDirFd);

	return OK;
}

void ps(void)
{
	// Open "/proc" dir and list all files
	DIR *procDir = opendir(PROC_DIR_PATH);
	if (!procDir)
	{
		report_error(PROC_DIR_PATH, errno);
		return;
	}

	struct dirent *currentFileStruct = NULL;
	errno = 0;
	while ((currentFileStruct = readdir(procDir)) != NULL)
	{
		process_info_t processInfo = {};

		// Proccess are directories with only digits in their names
		if (!IS_TRUE(IsNumber(currentFileStruct->d_name, &processInfo.pid)))
			continue;

		if (!IS_OK(HandleFile(currentFileStruct, &processInfo)))
		{
			DestroyProcessInfo(&processInfo);
			continue;
		}

		report_process(processInfo.pid, processInfo.exe, processInfo.argv, processInfo.envp);
		DestroyProcessInfo(&processInfo);
	}

	closedir(procDir);
}
