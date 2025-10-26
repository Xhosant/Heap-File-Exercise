#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf.h"
#include "hp_file_structs.h"
#include "record.h"

#define CALL_BF(call)         \
  {                           \
    BF_ErrorCode code = call; \
    if (code != BF_OK)        \
    {                         \
      BF_PrintError(code);    \
      return 0;        \
    }                         \
  }

 //Υπολογίζει πόσες εγγραφές χωρά κάθε block 
static int calc_records_per_block(void) {
  int cap = (BF_BLOCK_SIZE - sizeof(int)) / sizeof(Record);
  return (cap > 0) ? cap : 1;
}

// Διαβάζει το header από το block 0 
static int read_header(int fd, HeapFileHeader* h) {
  BF_Block* block = NULL;
  BF_Block_Init(&block);
  CALL_BF_BOOL(BF_GetBlock(fd, 0, block));
  memcpy(h, BF_Block_GetData(block), sizeof(*h));
  CALL_BF_BOOL(BF_UnpinBlock(block));
  BF_Block_Destroy(&block);
  return (h->magic[0] != '\0');
}

/* Εγγράφει το header στο block 0 */
static int write_header(int fd, const HeapFileHeader* h) {
  BF_Block* block = NULL;
  BF_Block_Init(&block);
  CALL_BF_BOOL(BF_GetBlock(fd, 0, block));
  memcpy(BF_Block_GetData(block), h, sizeof(*h));
  BF_Block_SetDirty(block);
  CALL_BF_BOOL(BF_UnpinBlock(block));
  BF_Block_Destroy(&block);
  return 1;
}

//Επιστρέφει το πλήθος blocks 
static int get_block_count(int fd, int* out) {
  CALL_BF_BOOL(BF_GetBlockCounter(fd, out));
  return 1;
}

// Δημιουργεί νέο data block με count=0 
static int init_data_block(int fd) {
  BF_Block* block = NULL;
  BF_Block_Init(&block);
  CALL_BF_BOOL(BF_AllocateBlock(fd, block));
  int zero = 0;
  memcpy(BF_Block_GetData(block), &zero, sizeof(int));
  BF_Block_SetDirty(block);
  CALL_BF_BOOL(BF_UnpinBlock(block));
  BF_Block_Destroy(&block);
  return 1;
}


// Δημιουργία αρχείου Heap

int HeapFile_Create(const char* fileName) {
  CALL_BF_BOOL(BF_CreateFile(fileName));
  int fd = -1;
  CALL_BF_BOOL(BF_OpenFile(fileName, &fd));

  BF_Block* block = NULL;
  BF_Block_Init(&block);
  CALL_BF_BOOL(BF_AllocateBlock(fd, block));

  HeapFileHeader h;
  memset(&h, 0, sizeof(h));
  strncpy(h.magic, "HPF1", sizeof(h.magic)-1);
  h.version = 1;
  h.total_records = 0;
  h.records_per_block = calc_records_per_block();

  memcpy(BF_Block_GetData(block), &h, sizeof(h));
  BF_Block_SetDirty(block);

  CALL_BF_BOOL(BF_UnpinBlock(block));
  BF_Block_Destroy(&block);
  CALL_BF_BOOL(BF_CloseFile(fd));
  return 1;
}


// Άνοιγμα αρχείου Heap                                      

int HeapFile_Open(const char *fileName, int *file_handle, HeapFileHeader** header_info) {
  CALL_BF_BOOL(BF_OpenFile(fileName, file_handle));

  HeapFileHeader temp;
  if (!read_header(*file_handle, &temp)) { BF_CloseFile(*file_handle); return 0; }

  HeapFileHeader* h = malloc(sizeof(HeapFileHeader));
  if (!h) { BF_CloseFile(*file_handle); return 0; }
  *h = temp;
  *header_info = h;
  return 1;
}


// Κλείσιμο αρχείου Heap                                      

int HeapFile_Close(int file_handle, HeapFileHeader *hp_info) {
  if (hp_info) write_header(file_handle, hp_info);
  CALL_BF_BOOL(BF_CloseFile(file_handle));
  return 1;
}


// Εισαγωγή εγγραφής                                          

int HeapFile_InsertRecord(int file_handle, HeapFileHeader *hp_info, const Record record) {
  int blocks = 0;
  if (!get_block_count(file_handle, &blocks)) return 0;
  if (blocks == 1 && !init_data_block(file_handle)) return 0;
  if (!get_block_count(file_handle, &blocks)) return 0;

  int last = blocks - 1;
  BF_Block* block = NULL;
  BF_Block_Init(&block);
  CALL_BF_BOOL(BF_GetBlock(file_handle, last, block));

  char* data = BF_Block_GetData(block);
  int* pcount = (int*)data;
  Record* recs = (Record*)(data + sizeof(int));

  if (*pcount < hp_info->records_per_block) {
    recs[*pcount] = record;
    (*pcount)++;
    BF_Block_SetDirty(block);
  } else {
    CALL_BF_BOOL(BF_UnpinBlock(block));
    BF_Block_Destroy(&block);
    if (!init_data_block(file_handle)) return 0;
    if (!get_block_count(file_handle, &blocks)) return 0;
    last = blocks - 1;
    BF_Block_Init(&block);
    CALL_BF_BOOL(BF_GetBlock(file_handle, last, block));
    data = BF_Block_GetData(block);
    pcount = (int*)data;
    recs = (Record*)(data + sizeof(int));
    recs[0] = record;
    *pcount = 1;
    BF_Block_SetDirty(block);
  }

  CALL_BF_BOOL(BF_UnpinBlock(block));
  BF_Block_Destroy(&block);

  hp_info->total_records++;
  write_header(file_handle, hp_info);
  return 1;
}


// Δημιουργία iterator                                         
HeapFileIterator HeapFile_CreateIterator(int file_handle, HeapFileHeader* header_info, int id) {
  HeapFileIterator it;
  memset(&it, 0, sizeof(it));
  it.file_handle = file_handle;
  it.header = *header_info;
  it.filter_id = id;
  it.current_block = 1;
  it.current_index_in_block = -1;
  get_block_count(file_handle, &it.blocks_total);
  return it;
}


// Επόμενη εγγραφή                                            

int HeapFile_GetNextRecord(HeapFileIterator* it, Record** record) {
  static Record temp;
  *record = NULL;

  while (it->current_block < it->blocks_total) {
    BF_Block* block = NULL;
    BF_Block_Init(&block);
    if (BF_GetBlock(it->file_handle, it->current_block, block) != BF_OK) { BF_Block_Destroy(&block); return 0; }

    char* data = BF_Block_GetData(block);
    int count = *(int*)data;
    Record* recs = (Record*)(data + sizeof(int));

    int start = it->current_index_in_block + 1;
    for (int i = start; i < count; ++i) {
      if (it->filter_id < 0 || recs[i].id == it->filter_id) {
        temp = recs[i];
        *record = &temp;
        it->current_index_in_block = i;
        BF_UnpinBlock(block);
        BF_Block_Destroy(&block);
        return 1;
      }
    }

    it->current_block++;
    it->current_index_in_block = -1;
    BF_UnpinBlock(block);
    BF_Block_Destroy(&block);
  }
  return 0;
}
