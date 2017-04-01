/*
** $Id: lpcap.c,v 1.6 2015/06/15 16:09:57 roberto Exp $
** Copyright 2007, Lua.org & PUC-Rio  (see 'lpeg.html' for license)
*/

#include "lua.h"
#include "lauxlib.h"

#include "lpcap.h"
#include "lptypes.h"
#include <string.h>

#include "rbuf.h"

#define captype(cap)	((cap)->kind)

#define isclosecap(cap)	(captype(cap) == Cclose)

#define closeaddr(c)	((c)->s + (c)->siz - 1)

#define isfullcap(cap)	((cap)->siz != 0)

#define getfromktable(cs,v)	lua_rawgeti((cs)->L, ktableidx((cs)->ptop), v)

#define pushluaval(cs)		getfromktable(cs, (cs)->cap->idx)


/* arg: name <string>
   arg: pos <int>
   arg: capture <string>
   args: subs <tables>
 */
/* int r_create_match(lua_State *L) { */
/*   int i, nargs; */
/*   lua_Integer pos; */
/*   size_t name_l, captext_l; */
/*   const char *name, *captext; */
/*   nargs = lua_gettop(L); */
/*   name = luaL_checklstring(L, 1, &name_l); */
/*   pos = luaL_checkinteger(L, 2); */
/*   captext = luaL_checklstring(L, 3, &captext_l); */
/*   /\* process submatches *\/ */
/*   nargs = nargs-3; */

/*   if (nargs > 0) { */
/*        lua_createtable(L, nargs, 0); /\* create subs table *\/ */
/*        lua_insert(L, 1);	     /\* move subs table to bottom *\/ */
/*        /\* fill the subs table (lua_rawseti pops the value as well) *\/ */
/*        for (i=nargs; i>=1; i--) lua_rawseti(L, 1, (lua_Integer) i);  */
/*        /\* subs table now at top (below are captext, pos, name) *\/ */
/*   } */

/*   lua_createtable(L, 0, 1);	    /\* create match table *\/ */
/*   lua_pushlstring(L, name, name_l); /\* push match name *\/ */

/*   lua_createtable(L, 0, 3);	    /\* create match body table *\/ */
/*   lua_pushliteral(L, "pos"); */
/*   lua_pushinteger(L, pos); */
/*   lua_rawset(L, -3);		    /\* body["pos"] = pos *\/ */
/*   lua_pushliteral(L, "text"); */
/*   lua_pushlstring(L, captext, captext_l); */
/*   lua_rawset(L, -3);		    /\* body["text"] = captext *\/ */

/*   /\* stack top is body table (next: name, match table, maybe subs table, captext, pos, name) *\/ */
/*   if (nargs > 0) { */
/*        lua_pushliteral(L, "subs"); */
/*        lua_pushvalue(L, 1);	/\* push copy of subs table *\/ */
/*        lua_rawset(L, -3);	/\* body["subs"] = subs table *\/ */
/*   } */
  
/*   /\* stack top is body table. (next: name, match table, maybe subs table, captext, pos, name) *\/ */
/*   lua_rawset(L, -3);		    /\* match[name] = body *\/ */

/*   /\* all items below the return values will be discarded automatically *\/ */
/*   return 1; */
/* } */


/*
** Put at the cache for Lua values the value indexed by 'v' in ktable
** of the running pattern (if it is not there yet); returns its index.
*/
static int updatecache (CapState *cs, int v) {
  int idx = cs->ptop + 1;  /* stack index of cache for Lua values */
  if (v != cs->valuecached) {  /* not there? */
    getfromktable(cs, v);  /* get value from 'ktable' */
    lua_replace(cs->L, idx);  /* put it at reserved stack position */
    cs->valuecached = v;  /* keep track of what is there */
  }
  return idx;
}


static int pushcapture (CapState *cs);


/*
** Goes back in a list of captures looking for an open capture
** corresponding to a close
*/
static Capture *findopen (Capture *cap) {
  int n = 0;  /* number of closes waiting an open */
  for (;;) {
    cap--;
    if (isclosecap(cap)) n++;  /* one more open to skip */
    else if (!isfullcap(cap))
      if (n-- == 0) return cap;
  }
}


