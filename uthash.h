/*
Copyright (c) 2003-2022, Troy D. Hanson     http://troydhanson.github.com/uthash/
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* a note on threading
 * The uthash macros are not thread-safe. If you are using uthash in a
 * multi-threaded program, you must use a mutex around any calls to
 * HASH_ADD, HASH_REPLACE, HASH_DELETE, HASH_FIND, HASH_SORT, HASH_CLEAR,
 * HASH_ITER, HASH_SELECT, or HASH_SRT.
 * (Basically, any macro that modifies the hash may need to be protected,
 * as well as any that would be affected by modifications).
 * The HASH_COUNT macro is thread-safe, as long as the hash is only being
 * read (e.g. no HASH_ADD, etc calls are occurring at the same time).
 */
#ifndef UTHASH_H
#define UTHASH_H

#define UTHASH_VERSION 2.3.0

#include <string.h>   /* memcmp, strlen */
#include <stddef.h>   /* ptrdiff_t */
#include <stdlib.h>   /* exit */
#include <stdint.h>

/* define uthash_fatal to customize error handling */
#ifndef uthash_fatal
#define uthash_fatal(msg) exit(-1)        /* fatal error (out of memory,etc) */
#endif

/* define uthash_malloc to override malloc */
#ifndef uthash_malloc
#define uthash_malloc(sz) malloc(sz)      /* malloc fcn                      */
#endif

/* define uthash_free to override free */
#ifndef uthash_free
#define uthash_free(ptr,sz) free(ptr)     /* free fcn                        */
#endif

/* define uthash_strlen to override strlen */
#ifndef uthash_strlen
#define uthash_strlen(s) strlen(s)
#endif

/* define uthash_memcmp to override memcmp */
#ifndef uthash_memcmp
#define uthash_memcmp(a,b,n) memcmp(a,b,n)
#endif

/* define uthash_nonfatal_oom to indicate that OOM is not a fatal error */
#ifdef uthash_nonfatal_oom
#define uthash_nonfatal_oom_p(p) p
#else
#define uthash_nonfatal_oom_p(p) 1
#endif

#ifndef uthash_noexpand_fyi
#define uthash_noexpand_fyi(tbl)
#endif
#ifndef uthash_expand_fyi
#define uthash_expand_fyi(tbl)
#endif

/* initial number of buckets */
#define HASH_INITIAL_NUM_BUCKETS 32U     /* initial number of buckets        */
#define HASH_INITIAL_NUM_BUCKETS_LOG2 5U /* log2 of initial num buckets      */
#define HASH_BKT_CAPACITY_THRESH 10U     /* expand when bucket count reaches */

/* calculate the element whose hash handle address is hhp */
#define ELMT_FROM_HH(tbl,hhp) ((void*)(((char*)(hhp)) - ((tbl)->hho)))

/* get the key from the element */
#define KEY_PTR(hh, elmt) ((hh)->keylen ? (void*)((char*)(elmt) + (hh)->key_off) : *(void**)((char*)(elmt) + (hh)->key_off))

#define HASH_TO_BKT(hashv,num_bkts) ((hashv) & ((num_bkts) - 1U))

#define HASH_MAKE_TABLE(hh,head)                                               \
do {                                                                           \
  (head)->hh.tbl = (UT_hash_table*)uthash_malloc(sizeof(UT_hash_table));        \
  if (!uthash_nonfatal_oom_p((head)->hh.tbl)) {                                 \
      uthash_fatal("out of memory");                                           \
  }                                                                            \
  memset((head)->hh.tbl, 0, sizeof(UT_hash_table));                            \
  (head)->hh.tbl->tail = &((head)->hh);                                        \
  (head)->hh.tbl->num_buckets = HASH_INITIAL_NUM_BUCKETS;                      \
  (head)->hh.tbl->log2_num_buckets = HASH_INITIAL_NUM_BUCKETS_LOG2;            \
  (head)->hh.tbl->hho = (char*)(&(head)->hh) - (char*)(head);                   \
  (head)->hh.tbl->buckets = (UT_hash_bucket*)uthash_malloc(                     \
      HASH_INITIAL_NUM_BUCKETS*sizeof(struct UT_hash_bucket));                 \
  if (!uthash_nonfatal_oom_p((head)->hh.tbl->buckets)) {                        \
      uthash_free((head)->hh.tbl, sizeof(UT_hash_table));                      \
      uthash_fatal("out of memory");                                           \
  }                                                                            \
  memset((head)->hh.tbl->buckets, 0,                                           \
      HASH_INITIAL_NUM_BUCKETS*sizeof(struct UT_hash_bucket));                 \
} while(0)

