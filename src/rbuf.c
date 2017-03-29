/*  -*- Mode: C/l; -*-                                                       */
/*                                                                           */
/*  rbuf.c   Custom version of luaL_Buffer                                   */
/*                                                                           */
/*  © Copyright IBM Corporation 2017.                                        */
/*  LICENSE: MIT License (https://opensource.org/licenses/mit-license.html)  */
/*  AUTHOR: Jamie A. Jennings                                                */

#include <string.h>
#include <stdlib.h>

#include "lua.h"
#include "lauxlib.h"
#include "rbuf.h"

#define addsize(B,s)	((B)->n += (s))

/* --------------------------------------------------------------------------------------------------- */

/* dynamically allocate storage to replace initb when initb becomes too small */
/* returns pointer to start of new buffer */
static void *resizebuf (lua_State *L, r_Buffer *buf, size_t newsize) {
  void *ud;
  lua_Alloc allocf = lua_getallocf(L, &ud);
  void *temp = allocf(ud, buf->data, buf->capacity, newsize);
  if (temp == NULL && newsize > 0) {  /* allocation error? */
    resizebuf(L, buf, 0);  /* free buffer */
    luaL_error(L, "not enough memory for dynamic buffer expansion");
  }
  buf->data = temp;
  buf->capacity = newsize;
#ifdef ROSIE_DEBUG
  printf("*** resized to new capacity: %ld\n", newsize);
#endif
  return temp;
}

/* true when buffer's data has overflowed initb and is now allocated elswhere */
#define buffisdynamic(B)	((B)->data != (B)->initb)

/* returns a pointer to a free area with at least 'sz' bytes */
char *r_prepbuffsize (lua_State *L, r_Buffer *B, size_t sz) {
  char *newbuff;
  if (B->capacity - B->n < sz) {  /* not enough space? */
    size_t newsize = B->capacity * 2; /* double buffer size */
#ifdef ROSIE_DEBUG
    printf("*** not enough dynamic space: open capacity is %ld, looking for %ld\n", 
	   B->capacity - B->n, sz);
#endif
    if (newsize - B->n < sz) newsize = B->n + sz; /* not big enough? */
    if (newsize < B->n || newsize - B->n < sz) luaL_error(L, "buffer too large");
    /* else create larger buffer */
    if (B->data != B->initb) newbuff = (char *)resizebuf(L, B, newsize);
    else {  /* all data currently still in initb, i.e. no malloc'd storage */
      newbuff = (char *)malloc(newsize * sizeof(char));
      if (newbuff == NULL) luaL_error(L, "not enough memory to expand static buffer");
      memcpy(newbuff, B->data, B->n * sizeof(char));  /* copy original content */
    }
    B->data = newbuff;
    B->capacity = newsize;
  }
  return &B->data[B->n];
}

/* --------------------------------------------------------------------------------------------------- */

static int buffgc (lua_State *L) {
  /* top of stack is 'self' for gc metamethod */
  r_Buffer *buf = (r_Buffer *)luaL_checkudata(L, 1, ROSIE_BUFFER);
  if (buffisdynamic(buf)) resizebuf(L, buf, 0);
  return 0;
}

static int buffsize (lua_State *L) {
  r_Buffer *buf = (r_Buffer *)luaL_checkudata(L, 1, ROSIE_BUFFER);
  lua_pushinteger(L, buf->n);
  return 1;
}

r_Buffer *r_newbuffer (lua_State *L) {
  r_Buffer *buf = (r_Buffer *)lua_newuserdata(L, sizeof(r_Buffer));
  buf->data = buf->initb;		/* intially, data storage is statically allocated in initb  */
  buf->n = 0;			/* contents length is 0 */
  buf->capacity = R_BUFFERSIZE;	/* size of initb */
  if (luaL_newmetatable(L, ROSIE_BUFFER)) {
    /* first time:  
         enters ROSIE_BUFFER into registry;  
         creates new metatable for ROSIE_BUFFER objects (on stack); 
       otherwise: 
         puts the ROSIE_BUFFER entry from the registry on the stack; 
    */ 
    lua_pushcfunction(L, buffgc); 
    lua_setfield(L, -2, "__gc"); 
    lua_pushcfunction(L, buffsize); 
    lua_setfield(L, -2, "__len"); 
  }
  /* set the new userdata's metatable to the one for ROSIE_BUFFER objects  */
  lua_setmetatable(L, -2);	/* pops the metatable, leaving the userdata at the top */
  return buf;
}

void r_addlstring (lua_State *L, r_Buffer *buf, const char *s, size_t l) {
  if (l > 0) {		     /* avoid 'memcpy' when 's' can be NULL */
    char *b = r_prepbuffsize(L, buf, l);
    memcpy(b, s, l * sizeof(char));
    addsize(buf, l);
  }
}

void r_addstring (lua_State *L, r_Buffer *buf, const char *s) {
  r_addlstring(L, buf, s, strlen(s));
}

int r_lua_newbuffer(lua_State *L) {
  r_newbuffer(L);		/* leaves buffer on stack */
  return 1;
}

int r_lua_getdata (lua_State *L) {
  r_Buffer *buf = (r_Buffer *)luaL_checkudata(L, 1, ROSIE_BUFFER);
  lua_pushlstring(L, buf->data, buf->n);
  return 1;
}

int r_lua_add (lua_State *L) {
  size_t len;
  const char *s;
  r_Buffer *buf = (r_Buffer *)luaL_checkudata(L, 1, ROSIE_BUFFER);
  s = lua_tolstring(L, 2, &len);
  r_addlstring(L, buf, s, len);
  lua_pushvalue(L, 1);		/* return the buffer itself */
  return 1;
}