/*
** Go to the next capture
*/
static void nextcap (CapState *cs) {
  Capture *cap = cs->cap;
  if (!isfullcap(cap)) {  /* not a single capture? */
    int n = 0;  /* number of opens waiting a close */
    for (;;) {  /* look for corresponding close */
      cap++;
      if (isclosecap(cap)) {
        if (n-- == 0) break;
      }
      else if (!isfullcap(cap)) n++;
    }
  }
  cs->cap = cap + 1;  /* + 1 to skip last close (or entire single capture) */
}


/*
** Push on the Lua stack all values generated by nested captures inside
** the current capture. Returns number of values pushed. 'addextra'
** makes it push the entire match after all captured values. The
** entire match is pushed also if there are no other nested values,
** so the function never returns zero.
*/
static int pushnestedvalues (CapState *cs, int addextra) {
  Capture *co = cs->cap;
  if (isfullcap(cs->cap++)) {  /* no nested captures? */
    lua_pushlstring(cs->L, co->s, co->siz - 1);  /* push whole match */
    return 1;  /* that is it */
  }
  else {
    int n = 0;
    while (!isclosecap(cs->cap))  /* repeat for all nested patterns */
      n += pushcapture(cs);
    if (addextra || n == 0) {  /* need extra? */
      lua_pushlstring(cs->L, co->s, cs->cap->s - co->s);  /* push whole match */
      n++;
    }
    cs->cap++;  /* skip close entry */
    return n;
  }
}

/*
** Push only the first value generated by nested captures
*/
static void pushonenestedvalue (CapState *cs) {
  int n = pushnestedvalues(cs, 0);
  if (n > 1)
    lua_pop(cs->L, n - 1);  /* pop extra values */
}


/*
** Try to find a named group capture with the name given at the top of
** the stack; goes backward from 'cap'.
*/
static Capture *findback (CapState *cs, Capture *cap) {
  lua_State *L = cs->L;
  while (cap-- > cs->ocap) {  /* repeat until end of list */
    if (isclosecap(cap))
      cap = findopen(cap);  /* skip nested captures */
    else if (!isfullcap(cap))
      continue; /* opening an enclosing capture: skip and get previous */
    if (captype(cap) == Cgroup) {
      getfromktable(cs, cap->idx);  /* get group name */
      if (lp_equal(L, -2, -1)) {  /* right group? */
        lua_pop(L, 2);  /* remove reference name and group name */
        return cap;
      }
      else lua_pop(L, 1);  /* remove group name */
    }
  }
  luaL_error(L, "back reference '%s' not found", lua_tostring(L, -1));
  return NULL;  /* to avoid warnings */
}


/*
** Back-reference capture. Return number of values pushed.
*/
static int backrefcap (CapState *cs) {
  int n;
  Capture *curr = cs->cap;
  pushluaval(cs);  /* reference name */
  cs->cap = findback(cs, curr);  /* find corresponding group */
  n = pushnestedvalues(cs, 0);  /* push group's values */
  cs->cap = curr + 1;
  return n;
}


/*
** Table capture: creates a new table and populates it with nested
** captures.
*/
static int tablecap (CapState *cs) {
  lua_State *L = cs->L;
  int n = 0;
  lua_newtable(L);
  if (isfullcap(cs->cap++))
    return 1;  /* table is empty */
  while (!isclosecap(cs->cap)) {
    if (captype(cs->cap) == Cgroup && cs->cap->idx != 0) {  /* named group? */
      pushluaval(cs);  /* push group name */
      pushonenestedvalue(cs);
      lua_settable(L, -3);
    }
    else {  /* not a named group */
      int i;
      int k = pushcapture(cs);
      for (i = k; i > 0; i--)  /* store all values into table */
        lua_rawseti(L, -(i + 1), n + i);
      n += k;
    }
  }
  cs->cap++;  /* skip close entry */
  return 1;  /* number of values pushed (only the table) */
}


/*
** Table-query capture
*/
static int querycap (CapState *cs) {
  int idx = cs->cap->idx;
  pushonenestedvalue(cs);  /* get nested capture */
  lua_gettable(cs->L, updatecache(cs, idx));  /* query cap. value at table */
  if (!lua_isnil(cs->L, -1))
    return 1;
  else {  /* no value */
    lua_pop(cs->L, 1);  /* remove nil */
    return 0;
  }
}