#define HASH_ADD(hh,head,fieldname,keylen_in,add)                              \
        HASH_ADD_KEYPTR(hh,head,add ? ((void*)(((char*)(add)) + ((head)->hh.tbl->hho))) : NULL, &((add)->fieldname),keylen_in,add)

#define HASH_REPLACE(hh,head,fieldname,keylen_in,add,replaced)                 \
do {                                                                           \
  (replaced) = NULL;                                                           \
  HASH_FIND(hh,head,add ? &((add)->fieldname) : NULL,keylen_in,replaced);      \
  if (replaced) {                                                              \
    HASH_DELETE(hh,head,replaced);                                             \
  }                                                                            \
  HASH_ADD(hh,head,fieldname,keylen_in,add);                                   \
} while(0)

#define HASH_ADD_KEYPTR(hh,head,key_ptr,key,keylen_in,add)                       \
do {                                                                           \
  unsigned _ha_bkt;                                                            \
  (add)->hh.next = NULL;                                                       \
  (add)->hh.key = (void*)(key);                                                \
  (add)->hh.keylen = (unsigned)(keylen_in);                                    \
  if (!(head)) {                                                               \
    (head) = (add);                                                            \
    (head)->hh.prev = NULL;                                                    \
    HASH_MAKE_TABLE(hh,head);                                                  \
  } else {                                                                     \
    (head)->hh.tbl->tail->next = (add);                                        \
    (add)->hh.prev = ELMT_FROM_HH((head)->hh.tbl, (head)->hh.tbl->tail);        \
    (head)->hh.tbl->tail = &((add)->hh);                                       \
  }                                                                            \
  (head)->hh.tbl->num_items++;                                                 \
  (add)->hh.tbl = (head)->hh.tbl;                                              \
  (add)->hh.hashv = HASH_FCN(key_ptr, key, keylen_in, (head)->hh.tbl->hash_fcn_seed); \
  _ha_bkt = HASH_TO_BKT((add)->hh.hashv, (head)->hh.tbl->num_buckets);          \
  HASH_ADD_TO_BKT((head)->hh.tbl->buckets[_ha_bkt],&(add)->hh);                 \
  HASH_BLOOM_ADD((head)->hh.tbl,&(add)->hh);                                   \
  HASH_MAYBE_EXPAND(hh,head);                                                  \
} while(0)

#define HASH_ADD_INT(head,fieldname,add)                                       \
    HASH_ADD(hh,head,fieldname,sizeof(int),add)
#define HASH_ADD_PTR(head,fieldname,add)                                       \
    HASH_ADD(hh,head,fieldname,sizeof(void*),add)
#define HASH_ADD_STR(head,fieldname,add)                                       \
    HASH_ADD(hh,head,fieldname,uthash_strlen((add)->fieldname),add)

#define HASH_REPLACE_INT(head,fieldname,add,replaced)                          \
    HASH_REPLACE(hh,head,fieldname,sizeof(int),add,replaced)
#define HASH_REPLACE_PTR(head,fieldname,add,replaced)                          \
    HASH_REPLACE(hh,head,fieldname,sizeof(void*),add,replaced)
