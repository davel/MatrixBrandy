/*
** This file is part of the Matrix Brandy Basic VI Interpreter.
** Copyright (C) 2000-2014 David Daniels
** Copyright (C) 2018-2024 Michael McConnell and contributors
**
** Brandy is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2, or (at your option)
** any later version.
**
** Brandy is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with Brandy; see the file COPYING.  If not, write to
** the Free Software Foundation, 59 Temple Place - Suite 330,
** Boston, MA 02111-1307, USA.
**
**
**      This file contains all of the built-in Basic functions
*/
/*
** Changed by Crispian Daniels on August 12th 2002.
**      Changed 'fn_rnd' to use a pseudo-random generator equivalent
**      to the BASIC II implementation.
**
** 05-Apr-2014 JGH: Seperated BEAT from BEATS, they do different things.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include "common.h"
#include "target.h"
#include "basicdefs.h"
#include "tokens.h"
#include "variables.h"
#include "strings.h"
#include "convert.h"
#include "stack.h"
#include "errors.h"
#include "evaluate.h"
#include "keyboard.h"
#include "screen.h"
#include "mos.h"
#include "miscprocs.h"
#include "fileio.h"
#include "functions.h"
#include "mos_sys.h"


/* #define DEBUG */

/*
 * This file contains all of the built-in Basic functions. Most of
 * them are dispatched via exec_function() as they have two byte
 * tokens but some of them, particularly tokens that can be used
 * as either functions or statements such as 'MODE', are called
 * directly from the factor code in evaluate.c. The ones
 * invoked via exec_function() are marked as 'static'. If they
 * are not static they are called from evaluate.c. The value of
 * basicvars.current depends on where the function was called
 * from. If from exec_function() then it points at the byte
 * after the function's token. (This is a two byte value where
 * the second byte is a function number and basicvars.current
 * points at the byte after the function number.) If called from
 * factor() then it points at the function token still. This
 * will always be a one byte token.
 */

#define RADCONV 57.29577951308232286    /* Used when converting degrees -> radians and vice versa */
#define TIMEFORMAT "%a,%d %b %Y.%H:%M:%S"  /* Date format used by 'TIME$' */

/* RISC OS BASIC V uses &B0A, BASIC VI uses &110A. RTR BASICs use &90A */
#define STRFORMAT 0x110A                /* Default format used by function STR$ */

static int32 lastrandom;                /* 32-bit pseudo-random number generator value */
static int32 randomoverflow;            /* 1-bit overflow from pseudo-random number generator */
static float64 floatvalue;              /* Temporary for holding floating point values */

/*
** 'bad_token' is called to report a bad token value. This could mean
** two things: either the program has been corrupted or there is a bug
** in the interpreter. At this stage, the latter is more often the
** case.
*/
static void bad_token(void)  {
  DEBUGFUNCMSGIN;
#ifdef DEBUG
  fprintf(stderr, "Bad token value %x at %p\n", *basicvars.current, basicvars.current);
#endif
  DEBUGFUNCMSGOUT;
  error(ERR_BROKEN, __LINE__, "functions");
}

static uint64 resize32(size_t value) {
  DEBUGFUNCMSGIN;
  if (sizeof(size_t) == 4) { /* 32-bit */
    value &= 0xFFFFFFFFll;
  }
  DEBUGFUNCMSGOUT;
  return value;
}

