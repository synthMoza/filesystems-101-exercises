#include <solution.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

typedef enum op
{
    OP_INSERT,
    OP_DELETE,
    OP_CONTAINS,
    OP_COUNT
} op_t;

op_t GetRandomOp()
{
    int rand = random() % OP_COUNT;
    return rand;
}

int GetOpArg(char* arr, size_t size, op_t op)
{
    (void) arr;

    switch (op)
    {
        case OP_INSERT:
        case OP_CONTAINS:
        case OP_DELETE:
            // generate just random int
            return random() % size;
        default:
            return -1;
    }
}

void PrintArray(const char* bytes, size_t size)
{
    printf("// ");
    for (size_t i = 0; i < size; ++i)
        if (bytes[i] != 0)
            printf("%lu ", i);
    
    printf("\n");
}

void PrintTree(struct btree* t)
{   
    struct btree_iter* it = btree_iter_start(t);
    int x = 0;
    printf("// ");
    for (;btree_iter_next(it, &x);)
        printf("%d ", x);
    btree_iter_end(it);
    printf("\n");
}

void RandomTest()
{
    srand(time(NULL));

    const unsigned L = 6;
    const unsigned maxElement = 1000;
    const unsigned iterations = 1000000;
    
    char bytes[maxElement];
    memset(bytes, 0, maxElement * sizeof(char));

    struct btree* t = btree_alloc(L);

    printf("const unsigned L = 6;\n");
    printf("struct btree* t = btree_alloc(L);\n");

    for (size_t i = 0; i < iterations; ++i)
    {
        // generate new operation
        op_t op = GetRandomOp();
        int value = GetOpArg(bytes, maxElement, op);

        // modify tree with this operation
        bool flag = false;
        switch (op)
        {
            case OP_INSERT:
            {
                printf("btree_insert(t, %d);\n", value);
                
                btree_insert(t, value);
                bytes[value] = 1;

                PrintArray(bytes, maxElement);
                PrintTree(t);
                break;
            }
            case OP_CONTAINS:
            {
                //printf("flag = btree_contains(t, %d)\n", value);
                flag = btree_contains(t, value);
                
                PrintArray(bytes, maxElement);
                PrintTree(t);

                if (flag != bytes[value])
                {
                    printf("// btree_contains returned false!\n");
                    printf("// value = %d\n", value);
                    printf("// expected: %d, got: %d\n", bytes[value], flag);
                    
                    PrintArray(bytes, maxElement);
                    PrintTree(t);
                    
                    return ;
                }

                break;
            }
            case OP_DELETE:
            {
                printf("btree_delete(t, %d);\n", value);
                
                btree_delete(t, value);
                bytes[value] = 0;


                PrintArray(bytes, maxElement);
                PrintTree(t);

                break;
            }
            default:
                return ;
        }
    }
}

int main()
{
    RandomTest();
    // FailedTest();

    return 0;
}
