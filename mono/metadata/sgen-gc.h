/*
 * Copyright 2001-2003 Ximian, Inc
 * Copyright 2003-2010 Novell, Inc.
 * Copyright 2011 Xamarin Inc (http://www.xamarin.com)
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __MONO_SGENGC_H__
#define __MONO_SGENGC_H__

/* pthread impl */
#include "config.h"

#ifdef HAVE_SGEN_GC

typedef struct _SgenThreadInfo SgenThreadInfo;
#define THREAD_INFO_TYPE SgenThreadInfo

#include <glib.h>
#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif
#include <signal.h>
#include <mono/utils/mono-compiler.h>
#include <mono/utils/mono-threads.h>
#include <mono/io-layer/mono-mutex.h>
#include <mono/metadata/class-internals.h>
#include <mono/metadata/object-internals.h>
#include <mono/metadata/sgen-conf.h>
#include <mono/metadata/sgen-archdep.h>
#include <mono/metadata/sgen-descriptor.h>

#if defined(__MACH__)
	#include <mach/mach_port.h>
#endif

/* The method used to clear the nursery */
/* Clearing at nursery collections is the safest, but has bad interactions with caches.
 * Clearing at TLAB creation is much faster, but more complex and it might expose hard
 * to find bugs.
 */
typedef enum {
	CLEAR_AT_GC,
	CLEAR_AT_TLAB_CREATION
} NurseryClearPolicy;

NurseryClearPolicy mono_sgen_get_nursery_clear_policy (void) MONO_INTERNAL;

#define SGEN_TV_DECLARE(name) gint64 name
#define SGEN_TV_GETTIME(tv) tv = mono_100ns_ticks ()
#define SGEN_TV_ELAPSED(start,end) (int)((end-start) / 10)
#define SGEN_TV_ELAPSED_MS(start,end) ((SGEN_TV_ELAPSED((start),(end)) + 500) / 1000)

/* for use with write barriers */
typedef struct _RememberedSet RememberedSet;
struct _RememberedSet {
	mword *store_next;
	mword *end_set;
	RememberedSet *next;
	mword data [MONO_ZERO_LEN_ARRAY];
};

/* eventually share with MonoThread? */
struct _SgenThreadInfo {
	MonoThreadInfo info;
#if defined(__MACH__)
	thread_port_t mach_port;
#else
	int signal;
	unsigned int stop_count; /* to catch duplicate signals */
#endif
	int skip;
	volatile int in_critical_region;
	gboolean doing_handshake;
	gboolean thread_is_dying;
	void *stack_end;
	void *stack_start;
	void *stack_start_limit;
	char **tlab_next_addr;
	char **tlab_start_addr;
	char **tlab_temp_end_addr;
	char **tlab_real_end_addr;
	gpointer **store_remset_buffer_addr;
	long *store_remset_buffer_index_addr;
	RememberedSet *remset;
	gpointer runtime_data;
	gpointer stopped_ip;	/* only valid if the thread is stopped */
	MonoDomain *stopped_domain; /* ditto */

#ifdef USE_MONO_CTX
#ifdef __MACH__
	MonoContext ctx;		/* ditto */
#endif
	MonoContext *monoctx;	/* ditto */

#else

#if defined(__MACH__) || defined(HOST_WIN32)
	gpointer regs[ARCH_NUM_REGS];	    /* ditto */
#endif
	gpointer *stopped_regs;	    /* ditto */
#endif

#ifndef HAVE_KW_THREAD
	char *tlab_start;
	char *tlab_next;
	char *tlab_temp_end;
	char *tlab_real_end;
	gpointer *store_remset_buffer;
	long store_remset_buffer_index;
#endif
};

enum {
	MEMORY_ROLE_GEN0,
	MEMORY_ROLE_GEN1,
	MEMORY_ROLE_PINNED
};

typedef struct _SgenBlock SgenBlock;
struct _SgenBlock {
	void *next;
	unsigned char role;
};

/*
 * The nursery section and the major copying collector's sections use
 * this struct.
 */
