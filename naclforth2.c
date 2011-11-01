#include <assert.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>

#ifdef __native_client__
#include <sys/nacl_syscalls.h>
#include <ppapi/c/pp_errors.h>
#include <ppapi/c/pp_module.h>
#include <ppapi/c/pp_var.h>
#include <ppapi/c/ppb.h>
#include <ppapi/c/ppb_instance.h>
#include <ppapi/c/ppb_messaging.h>
#include <ppapi/c/ppb_var.h>
#include <ppapi/c/ppp.h>
#include <ppapi/c/ppp_instance.h>
#include <ppapi/c/ppp_messaging.h>
#endif


#define STACK_SIZE (1024 * 1024)
#define RSTACK_SIZE (1024 * 1024)
#define HEAP_SIZE (10 * 1024 * 1024)


typedef long cell_t;
typedef unsigned long ucell_t;

typedef struct _DICTIONARY {
  void* code;
  char* name;
  cell_t flags;
  struct _DICTIONARY **does;
  struct _DICTIONARY* next;
} DICTIONARY;


// Stacks / heap
static cell_t* stack_base;
static cell_t* rstack_base;
static cell_t* heap_base;
static cell_t* sp_global;
static cell_t* rp_global;
static cell_t* here = 0;

// State
static DICTIONARY* dictionary_head = 0;
static cell_t compile_mode = 0;
static cell_t number_base = 10;

// Input source
static unsigned char input_buffer[1024];
static unsigned char *source = 0;
static cell_t source_length = 0;
static cell_t source_id = 0;
static cell_t source_in = 0;


// Dictionary and flow macros.
#define FLAG_IMMEDIATE 1
#define FLAG_LINKED 2
#define FLAG_SMUDGE 4
#define NEXT { nextw = *ip++; goto *nextw->code; }
#define COMMA(val) { *here++ = (cell_t)(val); }
#define WORD(label)        {&& _##label, #label, 0, 0, 0},
#define IWORD(label)       {&& _##label, #label, FLAG_IMMEDIATE, 0, 0},
#define SWORD(name,label)  {&& _##label, name, 0, 0, 0},
#define SIWORD(name,label) {&& _##label, name, FLAG_IMMEDIATE, 0, 0},
#define END_OF_DICTIONARY  {0, 0, 0, 0, 0},


static void Find(const unsigned char* name, cell_t name_len,
                 DICTIONARY** xt, cell_t* ret) {
  DICTIONARY *pos = dictionary_head;

  while(pos && pos->name) {
    if (!(pos->flags & FLAG_SMUDGE) &&
        name_len == strlen(pos->name) &&
        memcmp(name, pos->name, name_len) == 0) {
      if (pos->flags & FLAG_IMMEDIATE) {
        *ret = 1;
      } else {
        *ret = -1;
      }
      *xt = pos;
      return;
    }
    // Follow links differently based on flags.
    if (pos->flags & FLAG_LINKED) {
      pos = pos->next;
    } else {
      ++pos;
    }
  }

  *ret = 0;
}

static void Ok(void) {
  printf("  ok\n");
}

static void ReadLine(void) {
  fgets((char*)input_buffer, sizeof(input_buffer), stdin);
  source = input_buffer;
  source_length = strlen((char*)input_buffer);
  if (source_length > 0 && source[source_length - 1] == '\n') {
    --source_length;
  }
}

static unsigned char* Word(void) {
  unsigned char* ret = (unsigned char*)here;
  // Skip whitespace.
  while(source_in < source_length && source[source_in] == ' ') {
    ++source_in;
  }
  while(source_in < source_length && source[source_in] != ' ') {
    *(unsigned char*)here = source[source_in];
    here = (cell_t*)(1 + (unsigned char*)here);
    ++source_in;
  }
  ++source_in;
  // Add null.
  *(unsigned char*)here = 0;
  here = (cell_t*)(1 + (unsigned char*)here);
  return ret;
}

static void Align(void) {
  while (((cell_t)here) % sizeof(cell_t) != 0) {
    here = (cell_t*)(1 + (unsigned char*)here);
  }
}

static int DigitValue(unsigned char ch) {
  ch = tolower(ch);

  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 10;
  return -1;
}

