/*
** © Copyright IBM Corporation 2016, 2017
** LICENSE: MIT License (https://opensource.org/licenses/mit-license.html)
** AUTHOR: Mark Stoodley
 */


#if !defined(lpjit_h)
#define lpjit_h

struct lua_State;
struct Pattern;
union Instruction;
struct Capture;

typedef struct JitInterface {
  void *lib;
  int jitSizeThreshold;
  int jitCountThreshold;
  int (*startJit)(const char *libpath, int sizeThreshold, int countThreshold);
  void (*stopJit)(void);
  void (*compilePattern)(struct Pattern *pattern, Instruction *op, int ncode);
  const char * (*matchWithCompiledPattern)(lua_State *L, const char *o, const char *s, const char *e,
                                           struct Pattern *pattern, Instruction *op, Capture *capture, int ptop, int ncode);
} JitInterface;

extern JitInterface *JIT;

extern int loadJit(const char *libpath, int jitSizeThreshold, int jitCompileThreshold);
extern void unloadJit(void);

#endif // lpjit_h