#define HASH_REPLACE_STR(head,fieldname,add,replaced)                          \
    HASH_REPLACE(hh,head,fieldname,uthash_strlen((add)->fieldname),add,replaced)

#define HASH_FIND(hh,head,keyptr,keylen_in,out)                                \
do {                                                                           \
  (out) = NULL;                                                                \
  if (head) {                                                                  \
    unsigned _hf_bkt;                                                          \
    unsigned _hf_hashv;                                                        \
    _hf_hashv = HASH_FCN(keyptr,keyptr,keylen_in,(head)->hh.tbl->hash_fcn_seed);\
    _hf_bkt = HASH_TO_BKT(_hf_hashv, (head)->hh.tbl->num_buckets);              \
    if (HASH_BLOOM_TEST((head)->hh.tbl,_hf_hashv)) {                            \
      HASH_FIND_IN_BKT((head)->hh.tbl, hh, (head)->hh.tbl->buckets[_hf_bkt],    \
                       keyptr,keylen_in,out);                                  \
    }                                                                          \
  }                                                                            \
} while(0)

#define HASH_FIND_INT(head,findint,out)                                        \
    HASH_FIND(hh,head,findint,sizeof(int),out)
#define HASH_FIND_PTR(head,findptr,out)                                        \
    HASH_FIND(hh,head,findptr,sizeof(void*),out)
#define HASH_FIND_STR(head,findstr,out)                                        \
    HASH_FIND(hh,head,findstr,uthash_strlen(findstr),out)

#define HASH_DELETE(hh,head,delptr)                                            \
    HASH_DELETE_HH(hh,head,&(delptr)->hh)

#define HASH_DELETE_HH(hh,head,del_hh)                                         \
do {                                                                           \
  struct UT_hash_handle *_hd_hh_p = (del_hh);                                  \
  if (_hd_hh_p->prev) {                                                        \
    ((UT_hash_handle*)_hd_hh_p->prev->hh.next)->prev = _hd_hh_p->prev;          \
  } else {                                                                     \
    (head) = _hd_hh_p->next ? ELMT_FROM_HH((head)->hh.tbl, _hd_hh_p->next) : NULL; \
  }                                                                            \
  if (_hd_hh_p->next) {                                                        \
    ((UT_hash_handle*)_hd_hh_p->next->hh.prev)->next = _hd_hh_p->next;          \
  } else {                                                                     \
    (head)->hh.tbl->tail = _hd_hh_p->prev ? &(_hd_hh_p->prev->hh) : NULL;       \
  }                                                                            \
  (head)->hh.tbl->num_items--;                                                 \
  HASH_DEL_IN_BKT(hh,_hd_hh_p);                                                \
  HASH_BLOOM_DEL((head)->hh.tbl,_hd_hh_p);                                     \
} while (0)


#define HASH_CLEAR(hh,head)                                                    \
do {                                                                           \
  if (head) {                                                                  \
    uthash_free((head)->hh.tbl->buckets,                                       \
                (head)->hh.tbl->num_buckets*sizeof(struct UT_hash_bucket));    \
    HASH_BLOOM_FREE((head)->hh.tbl);                                           \
    uthash_free((head)->hh.tbl, sizeof(UT_hash_table));                         \
    (head)=NULL;                                                               \
  }                                                                            \
} while(0)

#define HASH_COUNT(head) HASH_CNT(hh,head)
#define HASH_CNT(hh,head) ((head)?((head)->hh.tbl->num_items):0)

#define HASH_ITER(hh,head,el,tmp)                                              \
  for(((el)=(head)), ((tmp)=((head)?(ELMT_FROM_HH((head)->hh.tbl,(head)->hh.next)):NULL)); \
      (el); ((el)=(tmp)), ((tmp)=((tmp)?(ELMT_FROM_HH((head)->hh.tbl,(tmp)->hh.next)):NULL)))