/*
** 'fn_himem' pushes the value of HIMEM on to the Basic stack
*/
static void fn_himem(void) {
  DEBUGFUNCMSGIN;
  if (matrixflags.pseudovarsunsigned) {
    push_int64(resize32((size_t)basicvars.himem));
  } else {
    push_int64((size_t)basicvars.himem);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_ext' pushes the size of the open file referenced by the handle
** given by its argument on to the Basic stack
*/
static void fn_ext(void) {
  DEBUGFUNCMSGIN;
  if (*basicvars.current != '#') {
    DEBUGFUNCMSGOUT;
    error(ERR_HASHMISS);
    return;
  }
  basicvars.current++;
  push_int64(fileio_getext(eval_intfactor()));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_filepath' pushes a copy of the current program and library
** load path on to the Basic stack
*/
static void fn_filepath(void) {
  int32 length;
  char *cp;

  DEBUGFUNCMSGIN;
  if (basicvars.loadpath == NIL)
    length = 0;
  else {
    length = strlen(basicvars.loadpath);
  }
  cp = alloc_string(length);
  if (length>0) memcpy(cp, basicvars.loadpath, length);
  push_strtemp(length, cp);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_left' handles the 'LEFT$(' function
*/
static void fn_left(void) {
  stackitem stringtype;
  basicstring descriptor;
  int32 length;
  char *cp;

  DEBUGFUNCMSGIN;
  expression();         /* Fetch the string */
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  if (*basicvars.current == ',') {      /* Function call is of the form LEFT(<string>,<value>) */
    basicvars.current++;
    length = eval_integer();
    if (*basicvars.current != ')') {   /* ')' missing */
      DEBUGFUNCMSGOUT;
      error(ERR_RPMISS);
      return;
    }
    basicvars.current++;
    if (length<0)
      return;   /* Do nothing if required length is negative, that is, return whole string */
    else if (length == 0) {             /* Don't want anything from the string */
      descriptor = pop_string();
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
      cp = alloc_string(0);             /* Allocate a null string */
      push_strtemp(0, cp);
    }
    else {
      descriptor = pop_string();
      if (length>=descriptor.stringlen) /* Substring length exceeds that of original string */
        push_string(descriptor);        /* So put the old string back on the stack */
      else {
        cp = alloc_string(length);
        memcpy(cp, descriptor.stringaddr, length);
        push_strtemp(length, cp);
        if (stringtype == STACK_STRTEMP) free_string(descriptor);
      }
    }
  }
  else {        /* Return original string with the last character sawn off */
    if (*basicvars.current != ')') {   /* ')' missing */
      DEBUGFUNCMSGOUT;
      error(ERR_RPMISS);
      return;
    }
    basicvars.current++;        /* Skip past the ')' */
    descriptor = pop_string();
    length = descriptor.stringlen-1;
    if (length<=0) {
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
      cp = alloc_string(0);             /* Allocate a null string */
      push_strtemp(0, cp);
    }
    else {      /* Create a new string of the required length */
      cp = alloc_string(length);
      memmove(cp, descriptor.stringaddr, length);
      push_strtemp(length, cp);
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_lomem' pushes the address of the start of the Basic heap on
** to the Basic stack
*/
static void fn_lomem(void) {
  DEBUGFUNCMSGIN;
  if (matrixflags.pseudovarsunsigned) {
    push_int64(resize32((size_t)basicvars.lomem));
  } else {
    push_int64((size_t)basicvars.lomem);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_mid' handles the 'MID$(' function, which returns the middle
** part of a string
*/
static void fn_mid(void) {
  stackitem stringtype;
  basicstring descriptor;
  int32 start, length;
  char *cp;

  DEBUGFUNCMSGIN;
  expression();         /* Fetch the string */
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  start = eval_integer();
  if (*basicvars.current == ',') {      /* Call of the form 'MID$(<string>,<expr>,<expr>) */
    basicvars.current++;
    length = eval_integer();
    if (length<0) length = MAXSTRING;   /* -ve length = use remainder of string */
  }
  else {        /* Length not given - Use remainder of string */
    length = MAXSTRING;
  }
  if (*basicvars.current != ')') {     /* ')' missing */
    DEBUGFUNCMSGOUT;
    error(ERR_RPMISS);
    return;
  }
  basicvars.current++;
  descriptor = pop_string();
  if (length == 0 || start<0 || start>descriptor.stringlen) {   /* Don't want anything from the string */
    if (stringtype == STACK_STRTEMP) free_string(descriptor);
    cp = alloc_string(0);               /* Allocate a null string */
    push_strtemp(0, cp);
  }
  else {        /* Want only some of the original string */
    if (start>0) start-=1;      /* Turn start position into an offset from zero */
    if (start == 0 && length>=descriptor.stringlen)     /* Substring is entire string */
      push_string(descriptor);  /* So put the old string back on the stack */
    else {
      if (start+length>descriptor.stringlen) length = descriptor.stringlen-start;
      cp = alloc_string(length);
      memcpy(cp, descriptor.stringaddr+start, length);
      push_strtemp(length, cp);
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_page' pushes the address of the start of the basic program on to the
** Basic stack
*/
static void fn_page(void) {
  DEBUGFUNCMSGIN;
  if (matrixflags.pseudovarsunsigned) {
    push_int64(resize32((size_t)basicvars.page));
  } else {
    push_int64((size_t)basicvars.page);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_ptr' returns the current offset within the file of the file
** pointer for the file associated with file handle 'handle'
*/
static void fn_ptr(void) {
  DEBUGFUNCMSGIN;
  if (*basicvars.current == '#') {
    basicvars.current++;
    push_int64(fileio_getptr(eval_intfactor()));
  } else if (*basicvars.current == '(') {
    stackitem topitem;
    basicarray *descriptor;
    basicstring strdesc;

    basicvars.current++;

    expression();
    topitem = GET_TOPITEM;
    switch(topitem) {
      case STACK_INTARRAY: case STACK_UINT8ARRAY: case STACK_INT64ARRAY: case STACK_FLOATARRAY: case STACK_STRARRAY:
        descriptor=pop_array();
        push_int64((int64)(size_t)descriptor);
        break;
      case STACK_STRING:
        strdesc=pop_string();
        push_int64((int64)(size_t)strdesc.stringaddr);
        break;
      default:
        DEBUGFUNCMSGOUT;
        error(ERR_UNSUITABLEVAR);
        return;
    }
    basicvars.current++;
  } else {
    DEBUGFUNCMSGOUT;
    error(ERR_HASHMISS);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_right' evaluates the function 'RIGHT$('.
*/
static void fn_right(void) {
  stackitem stringtype;
  basicstring descriptor;
  char *cp;

  DEBUGFUNCMSGIN;
  expression();         /* Fetch the string */
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  if (*basicvars.current == ',') {      /* Function call is of the form RIGHT$(<string>,<value>) */
    int32 length;
    basicvars.current++;
    length = eval_integer();
    if (*basicvars.current != ')') {   /* ')' missing */
      DEBUGFUNCMSGOUT;
      error(ERR_RPMISS);
      return;
    }
    basicvars.current++;
    if (length<=0) {    /* Do not want anything from string */
      descriptor = pop_string();
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
      cp = alloc_string(0);             /* Allocate a null string */
      push_strtemp(0, cp);
    }
    else {
      descriptor = pop_string();
      if (length>=descriptor.stringlen) /* Substring length exceeds that of original string */
        push_string(descriptor);        /* So put the old string back on the stack */
      else {
        cp = alloc_string(length);
        memcpy(cp, descriptor.stringaddr+descriptor.stringlen-length, length);
        push_strtemp(length, cp);
        if (stringtype == STACK_STRTEMP) free_string(descriptor);
      }
    }
  }
  else {        /* Return only the last character */
    if (*basicvars.current != ')') {    /* ')' missing */
      DEBUGFUNCMSGOUT;
      error(ERR_RPMISS);
      return;
    }
    basicvars.current++;        /* Skip past the ')' */
    descriptor = pop_string();
    if (descriptor.stringlen == 0)
      push_string(descriptor);  /* String length is zero - Just put null string back on stack */
    else {      /* Create a new single character string */
      cp = alloc_string(1);
      *cp = *(descriptor.stringaddr+descriptor.stringlen-1);
      push_strtemp(1, cp);
      if (stringtype == STACK_STRTEMP) free_string(descriptor);
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'timedol' returns the date and time as string in the standard
** RISC OS format. There is no need to emulate this as standard C
** functions can be used to return the value
*/
static void fn_timedol(void) {
  size_t length;
  time_t thetime;
  char *cp;

  DEBUGFUNCMSGIN;
  thetime = time(NIL);
  length = strftime(basicvars.stringwork, MAXSTRING, TIMEFORMAT, localtime(&thetime));
  cp = alloc_string(length);
  memcpy(cp, basicvars.stringwork, length);
  push_strtemp(length, cp);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_time' returns the value of the centisecond timer. How accurate
** this is depends on the underlying OS
*/
static void fn_time(void) {
  DEBUGFUNCMSGIN;
  if (*basicvars.current == '$') {
    basicvars.current++;
    fn_timedol();
  } else {
    push_int(mos_rdtime());
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_abs' returns the absolute value of the function's argument. The
** values are updated in place on the Basic stack
*/
static void fn_abs(void) {
  stackitem numtype;

  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  numtype = GET_TOPITEM;
  if (numtype == STACK_UINT8)
    return; /* No-op on unsigned 8-bit int */
  else if (numtype == STACK_INT)
    ABS_INT;
  else if (numtype == STACK_INT64)
    ABS_INT64;
  else if (numtype == STACK_FLOAT)
    ABS_FLOAT;
  else {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPENUM);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_acs' evalutes the arc cosine of its argument
*/
static void fn_acs(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  push_float(acos(pop_anynumfp()));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_adval' deals with the 'ADVAL' function. This is a BBC Micro-specific
** function that returns the current value of that machine's built-in A/D
** convertor. As per RISC OS, using the function for this purpose generates
** an error. 'ADVAL' can also be used to return the space left or the number
** of entries currently used in the various buffers within RISC OS for the
** serial port, parallel port and so on. A dummy value is returned for
** these
*/
static void fn_adval(void) {
  DEBUGFUNCMSGIN;
  push_int(mos_adval(eval_intfactor()));
  DEBUGFUNCMSGOUT;
}

/*
** fn_argc pushes the number of command line arguments on to the
** Basic stack
*/
static void fn_argc(void) {
  DEBUGFUNCMSGIN;
  push_int(basicvars.argcount);
  DEBUGFUNCMSGOUT;
}

/*
** fn_argvdol' pushes a copy of a command line parameter on to
** the Basic stack
*/
static void fn_argvdol(void) {
  int32 number, length;
  cmdarg *ap;
  char *cp;

  DEBUGFUNCMSGIN;
  number = eval_intfactor();    /* Fetch number of argument to push on to stack */
  if (number<0 || number>basicvars.argcount) {
    DEBUGFUNCMSGOUT;
    error(ERR_RANGE);
    return;
  }
  ap = basicvars.arglist;
  while (number > 0) {
    number--;
    ap = ap->nextarg;
  }    
  length = strlen(ap->argvalue);
  cp = alloc_string(length);
  if (length>0) memcpy(cp, ap->argvalue, length);
  push_strtemp(length, cp);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_asc' returns the character code for the first character of the string
** given as its argument or -1 if the string is the null string
*/
static void fn_asc(void) {
  basicstring descriptor;
  stackitem topitem;

  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  topitem = GET_TOPITEM;
  if (topitem == STACK_STRING || topitem == STACK_STRTEMP) {
    descriptor = pop_string();
    if (descriptor.stringlen == 0)      /* Null string returns -1 with ASC */
      push_int(-1);
    else {
      push_int(*descriptor.stringaddr & BYTEMASK);
      if (topitem == STACK_STRTEMP) free_string(descriptor);
    }
  }
  else {      /* String wanted */
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_asn' evalutes the arc sine of its argument
*/
static void fn_asn(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  push_float(asin(pop_anynumfp()));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_atn' evalutes the arc tangent of its argument
*/
static void fn_atn(void) {
  DEBUGFUNCMSGIN;
  if (*basicvars.current == '(') {
    float64 parmx;
    basicvars.current++;
    expression();
    parmx=pop_anynumfp();
    if(*basicvars.current != ',') {
      push_float(atan(parmx));
    } else {
      float64 parmy;
      basicvars.current++;
      expression();
      parmy=pop_anynumfp();
      push_float(atan2(parmx, parmy));
    }
    if (*basicvars.current != ')') {
      DEBUGFUNCMSGOUT;
      error(ERR_SYNTAX);
      return;
    }
    basicvars.current++;
  } else {
    (*factor_table[*basicvars.current])();
    push_float(atan(pop_anynumfp()));
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_beat' is one of the functions associated with the RISC OS sound system.
** 'BEAT' returns the current microbeat number.
*/
static void fn_beat(void) {
  DEBUGFUNCMSGIN;
  push_int(mos_rdbeat());
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_beats' is one of the functions associated with the RISC OS sound system.
** 'BEATS' returns the number of microbeats in a bar.
*/
void fn_beats(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  push_int(mos_rdbeats());
  DEBUGFUNCMSGOUT;
}

/*
** 'bget' returns the next byte from the file identified by the
** handle specified as its argument
*/
static void fn_bget(void) {
  DEBUGFUNCMSGIN;
  if (*basicvars.current != '#') {
    DEBUGFUNCMSGOUT;
    error(ERR_HASHMISS);
    return;
  }
  basicvars.current++;
  push_int(fileio_bget(eval_intfactor()));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_chr' converts the value given as its argument to a single
** character string
*/
static void fn_chr(void) {
  char *cp, value;

  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  value = pop_anynum32();
  cp = alloc_string(1);
  *cp=value;
  push_strtemp(1, cp);
  DEBUGFUNCMSGOUT;
}

/*
** fn_colour - Return the colour number of the colour
** which most closely matches the colour with red, green
** and blue components passed to it. The colour is matched
** against the colours available in the current screen
** mode
*/
void fn_colour(void) {
  int32 red, green, blue;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  if (*basicvars.current != '(') {     /* COLOUR must be followed by a '(' */
    DEBUGFUNCMSGOUT;
    error(ERR_SYNTAX);
    return;
  }
  basicvars.current++;
  red = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_SYNTAX);
    return;
  }
  basicvars.current++;
  green = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_SYNTAX);
    return;
  }
  basicvars.current++;
  blue = eval_integer();
  if (*basicvars.current != ')') {
    DEBUGFUNCMSGOUT;
    error(ERR_RPMISS);
    return;
  }
  basicvars.current++;
  push_int(emulate_colourfn(red, green, blue));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_cos' evaluates the cosine of its argument
*/
static void fn_cos(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  push_float(cos(pop_anynumfp()));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_count' returns the number of characters printed on the current
** line by 'PRINT'
*/
static void fn_count(void) {
  DEBUGFUNCMSGIN;
  push_int(basicvars.printcount);
  DEBUGFUNCMSGOUT;
}

/*
** 'get_arrayname' parses an array name and returns a pointer to its
** symbol table entry. On entry, 'basicvars.current' points at the
** array's variable token. It is left pointing at the byte after the
** pointer to the array's symbol table entry
*/
static variable *get_arrayname(void) {
  variable *vp = NULL;

  DEBUGFUNCMSGIN;
  if (*basicvars.current == BASTOKEN_ARRAYVAR)       /* Known reference */
    vp = GET_ADDRESS(basicvars.current, variable *);
  else if (*basicvars.current == BASTOKEN_XVAR) {    /* Reference not seen before */
    byte *base, *ep;
    base = GET_SRCADDR(basicvars.current);      /* Find address of array's name */
    ep = skip_name(base);
    vp = find_variable(base, ep-base);
    if (vp == NIL) {
      error(ERR_ARRAYMISS, tocstring(CAST(base, char *), ep-base));
      return NULL;
    }
    if ((vp->varflags & VAR_ARRAY) == 0) {  /* Not an array */
      DEBUGFUNCMSGOUT;
      error(ERR_VARARRAY);
      return NULL;
    }
    if (*(basicvars.current+LOFFSIZE+1) != ')') {      /* Array name must be suppled as 'array()' */
      DEBUGFUNCMSGOUT;
      error(ERR_RPMISS);
      return NULL;
    }
    *basicvars.current = BASTOKEN_ARRAYVAR;
    set_address(basicvars.current, vp);
  }
  else {        /* Not an array name */
    DEBUGFUNCMSGOUT;
    error(ERR_VARARRAY);        /* Name not found */
    return NULL;
  }
  if (vp->varentry.vararray == NIL) {     /* Array has not been dimensioned */
    DEBUGFUNCMSGOUT;
    error(ERR_NODIMS, vp->varname);
    return NULL;
  }
  basicvars.current+=LOFFSIZE+2;        /* Skip pointer to array and ')' */
  DEBUGFUNCMSGOUT;
  return vp;
}

/*
** 'fn_dim' handles the 'DIM' function. This returns either the number
** of dimensions the specified array has or the upper bound of the
** dimension given by the second parameter
*/
void fn_dim(void) {
  variable *vp;
  int32 dimension;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  if (*basicvars.current != '(') {     /* DIM must be followed by a '(' */
    DEBUGFUNCMSGOUT;
    error(ERR_SYNTAX);
    return;
  }
  basicvars.current++;
  vp = get_arrayname();
  switch (*basicvars.current) {
  case ',':     /* Got 'array(),<x>) - Return upper bound of dimension <x> */
    basicvars.current++;
    dimension = eval_integer();         /* Get dimension number */
    if (*basicvars.current != ')') {
      DEBUGFUNCMSGOUT;
      error(ERR_RPMISS);
      return;
    }
    basicvars.current++;        /* Skip the trailing ')' */
    if (dimension<1 || dimension>vp->varentry.vararray->dimcount) {
      DEBUGFUNCMSGOUT;
      error(ERR_DIMRANGE);
      return;
    }
    push_int(vp->varentry.vararray->dimsize[dimension-1]-1);
    break;
  case ')':     /* Got 'array())' - Return the number of dimensions */
    push_int(vp->varentry.vararray->dimcount);
    basicvars.current++;
    break;
  default:
    DEBUGFUNCMSGOUT;
    error(ERR_SYNTAX);
  }
  DEBUGFUNCMSGOUT;
}

/*
** The function 'DEG' converts an angrle expressed in radians to degrees
*/
static void fn_deg(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  push_float(pop_anynumfp()*RADCONV);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_end' deals with the 'END' function, which pushes the address of
** the top of the Basic program and variables on to the Basic stack
*/
void fn_end(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  if (matrixflags.pseudovarsunsigned) {
    push_int64(resize32((size_t)basicvars.vartop));
  } else {
    push_int64((size_t)basicvars.vartop);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_eof' deals with the 'EOF' function, which returns 'TRUE' if the
** 'at end of file' flag is set for the file specified
*/
static void fn_eof(void) {
  int32 handle;

  DEBUGFUNCMSGIN;
  if (*basicvars.current != '#') {
    DEBUGFUNCMSGOUT;
    error(ERR_HASHMISS);
    return;
  }
  basicvars.current++;
  handle = eval_intfactor();
  push_int(fileio_eof(handle) ? BASTRUE : BASFALSE);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_erl' pushes the line number of the line at which the last error
** occured
*/
static void fn_erl(void) {
  DEBUGFUNCMSGIN;
  push_int(basicvars.error_line);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_err' pushes the error number of the last error on to the Basic
** stack
*/
static void fn_err(void) {
  DEBUGFUNCMSGIN;
  push_int(basicvars.error_number);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_eval' deals with the function 'eval'
** The argument of the function is tokenized and stored in
** 'evalexpr'. The current value of 'basicvars.current' is
** saved locally, but this is not the proper place if an
** error occurs in the expression being evaluated as the
** current will not be pointing into the Basic program. I
** think the value should be saved on the Basic stack.
*/
static void fn_eval(void) {
  stackitem stringtype;
  basicstring descriptor;
  byte evalexpr[MAXSTATELEN];

  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  descriptor = pop_string();
  memmove(basicvars.stringwork, descriptor.stringaddr, descriptor.stringlen);
  basicvars.stringwork[descriptor.stringlen] = asc_NUL; /* Now have a null-terminated version of string */
  if (stringtype == STACK_STRTEMP) free_string(descriptor);
  tokenize(basicvars.stringwork, evalexpr, NOLINE, FALSE);      /* 'tokenise' leaves its results in 'thisline' */
  save_current();               /* Save pointer to current position in expression */
  basicvars.current = FIND_EXEC(evalexpr);
  expression();
  if (basicvars.runflags.flag_cosmetic && (*basicvars.current != asc_NUL)) {
    DEBUGFUNCMSGOUT;
    error(ERR_SYNTAX);
    return;
  }
  restore_current();
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_exp' evaluates the exponentinal function of its argument
*/
static void fn_exp(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  push_float(exp(pop_anynumfp()));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_false' pushes the value which represents 'FALSE' on to the Basic stack
*/
void fn_false(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  push_int(BASFALSE);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_get' implements the 'get' function which reads a character from the
** keyboard and saves it on the Basic stack as a number
*/
static void fn_get(void) {
  DEBUGFUNCMSGIN;
  if (*basicvars.current == '(') {      /* Have encountered the 'GET(x,y)' version */
    int32 x, y;
    basicvars.current++;
    x = eval_integer();
    if (*basicvars.current != ',') {
      DEBUGFUNCMSGOUT;
      error(ERR_COMISS);
      return;
    }
    basicvars.current++;
    y = eval_integer();
    if (*basicvars.current != ')') {
      DEBUGFUNCMSGOUT;
      error(ERR_RPMISS);
      return;
    }
    basicvars.current++;
    push_int(get_character_at_pos(x, y));
  } else {
    int ch;
    do {
      ch=kbd_get() & 0xFF;
    } while (ch==0);
    push_int(ch);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_getdol' implements the 'get$' function which either reads a character
** from the keyboard or a string from a file
*/
static void fn_getdol(void) {
  char *cp;
  int ch;
  int32 handle, count;

  DEBUGFUNCMSGIN;
  if (*basicvars.current == '(') {      /* Have encountered the 'GET$(x,y)' version */
    int32 x, y;
    basicvars.current++;
    x = eval_integer();
    if (*basicvars.current != ',') {
      DEBUGFUNCMSGOUT;
      error(ERR_COMISS);
      return;
    }
    basicvars.current++;
    y = eval_integer();
    if (*basicvars.current != ')') {
      DEBUGFUNCMSGOUT;
      error(ERR_RPMISS);
      return;
    }
    basicvars.current++;
    cp = alloc_string(1);
    *cp = get_character_at_pos(x, y);
    push_strtemp(1, cp);
  } else if (*basicvars.current == '#') {       /* Have encountered the 'GET$#' version */
    basicvars.current++;
    handle = eval_intfactor();
    count = fileio_getdol(handle, basicvars.stringwork);
    cp = alloc_string(count);
    memcpy(cp, basicvars.stringwork, count);
    push_strtemp(count, cp);
  }
  else {        /* Normal 'GET$' - Return character read as a string */
    cp = alloc_string(1);
    do {
      ch=kbd_get() & 0xFF;
    } while (ch==0);
    *cp = ch;
    push_strtemp(1, cp);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_inkey' deals with the 'inkey' function. Under RISC OS this is just a call to
** OS_Byte 129 under a different name.
*/
static void fn_inkey(void) {
  DEBUGFUNCMSGIN;
  push_int(kbd_inkey(eval_intfactor()));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_inkeydol' carries out the same functions as 'fn_inkey' except that the
** result is returned as a string. Where the result is -1, a null string is
** saved on the Basic stack.
*/
static void fn_inkeydol(void) {
  int32 result;
  char *cp;

  DEBUGFUNCMSGIN;
  result=kbd_inkey(eval_intfactor());
  if (result == -1) {
    cp = alloc_string(0);
    push_strtemp(0, cp);
  }
  else {
    cp = alloc_string(1);
    *cp = result;
    push_strtemp(1, cp);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_instr' deals with the 'INSTR' function.
** Note: in the case where the search string is the null string, the value
** returned by BBC Basic is not what the Acorn documentation says it should be.
** The manuals say that the function should return either one or the starting
** position of the search if it was specified. It only does this if the starting
** position is one or two. If greater than two, zero is returned. 'fn_instr'
** mimics this behaviour
*/
static void fn_instr(void) {
  basicstring needle, haystack;
  stackitem needtype, haytype;
  char *hp, *p;
  int32 start, count;
  char first;

  DEBUGFUNCMSGIN;
  expression();
  if (*basicvars.current != ',') {     /* ',' missing */
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  haytype = GET_TOPITEM;
  if (haytype != STACK_STRING && haytype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  haystack = pop_string();
  expression();
  needtype = GET_TOPITEM;
  if (needtype != STACK_STRING && needtype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  needle = pop_string();
  if (*basicvars.current == ',') {      /* Starting position given */
    basicvars.current++;
    start = eval_integer();
    if (start<1) start = 1;
  }
  else {        /* Set starting position to one */
    start = 1;
  }
  if (*basicvars.current != ')') {
    DEBUGFUNCMSGOUT;
    error(ERR_RPMISS);
    return;
  }
  basicvars.current++;
/*
** After finding the string to be searched (haystack) and what to look
** for (needle) and the starting position (start), deal with the special
** cases first. First, check if the search string is longer than the
** original string or would extend beyond the end of that string then
** deal with a zero-length target string. If anything is left after
** this, there is nothing else for it but to search for the target
** string
*/
  if (needle.stringlen>haystack.stringlen-start+1)
    push_int(0);        /* Always returns zero if search string is longer than main string */
  else if (needle.stringlen == 0) {     /* Search string length is zero */
    if (haystack.stringlen == 0)        /* Both string are the null string */
      push_int(1);
    else if (start<3)
      push_int(start);
    else {
      push_int(0);
    }
  }
  else {        /* Will have to search string */
    hp = haystack.stringaddr+start-1;   /* Start searching from this address */
    first = *needle.stringaddr;
    count = haystack.stringaddr+haystack.stringlen-hp;  /* Count of chars in original string to check */
    if (needle.stringlen == 1) {                /* Looking for a single character */
      p = memchr(hp, first, count);
      if (p == NIL)     /* Did not find the character */
        push_int(0);
      else {    /* Found character - Place its offset (from 1) on stack */
        push_int(p-haystack.stringaddr+1);
      }
    }
    else {      /* Looking for more than one character */
      do {
        p = memchr(hp, first, count);   /* Look for first char in string */
        if (p == NIL)
          count = 0;    /* Character not found */
        else {  /* Found an occurence of the first search char in the original string */
          count-=(p-hp);
          if (count<needle.stringlen)   /* Chars left to search is less that search string length */
            count = 0;
          else {
            if (memcmp(p, needle.stringaddr, needle.stringlen) == 0) break;
            hp = p+1;
            count--;
          }
        }
      } while (count>0);
      if (count == 0)   /* Search string not found */
        push_int(0);
      else {            /* Push offset (from 1) at which string was found on to stack */
        push_int(p-haystack.stringaddr+1);
      }
    }
  }
  if (haytype == STACK_STRTEMP) free_string(haystack);
  if (needtype == STACK_STRTEMP) free_string(needle);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_int' implements the 'INT' function. It pushes the integer part
** of its argument on to the Basic stack
*/
static void fn_int(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  if (GET_TOPITEM == STACK_FLOAT) {
    if (matrixflags.int_uses_float) {
      float64 localfloat = floor(pop_float());
      int64 localint64 = (int64)localfloat;
      if (localint64 == localfloat) {
        push_varyint(localint64);
      } else {
        push_float(localfloat);
      }
    } else {
      push_int(TOINT(floor(pop_float())));
    }
  } else if (GET_TOPITEM != STACK_INT && GET_TOPITEM != STACK_UINT8 && GET_TOPITEM != STACK_INT64) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPENUM);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_len' pushes the length of its string argument on to the Basic stack
*/
static void fn_len(void) {
  basicstring descriptor;
  stackitem stringtype;

  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  stringtype = GET_TOPITEM;
  if (stringtype == STACK_STRING || stringtype == STACK_STRTEMP) {
    descriptor = pop_string();
    push_int(descriptor.stringlen);
    if (stringtype == STACK_STRTEMP) free_string(descriptor);
  }
  else {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_listofn' pushes the current 'LISTO' value on to the stack
*/
static void fn_listofn(void) {
  DEBUGFUNCMSGIN;
  push_int(get_listo());
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_ln' evaluates the natural log of its argument
*/
static void fn_ln(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  floatvalue = pop_anynumfp();
  if (floatvalue<=0.0) {
    error(ERR_LOGRANGE);
    return;
  }
  push_float(log(floatvalue));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_log' computes the base 10 log of its argument
*/
static void fn_log(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  floatvalue = pop_anynumfp();
  if (floatvalue<=0.0) {
    error(ERR_LOGRANGE);
    return;
  }
  push_float(log10(floatvalue));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_mod' deals with 'mod' when it is used as a function. It
** returns the modulus (square root of the sum of the squares)
** of an array
*/
void fn_mod(void) {
  static float64 fpsum;
  int32 n, elements;
  variable *vp;

  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip MOD token */
  if(*basicvars.current == '(') {       /* One level of parentheses is allowed */
    basicvars.current++;
    vp = get_arrayname();
    if (*basicvars.current != ')') {
      DEBUGFUNCMSGOUT;
      error(ERR_RPMISS);
      return;
    }
    basicvars.current++;
  }
  else {
    vp = get_arrayname();
  }
  if (vp == NULL) {
    error(ERR_BROKEN, __LINE__, "functions");
    return;
  }
  elements = vp->varentry.vararray->arrsize;
  switch (vp->varflags) {
  case VAR_INTARRAY: {  /* Calculate the modulus of an integer array */
    int32 *p = vp->varentry.vararray->arraystart.intbase;
    fpsum = 0;
    for (n=0; n<elements; n++) fpsum+=TOFLOAT(p[n])*TOFLOAT(p[n]);
    push_float(sqrt(fpsum));
    break;
  }
  case VAR_INT64ARRAY: {        /* Calculate the modulus of an integer array */
    int64 *p = vp->varentry.vararray->arraystart.int64base;
    fpsum = 0;
    for (n=0; n<elements; n++) fpsum+=TOFLOAT(p[n])*TOFLOAT(p[n]);
    push_float(sqrt(fpsum));
    break;
  }
  case VAR_FLOATARRAY: {        /* Calculate the modulus of a floating point array */
    float64 *p = vp->varentry.vararray->arraystart.floatbase;
    fpsum = 0;
    for (n=0; n<elements; n++) fpsum+=p[n]*p[n];
    push_float(sqrt(fpsum));
    break;
  }
  case VAR_STRARRAY:
    DEBUGFUNCMSGOUT;
    error(ERR_NUMARRAY);        /* Numeric array wanted */
    return;
  default:      /* Bad 'varflags' value found */
    DEBUGFUNCMSGOUT;
    error(ERR_BROKEN, __LINE__, "functions");
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_mode' pushes the current screen mode number on to the Basic
** stack. Under operating systems other than RISC OS this might have
** no meaning
*/
void fn_mode(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  push_int(emulate_modefn());
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_not' implements the 'not' function, pushing the bitwise
** 'not' of its argument on to the stack
*/
void fn_not(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip NOT token */
  (*factor_table[*basicvars.current])();
  push_varyint(~pop_anynum64());
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_openin deals with the function OPENIN which opens a file for input
*/
static void fn_openin(void) {
  stackitem stringtype;
  basicstring descriptor;

  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  descriptor = pop_string();
  push_int(fileio_openin(descriptor.stringaddr, descriptor.stringlen));
  if (stringtype == STACK_STRTEMP) free_string(descriptor);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_openout' deals the function 'OPENOUT', which opens a file for
** output
*/
static void fn_openout(void) {
  stackitem stringtype;
  basicstring descriptor;

  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  descriptor = pop_string();
  push_int(fileio_openout(descriptor.stringaddr, descriptor.stringlen));
  if (stringtype == STACK_STRTEMP) free_string(descriptor);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_openup' deals the function 'OPENUP', which opens a file for
** both input and output
*/
static void fn_openup(void) {
  stackitem stringtype;
  basicstring descriptor;

  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  descriptor = pop_string();
  push_int(fileio_openup(descriptor.stringaddr, descriptor.stringlen));
  if (stringtype == STACK_STRTEMP) free_string(descriptor);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_pi' pushes the constant value pi on to the Basic stack
*/
static void fn_pi(void) {
  DEBUGFUNCMSGIN;
  push_float(PI);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_point' emulates the Basic function 'POINT'
*/
static void fn_pointfn(void) {
  int32 x, y;

  DEBUGFUNCMSGIN;
  x = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  y = eval_integer();
  if (*basicvars.current != ')') {
    DEBUGFUNCMSGOUT;
    error(ERR_RPMISS);
    return;
  }
  basicvars.current++;
  push_int(emulate_pointfn(x, y));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_pos' emulates the Basic function 'POS'
*/
static void fn_pos(void) {
  DEBUGFUNCMSGIN;
  push_int(emulate_pos());
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_quit' saves 'true' or' false' on the stack depending
**on the value of the 'quit interpreter at end of run' flag
*/
void fn_quit(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  push_int(basicvars.runflags.quitatend);
  DEBUGFUNCMSGOUT;
}

/*
** 'rad' converts the value on top of the Basic stack from degrees
** to radians
*/
static void fn_rad(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  push_float(pop_anynumfp()/RADCONV);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_reportdol' handles the 'REPORT$' function, which puts a
** copy of the last error message on the Basic stack
*/
static void fn_reportdol(void) {
  char *p;
  int32 length;

  DEBUGFUNCMSGIN;
  length = strlen(get_lasterror());
  p = alloc_string(length);
  memmove(p, get_lasterror(), length);
  push_strtemp(length, p);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_retcode' pushes the return code from the last command
** issued via OSCLI or '*' on to the Basic stack
*/
static void fn_retcode(void) {
  DEBUGFUNCMSGIN;
  push_int(basicvars.retcode);
  DEBUGFUNCMSGOUT;
}

/*
** 'nextrandom' updates the pseudo-random number generator
**
** Based on the BASIC II pseudo-random number generator
*/
static void nextrandom(void) {
  int n;

  DEBUGFUNCMSGIN;
  for (n=0; n<32; n++) {
    int newbit = ((lastrandom>>19) ^ randomoverflow) & 1;
    randomoverflow = lastrandom>>31;
    lastrandom = (lastrandom<<1) | newbit;
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'randomfraction' returns the pseudo-random number as float fraction
*/
static float64 randomfraction(void) {
  uint32 reversed = ((lastrandom>>24)&0xFF)|((lastrandom>>8)&0xFF00)|((lastrandom<<8)&0xFF0000)|((lastrandom<<24)&0xFF000000);

  DEBUGFUNCMSGIN;
  DEBUGFUNCMSGOUT;
  return TOFLOAT(reversed) / 4294967296.0;
}

/*
** 'fn_rnd' evaluates the function 'RND'. See also fn_rndpar
*/
static void fn_rnd(void) {
  DEBUGFUNCMSGIN;
  nextrandom();
  push_int(lastrandom);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_rndpar' evaluates the function 'RND('. See also fn_rnd
*/
static void fn_rndpar(void) {
  int32 value;

  DEBUGFUNCMSGIN;
  value = eval_integer();
  if (*basicvars.current != ')') {
    DEBUGFUNCMSGOUT;
    error(ERR_RPMISS);
    return;
  }
  basicvars.current++;
  if (value<0) {        /* Negative value = reseed random number generator */
    lastrandom = value;
    randomoverflow = 0;
    push_int(value);
  } else if (value == 0) {      /* Return last result */
    push_float(randomfraction());
  } else if (value == 1) {      /* Return value in range 0 to 0.9999999999 */
    nextrandom();
    push_float(randomfraction());
  } else {
    nextrandom();
    push_int(TOINT(1+randomfraction()*TOFLOAT(value)));
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_sgn' pushes +1, 0 or -1 on to the Basic stack depending on
** whether the value there is positive, zero or negative
*/
static void fn_sgn(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  if (TOPITEMISINT) {
    push_int(sgni(pop_anyint()));
  } else if (GET_TOPITEM == STACK_FLOAT) {
    push_int(sgnf(pop_float()));
  } else {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPENUM);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_sin' evaluates the sine of its argument
*/
static void fn_sin(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  push_float(sin(pop_anynumfp()));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_sqr' evaluates the square root of its argument
*/
static void fn_sqr(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  floatvalue = pop_anynumfp();
  if (floatvalue<0.0) {
    DEBUGFUNCMSGOUT;
    error(ERR_NEGROOT);
    return;
  }
  push_float(sqrt(floatvalue));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_str' converts its numeric argument to a character string. The
** number is converted to its hex representation if 'STR$' is followed
** with a '~'
*/
static void fn_str(void) {
  stackitem resultype;
  boolean ishex;
  int32 length = 0;
  char *cp;

  DEBUGFUNCMSGIN;
  ishex = *basicvars.current == '~';
  if (ishex) basicvars.current++;
  (*factor_table[*basicvars.current])();
  resultype=GET_TOPITEM;
  if (IS_NUMERIC(resultype)) {
    if (ishex)
      if (matrixflags.hex64)
        length = snprintf(basicvars.stringwork, MAXSTRING, "%llX", pop_anynum64());
      else
        length = snprintf(basicvars.stringwork, MAXSTRING, "%X", pop_anynum32());
    else {
      int32 format, numdigits;
      char *fmt, *bufptr;
      format = basicvars.staticvars[ATPERCENT].varentry.varinteger;
      if ((format & STRUSECHK) == 0) format = STRFORMAT;        /* Use predefined format, not @% */
      switch ((format>>2*BYTESHIFT) & BYTEMASK) {       /* Determine format of floating point values */
      case FORMAT_E:
       fmt = "%.*E";
        break;
      case FORMAT_F:
        fmt = "%.*F";
        break;
      default:  /* Assume anything else will be general format */
        fmt = "%.*G";
      }
      numdigits = (format>>BYTESHIFT) & BYTEMASK;
      if (numdigits == 0 && ((format>>2*BYTESHIFT) & BYTEMASK) != FORMAT_F) numdigits = DEFDIGITS;
      if (((format>>2*BYTESHIFT) & BYTEMASK) == FORMAT_E) numdigits--;
      if (numdigits > 19 ) numdigits = 19; /* Maximum meaningful length */
      if (resultype == STACK_FLOAT) {
        length = snprintf(basicvars.stringwork, MAXSTRING, fmt, numdigits, pop_anynumfp());
      } else {
        int64 fromstack=pop_anynum64();
        length = snprintf(basicvars.stringwork, MAXSTRING, "%lld", fromstack);
        if (length > numdigits) {
          length = snprintf(basicvars.stringwork, MAXSTRING, fmt, numdigits, TOFLOAT(fromstack));
        }
      }
      if (format & COMMADPT) decimaltocomma(basicvars.stringwork, length);
      /* Hack to mangle the exponent format to BBC-style rather than C-style */
      bufptr = strchr(basicvars.stringwork,'E');
      if(bufptr) {
        bufptr++;
        if (*bufptr == '+') {
          /* Not worried about the length value, the buffer is 64K long and we 
           * will never get any numbers that long! */
          memmove(bufptr, bufptr+1, length);
          length--;
        } else bufptr++;
        while (*bufptr == '0' && *(bufptr+1) != '\0') {
          memmove(bufptr, bufptr+1, length);
          length--;
        }
      }
    }
  } else {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPENUM);
    return;
  }
  cp = alloc_string(length);
  memcpy(cp, basicvars.stringwork, length);
  push_strtemp(length, cp);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_string' implements the 'STRING$' function
*/
static void fn_string(void) {
  int32 count, newlen;
  basicstring descriptor;
  char* base, *cp;
  stackitem stringtype;

  DEBUGFUNCMSGIN;
  count = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  expression();
  if (*basicvars.current != ')') {
    DEBUGFUNCMSGOUT;
    error(ERR_RPMISS);
    return;
  }
  basicvars.current++;
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  if (count == 1) return;       /* Leave things as they are if repeat count is 1 */
  descriptor = pop_string();
  if (count<=0)
    newlen = 0;
  else  {
    newlen = count*descriptor.stringlen;
    if (newlen>MAXSTRING) { /* New string is too long */
      DEBUGFUNCMSGOUT;
      error(ERR_STRINGLEN);
      return;
    }
  }
  base = cp = alloc_string(newlen);
  while (count>0) {
    memmove(cp, descriptor.stringaddr, descriptor.stringlen);
    cp+=descriptor.stringlen;
    count--;
  }
  if (stringtype == STACK_STRTEMP) free_string(descriptor);
  push_strtemp(newlen, base);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_sum' implements the Basic functions 'SUM' and 'SUM LEN'. 'SUM'
** either calculates the sum of all the elements if a numeric array or
** concatenates them to form one large string if a string array.
** 'SUM LEN' calculates the total length of all the strings in a
** string array
*/
static void fn_sum(void) {
  int32 n, elements;
  variable *vp;
  boolean sumlen;

  DEBUGFUNCMSGIN;
  sumlen = *basicvars.current == TYPE_FUNCTION && *(basicvars.current+1) == BASTOKEN_LEN;
  if (sumlen) basicvars.current+=2;     /* Skip the 'LEN' token */
  if(*basicvars.current == '(') {       /* One level of parentheses is allowed */
    basicvars.current++;
    vp = get_arrayname();
    if (*basicvars.current != ')') {
      DEBUGFUNCMSGOUT;
      error(ERR_RPMISS);
      return;
    }
    basicvars.current++;
  }
  else {
    vp = get_arrayname();
  }
  if (vp == NULL) {
    error(ERR_BROKEN, __LINE__, "functions");
    return;
  }
  elements = vp->varentry.vararray->arrsize;
  if (sumlen) {         /* Got 'SUM LEN' */
    int32 length;
    basicstring *p;
    if (vp->varflags != VAR_STRARRAY) {       /* Array is not a string array */
      DEBUGFUNCMSGOUT;
      error(ERR_TYPESTR);
      return;
    }
    p = vp->varentry.vararray->arraystart.stringbase;
    length = 0;
    for (n=0; n<elements; n++) length+=p[n].stringlen;  /* Find length of all strings in array */
    push_int(length);
  }
  else {        /* Got 'SUM' */
    switch (vp->varflags) {
    case VAR_INTARRAY: {        /* Calculate sum of elements in an integer array */
      int32 intsum, *p;
      p = vp->varentry.vararray->arraystart.intbase;
      intsum = 0;
      for (n=0; n<elements; n++) intsum+=p[n];
      push_int(intsum);
      break;
    }
    case VAR_INT64ARRAY: {      /* Calculate sum of elements in an integer array */
      int64 intsum, *p;
      p = vp->varentry.vararray->arraystart.int64base;
      intsum = 0;
      for (n=0; n<elements; n++) intsum+=p[n];
      push_int(intsum);
      break;
    }
    case VAR_FLOATARRAY: {      /* Calculate sum of elements in a floating point array */
      float64 fpsum, *p;
      fpsum = 0;
      p = vp->varentry.vararray->arraystart.floatbase;
      for (n=0; n<elements; n++) fpsum+=p[n];
      push_float(fpsum);
      break;
    }
    case VAR_STRARRAY: {        /* Concatenate all strings in a string array */
      int32 length;
      char *cp, *cp2;
      basicstring *p;
      p = vp->varentry.vararray->arraystart.stringbase;
      length = 0;
      for (n=0; n<elements; n++) length+=p[n].stringlen;    /* Find length of result string */
      if (length>MAXSTRING) {              /* String is too long */
        DEBUGFUNCMSGOUT;
        error(ERR_STRINGLEN);
        return;
      }
      cp = cp2 = alloc_string(length);  /* Grab enough memory to hold the result string */
      if (length>0) {
        for (n=0; n<elements; n++) {    /* Concatenate strings */
          int32 strlen = p[n].stringlen;
          if (strlen>0) {       /* Ignore zero-length strings */
            memmove(cp2, p[n].stringaddr, strlen);
            cp2+=strlen;
          }
        }
      }
      push_strtemp(length, cp);
      break;
    }
    default:    /* Bad 'varflags' value found */
      DEBUGFUNCMSGOUT;
      error(ERR_BROKEN, __LINE__, "functions");
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_tan' calculates the tangent of its argument
*/
static void fn_tan(void) {
  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  push_float(tan(pop_anynumfp()));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_tempofn' pushes the value returned by the Basic function
** 'TEMPO' on to the stack
*/
static void fn_tempofn(void) {
  DEBUGFUNCMSGIN;
  push_int(mos_rdtempo());
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_tint' deals with 'TINT' when used as a function, pushing
** the 'tint' value of point (x,y) on the screen on to the stack
*/
void fn_tint(void) {
  int32 x, y;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  if (*basicvars.current != '(') {
    DEBUGFUNCMSGOUT;
    error(ERR_LPMISS);
    return;
  }
  basicvars.current++;
  x = eval_integer();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  y = eval_integer();
  if (*basicvars.current != ')') {
    DEBUGFUNCMSGOUT;
    error(ERR_RPMISS);
    return;
  }
  basicvars.current++;
  push_int(emulate_tintfn(x, y));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_top' pushes the address of the end of the Basic program
** itself on to the Basic stack.
** Note that 'TOP' is encoded as the token for 'TO' followed by
** the letter 'P'. There is no token for 'TOP'. This is the way
** all of Acorn's BASIC interpreters work.
*/
void fn_top(void) {
  byte *p;

  DEBUGFUNCMSGIN;
  basicvars.current++;          /* Skip the 'TO' token */
  if (*basicvars.current != BASTOKEN_XVAR) { /* 'TO' is not followed by a variable name */
    DEBUGFUNCMSGOUT;
    error(ERR_SYNTAX);
    return;
  }
  p = GET_SRCADDR(basicvars.current);           /* Find the address of the variable */
  if (*p != 'P') {            /* But it does not start with the letter 'P' */
    DEBUGFUNCMSGOUT;
    error(ERR_SYNTAX);
    return;
  }
  basicvars.current+=LOFFSIZE + 1;
  if (matrixflags.pseudovarsunsigned) {
    push_int64(resize32((size_t)basicvars.top));
  } else {
    push_int64((size_t)basicvars.top);
  }
  DEBUGFUNCMSGOUT;
}

/*
** 'trace' returns the handle of the file to which trace output
** is written
*/
void fn_trace(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  push_int(basicvars.tracehandle);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_true' pushes the value that Basic uses to represent 'TRUE' on to
** the stack
*/
void fn_true(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;
  push_int(BASTRUE);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_usr' is called to deal with the Basic function 'USR'. This
** allows machine code routines to be called from a Basic program.
** It is probably safer to say that this function is unsupported
*/
static void fn_usr(void) {
  DEBUGFUNCMSGIN;
  push_int(mos_usr(eval_intfactor()));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_val' converts a number held as a character string to binary. It
** interprets the string as a number as far as the first character that
** is not a valid digit, decimal point or 'E' (exponent mark). The number
** can be preceded with a sign. Both floating point and integer values
** are dealt with, but must be decimal values. The result is left on
** the Basic stack
*/
static void fn_val(void) {
  stackitem stringtype;
  basicstring descriptor;
  boolean isint;
  int32 intvalue;
  int64 int64value;
  static float64 fpvalue;

  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  descriptor = pop_string();
  if (descriptor.stringlen == 0)
    push_int(0);        /* Nothing to do */
  else {
    char *cp;
    memmove(basicvars.stringwork, descriptor.stringaddr, descriptor.stringlen);
    basicvars.stringwork[descriptor.stringlen] = asc_NUL;
    if (stringtype == STACK_STRTEMP) free_string(descriptor);
    cp = todecimal(basicvars.stringwork, &isint, &intvalue, &int64value, &fpvalue);
    if (cp == NIL) {    /* Error found when converting number */
      DEBUGFUNCMSGOUT;
      error(intvalue);  /* 'intvalue' is used to return the precise error */
      return;
    }
    if (isint)
      if (intvalue == int64value)
        push_int(intvalue);
      else
        push_int64(int64value);
    else {
      push_float(fpvalue);
    }
  }
  DEBUGFUNCMSGOUT;
}

/*
** fn_vdu - Handle VDU when it is used as a function. It pushes
** the value of the VDU variable after the function name
*/
void fn_vdu(void) {
  int variable;

  DEBUGFUNCMSGIN;
  basicvars.current++;
  variable = eval_intfactor();  /* Number of VDU variable */
  push_int64(emulate_vdufn(variable));
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_verify' handles the Basic function 'VERIFY'
*/
static void fn_verify(void) {
  stackitem stringtype, veritype;
  basicstring string, verify;
  int32 start, n;
  byte present[256];

  DEBUGFUNCMSGIN;
  expression();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  string = pop_string();
  if (*basicvars.current != ',') {
    DEBUGFUNCMSGOUT;
    error(ERR_COMISS);
    return;
  }
  basicvars.current++;
  expression();
  veritype = GET_TOPITEM;
  if (veritype != STACK_STRING && veritype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  verify = pop_string();
  if (*basicvars.current == ',') {      /* Start position supplied */
    basicvars.current++;
    start = eval_integer();
    if (start<1) start = 1;
  }
  else {        /* Set starting position to one */
    start = 1;
  }
  if (*basicvars.current != ')') {
    DEBUGFUNCMSGOUT;
    error(ERR_RPMISS);
    return;
  }
  basicvars.current++;
/*
** Start by dealing with the special cases. These are:
** 1) Start position is greater than the string length.
** 2) String is a null string (special case of 1).
** 3) Verify string is a null string.
** In cases 1) and 2) the value returned by the function is zero.
** In case 2) the start position is returned.
*/
  if (start>string.stringlen || verify.stringlen == 0) {
    if (veritype == STACK_STRTEMP) free_string(verify);
    if (stringtype == STACK_STRTEMP) free_string(string);
    if (verify.stringlen == 0)
      push_int(start);
    else {
      push_int(0);
    }
  }
/* Build a table of the characters present in the verify string */
  memset(present, FALSE, sizeof(present));
  for (n=0; n<verify.stringlen; n++) present[CAST(verify.stringaddr[n], byte)] = TRUE;
  start--;      /* Convert start index to offset in string */
/* Now ensure that all characters in string are in the verify string */
  while (start<string.stringlen && present[CAST(string.stringaddr[start], byte)]) start++;
  if (start == string.stringlen)        /* All characters are present and correct */
    push_int(0);
  else {        /* Character found that is not in the verify string */
    push_int(start+1);  /* Push its index on to the stack */
  }
  if (veritype == STACK_STRTEMP) free_string(verify);
  if (stringtype == STACK_STRTEMP) free_string(string);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_vpos' pushes the row number in which the text cursor is to be found
** on to the Basic stack
*/
static void fn_vpos(void) {
  DEBUGFUNCMSGIN;
  push_int(emulate_vpos());
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_width' pushes the current value of 'WIDTH' on to the Basic stack
*/
void fn_width(void) {
  DEBUGFUNCMSGIN;
  basicvars.current++;  /* Skip WIDTH token */
  push_int(basicvars.printwidth);
  DEBUGFUNCMSGOUT;
}

/*
** 'fn_xlatefol' either converts the string argument to lower case
** or translates it using the user-supplied translate table. The
** translated string is pushed back on to the Basic stack
*/
static void fn_xlatedol(void) {
  stackitem stringtype, transtype;
  basicstring string, transtring = {0, NULL};
  basicarray *transarray = NULL;
  char *cp;
  int32 n;

  DEBUGFUNCMSGIN;
  expression();
  stringtype = GET_TOPITEM;
  if (stringtype != STACK_STRING && stringtype != STACK_STRTEMP) {
    DEBUGFUNCMSGOUT;
    error(ERR_TYPESTR);
    return;
  }
  string = pop_string();
  if (*basicvars.current == ',') {  /* Got user-supplied translate table */
    byte ch;

    basicvars.current++;
    expression();
    if (*basicvars.current != ')') {
      DEBUGFUNCMSGOUT;
      error(ERR_RPMISS);
      return;
    }
    basicvars.current++;        /* Skip the ')' */
    transtype = GET_TOPITEM;
    if (transtype == STACK_STRING || transtype == STACK_STRTEMP)
      transtring = pop_string();
    else if (transtype == STACK_STRARRAY) {
      transarray = pop_array();
      if (transarray->dimcount != 1) {      /* Must be a 1-D array */
        DEBUGFUNCMSGOUT;
        error(ERR_NOTONEDIM);
        return;
      }
    }
    else {
      DEBUGFUNCMSGOUT;
      error(ERR_TYPESTR);
      return;
    }
/* If the string or table length is zero then there is nothing to do */
    if (string.stringlen == 0 || (transtype != STACK_STRARRAY && transtring.stringlen == 0)) {
      push_string(string);      /* Put the old string back on the stack */
      return;
    }
    if (stringtype == STACK_STRING) {   /* Have to make a copy of the string to modify */
      cp = alloc_string(string.stringlen);
      memmove(cp, string.stringaddr, string.stringlen);
    }
    else {
      cp = string.stringaddr;
    }
/*
** Translate the string according to the user-supplied translate
** table. The table can be either a string or a string array.
** Only the characters that lie in the range covered by the
** translate table are altered, for example, if the translate
** table string is 100 characters long, only characters in the
** original string with an ASCII code in the range 0 to 99 are
** changed.
*/
    if (transtype == STACK_STRARRAY) {          /* Translate table is an array */
      int32 highcode = transarray->dimsize[0];
      basicstring *arraybase = transarray->arraystart.stringbase;
      for (n=0; n<string.stringlen; n++) {
        ch = CAST(cp[n], byte);         /* Must work with unsigned characters */
        if (ch<highcode && arraybase[ch].stringlen>0) cp[n] = arraybase[ch].stringaddr[0];
      }
    }
    else {
      for (n=0; n<string.stringlen; n++) {
        ch = CAST(cp[n], byte);         /* Must work with unsigned characters */
        if (ch<transtring.stringlen) cp[n] = transtring.stringaddr[ch];
      }
       if (transtype == STACK_STRTEMP) free_string(transtring);
    }
    push_strtemp(string.stringlen, cp);
  }
  else if (*basicvars.current != ')') {
    DEBUGFUNCMSGOUT;
    error(ERR_RPMISS);  /* Must have a ')' next */
    return;
  } else {
/* Translate string to lower case */
    basicvars.current++;        /* Skip the ')' */
    if (string.stringlen == 0) {        /* String length is zero */
      push_string(string);      /* So put the old string back on the stack */
      return;
    }
    if (stringtype == STACK_STRING) {   /* Have to make a copy of the string to modify */
      cp = alloc_string(string.stringlen);
      memmove(cp, string.stringaddr, string.stringlen);
    }
    else {
      cp = string.stringaddr;
    }
/*
** Translate string to lower case. To avoid any signed/unsigned
** char problems, only characters with an ASCII code in the
** range 0 to 0x7F are changed
*/
    for (n=0; n<string.stringlen; n++) {
      if (CAST(cp[n], byte)<0x80) cp[n] = tolower(cp[n]);
    }
    push_strtemp(string.stringlen, cp);
  }
  DEBUGFUNCMSGOUT;
}

static void fn_sysfn(void) {
  stackitem stringtype;

  DEBUGFUNCMSGIN;
  (*factor_table[*basicvars.current])();
  stringtype = GET_TOPITEM;
  if (stringtype == STACK_STRING || stringtype == STACK_STRTEMP) {
    basicstring descriptor;
    sysparm inregs[MAXSYSPARMS];
    char *tmpstring;
    size_t outregs[MAXSYSPARMS];

    descriptor = pop_string();
    tmpstring = strdup(descriptor.stringaddr);
    if (tmpstring == NULL) {
      DEBUGFUNCMSGOUT;
      error(ERR_BROKEN, __LINE__, "functions");
      return;
    }
    tmpstring[descriptor.stringlen]='\0';
    inregs[1].i = (size_t)tmpstring;
    mos_sys(SWI_OS_SWINumberFromString+XBIT, inregs, outregs, 0);
    push_varyint(outregs[0]);
    free(tmpstring);
    if (stringtype == STACK_STRTEMP) free_string(descriptor);
  }
  else {
   DEBUGFUNCMSGOUT;
   error(ERR_TYPESTR);
   return;
  }
  if (*basicvars.current != ')')  {
    DEBUGFUNCMSGOUT;
    error(ERR_RPMISS);
    return;
  }
  basicvars.current++;  /* Skip the ')' */
  DEBUGFUNCMSGOUT;
}

/*
** The function table maps the function token to the function that deals
** with it
*/
static void (*function_table[])(void) = {
  bad_token,    fn_himem,   fn_ext,      fn_filepath,   /* 00..03 */
  fn_left,      fn_lomem,   fn_mid,      fn_page,       /* 04..07 */
  fn_ptr,       fn_right,   fn_time,     bad_token,     /* 08..0B */
  bad_token,    bad_token,  bad_token,   bad_token,     /* 0C..0F */
  fn_abs,       fn_acs,     fn_adval,    fn_argc,       /* 10..13 */
  fn_argvdol,   fn_asc,     fn_asn,      fn_atn,        /* 14..17 */
  fn_beat,      fn_bget,    fn_chr,      fn_cos,        /* 18..1B */
  fn_count,     fn_deg,     fn_eof,      fn_erl,        /* 1C..1F */
  fn_err,       fn_eval,    fn_exp,      fn_get,        /* 20..23 */
  fn_getdol,    fn_inkey,   fn_inkeydol, fn_instr,      /* 24..27 */
  fn_int,       fn_len,     fn_listofn,  fn_ln,         /* 28..2B */
  fn_log,       fn_openin,  fn_openout,  fn_openup,     /* 2C..2F */
  fn_pi,        fn_pointfn, fn_pos,      fn_rad,        /* 30..33 */
  fn_reportdol, fn_retcode, fn_rnd,      fn_sgn,        /* 34..37 */
  fn_sin,       fn_sqr,     fn_str,      fn_string,     /* 38..3B */
  fn_sum,       fn_tan,     fn_tempofn,  fn_usr,        /* 3C..3F */
  fn_val,       fn_verify,  fn_vpos,     fn_sysfn,      /* 40..43 */
  fn_rndpar,    fn_xlatedol                             /* 44..45 */
};

/*
** 'exec_function' dispatches one of the built-in function routines
*/
void exec_function(void) {
  byte token = *(basicvars.current+1);

  DEBUGFUNCMSGIN;
  basicvars.current+=2;
  if (token>BASTOKEN_XLATEDOL) bad_token();  /* Function token is out of range */
  (*function_table[token])();
  DEBUGFUNCMSGOUT;
}

/*
** 'init_functions' is called before running a program
*/
void init_functions(void) {
  DEBUGFUNCMSGIN;
  srand( (unsigned)time( NULL ) );
  lastrandom=rand();
  randomoverflow = 0;
  DEBUGFUNCMSGOUT;
}
