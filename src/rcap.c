/*  -*- Mode: C/l; -*-                                                       */
/*                                                                           */
/*  rcap.c                                                                   */
/*                                                                           */
/*  © Copyright IBM Corporation 2017.                                        */
/*  LICENSE: MIT License (https://opensource.org/licenses/mit-license.html)  */
/*  AUTHOR: Jamie A. Jennings                                                */


#include <stdio.h>
#include <string.h>
#include "lpcap.h"
#include "rbuf.h"
#include "rcap.h"

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

int debug_Fullcapture(CapState *cs, rBuffer *buf, int count) {
  Capture *c = cs->cap;
  const char *start = c->s;
  const char *last = c->s + c->siz - 1;
  UNUSED(buf); UNUSED(count);
  printf("Full capture:\n");
  print_capture(cs);
  if ((cs->cap->siz == 0) || (c->kind == Cclose)) return ROSIE_FULLCAP_ERROR;
  print_capture_text(start, last);
  return ROSIE_OK;
}

int debug_Close(CapState *cs, rBuffer *buf, int count) {
  UNUSED(buf); UNUSED(count);
  if (!isclosecap(cs->cap)) return ROSIE_CLOSE_ERROR;
  printf("CLOSE:\n");
  print_capture(cs);
  return ROSIE_OK;
}

int debug_Open(CapState *cs, rBuffer *buf, int count) {
  UNUSED(buf); UNUSED(count);
  if ((cs->cap->kind == Cclose) || (cs->cap->siz != 0)) return ROSIE_OPEN_ERROR;
  printf("OPEN:\n");
  print_capture(cs);
  return ROSIE_OK;
}

/* Signed 32-bit integers: from −2,147,483,648 to 2,147,483,647  */
#define MAXNUMBER2STR 16
#define INT_FMT "%d"
#define r_inttostring(s, i) (snprintf((char *)(s), (MAXNUMBER2STR), (INT_FMT), (i)))
#define isopencap(cap)	((captype(cap) != Cclose) && ((cap)->siz == 0))

static void json_encode_pos(lua_State *L, size_t pos, rBuffer *buf) {
  char nb[MAXNUMBER2STR];
  size_t len;
  len = r_inttostring(nb, (int) pos);
  r_addlstring(L, buf, nb, len);
}

static void json_encode_name(CapState *cs, rBuffer *buf) {
  const char *name;
  size_t len;
  lua_rawgeti(cs->L, ktableidx(cs->ptop), cs->cap->idx);
  name = lua_tolstring(cs->L, -1, &len);
  r_addlstring(cs->L, buf, name, len);
  lua_pop(cs->L, 1);
}

int json_Fullcapture(CapState *cs, rBuffer *buf, int count) {
  Capture *c = cs->cap;
  size_t s, e;
  if ((c->siz == 0) || (c->kind != Crosiecap)) return ROSIE_FULLCAP_ERROR;
  if (count) r_addstring(cs->L, buf, ",");
  s = c->s - cs->s + 1;		/* 1-based start position */
  r_addstring(cs->L, buf, "{\"s\":");
  json_encode_pos(cs->L, s, buf);
  r_addstring(cs->L, buf, ",\"type\":\"");
  json_encode_name(cs, buf);
  /* r_addstring(cs->L, buf, "\",\"subs\":[],\"e\":"); */
  r_addstring(cs->L, buf, "\",\"e\":");
  e = s + c->siz - 1;		/* length */
  json_encode_pos(cs->L, e, buf);
  r_addstring(cs->L, buf, "}");
  return ROSIE_OK;
}

int json_Close(CapState *cs, rBuffer *buf, int count) {
  size_t e;
  UNUSED(count);
  if (!isclosecap(cs->cap)) return ROSIE_CLOSE_ERROR;
  e = cs->cap->s - cs->s + 1;	/* 1-based end position */
  if (!isopencap(cs->cap-1)) r_addstring(cs->L, buf, "]");
  r_addstring(cs->L, buf, ",\"e\":");
  json_encode_pos(cs->L, e, buf);
  r_addstring(cs->L, buf, "}");
  return ROSIE_OK;
}

int json_Open(CapState *cs, rBuffer *buf, int count) {
  size_t s;
  if (!isopencap(cs->cap) || cs->cap->kind != Crosiecap) return ROSIE_OPEN_ERROR;
  if (count) r_addstring(cs->L, buf, ",");
  s = cs->cap->s - cs->s + 1;	/* 1-based start position */
  r_addstring(cs->L, buf, "{\"s\":");
  json_encode_pos(cs->L, s, buf);
  r_addstring(cs->L, buf, ",\"type\":\"");
  json_encode_name(cs, buf);
  if (isclosecap(cs->cap+1)) {r_addstring(cs->L, buf, "\"");}
  else {r_addstring(cs->L, buf, "\",\"subs\":[");}
  return ROSIE_OK;
}

/* The byte array encoding assumes that the input text length fits
   into 2^31, i.e. a signed int, and that the name length fits into
   2^15, i.e. a signed short.  It is the responsibility of rmatch to
   ensure this. */

static void encode_pos(lua_State *L, size_t pos, int negate, rBuffer *buf) {
  int intpos = (int) pos;
  if (negate) intpos = - intpos;
  r_addint(L, buf, intpos);
}

static void encode_string(lua_State *L, const char *str, size_t len, byte shortflag, rBuffer *buf) {
  /* encode size as a short or an int */
  if (shortflag) r_addshort(L, buf, (short) len);
  else r_addint(L, buf, (int) len);
  /* encode the string by copying it into the buffer */
  r_addlstring(L, buf, str, len); 
}

static void encode_name(CapState *cs, rBuffer *buf) {
  const char *name;
  size_t len;
  lua_rawgeti(cs->L, ktableidx(cs->ptop), cs->cap->idx); 
  name = lua_tolstring(cs->L, -1, &len); 
  encode_string(cs->L, name, len, 1, buf); /* shortflag is set */
  lua_pop(cs->L, 1);			   /* pop name */
}

int byte_Fullcapture(CapState *cs, rBuffer *buf, int count) {
  size_t s, e;
  Capture *c = cs->cap;
  UNUSED(count);
  if (!isfullcap(c) || (c->kind != Crosiecap)) return ROSIE_FULLCAP_ERROR;
  s = c->s - cs->s + 1;		/* 1-based start position */
  e = s + c->siz - 1;
  encode_pos(cs->L, s, 1, buf);	/* negative flag is set */
  encode_name(cs, buf);
  encode_pos(cs->L, e, 0, buf);
  return ROSIE_OK;
}

int byte_Close(CapState *cs, rBuffer *buf, int count) {
  size_t e;
  UNUSED(count);
  if (!isclosecap(cs->cap)) return ROSIE_CLOSE_ERROR;
  e = cs->cap->s - cs->s + 1;	/* 1-based end position */
  encode_pos(cs->L, e, 0, buf);
  return ROSIE_OK;
}

int byte_Open(CapState *cs, rBuffer *buf, int count) {
  size_t s;
  UNUSED(count);
  if ((cs->cap->kind != Crosiecap) || (cs->cap->siz != 0)) return ROSIE_OPEN_ERROR;
  s = cs->cap->s - cs->s + 1;	/* 1-based start position */
  encode_pos(cs->L, s, 1, buf);
  encode_name(cs, buf);
  return ROSIE_OK;
}

