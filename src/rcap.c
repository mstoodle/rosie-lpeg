/*  -*- Mode: C/l; -*-                                                       */
/*                                                                           */
/*  rcap.c                                                                   */
/*                                                                           */
/*  © Copyright IBM Corporation 2017.                                        */
/*  LICENSE: MIT License (https://opensource.org/licenses/mit-license.html)  */
/*  AUTHOR: Jamie A. Jennings                                                */

#define acceptable_capture(kind) (((kind) == Crosiecap) || ((kind) == Crosieconst))

#include <stdio.h>
#include <string.h>
#include "lpcap.h"
#include "rbuf.h"
#include "rcap.h"

static const char *char2escape[256] = {
    "\\u0000", "\\u0001", "\\u0002", "\\u0003",
    "\\u0004", "\\u0005", "\\u0006", "\\u0007",
    "\\b", "\\t", "\\n", "\\u000b",
    "\\f", "\\r", "\\u000e", "\\u000f",
    "\\u0010", "\\u0011", "\\u0012", "\\u0013",
    "\\u0014", "\\u0015", "\\u0016", "\\u0017",
    "\\u0018", "\\u0019", "\\u001a", "\\u001b",
    "\\u001c", "\\u001d", "\\u001e", "\\u001f", /* 28 .. 31 */
    NULL, NULL, "\\\"", NULL, NULL, NULL, NULL, NULL, /* 32 .. 39 */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, "\\/",  /* 40 .. 47 */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, /* 64 .. 71 */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, "\\\\", NULL, NULL, NULL, /* 88 .. 95 */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, "\\u007f", /* 120 .. 127 */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};


/* Worst case is len * 6 (all unicode escapes). By reserving all of
   this space in advance, we gain 15-20% performance improvement */

static void r_addlstring_json(lua_State *L, rBuffer *buf, const char *str, size_t len)
{
    static const char dquote = '\"';
    const char *escstr;
    size_t esclen;
    char c;
    size_t i;
    r_prepbuffsize(L, buf, 2 + 6*len);
    r_addchar_UNSAFE(L, buf, dquote);
    for (i = 0; i < len; i++) { 
      c = str[i];
      /* this explicit test on c gives about a 5% speedup on typical data */
      if ((c=='\"') || (c=='/') || (c=='\\') || (c>0 && c<32) || (c==127)) {
	escstr = char2escape[(unsigned char)c]; 
	if (escstr) {
	  esclen = strlen(escstr);
	  r_addlstring_UNSAFE(L, buf, escstr, esclen); /* escstr is null terminated */ 
	}
	/* TODO: what to do in case of error? it is a coding error, a "should not get here" situation */
	else fprintf(stderr, "*** INTERNAL ERROR in addlstring_json: unmapped esc for char code %d", (int) c);
      }
      else r_addchar_UNSAFE(L, buf, c);
    } 
    r_addchar(L, buf, dquote);
}



#define UNUSED(x) (void)(x)

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

static void print_constant_capture(CapState *cs) {
  const char *name;
  size_t len;
  lua_rawgeti(cs->L, ktableidx(cs->ptop), cs->cap->idx+1);
  name = lua_tolstring(cs->L, -1, &len);
  printf("  constant match: %s\n", name);
  lua_pop(cs->L, 1);
}

int debug_Fullcapture(CapState *cs, rBuffer *buf, int count) {
  Capture *c = cs->cap;
  const char *start = c->s;
  const char *last = c->s + c->siz - 1;
  UNUSED(buf); UNUSED(count);
  printf("Full capture:\n");
  print_capture(cs);
  if ( !(isfullcap(c) && acceptable_capture(c->kind)) ) return ROSIE_FULLCAP_ERROR;
  if (c->kind==Crosieconst)
       print_constant_capture(cs);
  else { print_capture_text(start, last); }
  return ROSIE_OK;
}

