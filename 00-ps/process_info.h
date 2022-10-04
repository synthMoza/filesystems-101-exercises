#ifndef PROCESS_INFO_HEADER
#define PROCESS_INFO_HEADER


typedef struct process_info
{
	int pid;
	char* exe;
	
	char** argv;
	char* argvString;

	char** envp;
	char* envpString;
} process_info_t;

void DestroyProcessInfo(process_info_t* processInfo);

#endif // PROCESS_INFO_HEADER
