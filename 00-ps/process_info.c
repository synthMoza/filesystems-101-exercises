#include <process_info.h>
#include <stdlib.h>

void DestroyProcessInfo(process_info_t* processInfo)
{
	if (!processInfo)
		return; // no need to destroy null

	if (processInfo->exe)
		free(processInfo->exe);
	
    if (processInfo->argv)
        free(processInfo->argv);

    if (processInfo->argvString)
        free(processInfo->argvString);

    if (processInfo->envp)
        free(processInfo->envp);

    if (processInfo->envpString)
        free(processInfo->envpString);
}