int debug_Close(CapState *cs, rBuffer *buf, int count, const char *start) {
  UNUSED(buf); UNUSED(count); UNUSED(start);
  if (!isclosecap(cs->cap)) return ROSIE_CLOSE_ERROR;
  printf("CLOSE:\n");
  print_capture(cs);
  return ROSIE_OK;
}

int debug_Open(CapState *cs, rBuffer *buf, int count) {
  UNUSED(buf); UNUSED(count);
  /* if ((cs->cap->kind == Cclose) || isfullcap(cs->cap)) return ROSIE_OPEN_ERROR; */
  if (isfullcap(cs->cap) || !acceptable_capture(cs->cap->kind)) return ROSIE_OPEN_ERROR;
  printf("OPEN:\n");
  print_capture(cs);
  return ROSIE_OK;
}

static void json_encode_pos(lua_State *L, size_t pos, rBuffer *buf) {
  char nb[MAXNUMBER2STR];
  size_t len;
  len = r_inttostring(nb, (int) pos);
  r_addlstring(L, buf, nb, len);
}

static void json_encode_name(CapState *cs, rBuffer *buf, int offset) {
  const char *name;
  size_t len;
  lua_rawgeti(cs->L, ktableidx(cs->ptop), cs->cap->idx + offset);
  name = lua_tolstring(cs->L, -1, &len);
  r_addlstring(cs->L, buf, name, len);
  lua_pop(cs->L, 1);
}

int json_Fullcapture(CapState *cs, rBuffer *buf, int count) {
  Capture *c = cs->cap;
  size_t s, e;
  if ( !(isfullcap(c) && acceptable_capture(c->kind)) ) return ROSIE_FULLCAP_ERROR;
  if (count) r_addstring(cs->L, buf, ",");
  s = c->s - cs->s + 1;		/* 1-based start position */
  r_addstring(cs->L, buf, TYPE_LABEL);
  json_encode_name(cs, buf, 0);
  r_addstring(cs->L, buf, "\"");
  r_addstring(cs->L, buf, START_LABEL);
  json_encode_pos(cs->L, s, buf);
  r_addstring(cs->L, buf, END_LABEL);
  e = s + c->siz - 1;		/* length */
  json_encode_pos(cs->L, e, buf);
  r_addstring(cs->L, buf, DATA_LABEL);

  switch (c->kind) {
  case Crosiecap: { r_addlstring_json(cs->L, buf, c->s, c->siz -1); break; }
  case Crosieconst: {
       r_addstring(cs->L, buf, "\"");
       json_encode_name(cs, buf, 1);
       r_addstring(cs->L, buf, "\"");
       break; }
  default: return ROSIE_FULLCAP_ERROR;
  }
  r_addstring(cs->L, buf, "}");
  return ROSIE_OK;
}

int json_Close(CapState *cs, rBuffer *buf, int count, const char *start) {
  size_t e;
  UNUSED(count);
  if (!isclosecap(cs->cap)) return ROSIE_CLOSE_ERROR;
  e = cs->cap->s - cs->s + 1;	/* 1-based end position */
  if (!isopencap(cs->cap-1)) r_addstring(cs->L, buf, "]");
  r_addstring(cs->L, buf,  END_LABEL);
  json_encode_pos(cs->L, e, buf);
  if (start) {
    r_addstring(cs->L, buf, DATA_LABEL);
    r_addlstring_json(cs->L, buf, start, cs->cap->s - start);
  }
  r_addstring(cs->L, buf, "}");
  return ROSIE_OK;
}

int json_Open(CapState *cs, rBuffer *buf, int count) {
  size_t s;
  /* if (! (isopencap(cs->cap) || acceptable_capture(cs->cap->kind)) ) return ROSIE_OPEN_ERROR; */
  if (isfullcap(cs->cap) || !acceptable_capture(cs->cap->kind)) return ROSIE_OPEN_ERROR;
  if (count) r_addstring(cs->L, buf, ",");
  r_addstring(cs->L, buf, TYPE_LABEL);
  json_encode_name(cs, buf, 0);
  r_addstring(cs->L, buf, "\"");
  s = cs->cap->s - cs->s + 1;	/* 1-based start position */
  r_addstring(cs->L, buf, START_LABEL);
  json_encode_pos(cs->L, s, buf);
  /* introduce subs array if needed */
  if (!isclosecap(cs->cap+1)) r_addstring(cs->L, buf, COMPONENT_LABEL);
  return ROSIE_OK;
}

