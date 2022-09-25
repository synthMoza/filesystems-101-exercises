#ifndef PROCESS_INFO_HEADER
#define PROCESS_INFO_HEADER

typedef struct process_info
{
	int pid;
	char* exe;
	char** argv;
	char** envp;
} process_info_t;

#endif // PROCESS_INFO_HEADER
