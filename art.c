#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <assert.h>
#include "art.h"

#ifdef __i386__
    #include <emmintrin.h>
#else
#ifdef __amd64__
    #include <emmintrin.h>
#endif
#endif


/**
 * Macros to manipulate pointer tags
 */
#define IS_LEAF(x) (((uintptr_t)x & 1))
#define SET_LEAF(x) ((void*)((uintptr_t)x | 1))
#define LEAF_RAW(x) ((art_leaf*)((void*)((uintptr_t)x & ~1)))

/**
 * Allocates a node of the given type,
 * initializes to zero and sets the type.
 */
static art_node* alloc_node(uint8_t type) {
    art_node* n;
    switch (type) {
        case NODE4:
            n = (art_node*)calloc(1, sizeof(art_node4));
            break;
        case NODE16:
            n = (art_node*)calloc(1, sizeof(art_node16));
            break;
        case NODE48:
            n = (art_node*)calloc(1, sizeof(art_node48));
            break;
        case NODE256:
            n = (art_node*)calloc(1, sizeof(art_node256));
            break;
        case NODE4LEAF:
            n = (art_node*)calloc(1, sizeof(art_node4_leaf));
            break;
        case NODE16LEAF:
            n = (art_node*)calloc(1, sizeof(art_node16_leaf));
            break;
        case NODE48LEAF:
            n = (art_node*)calloc(1, sizeof(art_node48_leaf));
            break;
        case NODE256LEAF:
            n = (art_node*)calloc(1, sizeof(art_node256_leaf));
            break;
        default:
            abort();
    }
    n->type = type;
    return n;
}

/**
 * Initializes an ART tree
 * @return 0 on success.
 */
int art_tree_init(art_tree *t) {
    t->root = NULL;
    t->size = 0;
    return 0;
}

// Recursively destroys the tree
static void destroy_node(art_node *n) {
    // Break if null
    if (!n) return;

    // Special case leafs
//    if (IS_LEAF(n)) {
//        free(LEAF_RAW(n));
//        return;
//    }
    if(n->type > 4){
        free(n);
        return;
    }

    // Handle each node type
    int i, idx;
    union {
        art_node4 *p1;
        art_node16 *p2;
        art_node48 *p3;
        art_node256 *p4;
    } p;
    switch (n->type) {
        case NODE4:
            p.p1 = (art_node4*)n;
            for (i=0;i<n->num_children;i++) {
                destroy_node(p.p1->children[i]);
            }
            break;

        case NODE16:
            p.p2 = (art_node16*)n;
            for (i=0;i<n->num_children;i++) {
                destroy_node(p.p2->children[i]);
            }
            break;

        case NODE48:
            p.p3 = (art_node48*)n;
            for (i=0;i<256;i++) {
                idx = ((art_node48*)n)->keys[i]; 
                if (!idx) continue; 
                destroy_node(p.p3->children[idx-1]);
            }
            break;

        case NODE256:
            p.p4 = (art_node256*)n;
            for (i=0;i<256;i++) {
                if (p.p4->children[i])
                    destroy_node(p.p4->children[i]);
            }
            break;
        default:
            abort();
    }

    // Free ourself on the way up
    free(n);
}

/**
 * Destroys an ART tree
 * @return 0 on success.
 */
int art_tree_destroy(art_tree *t) {
    destroy_node(t->root);
    return 0;
}

/**
 * Returns the size of the ART tree.
 */

#ifndef BROKEN_GCC_C99_INLINE
extern inline uint64_t art_size(art_tree *t);
#endif

//find_child???????????????????????????????????????????????????????????????key????????????????????????????????????????????????
static art_node** find_child(art_node *n, unsigned char c, bool direction, art_node*** border) {
    int i, mask, bitfield;
    union {
        art_node4 *p1;
        art_node16 *p2;
        art_node48 *p3;
        art_node256 *p4;
    } p;
    switch (n->type) {
        case NODE4:
            p.p1 = (art_node4*)n;
            if(direction)
                *border = &p.p1->children[n->num_children - 1];
            else
                *border = &p.p1->children[0];
            //traverse children
            for (i=0 ; i < n->num_children; i++) {
		/* this cast works around a bug in gcc 5.1 when unrolling loops
		 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=59124
		 */
                if (((unsigned char*)p.p1->keys)[i] == c)
                    // attention &
                    return &p.p1->children[i];
            }
            break;

        {
        case NODE16:
            p.p2 = (art_node16*)n;
            if(direction)
                *border = &p.p2->children[n->num_children - 1];
            else
                *border = &p.p2->children[0];
            // support non-86 architectures
            #ifdef __i386__
                // Compare the key to all 16 stored keys
                __m128i cmp;
                cmp = _mm_cmpeq_epi8(_mm_set1_epi8(c),
                        _mm_loadu_si128((__m128i*)p.p2->keys));
                
                // Use a mask to ignore children that don't exist
                mask = (1 << n->num_children) - 1;
                bitfield = _mm_movemask_epi8(cmp) & mask;
            #else
            #ifdef __amd64__
                // Compare the key to all 16 stored keys
                __m128i cmp;
                cmp = _mm_cmpeq_epi8(_mm_set1_epi8(c),
                        _mm_loadu_si128((__m128i*)p.p2->keys));

                // Use a mask to ignore children that don't exist
                mask = (1 << n->num_children) - 1;
                bitfield = _mm_movemask_epi8(cmp) & mask;
            #else
                // Compare the key to all 16 stored keys
                bitfield = 0;
                for (i = 0; i < 16; ++i) {
                    if (p.p2->keys[i] == c)
                        bitfield |= (1 << i);
                }

                // Use a mask to ignore children that don't exist
                mask = (1 << n->num_children) - 1;
                bitfield &= mask;
            #endif
            #endif

            /*
             * If we have a match (any bit set) then we can
             * return the pointer match using ctz to get
             * the index.
             */
            if (bitfield)
                return &p.p2->children[__builtin_ctz(bitfield)];
            break;
        }

        case NODE48:
            p.p3 = (art_node48*)n;
            if(direction)
                *border = &p.p3->children[n->num_children - 1];
            else
                *border = &p.p3->children[0];
            i = p.p3->keys[c];
            if (i)
                return &p.p3->children[i-1];
            break;

        case NODE256:
            p.p4 = (art_node256*)n;
            if(direction)
                *border = &p.p4->children[n->num_children - 1];
            else
                *border = &p.p4->children[0];
            if (p.p4->children[c])
                return &p.p4->children[c];
            break;

        default:
            abort();
    }
    //??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
    return NULL;
}

static art_node** find_child_boundary(art_node *n, unsigned char c, bool direction, art_node ***border) {
    int mask, bitfield;
    union {
        art_node4 *p1;
        art_node16 *p2;
        art_node48 *p3;
        art_node256 *p4;
    } p;
    switch (n->type) {
        case NODE4:
            p.p1 = (art_node4*)n;
            //traverse children
            if(direction){//low
                for(int i=0; i < n->num_children; i++){
                    if(((unsigned char*)p.p1->keys)[i] > c)
                        return &p.p1->children[i];
                }
            }else{
                for(int i=n->num_children-1; i>-1; i--){
                    if(((unsigned char*)p.p1->keys)[i] < c)
                        return &p.p1->children[i];
                }
            }
            break;

            {
                case NODE16:
                    p.p2 = (art_node16*)n;

                // support non-86 architectures
#ifdef __i386__
                // Compare the key to all 16 stored keys
                __m128i cmp;
                cmp = _mm_cmpeq_epi8(_mm_set1_epi8(c),
                        _mm_loadu_si128((__m128i*)p.p2->keys));

                // Use a mask to ignore children that don't exist
                mask = (1 << n->num_children) - 1;
                bitfield = _mm_movemask_epi8(cmp) & mask;
#else
#ifdef __amd64__
                // Compare the key to all 16 stored keys
                //Variables of type _m128i are automatically
                // aligned on 16-byte boundaries.
                __m128i cmp;
                if(direction){
                    cmp = _mm_cmplt_epi8(_mm_set1_epi8(c),
                                         _mm_loadu_si128((__m128i*)p.p2->keys));
                    *border = &p.p2->children[n->num_children - 1];
                }else{
                    cmp = _mm_cmpgt_epi8(_mm_set1_epi8(c),
                                         _mm_loadu_si128((__m128i*)p.p2->keys));
                    *border = &p.p2->children[0];
                }
                // Use a mask to ignore children that don't exist
                mask = (1 << n->num_children) - 1;
                bitfield = _mm_movemask_epi8(cmp) & mask;
#else
                // Compare the key to all 16 stored keys
                bitfield = 0;
                for (i = 0; i < 16; ++i) {
                    if (p.p2->keys[i] == c)
                        bitfield |= (1 << i);
                }

                // Use a mask to ignore children that don't exist
                mask = (1 << n->num_children) - 1;
                bitfield &= mask;
#endif
#endif

                /*
                 * If we have a match (any bit set) then we can
                 * return the pointer match using ctz to get
                 * the index.
                 */
                if (bitfield){
                    if(direction)
                        return &p.p2->children[__builtin_ctz(bitfield)];
                    else
                        return &p.p2->children[31 - __builtin_clz(bitfield)];
                }
                break;
            }

        case NODE48:
            //Node48 used a 6 bits array to store pointer to 48array.
            p.p3 = (art_node48*)n;
            //Traverse in one direction to find the first greater than or less than c
            if(direction){
                //from key find first greater than c
                for(int i=c; i < 256; i++){
                    if(p.p3->keys[i])
                        return &p.p3->children[i-1];
                }
            }else{
                for(int i=c; i > 0; i--){
                    if(p.p3->keys[i])
                        return &p.p3->children[i-1];
                }
            }
            break;

        case NODE256:
            p.p4 = (art_node256*)n;
            if(direction){
                for(int i=c; i<256; i++){
                    if(p.p4->children[c])
                        return &p.p4->children[i];
                }
            }else{
                for(int i=c; i>-1; i--){
                    if(p.p4->children[i])
                        return &p.p4->children[i];
                }
            }
           break;

        default:
            abort();
    }
    //this is why return a double-pointer, we can judge the key searching is exist or not by pointer
    return NULL;
}

