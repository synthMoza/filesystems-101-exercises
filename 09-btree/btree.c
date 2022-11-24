#include <solution.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

struct btree_node
{
    unsigned t;
    int* keys;
    struct btree_node** children;
    unsigned count; // count keys, children amount == count + 1
    bool leaf;
};

struct btree
{
    unsigned t;
    struct btree_node* root;
};

static void btree_node_delete(struct btree_node* n, int x);

// ========================================================================

static struct btree_node* btree_node_alloc(unsigned L)
{
    struct btree_node* node = (struct btree_node*) malloc(sizeof(*node));
    if (!node)
        return NULL;
    
    node->t = L;
    node->keys = (int*) malloc(2 * L * sizeof(int));
    if (!node->keys)
    {
        free(node);
        return NULL;
    }

    node->children = (struct btree_node**) malloc((2 * L + 1) * sizeof(struct btree*));
    if (!node->children)
    {
        free(node->keys);
        free(node);
        return NULL;
    }

    for (unsigned i = 0; i < 2 * L + 1; ++i)
        node->children[i] = NULL;

    node->count = 0;
    node->leaf = true;

    return node;
}

static void btree_node_free(struct btree_node *t)
{
    if (t)
    {
        if (t->keys)
            free(t->keys);
        
        if (t->children)
        {
            for (unsigned i = 0; i < t->count + 1; ++i)
                if (t->children[i])
                    btree_node_free(t->children[i]);
            
            free(t->children);
        }

        free(t);
    }
}

// x - parent
// y - overflow node ( 2 * t keys)
// i - median key
static void btree_split_child(struct btree_node* x, int i, struct btree_node* y)
{
    if (!x || !y)
    {
        errno = EINVAL;
        return ;
    }

    struct btree_node* z = btree_node_alloc(x->t);
    if (!z)
        return ;
    
    z->leaf = y->leaf;
    z->count = x->t - 1;

    // copy keys to new node
    for (unsigned j = 0; j < x->t - 1; ++j)
        z->keys[j] = y->keys[j + x->t];
    if (!y->leaf) // copy children if it is not a leaf
        for (unsigned j = 0; j < z->t; ++j)
            z->children[j] = y->children[j + z->t];
    
    y->count = x->t - 1;
    for (int j = x->count; j >= i + 1; --j)
        x->children[j + 1] = x->children[j];
    
    x->children[i + 1] = z;

    for (int j = x->count - 1; j >= i; --j)
        x->keys[j + 1] = x->keys[j];

    x->keys[i] = y->keys[x->t - 1];
    x->count++;
}

static void btree_insert_nonfull(struct btree_node* x, int k)
{
    int i = x->count - 1;
    if (x->leaf)
    {
        while (i >= 0 && k < x->keys[i])
            x->keys[i + 1] = x->keys[i], --i;
        
        x->keys[i + 1] = k;
        x->count++;
    }
    else
    {
        while (i >= 0 && k < x->keys[i])
            --i;
        
        i++;
        if (x->children[i]->count == 2 * x->t - 1)
        {
            btree_split_child(x, i, x->children[i]);
            if (k > x->keys[i])
                i++;
        }

        btree_insert_nonfull(x->children[i], k);
    }
}

// ========================================================================

struct btree* btree_alloc(unsigned int L)
{
    struct btree* tree = (struct btree*) malloc(sizeof(*tree));
    if (!tree)
        return NULL;

    tree->t = L;
    tree->root = btree_node_alloc(L);
    if (!tree->root)
    {
        free(tree);
        return NULL;
    }

    return tree;
}

void btree_free(struct btree *t)
{
    if (t)
    {
        if (t->root)
            btree_node_free(t->root);
        free(t);
    }
}

