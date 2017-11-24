/*
** $Id: lpvm.c,v 1.6 2015/09/28 17:01:25 roberto Exp $
** Copyright 2007, Lua.org & PUC-Rio  (see 'lpeg.html' for license)
*/

#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <time.h>


#include "lua.h"
#include "lauxlib.h"

#include "lpcap.h"
#include "lptypes.h"
#include "lpvm.h"
#include "lpprint.h"

//#define MEASURE // record time before and after matches, accumulate across all match calls, report at exit
//#define DEBUG   // detailed info about interpreter execution

/* initial size for call/backtrack stack */
#if !defined(INITBACK)
#define INITBACK	MAXBACK
#endif

#define getoffset(p)	(((p) + 1)->offset)

static const Instruction giveup = {{IGiveup, 0, 0}};


/*
** {======================================================
** Virtual Machine
** =======================================================
*/


typedef struct Stack {
  const char *s;  /* saved position (or NULL for calls) */
  const Instruction *p;  /* next instruction */
  int caplevel;
} Stack;


#define getstackbase(L, ptop)	((Stack *)lua_touserdata(L, stackidx(ptop)))


/*
** Double the size of the array of captures
*/
Capture *doublecap (lua_State *L, Capture *cap, int captop, int ptop) {
  Capture *newc;
  if (captop >= INT_MAX/((int)sizeof(Capture) * 2))
    luaL_error(L, "too many captures");
  newc = (Capture *)lua_newuserdata(L, captop * 2 * sizeof(Capture));
  memcpy(newc, cap, captop * sizeof(Capture));
  lua_replace(L, caplistidx(ptop));
  return newc;
}


/*
** Double the size of the stack
*/
Stack *doublestack (lua_State *L, Stack **stacklimit, int ptop) {
  Stack *stack = getstackbase(L, ptop);
  Stack *newstack;
  int n = *stacklimit - stack;  /* current stack size */
  int max, newn;
  lua_getfield(L, LUA_REGISTRYINDEX, MAXSTACKIDX);
  max = lua_tointeger(L, -1);  /* maximum allowed size */
  lua_pop(L, 1);
  if (n >= max)  /* already at maximum size? */
    luaL_error(L, "backtrack stack overflow (current limit is %d)", max);
  newn = 2 * n;  /* new size */
  if (newn > max) newn = max;
  newstack = (Stack *)lua_newuserdata(L, newn * sizeof(Stack));
  memcpy(newstack, stack, n * sizeof(Stack));
  lua_replace(L, stackidx(ptop));
  *stacklimit = newstack + newn;
  return newstack + n;  /* return next position */
}


/*
** Interpret the result of a dynamic capture: false -> fail;
** true -> keep current position; number -> next position.
** Return new subject position. 'fr' is stack index where
** is the result; 'curr' is current subject position; 'limit'
** is subject's size.
*/
int resdyncaptures (lua_State *L, int fr, int curr, int limit) {
  lua_Integer res;
  if (!lua_toboolean(L, fr)) {  /* false value? */
    lua_settop(L, fr - 1);  /* remove results */
    return -1;  /* and fail */
  }
  else if (lua_isboolean(L, fr))  /* true? */
    res = curr;  /* keep current position */
  else {
    res = lua_tointeger(L, fr) - 1;  /* new position */
    if (res < curr || res > limit)
      luaL_error(L, "invalid position returned by match-time capture");
  }
  lua_remove(L, fr);  /* remove first result (offset) */
  return res;
}


/*
** Add capture values returned by a dynamic capture to the capture list
** 'base', nested inside a group capture. 'fd' indexes the first capture
** value, 'n' is the number of values (at least 1).
*/
void adddyncaptures (const char *s, Capture *base, int n, int fd) {
  int i;
  /* Cgroup capture is already there */
  assert(base[0].kind == Cgroup && base[0].siz == 0);
  base[0].idx = 0;  /* make it an anonymous group */
  for (i = 1; i <= n; i++) {  /* add runtime captures */
    base[i].kind = Cruntime;
    base[i].siz = 1;  /* mark it as closed */
    base[i].idx = fd + i - 1;  /* stack index of capture value */
    base[i].s = s;
  }
  base[i].kind = Cclose;  /* close group */
  base[i].siz = 1;
  base[i].s = s;
}


/*
** Remove dynamic captures from the Lua stack (called in case of failure)
*/
int removedyncap (lua_State *L, Capture *capture,
                         int level, int last) {
  int id = finddyncap(capture + level, capture + last);  /* index of 1st cap. */
  int top = lua_gettop(L);
  if (id == 0) return 0;  /* no dynamic captures? */
  lua_settop(L, id - 1);  /* remove captures */
  return top - id + 1;  /* number of values removed */
}