typedef struct _GCMemSection GCMemSection;
struct _GCMemSection {
	SgenBlock block;
	char *data;
	mword size;
	/* pointer where more data could be allocated if it fits */
	char *next_data;
	char *end_data;
	/*
	 * scan starts is an array of pointers to objects equally spaced in the allocation area
	 * They let use quickly find pinned objects from pinning pointers.
	 */
	char **scan_starts;
	/* in major collections indexes in the pin_queue for objects that pin this section */
	void **pin_queue_start;
	int pin_queue_num_entries;
	unsigned short num_scan_start;
	gboolean is_to_space;
};

#define SGEN_PINNED_CHUNK_FOR_PTR(o)	((SgenBlock*)(((mword)(o)) & ~(SGEN_PINNED_CHUNK_SIZE - 1)))

typedef struct _SgenPinnedChunk SgenPinnedChunk;

/*
 * Recursion is not allowed for the thread lock.
 */
#define LOCK_DECLARE(name) mono_mutex_t name
/* if changing LOCK_INIT to something that isn't idempotent, look at
   its use in mono_gc_base_init in sgen-gc.c */
#define LOCK_INIT(name)	mono_mutex_init (&(name), NULL)
#define LOCK_GC mono_mutex_lock (&gc_mutex)
#define TRYLOCK_GC (mono_mutex_trylock (&gc_mutex) == 0)
#define UNLOCK_GC mono_mutex_unlock (&gc_mutex)
#define LOCK_INTERRUPTION mono_mutex_lock (&interruption_mutex)
#define UNLOCK_INTERRUPTION mono_mutex_unlock (&interruption_mutex)

#define SGEN_CAS_PTR	InterlockedCompareExchangePointer
#define SGEN_ATOMIC_ADD(x,i)	do {					\
		int __old_x;						\
		do {							\
			__old_x = (x);					\
		} while (InterlockedCompareExchange (&(x), __old_x + (i), __old_x) != __old_x); \
	} while (0)

#ifndef HOST_WIN32
/* we intercept pthread_create calls to know which threads exist */
#define USE_PTHREAD_INTERCEPT 1
#endif

#ifdef HEAVY_STATISTICS
#define HEAVY_STAT(x)	x

extern long long stat_objects_alloced_degraded;
extern long long stat_bytes_alloced_degraded;
extern long long stat_copy_object_called_major;
extern long long stat_objects_copied_major;
#else
#define HEAVY_STAT(x)
#endif

#define DEBUG(level,a) do {if (G_UNLIKELY ((level) <= SGEN_MAX_DEBUG_LEVEL && (level) <= gc_debug_level)) { a; fflush (gc_debug_file); } } while (0)

extern int gc_debug_level;
extern FILE* gc_debug_file;

extern int current_collection_generation;

extern unsigned int mono_sgen_global_stop_count;

#define SGEN_ALLOC_ALIGN		8
#define SGEN_ALLOC_ALIGN_BITS	3

#define SGEN_ALIGN_UP(s)		(((s)+(SGEN_ALLOC_ALIGN-1)) & ~(SGEN_ALLOC_ALIGN-1))

#ifdef SGEN_ALIGN_NURSERY
#define SGEN_PTR_IN_NURSERY(p,bits,start,end)	(((mword)(p) & ~((1 << (bits)) - 1)) == (mword)(start))
#else
#define SGEN_PTR_IN_NURSERY(p,bits,start,end)	((char*)(p) >= (start) && (char*)(p) < (end))
#endif

/* Structure that corresponds to a MonoVTable: desc is a mword so requires
 * no cast from a pointer to an integer
 */
typedef struct {
	MonoClass *klass;
	mword desc;
} GCVTable;

/* these bits are set in the object vtable: we could merge them since an object can be
 * either pinned or forwarded but not both.
 * We store them in the vtable slot because the bits are used in the sync block for
 * other purposes: if we merge them and alloc the sync blocks aligned to 8 bytes, we can change
 * this and use bit 3 in the syncblock (with the lower two bits both set for forwarded, that
 * would be an invalid combination for the monitor and hash code).
 * The values are already shifted.
 * The forwarding address is stored in the sync block.
 */
#define SGEN_FORWARDED_BIT 1
#define SGEN_PINNED_BIT 2
#define SGEN_VTABLE_BITS_MASK 0x3

