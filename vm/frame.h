#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include "lib/kernel/hash.h"
#include "threads/synch.h"
#include "threads/palloc.h"




void vm_frame_init(void);
/*kpage : mapping된 frame의 kernel page 주소 , page frame hash fuction의 key 값이다.*/

void* vm_frame_allocate(enum palloc_flags flag, void *upage);
/*upage가 가리키는 virtual page에 해당하는 frame page를 생성하고 page frame의 
kernel 주소를 반환한다.*/
//void vm_frame_flag_free (void *kpage, bool free_page);
void vm_frame_del_entry_freepage(void *kpage);
void vm_frame_del_entry_notfreepage(void *kpage);
void vm_frame_free(void* kpage);
void vm_frame_remove_entry(void *kpage); // entry 만 제거하고 page를 free하지는 않는다.ㄴ
static void vm_frame_set_pinned (void *kpage, bool new_value);
void vm_frame_pin(void*kpage);
void vm_frame_unpin(void *kapge);



#endif