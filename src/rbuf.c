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

/* --------------------------------------------------------------------------------------------------- */

/* dynamically allocate storage to replace initb when initb becomes too small */
/* returns pointer to start of new buffer */
static void *resizebuf (lua_State *L, rBuffer *buf, size_t newsize) {
  /* void *temp; */
  /* temp = realloc((void *)buf->data, (newsize * sizeof(char))); */
  /* if (temp == NULL) { */
  /*   free(buf->data); */
  /*   buf->data = NULL; buf->capacity=0; buf->n=0; */
  /*   luaL_error(L, "not enough memory for buffer expansion"); */
  /* } */
  void *ud;
  lua_Alloc allocf = lua_getallocf(L, &ud);
  void *temp = allocf(ud, buf->data, buf->capacity, newsize);
  if (temp == NULL && newsize > 0) {  /* allocation error? */
    allocf(ud, buf->data, buf->capacity, 0);  /* free buffer */
    luaL_error(L, "not enough memory for buffer allocation");
  }

#ifdef ROSIE_DEBUG
  if (buf->data) fprintf(stderr, "*** resized rbuffer %p to new capacity %ld\n", (void *)buf, newsize);
  else fprintf(stderr, "*** allocated rbuffer %p with capacity %ld\n", (void *)buf, newsize);
  if (buf->data != temp) fprintf(stderr, "*** buf->data changed from %p to %p\n", (void *)buf->data, temp);
  fflush(NULL);
#endif
  buf->data = temp;
  buf->capacity = newsize;
  return temp;
}

/* true when buffer's data has overflowed initb and is now allocated elswhere */
#define buffisdynamic(B)	((B)->data != (B)->initb)

/* returns a pointer to a free area with at least 'sz' bytes */
char *r_prepbuffsize (lua_State *L, rBuffer *B, size_t sz) {
  if (B->capacity - B->n < sz) {
    size_t newsize = B->capacity * 2; /* double buffer size */ 

#ifdef ROSIE_DEBUG
    fprintf(stderr, "*** not enough space for rbuffer %p (%ld/%ld %s): open capacity is %ld, looking for %ld\n", 
	    (void *)B, B->n, B->capacity, (buffisdynamic(B) ? "DYNAMIC" : "STATIC"), B->capacity - B->n, sz);
#endif

    if (newsize - B->n < sz) newsize = B->n + sz; /* not big enough? */
    if (newsize < B->n || newsize - B->n < sz) luaL_error(L, "buffer too large");
    /* else create larger buffer */
    if (buffisdynamic(B)) resizebuf(L, B, newsize);
    else {
      /* all data currently still in initb, i.e. no malloc'd storage */
      B->data = NULL; 		/* force an allocation */
      resizebuf(L, B, newsize);
      memcpy(B->data, B->initb, B->n * sizeof(char));  /* copy original content */
    }
  }
  return &B->data[B->n];
}

/* --------------------------------------------------------------------------------------------------- */

static int buffgc (lua_State *L) {
  /* top of stack is 'self' for gc metamethod */
  rBuffer *buf = (rBuffer *)lua_touserdata(L, 1);
  if (buffisdynamic(buf)) { 
#ifdef ROSIE_DEBUG 
  fprintf(stderr, "*** freeing rbuffer->data %p (capacity was %ld)\n", (void *)(buf->data), buf->capacity); 
#endif 
  resizebuf(L, buf, 0);
  /* free((void *)buf->data);	/\* free dynamically allocated data *\/  */
  } 
  return 0;
}

static int buffsize (lua_State *L) {
  rBuffer *buf = (rBuffer *)luaL_checkudata(L, 1, ROSIE_BUFFER);
  lua_pushinteger(L, buf->n);
  return 1;
}

rBuffer *r_newbuffer (lua_State *L) {
  rBuffer *buf = (rBuffer *)lua_newuserdata(L, sizeof(rBuffer));
  buf->data = buf->initb;       /* intially, data storage is statically allocated in initb  */
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

void r_addlstring (lua_State *L, rBuffer *buf, const char *s, size_t l) {
  if (l > 0) {		     /* noop when 's' is an empty string */
    char *b = r_prepbuffsize(L, buf, l * sizeof(char));
    memcpy(b, s, l * sizeof(char));
    addsize(buf, l);
  }
}

void r_addint (lua_State *L, rBuffer *buf, int i) {
  unsigned char str[4];
  unsigned int iun = (int) i;
  str[3] = (iun >> 24) & 0xFF;
  str[2] = (iun >> 16) & 0xFF;
  str[1] = (iun >> 8) & 0xFF;
  str[0] = iun & 0xFF;
  r_addlstring(L, buf, (const char *)str, 4);
}

int r_readint(const char **s) {
  const unsigned char *sun = (const unsigned char *) *s;
  int i = *sun | (*(sun+1)<<8) | (*(sun+2)<<16) | *(sun+3)<<24;
  (*s) += 4;
  return i;
}

int r_peekint(const char **s) {
  const unsigned char *sun = (const unsigned char *) *s;
  return *sun | (*(sun+1)<<8) | (*(sun+2)<<16) | *(sun+3)<<24;
}

void r_addshort (lua_State *L, rBuffer *buf, short i) {
  char str[2];
  short iun = (short) i;
  str[1] = (iun >> 8) & 0xFF;
  str[0] = iun & 0xFF;
  r_addlstring(L, buf, str, 2);
}

int r_readshort(const char **s) {
  const char *sun = *s;
  short i = *sun | (*(sun+1)<<8);
  (*s) += 2;
  return i;
}

int r_lua_newbuffer(lua_State *L) {
  r_newbuffer(L);		/* leaves buffer on stack */
  return 1;
}

int r_lua_getdata (lua_State *L) {
  rBuffer *buf = (rBuffer *)luaL_checkudata(L, 1, ROSIE_BUFFER);
  lua_pushlstring(L, buf->data, buf->n);
  return 1;
}

int r_lua_writedata(lua_State *L) {
    FILE *fp = *(FILE**) luaL_checkudata(L, 1, LUA_FILEHANDLE);
    rBuffer *buf = (rBuffer *)luaL_checkudata(L, 2, ROSIE_BUFFER);
    size_t items = fwrite((void *) buf->data, buf->n, 1, fp);
    if (!items) luaL_error(L, "writedata encountered a write error");
    return 0;
}

int r_lua_add (lua_State *L) {
  size_t len;
  const char *s;
  rBuffer *buf = (rBuffer *)luaL_checkudata(L, 1, ROSIE_BUFFER);
  s = lua_tolstring(L, 2, &len);
  r_addlstring(L, buf, s, len);
  return 0;
}
