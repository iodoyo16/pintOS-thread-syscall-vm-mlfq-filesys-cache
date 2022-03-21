#include <hash.h>
#include <list.h>
#include <stdio.h>
#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"

#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"


/* clock algorithm. 을 위한 list*/
static struct list frame_list;      
static struct list_elem *victim_ptr; 

static struct lock frame_lock;
static struct hash frame_map;



struct frame_table_entry{
    void *kpage;               
    void *upage;               
    struct hash_elem helem;
    int pid;    
    struct list_elem lelem;    
    bool pinned;               /*frame이 evicted 되는 것을 막는 flag*/
    struct thread *t;
  };

struct frame_table_entry* clock_pointing_frame(void);
static struct frame_table_entry* clock_algorithm(uint32_t* pagedir);
static unsigned frame_hash_func(const struct hash_elem *elem, void *aux);
static bool     frame_less_func(const struct hash_elem *, const struct hash_elem *, void *aux);

void vm_frame_init (){
  lock_init (&frame_lock);
  hash_init (&frame_map, frame_hash_func, frame_less_func, NULL);
  list_init (&frame_list);
  victim_ptr = NULL;
}
static unsigned frame_hash_func(const struct hash_elem *elem, void *aux UNUSED){
  struct frame_table_entry *ft_entry = hash_entry(elem, struct frame_table_entry, helem);
  return hash_bytes( &ft_entry->kpage, sizeof ft_entry->kpage );
}
static bool frame_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
  struct frame_table_entry *s_ft_entry = hash_entry(a, struct frame_table_entry, helem);
  struct frame_table_entry *e_ft_entry = hash_entry(b, struct frame_table_entry, helem);
  return s_ft_entry->kpage < e_ft_entry->kpage;
}
/* Allocate a new frame */
void*
vm_frame_allocate (enum palloc_flags flags, void *upage)
{
  lock_acquire (&frame_lock);
  void *frame_page = palloc_get_page (PAL_USER | flags);
  if (frame_page == NULL) {
    //swap out 해서 page할당할 공간을 만든다.
    struct frame_table_entry *evic_entry = clock_algorithm( thread_current()->pagedir );
    uint32_t* evict_pagedir=evic_entry->t->pagedir;
    pagedir_clear_page(evict_pagedir, evic_entry->upage);
    bool is_dirty = false;
    bool u_is_dirty=pagedir_is_dirty(evict_pagedir, evic_entry->upage);
    bool k_is_dirty=pagedir_is_dirty(evict_pagedir, evic_entry->kpage);
    is_dirty = is_dirty || u_is_dirty;
    is_dirty = is_dirty|| k_is_dirty;
    swap_index_t swap_idx = vm_swap_out( evic_entry->kpage );
    vm_pt_set_swap(evic_entry->t->supt, evic_entry->upage, swap_idx);
    vm_pt_set_dirty(evic_entry->t->supt, evic_entry->upage, is_dirty);
    vm_frame_del_entry_freepage(evic_entry->kpage); 
    frame_page = palloc_get_page (PAL_USER | flags);
  }
  struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
  if(fte != NULL) {
    fte->upage = upage;
    fte->pinned = true;
    fte->kpage = frame_page;
    fte->t = thread_current ();
    hash_insert (&frame_map, &fte->helem);
    list_push_back (&frame_list, &fte->lelem);

    lock_release (&frame_lock);
    return frame_page; 
  }
  else{
    lock_release (&frame_lock);
    return NULL;
  }
}
void vm_frame_free (void *kpage){
  lock_acquire (&frame_lock);
  vm_frame_del_entry_freepage(kpage);
  lock_release (&frame_lock);
}

void vm_frame_remove_entry (void *kpage){
  lock_acquire (&frame_lock);
  vm_frame_del_entry_notfreepage(kpage);
  lock_release (&frame_lock);
}

void vm_frame_del_entry_freepage (void *kpage){
  struct frame_table_entry tmp_fte;
  tmp_fte.kpage = kpage;
  struct hash_elem *find_e = hash_find (&frame_map, &(tmp_fte.helem));
  if (find_e != NULL) {
    struct frame_table_entry *fte= hash_entry(find_e, struct frame_table_entry, helem);
    hash_delete (&frame_map, &fte->helem);
    list_remove (&fte->lelem);
    palloc_free_page(kpage);
    free(fte);
  }else{
    sys_exit(-1);
  }
}
void vm_frame_del_entry_notfreepage (void *kpage){
  struct frame_table_entry tmp_fte;
  tmp_fte.kpage = kpage;
  struct hash_elem *find_e = hash_find (&frame_map, &(tmp_fte.helem));
  if (find_e != NULL) {
    struct frame_table_entry *fte= hash_entry(find_e, struct frame_table_entry, helem);
    hash_delete (&frame_map, &fte->helem);
    list_remove (&fte->lelem);
    free(fte);
  }
  else{
    sys_exit(-1);
  }
}
struct frame_table_entry* clock_pointing_frame(void){
  if (!list_empty(&frame_list)){
    if(victim_ptr == NULL){
      victim_ptr = list_begin (&frame_list);
    }
    else if (victim_ptr == list_end(&frame_list)){
      victim_ptr = list_begin (&frame_list);
    }
    else{
      victim_ptr = list_next (victim_ptr);
    }
    struct frame_table_entry *ft_entry = list_entry(victim_ptr, struct frame_table_entry, lelem);
    return ft_entry;
  }
}
struct frame_table_entry* clock_algorithm( uint32_t *pagedir ) {
  size_t n = hash_size(&frame_map);
  if(n<=0){
    sys_exit(-1);
  }
  for(size_t it = 0; it <= 2*n; ++ it) // prevent infinite loop. 
  {
    struct frame_table_entry *e = clock_pointing_frame();
    if(e->pinned)
      continue;
    else if( pagedir_is_accessed(pagedir, e->upage)) {
      pagedir_set_accessed(pagedir, e->upage, false);
      continue;
    }
    return e;
  }
}

void vm_frame_pin (void* kpage) {
  vm_frame_set_pinned (kpage, true);
}
void vm_frame_unpin (void* kpage) {
  vm_frame_set_pinned (kpage, false);
}
static void vm_frame_set_pinned (void *kpage, bool new_value){
  lock_acquire (&frame_lock);
  struct frame_table_entry tmp_fte;
  tmp_fte.kpage = kpage;
  struct hash_elem *find_e = hash_find (&frame_map, &(tmp_fte.helem));
  if (find_e != NULL) {
    struct frame_table_entry *ft_entry;
    ft_entry = hash_entry(find_e, struct frame_table_entry, helem);
    ft_entry->pinned = new_value;
    lock_release (&frame_lock);
  }
}



