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

/* buffer for arbitrary char data, grows and shrinks */
typedef struct rBuffer {
  char *data;
  size_t capacity;
  size_t n;			/* number of bytes in use */
  char initb[R_BUFFERSIZE];	/* initial buffer */
} rBuffer;

int r_lua_newbuffer (lua_State *L);
int r_lua_getdata (lua_State *L);
int r_lua_add (lua_State *L);
int r_lua_writedata(lua_State *L);

rBuffer *r_newbuffer (lua_State *L);

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

#endif
