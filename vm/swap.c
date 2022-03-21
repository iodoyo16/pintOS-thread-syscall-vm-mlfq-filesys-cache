#include <bitmap.h>

#include "vm/swap.h"
#include "threads/vaddr.h"
#include "devices/block.h"

static struct block *swap_block;
static struct bitmap *available_swap;

/*pagesize가 swap sector 보다 크므로 
'PGSIZE / BLOCK_SECTOR_SIZE' 개의 contiguous blocks 이 필요하다.*/
static const size_t SECTORS_PER_PAGE = PGSIZE / BLOCK_SECTOR_SIZE; 
static size_t swap_size;
// swap table 을 초기화 한다.
void vm_swap_init(void){
    //ASSERT(SECTORS_PER_PAGE > 0);
    // swap disk 초기화
    swap_block = block_get_role(BLOCK_SWAP);
    if(swap_block != NULL){
        swap_size = block_size(swap_block) / SECTORS_PER_PAGE;
        available_swap = bitmap_create(swap_size);
        bitmap_set_all(available_swap, true);
    }
}
/* swap table에 mapping 된 swap disk 영역의 data를 memory로 load한다. (swap in)*/
void vm_swap_in(swap_index_t swap_idx, void *page){
    if(!bitmap_test(available_swap, swap_idx)){
        size_t i=0;
        while(i < SECTORS_PER_PAGE){
            size_t s=swap_idx * SECTORS_PER_PAGE + i;
            void* e=page + (BLOCK_SECTOR_SIZE * i);
            block_read(swap_block,s, e);
            i++;
        }
        bitmap_set(available_swap, swap_idx, true);// 이제 available
    }
}
/*page를 swap disk 에 write 하고 그 index를 반환 한다. (swap out) */
swap_index_t vm_swap_out(void *page){
    //ASSERT(page >= PHYS_BASE);
    size_t swap_idx = bitmap_scan(available_swap, 0 , 1, true); // 가능한 region을 scan.
    size_t i = 0;
    while(i < SECTORS_PER_PAGE){
        size_t e = swap_idx * SECTORS_PER_PAGE + i;
        void* s= page+(BLOCK_SECTOR_SIZE * i);
        block_write(swap_block,e,s );
        i++;
    }
    bitmap_set(available_swap, swap_idx, false); // not available 표시
    return swap_idx;
}


/* Free the swap region. */
void vm_swap_free(swap_index_t swap_index){
    //ASSERT(swap_index < swap_size);
    if(!bitmap_test(available_swap, swap_index)){
        bitmap_set(available_swap, swap_index, true);
    }
}