void btree_insert(struct btree *t, int x)
{
    if (!t)
    {
        errno = EINVAL;
        return ;
    }

    if (!t->root)
    {
        t->root = btree_node_alloc(t->t);
        if (!t->root)
            return ; // no mem
        t->root->keys[0] = x;
        t->root->count = 1;
        t->root->leaf = true;
        return ;
    }

    if (btree_contains(t, x))
        return ; // already is presented in tree

    struct btree_node* r = t->root;
    if (r->count == 2 * t->t - 1)
    {
        struct btree_node* s = btree_node_alloc(t->t);
        if (!s)
            return ;
        
        t->root = s;
        s->leaf = false;
        s->count = 0;
        s->children[0] = r;
        btree_split_child(s, 0, r);
        btree_insert_nonfull(s, x);
    }
    else
    {
        btree_insert_nonfull(r, x);
    }
}

static void btree_merge(struct btree_node* n, unsigned i)
{
    struct btree_node* first = n->children[i];
    struct btree_node* second = n->children[i + 1];

    first->keys[n->t - 1] = n->keys[i];

    for (unsigned j = 0; j < second->count; ++j)
        first->keys[j + n->t] = second->keys[j];
    
    if (!first->leaf)
    {
        for (unsigned j = 0; j <= second->count; ++j)
            first->children[j + n->t] = second->children[j];
    }

    for (unsigned j = i + 1; j < n->count; ++j)
    {
        n->keys[j - 1] = n->keys[j];
        n->children[j] = n->children[j + 1];
    }

    first->count += second->count + 1;
    n->count--;

    free(second->keys);
    free(second->children);
    free(second);
}

static void btree_remove_from_leaf(struct btree_node* n, unsigned i)
{
    for (unsigned j = i + 1; j < n->count; ++j)
        n->keys[j - 1] = n->keys[j];
    
    n->count--;
}

static void btree_remove_from_non_leaf(struct btree_node* n, unsigned i)
{
    int k = n->keys[i];

    if (n->children[i]->count >= n->t)
    {
        // get predecessor value (the fatherst right leaf in the sub tree)
        struct btree_node* current = n->children[i];
        while (!current->leaf)
            current = current->children[current->count];

        int pred = current->keys[current->count - 1];
        n->keys[i] = pred;
        btree_node_delete(n->children[i], pred);
    }
    else if(n->children[i + 1]->count >= n->t)
    {
        // get successor
        struct btree_node* current = n->children[i + 1];
        while (!current->leaf)
            current = current->children[0];
        
        int succ = current->keys[0];
        n->keys[i] = succ;
        btree_node_delete(n->children[i + 1], succ);
    }
    else
    {
        // merge and delete from the new node
        btree_merge(n, i);
        btree_node_delete(n->children[i], k);
    }
}

static void btree_fill(struct btree_node* n, unsigned i)
{
    if (i != 0 && n->children[i - 1]->count >= n->t)
    {
        // borrow keys from previous child
        struct btree_node* first = n->children[i];
        struct btree_node* second = n->children[i - 1];

        for (int j = first->count - 1; j >= 0; --j)
            first->keys[j + 1] = first->keys[j];
        
        if (!first->leaf)
        {
            for (int j = first->count; j >= 0; --j)
                first->children[j + 1] = first->children[j];
        }

        first->keys[0] = n->keys[i - 1];

        if (!first->leaf)
        {
            first->children[0] = second->children[second->count];
        }

        n->keys[i - 1] = second->keys[second->count - 1];

        first->count++;
        second->count--;
    }
    else if (i != n->count && n->children[i + 1]->count >= n->t)
    {
        // borrow keys from next child
        struct btree_node* first = n->children[i];
        struct btree_node* second = n->children[i + 1];

        first->keys[first->count] = n->keys[i];

        if (!first->leaf)
            first->children[first->count + 1] = second->children[0];
        
        n->keys[i] = second->keys[0];

        for (unsigned j = 1; j < second->count; ++j)
            second->keys[j - 1] = second->keys[j];
        
        if (!second->leaf)
        {
            for (unsigned j = 1; j <= second->count; ++j)
                second->children[j - 1] = second->children[j];
        }

        first->count++;
        second->count--;
    }
    else
    {
        if (i != n->count)
            btree_merge(n, i);
        else
            btree_merge(n, i - 1);
    }
}