/* returns NULL if not forwarded, or the forwarded address */
#define SGEN_OBJECT_IS_FORWARDED(obj) (((mword*)(obj))[0] & SGEN_FORWARDED_BIT ? (void*)(((mword*)(obj))[0] & ~SGEN_VTABLE_BITS_MASK) : NULL)
#define SGEN_OBJECT_IS_PINNED(obj) (((mword*)(obj))[0] & SGEN_PINNED_BIT)

/* set the forwarded address fw_addr for object obj */
#define SGEN_FORWARD_OBJECT(obj,fw_addr) do {				\
		((mword*)(obj))[0] = (mword)(fw_addr) | SGEN_FORWARDED_BIT; \
	} while (0)
#define SGEN_PIN_OBJECT(obj) do {	\
		((mword*)(obj))[0] |= SGEN_PINNED_BIT;	\
	} while (0)
#define SGEN_UNPIN_OBJECT(obj) do {	\
		((mword*)(obj))[0] &= ~SGEN_PINNED_BIT;	\
	} while (0)

/*
 * Since we set bits in the vtable, use the macro to load it from the pointer to
 * an object that is potentially pinned.
 */
#define SGEN_LOAD_VTABLE(addr) ((*(mword*)(addr)) & ~SGEN_VTABLE_BITS_MASK)


#define SGEN_GRAY_QUEUE_SECTION_SIZE	(128 - 3)

/*
 * This is a stack now instead of a queue, so the most recently added items are removed
 * first, improving cache locality, and keeping the stack size manageable.
 */
typedef struct _GrayQueueSection GrayQueueSection;
struct _GrayQueueSection {
	int end;
	GrayQueueSection *next;
	char *objects [SGEN_GRAY_QUEUE_SECTION_SIZE];
};

typedef struct _SgenGrayQueue SgenGrayQueue;

typedef void (*GrayQueueAllocPrepareFunc) (SgenGrayQueue*);

struct _SgenGrayQueue {
	GrayQueueSection *first;
	GrayQueueSection *free_list;
	int balance;
	GrayQueueAllocPrepareFunc alloc_prepare_func;
	void *alloc_prepare_data;
};

typedef void (*CopyOrMarkObjectFunc) (void**, SgenGrayQueue*);
typedef void (*ScanObjectFunc) (char*, SgenGrayQueue*);
typedef void (*ScanVTypeFunc) (char*, mword desc, SgenGrayQueue*);

#if SGEN_MAX_DEBUG_LEVEL >= 9
#define GRAY_OBJECT_ENQUEUE mono_sgen_gray_object_enqueue
#define GRAY_OBJECT_DEQUEUE(queue,o) ((o) = mono_sgen_gray_object_dequeue ((queue)))
#else
#define GRAY_OBJECT_ENQUEUE(queue,o) do {				\
		if (G_UNLIKELY (!(queue)->first || (queue)->first->end == SGEN_GRAY_QUEUE_SECTION_SIZE)) \
			mono_sgen_gray_object_enqueue ((queue), (o));	\
		else							\
			(queue)->first->objects [(queue)->first->end++] = (o); \
		PREFETCH ((o));						\
	} while (0)
#define GRAY_OBJECT_DEQUEUE(queue,o) do {				\
		if (!(queue)->first)					\
			(o) = NULL;					\
		else if (G_UNLIKELY ((queue)->first->end == 1))		\
			(o) = mono_sgen_gray_object_dequeue ((queue));		\
		else							\
			(o) = (queue)->first->objects [--(queue)->first->end]; \
	} while (0)
#endif

void mono_sgen_gray_object_enqueue (SgenGrayQueue *queue, char *obj) MONO_INTERNAL;
char* mono_sgen_gray_object_dequeue (SgenGrayQueue *queue) MONO_INTERNAL;

typedef void (*IterateObjectCallbackFunc) (char*, size_t, void*);

void* mono_sgen_alloc_os_memory (size_t size, int activate) MONO_INTERNAL;
void* mono_sgen_alloc_os_memory_aligned (mword size, mword alignment, gboolean activate) MONO_INTERNAL;
void mono_sgen_free_os_memory (void *addr, size_t size) MONO_INTERNAL;