//????????????????????????????????????art_leaf children[n]?????????????????????n
static int find_child_leaf(art_node* n, unsigned char c, bool direction, int *border){
    int i, mask, bitfield;
    union {
        art_node4_leaf *p1;
        art_node16_leaf *p2;
        art_node48_leaf *p3;
        art_node256_leaf *p4;
    } p;
    switch(n->type){
        case NODE4LEAF:
            p.p1 = (art_node4_leaf*)n;
            //TODO: direction
            for(int i=0; i<n->num_children; i++){
                if(((unsigned char*)p.p1->keys)[i] == c){
                    return i;
                }
            }
            break;
        case NODE16LEAF:
            p.p2 = (art_node16_leaf*)n;
            // support non-86 architectures
#ifdef __i386__
            // Compare the key to all 16 stored keys
                __m128i cmp;
                cmp = _mm_cmpeq_epi8(_mm_set1_epi8(c),
                        _mm_loadu_si128((__m128i*)p.p2->keys));

                // Use a mask to ignore children that don't exist
                mask = (1 << n->num_children) - 1;
                bitfield = _mm_movemask_epi8(cmp) & mask;
#else
#ifdef __amd64__
            // Compare the key to all 16 stored keys
                //Variables of type _m128i are automatically
                // aligned on 16-byte boundaries.
                __m128i cmp;
                //FIXME:??????direction
                // Use a mask to ignore children that don't exist
                mask = (1 << (n->num_children+1)) - 1;
            cmp = _mm_cmpeq_epi8(_mm_set1_epi8(c),
                                 _mm_loadu_si128((__m128i*)p.p2->keys));
            bitfield = _mm_movemask_epi8(cmp) & mask;
#else
            // Compare the key to all 16 stored keys
            bitfield = 0;
            for (i = 0; i < 16; ++i) {
                if (p.p2->keys[i] == c)
                    bitfield |= (1 << i);
            }

            // Use a mask to ignore children that don't exist
            mask = (1 << n->num_children) - 1;
            bitfield &= mask;
#endif
#endif
            /*
             * If we have a match (any bit set) then we can
             * return the pointer match using ctz to get
             * the index.
             */
            if (bitfield){
                return __builtin_ctz(bitfield);
            }
            break;
        case NODE48LEAF:
            p.p3 = (art_node48_leaf*)n;
            i = p.p3->keys[c];
            if(i)
                return i-1;
            break;
        case NODE256LEAF:
            p.p4 = (art_node256_leaf*)n;
            if(p.p4->children[c].key_len)
                return c;
            break;
        default:
            abort();
    }
    return -1;
}

static int find_child_leaf_boundary(art_node *n, unsigned char c,
                                    bool direction, int *border){
    int i, mask, bitfield;
    union {
        art_node4_leaf *p1;
        art_node16_leaf *p2;
        art_node48_leaf *p3;
        art_node256_leaf *p4;
    } p;

    switch (n->type) {
        case NODE4LEAF:
            p.p1 = (art_node4_leaf*)n;
            if(direction){
                *border = n->num_children - 1;
                for(int i=0; i<n->num_children;i++){
                    if(p.p1->keys[i] >= c)
                        return i;
                }
                return -1;
            } else {
                *border = 0;
                for(int i=n->num_children; i > -1; i--){
                    if(p.p1->keys[i] <= c)
                        return i;
                }
                return -1;
            }
            break;
            {case NODE16LEAF:
            p.p2 = (art_node16_leaf*)n;
            // support non-86 architectures
#ifdef __i386__
            // Compare the key to all 16 stored keys
                __m128i cmp;
                cmp = _mm_cmpeq_epi8(_mm_set1_epi8(c),
                        _mm_loadu_si128((__m128i*)p.p2->keys));

                // Use a mask to ignore children that don't exist
                mask = (1 << n->num_children) - 1;
                bitfield = _mm_movemask_epi8(cmp) & mask;
#else
#ifdef __amd64__
            // Compare the key to all 16 stored keys
            //Variables of type _m128i are automatically
            // aligned on 16-byte boundaries.
            __m128i cmp;
            if(direction){
                cmp = _mm_cmplt_epi8(_mm_set1_epi8(c),
                                     _mm_loadu_si128((__m128i*)p.p2->keys));
                *border = n->num_children - 1;
            }else{
                cmp = _mm_cmpgt_epi8(_mm_set1_epi8(c),
                                     _mm_loadu_si128((__m128i*)p.p2->keys));
                *border = 0;
            }
            // Use a mask to ignore children that don't exist
            mask = (1 << n->num_children) - 1;
            bitfield = _mm_movemask_epi8(cmp) & mask;
#else
            // Compare the key to all 16 stored keys
                bitfield = 0;
                for (i = 0; i < 16; ++i) {
                    if (p.p2->keys[i] == c)
                        bitfield |= (1 << i);
                }

                // Use a mask to ignore children that don't exist
                mask = (1 << n->num_children) - 1;
                bitfield &= mask;
#endif
#endif

            /*
             * If we have a match (any bit set) then we can
             * return the pointer match using ctz to get
             * the index.
             */
            if (bitfield){
                if(direction)
                    return __builtin_ctz(bitfield);
                else
                    return 31 - __builtin_clz(bitfield);
            }
            break;
        }
        case NODE48LEAF:
            p.p3 = (art_node48_leaf *)n;
            if(direction){
                *border = n->num_children - 1;
                for(int i=c; i<256; i++){
                    if(p.p3->keys[i])
                        return i;
                }
            } else {
                *border = 0;
                for(int i=c; i>-1; i--){
                    if(p.p3->keys[i])
                        return i;
                }
            }
            break;
        case NODE256LEAF:
            p.p4 = (art_node256_leaf*)n;
            if(direction){
                *border = 256;
                for(int i=c; i<256; i++){
                    if(p.p4->children[i].key_len)
                        return i;
                }
                return -1;
            } else {
                *border = 0;
                for(int i=c; i>-1; i--){
                    if(p.p4->children[i].key_len)
                        return i;
                }
                return -1;
            }
        default:
            abort();
    }
}

// Simple inlined if
static inline int min(int a, int b) {
    return (a < b) ? a : b;
}

/**
 * Returns the number of prefix characters shared between
 * the key and node.
 */
static int check_prefix(const art_node *n, const unsigned char *key, int key_len, int depth) {
    int max_cmp = min(min(n->partial_len, MAX_PREFIX_LEN), key_len - depth);
    int idx;
    for (idx=0; idx < max_cmp; idx++) {
        if (n->partial[idx] != key[depth+idx])
            return idx;
    }
    return idx;
}

static int check_prefix_direction(const art_node *n, const unsigned char *key, int key_len, int depth){
   int max_cmp = min(min(n->partial_len, MAX_PREFIX_LEN), key_len - depth);
   int idx;
   for(idx = 0; idx < max_cmp; idx++){
       if(n->partial[idx] > key[depth + idx])
           return -1;
       if(n->partial[idx] < key[depth + idx])
           return -2;
   }
   return idx;
}



/**
 * Checks if a leaf matches
 * @return 0 on success.
 */
static int leaf_matches(art_leaf *n, const unsigned char *key, int key_len, int depth) {
    (void)depth;
    // Fail if the key lengths are different
    if (n->key_len != (uint32_t)key_len) return 1;

    // Compare the keys starting at the depth, 0 represents equal.
    return memcmp(n->key, key, key_len);
}

//leaf_matches
//static int leaf_matches_node(art_node *n, int pos, const unsigned char *key, int key_len, int depth){
//    switch (n->type) {
//        case NODE4LEAF:
//            return leaf_matches(&(((art_node4_leaf*)n)->children[pos]), key, key_len, depth);
//        case NODE16LEAF:
//            return leaf_matches(&(((art_node16_leaf*)n)->children[pos]), key, key_len, depth);
//        case NODE48LEAF:
//            return leaf_matches(&(((art_node48_leaf*)n)->children[pos]), key, key_len, depth);
//        case NODE256LEAF:
//            return leaf_matches(&(((art_node256_leaf*)n)->children[pos]), key, key_len, depth);
//        default:
//            return -1;
//    }
//}