static void btree_node_delete(struct btree_node* n, int x)
{
    // find key in this node
    unsigned i = 0;
    while (i < n->count && n->keys[i] < x)
        ++i;
    
    if (i < n->count && n->keys[i] == x)
    {
        // remove from this node
        if (n->leaf)
            btree_remove_from_leaf(n, i);
        else
            btree_remove_from_non_leaf(n, i);
    }
    else
    {
        // key is not in this node
        if (n->leaf)
        {
            // no key in this tree (got leaf, didnt find)
            return ;
        }
 
        unsigned savedCount = n->count;
        if (n->children[i]->count < n->t)
            btree_fill(n, i);
 
        if (savedCount == i && i > n->count)
            btree_node_delete(n->children[i - 1], x);
        else
            btree_node_delete(n->children[i], x);
    }
}

void btree_delete(struct btree *t, int x)
{
    if (!t)
    {
        errno = EINVAL;
        return ;
    }

    if (!t->root)
        return ;

    if (!btree_contains(t, x))
        return ; // do not need to delete
    
    btree_node_delete(t->root, x);

    // Root might have zero keys but only one child, replace root with this child
    if (t->root->count == 0)
    {
        // no keys but one child
        struct btree_node* node = t->root;
        if (t->root->leaf)
            t->root = NULL; // root is a leaf
        else
            t->root = t->root->children[0]; // save one and only child instead of empty root

        // remove root but not recursively
        free(node->keys);
        free(node->children);
        free(node);
    }
}

static bool btree_node_contains(struct btree_node* r, int x)
{
    if (r->count == 0)
        return false;

    unsigned i = 0;
    while (i < r->count && x > r->keys[i])
        ++i;

    if (i < r->count) // check not to be out of range with r->keys[i]
    {
        if (i < r->count && r->keys[i] == x)
        {
            return true;
        }
        else
        {
            if (r->leaf) // nowhere to search
                return false;
            return btree_node_contains(r->children[i], x);
        }
    }

    if (r->leaf) // nowhere to search
        return false;
    
    return btree_node_contains(r->children[i], x);
}

bool btree_contains(struct btree *t, int x)
{
    if (!t)
    {
        errno = EINVAL;
        return false;
    }

    if (!t->root)
        return false;
    
    return btree_node_contains(t->root, x);
}

struct stack_data
{
    struct btree_node* node;
    unsigned idx;
};

struct stack
{
    struct stack_data* data;
    unsigned maxSize;
    unsigned currentSize;
};

struct stack* stack_alloc(unsigned maxSize)
{
    struct stack* st = (struct stack*) malloc(sizeof(*st));
    if (!st)
        return NULL;
    
    st->data = (struct stack_data*) malloc(sizeof(struct stack_data) * maxSize);
    if (!st->data)
    {
        free(st);
        return NULL;
    }

    st->maxSize = maxSize;
    st->currentSize = 0;

    return st;
}

void stack_free(struct stack* st)
{
    if (st)
    {
        if (st->data)
            free(st->data);
        
        free(st);
    }
}

void stack_push_back(struct stack* st, struct btree_node* node, unsigned idx)
{
    if (!st || !node)
    {
        errno = EINVAL;
        return ;
    }

    if (st->currentSize + 1 >= st->maxSize)
    {
        errno = EOVERFLOW;
        return ;
    }

    st->data[st->currentSize].node = node;
    st->data[st->currentSize].idx = idx;
    st->currentSize++;
}

struct stack_data* stack_pop(struct stack* st)
{
    if (!st)
    {
        errno = EINVAL;
        return NULL;
    }