static int ToNumber(const unsigned char *str, cell_t len, cell_t *dst) {
  int negative = 0;
  int digit;

  if (len <= 0) { return 0; }
  *dst = 0;
  if (str[0] == '-') { negative = 1; ++str; --len; }
  while (len > 0) {
    digit = DigitValue(*str);
    if (digit < 0 || digit >= number_base) { return 0; }
    *dst *= number_base;
    *dst += digit;
    ++str;
    --len;
  }
  if (negative) { *dst = -*dst; }
  return 1;
}

static void PrintNumber(cell_t value) {
  static char digit[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static char buf[20];
  int len = 0;
  
  printf(" ");
  if (value < 0) { value = -value; printf("-"); }
  do {
    buf[len++] = digit[value % number_base];
    value /= number_base;
  } while (value);
  while (len) {
    fputc(buf[len - 1], stdout);
    --len;
  }
  fflush(stdout);
}

static void Run(void) {
  register cell_t* sp = sp_global;
  register cell_t* rp = rp_global;
  register DICTIONARY** ip = *(DICTIONARY***)rp--;
  register DICTIONARY* nextw;

  static DICTIONARY base_dictionary[] = {
    // Put some at predictable locations.
    WORD(_lit)  // 0
#define WORD_LIT (&base_dictionary[0])
    WORD(_jump)  // 1
#define WORD_JUMP (&base_dictionary[1])
    WORD(_zbranch)  // 2
#define WORD_ZBRANCH (&base_dictionary[2])
    WORD(exit)  // 3
#define WORD_EXIT (&base_dictionary[3])
    WORD(_imp_do)  // 4
#define WORD_IMP_DO (&base_dictionary[4])
    WORD(_imp_qdo)  // 5
#define WORD_IMP_QDO (&base_dictionary[5])
    WORD(_imp_loop)  // 6
#define WORD_IMP_LOOP (&base_dictionary[6])
    WORD(_imp_plus_loop)  // 7
#define WORD_IMP_PLUS_LOOP (&base_dictionary[7])
    WORD(_imp_does)  // 8
#define WORD_IMP_DOES (&base_dictionary[8])
    WORD(quit) // 9
#define WORD_QUIT (&base_dictionary[9])

    SWORD(">r", push) SWORD("r>", pop)
    SWORD("+", add) SWORD("-", subtract) SWORD("*", multiply) SWORD("/", divide)
    SWORD("=", equal) SWORD("<>", nequal)
    SWORD("<", less) SWORD("<=", lequal)
    SWORD(">", greater) SWORD(">=", gequal)
    WORD(min) WORD(max)
    SWORD("@", load) SWORD("!", store)
    WORD(dup) WORD(drop) WORD(swap) WORD(over) 
    SWORD(",", comma) WORD(here)
    SWORD(".", dot) WORD(emit) WORD(cr)
    SWORD(":", colon) SIWORD(";", semicolon)
    WORD(immediate) SIWORD("[", lbracket) SIWORD("]", rbracket)
    WORD(create) SIWORD("does>", does) WORD(variable) WORD(constant)
    IWORD(if) IWORD(else) IWORD(then)
    IWORD(begin) IWORD(again) IWORD(until)
    IWORD(while) IWORD(repeat)
    IWORD(do) SIWORD("?do", qdo) IWORD(loop) SIWORD("+loop", plus_loop)
    WORD(i) WORD(j)
    WORD(leave) WORD(exit) WORD(unloop)
    WORD(literal) SWORD("compile,", compile) WORD(find)
    WORD(base) WORD(decimal) WORD(hex)
    SWORD("source-id", source_id)
    WORD(yield) END_OF_DICTIONARY  // This must go last.
  };

  DICTIONARY *quit_loop[] = {WORD_QUIT};

  // Start dictionary out with these.
  if (!dictionary_head) { dictionary_head = base_dictionary; }

  // Go to quit loop if nothing else.
  if (!ip) { ip = quit_loop; }

  // Go to work.
  NEXT;

 _add: --sp; *sp += sp[1]; NEXT;
 _subtract: --sp; *sp -= sp[1]; NEXT;
 _multiply: --sp; *sp *= sp[1]; NEXT;
 _divide: --sp; *sp /= sp[1]; NEXT;

 _equal: --sp; *sp = sp[0] == sp[1]; NEXT; 
 _nequal: --sp; *sp = sp[0] != sp[1]; NEXT; 
 _less: --sp; *sp = sp[0] < sp[1]; NEXT; 
 _lequal: --sp; *sp = sp[0] <= sp[1]; NEXT; 
 _greater: --sp; *sp = sp[0] > sp[1]; NEXT; 
 _gequal: --sp; *sp = sp[0] >= sp[1]; NEXT; 

 _min: --sp; if (sp[1] < sp[0]) { *sp = sp[1]; } NEXT; 
 _max: --sp; if (sp[1] > sp[0]) { *sp = sp[1]; } NEXT; 

 _load: *sp = *(cell_t*)*sp; NEXT;
 _store: sp -= 2; *(cell_t*)sp[2] = sp[1]; NEXT;

 _push: *++rp = *sp--; NEXT;
 _pop: *++sp = *rp--; NEXT;

 _dup: ++sp; sp[0] = sp[-1]; NEXT;
 _drop: --sp; NEXT;
 _swap: sp[1] = sp[0]; sp[0] = sp[-1]; sp[-1] = sp[1]; NEXT;
 _over: ++sp; *sp = sp[-2]; NEXT;

 _comma: COMMA(*sp--); NEXT;
 _here: *++sp = (cell_t)here; NEXT;

 _dot: PrintNumber(*sp--); NEXT;
 _emit: fputc(*sp--, stdout); NEXT;
 _cr: fputc('\n', stdout); NEXT;

 __lit: *++sp = *(cell_t*)ip++; NEXT;
 __jump: ip = *(DICTIONARY***)ip; NEXT;
 __zbranch: if (*sp--) { ++ip; } else { ip = *(DICTIONARY***)ip; } NEXT;

 __enter: *++rp = (cell_t)ip; ip = (DICTIONARY**)(nextw + 1); NEXT;
 __enter_create: *++sp = (cell_t)(nextw + 1); NEXT;
 __enter_does: *++rp = (cell_t)ip; ip = nextw->does;
  *++sp = (cell_t)(nextw + 1); NEXT;
 __enter_constant: *++sp = *(cell_t*)(nextw + 1); NEXT;
  
 _exit: ip = *(DICTIONARY***)rp--; NEXT;
 _unloop: rp -= 3; NEXT;
 _leave: ip = *(DICTIONARY***)rp[-2]; rp -= 3; NEXT;

 _colon: {
    unsigned char* name = Word();
    Align();
    COMMA(&& __enter); COMMA(name); COMMA(FLAG_SMUDGE | FLAG_LINKED);
    COMMA(0); COMMA(dictionary_head);
    compile_mode = 1; dictionary_head = ((DICTIONARY*)here) - 1;
    NEXT;
  }
 _semicolon: COMMA(WORD_EXIT); compile_mode = 0;
  dictionary_head->flags &= (~FLAG_SMUDGE); NEXT;
 _immediate: dictionary_head->flags |= FLAG_IMMEDIATE; NEXT;
 _lbracket: compile_mode = 0; NEXT;
 _rbracket: compile_mode = 1; NEXT;
 _create: {
    unsigned char* name = Word();
    Align();
    COMMA(&& __enter_create); COMMA(name);
    COMMA(FLAG_LINKED); COMMA(0); COMMA(dictionary_head);
    dictionary_head = ((DICTIONARY*)here) - 1;
    NEXT;
  }
 __imp_does: {
    dictionary_head->code = && __enter_does;
    dictionary_head->does = *(DICTIONARY***)sp--;
    NEXT;
  }
 _does: {
    if (compile_mode) {
      nextw = (DICTIONARY*)(here + 4);
      COMMA(WORD_LIT); COMMA(nextw); COMMA(WORD_IMP_DOES); COMMA(WORD_EXIT);
    } else {
      *++sp = (cell_t)here;
      compile_mode = 1;
      goto __imp_does;
    }
    NEXT;
  }
 _variable: {
    unsigned char* name = Word();
    Align();
    COMMA(&& __enter_create); COMMA(name);
    COMMA(FLAG_LINKED); COMMA(0); COMMA(dictionary_head);
    dictionary_head = ((DICTIONARY*)here) - 1;
    COMMA(0);
    NEXT;
  }
 _constant: {
    unsigned char* name = Word();
    Align();
    COMMA(&& __enter_constant); COMMA(name);
    COMMA(FLAG_LINKED); COMMA(0); COMMA(dictionary_head);
    dictionary_head = ((DICTIONARY*)here) - 1;
    COMMA(*sp--);
    NEXT;
  }

 _if: COMMA(WORD_ZBRANCH); *++sp = (cell_t)here; COMMA(0); NEXT;
 _then: *(cell_t*)*sp = (cell_t)here; NEXT;
 _else: COMMA(WORD_JUMP); *(cell_t*)*sp = (cell_t)(here + 1);
  *++sp = (cell_t)here ; COMMA(0); NEXT;

 _begin: *++sp = (cell_t)here; NEXT;
 _again: COMMA(WORD_JUMP); COMMA(*sp--); NEXT;
 _until: COMMA(WORD_ZBRANCH); COMMA(*sp--); NEXT;
         
 _while: COMMA(WORD_ZBRANCH); *++sp = (cell_t)here; COMMA(0); NEXT;
 _repeat: COMMA(WORD_JUMP); COMMA(sp[-1]);
  *(cell_t*)*sp = (cell_t)here; sp -= 2; NEXT;

 _i: *++sp = rp[-1]; NEXT;
 _j: *++sp = rp[-4]; NEXT;

 __imp_do: *++rp = *(cell_t*)ip++; *++rp = *sp--; *++rp = *sp--; NEXT;
 _do: COMMA(WORD_IMP_DO); *++sp = (cell_t)here; COMMA(0); NEXT;
 __imp_qdo: {
    if (sp[0] == sp[1]) {
      ip = *(DICTIONARY***)ip;
    } else {
      goto __imp_do;
    }
    NEXT;
  }
 _qdo: COMMA(WORD_IMP_QDO); *++sp = (cell_t)here; COMMA(0); NEXT;
 __imp_loop: {
    ++rp[-1];
    if (rp[-1] == rp[0]) {
      ++ip; rp -= 3;
    } else {
      ip = *(DICTIONARY***)ip;
    }
    NEXT;
  }
 _loop_common: COMMA(1 + (cell_t*)*sp); *(cell_t**)sp-- = here; NEXT;
 _loop: COMMA(WORD_IMP_LOOP); goto _loop_common;
 __imp_plus_loop: {
    rp[-1] += *sp--;
    if (rp[-1] == rp[0]) {
      ++ip; rp -= 3;
    } else {
      ip = *(DICTIONARY***)ip;
    }
    NEXT;
  }
 __imp_plus_loop: rp[-1] += *sp--; goto __imp_loop_common;
 _plus_loop: COMMA(WORD_IMP_PLUS_LOOP); goto _loop_common;

 _find: ++sp; Find(1 + (const unsigned char*)sp[-1], *(unsigned char*)sp[-1],
                   (DICTIONARY**)&sp[-1], &sp[0]); NEXT;
 _quit:
  if (source_in >= source_length) {
    Ok();
    source_in = 0;
    source_id = 0;
    ReadLine();
  }
  // Skip space.
  while (source_in < source_length && source[source_in] == ' ') {
    ++source_in;
  }
  if (source_in >= source_length) {
    goto _quit;
  }
  {
    cell_t start = source_in;
    while (source_in < source_length &&
           source[source_in] != ' ') { ++source_in; }
    if (source_in <= start) goto _quit;
    cell_t found;
    DICTIONARY* xt;
    Find(&source[start], source_in - start, &xt, &found);
    ++source_in;
    if ((found == 1 && compile_mode) || (found && !compile_mode)) {
      --ip;
      nextw = xt;
      goto *nextw->code;
    } else if (found) {
      COMMA(xt);
    } else if (ToNumber(&source[start], source_in - start - 1, &found)) {
      if (compile_mode) {
        COMMA(WORD_LIT); COMMA(found);
      } else {
        *++sp = found;
      }
    } else {
      printf("Unknown word\n");
      source_in = source_length;
    }
  }
  --ip;
  NEXT;

 _literal: COMMA(WORD_LIT); COMMA(*sp--); NEXT;
 _compile: goto _comma;

 _base: *++sp = (cell_t)&number_base; NEXT;
 _decimal: number_base = 10; NEXT;
 _hex: number_base = 16; NEXT;

 _source_id: *++sp = source_id; NEXT;

  // Exit from run.
 _yield: sp_global = sp; rp_global = rp;
}

int main(void) {
  stack_base = malloc(STACK_SIZE);
  sp_global = stack_base;
  
  rstack_base = malloc(RSTACK_SIZE);
  rp_global = rstack_base;
  *rp_global = 0;
  
  heap_base = malloc(HEAP_SIZE);
  here = heap_base;

  Run();
  
  return 0;
}