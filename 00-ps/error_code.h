#ifndef ERROR_CODE_HEADER
#define ERROR_CODE_HEADER

/*
	All error codes with useful macros to treat errors
*/
typedef enum
{
    ERR = -1,
	OK = 0,
	TRUE,
	FALSE,
	WRONG_ARG,
	OUT_OF_RANGE,
    IO_ERR,
    OUT_OF_MEM,
	COUNT
} result_t;

#define IS_TRUE(res) (res == TRUE)
#define IS_OK(res) (res == OK)
#define IS_ERR(res) (res == ERR)

#define RETURN_IF_FAIL(res)                             \
if (!IS_OK(res))                                        \
{                                                       \
    perror("RETURN_IF_FAIL macro: ");                   \
    return res;                                         \
}

#define RETURN_IF_ERR(res)                              \
if (IS_ERR(res))                                        \
{                                                       \
    perror("RETURN_IF_ERR macro: ");                    \
    return res;                                         \
}


#define RETURN_IF_NULL(res)                             \
if (!res)                                               \
{                                                       \
    perror("RETURN_IF_NULL macro: ");                   \
    return ERR;                                         \
}

#endif // ERROR_CODE_HEADER