int mono_sgen_thread_handshake (BOOL suspend) MONO_INTERNAL;
gboolean mono_sgen_suspend_thread (SgenThreadInfo *info) MONO_INTERNAL;
gboolean mono_sgen_resume_thread (SgenThreadInfo *info) MONO_INTERNAL;
void mono_sgen_wait_for_suspend_ack (int count) MONO_INTERNAL;
gboolean mono_sgen_park_current_thread_if_doing_handshake (SgenThreadInfo *p) MONO_INTERNAL;
void mono_sgen_os_init (void) MONO_INTERNAL;

void mono_sgen_fill_thread_info_for_suspend (SgenThreadInfo *info) MONO_INTERNAL;

gboolean mono_sgen_is_worker_thread (MonoNativeThreadId thread) MONO_INTERNAL;

void mono_sgen_update_heap_boundaries (mword low, mword high) MONO_INTERNAL;

void mono_sgen_register_major_sections_alloced (int num_sections) MONO_INTERNAL;
mword mono_sgen_get_minor_collection_allowance (void) MONO_INTERNAL;

void mono_sgen_scan_area_with_callback (char *start, char *end, IterateObjectCallbackFunc callback, void *data, gboolean allow_flags) MONO_INTERNAL;
void mono_sgen_check_section_scan_starts (GCMemSection *section) MONO_INTERNAL;

/* Keep in sync with mono_sgen_dump_internal_mem_usage() in dump_heap()! */
enum {
	INTERNAL_MEM_PIN_QUEUE,
	INTERNAL_MEM_FRAGMENT,
	INTERNAL_MEM_SECTION,
	INTERNAL_MEM_SCAN_STARTS,
	INTERNAL_MEM_FIN_TABLE,
	INTERNAL_MEM_FINALIZE_ENTRY,
	INTERNAL_MEM_FINALIZE_READY_ENTRY,
	INTERNAL_MEM_DISLINK_TABLE,
	INTERNAL_MEM_DISLINK,
	INTERNAL_MEM_ROOTS_TABLE,
	INTERNAL_MEM_ROOT_RECORD,
	INTERNAL_MEM_STATISTICS,
	INTERNAL_MEM_STAT_PINNED_CLASS,
	INTERNAL_MEM_STAT_REMSET_CLASS,
	INTERNAL_MEM_REMSET,
	INTERNAL_MEM_GRAY_QUEUE,
	INTERNAL_MEM_STORE_REMSET,
	INTERNAL_MEM_MS_TABLES,
	INTERNAL_MEM_MS_BLOCK_INFO,
	INTERNAL_MEM_EPHEMERON_LINK,
	INTERNAL_MEM_WORKER_DATA,
	INTERNAL_MEM_BRIDGE_DATA,
	INTERNAL_MEM_JOB_QUEUE_ENTRY,
	INTERNAL_MEM_TOGGLEREF_DATA,
	INTERNAL_MEM_MAX
};

#define SGEN_PINNED_FREELIST_NUM_SLOTS	30

typedef struct {
	SgenPinnedChunk *chunk_list;
	SgenPinnedChunk *free_lists [SGEN_PINNED_FREELIST_NUM_SLOTS];
	void *delayed_free_lists [SGEN_PINNED_FREELIST_NUM_SLOTS];
} SgenPinnedAllocator;

enum {
	GENERATION_NURSERY,
	GENERATION_OLD,
	GENERATION_MAX
};

void mono_sgen_init_internal_allocator (void) MONO_INTERNAL;
void mono_sgen_init_pinned_allocator (void) MONO_INTERNAL;

void mono_sgen_report_internal_mem_usage (void) MONO_INTERNAL;
void mono_sgen_report_pinned_mem_usage (SgenPinnedAllocator *alc) MONO_INTERNAL;
void mono_sgen_dump_internal_mem_usage (FILE *heap_dump_file) MONO_INTERNAL;
void mono_sgen_dump_section (GCMemSection *section, const char *type) MONO_INTERNAL;
void mono_sgen_dump_occupied (char *start, char *end, char *section_start) MONO_INTERNAL;

void mono_sgen_register_moved_object (void *obj, void *destination) MONO_INTERNAL;

void mono_sgen_register_fixed_internal_mem_type (int type, size_t size) MONO_INTERNAL;