/*
** Fold capture
*/
static int foldcap (CapState *cs) {
  int n;
  lua_State *L = cs->L;
  int idx = cs->cap->idx;
  if (isfullcap(cs->cap++) ||  /* no nested captures? */
      isclosecap(cs->cap) ||  /* no nested captures (large subject)? */
      (n = pushcapture(cs)) == 0)  /* nested captures with no values? */
    return luaL_error(L, "no initial value for fold capture");
  if (n > 1)
    lua_pop(L, n - 1);  /* leave only one result for accumulator */
  while (!isclosecap(cs->cap)) {
    lua_pushvalue(L, updatecache(cs, idx));  /* get folding function */
    lua_insert(L, -2);  /* put it before accumulator */
    n = pushcapture(cs);  /* get next capture's values */
    lua_call(L, n + 1, 1);  /* call folding function */
  }
  cs->cap++;  /* skip close entry */
  return 1;  /* only accumulator left on the stack */
}


/*
** Function capture
*/
static int functioncap (CapState *cs) {
  int n;
  int top = lua_gettop(cs->L);
  pushluaval(cs);  /* push function */
  n = pushnestedvalues(cs, 0);  /* push nested captures */
  lua_call(cs->L, n, LUA_MULTRET);  /* call function */
  return lua_gettop(cs->L) - top;  /* return function's results */
}


/*
** Select capture
*/
static int numcap (CapState *cs) {
  int idx = cs->cap->idx;  /* value to select */
  if (idx == 0) {  /* no values? */
    nextcap(cs);  /* skip entire capture */
    return 0;  /* no value produced */
  }
  else {
    int n = pushnestedvalues(cs, 0);
    if (n < idx)  /* invalid index? */
      return luaL_error(cs->L, "no capture '%d'", idx);
    else {
      lua_pushvalue(cs->L, -(n - idx + 1));  /* get selected capture */
      lua_replace(cs->L, -(n + 1));  /* put it in place of 1st capture */
      lua_pop(cs->L, n - 1);  /* remove other captures */
      return 1;
    }
  }
}


/*
** Return the stack index of the first runtime capture in the given
** list of captures (or zero if no runtime captures)
*/
int finddyncap (Capture *cap, Capture *last) {
  for (; cap < last; cap++) {
    if (cap->kind == Cruntime)
      return cap->idx;  /* stack position of first capture */
  }
  return 0;  /* no dynamic captures in this segment */
}


/*
** Calls a runtime capture. Returns number of captures removed by
** the call, including the initial Cgroup. (Captures to be added are
** on the Lua stack.)
*/
int runtimecap (CapState *cs, Capture *close, const char *s, int *rem) {
  int n, id;
  lua_State *L = cs->L;
  int otop = lua_gettop(L);
  Capture *open = findopen(close);
  assert(captype(open) == Cgroup);
  id = finddyncap(open, close);  /* get first dynamic capture argument */
  close->kind = Cclose;  /* closes the group */
  close->s = s;
  cs->cap = open; cs->valuecached = 0;  /* prepare capture state */
  luaL_checkstack(L, 4, "too many runtime captures");
  pushluaval(cs);  /* push function to be called */
  lua_pushvalue(L, SUBJIDX);  /* push original subject */
  lua_pushinteger(L, s - cs->s + 1);  /* push current position */
  n = pushnestedvalues(cs, 0);  /* push nested captures */
  lua_call(L, n + 2, LUA_MULTRET);  /* call dynamic function */
  if (id > 0) {  /* are there old dynamic captures to be removed? */
    int i;
    for (i = id; i <= otop; i++)
      lua_remove(L, id);  /* remove old dynamic captures */
    *rem = otop - id + 1;  /* total number of dynamic captures removed */
  }
  else
    *rem = 0;  /* no dynamic captures removed */
  return close - open;  /* number of captures of all kinds removed */
}


/*
** Auxiliary structure for substitution and string captures: keep
** information about nested captures for future use, avoiding to push
** string results into Lua
*/
typedef struct StrAux {
  int isstring;  /* whether capture is a string */
  union {
    Capture *cp;  /* if not a string, respective capture */
    struct {  /* if it is a string... */
      const char *s;  /* ... starts here */
      const char *e;  /* ... ends here */
    } s;
  } u;
} StrAux;

#define MAXSTRCAPS	10

