#include <kernel/memory.h>
#include <kernel/drivers.h>
#include <kernel/kstring.h>
#include <kernel/debug.h>

#define CACHE_NAME_MAX      32
#define OBJECTS_PER_SLAB    64
#define CACHE_LINE_SIZE     64
#define COLOR_COUNT         8

#define MAGIC_ALLOC         0xA110CA7E
#define MAGIC_FREE          0xF4EE0B13

static slub_cache_t *g_cache_list;
static uint32_t      g_color_idx;
static int           g_initialized;

void slub_init(void) {
    g_cache_list = NULL;
    g_color_idx  = 0;
    g_initialized = 1;
}

slub_cache_t *slub_cache_create(const char *name, size_t obj_size) {
    if (!g_initialized) return NULL;
    
    slub_cache_t *c = (slub_cache_t *)malloc(sizeof(slub_cache_t));
    if (!c) return NULL;
    
    kstrncpy(c->name, name, CACHE_NAME_MAX);
    c->name[CACHE_NAME_MAX - 1] = '\0';
    c->object_size     = obj_size;
    c->objects_per_slab = OBJECTS_PER_SLAB;
    c->first_slab      = NULL;
    c->total_objects   = 0;
    c->free_objects    = 0;
    c->allocations     = 0;
    c->deallocations   = 0;
    c->lock            = 0;
    c->next            = g_cache_list;
    g_cache_list       = c;
    return c;
}

static slub_slab_t *slab_create(slub_cache_t *c) {
    uint32_t color = (g_color_idx++ % COLOR_COUNT) * CACHE_LINE_SIZE;
    size_t obj_stride = c->object_size + sizeof(slub_object_t);
    size_t slab_size = sizeof(slub_slab_t) + color + (c->objects_per_slab * obj_stride);
    
    slub_slab_t *s = (slub_slab_t *)malloc(slab_size);
    if (!s) return NULL;
    
    s->free_count = c->objects_per_slab;
    s->freelist   = NULL;
    s->next       = NULL;
    s->objects    = (uint8_t *)s + sizeof(slub_slab_t) + color;
    
    /* Build freelist */
    uint8_t *base = (uint8_t *)s->objects;
    slub_object_t *prev = NULL;
    for (uint32_t i = 0; i < c->objects_per_slab; i++) {
        slub_object_t *obj = (slub_object_t *)(base + i * obj_stride);
        obj->magic = MAGIC_FREE;
        obj->owner_cache = c;
        obj->next_free = NULL;
        if (prev) prev->next_free = obj;
        else s->freelist = obj;
        prev = obj;
    }
    
    c->total_objects += c->objects_per_slab;
    c->free_objects  += c->objects_per_slab;
    return s;
}

#define LOCK(c)   while (__sync_lock_test_and_set(&(c)->lock, 1)) __asm__ volatile("pause")
#define UNLOCK(c) __sync_lock_release(&(c)->lock)

void *slub_alloc(slub_cache_t *c) {
    if (!c || !g_initialized) return NULL;
    LOCK(c);
    
    slub_slab_t *s = c->first_slab;
    if (!s || s->free_count == 0) {
        s = slab_create(c);
        if (!s) { UNLOCK(c); return NULL; }
        s->next = (slub_slab_t *)c->first_slab;
        c->first_slab = s;
    }
    
    slub_object_t *obj = (slub_object_t *)s->freelist;
    if (!obj) { UNLOCK(c); return NULL; }
    
    s->freelist = obj->next_free;
    s->free_count--;
    c->free_objects--;
    c->allocations++;
    obj->magic = MAGIC_ALLOC;
    
    UNLOCK(c);
    return (void *)((uint8_t *)obj + sizeof(slub_object_t));
}

void slub_free(slub_cache_t *c, void *ptr) {
    if (!c || !ptr || !g_initialized) return;
    
    slub_object_t *obj = (slub_object_t *)((uint8_t *)ptr - sizeof(slub_object_t));
    if (obj->magic != MAGIC_ALLOC) return;
    
    LOCK(c);
    obj->magic = MAGIC_FREE;
    
    /* Find containing slab */
    size_t stride = c->object_size + sizeof(slub_object_t);
    slub_slab_t *target = NULL;
    for (slub_slab_t *s = c->first_slab; s; s = s->next) {
        uint8_t *start = s->objects;
        uint8_t *end = start + (c->objects_per_slab * stride);
        if ((uint8_t *)obj >= start && (uint8_t *)obj < end) {
            target = s;
            break;
        }
    }
    
    if (!target) { UNLOCK(c); return; }
    
    obj->next_free = (slub_object_t *)target->freelist;
    target->freelist = obj;
    target->free_count++;
    c->free_objects++;
    c->deallocations++;
    UNLOCK(c);
}

void slub_cache_destroy(slub_cache_t *c) {
    if (!c) return;
    
    /* Free all slabs */
    for (slub_slab_t *s = c->first_slab; s; ) {
        slub_slab_t *next = s->next;
        free(s);
        s = next;
    }
    
    /* Remove from global list */
    if (g_cache_list == c) {
        g_cache_list = c->next;
    } else {
        for (slub_cache_t *p = g_cache_list; p; p = p->next) {
            if (p->next == c) { p->next = c->next; break; }
        }
    }
    free(c);
}