void* mono_sgen_alloc_internal (int type) MONO_INTERNAL;
void mono_sgen_free_internal (void *addr, int type) MONO_INTERNAL;

void* mono_sgen_alloc_internal_dynamic (size_t size, int type) MONO_INTERNAL;
void mono_sgen_free_internal_dynamic (void *addr, size_t size, int type) MONO_INTERNAL;

void* mono_sgen_alloc_pinned (SgenPinnedAllocator *allocator, size_t size) MONO_INTERNAL;
void mono_sgen_free_pinned (SgenPinnedAllocator *allocator, void *addr, size_t size) MONO_INTERNAL;


void mono_sgen_debug_printf (int level, const char *format, ...) MONO_INTERNAL;

gboolean mono_sgen_parse_environment_string_extract_number (const char *str, glong *out) MONO_INTERNAL;

void mono_sgen_pinned_scan_objects (SgenPinnedAllocator *alc, IterateObjectCallbackFunc callback, void *callback_data) MONO_INTERNAL;
void mono_sgen_pinned_scan_pinned_objects (SgenPinnedAllocator *alc, IterateObjectCallbackFunc callback, void *callback_data) MONO_INTERNAL;

void mono_sgen_pinned_update_heap_boundaries (SgenPinnedAllocator *alc) MONO_INTERNAL;

void** mono_sgen_find_optimized_pin_queue_area (void *start, void *end, int *num) MONO_INTERNAL;
void mono_sgen_find_section_pin_queue_start_end (GCMemSection *section) MONO_INTERNAL;
void mono_sgen_pin_objects_in_section (GCMemSection *section, SgenGrayQueue *queue) MONO_INTERNAL;

void mono_sgen_pin_stats_register_object (char *obj, size_t size);
void mono_sgen_pin_stats_register_global_remset (char *obj);
void mono_sgen_pin_stats_print_class_stats (void);

void mono_sgen_sort_addresses (void **array, int size) MONO_INTERNAL;
void mono_sgen_add_to_global_remset (gpointer ptr) MONO_INTERNAL;

int mono_sgen_get_current_collection_generation (void) MONO_INTERNAL;
gboolean mono_sgen_nursery_collection_is_parallel (void) MONO_INTERNAL;
CopyOrMarkObjectFunc mono_sgen_get_copy_object (void) MONO_INTERNAL;
ScanObjectFunc mono_sgen_get_minor_scan_object (void) MONO_INTERNAL;
ScanVTypeFunc mono_sgen_get_minor_scan_vtype (void) MONO_INTERNAL;

typedef void (*sgen_cardtable_block_callback) (mword start, mword size);
void sgen_major_collector_iterate_live_block_ranges (sgen_cardtable_block_callback callback) MONO_INTERNAL;

typedef struct _SgenMajorCollector SgenMajorCollector;
struct _SgenMajorCollector {
	size_t section_size;
	gboolean is_parallel;
	gboolean supports_cardtable;

	/*
	 * This is set to TRUE if the sweep for the last major
	 * collection has been completed.
	 */
	gboolean *have_swept;

