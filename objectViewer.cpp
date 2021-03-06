/*
########################################################################
# This is an object viewer for wasm made object files.
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

#include <iostream>
#include <iomanip>
#include <fstream>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#include "object_file.h"
#include "instructions.h"

using namespace std;

bool error_flag = false, verbose_flag = false;

unsigned int starting_text_address = 0x00000, text_address, text_size = 0;
unsigned int data_address = 0xfffff, data_size = 0;
unsigned int bss_address = 0xfffff, bss_size = 0;
bool bss_end_justify = false;
int local_label_counter[] = {0,0,0};

const int max_label_length = 30;

struct label_entry
{
	char name[max_label_length];
	int address;
	seg_type segment;
	bool resolved;
	bool isGlobal;
	label_entry *next;
	int file_no;
};

label_entry *label_list = NULL;


// Remove all dynamically allocated data structures
void cleanup()
{
	while (label_list != NULL)
	{
		label_entry *temp = label_list->next;
		delete label_list;
		label_list = temp;
	}
}

// This searches for a reference to a label, creating a new entry
// if none is found
label_entry *get_label(char *name)
{
	//cerr << "looking for " << name << ": ";

	label_entry *temp = label_list;

	// Check for the label already existing
	while (temp != NULL)
	{
		//cerr << temp->address << ", "; 
		if (strcmp(temp->name, name) == 0){
			//cout << " FOUND" << endl;
			return temp;
		}
		temp = temp->next;
	}
	//cerr << " CREATING " << endl;

	temp = new label_entry;

	temp->next = label_list;
	temp->resolved = false;
	temp->file_no = 0;

	strcpy(temp->name, name);

	label_list = temp;

	return label_list;
}

// This searches for a reference to a label, creating a new entry
// if none is found
label_entry *get_label_address(int address, reference_type type)
{
	//cerr << "looking for " << address << ": ";

	label_entry *temp = label_list;

	//cout << temp->address;

	seg_type temp_seg;

	if (type == TEXT_LABEL_REF)
		temp_seg = TEXT;
	else if (type == DATA_LABEL_REF)
		temp_seg = DATA;
	else
		temp_seg = BSS;

	

	// Check for the label already existing
	while (temp != NULL)
	{
		//cerr << temp->address << ", "; 
		if (temp->address == address && temp_seg == temp->segment ){
			//cout << " FOUND" << endl;
			return temp;
		}
		temp = temp->next;
	}
	//cerr << " CREATING ";
	
	temp = new label_entry;

	temp->next = label_list;
	temp->resolved = true;
	temp->file_no = 0;
	temp->address = address;
			
	

	char base_string[] = "L.";
	char segNames[] = "TDB";
	base_string[0] = segNames[temp_seg];

	char buff [5]; 
	sprintf(buff, "%s%d", base_string, local_label_counter[temp_seg]++);

	strcpy(temp->name, buff);

	label_list = temp;

	// cerr << "created label {" << buff << "} @ 0x" << setw(5) << setfill('0') << hex << address << endl;

	return label_list;
}

struct reference
{
	// The label this refers to
	label_entry *label;
	seg_type source_seg;
	seg_type target_seg;
	int address;
	reference *next;
};

typedef struct
{
	char filename[300];
	object_header file_header;
	unsigned int *segment[NUM_SEGMENTS];
	// These hold the starting address of each segment
	unsigned int segment_address[NUM_SEGMENTS];

	reference *references;
} file_type;

void usage(char *progname)
{
	cerr << "USAGE: " << progname << "  file [options]\n";
	cerr << "\t '-d' display dissasembly" << endl;

	exit(1);
}

int main(int argc, char *argv[])
{
	int i;
	bool display_object = true;
	bool display_dissasemble = false;

	if (argc < 2)
		usage(argv[0]);

	// Here we must parse the arguments
	char *input_filename = NULL;

	for (i = 1; i < argc; i++)
	{
		///cout << "testing : " << argv[i] << endl;
		// Is this an option
		if (argv[i][0] == '-')
		{
			// This is the only valid option
			if (strcmp(argv[i], "-d") == 0)
			{
				display_dissasemble = true;
			}
			else
				usage(argv[0]);
		}
		else
		{
			// Otherwise it is a filename
			if (argv[i] == NULL)
			{
				usage(argv[0]);
			}
			input_filename = argv[i];
		}
	}

	file_type file;

	// Read in the data from all the files
	// Open the current file
	ifstream sourcefile;
	sourcefile.open(input_filename, ios::in | ios::binary);

	if (!sourcefile)
	{
		cerr << "ERROR: Could not open file for input : " << input_filename << endl;
		exit(1);
	}

	// Copy the filename into the structure
	strcpy(file.filename, input_filename);

	// Read the header in
	sourcefile.read((char *)&(file.file_header), sizeof(object_header));

	// Verify the magic number
	if (file.file_header.magic_number != OBJ_MAGIC_NUM)
	{
		cerr << "ERROR: File is not an object file : " << file.filename << endl;
		exit(1);
	}

	// The segments all start at zero
	file.segment_address[TEXT] = 0;
	file.segment_address[DATA] = 0;
	file.segment_address[BSS] = 0;
	file.references = NULL;

	// Now we allocate space for, and read the segments in
	file.segment[TEXT] = new unsigned int[file.file_header.text_seg_size];
	// Read in the text segment
	sourcefile.read((char *)file.segment[TEXT], (file.file_header.text_seg_size * sizeof(unsigned int)));
	file.segment[DATA] = new unsigned int[file.file_header.data_seg_size];
	// Read in the data segment
	sourcefile.read((char *)file.segment[DATA], (file.file_header.data_seg_size * sizeof(unsigned int)));

	// Increment the size counters
	text_size += file.file_header.text_seg_size;
	data_size += file.file_header.data_seg_size;
	bss_size += file.file_header.bss_seg_size;

	// Now we should read in all the labels for this segment
	int num_relocs = file.file_header.num_references;
	reloc_entry *relocation_array = new reloc_entry[num_relocs];
	sourcefile.read((char *)relocation_array, sizeof(reloc_entry) * num_relocs);

	// And the symbol labels
	char *symbol_names = new char[file.file_header.symbol_name_table_size];
	sourcefile.read(symbol_names, file.file_header.symbol_name_table_size);


	for(unsigned int i = 0; i < file.file_header.text_seg_size; i++){ //find br labels	
		if(((file.segment[TEXT][i]>>24) & 0xef) == 0xa0 
		
		){
			// cerr << "B-ins: @ 0x"<< setw(5) << setfill('0') << hex << i;
			// cerr << "\t0x"<< setw(8) << setfill('0') << hex << file.segment[TEXT][i];
			// cerr << "\t0x"<< setw(8) << setfill('0') << hex << ((((signed int)(file.segment[TEXT][i] << 12)) >> 12) + i +1) << endl;

			reference *new_ref = new reference;
			new_ref->source_seg = TEXT;
			new_ref->address = i;
			new_ref->label = get_label_address((((signed int)(file.segment[TEXT][i] << 12)) >> 12) + i +1, TEXT_LABEL_REF);
			new_ref->next = file.references;
			file.references = new_ref;
		}
	}


	// Scan through the segment labels
	for (i = 0; i < num_relocs; i++)
	{
		// Make a note of all the globals
		if (relocation_array[i].type == GLOBAL_TEXT || relocation_array[i].type == GLOBAL_DATA || relocation_array[i].type == GLOBAL_BSS)
		{
			// Create a new label entry for this global
			label_entry *temp = get_label(&(symbol_names[relocation_array[i].symbol_ptr]));			

			// Check for duplicate labels
			if (temp->resolved == true)
			{
				cerr << "ERROR: Duplicate label in file " << file.filename << ". '"
						<< &(symbol_names[relocation_array[i].symbol_ptr])
						<< "' already declared in file " << file.filename << endl;
				// We should really keep going to see if other errors arise, and bail out later
				error_flag = true;
				exit(1);
			}

			temp->isGlobal = true;

			// Mark this as being resolved
			temp->resolved = true;
			// Fill in the address field
			temp->address = relocation_array[i].address;

			// Fill in the segment field

			//cerr <<temp->name << endl;

			if (relocation_array[i].type == GLOBAL_TEXT)
				temp->segment = TEXT;
			else if (relocation_array[i].type == GLOBAL_DATA)
				temp->segment = DATA;
			else
				temp->segment = BSS;

		}
		else if (relocation_array[i].type == EXTERNAL_REF)
		{
			// Create a label entry for this reference
			label_entry *temp = get_label(&(symbol_names[relocation_array[i].symbol_ptr]));
			temp-> isGlobal = false;
			// Only allowed external references from the text segment for now
			assert(relocation_array[i].source_seg == TEXT || relocation_array[i].source_seg == DATA);

			// Add a new reference to the list from this file
			reference *new_ref = new reference;
			new_ref->label = temp;

			if (relocation_array[i].type == GLOBAL_TEXT) //TODO
				temp->segment = TEXT;
			else if (relocation_array[i].type == GLOBAL_DATA)
				temp->segment = DATA;
			else
				temp->segment = BSS;

			new_ref->address = relocation_array[i].address;
			new_ref->next = file.references;
			new_ref->source_seg = relocation_array[i].source_seg;
			file.references = new_ref;

			//cerr <<temp->name << endl;
		}
		else
		{
			// Must be an internal reference that requires relocating
			// This won't have a label, and could refer to either the data, or text segment

			reference *new_ref = new reference;
			new_ref->source_seg = relocation_array[i].source_seg;
			new_ref->address = relocation_array[i].address;
			new_ref->next = file.references;

			label_entry *temp = get_label_address(file.segment[TEXT][relocation_array[i].address]&0xfffff,relocation_array[i].type);

			new_ref->label = temp;
			

			//cerr <<temp->name << endl;

			if (relocation_array[i].type == TEXT_LABEL_REF)
				new_ref->label->segment = TEXT;
			else if (relocation_array[i].type == DATA_LABEL_REF)
				new_ref->label->segment  = DATA;
			else if (relocation_array[i].type == GLOBAL_TEXT)
				temp->segment = TEXT;
			else
				new_ref->label->segment  = BSS;

			file.references = new_ref;
		}
	}

	label_entry * currLabel = label_list;
	reference * currRef = file.references;

	if(display_object){
		cout << setw(45) << setfill('#') << "#" << endl;
		//basic object file information
		cout << "# File name:      " << file.filename << endl;
		cout << "# Text size:      " << setw(5) << setfill(' ') << hex << file.file_header.text_seg_size          << endl;
		cout << "# Data Size:      " << setw(5) << setfill(' ') << hex << file.file_header.data_seg_size          << endl;
		cout << "# Bss Size:       " << setw(5) << setfill(' ') << hex << file.file_header.bss_seg_size           << endl;

		//global and external references
		
		cout << "#" << endl << "# LABEL_LIST" << endl;
		cout << "#" << setw(15) << setfill(' ') << "Label Name";
		cout << ", " << setw(8) << setfill(' ') << "Location";
		cout << ", " << setw(7) << setfill(' ') << "Address";	
		cout << ", " << setw(8) << setfill(' ') << "Segment";
		cout << endl;
		while(currLabel != NULL){
			if (currLabel->isGlobal == 1){
				cout << "#" << setw(15) << setfill(' ') << currLabel->name;
				cout << ", " << setw(8) << setfill(' ')<< "GLOBAL";
				cout << ", 0x" << setw(5) << setfill('0') << hex << currLabel->address;
				cout << ", " << setw(8) << setfill(' ') << seg_type_name[currLabel->segment + 1];
				cout << endl;
			} else if (!currLabel->resolved){
				cout << "#" << setw(15) << setfill(' ') << currLabel->name;
				cout << ", " << setw(8) << setfill(' ')<< "EXTERNAL";
				cout << ", 0x" << setw(5) << setfill('?') << "";
				cout << endl;
			} else if (false){
				cout << "#" << setw(15) << setfill(' ') << currLabel->name;
				cout << ", " << setw(8) << setfill(' ')<< "LOCAL";
				cout << ", 0x" << setw(5) << setfill('0') << hex << currLabel->address;
				cout << ", " << setw(8) << setfill(' ') << seg_type_name[currLabel->segment + 1];
				cout << endl;
			}
			currLabel = currLabel->next;	
		}
		cout << setw(45) << setfill('#') << "#" << endl;
	}

	if(display_dissasemble){
		cout << endl << ".text # size: 0x" << setw(5) << setfill('0') << hex << file.file_header.text_seg_size << endl;
		for(unsigned int i = 0; i < file.file_header.text_seg_size; i++){ //print TEXT		

			currLabel = label_list;
			while(currLabel != NULL){	//handle label markers
				//cout << " testsing: " << currLabel->name;
				if ((unsigned int)currLabel->address == i && currLabel->segment == TEXT){
					if (currLabel->isGlobal) cout << ".global " << currLabel->name << endl;
					cout << currLabel->name << ":" << endl;
					break;
				}		
				currLabel = currLabel->next;	
			}

			//replace references to labels
			currRef = file.references;
			char * temp_name = NULL; //seg_type_name[currRef->target_seg+1];
			while(currRef != NULL){		
				label_entry * currLabel = currRef->label;	
				if(currLabel != NULL){
					if ((unsigned int)currRef->address == i){				
						temp_name = currLabel->name;
						break;
					}
				}		
				currRef = currRef->next;	
			}
			cout << "\t";
			disassemble_view(i,file.segment[TEXT][i], temp_name);
			
			cout << endl;
		}

		cout << endl << ".data # size: 0x" << setw(5) << setfill('0') << hex << file.file_header.data_seg_size << endl;
		for(unsigned int i = 0; i < file.file_header.data_seg_size; i++){ //print DATA

			currLabel = label_list;
			while(currLabel != NULL){	//handle label markers	
				//cout << " testsing: " << currLabel->name;
				if ((unsigned int)currLabel->address == i && currLabel->segment == DATA){
					cout << currLabel->name << ":" << endl;
					break;
				}		
				currLabel = currLabel->next;	
			}

			// If the .word contains a value in the printable character range,
			// add a comment that shows the character.
			if (file.segment[DATA][i] >= 20 && file.segment[DATA][i] < 127)
			{
				cout << "\t.word\t0x" << setw(8) << setfill('0') << hex << file.segment[DATA][i] << "\t# '" << (char)file.segment[DATA][i] << "'" << endl;
			}
			else
			{
				cout << "\t.word\t0x" << setw(8) << setfill('0') << hex << file.segment[DATA][i] << endl;
			}
		}

		cout << endl << ".bss # size: 0x" << setw(5) << setfill('0') << hex << file.file_header.bss_seg_size << endl;		
		
		label_entry * bss_entry = NULL;
		label_entry * last_entry = NULL;
		currLabel = label_list;
		int lastAddress = -1;
		int addressItr = 0xfffff;
		while(currLabel != NULL){
			if(currLabel->segment == BSS && currLabel->resolved){ //if the current label is a BSS pointer
				if(currLabel->address <= addressItr && currLabel->address > lastAddress){
					addressItr = currLabel->address;
					last_entry = currLabel;
				}
			}
			currLabel = currLabel->next;
		}

		lastAddress = 0;
		addressItr = 0xfffff;
		for(int i = 0; i < local_label_counter[2]; i++){
			
			

			currLabel = label_list;
			while(currLabel != NULL){ 
				if(currLabel->segment == BSS && currLabel->resolved){ //if the current label is a BSS pointer
					if(currLabel->address <= addressItr && currLabel->address > lastAddress){
						addressItr = currLabel->address;
						bss_entry = currLabel;
					}
				}
				currLabel = currLabel->next;
			}			
			addressItr = 0xfffff;
			
			cout << last_entry->name << ":" << endl;			
			if(bss_entry == NULL){
				cout << "\t.space 0x" << setw(5) << setfill('0') <<  hex << file.file_header.bss_seg_size <<endl;
				break;
			}
			else if ((bss_entry->address - lastAddress) != 0 )
				cout << "\t.space 0x" << setw(5) << setfill('0') <<  hex << (bss_entry->address - lastAddress) <<endl;
			
			lastAddress = bss_entry->address;
			
			

			last_entry = bss_entry;
		}
	}
	
	cleanup();
	return 0;
}