static art_leaf* get_leaf(art_node *n, int pos){
    switch (n->type){
        case NODE4LEAF:
            return  &(((art_node4_leaf*)n)->children[pos]);
        case NODE16LEAF:
            return  &(((art_node16_leaf*)n)->children[pos]);
        case NODE48LEAF:
            return  &(((art_node48_leaf*)n)->children[pos]);
        case NODE256LEAF:
            return  &(((art_node256_leaf*)n)->children[pos]);
    }
    return NULL;
}

/**
 * Searches for a value in the ART tree
 * @arg t The tree
 * @arg key The key
 * @arg key_len The length of the key
 * @return NULL if the item was not found, otherwise
 * the value pointer is returned.
 */
void* art_search(const art_tree *t, const unsigned char *key, int key_len) {
    art_node **child;
    art_node *n = t->root;
    int prefix_len, depth = 0;
    while (n) {
        // Might be a leaf
//        if (IS_LEAF(n)) {
//            n = (art_node*)LEAF_RAW(n);
//            // Check if the expanded path matches
//            if (!leaf_matches((art_leaf*)n, key, key_len, depth)) {
//                return ((art_leaf*)n)->value;
//            }
//            return NULL;
//        }

        // Bail if the prefix does not match
        //this is path compression of the paper. If prefix exists, need to compare and skip
        if (n->partial_len) {
            prefix_len = check_prefix(n, key, key_len, depth);
            if (prefix_len != min(MAX_PREFIX_LEN, n->partial_len))
                return NULL;
            depth = depth + n->partial_len;
        }

        if(n->type > 4){ // if n is a leaf node
            int *border;
            //ATTENTION: here the position l is pos+1
            int l = find_child_leaf(n, key[depth], true, border);
            if(l != -1){
                return get_leaf(n, l)->value;
            } else {
                return NULL;
            }
        }

        // Recursively search
        art_node **tmp = &n;
        art_node ***border = &tmp;
        child = find_child(n, key[depth], true, border);
        n = (child) ? *child : NULL;
        depth++;
    }
    return NULL;
}

// Find the minimum leaf under a node
//FIXME:??????minimum??????????????????????????????art_leaf
static art_leaf* minimum(const art_node *n) {
    // Handle base cases
    if (!n) return NULL;
    int idx;
    switch (n->type) {
        case NODE4:
            return minimum(((const art_node4*)n)->children[0]);
        case NODE16:
            return minimum(((const art_node16*)n)->children[0]);
        case NODE48:
            idx=0;
            while (!((const art_node48*)n)->keys[idx]) idx++;
            idx = ((const art_node48*)n)->keys[idx] - 1;
            return minimum(((const art_node48*)n)->children[idx]);
        case NODE256:
            idx=0;
            while (!((const art_node256*)n)->children[idx]) idx++;
            return minimum(((const art_node256*)n)->children[idx]);
        case NODE4LEAF:
            //art_node4_leaf??????children???????????????
            //FIXME: there is a const before
            return ((art_node4_leaf*)n)->children;
        case NODE16LEAF:
            return ((art_node16_leaf*)n)->children;
        case NODE48LEAF:
            idx = 0;
            while(!((const art_node48_leaf*)n)->keys[idx]) idx++;
            idx = ((const art_node48_leaf*)n)->keys[idx] - 1;
            return ((art_node48_leaf*)n)->children+idx;
        case NODE256LEAF:
            idx = 0;
            while(!((const art_node256_leaf*)n)->children[idx].key_len) idx++;
            return ((art_node256_leaf*)n)->children+idx;
        default:
            abort();
    }
}

// Find the maximum leaf under a node
static art_leaf* maximum(const art_node *n) {
    // Handle base cases
    if (!n) return NULL;
    if (IS_LEAF(n)) return LEAF_RAW(n);

    int idx;
    switch (n->type) {
        case NODE4:
            return maximum(((const art_node4*)n)->children[n->num_children-1]);
        case NODE16:
            return maximum(((const art_node16*)n)->children[n->num_children-1]);
        case NODE48:
            idx=255;
            while (!((const art_node48*)n)->keys[idx]) idx--;
            idx = ((const art_node48*)n)->keys[idx] - 1;
            return maximum(((const art_node48*)n)->children[idx]);
        case NODE256:
            idx=255;
            while (!((const art_node256*)n)->children[idx]) idx--;
            return maximum(((const art_node256*)n)->children[idx]);
        default:
            abort();
    }
}

/**
 * Returns the minimum valued leaf
 */
art_leaf* art_minimum(art_tree *t) {
    return minimum((art_node*)t->root);
}

/**
 * Returns the maximum valued leaf
 */
art_leaf* art_maximum(art_tree *t) {
    return maximum((art_node*)t->root);
}

//static art_leaf* make_leaf(const unsigned char *key, int key_len, void *value) {
//    art_leaf *l = (art_leaf*)calloc(1, sizeof(art_leaf)+key_len);
//    l->value = value;
//    l->key_len = key_len;
//    memcpy(l->key, key, key_len);
//    return l;
//}

static void init_leaf_array(art_leaf * leaves){
    art_leaf *l = leaves;
    while(l){
        l->key_len = 0;
        l++;
    }
}

//make a node4_leaf
static art_node* make_leaf(const unsigned char *key, int key_len, void *value, int depth) {
    art_node4_leaf *l = (art_node4_leaf*)calloc(1, sizeof(art_node4_leaf));
    // l->n partial_len, partial, num_children
    l->n.num_children = 1;
    l->n.partial_len = key_len - depth - 1;
    memcpy(l->n.partial, key+depth, min(MAX_PREFIX_LEN, l->n.partial_len));
    l->n.type = 5;
    l->keys[0] = key[depth + l->n.partial_len];

    memcpy(l->children[0].key, key, key_len);
    l->children[0].key_len = key_len;
    l->children[0].value = value;
    return (art_node*)l;
}

//static int longest_common_prefix(art_leaf *l1, art_leaf *l2, int depth) {
//    int max_cmp = min(l1->key_len, l2->key_len) - depth;
//    int idx;
//    for (idx=0; idx < max_cmp; idx++) {
//        if (l1->key[depth+idx] != l2->key[depth+idx])
//            return idx;
//    }
//    return idx;
//}

static void copy_header(art_node *dest, art_node *src) {
    dest->num_children = src->num_children;
    dest->partial_len = src->partial_len;
    memcpy(dest->partial, src->partial, min(MAX_PREFIX_LEN, src->partial_len));
}

static void add_child256(art_node256 *n, art_node **ref, unsigned char c, void *child) {
    (void)ref;
    n->n.num_children++;
    n->children[c] = (art_node*)child;
}

static void add_child48(art_node48 *n, art_node **ref, unsigned char c, void *child) {
    if (n->n.num_children < 48) {
        int pos = 0;
        while (n->children[pos]) pos++;
        n->children[pos] = (art_node*)child;
        n->keys[c] = pos + 1;
        n->n.num_children++;
    } else {
        art_node256 *new_node = (art_node256*)alloc_node(NODE256);
        for (int i=0;i<256;i++) {
            if (n->keys[i]) {
                new_node->children[i] = n->children[n->keys[i] - 1];
            }
        }
        copy_header((art_node*)new_node, (art_node*)n);
        *ref = (art_node*)new_node;
        free(n);
        add_child256(new_node, ref, c, child);
    }
}

static void add_child16(art_node16 *n, art_node **ref, unsigned char c, void *child) {
    if (n->n.num_children < 16) {
        unsigned mask = (1 << n->n.num_children) - 1;
        
        // support non-x86 architectures
        #ifdef __i386__
            __m128i cmp;

            // Compare the key to all 16 stored keys
            cmp = _mm_cmplt_epi8(_mm_set1_epi8(c),
                    _mm_loadu_si128((__m128i*)n->keys));

            // Use a mask to ignore children that don't exist
            unsigned bitfield = _mm_movemask_epi8(cmp) & mask;
        #else
        #ifdef __amd64__
            __m128i cmp;

            // Compare the key to all 16 stored keys
            cmp = _mm_cmplt_epi8(_mm_set1_epi8(c),
                    _mm_loadu_si128((__m128i*)n->keys));

            // Use a mask to ignore children that don't exist
            unsigned bitfield = _mm_movemask_epi8(cmp) & mask;
        #else
            // Compare the key to all 16 stored keys
            unsigned bitfield = 0;
            for (short i = 0; i < 16; ++i) {
                if (c < n->keys[i])
                    bitfield |= (1 << i);
            }

            // Use a mask to ignore children that don't exist
            bitfield &= mask;    
        #endif
        #endif

        // Check if less than any
        unsigned idx;
        if (bitfield) {
            idx = __builtin_ctz(bitfield);
            memmove(n->keys+idx+1,n->keys+idx,n->n.num_children-idx);
            memmove(n->children+idx+1,n->children+idx,
                    (n->n.num_children-idx)*sizeof(void*));
        } else
            idx = n->n.num_children;

        // Set the child
        n->keys[idx] = c;
        n->children[idx] = (art_node*)child;
        n->n.num_children++;

    } else {
        art_node48 *new_node = (art_node48*)alloc_node(NODE48);

        // Copy the child pointers and populate the key map
        memcpy(new_node->children, n->children,
                sizeof(void*)*n->n.num_children);
        for (int i=0;i<n->n.num_children;i++) {
            new_node->keys[n->keys[i]] = i + 1;
        }
        copy_header((art_node*)new_node, (art_node*)n);
        *ref = (art_node*)new_node;
        free(n);
        add_child48(new_node, ref, c, child);
    }
}

