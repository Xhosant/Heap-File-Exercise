#ifndef HP_FILE_STRUCTS_H
#define HP_FILE_STRUCTS_H

#include <record.h>

/**
 * @file hp_file_structs.h
 * @brief Data structures for heap file management
 */

/* -------------------------------------------------------------------------- */
/*                              Data Structures                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Heap file header containing metadata about the file organization
 */
typedef struct HeapFileHeader {
     char magic[8];           // Magic number
    int version;             // έκδοση
    int total_records;       // συνολικές εγγραφές στο αρχείο
    int records_per_block;   // πόσες εγγραφές χωράει κάθε block
} HeapFileHeader;

/**
 * @brief Iterator for scanning through records in a heap file
 */
typedef struct HeapFileIterator{
    int file_handle;   //αναγνωριστικό αρχείου BF
    HeapFileHeader *header;   //δείκτης στα metadata
    int filter_id;       //-1 για όλα, ή συγκεκριμένο id
    int current_block;        // ποιο block διαβάζεται τώρα
    int current_index_in_block;    //index εγγραφής μέσα στο block
    int blocks_total;     // πόσα blocks υπάρχουν συνολικά
} HeapFileIterator;

#endif /* HP_FILE_STRUCTS_H */