	void* (*alloc_heap) (mword nursery_size, mword nursery_align, int nursery_bits);
	gboolean (*is_object_live) (char *obj);
	void* (*alloc_small_pinned_obj) (size_t size, gboolean has_references);
	void* (*alloc_degraded) (MonoVTable *vtable, size_t size);
	void (*copy_or_mark_object) (void **obj_slot, SgenGrayQueue *queue);
	void (*minor_scan_object) (char *start, SgenGrayQueue *queue);
	void (*nopar_minor_scan_object) (char *start, SgenGrayQueue *queue);
	void (*minor_scan_vtype) (char *start, mword desc, SgenGrayQueue *queue);
	void (*nopar_minor_scan_vtype) (char *start, mword desc, SgenGrayQueue *queue);
	void (*major_scan_object) (char *start, SgenGrayQueue *queue);
	void (*copy_object) (void **obj_slot, SgenGrayQueue *queue);
	void (*nopar_copy_object) (void **obj_slot, SgenGrayQueue *queue);
	void* (*alloc_object) (int size, gboolean has_references);
	void (*free_pinned_object) (char *obj, size_t size);
	void (*iterate_objects) (gboolean non_pinned, gboolean pinned, IterateObjectCallbackFunc callback, void *data);
	void (*free_non_pinned_object) (char *obj, size_t size);
	void (*find_pin_queue_start_ends) (SgenGrayQueue *queue);
	void (*pin_objects) (SgenGrayQueue *queue);
	void (*scan_card_table) (SgenGrayQueue *queue);
	void (*iterate_live_block_ranges) (sgen_cardtable_block_callback callback);
	void (*init_to_space) (void);
	void (*sweep) (void);
	void (*check_scan_starts) (void);
	void (*dump_heap) (FILE *heap_dump_file);
	gint64 (*get_used_size) (void);
	void (*start_nursery_collection) (void);
	void (*finish_nursery_collection) (void);
	void (*start_major_collection) (void);
	void (*finish_major_collection) (void);
	void (*have_computed_minor_collection_allowance) (void);
	gboolean (*ptr_is_in_non_pinned_space) (char *ptr);
	gboolean (*obj_is_from_pinned_alloc) (char *obj);
	void (*report_pinned_memory_usage) (void);
	int (*get_num_major_sections) (void);
	gboolean (*handle_gc_param) (const char *opt);
	void (*print_gc_param_usage) (void);
	gboolean (*is_worker_thread) (MonoNativeThreadId thread);
	void (*post_param_init) (void);
	void* (*alloc_worker_data) (void);
	void (*init_worker_thread) (void *data);
	void (*reset_worker_data) (void *data);
};

void mono_sgen_marksweep_init (SgenMajorCollector *collector) MONO_INTERNAL;
void mono_sgen_marksweep_fixed_init (SgenMajorCollector *collector) MONO_INTERNAL;
void mono_sgen_marksweep_par_init (SgenMajorCollector *collector) MONO_INTERNAL;
void mono_sgen_marksweep_fixed_par_init (SgenMajorCollector *collector) MONO_INTERNAL;
void mono_sgen_copying_init (SgenMajorCollector *collector) MONO_INTERNAL;

static guint /*__attribute__((noinline)) not sure if this hint is a good idea*/
slow_object_get_size (MonoVTable *vtable, MonoObject* o)
{
	MonoClass *klass = vtable->klass;

	/*
	 * We depend on mono_string_length_fast and
	 * mono_array_length_fast not using the object's vtable.
	 */
	if (klass == mono_defaults.string_class) {
		return sizeof (MonoString) + 2 * mono_string_length_fast ((MonoString*) o) + 2;
	} else if (klass->rank) {
		MonoArray *array = (MonoArray*)o;
		size_t size = sizeof (MonoArray) + klass->sizes.element_size * mono_array_length_fast (array);
		if (G_UNLIKELY (array->bounds)) {
			size += sizeof (mono_array_size_t) - 1;
			size &= ~(sizeof (mono_array_size_t) - 1);
			size += sizeof (MonoArrayBounds) * klass->rank;
		}
		return size;
	} else {
		/* from a created object: the class must be inited already */
		return klass->instance_size;
	}
}

/*
 * This function can be called on an object whose first word, the
 * vtable field, is not intact.  This is necessary for the parallel
 * collector.
 */
static inline guint
mono_sgen_par_object_get_size (MonoVTable *vtable, MonoObject* o)
{
	mword descr = (mword)vtable->gc_descr;
	mword type = descr & 0x7;

	if (type == DESC_TYPE_RUN_LENGTH || type == DESC_TYPE_SMALL_BITMAP) {
		mword size = descr & 0xfff8;
		if (size == 0) /* This is used to encode a string */
			return sizeof (MonoString) + 2 * mono_string_length_fast ((MonoString*) o) + 2;
		return size;
	} else if (type == DESC_TYPE_VECTOR) {
		int element_size = ((descr) >> VECTOR_ELSIZE_SHIFT) & MAX_ELEMENT_SIZE;
		MonoArray *array = (MonoArray*)o;
		size_t size = sizeof (MonoArray) + element_size * mono_array_length_fast (array);

		if (descr & VECTOR_KIND_ARRAY) {
			size += sizeof (mono_array_size_t) - 1;
			size &= ~(sizeof (mono_array_size_t) - 1);
			size += sizeof (MonoArrayBounds) * vtable->klass->rank;
		}
		return size;
	}

	return slow_object_get_size (vtable, o);
}