static void add_child4(art_node4 *n, art_node **ref, unsigned char c, void *child) {
    if (n->n.num_children < 4) {
        int idx;
        for (idx=0; idx < n->n.num_children; idx++) {
            if (c < n->keys[idx]) break;
        }

        // Shift to make room
        memmove(n->keys+idx+1, n->keys+idx, n->n.num_children - idx);
        memmove(n->children+idx+1, n->children+idx,
                (n->n.num_children - idx)*sizeof(void*));

        // Insert element
        n->keys[idx] = c;
        n->children[idx] = (art_node*)child;
        n->n.num_children++;

    } else {
        art_node16 *new_node = (art_node16*)alloc_node(NODE16);

        // Copy the child pointers and the key map
        memcpy(new_node->children, n->children,
                sizeof(void*)*n->n.num_children);
        memcpy(new_node->keys, n->keys,
                sizeof(unsigned char)*n->n.num_children);
        copy_header((art_node*)new_node, (art_node*)n);
        *ref = (art_node*)new_node;
        free(n);
        add_child16(new_node, ref, c, child);
    }
}

static void add_child(art_node *n, art_node **ref, unsigned char c, void *child) {
    switch (n->type) {
        case NODE4:
            return add_child4((art_node4*)n, ref, c, child);
        case NODE16:
            return add_child16((art_node16*)n, ref, c, child);
        case NODE48:
            return add_child48((art_node48*)n, ref, c, child);
        case NODE256:
            return add_child256((art_node256*)n, ref, c, child);
        default:
            abort();
    }
}

//this leaf is not initialize now, so add operation only care add.
static void add_child256_leaf(art_node256_leaf *n, art_node** ref,
                              int depth, const unsigned char *key,
                              int key_len, void* value){
    (void)ref;
    int c = (int)key[depth];
    if(n->children[c].key_len){
        n->n.num_children++;
    }
    n->children[c].key_len = key_len;
    n->children[c].value = value;
    memcpy(n->children[c].key, key, key_len);
}

static void add_child48_leaf(art_node48_leaf *n, art_node** ref,
                             int depth, const unsigned char *key,
                             int key_len, void* value){
    (void)ref;
    int c = (int)key[depth];
    if(n->n.num_children < 48){
        int pos = 0;
        while(!n->children[pos].key_len) pos++;
        n->children[pos].key_len = key_len;
        n->keys[c] = pos + 1;
        memcpy(n->children[c].key, key, key_len);
        n->n.num_children++;//not considering have same key
    } else {
        art_node256_leaf *new_node = (art_node256_leaf*)alloc_node(NODE256LEAF);
        for(int i=0; i<256; i++){
            if(n->keys[i]){
                int cur = n->keys[i] - 1;
                new_node->children[i].key_len = n->children[cur].key_len;
                memcpy(new_node->children[i].key, n->children[cur].key, n->children[cur].key_len);
                new_node->children[i].value = n->children[cur].value;
            }
        }
        copy_header((art_node*)new_node, (art_node*)n);
        *ref = (art_node*)new_node;
        free(n);
        //?????????????????????
        add_child256_leaf(new_node, ref, depth, key, key_len, value);
    }
}

static void add_child16_leaf(art_node16_leaf *n, art_node** ref,
                             int depth, const unsigned char *key,
                             int key_len, void* value){
    char c = key[depth];
    if (n->n.num_children < 16) {
        unsigned mask = (1 << n->n.num_children) - 1;

        // support non-x86 architectures
#ifdef __i386__
        __m128i cmp;

            // Compare the key to all 16 stored keys
            cmp = _mm_cmplt_epi8(_mm_set1_epi8(c),
                    _mm_loadu_si128((__m128i*)n->keys));

            // Use a mask to ignore children that don't exist
            unsigned bitfield = _mm_movemask_epi8(cmp) & mask;
#else
#ifdef __amd64__
        __m128i cmp;

        // Compare the key to all 16 stored keys
        cmp = _mm_cmplt_epi8(_mm_set1_epi8(c),
                             _mm_loadu_si128((__m128i*)n->keys));

        // Use a mask to ignore children that don't exist
        unsigned bitfield = _mm_movemask_epi8(cmp) & mask;
#else
        // Compare the key to all 16 stored keys
            unsigned bitfield = 0;
            for (short i = 0; i < 16; ++i) {
                if (c < n->keys[i])
                    bitfield |= (1 << i);
            }

            // Use a mask to ignore children that don't exist
            bitfield &= mask;
#endif
#endif

        // Check if less than any
        unsigned idx;
        if (bitfield) {
            idx = __builtin_ctz(bitfield);
            memmove(n->keys+idx+1,n->keys+idx,n->n.num_children-idx);
            memmove(n->children+idx+1,n->children+idx,
                    (n->n.num_children-idx)*sizeof(art_leaf));
        } else
            idx = n->n.num_children;
            //set the child
            n->n.num_children++;
            n->keys[idx] = key[depth];
            n->children[idx].key_len = key_len;
            n->children[idx].value = value;
            memcpy(n->children[idx].key, key, key_len);
    } else {
        art_node48_leaf *new_node = (art_node48_leaf*)alloc_node(NODE48LEAF);

        //????????????Node16Leaf??????????????????????????????
        //????????????????????????????????????????????????????????????????????????????????????
        for(int i=0; i<n->n.num_children; ++i){
            new_node->keys[n->keys[i]] = i + 1;
            //FIXME:???????????????????????????????????????, maybe should use memcpy
            new_node->children[i] = n->children[i];
        }
        copy_header((art_node*)new_node, (art_node*)n);
        *ref = (art_node*)new_node;
        free(n);
        add_child48_leaf(new_node, ref, depth, key, key_len, value);
    }
}

static void add_child4_leaf(art_node4_leaf *n, art_node** ref,
                            int depth, const unsigned char *key,
                            int key_len, void* value){
    char c = key[depth];
    if(n->n.num_children < 4){
        int idx;
        for(idx = 0; idx < n->n.num_children; idx++){
            if(c < n->keys[idx]) break;
        }
        //FIXME:???????????????????????????????????????????????????c???????????????????????????????????????????????????????????????????????????????????????????????????????????????
        memmove(n->keys+idx+1, n->keys+idx, n->n.num_children - idx);
        //FIXME: here maybe use sizeof(art_leaf)
        memmove(n->children+idx+1, n->children+idx,
                (n->n.num_children - idx)*sizeof(art_leaf));

        //??????
        n->keys[idx] = c;
        n->children[idx].key_len = key_len;
        n->children[idx].value = value;
        memcpy(n->children[idx].key, key, key_len);
        n->n.num_children++;
    } else {
        art_node16_leaf *new_node = (art_node16_leaf*)alloc_node(NODE16LEAF);

        //????????????
        //FIXME:??????????????????????????????memcpy
        memcpy(new_node->children, n->children, sizeof(art_leaf)*n->n.num_children);
        memcpy(new_node->keys, n->keys, sizeof(unsigned char)*n->n.num_children);
        copy_header((art_node*)new_node, (art_node*)n);
        *ref = (art_node*)new_node;
        free(n);
        //FIXME: here not work.
        add_child16_leaf(new_node, ref, depth, key, key_len, value);
    }
}

static void add_child_leaf(art_node *n, art_node** ref,
                           int depth, const unsigned char *key,
                           int key_len, void* value){
    switch(n->type){
        case NODE4LEAF:
            return add_child4_leaf((art_node4_leaf*)n, ref, depth, key, key_len, value);
        case NODE16LEAF:
            return add_child16_leaf((art_node16_leaf*)n, ref, depth, key, key_len, value);
        case NODE48LEAF:
            return add_child48_leaf((art_node48_leaf*)n, ref, depth, key, key_len, value);
        case NODE256LEAF:
            return add_child256_leaf((art_node256_leaf*)n, ref, depth, key, key_len, value);
        default:
            abort();
    }
}

