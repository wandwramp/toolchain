#include <iostream>
#include <iomanip>
#include <fstream>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "instructions.h"
#include "object_file.h"

using namespace std;

int num_globals = 0, num_local_refs = 0, num_unresolved = 0;

char *input_filename = NULL;
int current_line = 1;

const int max_line = 10000;
const int max_string = 10000;
unsigned int address[NUM_SEGMENTS];
seg_type current_segment;
const int max_label_length = 30;
char string_buffer[max_string];

struct label_entry {
  char name[max_label_length];
  int address;
  seg_type segment;
  bool resolved;
  bool global;
  label_entry *next;
  int name_ptr;
  int line;
};

// GPR table
reg_type GPR_table[] = {
  { "zero", 0 },
  { "sp", 14 },
  { "ra", 15 },
  { NULL, 0 }
};

// SPR table
reg_type SPR_table[] = {
  { "cctrl", 4 },
  { "estat", 5 },
  { "icount", 6 },
  { "ccount", 7 },
  { "evec", 8 },
  { "ear", 9 },
  { "esp", 10 },
  { "ers", 11 },
  { "ptable", 12 },
  { "rbase", 13 },
  { NULL, 0 }
};

enum label_descriptor { absolute, relative, immediate };

struct memory_entry {
  int line;               // The line in the program file that caused this entry
  unsigned int address;   // The address that this will lie
  unsigned int data;      // The data 
  
  char label[max_label_length]; // The name of a label this entry needs resolved
  label_descriptor reference_type;  // The value we want from this label when resolved
  memory_entry *next;
};

label_entry *label_list = NULL;
memory_entry *segment[NUM_SEGMENTS], *segment_end[NUM_SEGMENTS];

char symbol_buffer[max_label_length];

void init()
{
  for (int i = 0 ; i < NUM_SEGMENTS ; i++) {
    segment[i] = NULL;
    segment_end[i] = NULL;
    address[i] = 0;
  }

  current_segment = TEXT;
}

// Remove all dynamically allocated data structures
void cleanup()
{
  while (label_list != NULL) {
    label_entry *temp = label_list->next;
    delete label_list;
    label_list = temp;
  }
  for (int i = 0 ; i < NUM_SEGMENTS ; i++) {
    while (segment[i] != NULL) {
      memory_entry *temp = segment[i]->next;
      delete segment[i];
      segment[i] = temp;
    }
  }
}

void bailout()
{
  cleanup();
  exit(1);
}

// Display an error message and die
void error(char *filename, int line_no, char *msg, char *param)
{
  if (filename)
    cerr << filename<< ":" << current_line << ": ";
  cerr << "ERROR: " << msg;

  if (param)
    cerr << "`" << param << "'";

  cerr << endl;
  bailout();
}

// Display an warning message
void warning(char *filename, int line_no, char *msg, char *param)
{
  if (filename)
    cerr << filename<< ":" << current_line << ": ";
  cerr << "WARNING: " << msg;

  if (param)
    cerr << "`" << param << "'";

  cerr << endl;
}


void chew_whitespace(char *&ptr)
{
  while (ptr != NULL && isspace(*ptr))
    ptr++;
  
  if (ptr != NULL && *ptr == '#')
    *ptr = '\0';
}

int still_more(char *&ptr)
{
  chew_whitespace(ptr);

  return (ptr != NULL && *ptr != '\0' && !isspace(*ptr));
}

// This function will parse a symbol, returning true if one is found, or false otherwise
bool parse_symbol(char *&ptr, char *buffer)
{
  int char_count = 0;

  // Chew any leading whitespace
  chew_whitespace(ptr);
  
  // First character must be a letter or an underscore.
  if (!(isalpha(*ptr) || *ptr == '_'))
    return false;

  do {
    *buffer++ = *ptr++;
    char_count++;
    if (char_count == max_label_length)
      error(input_filename, current_line, "Label too long", NULL);
      
  } while (isalnum(*ptr) || *ptr == '_' || *ptr == '.');
  *buffer++ = '\0';
  return true;
}



// This searches for a reference to a label, creating a new entry
// if none is found
label_entry *get_label(char *name)
{
  // Check for a label that is too long
  int len = strlen(name);
  if (isdigit(*name))
    error(input_filename, current_line, "Label must not begin with a digit", NULL);
  if (strchr(name, ' ') != NULL)
    error(input_filename, current_line, "Space in label", NULL);
  if (len >= max_label_length)
    error(input_filename, current_line, "Label too long", NULL);

  label_entry *temp = label_list;

  // Check for the label already existing
  while (temp != NULL) {
    if (strcmp(temp->name, name) == 0)
      return temp;
    temp = temp->next;
  }
  
  temp = new label_entry;

  temp->next = label_list;
  temp->resolved = false;
  temp->global = false;
  strcpy(temp->name, name);

  //  cerr << "New label : '" << name << "'\n";

  label_list = temp;

  return label_list;
}