#if defined(MEASURE)
// these are also referenced in lpjit.cpp
double cumulativeInterpretedMatchTime = 0.0;
double cumulativeTimeToMatch = 0.0;
int numMeasuredMatches = 0;
#endif

/* 
  Mark reports: 98% of bytecodes executed in the Rosie syslog pattern are these (in order): 
    TestSet, Any, PartialCommit
  Also from Mark: His code looks for char sets that can be replaced with a range, because the
  range test is faster.  New lpeg opcode and compiler optimization?
*/

/* Support for JIT compilation and profiling */

extern void startJit(int sizeThreshold);
extern void stopJit();

void cleanup(void) {
#if defined(MEASURE)
  fprintf(stderr,"Measured %d matches\n", numMeasuredMatches);
  if (cumulativeTimeToMatch > 1)
    fprintf(stderr,"Total match time is %l6.3f seconds\n", cumulativeTimeToMatch);
  else
    fprintf(stderr,"Total match time is %l3.0f milliseconds\n", cumulativeTimeToMatch * 1000.0);

  if (cumulativeInterpretedMatchTime > 1)
    fprintf(stderr,"Total interpreted match time is %l6.3f seconds\n", cumulativeInterpretedMatchTime);
  else
    fprintf(stderr,"Total interpreted match time is %l3.0f milliseconds\n", cumulativeInterpretedMatchTime * 1000.0);
#endif
  stopJit();
}