//???????????????????????????????????????art_leaf???????????????art_node4_leaf?????????????????????art_nodex?????????
//??????depth????????????????????????????????????
static art_node* transform(art_node *n, int depth){
    union {
        art_node4_leaf *p1;
        art_node16_leaf *p2;
        art_node48_leaf *p3;
        art_node256_leaf *p4;
    } p;
    switch(n->type){
        case NODE4LEAF:
            p.p1 = (art_node4_leaf*)n;
            art_node4 *new_node4 = (art_node4*) alloc_node(NODE4);
            memcpy(new_node4->keys, p.p1->keys, p.p1->n.num_children);
            copy_header((art_node*)new_node4, (art_node*)n);
            for(int i=0; i < p.p1->n.num_children; i++){
                art_node4_leaf* new_leaf = (art_node4_leaf*)make_leaf(p.p1->children[i].key,
                                                     p.p1->children[i].key_len,
                                                     p.p1->children[i].value,
                                                     depth+n->partial_len+1);
                new_node4->children[i] = (art_node*)new_leaf;
            }
            free(n);
            return (art_node*)new_node4;
        case NODE16LEAF:
            p.p2 = (art_node16_leaf*)n;
//            art_node16* new_node = (art_node16*)alloc_node(NODE16);
            art_node16 *new_node16 = (art_node16*)alloc_node(NODE16);
            copy_header((art_node*)new_node16, (art_node*)n);
            memcpy(new_node16->keys, p.p2->keys, p.p2->n.num_children);
            for(int i=0; i < p.p1->n.num_children; i++){
                art_node4_leaf* new_leaf = (art_node4_leaf*)make_leaf(p.p2->children[i].key,
                                                     p.p2->children[i].key_len,
                                                     p.p2->children[i].value,
                                                     depth+n->partial_len+1);
                new_node16->children[i] = (art_node*)new_leaf;
            }
            return (art_node*)new_node16;
        case NODE48LEAF:
            p.p3 = (art_node48_leaf*)n;
            art_node48 *new_node48 = (art_node48*) alloc_node(NODE48);
            copy_header((art_node*)new_node48, (art_node*)n);
            memcpy(new_node48->keys, p.p3->keys, 256);
            for(int i=0; i < 256; i++){
                if(p.p3->keys[i]){
                    art_node4_leaf* new_leaf = (art_node4_leaf*)make_leaf(p.p3->children[i-1].key,
                                                         p.p3->children[i-1].key_len,
                                                         p.p3->children[i-1].value,
                                                         depth+n->partial_len+1);
                    new_node48->children[i] = (art_node*)new_leaf;
                }
            }
            return (art_node*)new_node48;
        case NODE256LEAF:
            p.p4 = (art_node256_leaf*)n;
            art_node256 *new_node256 = (art_node256*) alloc_node(NODE256);
            copy_header((art_node*)new_node256, (art_node*)n);
            for(int i=0; i < 256; i++){
                if(p.p3->children[i].key_len){
                    art_node4_leaf* new_leaf = (art_node4_leaf*)make_leaf(p.p4->children[i-1].key,
                                                         p.p4->children[i-1].key_len,
                                                         p.p4->children[i-1].value,
                                                         depth+n->partial_len+1);
                    new_node256->children[i]  = (art_node*)new_leaf;
                }
            }
            return (art_node*)new_node256;
        default:
            abort();
    }
}

/*FIXME: what difference between prefix_mismatch and longest_common_prefix?
 * prefix_mismatch compare two prefixes;
 * longest_common_prefix compare two strings;
 * */
/**
 * Calculates the index at which the prefixes mismatch
 */
static int prefix_mismatch(const art_node *n, const unsigned char *key, int key_len, int depth) {
    int max_cmp = min(min(MAX_PREFIX_LEN, n->partial_len), key_len - depth);
    int idx;
    for (idx=0; idx < max_cmp; idx++) {
        if (n->partial[idx] != key[depth+idx])
            return idx;
    }

    // If the prefix is short we can avoid finding a leaf
    if (n->partial_len > MAX_PREFIX_LEN) {
        // Prefix is longer than what we've checked, find a leaf
        art_leaf *l = minimum(n);
        max_cmp = min(l->key_len, key_len)- depth;
        for (; idx < max_cmp; idx++) {
            if (l->key[idx+depth] != key[depth+idx])
                return idx;
        }
    }
    return idx;
}

static int string_dismatch(const unsigned char *key1, const unsigned char *key2, int key_len, int depth){
    int max_cmp = key_len - depth;
    int idx;
    for(idx = depth; idx < max_cmp; idx++){
        if(key1[idx] != key2[idx])
            return idx;
    }
    return depth;
}