#define HASH_BACKWARD(hh,head,el,tmp)                                          \
  for(((el)=ELMT_FROM_HH((head)->hh.tbl, (head)->hh.tbl->tail)),                \
      ((tmp)=(el)?ELMT_FROM_HH((head)->hh.tbl, (el)->hh.prev):NULL));           \
      (el); ((el)=(tmp)), ((tmp)=(tmp)?ELMT_FROM_HH((head)->hh.tbl, (tmp)->hh.prev):NULL))

#define HASH_SORT(head,cmpfcn) HASH_SRT(hh,head,cmpfcn)
#define HASH_SRT(hh,head,cmpfcn)                                               \
do {                                                                           \
  struct UT_hash_handle *_hs_p;                                                \
  (head) = HASH_SORT_IN_PLACE(head,cmpfcn);                                    \
  _hs_p = (head)->hh.tbl->tail;                                                \
  while (_hs_p) {                                                              \
    _hs_p->next = (_hs_p->prev) ? &(_hs_p->prev->hh) : NULL;                    \
    _hs_p = _hs_p->prev ? &(_hs_p->prev->hh) : NULL;                            \
  }                                                                            \
} while (0)

/* This is an adaptation of Simon Tatham's O(n log n) mergesort */
/* Note that HASH_SORT uses ->prev links but HASH_SORT_IN_PLACE uses ->next */
#define HASH_SORT_IN_PLACE(head,cmpfcn)                                        \
(head) ? merge_sort((head),cmpfcn) : NULL
#define merge_sort(list, cmpfcn)                                               \
(
  (list) && (list)->hh.next ?                                                  \
    (                                                                          \
      merge_sort_core(                                                         \
        (list),                                                                \
        cmpfcn,                                                                \
        find_mid(list)                                                         \
      )                                                                        \
    ) : (list)                                                                 \
)

#define find_mid(list)                                                         \
(
  (list)->hh.next ?                                                            \
    (                                                                          \
      find_mid_core((list)->hh.next, list)                                     \
    ) : (list)                                                                 \
)

#define find_mid_core(head, tail)                                              \
(
  (head) == (tail) ? (tail) :                                                  \
  (head)->hh.next == (tail) ? (tail) :                                         \
  find_mid_core((head)->hh.next, (tail)->hh.prev)                              \
)

#define merge_sort_core(uthash_list, cmpfcn, mid)                              \
(
  (mid)->hh.next = NULL,                                                       \
  merge_lists(                                                                 \
    merge_sort((uthash_list), cmpfcn),                                         \
    merge_sort((mid), cmpfcn),                                                 \
    cmpfcn                                                                     \
  )
)

#define merge_lists(l1, l2, cmpfcn)                                            \
(
  (l1) ?                                                                       \
  (                                                                            \
    (l2) ?                                                                     \
    (                                                                          \
      (cmpfcn(l1, l2) <= 0) ?                                                  \
        merge_lists_core((l1), (l2), cmpfcn) :                                 \
        merge_lists_core((l2), (l1), cmpfcn)                                   \
    ) : (l1)                                                                   \
  ) : (l2)                                                                     \
)

#define merge_lists_core(head, next, cmpfcn)                                   \
(
  (head)->hh.next = merge_lists((head)->hh.next, (next), cmpfcn),               \
  (head)                                                                       \
)

#define HASH_SELECT(hh,head,fieldname,keylen_in,keyptr,out)                    \
do {                                                                           \
  (out) = NULL;                                                                \
  if (head) {                                                                  \
    unsigned _hs_bkt;                                                          \
    unsigned _hs_hashv;                                                        \
    _hs_hashv = HASH_FCN(keyptr,keyptr,keylen_in,(head)->hh.tbl->hash_fcn_seed);\
    _hs_bkt = HASH_TO_BKT(_hs_hashv, (head)->hh.tbl->num_buckets);              \
    if (HASH_BLOOM_TEST((head)->hh.tbl,_hs_hashv)) {                            \
      HASH_FIND_IN_BKT((head)->hh.tbl, hh, (head)->hh.tbl->buckets[_hs_bkt],    \
                       keyptr,keylen_in,out);                                  \
    }                                                                          \
  }                                                                            \
} while(0)