/*
Collect values from current capture into array 'cps'. Current
capture must be Cstring (first call) or Csimple (recursive calls).
(In first call, fills %0 with whole match for Cstring.)
Returns number of elements in the array that were filled.
*/
static int getstrcaps (CapState *cs, StrAux *cps, int n) { 
  int k = n++; 
  cps[k].isstring = 1;  /* get string value */ 
  cps[k].u.s.s = cs->cap->s;  /* starts here */ 
  if (!isfullcap(cs->cap++)) {  /* nested captures? */ 
    while (!isclosecap(cs->cap)) {  /* traverse them */ 
      if (n >= MAXSTRCAPS)  /* too many captures? */ 
        nextcap(cs);  /* skip extra captures (will not need them) */ 
      else if (captype(cs->cap) == Csimple)  /* string? */ 
        n = getstrcaps(cs, cps, n);  /* put info. into array */ 
      else { 
        cps[n].isstring = 0;  /* not a string */ 
        cps[n].u.cp = cs->cap;  /* keep original capture */ 
        nextcap(cs); 
        n++; 
      } 
    } 
    cs->cap++;  /* skip close */ 
  } 
  cps[k].u.s.e = closeaddr(cs->cap - 1);  /* ends here */ 
  return n; 
} 


/*
add next capture value (which should be a string) to buffer 'b'
*/
static int addonestring (luaL_Buffer *b, CapState *cs, const char *what); 


/*
 String capture: add result to buffer 'b' (instead of pushing
 it into the stack)
*/
static void stringcap (luaL_Buffer *b, CapState *cs) { 
  StrAux cps[MAXSTRCAPS]; 
  int n; 
  size_t len, i; 
  const char *fmt;  /* format string */ 
  fmt = lua_tolstring(cs->L, updatecache(cs, cs->cap->idx), &len); 
  n = getstrcaps(cs, cps, 0) - 1;  /* collect nested captures */ 
  for (i = 0; i < len; i++) {  /* traverse them */ 
    if (fmt[i] != '%')  /* not an escape? */ 
      luaL_addchar(b, fmt[i]);  /* add it to buffer */ 
    else if (fmt[++i] < '0' || fmt[i] > '9')  /* not followed by a digit? */ 
      luaL_addchar(b, fmt[i]);  /* add to buffer */ 
    else { 
      int l = fmt[i] - '0';  /* capture index */ 
      if (l > n) 
        luaL_error(cs->L, "invalid capture index (%d)", l); 
      else if (cps[l].isstring) 
        luaL_addlstring(b, cps[l].u.s.s, cps[l].u.s.e - cps[l].u.s.s); 
      else { 
        Capture *curr = cs->cap; 
        cs->cap = cps[l].u.cp;  /* go back to evaluate that nested capture */ 
        if (!addonestring(b, cs, "capture")) 
          luaL_error(cs->L, "no values in capture index %d", l); 
        cs->cap = curr;  /* continue from where it stopped */ 
      } 
    } 
  } 
} 


/*
Substitution capture: add result to buffer 'b'
*/
static void substcap (luaL_Buffer *b, CapState *cs) { 
  const char *curr = cs->cap->s; 
  if (isfullcap(cs->cap))  /* no nested captures? */ 
    luaL_addlstring(b, curr, cs->cap->siz - 1);  /* keep original text */ 
  else { 
    cs->cap++;  /* skip open entry */ 
    while (!isclosecap(cs->cap)) {  /* traverse nested captures */ 
      const char *next = cs->cap->s; 
      luaL_addlstring(b, curr, next - curr);  /* add text up to capture */ 
      if (addonestring(b, cs, "replacement")) 
        curr = closeaddr(cs->cap - 1);  /* continue after match */ 
      else  /* no capture value */ 
        curr = next;  /* keep original text in final result */ 
    } 
    luaL_addlstring(b, curr, cs->cap->s - curr);  /* add last piece of text */ 
  } 
  cs->cap++;  /* go to next capture */ 
} 


/*
Evaluates a capture and adds its first value to buffer 'b'; returns
whether there was a value
*/
static int addonestring (luaL_Buffer *b, CapState *cs, const char *what) { 
  switch (captype(cs->cap)) { 
    case Cstring: 
      stringcap(b, cs);  /* add capture directly to buffer */ 
      return 1; 
    case Csubst: 
      substcap(b, cs);  /* add capture directly to buffer */ 
      return 1; 
    default: { 
      lua_State *L = cs->L; 
      int n = pushcapture(cs); 
      if (n > 0) { 
        if (n > 1) lua_pop(L, n - 1);  /* only one result */ 
        if (!lua_isstring(L, -1)) 
          luaL_error(L, "invalid %s value (a %s)", what, luaL_typename(L, -1)); 
        luaL_addvalue(b); 
      } 
      return n; 
    } 
  } 
} 


