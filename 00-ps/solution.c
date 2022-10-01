#include <solution.h>
#include <error_code.h>
#include <process_info.h>

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

#define PROC_DIR_PATH "/proc"

#define REPORT_RETURN_IF_NULL(res, path)                \
if (!res)                                        		\
{                                                       \
    report_error(path, errno);							\
    return ERR;                                         \
}

// ============================================================================

/*
	Checks if the given string is the valid integer number, returns true
	in case of success, otherwise returns false. Puts number in (@param number) 
	if it is not NULL
*/
static result_t IsNumber(const char* string, int* number);

/*
	Handles file, checks for "PID" file, puts it in processInfo struct. Returns OK in case of success,
	otherwise appropriate error code.
*/
static result_t HandleFile(const struct dirent* dirent, process_info_t* processInfo);

/*
	Reads file into the given pointer, allocates needed space (not more than PATH_MAX). Returns OK in case of success,
	otherwise appropriate error code.
*/
static result_t ReadFile(int dirfd, const char* name, char** string);

/*
	Put argv info into process_info_t struct. Returns OK in case of success,
	otherwise appropriate error code.
*/
static result_t GetArgv(int dirfd, process_info_t* processInfo);

/*
	Put exe info into process_info_t struct. Returns OK in case of success,
	otherwise appropriate error code.
*/
static result_t GetExe(int dirFd, process_info_t* processInfo);

/*
	Put envp info into process_info_t struct. Returns OK in case of success,
	otherwise appropriate error code.
*/
static result_t GetEnvp(int dirfd, process_info_t* processInfo);

/*
	Reads array of strings from the given string. Returns array in case of success, otherwise returns NULL
*/
static char** ReadArray(char* string);
// ============================================================================

static result_t IsNumber(const char* string, int* number)
{
	if (!string)
		return WRONG_ARG; // null input string

	char* endptr = NULL;
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

static result_t ReadFile(int dirfd, const char* name, char** string)
{
	RETURN_IF_NULL(string);

	int fd = openat(dirfd, name, O_RDONLY);
	RETURN_IF_ERR(fd);

	/*
		According to man 2 execve: "On kernel 2.6.23 and later, most architectures support a size
       	limit derived from the soft RLIMIT_STACK resource limit (see
       	getrlimit(2)) that is in force at the time of the execve() call."
	
		Also, use so-called "soft" limit to save some memory
	*/ 
	
	struct rlimit lim;
	if (getrlimit(RLIMIT_STACK, &lim) == -1)
	{
		perror("Failed to get RLIMIT_STACK");
		close(fd);
		return ERR;
	}

	*string = (char*) malloc(lim.rlim_cur * sizeof(char));
	if (!(*string))
	{
		perror("Failed to allocate space for array of strings");
		close(fd);
		return OUT_OF_MEM;
	}

	ssize_t size = read(fd, *string, lim.rlim_cur * sizeof(char));
	if (size == -1)
	{
		report_error(name, errno);

		free(*string);
		close(fd);
		return OUT_OF_MEM;
	}

	(*string)[size] = '\0';
	(*string)[size + 1] = '\0';

	close(fd);
	return OK;
}

static char** ReadArray(char* string)
{
	int argc = 0;
	
	// Count argc, then allocate array of pointers
	char* p = string;
	long argMax = sysconf(_SC_ARG_MAX); // ARG_MAX is defined in <limits.h>, too, but it is safer to get it runtime from sysconf
	while (*p != '\0' && argc != argMax)
	{
		++argc;
		p = strchr(p, '\0') + 1;
	}

	if (argc > argMax)
	{
		fprintf(stderr, "argc is too big (greater than %ld)\n", argMax);
		return NULL;
	}

	// Fill argv "flag by flag"
	char** argv = (char**) malloc(sizeof(char*) * (argc + 1));
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

static result_t GetArgv(int dirfd, process_info_t* processInfo)
{
	char* string = NULL;
	const char* cmdPath = "cmdline";

	RETURN_IF_FAIL(ReadFile(dirfd, cmdPath, &string));
	processInfo->argv = ReadArray(string);

	REPORT_RETURN_IF_NULL(processInfo->argv, cmdPath);
	return OK;
}

static result_t GetEnvp(int dirfd, process_info_t* processInfo)
{
	char* string = NULL;
	const char* envPath = "environ";

	RETURN_IF_FAIL(ReadFile(dirfd, envPath, &string));
	processInfo->envp = ReadArray(string);

	REPORT_RETURN_IF_NULL(processInfo->envp, envPath);
	return OK;
}

static result_t GetExe(int dirFd, process_info_t* processInfo)
{
	char buff[PATH_MAX + 1];
	const char* exeDir = "exe";

	ssize_t len = readlinkat(dirFd, exeDir, buff, PATH_MAX);
	REPORT_RETURN_IF_NULL(len, "proc/**/exe");
	buff[len] = '\0';

	processInfo->exe = (char*) malloc((len + 1) * sizeof(char));
	RETURN_IF_NULL(processInfo->exe);

	memcpy(processInfo->exe, buff, (len + 1) * sizeof(char));
	return OK;
}

static result_t HandleFile(const struct dirent* dirent, process_info_t* processInfo)
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
		report_error(dirent->d_name, errno);
		close(procDirFd);
		return ERR;
	}

	if (!IS_OK(GetExe(currentProcDirFd, processInfo)))
	{
		fprintf(stderr, "Failed to get info on exe");
		close(currentProcDirFd);
		close(procDirFd);

		return ERR;
	}

	if (!IS_OK(GetArgv(currentProcDirFd, processInfo)))
	{
		fprintf(stderr, "Failed to get info on argv");
		close(currentProcDirFd);
		close(procDirFd);

		return ERR;
	}

	if (!IS_OK(GetEnvp(currentProcDirFd, processInfo)))
	{
		fprintf(stderr, "Failed to get info on envp");
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
	// Open "/proc/" dir and list all files
	DIR* procDir = opendir(PROC_DIR_PATH);
	if (!procDir)
	{
		report_error(PROC_DIR_PATH, errno);
		return ;
	}

	struct dirent* currentFileStruct = NULL;
	while ((currentFileStruct = readdir(procDir)) != NULL)
	{
		process_info_t processInfo = {};

		// Proccess are directories with only digits in their names
		if (!IS_TRUE(IsNumber(currentFileStruct->d_name, &processInfo.pid)))
			continue;

		if (!IS_OK(HandleFile(currentFileStruct, &processInfo)))
		{
			DestroyProcessInfo(&processInfo);
			report_error(currentFileStruct->d_name, errno);
			continue;
		}

		report_process(processInfo.pid, processInfo.exe, processInfo.argv, processInfo.envp);
		DestroyProcessInfo(&processInfo);
	}

	closedir(procDir);
}