#define HASH_SELECT_INT(head,fieldname,findint,out)                            \
    HASH_FIND(hh,head,findint,sizeof(int),out)
#define HASH_SELECT_PTR(head,fieldname,findptr,out)                            \
    HASH_FIND(hh,head,findptr,sizeof(void*),out)
#define HASH_SELECT_STR(head,fieldname,findstr,out)                            \
    HASH_FIND(hh,head,findstr,uthash_strlen(findstr),out)

/* optional hash function */
#define HASH_FUNCTION(key,keylen,hashv)                                        \
do {                                                                           \
  (hashv) = HASH_JEN(key,keylen);                                              \
} while (0)

/* optional hash function, seeded */
#define HASH_FUNCTION_SEED(key,keylen,seed,hashv)                              \
do {                                                                           \
  (hashv) = HASH_JEN_SEED(key,keylen,seed);                                    \
} while (0)

/* The hash function is chosen using HASH_FUNCTION. HASH_JEN is the default.
 * Other hash functions (like HASH_SAX, HASH_SFH) are available,
 * just change the #define of HASH_FCN.
 */
#define HASH_FCN HASH_FUNCTION

/* The salt is a random value that is applied to the hash function to prevent
 * collision attacks.
 * This is not defined by default as it is not required for most applications.
 * To use it, uncomment the following line.
 * To use a specific value, you can define it in your build environment.
 */
/* #define HASH_SALT 0xabcde01234567890ULL */

#ifdef HASH_SALT
#define HASH_FCN_SEED(key,keylen,hash_fcn_seed,hashv)                           \
do {                                                                            \
  (hashv) = HASH_JEN_SEED(key,keylen,hash_fcn_seed);                            \
} while (0)
#else
#define HASH_FCN_SEED(key,keylen,hash_fcn_seed,hashv)                           \
do {                                                                            \
  (hashv) = HASH_JEN(key,keylen);                                               \
} while (0)
#endif

/* The seed is defined as a random number at compile time.
 * This is not defined by default as it is not required for most applications.
 * To use it, uncomment the following line.
 */
/* #define HASH_RANDOM_SEED time(NULL) */

#ifdef HASH_RANDOM_SEED
#define HASH_FCN_SEED_INIT(tbl)                                                 \
do {                                                                            \
  (tbl)->hash_fcn_seed = HASH_RANDOM_SEED;                                      \
} while (0)
#else
#define HASH_FCN_SEED_INIT(tbl)
#endif

#define HASH_JEN_MIX(a,b,c)                                                    \
do {                                                                           \
  a -= b; a -= c; a ^= ( c >> 13 );                                            \
  b -= c; b -= a; b ^= ( a << 8 );                                             \
  c -= a; c -= b; c ^= ( b >> 13 );                                            \
  a -= b; a -= c; a ^= ( c >> 12 );                                            \
  b -= c; b -= a; b ^= ( a << 16 );                                            \
  c -= a; c -= b; c ^= ( b >> 5 );                                             \
  a -= b; a -= c; a ^= ( c >> 3 );                                             \
  b -= c; b -= a; b ^= ( a << 10 );                                            \
  c -= a; c -= b; c ^= ( b >> 15 );                                            \
} while (0)

#define HASH_JEN(key,keylen)                                                   \
( HASH_JEN_SEED(key,keylen,0U) )

#define HASH_JEN_SEED(key,keylen,hashv)                                        \
( HASH_JEN_SEED_C(key,keylen,hashv) )

