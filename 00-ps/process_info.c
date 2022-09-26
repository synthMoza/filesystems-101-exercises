#include <process_info.h>
#include <stdlib.h>

static void DestroyArray(char** argv)
{
    free(*argv); // we allocate the whole string and set pointers for argv
    free(argv);    
}

void DestroyProcessInfo(process_info_t* processInfo)
{
	if (!processInfo)
		return; // no need to destroy null

	if (processInfo->exe)
		free(processInfo->exe);
	
    if (processInfo->argv)
        DestroyArray(processInfo->argv);

    if (processInfo->envp)
        DestroyArray(processInfo->envp);
}
