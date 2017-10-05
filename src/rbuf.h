/*  -*- Mode: C/l; -*-                                                       */
/*                                                                           */
/*  rbuf.h                                                                   */
/*                                                                           */
/*  © Copyright IBM Corporation 2017.                                        */
/*  LICENSE: MIT License (https://opensource.org/licenses/mit-license.html)  */
/*  AUTHOR: Jamie A. Jennings                                                */

#if !defined(rbuf_h)
#define rbuf_h

#define ROSIE_BUFFER "ROSIE_BUFFER"
#define R_BUFFERSIZE (8192 * sizeof(char))	  /* should experiment with different values */

/*
 * When the initial (statically allocated) buffer overflows, a new
 * "box" userdata is created and the contents of initb are copied
 * there.
 */

/* Buffer for arbitrary char data, can grow and shrink by calling
 * resize().  Will be garbage collected by Lua.  Has an initial
 * storage area pre-allocated (initb).  */
typedef struct rBuffer {
  char *data;
  size_t capacity;
  size_t n;			/* number of bytes in use */
  char *initb;
  char initialbuff[R_BUFFERSIZE];
} rBuffer;

typedef struct rBufferLite {
  char *data;
  size_t capacity;
  size_t n;			/* number of bytes in use */
  char *initb;	                /* no initial buffer */
} rBufferLite;

int r_lua_newbuffer (lua_State *L);
int r_lua_buffreset (lua_State *L, int pos);
int r_lua_getdata (lua_State *L);
int r_lua_add (lua_State *L);
int r_lua_writedata(lua_State *L);

rBuffer *r_newbuffer (lua_State *L);
rBuffer *r_newbuffer_wrap (lua_State *L, char *data, size_t len);

/* the functions below DO NOT use the stack */
char *r_prepbuffsize (lua_State *L, rBuffer *buf, size_t sz);
void r_addlstring (lua_State *L, rBuffer *buf, const char *s, size_t l);
void r_addint (lua_State *L, rBuffer *buf, int i);
int r_readint(const char **s);
int r_peekint(const char **s);
void r_addshort (lua_State *L, rBuffer *buf, short i);
int r_readshort(const char **s);
     
#define r_addstring(L, buf, s) (r_addlstring)((L), (buf), (s), strlen(s))
#define r_addchar(L, buf, c) (r_addlstring)((L), (buf), &(c), sizeof(char))

#define addsize(B,s)	((B)->n += (s))
#define r_addlstring_UNSAFE(L, buf, s, l) { memcpy(&((buf)->data[(buf)->n]), (s), (l) * sizeof(char)); addsize((buf), (l)); }
#define r_addchar_UNSAFE(L, buf, c) r_addlstring_UNSAFE((L), (buf), &(c), sizeof(char))

#endif