#define HASH_JEN_SEED_C(key,keylen,hashv_in)                                   \
(
  (hashv_in) ?                                                                 \
  (                                                                            \
    HASH_JEN_SEED_C_CORE(key,keylen,hashv_in)                                  \
  ) : (                                                                        \
    HASH_JEN_SEED_C_CORE(key,keylen, (uint32_t)HASH_SALT)                       \
  )                                                                            \
)

#define HASH_JEN_SEED_C_CORE(key,keylen,hashv)                                 \
(
  (hashv) = 0,                                                                 \
  (hashv) += (uint32_t)(keylen),                                               \
  HASH_JEN_MIX(                                                                \
    ((uint32_t*)((void*)key))[0],                                              \
    ((uint32_t*)((void*)key))[1],                                              \
    (hashv)                                                                    \
  ),                                                                           \
  (hashv)                                                                      \
)

/* The HASH_SAX hash function is not as good as HASH_JEN, but it is faster.
 * It is not used by default.
 */
#define HASH_SAX(key,keylen)                                                   \
( HASH_SAX_SEED(key,keylen,0U) )

#define HASH_SAX_SEED(key,keylen,hashv)                                        \
( HASH_SAX_SEED_C(key,keylen,hashv) )

#define HASH_SAX_SEED_C(key,keylen,hashv_in)                                   \
(
  (hashv_in) ?                                                                 \
  (                                                                            \
    HASH_SAX_SEED_C_CORE(key,keylen,hashv_in)                                  \
  ) : (                                                                        \
    HASH_SAX_SEED_C_CORE(key,keylen, (uint32_t)HASH_SALT)                       \
  )                                                                            \
)

#define HASH_SAX_SEED_C_CORE(key,keylen,hashv)                                 \
(
  (hashv) = 0,                                                                 \
  (hashv) ^= (hashv) << 5, (hashv) ^= (hashv) >> 2,                            \
  (hashv) += (unsigned char)(key)[0],                                          \
  (hashv) ^= (hashv) << 5, (hashv) ^= (hashv) >> 2,                            \
  (hashv)                                                                      \
)

/* The HASH_SFH hash function is not as good as HASH_JEN, but it is faster.
 * It is not used by default.
 */
#define HASH_SFH(key,keylen)                                                   \
( HASH_SFH_SEED(key,keylen,0U) )

#define HASH_SFH_SEED(key,keylen,hashv)                                        \
( HASH_SFH_SEED_C(key,keylen,hashv) )

#define HASH_SFH_SEED_C(key,keylen,hashv_in)                                   \
(
  (hashv_in) ?                                                                 \
  (                                                                            \
    HASH_SFH_SEED_C_CORE(key,keylen,hashv_in)                                  \
  ) : (                                                                        \
    HASH_SFH_SEED_C_CORE(key,keylen, (uint32_t)HASH_SALT)                       \
  )                                                                            \
)

#define HASH_SFH_SEED_C_CORE(key,keylen,hashv)                                 \
(
  (hashv) = 0,                                                                 \
  (hashv) = ((hashv) << 16) ^ ((hashv) >> 16) ^ (unsigned char)(key)[0],        \
  (hashv)                                                                      \
)

#define HASH_BLOOM_BYTELEN (1U << HASH_BLOOM)
#define HASH_BLOOM_BITLEN (8U * HASH_BLOOM_BYTELEN)

#ifdef HASH_BLOOM
#define HASH_BLOOM_ADD(tbl,hh)                                                 \
do {                                                                           \
  (tbl)->bloom_nbits++;                                                        \
  (tbl)->bloom_bv[ (hh)->hashv & (HASH_BLOOM_BITLEN-1) ] = 1;                   \
} while (0)

#define HASH_BLOOM_TEST(tbl,hashv)                                             \
  ((tbl)->bloom_bv[ (hashv) & (HASH_BLOOM_BITLEN-1) ])

#define HASH_BLOOM_DEL(tbl,hh)                                                 \
do {                                                                           \
  (tbl)->bloom_nbits--;                                                        \
} while (0)

