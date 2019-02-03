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

const int max_label_length = 30;

struct label_entry
{
	char name[max_label_length];
	int address;
	seg_type segment;
	bool resolved;
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

void output_srecord(ofstream &ofile, int record_type, unsigned int address, int *data, int num_words)
{
	unsigned char checksum = 0;
	unsigned char length = 0;

	assert((record_type == 3 && num_words > 0) || (record_type == 7 && num_words == 0));

	ofile << "S" << record_type;

	length = 4 + (4 * num_words) + 1;

	checksum += length;

	ofile << hex << setiosflags(ios::uppercase) << setw(2) << setfill('0') << ((unsigned int)length);

	checksum += (address >> 24) & 0xff;
	checksum += (address >> 16) & 0xff;
	checksum += (address >> 8) & 0xff;
	checksum += address & 0xff;

	ofile << hex << setiosflags(ios::uppercase) << setw(8) << setfill('0') << address;

	for (int i = 0; i < num_words; i++)
	{
		checksum += (data[i] >> 24) & 0xff;
		checksum += (data[i] >> 16) & 0xff;
		checksum += (data[i] >> 8) & 0xff;
		checksum += data[i] & 0xff;

		ofile << hex << setiosflags(ios::uppercase) << setw(8) << setfill('0') << data[i];
	}

	ofile << hex << setiosflags(ios::uppercase) << setw(2) << setfill('0') << ((~checksum) & 0xff) << endl;
}

// This searches for a reference to a label, creating a new entry
// if none is found
label_entry *get_label(char *name)
{
	label_entry *temp = label_list;

	// Check for the label already existing
	while (temp != NULL)
	{
		if (strcmp(temp->name, name) == 0)
			return temp;
		temp = temp->next;
	}

	temp = new label_entry;

	temp->next = label_list;
	temp->resolved = false;
	temp->file_no = 0;

	strcpy(temp->name, name);

	label_list = temp;

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
	cerr << "USAGE: " << progname << " [-Ttext address] [-Tdata address] [-[T|E]bss address] [-v] [-o output] file1 file2 ...\n";
	exit(1);
}

int main(int argc, char *argv[])
{
	int i;
	char *endptr = NULL;
	char output_filename[300] = {0};

	if (argc < 2)
		usage(argv[0]);

	// Here we must parse the arguments
	typedef char *char_p;
	char **input_filename = new char_p[argc];
	int num_files = 0;
	for (i = 1; i < argc; i++)
	{
		// Is this an option
		if (argv[i][0] == '-')
		{
			// This is the only valid option for now
			if (strcmp(argv[i], "-o") == 0)
			{
				if ((i + 1) == argc)
					usage(argv[0]);

				// Redefinition of the output file
				if (output_filename[0] != '\0')
					usage(argv[0]);

				i++;
				strcpy(output_filename, argv[i]);
			}
			else if (strcmp(argv[i], "-Ttext") == 0)
			{
				if ((i + 1) == argc)
					usage(argv[0]);
				i++;

				text_address = (starting_text_address = strtol(argv[i], &endptr, 0));

				if (*endptr != 0)
					usage(argv[0]);
			}
			else if (strcmp(argv[i], "-Tdata") == 0)
			{
				if ((i + 1) == argc)
					usage(argv[0]);
				i++;

				data_address = strtol(argv[i], &endptr, 0);

				if (*endptr != 0)
					usage(argv[0]);
			}
			else if (strcmp(argv[i], "-Tbss") == 0)
			{
				if ((i + 1) == argc)
					usage(argv[0]);
				i++;

				bss_address = strtol(argv[i], &endptr, 0);

				if (*endptr != 0)
					usage(argv[0]);
			}
			else if (strcmp(argv[i], "-Ebss") == 0)
			{
				if ((i + 1) == argc)
					usage(argv[0]);
				i++;

				bss_address = strtol(argv[i], &endptr, 0);
				bss_end_justify = true;

				if (*endptr != 0)
					usage(argv[0]);
			}
			else if (strcmp(argv[i], "-v") == 0)
			{
				verbose_flag = true;
			}
			else
				usage(argv[0]);
		}
		else
		{
			// Otherwise it is a filename
			input_filename[num_files] = argv[i];
			num_files++;
		}
	}

	if (num_files == 0)
		usage(argv[0]);

	if (output_filename[0] == '\0')
	{
		// default to link.out
		strcpy(output_filename, "link.out");
	}

	// Setup the linker special symbols
	label_entry *bss_size_symbol = get_label("bss_size");
	bss_size_symbol->resolved = true;
	bss_size_symbol->file_no = -1;
	label_entry *text_size_symbol = get_label("text_size");
	text_size_symbol->resolved = true;
	text_size_symbol->file_no = -1;
	label_entry *data_size_symbol = get_label("data_size");
	data_size_symbol->resolved = true;
	data_size_symbol->file_no = -1;

	int current_file = 0;

	file_type *file = new file_type[num_files];

	// Read in the data from all the files
	for (current_file = 0; current_file < num_files; current_file++)
	{
		// Open the current file
		ifstream sourcefile;
		sourcefile.open(input_filename[current_file], ios::in | ios::binary);

		if (!sourcefile)
		{
			cerr << "ERROR: Could not open file for input : " << input_filename[current_file] << endl;
			exit(1);
		}

		// Copy the filename into the structure
		strcpy(file[current_file].filename, input_filename[current_file]);

		// Read the header in
		sourcefile.read((char *)&(file[current_file].file_header), sizeof(object_header));

		// Verify the magic number
		if (file[current_file].file_header.magic_number != OBJ_MAGIC_NUM)
		{
			cerr << "ERROR: File is not an object file : " << file[current_file].filename << endl;
			exit(1);
		}

		// The segments all start at zero
		file[current_file].segment_address[TEXT] = 0;
		file[current_file].segment_address[DATA] = 0;
		file[current_file].segment_address[BSS] = 0;
		file[current_file].references = NULL;

		// Now we allocate space for, and read the segments in
		file[current_file].segment[TEXT] = new unsigned int[file[current_file].file_header.text_seg_size];
		// Read in the text segment
		sourcefile.read((char *)file[current_file].segment[TEXT], (file[current_file].file_header.text_seg_size * sizeof(unsigned int)));
		file[current_file].segment[DATA] = new unsigned int[file[current_file].file_header.data_seg_size];
		// Read in the data segment
		sourcefile.read((char *)file[current_file].segment[DATA], (file[current_file].file_header.data_seg_size * sizeof(unsigned int)));

		// Increment the size counters
		text_size += file[current_file].file_header.text_seg_size;
		data_size += file[current_file].file_header.data_seg_size;
		bss_size += file[current_file].file_header.bss_seg_size;

		// Now we should read in all the labels for this segment
		int num_relocs = file[current_file].file_header.num_references;
		reloc_entry *relocation_array = new reloc_entry[num_relocs];
		sourcefile.read((char *)relocation_array, sizeof(reloc_entry) * num_relocs);

		// And the symbol labels
		char *symbol_names = new char[file[current_file].file_header.symbol_name_table_size];
		sourcefile.read(symbol_names, file[current_file].file_header.symbol_name_table_size);

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
					cerr << "ERROR: Duplicate label in file " << file[current_file].filename << ". '"
						 << &(symbol_names[relocation_array[i].symbol_ptr])
						 << "' already declared in file " << file[temp->file_no].filename << endl;
					// We should really keep going to see if other errors arise, and bail out later
					error_flag = true;
					exit(1);
				}
				// Mark this as being resolved
				temp->resolved = true;
				// Fill in the address field
				temp->address = relocation_array[i].address;

				// Fill in the segment field
				if (relocation_array[i].type == GLOBAL_TEXT)
					temp->segment = TEXT;
				else if (relocation_array[i].type == GLOBAL_DATA)
					temp->segment = DATA;
				else
					temp->segment = BSS;

				temp->file_no = current_file;
			}
			else if (relocation_array[i].type == EXTERNAL_REF)
			{
				// Create a label entry for this reference
				label_entry *temp = get_label(&(symbol_names[relocation_array[i].symbol_ptr]));
				// Only allowed external references from the text segment for now
				assert(relocation_array[i].source_seg == TEXT || relocation_array[i].source_seg == DATA);

				// Add a new reference to the list from this file
				reference *new_ref = new reference;
				new_ref->label = temp;
				new_ref->address = relocation_array[i].address;
				new_ref->next = file[current_file].references;
				new_ref->source_seg = relocation_array[i].source_seg;
				file[current_file].references = new_ref;
			}
			else
			{
				// Must be an internal reference that requires relocating
				// This won't have a label, and could refer to either the data, or text segment
				reference *new_ref = new reference;
				new_ref->source_seg = relocation_array[i].source_seg;
				new_ref->address = relocation_array[i].address;
				new_ref->label = NULL;
				new_ref->next = file[current_file].references;

				if (relocation_array[i].type == TEXT_LABEL_REF)
					new_ref->target_seg = TEXT;
				else if (relocation_array[i].type == DATA_LABEL_REF)
					new_ref->target_seg = DATA;
				else
					new_ref->target_seg = BSS;

				file[current_file].references = new_ref;
			}
		}
	}

	// Bail out if we had an error
	if (error_flag == true)
		exit(1);

	// check for end justify on the bss
	if (bss_end_justify == true)
		bss_address -= bss_size;

	// Now we setup the segment addresses
	text_address = starting_text_address;

	// Text segment first, then data
	for (i = 0; i < NUM_SEGMENTS; i++)
	{
		// Loop through the files
		for (int j = 0; j < num_files; j++)
		{

			// Increment for the next segment
			if (i == TEXT)
			{
				file[j].segment_address[i] = text_address;
				text_address += file[j].file_header.text_seg_size;
			}
			else if (i == DATA)
			{
				if (data_address == 0xfffff)
				{
					// The data segment follows on from the text segment
					file[j].segment_address[i] = text_address;
					text_address += file[j].file_header.data_seg_size;
				}
				else
				{
					file[j].segment_address[i] = data_address;
					data_address += file[j].file_header.data_seg_size;
				}
			}
			else
			{
				if (bss_address == 0xfffff)
				{
					// The bss segment follows on from the text segment
					file[j].segment_address[i] = text_address;
					text_address += file[j].file_header.bss_seg_size;
				}
				else
				{
					file[j].segment_address[i] = bss_address;
					bss_address += file[j].file_header.bss_seg_size;
				}
			}
		}
	}

	// Set the global symbol values
	bss_size_symbol->address = bss_size;
	text_size_symbol->address = text_size;
	data_size_symbol->address = data_size;

	//check for segment overlaps





	// Now all the segment addresses have been set, we update all the references
	for (i = 0; i < num_files; i++)
	{
		reference *walk = file[i].references;

		while (walk != NULL)
		{
			unsigned int resolved_address;

			if (walk->label == NULL)
			{
				// Local reference : we must know which segment it targets
				resolved_address = file[i].segment_address[walk->target_seg];
			}
			else
			{
				// Check that we have a match for the external reference
				if (walk->label->resolved == false)
				{
					cerr << "ERROR: Undefined label '" << walk->label->name << "', referenced from file "
						 << file[i].filename << endl;
					// Keep going, bail out later
					error_flag = true;
					//	  exit(1);
				}

				// Resolve it
				resolved_address = walk->label->address;
				// Add the segment offset if this isn't a global symbol
				if (walk->label->file_no != -1)
					resolved_address += file[walk->label->file_no].segment_address[walk->label->segment];
			}

			//      cerr << "resolving reference at address : 0x" << setw(5) << setfill('0') << hex << (file[i].segment_address[walk->source_seg] + walk->address) << endl;

			// Now we have a resolved address, we can add it to the address part of the instruction
			unsigned int insn = file[i].segment[walk->source_seg][walk->address];

			//      cerr << "old val = 0x" << setw(8) << setfill('0') << hex << insn << endl;

			// Add our address
			resolved_address = (resolved_address + (insn & 0xfffff)) & 0xfffff;
			// Or it back into the instruction
			file[i].segment[walk->source_seg][walk->address] = (insn & 0xfff00000) | resolved_address;

			//      cerr << "new val = 0x" << setw(8) << setfill('0') << hex << file[i].segment[walk->source_seg][walk->address] << endl;

			// Next reference
			walk = walk->next;
		}
	}

	if (error_flag == true)
		exit(1);

	// Righto, now we are all done, dump the output for now
	unsigned int current_address;

	if (verbose_flag == true)
	{
		// Text segment first, then data
		for (i = 0; i < NUM_SEGMENTS; i++)
		{
			// Loop through the files
			for (int j = 0; j < num_files; j++)
			{
				// Set the starting address
				current_address = file[j].segment_address[i];

				cout << "file '" << file[j].filename << "', starting : 0x" << setw(5) << hex << setfill('0')
					 << current_address << ", ";

				int size;
				// Increment for the next segment
				if (i == TEXT)
				{
					size = file[j].file_header.text_seg_size;
					cout << ".text\n";
				}
				else if (i == DATA)
				{
					size = file[j].file_header.data_seg_size;
					cout << ".data\n";
				}
				else
				{
					size = 0;
					cout << ".bss : " << file[j].file_header.bss_seg_size << " words.\n";
				}

				for (int k = 0; k < size; k++)
				{
					cout << "0x" << setw(5) << hex << setfill('0') << current_address << " : "
						 << setw(8) << hex << setfill('0') << file[j].segment[i][k] << "    ";
					if (i == TEXT)
						disassemble(current_address, file[j].segment[i][k]);
					cout << endl;
					current_address++;
				}

				cout << endl;
			}
		}
	}

	unsigned int entry_point;

	// Get the main label address
	label_entry *main = get_label("main");
	if (main->resolved == false)
	{
		entry_point = starting_text_address;
		cerr << "ERROR: Can not find program entry point 'main', does a '.global main' directive exist?\n";
		exit(1);
	}
	else
		entry_point = main->address + file[main->file_no].segment_address[main->segment];

	if (verbose_flag == true)
	{
		cout << "entry point : 0x" << setw(5) << hex << setfill('0') << entry_point << endl;
		cout << ".text segment size = 0x" << setw(8) << setfill('0') << hex << text_size << endl;
		cout << ".data segment size = 0x" << setw(8) << setfill('0') << hex << data_size << endl;
		cout << ".bss  segment size = 0x" << setw(8) << setfill('0') << hex << bss_size << endl;
	}

	// What we probably want to do here, is output an S-Record
	// Now we have all the info, we just need to put it all together
	// first the text segments and then the data segments
	ofstream outputfile;
	outputfile.open(output_filename, ios::out);
	if (!outputfile)
	{
		cerr << "ERROR: Could not open output file " << output_filename << endl;
		exit(1);
	}

	// Here we output an SRecord
	// Starting record (optional)
	//  outputfile << "S0030000FC\n"; // length = 3, address = 0, checksum = 0xfc

	// S3 data records

	const int max_srecord_line = 10; // Maximum number of words in an srecord line
	int buffer[max_srecord_line];
	int starting_address = 0;
	int buf_ptr = 0;

	unsigned int  bss_start, bss_end = -1;
	unsigned int data_start, data_end = -1;
	unsigned int text_end = -1;

	for (i = 0; i < NUM_SEGMENTS; i++)
	{
		// Loop through the files
		if (i != BSS){
			for (int j = 0; j < num_files; j++)
			{

				// Set the starting address
				current_address = file[j].segment_address[i];

				if (j == 0){
					switch (i){
						case (TEXT):
							//starting text should be correct					
							break;
						case (DATA):
							data_start = current_address;					
							break;				
					}
				}

				int size;

				// Increment for the next segment
				if (i == TEXT)
					size = file[j].file_header.text_seg_size;
				else
					size = file[j].file_header.data_seg_size;

				for (int k = 0; k < size; k++)
				{
					// Get the starting address of this record's data
					if (buf_ptr == 0)
						starting_address = current_address;

					buffer[buf_ptr] = file[j].segment[i][k];
					buf_ptr++;

					if (buf_ptr == max_srecord_line)
					{
						output_srecord(outputfile, 3, starting_address, buffer, buf_ptr);
						buf_ptr = 0;
					}
					current_address++;
				}
			}
		}
		switch (i){
				case (TEXT):
					text_end = current_address;			
					break;
				case (BSS):

					bss_start = bss_address - bss_size;
					bss_end = bss_address;

					break;
				case (DATA):
					data_end = current_address;					
					break;				
			}
	}

	if (verbose_flag == true){
		//cout << "\nsegment locations: {start, end}" << endl;
		cout << ".text segment start = 0x" << setw(6) << hex << setfill('0') << starting_text_address << ", segment end = 0x" << setw(6) << hex << setfill('0') << text_end << endl;
		cout << ".data segment start = 0x" << setw(6) << hex << setfill('0') <<            data_start << ", segment end = 0x" << setw(6) << hex << setfill('0') << data_end << endl;
		cout << ".bss  segment start = 0x" << setw(6) << hex << setfill('0') <<             bss_start << ", segment end = 0x" << setw(6) << hex << setfill('0') <<  bss_end << endl;
	}

	if (bss_end	> starting_text_address && text_end > bss_start){
		cerr << "ERROR: .bss and .text segments overlap " << endl;
		exit(1);
	}
	if (text_end > data_start && data_end > starting_text_address){
		cerr << "ERROR: .text and .data segments overlap " << endl;
		exit(1);
	}
	if (data_end > bss_start && bss_end > data_start){
		cerr << "ERROR: .data and .bss segments overlap " << endl;
		exit(1);
	}

	if (buf_ptr > 0)
		output_srecord(outputfile, 3, starting_address, buffer, buf_ptr);

	output_srecord(outputfile, 7, entry_point, NULL, 0);

	return 0;
}