/*
** Push all values of the current capture into the stack; returns
** number of values pushed
*/
static int pushcapture (CapState *cs) {
  lua_State *L = cs->L;
  luaL_checkstack(L, 4, "too many captures");
  switch (captype(cs->cap)) {
    case Cposition: {
      lua_pushinteger(L, cs->cap->s - cs->s + 1);
      cs->cap++;
      return 1;
    }
    case Cconst: {
      pushluaval(cs);
      cs->cap++;
      return 1;
    }
    case Carg: {
      int arg = (cs->cap++)->idx;
      if (arg + FIXEDARGS > cs->ptop)
        return luaL_error(L, "reference to absent extra argument #%d", arg);
      lua_pushvalue(L, arg + FIXEDARGS);
      return 1;
    }
    case Csimple: {
      int k = pushnestedvalues(cs, 1);
      lua_insert(L, -k);  /* make whole match be first result */
      return k;
    }
    case Cruntime: {
      lua_pushvalue(L, (cs->cap++)->idx);  /* value is in the stack */
      return 1;
    }
    case Cstring: { 
      luaL_Buffer b; 
      luaL_buffinit(L, &b); 
      stringcap(&b, cs); 
      luaL_pushresult(&b); 
      return 1; 
    } 
    case Csubst: { 
      luaL_Buffer b; 
      luaL_buffinit(L, &b); 
      substcap(&b, cs); 
      luaL_pushresult(&b); 
      return 1; 
    } 
    case Cgroup: {
      if (cs->cap->idx == 0)  /* anonymous group? */
        return pushnestedvalues(cs, 0);  /* add all nested values */
      else {  /* named group: add no values */
        nextcap(cs);  /* skip capture */
        return 0;
      }
    }
    case Cbackref: return backrefcap(cs);
    case Ctable: return tablecap(cs);
    case Cfunction: return functioncap(cs);
    case Cnum: return numcap(cs);
    case Cquery: return querycap(cs);
    case Cfold: return foldcap(cs);
    default: assert(0); return -1; /* catch accidental use of 'match' with Crosiecap */
  }
}


/*
** Prepare a CapState structure and traverse the entire list of
** captures in the stack pushing its results. 's' is the subject
** string, 'r' is the final position of the match, and 'ptop' 
** the index in the stack where some useful values were pushed.
** Returns the number of results pushed. (If the list produces no
** results, push the final position of the match.)
*/
int getcaptures (lua_State *L, const char *s, const char *r, int ptop) {
  int i;
  int n = 0;
  Capture *capture = (Capture *)lua_touserdata(L, caplistidx(ptop));
  if (!isclosecap(capture)) {  /* is there any capture? */
    CapState cs;
    cs.ocap = cs.cap = capture; cs.L = L;
    cs.s = s; cs.valuecached = 0; cs.ptop = ptop;
    do {  /* collect their values */
      i = pushcapture(&cs);
      if (i<0) { lua_pushnil(L); lua_pushstring(L, "invalid capture type"); return 2; }
      n += i;
    } while (!isclosecap(cs.cap));
  }
  if (n == 0) {  /* no capture values? */
    lua_pushinteger(L, r - s + 1);  /* return only end position */
    n = 1;
  }
  return n;
}

/* Rosie extensions */
typedef struct {  
  int (*Open)(CapState *cs, rBuffer *buf, int count);
  int (*Fullcapture)(CapState *cs, rBuffer *buf, int count);
  int (*Close)(CapState *cs, rBuffer *buf, int count);
} encoder_functions;
 
static void print_capture(CapState *cs) {
  Capture *c = cs->cap;
  printf("  isfullcap? %s\n", isfullcap(c) ? "true" : "false");
  printf("  kind = %u\n", c->kind);
  printf("  pos (1-based) = %lu\n", c->s ? (c->s - cs->s + 1) : 0);
  printf("  size (actual) = %u\n", c->siz ? c->siz-1 : 0);
  printf("  idx = %u\n", c->idx);
  lua_rawgeti(cs->L, ktableidx(cs->ptop), c->idx);
  printf("  ktable[idx] = %s\n", lua_tostring(cs->L, -1));
  lua_pop(cs->L, 1);
}