// Recursive insert
static void* recursive_insert(art_node *n, art_node **ref, const unsigned char *key, int key_len, void *value, int depth, int *old, int replace) {
    // If we are at a NULL node, inject a leaf
    if (!n) {
        *ref = (art_node*)make_leaf(key, key_len, value, 0);
        return NULL;
    }
    int prefix_diff = 0;
    bool recursive_flag = true;
    if(n->partial_len){
        prefix_diff = prefix_mismatch(n, key, key_len, depth);
        if((uint32_t)prefix_diff >= n->partial_len)
            depth += n->partial_len;
        else
            recursive_flag = false;
    }

    if(!recursive_flag){ //have prefix, and prefix mismatch
        //?????????????????????????????????????????????inner node(art_node4)?????????
        art_node4 *new_node = (art_node4 *) alloc_node(NODE4);
        *ref = (art_node *) new_node;
        new_node->n.partial_len = prefix_diff;
        memcpy(new_node->n.partial, n->partial, min(MAX_PREFIX_LEN, new_node->n.partial_len));
        //?????????????????????n?????????, and keys of n
        if (n->partial_len <= MAX_PREFIX_LEN) { //n????????????????????????
            add_child4(new_node, ref, n->partial[prefix_diff], n);
            n->partial_len -= (prefix_diff + 1);
            memmove(n->partial, n->partial + prefix_diff + 1,
                    min(MAX_PREFIX_LEN, n->partial_len));
        } else { //n????????????????????????????????????????????????????????????????????????????????????key????????????????????????????????????????????????
            n->partial_len -= (prefix_diff + 1);
            art_leaf *l = minimum(n);
            //????????????????????????n???????????????????????????????????????node???
            add_child4(new_node, ref, l->key[depth + prefix_diff], n);
            memcpy(n->partial, l->key + depth + prefix_diff + 1,
                   min(MAX_PREFIX_LEN, n->partial_len));
        }
        //FIXME:????????????????????????????????????????????????????????????????????????key?????????
        art_node *new_leaf = make_leaf(key, key_len, value, depth + prefix_diff + 1);
        add_child4(new_node, ref, key[depth + prefix_diff], new_leaf);
        return NULL;
    } else {//no prefix, or have prefix and matched
        if(n->type < 5){
            //inner node, recursive
            art_node **tmp = &n;
            art_node *** border = &tmp;
            art_node **child = find_child(n, key[depth], true, border);
            if(child){
                return recursive_insert(*child, child, key, key_len,
                                        value, depth+1, old, replace);
            }
            //no child, node goes within us
            art_node4_leaf *l = (art_node4_leaf *)make_leaf(key, key_len, value, depth+1);
            //how to add_child
            add_child(n, ref, key[depth], l);
            return NULL;
        } else {
            //leaf node, need consider the case of the key found not equal to the insert key.
            //????????????????????????????????????????????????
            //?????????find_child_leaf?????????????????????art_leaf????????? or return the position number?
            int *border;
            int l = find_child_leaf(n, key[depth], true, border);
            if(l != -1){
                art_leaf *leaf = get_leaf(n, l-1);
                //??????????????????????????????????????????
                if(!leaf_matches(leaf, key, key_len, depth)){//??????key?????????memcmp?????????????????????0
                    //the same key, only need to update
                    *old = 1;
                    void *old_val = leaf->value;
                    if(replace) leaf->value = value;
                    return old_val;
                } else {
                    //not same key
                    if(n->num_children == 1){
                        //for this case, the prefix is matched, it's the MAX_PREFIX_LEN is too short and path compression.
                        // we only need to modify keys[0]
                        // the case of n.num_children==1
                        int diff = string_dismatch(leaf->key, key, key_len, depth);
                        art_node4_leaf *node = (art_node4_leaf*)n;
                        node->keys[0] = leaf->key[diff];
                        node->keys[1] = key[diff];
                        node->children[1].key_len = key_len;
                        node->children[1].value = value;
                        memcpy(node->children[1].key, key, key_len);
                    } else {
                        //??????????????????????????????????????????????????????
                        art_node *new_node = transform(n, depth);
                        *ref = new_node;
                        recursive_insert(new_node, ref, key, key_len, value, depth, old, replace);//??????????????????????????????????????????
                    }
                }
            } else {
                //????????????????????????????????????
                //FIXME: need get corresponding art_leaf
//                n->children[i].key_len = key_len;
//                memcpy(n->children[i].key, key, key_len);
//                n->children[i].value = value;
                  add_child_leaf(n, ref, depth, key, key_len, value);
            }
        }
    }

//    if(n->type < 5){//inner node
//        if(recursive_flag){//??????????????????????????????
//            art_node **tmp = &n;
//            art_node *** border = &tmp;
//            art_node **child = find_child(n, key[depth], true, border);
//            if(child){
//                return recursive_insert(*child, child, key, key_len,
//                                        value, depth+1, old, replace);
//            }
//            //no child, node goes within us
//            art_node4_leaf *l = (art_node4_leaf *)make_leaf(key, key_len, value, depth+1);
//            //how to add_child
//            add_child(n, ref, key[depth], l);
//            return NULL;
//        } else {
//            //?????????????????????????????????????????????inner node(art_node4)?????????
//            art_node4 *new_node = (art_node4 *) alloc_node(NODE4);
//            *ref = (art_node *) new_node;
//            new_node->n.partial_len = prefix_diff;
//            memcpy(new_node->n.partial, n->partial, min(MAX_PREFIX_LEN, n->partial_len));
//
//            //?????????????????????n?????????
//            if (n->partial_len <= MAX_PREFIX_LEN) { //n????????????????????????
//                add_child4(new_node, ref, n->partial[prefix_diff], n);
//                n->partial_len -= (prefix_diff + 1);
//                memmove(n->partial, n->partial + prefix_diff + 1,
//                        min(MAX_PREFIX_LEN, n->partial_len));
//            } else { //n????????????????????????????????????????????????????????????????????????????????????key????????????????????????????????????????????????
//                n->partial_len -= (prefix_diff + 1);
//                art_leaf *l = minimum(n);
//                //????????????????????????n???????????????????????????????????????node???
//                add_child4(new_node, ref, l->key[depth + prefix_diff], n);
//                memcpy(n->partial, l->key + depth + prefix_diff + 1,
//                       min(MAX_PREFIX_LEN, n->partial_len));
//            }
//            //FIXME:????????????????????????????????????????????????????????????????????????key?????????
//            art_node *new_leaf = make_leaf(key, key_len, value, depth + prefix_diff + 1);
//            add_child4(new_node, ref, key[depth + prefix_diff], new_leaf);
//            return NULL;
//        }
//    }  else {//????????????????????????????????????key
//        if(recursive_flag){
//            //????????????????????????????????????????????????
//            int *border;
//            int l = find_child_leaf(n, key[depth], true, border);
//            if(l != -1){
//                //??????????????????????????????????????????
//                if(!leaf_matches(&(n->children[l]), key, key_len, depth)){//??????key?????????memcmp?????????????????????0
//                    *old = 1;
//                    void *old_val = *l->value;
//                    if(replace) *l->value = value;
//                    return old_val;
//                }
//                //??????????????????????????????????????????????????????
//                art_node *new_node = transform(n);
//                *ref = new_node;
//                recursive_insert(new_node, ref, key, key_len, value, depth, old, replace);//??????????????????????????????????????????
//            } else {
//                //????????????????????????????????????
//                n->children[i].key_len = key_len;
//                memcpy(n->children[i].key, key, key_len);
//                n->children[i].value = value;
//            }
//        }else {//????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
//            art_node4 *new_node = (art_node4*) alloc_node(NODE4);
//            *ref = (art_node4*)new_node;
//            new_node->n.partial_len = prefix_diff;
//            memcpy(new_node->n.partial, n->partial, min(MAX_PREFIX_LEN, n->partial_len));
//            //???????????????????????????
//            if(n->partial_len <= MAX_PREFIX_LEN){
//                add_child4(new_node, ref, n->partial[prefix_diff], n);
//                n->partial_len -= (prefix_diff + 1);
//                memmove(n->partial, n->partial+prefix_diff+1,
//                        min(MAX_PREFIX_LEN, n->partial_len));
//            } else {
//                n->partial_len -= (prefix_diff + 1);
//                art_leaf *l = minimum(n);
//
//                add_child4(new_node, ref, l->key[depth+prefix_diff], n);
//                memcpy(n->partial, l->key+depth+prefix_diff+1,
//                       min(MAX_PREFIX_LEN, n->partial_len));
//            }
//            //?????????depth?????????????????? ?????????new_leaf??????????????????n???????????????
//            //FIXME????????????????????????????????????node4???????????????????????????????????????????????????????????????????????????inner_node??????
//            //?????????????????????????????????
//            art_node *new_leaf = make_leaf(key,key_len, value, depth+prefix_diff+1);
//            add_child4(new_node, ref, key[depth+prefix_diff], new_leaf);
//            return NULL;
//        }
//    }

//    // If we are at a leaf, we need to replace it with a node
//    if (IS_LEAF(n)) {
//        art_leaf *l = LEAF_RAW(n);
//
//        // Check if we are updating an existing value
//        if (!leaf_matches(l, key, key_len, depth)) {
//            *old = 1;
//            void *old_val = l->value;
//            if(replace) l->value = value;
//            return old_val;
//        }
//
//        // New value, we must split the leaf into a node4
//        art_node4 *new_node = (art_node4*)alloc_node(NODE4);
//
//        // Create a new leaf
//        art_leaf *l2 = make_leaf(key, key_len, value);
//
//        // Determine longest prefix
//        int longest_prefix = longest_common_prefix(l, l2, depth);
//        new_node->n.partial_len = longest_prefix;
//        memcpy(new_node->n.partial, key+depth, min(MAX_PREFIX_LEN, longest_prefix));
//        // Add the leafs to the new node4
//        *ref = (art_node*)new_node;
//        add_child4(new_node, ref, l->key[depth+longest_prefix], SET_LEAF(l));
//        add_child4(new_node, ref, l2->key[depth+longest_prefix], SET_LEAF(l2));
//        return NULL;
//    }
//
//    // Check if given node has a prefix
//    if (n->partial_len) {
//        // Determine if the prefixes differ, since we need to split
//        int prefix_diff = prefix_mismatch(n, key, key_len, depth);
//        if ((uint32_t)prefix_diff >= n->partial_len) {
//            depth += n->partial_len;
//            goto RECURSE_SEARCH;
//        }
//
//        // Create a new node
//        art_node4 *new_node = (art_node4*)alloc_node(NODE4);
//        *ref = (art_node*)new_node;
//        new_node->n.partial_len = prefix_diff;
//        memcpy(new_node->n.partial, n->partial, min(MAX_PREFIX_LEN, prefix_diff));
//
//        // Adjust the prefix of the old node
//        if (n->partial_len <= MAX_PREFIX_LEN) {
//            add_child4(new_node, ref, n->partial[prefix_diff], n);
//            n->partial_len -= (prefix_diff+1);
//            memmove(n->partial, n->partial+prefix_diff+1,
//                    min(MAX_PREFIX_LEN, n->partial_len));
//        } else {
//            n->partial_len -= (prefix_diff+1);
//            art_leaf *l = minimum(n);
//            add_child4(new_node, ref, l->key[depth+prefix_diff], n);
//            memcpy(n->partial, l->key+depth+prefix_diff+1,
//                    min(MAX_PREFIX_LEN, n->partial_len));
//        }
//
//        // Insert the new leaf
//        art_leaf *l = make_leaf(key, key_len, value);
//        add_child4(new_node, ref, key[depth+prefix_diff], SET_LEAF(l));
//        return NULL;
//    }
//
//RECURSE_SEARCH:;
//
//    // Find a child to recurse to
//    art_node **tmp = &n;
//    art_node*** border = &tmp;
//    art_node **child = find_child(n, key[depth], true, border);
//    if (child) {
//        return recursive_insert(*child, child, key, key_len, value, depth+1, old, replace);
//    }
//
//    // No child, node goes within us
//    art_leaf *l = make_leaf(key, key_len, value);
//    add_child(n, ref, key[depth], SET_LEAF(l));
    return NULL;
}

/**
 * inserts a new value into the art tree
 * @arg t the tree
 * @arg key the key
 * @arg key_len the length of the key
 * @arg value opaque value.
 * @return null if the item was newly inserted, otherwise
 * the old value pointer is returned.
 */
void* art_insert(art_tree *t, const unsigned char *key, int key_len, void *value) {
    int old_val = 0;
    void *old = recursive_insert(t->root, &t->root, key, key_len, value, 0, &old_val, 1);
    if (!old_val) t->size++;
    return old;
}

/**
 * inserts a new value into the art tree (no replace)
 * @arg t the tree
 * @arg key the key
 * @arg key_len the length of the key
 * @arg value opaque value.
 * @return null if the item was newly inserted, otherwise
 * the old value pointer is returned.
 */
void* art_insert_no_replace(art_tree *t, const unsigned char *key, int key_len, void *value) {
    int old_val = 0;
    void *old = recursive_insert(t->root, &t->root, key, key_len, value, 0, &old_val, 0);
    if (!old_val) t->size++;
    return old;
}

static void remove_child256(art_node256 *n, art_node **ref, unsigned char c) {
    n->children[c] = NULL;
    n->n.num_children--;

    // Resize to a node48 on underflow, not immediately to prevent
    // trashing if we sit on the 48/49 boundary
    if (n->n.num_children == 37) {
        art_node48 *new_node = (art_node48*)alloc_node(NODE48);
        *ref = (art_node*)new_node;
        copy_header((art_node*)new_node, (art_node*)n);

        int pos = 0;
        for (int i=0;i<256;i++) {
            if (n->children[i]) {
                new_node->children[pos] = n->children[i];
                new_node->keys[i] = pos + 1;
                pos++;
            }
        }
        free(n);
    }
}

static void remove_child48(art_node48 *n, art_node **ref, unsigned char c) {
    int pos = n->keys[c];
    n->keys[c] = 0;
    n->children[pos-1] = NULL;
    n->n.num_children--;

    if (n->n.num_children == 12) {
        art_node16 *new_node = (art_node16*)alloc_node(NODE16);
        *ref = (art_node*)new_node;
        copy_header((art_node*)new_node, (art_node*)n);

        int child = 0;
        for (int i=0;i<256;i++) {
            pos = n->keys[i];
            if (pos) {
                new_node->keys[child] = i;
                new_node->children[child] = n->children[pos - 1];
                child++;
            }
        }
        free(n);
    }
}