void clean_up_line(char *&buf)
{
  char *temp;

  // Remove any stray newline or carriage return characters 
  temp = buf;
  while (*temp != '\0') {
    if (*temp == '\r' || *temp == '\n') {
      *temp = '\0';
      break;
    }
    temp++;
  }
    
  // Change tabs into spaces
  while ((temp = strchr(buf, '\t')) != NULL)
    *temp = ' ';
}

void check_labels(char *&buf)
{
  char *temp;
  char *comment;
  comment = strchr(buf, '#');

  // Check for a label on this line
  if ((temp = strchr(buf, ':')) != NULL) {

    // Check to see if the colon is after a comment marker
    if (comment != NULL && temp > comment)
        return;

    // Check to see if the colon is within a string
    if ((strchr(buf, '\"') != NULL) && (strchr(buf, '\"') < temp))
      return;

    // Check to see if the colon is a character constant
    if (temp > buf && *(temp - 1) == '\'' && *(temp + 1) == '\'')
      return;

    // Extract a symbol from the start of this line.
    if (parse_symbol(buf, symbol_buffer) == false)
      error(input_filename, current_line, "Label expected on line (because of ':')", NULL);
    
    chew_whitespace(buf);
    if (*buf != ':')
      error(input_filename, current_line, "Badly formed label", NULL);

    // Move past the colon
    buf++;

    // Register this label
    label_entry *label = get_label(symbol_buffer);
    
    // If this label has already been resolved then we have a duplicate label
    if (label->resolved == true)
      error(input_filename, current_line, "Duplicate label : ", symbol_buffer);
    
    // Fill in the values for this label
    label->segment = current_segment;
    label->address = address[current_segment];
    label->resolved = true;
  }
}

// This function creates a new empty program entry in the specified segment
memory_entry *add_entry(seg_type seg_no, int current_line)
{
  memory_entry *new_entry;
  if (segment[seg_no] == NULL && segment_end[seg_no] == NULL) {
    new_entry = (segment[seg_no] = (segment_end[seg_no] = new memory_entry));
  }
  else {
    segment_end[seg_no]->next = (new_entry = new memory_entry);
    segment_end[seg_no] = new_entry;
  }

  if (new_entry == NULL)
    error(input_filename, current_line, "Assembler error, could not allocate memory", NULL);

  new_entry->next = NULL;
  new_entry->line = current_line;
  new_entry->address = address[seg_no];
  new_entry->label[0] = 0;
  new_entry->data = 0;

  // Increment the address counter for this segment
  address[seg_no]++;

  return new_entry;
}

void decode_char(char *&buf, unsigned char &chr)
{
  if (*buf == '\\')
    {
      buf++;
      switch (*buf)
	{
	case 'n':
	  chr = '\n';
	  break;
	case 't':
	  chr = '\t';
	  break;
	case 'r':
	  chr = '\r';
	  break;
	case 'a':
	  chr = '\a';
	  break;
	case '\\':
	  chr = '\\';
	  break;
	case '\"':
	  chr = '\"';
	  break;
	case '\'':
	  chr = '\'';
	  break;
	case '0':
	  chr = '\0';
	  buf++;
	  while (isdigit(*buf)) {
	    chr = (chr << 3) + (*buf - '0');
	    buf++;
	  }
	  buf--;
	  break;
	default:
	  error(input_filename, current_line, "Bad character escape sequence", NULL);
	  break;
	}
      buf++;
      return;
    }
  chr = *buf;
  buf++;
}

int decode_GPR(char *&ptr)
{
  // We must scan until we get an end-of-line ('\0')
  // or until we see a comma, or until the register is too large

  chew_whitespace(ptr);

  int reg_no = 0;

  if (tolower(*ptr) != 'r' && *ptr != '$')
    error(input_filename, current_line, "Register identifier expected", NULL);

  ptr++;

  if (*ptr >= '0' && *ptr <= '9') {

    // Register specified by number
    do {
      reg_no = reg_no * 10;
      reg_no += *ptr - '0';

      if (reg_no >= 16) 
	error(input_filename, current_line, "Register identifier expected", NULL);

      ptr++;
    } while (*ptr >= '0' && *ptr <= '9');

  }
  else if (tolower(*(ptr - 1)) == 'r') {
    error(input_filename, current_line, "Register identifier expected", NULL);
  }
  else {
    // Here we are expecting a word specifier (as in $zero, or $istat)
    char buffer[20] = {0};
    int char_count = 0;

    while (isalnum(*ptr) && char_count < 9) {
      buffer[char_count] = tolower(*ptr);
      char_count++;
      ptr++;
    }

    //    chew_whitespace(ptr);

    //    if (*ptr != ',' && *ptr != '\0' && *ptr != ')')
    //      error(input_filename, current_line, "Register identifier expected", NULL);

    buffer[char_count] = '\0';

    int index = 0;

    // Now we must match our string with a register
    while (GPR_table[index].reg_name != NULL
	   && (strcmp(GPR_table[index].reg_name, buffer) != 0))
      index++;
  
    if (GPR_table[index].reg_name == NULL)
      error(input_filename, current_line, "Register identifier expected", NULL);

    reg_no = GPR_table[index].reg_num;
  }

  // Back up past the last (non-register) character we read
  //  ptr--;

  return reg_no;
}

