#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "figtree.h"
#include "figtreenode.h"
#include "interval.h"
#include "utils.h"

/* Fig Tree */

void ft_init(struct figtree* this) {
    this->root = ftn_new(0, true);
}

struct insertargs {
    struct interval range;
    figtree_value_t value;
    struct ft_node** path;
    int path_len;
    int* pathIndices;
    int pathIndices_len;
    struct ft_node* at;
    struct interval valid;
};

struct insertcont {
    bool hasleftc;
    struct insertargs leftc;
    bool hasrightc;
    struct insertargs rightc;
};

void _ft_insert(struct figtree* this, struct insertargs* args,
                bool rightcontinuation, struct insertcont* ic) {
    struct interval* range = &args->range;
    figtree_value_t value = args->value;
    struct ft_node** path = args->path;
    int* pathIndices = args->pathIndices;
    struct ft_node* currnode = args->at;
    struct interval* valid = &args->valid;

    int finalsharedindex = args->pathIndices_len - 1;

    /* Record the residual groups [star1, range->left - 1] and
       [range->right + 1, star2]. */

    int numentries, i;

    memset(ic, 0x00, sizeof(struct insertcont));

    outerloop:
    while (currnode != NULL) {
        struct ft_ent* current;
        struct interval* previval;
        struct interval* currival;
        ftn_pruneTo(currnode, valid);
        numentries = currnode->entries_len;
        current = NULL;
        currival = NULL;

        for (i = 0; i < numentries; i++) {
            previval = currival;
            current = &currnode->entries[i];
            currival = &current->irange;
            if (i_overlaps(currival, range)) {
                int j;
                struct ft_ent* previous;
                
                path[args->path_len++] = currnode;
                if (currival->left < range->left) {
                    // Create a continuation for the left subinterval
                    ic->hasleftc = true;
                    i_init(&ic->leftc.range, currival->left, range->left - 1);
                    ic->leftc.value = current->value;
                    ic->leftc.path = path;
                    ic->leftc.path_len = args->path_len;
                    ic->leftc.pathIndices = pathIndices;
                    // A new entry is to be added to pathIndices later
                    ic->leftc.pathIndices_len = args->pathIndices_len + 1;
                    ic->leftc.at = subtree_get(&currnode->subtrees[i]);
                    memcpy(&ic->leftc.valid, valid, sizeof(struct interval));
                    i_restrict_range(&ic->leftc.valid, i == 0 ? BYTE_INDEX_MIN :
                                     previval->right + 1,
                                     currival->left - 1, true);
                }
                
                /* The entry in this node immediately after current will either
                 * be disjoint from RANGE, or will left-overlap it. It can't
                 * right-overlap it (unless it also left-overlaps it).
                 */
                previous = current;
                for (j = i + 1; j < numentries &&
                         i_leftOverlaps(&(current =
                                          &currnode->entries[j])->irange,
                                        range); j++) {
                    previous = current;
                }

                /* Now, either CURRENT is the first entry in the node that is
                 * disjoint from RANGE, or, if there is no such entry, then
                 * j == numentries. In either case, PREVIOUS is the last entry
                 * in the node that overlaps with RANGE.
                 */
                ftn_replaceEntries(currnode, i, j, range, value);
                if (previous->irange.right > range->right) {
                    // Create a continuation for the right subinterval
                    ic->hasrightc = true;
                    i_init(&ic->rightc.range, range->right + 1,
                           previous->irange.right);
                    ic->rightc.value = previous->value;
                    ic->rightc.path = path;
                    ic->rightc.path_len = args->path_len;
                    ic->rightc.pathIndices = pathIndices;
                    // A new entry is to be added to pathIndices later
                    ic->rightc.pathIndices_len = args->pathIndices_len + 1;
                    ic->rightc.at = subtree_get(&currnode->subtrees[i + 1]);
                    memcpy(&ic->rightc.valid, valid, sizeof(struct interval));
                    i_restrict_range(&ic->rightc.valid,
                                     previous->irange.right + 1,
                                     j == numentries ? BYTE_INDEX_MAX :
                                     current->irange.left - 1, true);
                    /* If there's a right continuation, then we set the
                     * path index to that of the right continuation. */
                    /* If there's also a left continuation, then we adjust the
                     * index for the left continuation. */
                } else {
                    /* There's no right continuation that will adjust the final
                     * shared path index for the left continuation. So, we need
                     * to directly insert the index for the left continuation
                     * here, in case there is a left continuation.
                     */
                    pathIndices[args->pathIndices_len++] = i;
                }
		goto treeinsertion;
            } else if (i_rightOf_int(currival, range)) {
                path[args->path_len++] = currnode;
                pathIndices[args->pathIndices_len++] = i;
                currnode = subtree_get(&currnode->subtrees[i]);
                /* What if previval and currival are adjacent intervals? Then
                 * the entire subtree can be pruned. This is represented by the
                 * special empty interval.
                 */
                i_restrict_range(valid, previval == NULL ? BYTE_INDEX_MIN :
                                 previval->right + 1, currival->left - 1, true);
                goto outerloop;
            }
        }
        path[args->path_len++] = currnode;
        pathIndices[args->pathIndices_len++] = numentries;
        currnode = subtree_get(&currnode->subtrees[numentries]);
        i_restrict_range(valid, currival == NULL ? BYTE_INDEX_MIN :
                         currival->right + 1, BYTE_INDEX_MAX, true);
    }

    treeinsertion:
    if (currnode == NULL) {
        // In this case, we actually need to do an insertion
        struct ft_ent toinsert;
        struct ft_node* rv = NULL;
        struct ft_node* topushnode = NULL;
        struct ft_ent* topushent = &toinsert;
        struct ft_node* left = NULL;
        struct ft_node* right = NULL;
        struct ft_node* insertinto;
        int insertindex;
        int pathindex;

        memcpy(&toinsert.irange, range, sizeof(struct interval));
        toinsert.value = value;

        for (pathindex =
                 args->pathIndices_len - 1; pathindex >= 0; pathindex--) {
            insertinto = path[pathindex];
            insertindex = pathIndices[pathindex];
            rv = ftn_insert(insertinto, topushent, insertindex, left, right);
            mem_free(topushnode);
            topushnode = rv;
            
            if (rightcontinuation) {
                /*
                 * All indices in the pathIndices and path lists at or before
                 * FINALSHAREDINDEX are shared with the path in a left
                 * continuation that has not yet been executed. If any nodes get
                 * split along that path, we need to update the path accordingly.
                 * 
                 * Special case: we need to artificially decrement the stored
                 * insertindex at the FINALSHAREDINDEX because the left
                 * continuation takes the left subtree of the primary range
                 * (whereas we took the right branch).
                 */
                if (pathindex == finalsharedindex) {
                    pathIndices[pathindex] = --insertindex;
                } else if (pathindex < finalsharedindex) {
                    struct ft_node* nextpathmember = path[pathindex + 1];
                    if (nextpathmember == right) {
                        pathIndices[pathindex] = ++insertindex;
                    }
                }

                if (topushnode != NULL) {
                    /* If this node is being split, then there are some
                     * complications. We need to change the node itself along
                     * the path. We also need to adjust the index accordingly.
                     */
                    if (insertindex <= FT_ORDER) {
                        path[pathindex] = subtree_get(&topushnode->subtrees[0]);
                    } else {
                        path[pathindex] = subtree_get(&topushnode->subtrees[1]);
                        pathIndices[pathindex] =
                            (insertindex -= (FT_ORDER + 1));
                    }
                }
            }

            if (topushnode == NULL) {
                // Nothing to push up
                /* Edge case: If we didn't hit the case where
                 * pathindex == finalsharedindex, we need to make sure that that
                 * entry of pathIndices got decremented anyway (so that the left
                 * continuation works as expected).
                 */
                if (rightcontinuation && pathindex > finalsharedindex) {
                    pathIndices[finalsharedindex]--;
                }
                return;
            }
            
            topushent = &topushnode->entries[0];
            left = subtree_get(&topushnode->subtrees[0]);
            right = subtree_get(&topushnode->subtrees[1]);
        }

        // No parent to push to
        this->root = topushnode;
        if (rightcontinuation) {
            struct ft_node* nextpathmember = path[0];
            memmove(&pathIndices[1], pathIndices,
                    args->pathIndices_len * sizeof(int));
            args->pathIndices_len++;
            memmove(&path[1], path, args->path_len * sizeof(struct ft_node*));
            args->path_len++;
            if (nextpathmember == subtree_get(&topushnode->subtrees[1])) {
                pathIndices[0] = 1;
            } else {
                ASSERT(nextpathmember == subtree_get(&topushnode->subtrees[0]),
                       "First element of path is not a child of the new root");
                pathIndices[0] = 0;
            }
            finalsharedindex++;
        }
    }
    if (rightcontinuation) {
        args->path_len = finalsharedindex;
        args->pathIndices_len = finalsharedindex;
    }
}