static void remove_child16(art_node16 *n, art_node **ref, art_node **l) {
    int pos = l - n->children;
    memmove(n->keys+pos, n->keys+pos+1, n->n.num_children - 1 - pos);
    memmove(n->children+pos, n->children+pos+1, (n->n.num_children - 1 - pos)*sizeof(void*));
    n->n.num_children--;

    if (n->n.num_children == 3) {
        art_node4 *new_node = (art_node4*)alloc_node(NODE4);
        *ref = (art_node*)new_node;
        copy_header((art_node*)new_node, (art_node*)n);
        memcpy(new_node->keys, n->keys, 4);
        memcpy(new_node->children, n->children, 4*sizeof(void*));
        free(n);
    }
}

static void remove_child4(art_node4 *n, art_node **ref, art_node **l) {
    int pos = l - n->children;
    memmove(n->keys+pos, n->keys+pos+1, n->n.num_children - 1 - pos);
    memmove(n->children+pos, n->children+pos+1, (n->n.num_children - 1 - pos)*sizeof(void*));
    n->n.num_children--;

    // Remove nodes with only a single child
    if (n->n.num_children == 1) {
        art_node *child = n->children[0];
        if (!IS_LEAF(child)) {
            // Concatenate the prefixes
            int prefix = n->n.partial_len;
            if (prefix < MAX_PREFIX_LEN) {
                n->n.partial[prefix] = n->keys[0];
                prefix++;
            }
            if (prefix < MAX_PREFIX_LEN) {
                int sub_prefix = min(child->partial_len, MAX_PREFIX_LEN - prefix);
                memcpy(n->n.partial+prefix, child->partial, sub_prefix);
                prefix += sub_prefix;
            }

            // Store the prefix in the child
            memcpy(child->partial, n->n.partial, min(prefix, MAX_PREFIX_LEN));
            child->partial_len += n->n.partial_len + 1;
        }
        *ref = child;
        free(n);
    }
}

static void remove_child(art_node *n, art_node **ref, unsigned char c, art_node **l) {
    switch (n->type) {
        case NODE4:
            return remove_child4((art_node4*)n, ref, l);
        case NODE16:
            return remove_child16((art_node16*)n, ref, l);
        case NODE48:
            return remove_child48((art_node48*)n, ref, c);
        case NODE256:
            return remove_child256((art_node256*)n, ref, c);
        default:
            abort();
    }
}

static art_leaf* recursive_delete(art_node *n, art_node **ref, const unsigned char *key, int key_len, int depth) {
    // Search terminated
    if (!n) return NULL;

    // Handle hitting a leaf node
    if (IS_LEAF(n)) {
        art_leaf *l = LEAF_RAW(n);
        if (!leaf_matches(l, key, key_len, depth)) {
            *ref = NULL;
            return l;
        }
        return NULL;
    }

    // Bail if the prefix does not match
    if (n->partial_len) {
        int prefix_len = check_prefix(n, key, key_len, depth);
        if (prefix_len != min(MAX_PREFIX_LEN, n->partial_len)) {
            return NULL;
        }
        depth = depth + n->partial_len;
    }

    // Find child node
    art_node **tmp = &n;
    art_node ***border = &tmp;
    art_node **child = find_child(n, key[depth], true, border);
    if (!child) return NULL;

    // If the child is leaf, delete from this node
    if (IS_LEAF(*child)) {
        art_leaf *l = LEAF_RAW(*child);
        if (!leaf_matches(l, key, key_len, depth)) {
            remove_child(n, ref, key[depth], child);
            return l;
        }
        return NULL;

    // Recurse
    } else {
        return recursive_delete(*child, child, key, key_len, depth+1);
    }
}

/**
 * Deletes a value from the ART tree
 * @arg t The tree
 * @arg key The key
 * @arg key_len The length of the key
 * @return NULL if the item was not found, otherwise
 * the value pointer is returned.
 */
void* art_delete(art_tree *t, const unsigned char *key, int key_len) {
    art_leaf *l = recursive_delete(t->root, &t->root, key, key_len, 0);
    if (l) {
        t->size--;
        void *old = l->value;
        free(l);
        return old;
    }
    return NULL;
}

// scan the tree
// Recursively iterates over the tree
//???????????????
static int recursive_iter(art_node *n, art_callback cb, void *data) {
    // Handle base cases
    if (!n) return 0;
//    if (IS_LEAF(n)) {
//        art_leaf *l = LEAF_RAW(n);
//        //cb is a restriction, return 0 continue iteration, else stop iteration
//        return cb(data, (const unsigned char*)l->key, l->key_len, l->value);
//    }

    int idx, res;
    switch (n->type) {
        case NODE4:
            for (int i=0; i < n->num_children; i++) {
                res = recursive_iter(((art_node4*)n)->children[i], cb, data);
                if (res) return res;
            }
            break;

        case NODE16:
            for (int i=0; i < n->num_children; i++) {
//                printf("NODE16\n");
                res = recursive_iter(((art_node16*)n)->children[i], cb, data);
                if (res) return res;
            }
            break;

        case NODE48:
            for (int i=0; i < 256; i++) {
                idx = ((art_node48*)n)->keys[i];
                if (!idx) continue;

                res = recursive_iter(((art_node48*)n)->children[idx-1], cb, data);
                if (res) return res;
            }
            break;

        case NODE256:
            for (int i=0; i < 256; i++) {
                if (!((art_node256*)n)->children[i]) continue;
                res = recursive_iter(((art_node256*)n)->children[i], cb, data);
                if (res) return res;
            }
            break;
        //????????????????????????????????????
        case NODE4LEAF:{
            art_node4_leaf *l4 = (art_node4_leaf*)n;
            for(int i=0; i<n->num_children; i++){
                cb(data, (const unsigned char*)l4->children[i].key, l4->children[i].key_len, l4->children[i].value);
            }
            break;}
        case NODE16LEAF:{
            art_node16_leaf  *l16 = (art_node16_leaf*)n;
            for(int i=0; i<n->num_children; i++){
                cb(data, (const unsigned char*)l16->children[i].key, l16->children[i].key_len, l16->children[i].value);
            }
            break;}
        case NODE48LEAF:{
            art_node48_leaf  *l48 = (art_node48_leaf*)n;
            for(int i=0; i<n->num_children; i++){
                if(l48->keys[i]){
                    cb(data, (const unsigned char*)l48->children[l48->keys[i]].key,
                       l48->children[l48->keys[i]].key_len, l48->children[l48->keys[i]].value);
                }
            }
            break;}
        case NODE256LEAF:{
            art_node256_leaf *l256 = (art_node256_leaf*)n;
            for(int i=0; i<n->num_children; i++){
                if(l256->children[i].key_len){
                    cb(data, (const unsigned char*)l256->children[i].key,
                       l256->children[i].key_len, l256->children[i].value);
                }
            }
        }
        default:
            abort();
    }
    return 0;
}

/**
 * Iterates through the entries pairs in the map,
 * invoking a callback for each. The call back gets a
 * key, value for each and returns an integer stop value.
 * If the callback returns non-zero, then the iteration stops.
 * @arg t The tree to iterate over
 * @arg cb The callback function to invoke
 * @arg data Opaque handle passed to the callback
 * @return 0 on success, or the return of the callback.
 */
int art_iter(art_tree *t, art_callback cb, void *data) {
    return recursive_iter(t->root, cb, data);
}

int range_query_leaf_boundary(art_node *n, art_callback cb, void *data,
                              int depth, const unsigned char *key,
                              int key_len, bool direction){
    union {
        art_node4_leaf *p1;
        art_node16_leaf *p2;
        art_node48_leaf *p3;
        art_node256_leaf *p4;
    } p;
    int *border;
    int pos = find_child_leaf(n, key[depth], direction, border);
    int left, right;
    if(direction){
        left = pos;
        right = *border;
    } else {
        left = border;
        right = pos;
    }
    switch (n->type) {
        case NODE4LEAF:
            p.p1 = (art_node4_leaf*)n;
            while(left <= right){
                cb(data, p.p1->children[left].key, p.p1->children[left].key_len, p.p1->children[left].value);
                left++;
            }
            break;
        case NODE16LEAF:
            p.p2 = (art_node16_leaf*)n;
            while(left <= right){
                cb(data, p.p2->children[left].key, p.p2->children[left].key_len, p.p3->children[left].value);
                left++;
            }
            break;
        case NODE48LEAF:
            p.p3 = (art_node48_leaf*)n;
            while(left <= right){
                cb(data, p.p3->children[left].key, p.p3->children[left].key_len, p.p3->children[left].value);
                left++;
            }
            break;
        case NODE256LEAF:
            p.p4 = (art_node256_leaf*)n;
            while(left <= right){
                if(p.p4->children[left].key_len){
                    cb(data, p.p4->children[left].key, p.p4->children[left].key_len, p.p4->children[left].value);
                }
                left++;
            }
            break;
        default:
            abort();
    }
    return 0;
}

