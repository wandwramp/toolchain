#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H

enum insn_descriptor { INSN, I_TYPE, R_TYPE, J_TYPE, DIRECTIVE, OTHER };

struct insn_type {
  char *mnemonic;
  char *operands;
  unsigned int OPCode, func;
  insn_descriptor type;
};

struct reg_type {
  char *reg_name;
  int reg_num;
};

extern insn_type insn_table[];

extern void disassemble(unsigned int, unsigned int);

#endif

