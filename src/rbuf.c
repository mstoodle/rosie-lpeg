/*  -*- Mode: C; -*-                                                         */
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
  } 
  return 0;
}

static int buffsize (lua_State *L) {
  rBuffer *buf = (rBuffer *)luaL_checkudata(L, 1, ROSIE_BUFFER);
  lua_pushinteger(L, buf->n);
  return 1;
}

int r_lua_buffreset (lua_State *L, int pos) {
  rBuffer *buf = (rBuffer *)luaL_checkudata(L, pos, ROSIE_BUFFER);
  buf->n = 0;
  return 0;
}

static int r_buffsub (lua_State *L) {
  rBuffer *buf;
  int j = 1;
  int k = luaL_checkinteger(L, -1);
  int two_indices = lua_isinteger(L, -2);
  if (two_indices) {
    j = lua_tointeger(L, -2);
    buf = (rBuffer *)luaL_checkudata(L, -3, ROSIE_BUFFER);
    lua_pop(L, 3);
  }
  else {
    j = k;
    buf = (rBuffer *)luaL_checkudata(L, -2, ROSIE_BUFFER);
    k = buf->n;
    lua_pop(L, 2);
  }
  /* These are the rules of string.sub according to the Lua 5.3 reference */
  if (j < 0) j = buf->n + j + 1;
  if (j < 1) j = 1;
  if (k < 0) k = buf->n + k + 1;
  if (k > (int) buf->n) k = buf->n;
  if ((j > k) || (j > (int) buf->n)) {
    lua_pushliteral(L, "");
  }
  else {
    lua_pushlstring(L, (buf->data + j - 1), (size_t) k - j + 1);
  }
  return 1;
}

int r_lua_getdata (lua_State *L);

static struct luaL_Reg rbuf_meta_reg[] = {
    {"__gc", buffgc},
    {"__len", buffsize},
    {"__tostring", r_lua_getdata},
    {NULL, NULL}
};

static struct luaL_Reg rbuf_index_reg[] = {
    {"sub", r_buffsub},
    {NULL, NULL}
};

static void rbuf_type_init(lua_State *L) {
  /* Enter with a new metatable on the stack */
  int top = lua_gettop(L);
  luaL_setfuncs(L, rbuf_meta_reg, 0);
  luaL_newlib(L, rbuf_index_reg);
  lua_pushvalue(L, -1);
  lua_setfield(L, -3, "__index");
  lua_settop(L, top);
  /* Must leave the metatable on the stack */
}

rBuffer *r_newbuffer (lua_State *L) {
  rBuffer *buf = (rBuffer *)lua_newuserdata(L, sizeof(rBuffer));
  buf->initb = buf->initialbuff; /* the extra pointer to initialbuff enables r_newbuffer_wrap */
  buf->data = buf->initb;        /* intially, data storage is statically allocated in initb  */
  buf->n = 0;			 /* contents length is 0 */
  buf->capacity = R_BUFFERSIZE;	 /* size of initb */
  if (luaL_newmetatable(L, ROSIE_BUFFER)) rbuf_type_init(L);
  lua_setmetatable(L, -2);	 /* pops the metatable, leaving the userdata at the top */
  return buf;
}

rBuffer *r_newbuffer_wrap (lua_State *L, char *data, size_t len) {
  rBufferLite *buflite = (rBufferLite *)lua_newuserdata(L, sizeof(rBufferLite));
  rBuffer *buf = (rBuffer *)buflite;
  buf->initb = data;
  buf->data = data;
  buf->n = len;
  buf->capacity = len;
  if (luaL_newmetatable(L, ROSIE_BUFFER)) rbuf_type_init(L);
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
    size_t items;
    if (buf->n==0) return 0;
    items = fwrite((void *) buf->data, buf->n, 1, fp);
    if (!items) return luaL_error(L, "writedata: write error (buffer %p, size %d)", buf->data, buf->n);
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
