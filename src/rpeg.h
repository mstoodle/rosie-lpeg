/*  -*- Mode: C/l; -*-                                                       */
/*                                                                           */
/*  rpeg.h                                                                   */
/*                                                                           */
/*  © Copyright IBM Corporation 2017.                                        */
/*  LICENSE: MIT License (https://opensource.org/licenses/mit-license.html)  */
/*  AUTHOR: Jamie A. Jennings                                                */


#if !defined(rpeg_h)
#define rpeg_h

#include "lauxlib.h"
#include "lualib.h"
#include "rbuf.h"

typedef uint8_t * byte_ptr;
typedef struct rosie_string {
     uint32_t len;
     byte_ptr ptr;
} rstr;

typedef struct r_encoder_type {
  const char *name;
  int code;
} r_encoder_t;

#define ENCODE_DEBUG -1
#define ENCODE_JSON 1
#define ENCODE_LINE 2
#define ENCODE_BYTE 3

__attribute__((unused))
static r_encoder_t r_encoders[] = { 
     {"json",   ENCODE_JSON},
     {"line",   ENCODE_LINE},
     {"byte",   ENCODE_BYTE},
     {"debug",  ENCODE_DEBUG},
     {NULL, 0}
};

int r_match_C (lua_State *L);

#endif
