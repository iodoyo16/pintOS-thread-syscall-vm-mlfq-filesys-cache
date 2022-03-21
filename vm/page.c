#include <hash.h>
#include <string.h>

#include "lib/kernel/hash.h"
#include "page.h"
#include "frame.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

static unsigned pte_hash_func(const struct hash_elem *elem, void *aux);
static bool pte_less_func(const struct hash_elem *, const struct hash_elem *, void *aux);
static void pte_destroy_func(struct hash_elem *elem, void *aux);
static bool vm_load_page_from_filesys(struct vm_pt_entry *, void *);

//vm_page_hashtable 을 생성하는 함수
struct vm_page_table *vm_pt_create(void){
    struct vm_page_table *pt = 
    (struct vm_page_table *) malloc(sizeof(struct vm_page_table));
    struct hash* page_hash_table=&pt->page_hashmap;
    if(pt==NULL){
        sys_exit(-1);
    }
    hash_init(page_hash_table, pte_hash_func, pte_less_func, NULL);
    return pt;
}
// supplemental table 의 entry를 초기화 할때 쓰는 hash func;
static unsigned pte_hash_func(const struct hash_elem *elem, void *aux UNUSED){
    struct vm_pt_entry *entry = hash_entry(elem, struct vm_pt_entry, elem);
    return hash_int((int)entry->upage);
}
// supplemental table 의 entry를 비교하는 함수
static bool pte_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
    struct vm_pt_entry *s = hash_entry(a, struct vm_pt_entry, elem);
    struct vm_pt_entry *e = hash_entry(b, struct vm_pt_entry, elem);
    return s -> upage < e -> upage;
}


//vm_page_hashtable 을 삭제하는 함수
void vm_page_table_destroy(struct vm_page_table *pt){
    if(pt==NULL){
        sys_exit(-1);
    }
    struct hash* page_hash_table=&pt->page_hashmap;
    hash_destroy(page_hash_table, pte_destroy_func);
    if(pt!=NULL){
        free(pt);
    }
}

void preload_and_pin_pages(const void *buffer, size_t size)
{
  struct vm_page_table *pt = thread_current()->supt;
  uint32_t *pagedir = thread_current()->pagedir;
  void *upage=pg_round_down(buffer);
  while(upage < buffer + size){
    handle_mm_fault(pt, pagedir, upage);
    vm_pin_page (pt, upage);
    upage += PGSIZE;
  }
}

