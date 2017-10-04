/*  -*- Mode: C/l; -*-                                                       */
/*                                                                           */
/*  rpeg.h                                                                   */
/*                                                                           */
/*  © Copyright IBM Corporation 2017.                                        */
/*  LICENSE: MIT License (https://opensource.org/licenses/mit-license.html)  */
/*  AUTHOR: Jamie A. Jennings                                                */


#include "rbuf.h"

typedef uint8_t * byte_ptr;
typedef struct rosie_string {
     uint32_t len;
     byte_ptr ptr;
} rstr;

int r_match_C (lua_State *L);