int decode_SPR(char *&ptr)
{
  // We must scan until we get an end-of-line ('\0')
  // or until we see a comma, or until the register is too large

  chew_whitespace(ptr);

  int reg_no = 0;

  if (*ptr != '$')
    error(input_filename, current_line, "Register identifier expected", NULL);

  ptr++;

  // Here we are expecting a word specifier (as in $zero, or $istat)
  char buffer[20] = {0};
  int char_count = 0;
  
    while (isalnum(*ptr) && char_count < 9) {
      buffer[char_count] = tolower(*ptr);
      char_count++;
      ptr++;
    }

    //  if (*ptr != ',' && *ptr != '\0' && *ptr != ')')
    //    error(input_filename, current_line, "Special register identifier expected", NULL);

  buffer[char_count] = '\0';

  int index = 0;
  
  // Now we must match our string with a register
  while (SPR_table[index].reg_name != NULL
	 && (strcmp(SPR_table[index].reg_name, buffer) != 0))
    index++;
  
  if (SPR_table[index].reg_name == NULL)
    error(input_filename, current_line, "Special register identifier expected", NULL);

  reg_no = SPR_table[index].reg_num;

  // Back up past the last (non-register) character we read
  //  ptr--;

  return reg_no;
}

unsigned int parse_address(char *&ptr)
{
  unsigned int value = 0;

  if (ptr == NULL)
    error(input_filename, current_line, "Numeric value expected", NULL);

  chew_whitespace(ptr);

  // If this is hexadecimal
  if (*ptr == '0' && tolower(*(ptr + 1)) == 'x') {
    ptr += 2;
    if (!isxdigit(*ptr))
      error(input_filename, current_line, "Numeric value expected", NULL);
    while (isxdigit(*ptr)) {
      value = value << 4;
      if (*ptr >= '0' && *ptr <= '9')
	value += *ptr - '0';
      else
	value += tolower(*ptr) - 'a' + 10;
      ptr++;
    }
  }
  else
    error(input_filename, current_line, "Hexadecimal address expected", NULL);
  
  if (value > 0xfffff)
    error(input_filename, current_line, "Constant too large", NULL);

  return value;
}

unsigned int parse_word(char *&ptr)
{
  unsigned int value = 0;

  if (ptr == NULL)
    error(input_filename, current_line, "Numeric value expected", NULL);

  chew_whitespace(ptr);

  // If this is hexadecimal
  if (*ptr == '0' && tolower(*(ptr + 1)) == 'x') {
    ptr += 2;
    if (!isxdigit(*ptr))
      error(input_filename, current_line, "Numeric value expected", NULL);
    while (isxdigit(*ptr)) {
      if (value > 0x0fffffff)
	error(input_filename, current_line, "Constant too large", NULL);
      
      value = value << 4;
      if (*ptr >= '0' && *ptr <= '9')
	value += *ptr - '0';
      else
	value += tolower(*ptr) - 'a' + 10;
      ptr++;
    }
  }
  else {
    bool negative = false;
    if (*ptr == '-') {
      negative = true;
      ptr++;
    }  
    if (!isdigit(*ptr))
      error(input_filename, current_line, "Numeric value expected", NULL);
    
    while (isdigit(*ptr)) {
      if (value >= 0x19999999)
	error(input_filename, current_line, "Constant too large", NULL);
      value = value * 10;
      value += *ptr - '0';
      ptr++;
    }
    
    if (negative == true) {
      if (value & 0x80000000)
	error(input_filename, current_line, "Constant too large", NULL);
      value = ((unsigned)-((signed)value));
    }
  }
  //  ptr--;

  return value;
}