static inline guint
mono_sgen_safe_object_get_size (MonoObject *obj)
{
       char *forwarded;

       if ((forwarded = SGEN_OBJECT_IS_FORWARDED (obj)))
               obj = (MonoObject*)forwarded;

       return mono_sgen_par_object_get_size ((MonoVTable*)SGEN_LOAD_VTABLE (obj), obj);
}

const char* mono_sgen_safe_name (void* obj) MONO_INTERNAL;

gboolean mono_sgen_object_is_live (void *obj) MONO_INTERNAL;

gboolean mono_sgen_need_bridge_processing (void) MONO_INTERNAL;
void mono_sgen_bridge_processing_register_objects (int num_objs, MonoObject **objs) MONO_INTERNAL;
void mono_sgen_bridge_processing_stw_step (void) MONO_INTERNAL;
void mono_sgen_bridge_processing_finish (void) MONO_INTERNAL;
void mono_sgen_register_test_bridge_callbacks (const char *bridge_class_name) MONO_INTERNAL;
gboolean mono_sgen_is_bridge_object (MonoObject *obj) MONO_INTERNAL;
void mono_sgen_mark_bridge_object (MonoObject *obj) MONO_INTERNAL;

void mono_sgen_scan_togglerefs (CopyOrMarkObjectFunc copy_func, char *start, char *end, SgenGrayQueue *queue) MONO_INTERNAL;
void mono_sgen_process_togglerefs (void) MONO_INTERNAL;


gboolean mono_sgen_gc_is_object_ready_for_finalization (void *object) MONO_INTERNAL;
void mono_sgen_gc_lock (void) MONO_INTERNAL;
void mono_sgen_gc_unlock (void) MONO_INTERNAL;

enum {
	SPACE_MAJOR,
	SPACE_LOS
};

gboolean mono_sgen_try_alloc_space (mword size, int space) MONO_INTERNAL;
void mono_sgen_release_space (mword size, int space) MONO_INTERNAL;
void mono_sgen_pin_object (void *object, SgenGrayQueue *queue) MONO_INTERNAL;
void sgen_collect_major_no_lock (const char *reason) MONO_INTERNAL;
gboolean mono_sgen_need_major_collection (mword space_needed) MONO_INTERNAL;
void mono_sgen_set_pinned_from_failed_allocation (mword objsize) MONO_INTERNAL;

/* LOS */

typedef struct _LOSObject LOSObject;
struct _LOSObject {
	LOSObject *next;
	mword size; /* this is the object size */
	guint16 huge_object;
	int dummy; /* to have a sizeof (LOSObject) a multiple of ALLOC_ALIGN  and data starting at same alignment */
	char data [MONO_ZERO_LEN_ARRAY];
};

#define ARRAY_OBJ_INDEX(ptr,array,elem_size) (((char*)(ptr) - ((char*)(array) + G_STRUCT_OFFSET (MonoArray, vector))) / (elem_size))

extern LOSObject *los_object_list;
extern mword los_memory_usage;

void mono_sgen_los_free_object (LOSObject *obj) MONO_INTERNAL;
void* mono_sgen_los_alloc_large_inner (MonoVTable *vtable, size_t size) MONO_INTERNAL;
void mono_sgen_los_sweep (void) MONO_INTERNAL;
gboolean mono_sgen_ptr_is_in_los (char *ptr, char **start) MONO_INTERNAL;
void mono_sgen_los_iterate_objects (IterateObjectCallbackFunc cb, void *user_data) MONO_INTERNAL;
void mono_sgen_los_iterate_live_block_ranges (sgen_cardtable_block_callback callback) MONO_INTERNAL;
void mono_sgen_los_scan_card_table (SgenGrayQueue *queue) MONO_INTERNAL;
void sgen_major_collector_scan_card_table (SgenGrayQueue *queue) MONO_INTERNAL;
FILE *mono_sgen_get_logfile (void) MONO_INTERNAL;

/* nursery allocator */