int range_query_boundary(art_node *n, art_callback cb,
                              void *data, int depth,
                              const unsigned char *key, int key_len,
                              bool direction){
    art_node **child;
    int prefix_len;
    art_node **tmp = &n;
    art_node ***border = &tmp;
    //leaf node, return directly
//    if(IS_LEAF(n)){
//        art_leaf *l = (art_leaf*)LEAF_RAW(n);
//        return cb(data, (const unsigned char*)l->key, l->key_len, l->value);
//    }
    if (n->partial_len) {
        prefix_len = check_prefix_direction(n, key, key_len, depth);
        if(prefix_len == 0 && direction){
            printf("prefix unequal\n");
            return recursive_iter(n, cb, data);
        }else if(prefix_len == -2 && !direction){
            printf("prefix unequal\n");
            return recursive_iter(n, cb, data);
        }else if(prefix_len == min(MAX_PREFIX_LEN, n->partial_len)){
            depth += n->partial_len;
        }else
            return 1;
    }

    if(n->type > 4){
        range_query_leaf_boundary(n, cb, data, depth, key, key_len, direction);
        return 0;
    }

    //for the function of find_child, if cant find, it will return NULL
    if(direction){
        child = find_child(n, key[depth], direction, border);
        if(child){
            printf("left close\n");
            art_node **itr = child+1;
            while(itr <= *border){
                recursive_iter(*itr, cb, data);
                itr++;
            }
            depth++;
            printf("left boundary level %d\n", depth);
            range_query_boundary(*child, cb, data, depth, key, key_len, direction);
        }else{
            printf("left open\n");
            child = find_child_boundary(n, key[depth], direction, border);
            art_node **itr = child;
            while(itr <= *border){
                recursive_iter(*itr, cb, data);
                itr++;
            }
        }
        return 0;
    }else{
        printf("right closed\n");
        child = find_child(n, key[depth], direction, border);
//        printf("child %p, *border %p, n %p\n",child, *border, &n);
        if(child){
            art_node **itr = child - 1;
            while(itr >= *border){
                recursive_iter(*itr, cb, data);
                itr--;
            }
            depth++;
            printf("boundary level %d\n", depth);
            range_query_boundary(*child, cb, data, depth, key, key_len, direction);
        }else{
            printf("right open\n");
            child = find_child_boundary(n, key[depth], direction, border);
            art_node **itr = child;
            while(itr >= *border){
                recursive_iter(*itr, cb, data);
                itr --;
            }
        }
        return 0;
    }
    return 1;
}

int range_query_leaf(art_node* n, art_callback cb, void *data, int depth,
                     const unsigned char *low, int low_len,
                     const unsigned char *high, int high_len){
    //use find_child_leaf and get_leaf to get art_leaf
    union {
        art_node4_leaf *p1;
        art_node16_leaf *p2;
        art_node48_leaf *p3;
        art_node256_leaf *p4;
    } p;
    int *border1, *border2;
    int left = find_child_leaf(n, low[depth], true, border1);
    int right = find_child_leaf(n, high[depth], false, border2);
    switch (n->type) {
        case NODE4LEAF:
            p.p1 = (art_node4_leaf*)n;
            while(left <= right){
                cb(data, p.p1->children[left].key,
                   p.p1->children[left].key_len,
                   p.p1->children[left].value);
                left++;
            }
            break;
        case NODE16LEAF:
            p.p2 = (art_node16_leaf*)n;
            while(left <= right){
                cb(data, p.p2->children[left].key,
                   p.p2->children[left].key_len,
                   p.p2->children[left].value);
                left++;
            }
            break;
        case NODE48LEAF:
            p.p3 = (art_node48_leaf*)n;
            while(left <= right){
                cb(data, p.p3->children[left].key,
                   p.p3->children[left].key_len,
                   p.p3->children[left].value);
                left++;
            }
            break;
        case NODE256LEAF:
            p.p4 = (art_node256_leaf*)n;
            while(left <= right){
                if(p.p4->children[left].key_len){
                    cb(data, p.p4->children[left].key,
                       p.p4->children[left].key_len,
                       p.p4->children[left].value);
                }
                left++;
            }
            break;
    }
}



/**
 * Range Query
 * @arg t  The tree to query
 * @arg cb
 * @arg data
 * @arg low The query lower limit
 * @arg high The query higher limit
 */
int range_query(art_node *n, art_callback cb,
                void *data, int depth,
                const unsigned char *low, int low_len,
                const unsigned char *high, int high_len){
    art_node **child1, **child2;
    art_node **tmp = &n;
    art_node ***border = &tmp;
    int prefix_len;
    if (n->partial_len) {
        prefix_len = check_prefix(n, low, low_len, depth);
        if (prefix_len != min(MAX_PREFIX_LEN, n->partial_len))
            return 1;
        depth = depth + n->partial_len;
    }
    if(n->type > 4){
        range_query_leaf(n, cb, data, depth, low, low_len, high, high_len);
        return 0;
    }
    child1 = find_child(n, low[depth], true, border);
    child2 = find_child(n, high[depth], true, border);
//    recursive_iter(*child1, cb, data);
    if(child1 && child2){
        printf("left closed and right closed\n");
        if(child1 == child2){
            return range_query(*child1, cb, data, ++depth, low, low_len, high, high_len);
        }else{
            art_node **itr = child1 + 1;
            printf("start iterate\n");
            while(itr && itr < child2){
                recursive_iter(*itr, cb, data);
                itr++;
            }
            depth++;
            range_query_boundary(*child1, cb, data, depth, low, low_len, true);
            range_query_boundary(*child2, cb, data, depth, high, high_len, false);
            return 0;
        }
    }else if(child1 && !child2){
        printf("left closed and right open\n");
        art_node **child_n_eq2 = find_child_boundary(n, high[depth], false, border);
        art_node **itr = child1 + 1;
        while(itr <= child_n_eq2){
            recursive_iter(*itr, cb, data);
            itr++;
        }
        range_query_boundary(*child1, cb, data, ++depth, low, low_len, true);
        return 0;
    }else if(!child1  && child2){
        printf("left open and right closed\n");
        art_node **child_n_eq1 = find_child_boundary(n, low[depth], true, border);
        art_node **itr = child_n_eq1;
        while(itr < child2){
            recursive_iter(*itr, cb, data);
            itr++;
        }
        range_query_boundary(*child2, cb, data, ++depth, high, high_len, false);
        return 0;
    }else{
        printf("left closed and right closed\n");
        art_node ***border1 = &tmp;
        art_node ***border2 = &tmp;
        art_node **child_n_eq1 = find_child_boundary(n, low[depth], true, border1);
        art_node **child_n_eq2 = find_child_boundary(n, high[depth], false, border2);
        art_node **itr = child_n_eq1;
        while(itr <= child_n_eq2){
            recursive_iter(*itr, cb, data);
            itr++;
        }
        return 0;
    }
    return 1;
}



/**
 * Checks if a leaf prefix matches
 * @return 0 on success.
 */
static int leaf_prefix_matches(const art_leaf *n, const unsigned char *prefix, int prefix_len) {
    // Fail if the key length is too short
    if (n->key_len < (uint32_t)prefix_len) return 1;

    // Compare the keys
    return memcmp(n->key, prefix, prefix_len);
}

/**
 * Iterates through the entries pairs in the map,
 * invoking a callback for each that matches a given prefix.
 * The call back gets a key, value for each and returns an integer stop value.
 * If the callback returns non-zero, then the iteration stops.
 * @arg t The tree to iterate over
 * @arg prefix The prefix of keys to read
 * @arg prefix_len The length of the prefix
 * @arg cb The callback function to invoke
 * @arg data Opaque handle passed to the callback
 * @return 0 on success, or the return of the callback.
 */
int art_iter_prefix(art_tree *t, const unsigned char *key, int key_len, art_callback cb, void *data) {
    art_node **child;
    art_node *n = t->root;
    art_node **tmp = &n;
    art_node ***border = &tmp;
    int prefix_len, depth = 0;
    while (n) {
        // Might be a leaf
        if (IS_LEAF(n)) {
            n = (art_node*)LEAF_RAW(n);
            // Check if the expanded path matches
            if (!leaf_prefix_matches((art_leaf*)n, key, key_len)) {
                art_leaf *l = (art_leaf*)n;
                return cb(data, (const unsigned char*)l->key, l->key_len, l->value);
            }
            return 0;
        }

        // If the depth matches the prefix, we need to handle this node
        if (depth == key_len) {
            art_leaf *l = minimum(n);
            if (!leaf_prefix_matches(l, key, key_len))
               return recursive_iter(n, cb, data);
            return 0;
        }

        // Bail if the prefix does not match
        if (n->partial_len) {
            prefix_len = prefix_mismatch(n, key, key_len, depth);

            // Guard if the mis-match is longer than the MAX_PREFIX_LEN
            if ((uint32_t)prefix_len > n->partial_len) {
                prefix_len = n->partial_len;
            }

            // If there is no match, search is terminated
            if (!prefix_len) {
                return 0;

            // If we've matched the prefix, iterate on this node
            } else if (depth + prefix_len == key_len) {
                return recursive_iter(n, cb, data);
            }

            // if there is a full match, go deeper
            depth = depth + n->partial_len;
        }

        // Recursively search
        child = find_child(n, key[depth], true, border);
        n = (child) ? *child : NULL;
        depth++;
    }
    return 0;
}
