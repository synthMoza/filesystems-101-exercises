#include <solution.h>
#include <stdio.h>

int main()
{
	struct btree *t = btree_alloc(2);
	
	for (int i = 0; i < 6; i++)
		btree_insert(t, i);
	
	btree_delete(t, 2);

	for (int i = 0; i < 9; ++i)
		printf("contains %i: %d\n", i, btree_contains(t, i));

	struct btree_iter *i = btree_iter_start(t);
	int x;
	for (;btree_iter_next(i, &x);)
		printf("%i\n", x);
	btree_iter_end(i);

	btree_free(t);
	return 0;
}