void mono_sgen_clear_nursery_fragments (void) MONO_INTERNAL;
void mono_sgen_nursery_allocator_prepare_for_pinning (void) MONO_INTERNAL;
void mono_sgen_clear_current_nursery_fragment (void) MONO_INTERNAL;
void mono_sgen_nursery_allocator_set_nursery_bounds (char *nursery_start, char *nursery_end) MONO_INTERNAL;
mword mono_sgen_build_nursery_fragments (GCMemSection *nursery_section, void **start, int num_entries) MONO_INTERNAL;
void mono_sgen_init_nursery_allocator (void) MONO_INTERNAL;
void mono_sgen_nursery_allocator_init_heavy_stats (void) MONO_INTERNAL;
char* mono_sgen_nursery_alloc_get_upper_alloc_bound (void) MONO_INTERNAL;
void* mono_sgen_nursery_alloc (size_t size) MONO_INTERNAL;
void* mono_sgen_nursery_alloc_range (size_t size, size_t min_size, int *out_alloc_size) MONO_INTERNAL;
MonoVTable* mono_sgen_get_array_fill_vtable (void) MONO_INTERNAL;
gboolean mono_sgen_can_alloc_size (size_t size) MONO_INTERNAL;
void mono_sgen_nursery_retire_region (void *address, ptrdiff_t size) MONO_INTERNAL;

/* hash tables */

typedef struct _SgenHashTableEntry SgenHashTableEntry;
struct _SgenHashTableEntry {
	SgenHashTableEntry *next;
	gpointer key;
	char data [MONO_ZERO_LEN_ARRAY]; /* data is pointer-aligned */
};

typedef struct {
	int table_mem_type;
	int entry_mem_type;
	size_t data_size;
	GHashFunc hash_func;
	GEqualFunc equal_func;
	SgenHashTableEntry **table;
	guint size;
	guint num_entries;
} SgenHashTable;

#define SGEN_HASH_TABLE_INIT(table_type,entry_type,data_size,hash_func,equal_func)	{ (table_type), (entry_type), (data_size), (hash_func), (equal_func), NULL, 0, 0 }
#define SGEN_HASH_TABLE_ENTRY_SIZE(data_size)			((data_size) + sizeof (SgenHashTableEntry*) + sizeof (gpointer))

gpointer mono_sgen_hash_table_lookup (SgenHashTable *table, gpointer key) MONO_INTERNAL;
gboolean mono_sgen_hash_table_replace (SgenHashTable *table, gpointer key, gpointer data) MONO_INTERNAL;
gboolean mono_sgen_hash_table_set_value (SgenHashTable *table, gpointer key, gpointer data) MONO_INTERNAL;
gboolean mono_sgen_hash_table_set_key (SgenHashTable *hash_table, gpointer old_key, gpointer new_key) MONO_INTERNAL;
gboolean mono_sgen_hash_table_remove (SgenHashTable *table, gpointer key, gpointer data_return) MONO_INTERNAL;

void mono_sgen_hash_table_clean (SgenHashTable *table) MONO_INTERNAL;

#define mono_sgen_hash_table_num_entries(h)	((h)->num_entries)

#define SGEN_HASH_TABLE_FOREACH(h,k,v) do {				\
		SgenHashTable *__hash_table = (h);			\
		SgenHashTableEntry **__table = __hash_table->table;	\
		guint __i;						\
		for (__i = 0; __i < (h)->size; ++__i) {			\
			SgenHashTableEntry **__iter, **__next;			\
			for (__iter = &__table [__i]; *__iter; __iter = __next) {	\
				SgenHashTableEntry *__entry = *__iter;	\
				__next = &__entry->next;	\
				(k) = __entry->key;			\
				(v) = (gpointer)__entry->data;

/* The loop must be continue'd after using this! */
#define SGEN_HASH_TABLE_FOREACH_REMOVE(free)	do {			\
		*__iter = *__next;	\
		__next = __iter;	\
		--__hash_table->num_entries;				\
		if ((free))						\
			mono_sgen_free_internal (__entry, __hash_table->entry_mem_type); \
	} while (0)

#define SGEN_HASH_TABLE_FOREACH_SET_KEY(k)	((__entry)->key = (k))

#define SGEN_HASH_TABLE_FOREACH_END					\
			}						\
		}							\
	} while (0)

#endif /* HAVE_SGEN_GC */

#endif /* __MONO_SGENGC_H__ */