static void print_capture_text(const char *s, const char *e) {
  printf("  text of match: |");
  for (; s < e; s++) printf("%c", *s);
  printf("|\n");
}

static int debug_Fullcapture(CapState *cs, rBuffer *buf, int count) {
  Capture *c = cs->cap;
  const char *start = c->s;
  const char *last = c->s + c->siz - 1;
  printf("Full capture:\n");
  print_capture(cs);
  if (cs->cap->siz == 0) return ROSIE_SIZ_ERROR;
  if (c->kind == Cclose) return ROSIE_FULLCAP_ERROR;
  print_capture_text(start, last);
  return ROSIE_OK;
}

static int debug_Close(CapState *cs, rBuffer *buf, int count) {
  if (!cs->cap->kind==Cclose) return ROSIE_CLOSE_ERROR;
  printf("CLOSE:\n");
  print_capture(cs);
  return ROSIE_OK;
}

static int debug_Open(CapState *cs, rBuffer *buf, int count) {
  if ((cs->cap->kind == Cclose) || (cs->cap->siz != 0)) return ROSIE_OPEN_ERROR;
  printf("OPEN:\n");
  print_capture(cs);
  return ROSIE_OK;
}

static void encode_pos(lua_State *L, size_t pos, int negate, rBuffer *buf) {
  int intpos = (int) pos;
  if (negate) intpos = - intpos;
  r_addlstring(L, buf, (const char *)&intpos, sizeof(int));
}

static void encode_string(lua_State *L, const char *str, size_t len, rBuffer *buf) {
  int intlen = (int) len; 
  r_addlstring(L, buf, (const char *)&intlen, sizeof(int)); 
  r_addlstring(L, buf, str, len); 
}

static void encode_name(CapState *cs, rBuffer *buf) {
  const char *name;
  size_t len;
  lua_rawgeti(cs->L, ktableidx(cs->ptop), cs->cap->idx); 
  name = lua_tolstring(cs->L, -1, &len); 
  encode_string(cs->L, name, len, buf);
  lua_pop(cs->L, 1); 
}

static int byte_Fullcapture(CapState *cs, rBuffer *buf, int count) {
  Capture *c = cs->cap;
  size_t s, e;
  if (!isfullcap(c)) return ROSIE_SIZ_ERROR;
  if (c->kind != Crosiecap) return ROSIE_FULLCAP_ERROR;
  s = c->s - cs->s + 1;		/* 1-based start position */
  e = s + c->siz - 1;
  encode_pos(cs->L, s, 1, buf);
  encode_name(cs, buf);
  encode_pos(cs->L, e, 0, buf);
  return ROSIE_OK;
}

static int byte_Close(CapState *cs, rBuffer *buf, int count) {
  size_t e;
  if (!isclosecap(cs->cap)) return ROSIE_CLOSE_ERROR;
  e = cs->cap->s - cs->s + 1;	/* 1-based end position */
  encode_pos(cs->L, e, 0, buf);
  return ROSIE_OK;
}

static int byte_Open(CapState *cs, rBuffer *buf, int count) {
  size_t s;
  if ((cs->cap->kind == Cclose) || (cs->cap->siz != 0)) return ROSIE_OPEN_ERROR;
  s = cs->cap->s - cs->s + 1;	/* 1-based start position */
  encode_pos(cs->L, s, 1, buf);
  encode_name(cs, buf);
  return ROSIE_OK;
}

/* Signed 32-bit integers: from −2,147,483,648 to 2,147,483,647  */
#define MAXNUMBER2STR 16
#define INT_FMT "%d"
#define r_tostring(s, max, i) (snprintf((s), (max), (INT_FMT), (i)))
#define isopencap(cap)	((captype(cap) != Cclose) && ((cap)->siz == 0))

static void json_encode_pos(lua_State *L, size_t pos, rBuffer *buf) {
  char numbuff[MAXNUMBER2STR];
  size_t len;
  len = r_tostring(numbuff, MAXNUMBER2STR, (int) pos);
  r_addlstring(L, buf, numbuff, len);
}

static void json_encode_name(CapState *cs, rBuffer *buf) {
  const char *name;
  size_t len;
  lua_rawgeti(cs->L, ktableidx(cs->ptop), cs->cap->idx);
  name = lua_tolstring(cs->L, -1, &len);
  r_addlstring(cs->L, buf, name, len);
  lua_pop(cs->L, 1);
}

