/*
########################################################################
# This file is part of the toolchain for WRAMP assembly
#
# Copyright (C) 2019 The University of Waikato, Hamilton, New Zealand.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
########################################################################
*/

#ifndef OBJECT_FILE_H
#define OBJECT_FILE_H

typedef enum { NONE = -1, TEXT = 0, DATA, BSS, NUM_SEGMENTS } seg_type;
char * seg_type_name[] = {"NONE", "TEXT", "DATA", "BSS", "NUM_SEGMENTS"};

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

typedef enum {
		GLOBAL_DATA,    // This defines a declared global data segment label
		GLOBAL_TEXT,    // This defines a declared global text segment label
		GLOBAL_BSS,     // This defines a declared global bss segment label
		TEXT_LABEL_REF, // This is a reference to our own text segment
		DATA_LABEL_REF, // This is a reference to our own data segment
		BSS_LABEL_REF,  // This is a reference to our own bss segment
		EXTERNAL_REF    // This is an unresolved (ie. external) reference
} reference_type;

char * reference_type_name[7] ={ 
		"GLOBAL_DATA",    // This defines a declared global data segment label
		"GLOBAL_TEXT",    // This defines a declared global text segment label
		"GLOBAL_BSS",     // This defines a declared global bss segment label
		"TEXT_LABEL_REF", // This is a reference to our own text segment
		"DATA_LABEL_REF", // This is a reference to our own data segment
		"BSS_LABEL_REF",  // This is a reference to our own bss segment
		"EXTERNAL_REF"    // This is an unresolved (ie. external) reference
};

typedef struct {
  unsigned int address;
  unsigned int symbol_ptr;

  reference_type type;
  seg_type source_seg;
} reloc_entry;

#define OBJ_MAGIC_NUM 0xdaa1

#endif