/* ****************************************************************************************
 * See r_pushmatch in lptree.c for the decoder 
 * ****************************************************************************************
 */

/* The byte array encoding assumes that the input text length fits
   into 2^31, i.e. a signed int, and that the name length fits into
   2^15, i.e. a signed short.  It is the responsibility of rmatch to
   ensure this. */

static void encode_pos(lua_State *L, size_t pos, int negate, rBuffer *buf) {
  int intpos = (int) pos;
  if (negate) intpos = - intpos;
  r_addint(L, buf, intpos);
}

static void encode_string(lua_State *L, const char *str, size_t len,
			  byte shortflag, byte constcap, rBuffer *buf) {
  /* encode size as a short or an int */
  if (shortflag) r_addshort(L, buf, (short) (constcap ? -len : len));
  else r_addint(L, buf, (int) (constcap ? -len : len));
  /* encode the string by copying it into the buffer */
  r_addlstring(L, buf, str, len); 
}

static void encode_name(CapState *cs, rBuffer *buf, int offset) {
  const char *name;
  size_t len;
  lua_rawgeti(cs->L, ktableidx(cs->ptop), cs->cap->idx + offset); 
  name = lua_tolstring(cs->L, -1, &len); 
  encode_string(cs->L, name, len, 1, offset, buf); /* shortflag and constcap are set */
  lua_pop(cs->L, 1);				   /* pop name */
}

int byte_Fullcapture(CapState *cs, rBuffer *buf, int count) {
  size_t s, e;
  Capture *c = cs->cap;
  UNUSED(count);
  if (! (isfullcap(c) || acceptable_capture(c->kind)) ) return ROSIE_FULLCAP_ERROR;
  s = c->s - cs->s + 1;		/* 1-based start position */
  e = s + c->siz - 1;
  encode_pos(cs->L, s, 1, buf);	/* negative flag is set */
  /* special case for constant captures: put the capture text into the buffer
   * before the pattern typename, and use a negative length to mark its presence
   */
  if (c->kind == Crosieconst) encode_name(cs, buf, 1);
  encode_name(cs, buf, 0);
  encode_pos(cs->L, e, 0, buf);
  return ROSIE_OK;
}

int byte_Close(CapState *cs, rBuffer *buf, int count, const char *start) {
  size_t e;
  UNUSED(count); UNUSED(start);
  if (!isclosecap(cs->cap)) return ROSIE_CLOSE_ERROR;
  e = cs->cap->s - cs->s + 1;	/* 1-based end position */
  encode_pos(cs->L, e, 0, buf);
  return ROSIE_OK;
}

int byte_Open(CapState *cs, rBuffer *buf, int count) {
  size_t s;
  UNUSED(count);
  if (isfullcap(cs->cap) || !acceptable_capture(cs->cap->kind)) {
       fprintf(stderr, "*** isfullcap-> %d, !acceptable_capture()->%d\n",
	       isfullcap(cs->cap),
	       !acceptable_capture(cs->cap->kind));
       fprintf(stderr, "*** *s-> %p, idx-> %d, kind-> %d, siz-> %d\n",
	       (const void *)cs->cap->s,
	       cs->cap->idx,
	       cs->cap->kind,
	       cs->cap->siz);
       return ROSIE_OPEN_ERROR;
  }
  s = cs->cap->s - cs->s + 1;	/* 1-based start position */
  encode_pos(cs->L, s, 1, buf);
  encode_name(cs, buf, 0);
  return ROSIE_OK;
}