static int json_Fullcapture(CapState *cs, rBuffer *buf, int count) {
  Capture *c = cs->cap;
  size_t s, e;
  if (c->siz == 0) return ROSIE_SIZ_ERROR;
  if (c->kind != Crosiecap) return ROSIE_FULLCAP_ERROR;
  if (count) r_addstring(cs->L, buf, ",");
  s = c->s - cs->s + 1;		/* 1-based start position */
  r_addstring(cs->L, buf, "{\"s\":");
  json_encode_pos(cs->L, s, buf);
  r_addstring(cs->L, buf, ",\"type\":\"");
  json_encode_name(cs, buf);
  r_addstring(cs->L, buf, "\"");
  e = s + c->siz - 1;		/* length */
  r_addstring(cs->L, buf, ",\"e\":");
  json_encode_pos(cs->L, e, buf);
  r_addstring(cs->L, buf, "}");
  return ROSIE_OK;
}

static int json_Close(CapState *cs, rBuffer *buf, int count) {
  size_t e;
  if (!cs->cap->kind==Cclose) return ROSIE_CLOSE_ERROR;
  e = cs->cap->s - cs->s + 1;	/* 1-based end position */
  r_addstring(cs->L, buf, "]");
  r_addstring(cs->L, buf, ",\"e\":");
  json_encode_pos(cs->L, e, buf);
  r_addstring(cs->L, buf, "}");
  return ROSIE_OK;
}

static int json_Open(CapState *cs, rBuffer *buf, int count) {
  size_t s;
  if (!isopencap(cs->cap) || cs->cap->kind != Crosiecap) return ROSIE_OPEN_ERROR;
  if (count) r_addstring(cs->L, buf, ",");
  s = cs->cap->s - cs->s + 1;	/* 1-based start position */
  r_addstring(cs->L, buf, "{\"s\":");
  json_encode_pos(cs->L, s, buf);
  r_addstring(cs->L, buf, ",\"type\":\"");
  json_encode_name(cs, buf);
  r_addstring(cs->L, buf, "\",");
  r_addstring(cs->L, buf, "\"subs\":[");
  return ROSIE_OK;
}

encoder_functions debug_encoder = { debug_Open, debug_Fullcapture, debug_Close };
encoder_functions byte_encoder = { byte_Open, byte_Fullcapture, byte_Close };
encoder_functions json_encoder = { json_Open, json_Fullcapture, json_Close };

/* N.B. caploop does NOT have to be recursive.  It can be a flat loop
   if we keep our own "stack" of inner count values, which is needed
   so the json encoder can insert commas into lists of more than one item. */
static int caploop(CapState *cs, encoder_functions *encode, rBuffer *buf, int count) {
  int err;
  int inner_count = 0;
  err = encode->Open(cs, buf, count); if (err) return err;
  cs->cap++;
  while (!isclosecap(cs->cap)) {
    if (cs->cap->siz == 0) {
      err = caploop(cs, encode, buf, inner_count); if (err) return err;
    }
    else {
      err = encode->Fullcapture(cs, buf, inner_count); if (err) return err;
      cs->cap++;
    }
    inner_count++;
  }
  encode->Close(cs, buf, count);
  cs->cap++;
  return ROSIE_OK;
}

int r_getcaptures(lua_State *L, const char *s, const char *r, int ptop, int etype) {
  int err;
  char code;
  encoder_functions encode;
  Capture *capture;
  rBuffer *buf = r_newbuffer(L);
  switch (etype) {
  case -1: { encode = debug_encoder; break; }
  case 0: { encode = byte_encoder; break; }
  case 1: { encode = json_encoder; break; }
  default: {
    lua_pushnil(L);
    lua_pushstring(L, "invalid encoding type");
    return 2;
  } }
  capture = (Capture *)lua_touserdata(L, caplistidx(ptop));
  code = 'B'+etype; 
  r_addchar(L, buf, code); 
  if (!isclosecap(capture)) {  /* is there a capture? */
    CapState cs;
    cs.ocap = cs.cap = capture; cs.L = L;
    cs.s = s; cs.valuecached = 0; cs.ptop = ptop;
    err = caploop(&cs, &encode, buf, 0);
    if (err) {
      lua_pushnil(L);
      lua_pushinteger(L, err);
      return 2;
    }
  }
  lua_pushinteger(L, r - s + 1); /* last position */
  return 2;			 /* an rBuffer is on the stack */
}