void unpin_preloaded_pages(const void *buffer, size_t size)
{
  struct vm_page_table *pt = thread_current()->supt;
  void *upage=pg_round_down(buffer);
  while(upage < buffer + size){
    vm_unpin_page (pt, upage);
    upage += PGSIZE;
  }
}
static void pte_destroy_func(struct hash_elem *elem, void *aux UNUSED){
    struct vm_pt_entry *pt_entry = hash_entry(elem, struct vm_pt_entry, elem);
    if(pt_entry->kpage != NULL){
        vm_frame_remove_entry(pt_entry->kpage); //frame 삭제
    }
    else if(pt_entry -> status == ON_SWAP){
        vm_swap_free(pt_entry->swap_index); // swap free
    }
    if(pt_entry!=NULL){
        free(pt_entry); //pte free
    }
}
static bool vm_load_page_from_filesys(struct vm_pt_entry *pte, void *kpage){
    file_seek(pte->file, pte->file_offset);
    int read = file_read(pte->file, kpage, pte->read_bytes);
    if(read == (int) pte->read_bytes){
        memset(kpage + read, 0, pte->zero_bytes);
        return true;
    }
    else{
        return false;
    }
}
/*page_table에 entry 가 있는지 주소값을 이용해 확인*/
bool vm_pt_has_entry(struct vm_page_table *pt, void *page){
    struct vm_pt_entry *page_table_entry = vm_pt_look_up(pt, page);
    if(page_table_entry == NULL)
        return false;
    else
        return true;
}
/*entry에 dirty bit 를 set 한다*/
bool vm_pt_set_dirty(struct vm_page_table *pt, void *page, bool value){
    struct vm_pt_entry *page_table_entry = vm_pt_look_up(pt,page);
    if(page_table_entry == NULL){
        return false;
    }
    else {
        page_table_entry -> dirty = page_table_entry->dirty || value;
        return true;
    }
}
/*생성된 page의 주소(upage,kpage) 를 전달받고, 
page table entry를 생성해 초기화하고
page_hash_table에 insert한다.*/
bool vm_pt_install_frame(struct vm_page_table *pt, void *upage, void *kpage){
    struct vm_pt_entry *page_hash_table_entry;
    page_hash_table_entry = (struct vm_pt_entry *) malloc(sizeof(struct vm_pt_entry));

    if(page_hash_table_entry==NULL){
        sys_exit(-1);
    }
    page_hash_table_entry -> swap_index = -1;
    page_hash_table_entry -> kpage = kpage;
    page_hash_table_entry -> upage = upage;
    page_hash_table_entry -> dirty = false;
    page_hash_table_entry -> status = ON_FRAME;

    struct hash* page_hash_table=&pt->page_hashmap;
    struct hash_elem* pt_entry_elem=&page_hash_table_entry->elem;
    struct hash_elem *prev = hash_insert(page_hash_table, pt_entry_elem);
    if(prev != NULL){
        if(page_hash_table_entry!=NULL)
            free(page_hash_table_entry);
        return false;
    }
    return true;
}
/*vm_supt_look_up을 이용해서 page_table_entry의 flag
를 swap 으로 set 한다 (ON_SWAP)*/
bool vm_pt_set_swap(struct vm_page_table *pt, void *page, swap_index_t swap_index){
    struct vm_pt_entry* page_hash_table_entry;
    page_hash_table_entry = vm_pt_look_up(pt,page);
    if(page_hash_table_entry != NULL){
        page_hash_table_entry -> status = ON_SWAP;
        page_hash_table_entry -> kpage = NULL;
        page_hash_table_entry -> swap_index = swap_index;
        return true;
    }
    return false;
}
/*생성된 page의 주소(upage) 를 전달받고, 
page table entry를 생성해 초기화하고 (status 가 ALL_ZERO)
page_hash_table에 insert한다.*/
bool expand_stack (struct vm_page_table *pt, void *upage){
    struct vm_pt_entry *page_hash_table_entry;
    page_hash_table_entry = (struct vm_pt_entry *) malloc(sizeof(struct vm_pt_entry));
    if(page_hash_table_entry==NULL){
        sys_exit(-1);
    }
    page_hash_table_entry -> upage = upage;
    page_hash_table_entry -> kpage = NULL;
    page_hash_table_entry -> dirty = false;
    page_hash_table_entry -> status = ALL_ZERO;

    struct hash* page_hash_table=&pt->page_hashmap;
    struct hash_elem* pt_entry_elem=&page_hash_table_entry->elem;
    struct hash_elem *prev = hash_insert(page_hash_table, pt_entry_elem);
    if(prev != NULL){
        free(page_hash_table_entry);
        sys_exit(-1);
        return false;
    }
    return true;
}
/*생성된 page의 주소(upage) 를 전달받고, 
page table entry를 생성해 초기화하고 (status 가 FROM_FILESYS)
page_hash_table에 insert한다.*/
bool vm_pt_install_filesys(struct vm_page_table *pt, void *upage,
    struct file *file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes,bool writable){
    struct vm_pt_entry *page_hash_table_entry;
    page_hash_table_entry = (struct vm_pt_entry *) malloc(sizeof(struct vm_pt_entry));
    if(page_hash_table_entry==NULL){
        sys_exit(-1);
    }
    page_hash_table_entry -> upage = upage;
    page_hash_table_entry -> kpage = NULL;
    page_hash_table_entry -> status = FROM_FILESYS;
    page_hash_table_entry -> dirty = false;
    page_hash_table_entry -> file = file;
    page_hash_table_entry -> file_offset = offset;
    page_hash_table_entry -> read_bytes = read_bytes;
    page_hash_table_entry -> zero_bytes = zero_bytes;
    page_hash_table_entry -> writable = writable;

    struct hash* page_hash_table=&pt->page_hashmap;
    struct hash_elem* pt_entry_elem=&page_hash_table_entry->elem;
    struct hash_elem *prev = hash_insert(page_hash_table, pt_entry_elem);
    if(prev != NULL){
        free(page_hash_table_entry);
        sys_exit(-1);
        return false;
    }
    return true;
}

/* param page로 (upage) 로 hash_find 를 이용해 page_table_entry 포인터를 
반환 받는다*/
struct vm_pt_entry* vm_pt_look_up (struct vm_page_table* pt, void *page){
    struct vm_pt_entry tmp_entry;
    tmp_entry.upage = page;
    if(pt==NULL){
        sys_exit(-1);
    }
    struct hash* page_hash_table=&pt->page_hashmap;
    struct hash_elem *page_table_entry_elem = hash_find(page_hash_table, &tmp_entry.elem);
    if(page_table_entry_elem != NULL){
        return hash_entry(page_table_entry_elem,struct vm_pt_entry,elem);
    }
    return NULL; 
}