/*
** Opcode interpreter
*/
const char *match (lua_State *L, const char *o, const char *s, const char *e,
                   Instruction *op, Capture *capture, int ptop, int ncode) {
  Stack stackbase[INITBACK];
  Stack *stacklimit = stackbase + INITBACK;
  Stack *stack = stackbase;  /* point to first empty slot in stack */
  int capsize = INITCAPSIZE;
  int captop = 0;  /* point to first empty slot in captures */
  int ndyncap = 0;  /* number of dynamic captures (in Lua stack) */
  const Instruction *p = op;  /* current instruction */

#if defined(MEASURE)
  struct timespec before, after;
  int rcBefore = clock_gettime(CLOCK_REALTIME, &before);
#endif

  lua_pushlightuserdata(L, stackbase);

  stack->p = &giveup; stack->s = s; stack->caplevel = 0; stack++;
  for (;;) {
#if defined(DEBUG)
      printf("s: |%s| stck:%d, dyncaps:%d, caps:%d  ",
             s, stack - getstackbase(L, ptop), ndyncap, captop);
      printinst(op, p);
      printcaplist(capture, capture + captop);
      fflush();
#endif

    assert(stackidx(ptop) + ndyncap == lua_gettop(L) && ndyncap <= captop);
    switch ((Opcode)p->i.code) {
      case IEnd: {
        assert(stack == getstackbase(L, ptop) + 1);
	/* this Cclose capture is a sentinel to mark the end of the linked caplist */
        capture[captop].kind = Cclose;
        capture[captop].s = NULL;

#if defined(MEASURE)
        int rcAfter = clock_gettime(CLOCK_REALTIME, &after);
        if (rcBefore != -1 && rcAfter != -1) {
          double timeToMatch = (after.tv_sec - before.tv_sec) + (double)(after.tv_nsec - before.tv_nsec)/1000000000.0;
          cumulativeInterpretedMatchTime += timeToMatch;
        }
#endif

        return s;
      }
      case IGiveup: {
        assert(stack == getstackbase(L, ptop));
        return NULL;
      }
      case IRet: {
        assert(stack > getstackbase(L, ptop) && (stack - 1)->s == NULL);
        p = (--stack)->p;
        continue;
      }
      case IAny: {
        if (s < e) { p++; s++; }
        else goto fail;
        continue;
      }
      case ITestAny: {
        if (s < e) p += 2;
        else p += getoffset(p);
        continue;
      }
      case IChar: {
        if ((byte)*s == p->i.aux && s < e) { p++; s++; }
        else goto fail;
        continue;
      }
      case ITestChar: {
        if ((byte)*s == p->i.aux && s < e) p += 2;
        else p += getoffset(p);
        continue;
      }
      case ISet: {
        int c = (byte)*s;
        if (testchar((p+1)->buff, c) && s < e)
          { p += CHARSETINSTSIZE; s++; }
        else goto fail;
        continue;
      }
      case ITestSet: {
        int c = (byte)*s;
        if (testchar((p + 2)->buff, c) && s < e)
          p += 1 + CHARSETINSTSIZE;
        else p += getoffset(p);
        continue;
      }
      case IBehind: {
        int n = p->i.aux;
        if (n > s - o) goto fail;
        s -= n; p++;
        continue;
      }
      case ISpan: {
        for (; s < e; s++) {
          int c = (byte)*s;
          if (!testchar((p+1)->buff, c)) break;
        }
        p += CHARSETINSTSIZE;
        continue;
      }
      case IJmp: {
        p += getoffset(p);
        continue;
      }
      case IChoice: {
        if (stack == stacklimit)
          stack = doublestack(L, &stacklimit, ptop);
        stack->p = p + getoffset(p);
        stack->s = s;
        stack->caplevel = captop;
        stack++;
        p += 2;
        continue;
      }
      case ICall: {
        if (stack == stacklimit)
          stack = doublestack(L, &stacklimit, ptop);
        stack->s = NULL;
        stack->p = p + 2;  /* save return address */
        stack++;
        p += getoffset(p);
        continue;
      }
      case ICommit: {
        assert(stack > getstackbase(L, ptop) && (stack - 1)->s != NULL);
        stack--;
        p += getoffset(p);
        continue;
      }
      case IPartialCommit: {
        assert(stack > getstackbase(L, ptop) && (stack - 1)->s != NULL);
        (stack - 1)->s = s;
        (stack - 1)->caplevel = captop;
        p += getoffset(p);
        continue;
      }
      case IBackCommit: {
        assert(stack > getstackbase(L, ptop) && (stack - 1)->s != NULL);
        s = (--stack)->s;
        captop = stack->caplevel;
        p += getoffset(p);
        continue;
      }
      case IFailTwice:
        assert(stack > getstackbase(L, ptop));
        stack--;
	__attribute__ ((fallthrough));
      case IFail:
      fail: { /* pattern failed: try to backtrack */
        do {  /* remove pending calls */
          assert(stack > getstackbase(L, ptop));
          s = (--stack)->s;
        } while (s == NULL);
        if (ndyncap > 0)  /* is there matchtime captures? */
          ndyncap -= removedyncap(L, capture, stack->caplevel, captop);
        captop = stack->caplevel;
        p = stack->p;
        continue;
      }
      case ICloseRunTime: {
        /* note this code is also copied to lpjit.cpp */
        /* any changes made here should also be reflected in helperCloseRuntime() in lpjit.cpp */
        CapState cs;
        int rem, res, n;
        int fr = lua_gettop(L) + 1;  /* stack index of first result */
        cs.s = o; cs.L = L; cs.ocap = capture; cs.ptop = ptop;
        n = runtimecap(&cs, capture + captop, s, &rem);  /* call function */
        captop -= n;  /* remove nested captures */
        fr -= rem;  /* 'rem' items were popped from Lua stack */
        res = resdyncaptures(L, fr, s - o, e - o);  /* get result */
        if (res == -1)  /* fail? */
          goto fail;
        s = o + res;  /* else update current position */
        n = lua_gettop(L) - fr + 1;  /* number of new captures */
        ndyncap += n - rem;  /* update number of dynamic captures */
        if (n > 0) {  /* any new capture? */
          if ((captop += n + 2) >= capsize) {
            capture = doublecap(L, capture, captop, ptop);
            capsize = 2 * captop;
          }
          /* add new captures to 'capture' list */
          adddyncaptures(s, capture + captop - n - 2, n, fr); 
        }
        p++;
        continue;
      }
      case ICloseCapture: {
        const char *s1 = s;
        assert(captop > 0);
        /* if possible, turn capture into a full capture */
        if (capture[captop - 1].siz == 0 &&
            s1 - capture[captop - 1].s < UCHAR_MAX) {
          capture[captop - 1].siz = s1 - capture[captop - 1].s + 1;
          p++;
          continue;
        }
        else {
          capture[captop].siz = 1;  /* mark entry as closed */
          capture[captop].s = s;
          goto pushcapture;
        }
      }
      case IOpenCapture:
        capture[captop].siz = 0;  /* mark entry as open */
        capture[captop].s = s;
        goto pushcapture;
      case IFullCapture:
        capture[captop].siz = getoff(p) + 1;  /* save capture size */
        capture[captop].s = s - getoff(p);
        /* goto pushcapture; */
      pushcapture: {
        capture[captop].idx = p->i.key;
        capture[captop].kind = getkind(p);
        if (++captop >= capsize) {
          capture = doublecap(L, capture, captop, ptop);
          capsize = 2 * captop;
        }
        p++;
        continue;
      }
      case IHalt: {				    /* rosie */
	/* FUTURE: Maybe unwind the stack, if there is any info there that we could use? */
        capture[captop].kind = Cfinal;
        capture[captop].s = s;
        return s;
      }
      default: assert(0); return NULL;
    }
  }
}



/* }====================================================== */