void ft_write(struct figtree* this, byte_index_t start, byte_index_t end,
              figtree_value_t value) {
    // Plus one because the height of the tree may increase on insert
    int maxdepth = this->root->HEIGHT + 1;
    struct ft_node* path[maxdepth];
    int pathIndices[maxdepth];
    struct insertargs iargs;
    struct insertcont starinserts;
    struct insertcont newstarinserts;

    i_init(&iargs.range, start, end);
    iargs.value = value;
    iargs.path = path;
    iargs.path_len = 0;
    iargs.pathIndices = pathIndices;
    iargs.pathIndices_len = 0;
    iargs.at = this->root;
    i_init(&iargs.valid, BYTE_INDEX_MIN, BYTE_INDEX_MAX);
    
    _ft_insert(this, &iargs, false, &starinserts);
    if (starinserts.hasrightc) {
        _ft_insert(this, &starinserts.rightc, true, &newstarinserts);
        ASSERT(!newstarinserts.hasleftc && !newstarinserts.hasrightc,
               "Recursive star insert on right continutation");
    }
    if (starinserts.hasleftc) {
        _ft_insert(this, &starinserts.leftc, false, &newstarinserts);
        ASSERT(!newstarinserts.hasleftc && !newstarinserts.hasrightc,
               "Recursive star insert on left continuation");
    }

    // Free the (aliased) lists of the continuations
    if (starinserts.hasleftc) {
        mem_free(starinserts.leftc.path);
        mem_free(starinserts.leftc.pathIndices);
    } else if (starinserts.hasrightc) {
        mem_free(starinserts.rightc.path);
        mem_free(starinserts.rightc.pathIndices);
    }
}

void ft_dealloc(struct figtree* this) {
    ftn_free(this->root);
    this->root = NULL;
}