unsigned int parse_half(char *&ptr)
{
  unsigned int value = 0;

  if (ptr == NULL)
    error(input_filename, current_line, "Numeric value expected", NULL);

  chew_whitespace(ptr);

  // If this is a character
  if (*ptr == '\'') {
    if ((*(ptr + 1) == '\\' && *(ptr + 3) != '\'')
	|| (*(ptr + 1) != '\\' && *(ptr + 2) != '\''))
      error(input_filename, current_line, "Bad character constant", NULL);
    unsigned char chr = 0;
    // Skip past the opening quote
    ptr++;
    decode_char(ptr, chr);
    // Skip past the closing quote
    ptr++;
    value = chr;
  }
  // If this is hexadecimal
  else if (*ptr == '0' && tolower(*(ptr + 1)) == 'x') {
    ptr += 2;
    if (!isxdigit(*ptr))
      error(input_filename, current_line, "Numeric value expected", NULL);
    while (isxdigit(*ptr)) {
      value = value << 4;
      if (*ptr >= '0' && *ptr <= '9')
	value += *ptr - '0';
      else
	value += tolower(*ptr) - 'a' + 10;
      ptr++;
    }
  }
  else {
    bool negative = false;
    if (*ptr == '-') {
      negative = true;
      ptr++;
    }
    if (!isdigit(*ptr))
      error(input_filename, current_line, "Numeric value expected", NULL);
    
    while (isdigit(*ptr)) {
      value = value * 10;
      value += *ptr - '0';
      ptr++;
    }

    if (negative == true)
      value = ((unsigned)-((signed)value)) & 0xffff;
  }
  if (value > 0xffff)
    error(input_filename, current_line, "Constant too large", NULL);
  
  return value;
}

int parse_string(char *&ptr, char *buffer)
{
  int i = 0;

  if (ptr == NULL)
    error(input_filename, current_line, "String expected", NULL);
  
  chew_whitespace(ptr);

  if (*ptr++ != '\"')
    error(input_filename, current_line, "String expected", NULL);

  while (*ptr != '\"') {
    unsigned char tmp;
    if (*ptr == '\0')
      error(input_filename, current_line, "Unterminated string", NULL);


    decode_char(ptr, tmp);
    buffer[i] = tmp;
    i++;
    if (i == max_string)
      error(input_filename, current_line, "String exceeds maximum length", NULL);
  }
  ptr++;
  return i;
}