#define HASH_BLOOM_FREE(tbl)                                                   \
do {                                                                           \
  uthash_free((tbl)->bloom_bv, HASH_BLOOM_BYTELEN);                             \
} while (0)

#define HASH_BLOOM_MAKE(tbl)                                                   \
do {                                                                           \
  (tbl)->bloom_bv = (char*)uthash_malloc(HASH_BLOOM_BYTELEN);                   \
  if (!uthash_nonfatal_oom_p((tbl)->bloom_bv)) {                                \
     uthash_fatal("out of memory");                                            \
  }
  memset((tbl)->bloom_bv, 0, HASH_BLOOM_BYTELEN);                              \
  (tbl)->bloom_nbits = 0;                                                      \
} while (0)

#define HASH_BLOOM_INIT(tbl)                                                   \
do {                                                                           \
  if ((tbl)->num_buckets > HASH_BLOOM_BITLEN) {                                \
    HASH_BLOOM_MAKE(tbl);                                                      \
  } else {                                                                     \
    (tbl)->bloom_bv = NULL;                                                    \
  }                                                                            \
} while (0)
#else
#define HASH_BLOOM_ADD(tbl,hh)
#define HASH_BLOOM_TEST(tbl,hashv) (1)
#define HASH_BLOOM_DEL(tbl,hh)
#define HASH_BLOOM_FREE(tbl)
#define HASH_BLOOM_MAKE(tbl)
#define HASH_BLOOM_INIT(tbl)
#endif

#define HASH_MAKE_BUCKETS(tbl,new_num_bkts)                                    \
do {                                                                           \
  (tbl)->buckets = (UT_hash_bucket*)uthash_malloc(                             \
      (new_num_bkts)*sizeof(struct UT_hash_bucket));                           \
  if (!uthash_nonfatal_oom_p((tbl)->buckets)) {                                 \
     uthash_fatal("out of memory");                                            \
  }                                                                            \
  memset((tbl)->buckets, 0, (new_num_bkts)*sizeof(struct UT_hash_bucket));     \
} while (0)

#define HASH_EXPAND_BUCKETS(tbl,new_num_bkts)                                  \
do {                                                                           \
  UT_hash_bucket *_he_new_buckets;                                             \
  unsigned _he_bkt;                                                            \
  struct UT_hash_handle *_he_thh, *_he_hh;                                     \
  _he_new_buckets = (UT_hash_bucket*)uthash_malloc(                            \
      (new_num_bkts)*sizeof(struct UT_hash_bucket));                           \
  if (!uthash_nonfatal_oom_p(_he_new_buckets)) {                                \
      uthash_fatal("out of memory");                                           \
  }                                                                            \
  memset(_he_new_buckets, 0, (new_num_bkts)*sizeof(struct UT_hash_bucket));    \
  for(_he_bkt=0; _he_bkt < (tbl)->num_buckets; _he_bkt++) {                     \
    _he_thh = (tbl)->buckets[_he_bkt].hh_head;                                  \
    while (_he_thh) {                                                          \
      _he_hh = _he_thh;                                                        \
      _he_thh = _he_thh->hh_next;                                              \
      HASH_ADD_TO_BKT(                                                         \
        _he_new_buckets[HASH_TO_BKT(_he_hh->hashv, new_num_bkts)],_he_hh);      \
    }                                                                          \
  }                                                                            \
  uthash_free((tbl)->buckets, (tbl)->num_buckets*sizeof(struct UT_hash_bucket));\
  (tbl)->num_buckets = new_num_bkts;                                           \
  (tbl)->log2_num_buckets++;                                                   \
  (tbl)->buckets = _he_new_buckets;                                            \
  (tbl)->ineff_expands=0;                                                      \
} while (0)