/*entry 에 mapping 되어 있지만 메모리에 load 되지 않은 page를 load 한다.*/
bool handle_mm_fault(struct vm_page_table *pt, uint32_t *pagedir, void *upage){
    // entry 에 mapping 되어 있는지 확인, 없으면 false return
    struct vm_pt_entry *pte;
    pte = vm_pt_look_up(pt, upage);
    if(pte == NULL)
        return false;
    //이미 로드 되었을때
    if(pte->status == ON_FRAME)
        return true;
    //frame을 allocate 한다.
    void *frame_page = vm_frame_allocate(PAL_USER, upage);
    if(frame_page == NULL){
        return false;
    }
    //status에 맞게 data를 load한다.
    bool writable = true;
    switch(pte->status){
        case ON_SWAP:// swap space 에 있는 경우
            vm_swap_in(pte->swap_index, frame_page);// 디스크에서 메모리로 load
            break;
        case ALL_ZERO:
            memset(frame_page, 0 , PGSIZE);
            break;
        case FROM_FILESYS: // 디스크에서 메모리로 load하는 경우
            if(vm_load_page_from_filesys(pte, frame_page)){
                writable = pte->writable;
            }
            else{
                vm_frame_free(frame_page);
                return false;
            }
            break;
        case ON_FRAME:
            break;
    }
    //
    if(!pagedir_set_page(pagedir, upage, frame_page, writable)){
        vm_frame_free(frame_page);
        return false;
    }
    pte->kpage = frame_page;
    pte->status = ON_FRAME;

    pagedir_set_dirty(pagedir, frame_page, false);

    vm_frame_unpin(frame_page);
    return true;
}

bool vm_pt_mm_unmap(struct vm_page_table *pt, uint32_t *pagedir, void *page, 
      struct file *file, off_t offset, size_t bytes){
    struct vm_pt_entry *pt_entry = vm_pt_look_up(pt, page);
    if(pt_entry == NULL){
        sys_exit(-1);
    }
    else if(pt_entry->status == ON_FRAME){
        vm_frame_pin(pt_entry->kpage);
    }
    if(pt_entry->status==ON_FRAME){
        ASSERT (pt_entry->kpage != NULL);
        //dirty 인지 확인하고 만약 set되었다면 file에 write한다.
        bool is_dirty = pt_entry->dirty;
        bool u_is_dirty=pagedir_is_dirty(pagedir, pt_entry->upage);
        bool k_is_dirty=pagedir_is_dirty(pagedir, pt_entry->kpage);
        is_dirty = is_dirty || u_is_dirty||k_is_dirty;
        if(is_dirty){
            file_write_at(file, pt_entry->upage, bytes, offset);
        }
        //page mapping을 지우고 free
        vm_frame_free(pt_entry->kpage);
        pagedir_clear_page(pagedir, pt_entry->upage);
    }
    else if(pt_entry->status==ON_SWAP){
        bool is_dirty = pt_entry->dirty;
        bool u_is_dirty=pagedir_is_dirty(pagedir,pt_entry->upage);
        is_dirty = is_dirty || u_is_dirty;
        //dirty 가 set되었다면 swap space 에서 load 하고 write file
        if(is_dirty){
            void *tmp_page = palloc_get_page(0);
            vm_swap_in(pt_entry->swap_index, tmp_page);
            file_write_at(file,tmp_page,PGSIZE,offset);
            palloc_free_page(tmp_page);
        }
        else {
            vm_swap_free(pt_entry->swap_index);
        }
    }
    // page table 에서 entry 삭제
    hash_delete(&pt->page_hashmap, &pt_entry->elem);
    return true;
}    




void vm_pin_page(struct vm_page_table *pt, void *page){
    struct vm_pt_entry *pt_entry = vm_pt_look_up(pt, page);
    if(pt_entry != NULL){
        vm_frame_pin(pt_entry->kpage);
    }
}
void vm_unpin_page(struct vm_page_table *pt, void *page){
    struct vm_pt_entry *pt_entry = vm_pt_look_up(pt, page);
    if(pt_entry != NULL&&pt_entry->status == ON_FRAME)
        vm_frame_unpin(pt_entry->kpage);
    if(pt_entry==NULL){
        sys_exit(-1);
    }
}