void parse_line(char *buf)
{
  char *temp;

  //  cerr << "parse_line : " << buf << endl;

  clean_up_line(buf);
  chew_whitespace(buf);
  check_labels(buf);
    
  //  cerr << "tohere";

  // Remove leading spaces
  while (isspace(*buf))
    buf++;
  
  // Get rid of obvious comments
  if (*buf == '#')
    *buf = '\0';

  // Empty line now?
  if (*buf == '\0')
    return;
      
  // Here we can process the instruction 
  char *mnemonic = buf;
  
  char *operands = strchr(buf, ' ');
  if (operands != NULL)
    *(operands++) = '\0';

  chew_whitespace(operands);

  int insn_num = 0;
  unsigned int offset;

  // Convert the mnemonic to lower case
  temp = mnemonic;
  while (*temp != '\0') {
    *(temp) = tolower(*temp);
    temp++;
  }

  //  cerr << "up to here...";

  // Here we look up the mnemonic in our table.
  while (insn_table[insn_num].mnemonic != NULL
	 && (strcmp(insn_table[insn_num].mnemonic, mnemonic) != 0)) {
    insn_num++;
  }
  
  //  cerr << "now here...";

  if (insn_table[insn_num].mnemonic == NULL) {
    //  cerr << "mnemonic is : " << mnemonic << endl;
    error(input_filename, current_line, "Bad mnemonic : ", mnemonic);
  }

  //  cerr << "then here...";

  // Handle assembler directives
  if (insn_table[insn_num].type == DIRECTIVE) {
    if (strcmp(mnemonic, ".word") == 0) {
      
      if (current_segment != BSS) {
	if (operands == NULL) {
	  error(input_filename, current_line, "Expecting value or label.", NULL);
	}
	chew_whitespace(operands);
	if (*operands == '\0')
	  error(input_filename, current_line, "Expecting value or label.", NULL);	  
        if (strchr(operands, '"') != NULL) {
          error(input_filename, current_line, "Expecting value or label.", NULL);
        }

	do {
	  memory_entry *new_entry = add_entry(current_segment, current_line);
	  if (isdigit(*operands) || *operands == '-') {
	    new_entry->data = parse_word(operands);
	  }
	  else {
            if (parse_symbol(operands, symbol_buffer)) {
              // This word holds the value of a symbol
              new_entry->reference_type = absolute;
              strcpy(new_entry->label, symbol_buffer);
              new_entry->data = 0;
            }
            else {
              // This word could be a character in '' or otherwise
              unsigned char chr;
              if (*operands == '\'') {
		// Skip the opening quote
                operands++;
                decode_char(operands, chr);
		// Check and skip the ending quote
                if (*operands != '\'') {
                  error(input_filename, current_line, "Bad character constant.", NULL);
                }
                operands++;
                new_entry->data = chr;
              }
              else {
		// Otherwise is invalid.
                error(input_filename, current_line, "Expecting value or label.", NULL);
              }
            }
	  }

	  chew_whitespace(operands);
	  
	  if (*operands == ',')
	    operands++;
	  
	  chew_whitespace(operands);
	} while(*operands != '\0');

	if (*(operands - 1) == ',')
	  error(input_filename, current_line, "Expecting value or label.", NULL);
      }
      else {
	memory_entry *new_entry = add_entry(current_segment, current_line);
	if (operands != NULL && operands[0] != 0) {
	  // Attempt to initialise data in the bss segment
	  warning(input_filename, current_line, "Ignoring initial value in .bss segment.", NULL);
	}
	new_entry->data = 0;
      }
    }
    else if (strcmp(mnemonic, ".space") == 0) {
      if (current_segment != BSS) {
	error(input_filename, current_line, "Can only use .space directive in .bss segment.", NULL);
      }
      // Possible here we should just parse an int (not allow hex values)
      int num_words = parse_word(operands);
      // Add the appropriate number of data items
      for (int i = 0 ; i < num_words ; i++)
	add_entry(current_segment, current_line);
      
      //      cerr << "operands = '" << operands << "'\n";

      if (still_more(operands))
	error(input_filename, current_line, "Unexpected character encountered on line", NULL);
    }
    else if (strcmp(mnemonic, ".asciiz") == 0 || strcmp(mnemonic, ".ascii") == 0) {
      if (current_segment == BSS) {
	error(input_filename, current_line, "Cannot specify a string in .bss segment.", NULL);
      }
      memory_entry *new_entry;

      //cerr << "calling parse_string : " << operands << endl;
      int len = parse_string(operands, string_buffer);
      
      for (int i = 0 ; i < len ; i++) {
	// Add the character
	new_entry = add_entry(current_segment, current_line);
	new_entry->data = string_buffer[i];
      }

      //      cerr << "left : " << operands << endl;
      if (still_more(operands))
	error(input_filename, current_line, "Unexpected character encountered on line", NULL);

      // Add the null terminator
      if (strcmp(mnemonic, ".asciiz") == 0) {
	new_entry = add_entry(current_segment, current_line);
	new_entry->data = 0;
      }
    }
    else if (strcmp(mnemonic, ".equ") == 0) {
      if (operands == NULL) {
	error(input_filename, current_line, "Equ directive must specify a symbol name and value", NULL);
      }

      if (parse_symbol(operands, symbol_buffer) == false)
	error(input_filename, current_line, "Expected symbol name", NULL);

      chew_whitespace(operands);
      if (*operands++ != ',')
	error(input_filename, current_line, "Expected ',' after symbol name", NULL);

      // Make the specified symbol
      label_entry *new_label = get_label(symbol_buffer);

      if (new_label->resolved == true)
	error(input_filename, current_line, "Duplicate label : ", symbol_buffer);

      new_label->address = parse_word(operands);

      new_label->line = current_line;
      new_label->resolved = true;
      new_label->segment = NONE;

      if (still_more(operands))
	error(input_filename, current_line, "Unexpected character encountered on line", NULL);
    }
    else if (strcmp(mnemonic, ".data") == 0) {
      current_segment = DATA;
      if (still_more(operands))
	error(input_filename, current_line, "Unexpected character encountered on line", NULL);
    }
    else if (strcmp(mnemonic, ".text") == 0) {
      current_segment = TEXT;
      if (still_more(operands))
	error(input_filename, current_line, "Unexpected character encountered on line", NULL);
    }
    else if (strcmp(mnemonic, ".bss") == 0) {
      current_segment = BSS;
      if (still_more(operands))
	error(input_filename, current_line, "Unexpected character encountered on line", NULL);
    }
    else if (strcmp(mnemonic, ".global") == 0) {
      if (operands == NULL || parse_symbol(operands, symbol_buffer) == false)
	error(input_filename, current_line, "Global directive must specify a label", NULL);

      // Make the specified label global
      label_entry *temp = get_label(symbol_buffer);
      if (temp->global == false) {
	// Increment the global count
	num_globals++;
	temp->global = true;
	temp->line = current_line;
      }

      if (still_more(operands))
	error(input_filename, current_line, "Unexpected character encountered on line", NULL);      
    }
    else if (strcmp(mnemonic, ".mask") == 0)
      ;
    else if (strcmp(mnemonic, ".frame") == 0)
      ;
    else if (strcmp(mnemonic, ".extern") == 0)
      ;
    else
      error(input_filename, current_line, "Unknown directive : ", mnemonic);
    return;
  }

  //cerr << "up to here...\n";

  // We do not allow any instructions in the data or bss segments
  if (current_segment == DATA) {
    error(input_filename, current_line, "Instructions not permitted in data segment", NULL);
  }
  if (current_segment == BSS) {
    error(input_filename, current_line, "Instructions not permitted in bss segment", NULL);
  }

  memory_entry *new_entry = add_entry(current_segment, current_line);
  new_entry->data = 0;
  
  // Copy in the func & OPCode fields
  new_entry->data |= (insn_table[insn_num].OPCode << 28);
  new_entry->data |= (insn_table[insn_num].func << 16);

  //  cerr << "here.\n";
  // Scan through the operand format string
  for (unsigned int i = 0 ; i < strlen(insn_table[insn_num].operands) ; i++) {
    //    cerr << "parsing : '" << insn_table[insn_num].operands[i] << "'\n";
    chew_whitespace(operands);
    if (operands == NULL || *operands == '\0')
      error(input_filename, current_line, "Expecting more on line", NULL);
    //    cerr << "done.\n";
    switch (insn_table[insn_num].operands[i]) {
    case 'd':
      new_entry->data |= (decode_GPR(operands) & 0xf) << 24;
      break;
    case 'D':
      new_entry->data |= (decode_SPR(operands) & 0xf) << 24;
      break;
    case 's':
      new_entry->data |= (decode_GPR(operands) & 0xf) << 20;
      break;
    case 'S':
      new_entry->data |= (decode_SPR(operands) & 0xf) << 20;
      break;
    case 't':
      new_entry->data |= (decode_GPR(operands) & 0xf);
      break;
    case 'o': // Twenty bit offset

      offset = 0;
      // If this is hexadecimal
      if (*operands == '0' && tolower(*(operands + 1)) == 'x') {
	operands += 2;
	if (!isxdigit(*operands))
	  error(input_filename, current_line, "Numeric value expected", NULL);
	while (isxdigit(*operands)) {
	  offset = offset << 4;
	  if (*operands >= '0' && *operands <= '9')
	    offset += *operands - '0';
	  else
	    offset += tolower(*operands) - 'a' + 10;
	  operands++;
	}
      }
      else if (isdigit(*operands) || *operands == '-') {
	bool negative = false;
	if (*operands == '-')
	  {
	    negative = true;
	    operands++;
	  }
	if (!isdigit(*operands))
	  error(input_filename, current_line, "Numeric value expected", NULL);

	while (isdigit(*operands))
	  {
	    offset = offset * 10;
	    offset += *operands - '0';
	    operands++;
	  }

	if (negative == true)
	  offset = ((unsigned)-((signed)offset)) & 0xfffff;
      }
      else {
	if (parse_symbol(operands, symbol_buffer) == false)
	  error(input_filename, current_line, "Label expected on line (because of ':')", NULL);
	
	offset = 0;
	
	if (*operands == '+') {
	  operands++;
	  // Read an offset
	  offset = (parse_word(operands) & 0xfffff);
	}

	// Make a note of this label
	new_entry->reference_type = absolute;
	strcpy(new_entry->label, symbol_buffer);
      }

      //      operands--;
      if (offset > 0xfffff)
	error(input_filename, current_line, "Constant too large", NULL);
	  
      new_entry->data |= (offset & 0xfffff);

      //     cerr << "data here is : 0x" << setw(8) << hex << setfill('0') << new_entry->data << endl;
      break;
    case 'b':
      if (parse_symbol(operands, symbol_buffer) == false)
	error(input_filename, current_line, "Label expected", NULL);

      new_entry->reference_type = relative;
      strcpy(new_entry->label, symbol_buffer);
      break;
    case 'i': // 16 bit immediate value
      // Must be lower cased
      new_entry->data |= (parse_half(operands) & 0xffff);
      break;
    case 'j':
      if (*operands == '0' && tolower(*(operands + 1)) == 'x') {
	new_entry->data |= (parse_address(operands) & 0xfffff);
      }
      else {
	if (parse_symbol(operands, symbol_buffer) == false)
	  error(input_filename, current_line, "Label expected", NULL);
	new_entry->reference_type = absolute;
	strcpy(new_entry->label, symbol_buffer);
      }
      break;
    default:
      if (*operands != insn_table[insn_num].operands[i]) {
	error(input_filename, current_line, "Unexpected character encountered on line", NULL);
      }
      operands++;
    }
  }

  // Check for more characters than we expect.
  if (still_more(operands))
    error(input_filename, current_line, "Unexpected character encountered on line", NULL);
}