    if (st->currentSize == 0)
    {
        errno = ERANGE;
        return NULL;
    }

    return &st->data[--(st->currentSize)];
}

static unsigned stack_size(struct stack* st)
{
    return st->currentSize;
}

const unsigned maxDepth = 15;

struct btree_iter
{
    struct stack* st;
    struct btree_node* currentNode;
    unsigned currentIdx;
};

static struct btree_iter* btree_iter_alloc()
{
    struct btree_iter* it = (struct btree_iter*) malloc(sizeof(*it));
    if (!it)
        return NULL;
    
    it->currentNode = NULL;
    it->currentIdx = 0;
    it->st = stack_alloc(maxDepth);
    if (!it->st)
    {
        free(it);
        return NULL;
    }

    return it;
}

static void btree_iter_free(struct btree_iter* it)
{
    if (it)
    {
        // dont free current, we do not own it
        if (it->st)
            stack_free(it->st);

        free(it);
    }
}

static struct btree_iter* btree_node_iter_start(struct btree_node* n)
{
    // init iterator
    struct btree_iter* it = btree_iter_alloc();
    if (!it)
        return NULL;

    // get first element
    it->currentNode = n;
    while (it->currentNode->count > 0)
    {
        if (it->currentNode->leaf)
            break;
        stack_push_back(it->st, it->currentNode, 0);
        it->currentNode = it->currentNode->children[0];
    }

    return it;
}

struct btree_iter* btree_iter_start(struct btree *t)
{
    if (!t)
    {
        errno = EINVAL;
        return NULL;
    }

    if (!t->root)
        return NULL;

    return btree_node_iter_start(t->root);
}

void btree_iter_end(struct btree_iter *i)
{
    btree_iter_free(i);
}

bool btree_iter_next(struct btree_iter *i, int *x)
{
    if (!i || !x)
        return false;

    if (i->currentIdx >= i->currentNode->count)
        return false; // end

    if (!i->currentNode)
        return false;

    *x = i->currentNode->keys[i->currentIdx++];
    if (i->currentNode->leaf)
    {
        // leaf

        if (i->currentIdx >= i->currentNode->count) // out of range
        {
            // pop stack, get next it
            struct stack_data* data = stack_pop(i->st);

            if (data)
            {
                while (data->idx >= data->node->count && stack_size(i->st) > 0)
                    data = stack_pop(i->st); // pop until not iterated leaf
                
                if (stack_size(i->st) == 0)
                {
                    // we are in root node
                    if (data->idx > data->node->count + 1)
                    {
                        // end
                        return false;
                    }
                }

                i->currentNode = data->node;
                i->currentIdx = data->idx;
            }
            else
            {
                return true;
            }
        }
        // else == already incremented            
    }
    else
    {
        // not leaf

        if (i->currentIdx < i->currentNode->count + 1)
        {
            // there are children

            // push curent node with fresh idx
            stack_push_back(i->st, i->currentNode, i->currentIdx);
            // get new begin
            i->currentNode = i->currentNode->children[i->currentIdx];
            i->currentIdx = 0;
            while (i->currentNode->count > 0 && !i->currentNode->leaf)
            {
                stack_push_back(i->st, i->currentNode, 0);
                i->currentNode = i->currentNode->children[0];
            }
        }
        else
        {
            // no children
            // pop stack, get next it
            struct stack_data* data = stack_pop(i->st);
            if (data)
            {
                while (data->idx >= data->node->count && stack_size(i->st) > 0)
                data = stack_pop(i->st); // pop until not iterated leaf
            
                if (stack_size(i->st) == 0)
                {
                    // we are in root node
                    if (data->idx > data->node->count + 1)
                    {
                        // end
                        return false;
                    }
                }

                i->currentNode = data->node;
                i->currentIdx = data->idx;
            }
            else
            {
                return true;
            }
        }
    }

    return true;
}
