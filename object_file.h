#ifndef OBJECT_FILE_H
#define OBJECT_FILE_H

typedef enum { NONE = -1, TEXT = 0, DATA, BSS, NUM_SEGMENTS } seg_type;

typedef struct {
  // This magic number identifies the file as being an object file
  unsigned int magic_number;
  // The size of the text segment in this object file (zero if none)
  unsigned int text_seg_size;
  // The size of the data segment in this object file (zero if none)
  unsigned int data_seg_size;
  // The size of the bss segment in this object file (zero if none)
  unsigned int bss_seg_size;
  // The number of relocation entries
  unsigned int num_references;
  // The size (in bytes) of the symbol name table
  unsigned int symbol_name_table_size;
} object_header;

typedef enum { GLOBAL_DATA,    // This defines a declared global data segment label
	       GLOBAL_TEXT,    // This defines a declared global text segment label
	       GLOBAL_BSS,     // This defines a declared global bss segment label
	       TEXT_LABEL_REF, // This is a reference to our own text segment
	       DATA_LABEL_REF, // This is a reference to our own data segment
	       BSS_LABEL_REF,  // This is a reference to our own bss segment
	       EXTERNAL_REF    // This is an unresolved (ie. external) reference
} reference_type;

typedef struct {
  unsigned int address;
  unsigned int symbol_ptr;

  reference_type type;
  seg_type source_seg;
} reloc_entry;

#define OBJ_MAGIC_NUM 0xdaa1

#endif