// This function will resolve all the label references that it can within the text segment
// After this only external absolute references should remain unresolved
// This returns the number of external references
void resolve_labels()
{
  for (int i = 0 ; i < NUM_SEGMENTS ; i++)
    if (i == TEXT || i == DATA) {
      // The only unresolved labels should be in the text segment or the data segment
      memory_entry *walk = segment[i];
      
      // Walk through the text segment
      while (walk != NULL) {
	current_line = walk->line;
	
	// If this line refers to a label
	if (walk->label[0] != '\0') {
	  // We must get the entry for this label
	  label_entry *temp = get_label(walk->label);

	  // cerr << "checking out reference to " << walk->label << endl;
	  
	  // If we have resolved this label
	  if (temp->resolved == true) {
	    
	    // We have found the label now we resolve the address
	    // Check to see if we are looking for an absolute address or a branch
	    switch (walk->reference_type)
	      {
	      case absolute:
		//		cerr << "changed absolute data from 0x" << setw(8) << setfill('0') << hex << walk->data;
		walk->data += temp->address & 0xfffff;
		//		cerr << " to 0x" << setw(8) << setfill('0') << hex << walk->data << endl;
		break;
	      case relative:
		walk->data |= ((unsigned)((signed)temp->address - ((signed)walk->address + 1))) & 0xfffff;
		break;
	      case immediate:
		walk->data |= temp->address & 0xffff;
		break;
	      }
	  }
	  
	  // Check for branches to unresolved addresses - not allowed
	  if (walk->reference_type == relative && temp->resolved == false) {
	    error(input_filename, walk->line, "Branch target cannot be external : ", temp->name);
	  }
	  
	  // Count the number of internal absolute label references
	  if (walk->reference_type == absolute && temp->resolved == true && temp->segment != NONE) {
	    num_local_refs++;
	  }
	  
	  // Count the number of external absolute label references
	  if (walk->reference_type == absolute && temp->resolved == false) {
	    //	cout << "Unresolved reference : " << walk->label << endl;
	    num_unresolved++;
	  }
	}
	walk = walk->next;
      }
    }
}