#define HASH_MAYBE_EXPAND(hh,head)                                             \
do {                                                                           \
  if ((head)->hh.tbl->num_items > ((head)->hh.tbl->num_buckets*HASH_BKT_CAPACITY_THRESH)) { \
    uthash_expand_fyi((head)->hh.tbl);                                         \
    HASH_EXPAND_BUCKETS((head)->hh.tbl, (head)->hh.tbl->num_buckets*2);         \
  }                                                                            \
} while (0)

#define HASH_ADD_TO_BKT(head,add)                                              \
do {                                                                           \
  (add)->hh_next = (head).hh_head;                                             \
  if ((head).hh_head) (head).hh_head->hh_prev = (add);                          \
  (head).hh_head = (add);                                                      \
  (add)->hh_prev = NULL;                                                       \
} while(0)

#define HASH_DEL_IN_BKT(hh,hh_head)                                            \
do {                                                                           \
  if ((hh_head)->hh_prev) {                                                    \
    (hh_head)->hh_prev->hh_next = (hh_head)->hh_next;                          \
  } else {                                                                     \
    (hh_head)->tbl->buckets[HASH_TO_BKT((hh_head)->hashv,                       \
      (hh_head)->tbl->num_buckets)].hh_head = (hh_head)->hh_next;               \
  }                                                                            \
  if ((hh_head)->hh_next) {                                                    \
    (hh_head)->hh_next->hh_prev = (hh_head)->hh_prev;                          \
  }                                                                            \
  (hh_head)->tbl->buckets[HASH_TO_BKT((hh_head)->hashv,                         \
    (hh_head)->tbl->num_buckets)].count--;                                     \
} while (0)

#define HASH_FIND_IN_BKT(tbl,hh,head,keyptr,keylen_in,out)                      \
do {                                                                           \
  (out) = NULL;                                                                \
  if ((head).hh_head) {                                                        \
    struct UT_hash_handle *_hf_hh;                                             \
    _hf_hh = (head).hh_head;                                                   \
    while (_hf_hh) {                                                           \
      if ((_hf_hh->keylen == (keylen_in)) &&                                    \
          (uthash_memcmp(_hf_hh->key,keyptr,keylen_in) == 0)) {                 \
        (out) = ELMT_FROM_HH(tbl,_hf_hh);                                      \
        break;                                                                 \
      }                                                                        \
      _hf_hh = _hf_hh->hh_next;                                                \
    }                                                                          \
  }                                                                            \
} while (0)

#ifndef uthash_fatal
#define uthash_fatal(msg) exit(-1)
#endif

typedef struct UT_hash_bucket {
   struct UT_hash_handle *hh_head;
   unsigned count;
   unsigned expand_mult;
} UT_hash_bucket;

/* short signature for UT_hash_handle */
#define HASH_SIG 0x54484153 /* 'HASH' */

typedef struct UT_hash_table {
   UT_hash_bucket *buckets;
   unsigned num_buckets, log2_num_buckets;
   unsigned num_items;
   struct UT_hash_handle *tail; /* tail hh in app-level list */
   ptrdiff_t hho; /* hash handle offset */
   /* fields below are available after HASH_UT_TABLE_SETUP */
   unsigned ideal_chain_maxlen;
   unsigned nonideal_items;
   unsigned ineff_expands;
   uint32_t hash_fcn_seed;
#ifdef HASH_BLOOM
   unsigned bloom_nbits;
   char *bloom_bv;
#endif
} UT_hash_table;

typedef struct UT_hash_handle {
   struct UT_hash_table *tbl;
   void *prev;                       /* prev element in app order      */
   void *next;                       /* next element in app order      */
   struct UT_hash_handle *hh_prev;   /* previous hh in bucket order    */
   struct UT_hash_handle *hh_next;   /* next hh in bucket order        */
   void *key;                        /* ptr to enclosing struct's key  */
   unsigned keylen;                  /* enclosing struct's key len     */
   unsigned hashv;                   /* result of hash function        */
} UT_hash_handle;

#endif /* UTHASH_H */
