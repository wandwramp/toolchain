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
	cerr << "USAGE: " << progname << "  file\n";
	exit(1);
}

int main(int argc, char *argv[])
{
	int i;
	
	char output_filename[300] = {0};

	if (argc != 2)
		usage(argv[0]);

	// Here we must parse the arguments
	char *input_filename;

			// Otherwise it is a filename
	input_filename = argv[1];	

	if (output_filename[0] == '\0')
	{
		// default to link.out
		strcpy(output_filename, "link.out");
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
		// cerr << setw(3) << setfill(' ') << i;
		// cerr << ", 0x"<< setw(5) << setfill('0') << hex << relocation_array[i].address;
		// //cerr << ", " << setw(15) << setfill(' ') << &(symbol_names[relocation_array[i].symbol_ptr]);
		// cerr << ", " << setw(15) << setfill(' ') << reference_type_name[relocation_array[i].type] << ", ";
		// //cerr << endl;


		// typedef struct {
		// unsigned int address;
		// unsigned int symbol_ptr;

		// reference_type type;
		// seg_type source_seg;
		// } reloc_entry;




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
			

			
			// temp->address = relocation_array[i].address;
			// temp->segment = TEXT;
			//temp->isGlobal = false;
			// temp->resolved = true;
			// temp->next = NULL;
			// temp->file_no = 0;

			new_ref->label = temp;
			

			//cerr <<temp->name << endl;

			if (relocation_array[i].type == TEXT_LABEL_REF)
				new_ref->label->segment = TEXT;
			else if (relocation_array[i].type == DATA_LABEL_REF)
				new_ref->label->segment  = DATA;
			else if (relocation_array[i].type == GLOBAL_TEXT) //TODO
				temp->segment = TEXT;
			else
				new_ref->label->segment  = BSS;

			file.references = new_ref;
		}
	}


	cout << setw(45) << setfill('#') << "#" << endl;
	//basic object file information
	cout << "# File name:      " << file.filename << endl;
	cout << "# Text size:      " << setw(5) << setfill(' ') << hex << file.file_header.text_seg_size          << endl;
	cout << "# Data Size:      " << setw(5) << setfill(' ') << hex << file.file_header.data_seg_size          << endl;
	cout << "# Bss Size:       " << setw(5) << setfill(' ') << hex << file.file_header.bss_seg_size           << endl;

	//global and external references
	label_entry * currLabel = label_list;
	cout << "#" << endl << "# LABEL_LIST" << endl;
	cout << "#" << setw(15) << setfill(' ') << "Label Name";
	cout << ", " << setw(8) << setfill(' ') << "location";
	cout << ", " << setw(7) << setfill(' ') << "Address";	
	cout << ", " << setw(8) << setfill(' ') << "segment";
	cout << endl;
	while(currLabel != NULL){
#define SHOW_LOCAL 0
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
		} else if (SHOW_LOCAL){
			cout << "#" << setw(15) << setfill(' ') << currLabel->name;
			cout << ", " << setw(8) << setfill(' ')<< "LOCAL";
			cout << ", 0x" << setw(5) << setfill('0') << hex << currLabel->address;
			cout << ", " << setw(8) << setfill(' ') << seg_type_name[currLabel->segment + 1];
			cout << endl;
		}
		currLabel = currLabel->next;	
	}



	// cout << "RELOCATION ARRAY" << endl;
	// cout << "\t" << setw(5) << setfill(' ') << hex << "address";
	// cout << "," << setw(15) << setfill(' ') << "type";
	// cout << "," << setw(12) << setfill(' ') << "source_seg";
	// cout << "," << setw(16) << setfill(' ') << "symbol_names" << endl;

	// for (int i = 0; i < num_relocs; i++){
	// 	cout << "\t0x"<< setw(5) << setfill('0') << hex << relocation_array[i].address;
	// 	cout << "," << setw(15) << setfill(' ') << reference_type_name[relocation_array[i].type];
	// 	cout << "," << setw(12) << setfill(' ') << hex << seg_type_name[relocation_array[i].source_seg+1];
	// 	cout << "," << setw(16) << setfill(' ') << & symbol_names[relocation_array[i].symbol_ptr] << endl;
	// }

	reference * currRef = file.references;

	// cout << "#" << endl;
	// cout << "# REFERENCE_LIST" << endl;
	// cout << setw(5) << setfill(' ') << "from Addr";
	// cout << ", " << setw(15) << setfill(' ') << "name";
	// cout << ", " << setw(8) << setfill(' ') << "To Addr";
	// cout << endl;	
	// while(currRef != NULL){		
	// 	label_entry * currLabel = currRef->label;		
	// 	cout << "# " << setw(4) << setfill(' ') <<"0x"<< setw(5) << setfill('0') << currRef->address;		
	// 	if(currLabel != NULL){
	// 		cout << ", " << setw(15) << setfill(' ') << currLabel->name;
	// 	}
	// 	cout << ", " << setw(3) << setfill(' ') <<"0x" << setw(5) << setfill('0') << hex << (file.segment[TEXT][currRef->address] & 0xfffff);
	// 	cout << endl;		
	// 	currRef = currRef->next;	
	// }

	cout << setw(45) << setfill('#') << "#" << endl;
	cout << endl << ".text # size: 0x" << setw(5) << setfill('0') << hex << file.file_header.text_seg_size << endl;
	for(unsigned int i = 0; i < file.file_header.text_seg_size; i++){ //print TEXT		

		currLabel = label_list;
		while(currLabel != NULL){	//handle label markers	

			//cout << " testsing: " << currLabel->name;
			if (currLabel->address == i && currLabel->segment == TEXT){
				if (currLabel->isGlobal) cout << ".global " << currLabel->name << endl;
				cout << currLabel->name << ":" << endl;
				break;
			}		
			currLabel = currLabel->next;	
		}

		//cout << "\t#0x" << setw(5) << setfill('0') << hex << i <<  ": 0x"<< setw(8) << setfill('0') << hex << file.segment[TEXT][i] << "\t";		

		//replace references to labels
		currRef = file.references;
		char * temp_name = NULL; //seg_type_name[currRef->target_seg+1];
		while(currRef != NULL){		
			//cerr << " 0x" << setw(5) << setfill('0') << hex << (file.segment[TEXT][currRef->address] & 0xfffff);	
			label_entry * currLabel = currRef->label;	
			if(currLabel != NULL){
				if (currRef->address == i){ //&& (file.segment[TEXT][currRef->address] & 0xfffff) == currLabel->address){				
					temp_name = currLabel->name;
					break;
				}
			}		
			currRef = currRef->next;	
		}
		cout << "\t";
		disassemble_view(i,file.segment[TEXT][i], temp_name); //TODO revamp this
		
		//if(currRef != NULL)cerr << currRef->address << endl;		
		
		cout << endl;
	}

	cout << endl << ".data # size: 0x" << setw(5) << setfill('0') << hex << file.file_header.data_seg_size << endl;
	for(unsigned int i = 0; i < file.file_header.data_seg_size; i++){ //print DATA

		currLabel = label_list;
		while(currLabel != NULL){	//handle label markers	
			//cout << " testsing: " << currLabel->name;
			if (currLabel->address == i && currLabel->segment == DATA){
				cout << currLabel->name << ":" << endl;
				break;
			}		
			currLabel = currLabel->next;	
		}
		cout << "\t.word\t0x" << setw(8) << setfill('0') << hex << file.segment[DATA][i] << endl;
	}


	cout << endl << ".bss # size: 0x" << setw(5) << setfill('0') << hex << file.file_header.bss_seg_size << endl;
	
	
	label_entry * bss_entry = NULL;
	label_entry * last_entry = NULL;
	int lastAddress = -1;
	int addressItr = 0xfffff;

	while(currLabel != NULL){ //print BSS
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
		while(currLabel != NULL){ //print BSS
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
		
		if ((bss_entry->address - lastAddress) != 0 )
			cout << "\t.space 0x" << setw(5) << setfill('0') <<  hex << (bss_entry->address - lastAddress) <<endl;
		lastAddress = bss_entry->address;
		last_entry = bss_entry;
	}







	
	cleanup();
	return 0;
}