void usage(char *progname)
{
  cerr << "USAGE: " << progname << " [-o output] file\n";
  exit(1);
}

int main(int argc, char *argv[])
{
  int i;
  char output_filename[300] = {0};

  if (argc < 2)
    usage(argv[0]);

  // Here we must parse the arguments
  for (i = 1 ; i < argc ; i++) {
    // Is this an option
    if (argv[i][0] == '-') {
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
      else 
	usage(argv[0]);
    }
    else {
      // Otherwise it is a filename
      // Multiple input files are not allowed
      if (input_filename != NULL)
	usage(argv[0]);
      input_filename = argv[i];
    }
  }

  if (input_filename == NULL)
    usage(argv[0]);

  if (output_filename[0] == '\0') {
    // Try to strip .S or .s and add .o
    // Failing that, just add .o
    strcpy(output_filename, input_filename);
    
    int len = strlen(output_filename);

    if (len > 1 && output_filename[len - 2] == '.' && toupper(output_filename[len - 1]) == 'S')
      output_filename[len - 1] = 'o';
    else
      strcat(output_filename, ".o");
  }
 
  init();


  ifstream sourcefile;
  sourcefile.open(input_filename, ios::in);

  if (!sourcefile) {
    error(NULL, 0, "Could not open input file : ", input_filename);
  }

  init();
  
  char buffer[max_line];
  
  while (!sourcefile.eof()) {
    // Read a line from the sourcefile
    sourcefile.getline(buffer, max_line);
    if (sourcefile.bad()) {
        error(NULL, 0, "Source file is directory : ", input_filename);
    }
    parse_line(buffer);
    current_line++;
  }

  // Resolve internal references
  resolve_labels();


  ofstream outputfile;

  outputfile.open(output_filename, ios::out | ios::binary);

  if (!outputfile) {
    error(NULL, 0, "Could not open output file : ", output_filename);
  }

  object_header obj_header;

  obj_header.magic_number = OBJ_MAGIC_NUM;

  obj_header.text_seg_size = address[TEXT];
  //  cout << ".text size : " << obj_header.text_seg_size << " words\n";
  obj_header.data_seg_size = address[DATA];
  //  cout << ".data size : " << obj_header.data_seg_size << " words\n";
  obj_header.bss_seg_size = address[BSS];

  obj_header.num_references = num_globals + num_local_refs + num_unresolved;
  //  cout << "num globals : " << num_globals << endl;
  //  cout << "num unresolved absolute : " << num_unresolved << endl;
  //  cout << "num resolved absolute : " << num_local_refs << endl;

  // Figure out how many labels we need to store in the object file
  obj_header.symbol_name_table_size = 0;
  label_entry *temp = label_list;
  while (temp != NULL) {
    if (temp->global == true || temp->resolved == false) {
      obj_header.symbol_name_table_size += strlen(temp->name) + 1;
    }
    temp = temp->next;
  }

  //  cout << "length of symbols : " << obj_header.symbol_name_table_size << endl;

  // Write the header to the object file
  outputfile.write((char *)&obj_header, sizeof(obj_header));

  //  cout << endl;

  // Write the text segment
  memory_entry *walk = segment[TEXT];
  //  cout << ".text segment\n";
  for (i = 0 ; i < (int)obj_header.text_seg_size ; i++) {
    //    cout << "0x" << setw(5) << hex << setfill('0') << i << "  "
    //	 << setw(8) << hex << setfill('0') << walk->data << endl;
    // Write one word of data
    outputfile.write((char *)&(walk->data), sizeof(walk->data));
    walk = walk->next;
  }
  
  if (walk != NULL) {
    error(NULL, 0, "Assembler error : .text segment larger than thought", NULL);
  }
  
  //  cout << endl;

  // Write the data segment
  walk = segment[DATA];
  //  cout << ".data segment\n";
  for (i = 0 ; i < (int)obj_header.data_seg_size ; i++) {
    //    cout << "0x" << setw(5) << hex << setfill('0') << i << "  "
    //	 << setw(8) << hex << setfill('0') << walk->data << endl;
    // Write one word of data
    outputfile.write((char *)&(walk->data), sizeof(walk->data));
    walk = walk->next;
  }

  if (walk != NULL) {
    error(NULL, 0, "Assembler error : .data segment larger than thought", NULL);
  }

  char *symbol_names = new char[obj_header.symbol_name_table_size];
  char *ptr = symbol_names;
  reloc_entry *relocation_array = new reloc_entry[obj_header.num_references];
  int reloc_num = 0;

  temp = label_list;
  
  while (temp != NULL) {
    // Copy the label name into our character array
    
    if (temp->global == true || temp->resolved == false) {
      temp->name_ptr = ptr - symbol_names;
      strcpy(ptr, temp->name);
      ptr += strlen(temp->name) + 1;
    }

    // Now check for globals
    if (temp->global == true) {

      // Warn about declared globals which aren't in this file
      if (temp->resolved == false) {
	error(input_filename, temp->line, "Unresolved global : ", temp->name);
      }
      
      // Fill in the address details
      relocation_array[reloc_num].address = temp->address;
      relocation_array[reloc_num].symbol_ptr = temp->name_ptr;

      if (temp->segment == TEXT)
	relocation_array[reloc_num].type = GLOBAL_TEXT;
      else if (temp->segment == DATA)
	relocation_array[reloc_num].type = GLOBAL_DATA;
      else
	relocation_array[reloc_num].type = GLOBAL_BSS;
      
      reloc_num++;
    }
    
    temp = temp->next;
  }

  // Globals are all handled, now we need to scan the text and data segments and add all
  // unresolved references, and all absolute references
  for (i = 0 ; i < NUM_SEGMENTS ; i++)
    if (i == TEXT || i == DATA) {
      walk = segment[i];
      
      while (walk != NULL) {
	if (walk->label[0] != '\0' && walk->reference_type == absolute) {
	  temp = get_label(walk->label);

	  // If this is a local symbol reference like an .equ then we don't include
	  // it in the object file
	  if (temp->segment != NONE) {
	    relocation_array[reloc_num].address = walk->address;
	    relocation_array[reloc_num].source_seg = (seg_type)i;
	    
	    if (temp->resolved == true) {
	      // This is a local resolved reference
	      //	    cerr << " local resolved reference! '" << walk->label << "'\n";
	      if (temp->segment == TEXT)
		relocation_array[reloc_num].type = TEXT_LABEL_REF;
	      else if (temp->segment == DATA)
		relocation_array[reloc_num].type = DATA_LABEL_REF;
	      else
		relocation_array[reloc_num].type = BSS_LABEL_REF;
	    }
	    else {
	      // This must be an external reference
	      relocation_array[reloc_num].type = EXTERNAL_REF;
	      relocation_array[reloc_num].symbol_ptr = temp->name_ptr;
	    }
	    
	    reloc_num++;
	  }
	}
	walk = walk->next;
      }
    }
  // Write the relocation array
  outputfile.write((char *)relocation_array, (sizeof(reloc_entry) * obj_header.num_references));
  // Write the symbol names
  outputfile.write(symbol_names, obj_header.symbol_name_table_size);

  // Clean up our data structures
  cleanup();

  delete[] relocation_array;
  delete[] symbol_names;

  return 0;
}
