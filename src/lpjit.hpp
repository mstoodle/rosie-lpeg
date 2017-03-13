/*
** © Copyright IBM Corporation 2016, 2017
** LICENSE: MIT License (https://opensource.org/licenses/mit-license.html)
** AUTHOR: Mark Stoodley
 */


#if !defined(lpjit_hpp)
#define lpjit_hpp

namespace TR { class TypeDictionary; }
namespace TR { class BytecodeBuilder; }

class LpMatcher : public TR::MethodBuilder {
  public:
  LpMatcher(TR::TypeDictionary *types, Pattern *p, Instruction *op, int ncode);

  void init();

  bool buildIL();

  protected:
  TR::IlValue *TestChar(TR::IlBuilder *b, uint8_t *buf, TR::IlValue *c);
  void FailBuilder(TR::BytecodeBuilder *failBuilder);
  void incrementBy1(TR::IlBuilder *b, char *sym);

  // bytecode handlers
  void doEnd         (TR::BytecodeBuilder *b);
  void doGiveUp      (TR::BytecodeBuilder *b);
  void doRet         (TR::BytecodeBuilder *b);

  void anyCommon     (TR::BytecodeBuilder *b, TR::BytecodeBuilder *targetBuilder);
  void doAny         (TR::BytecodeBuilder *b, TR::BytecodeBuilder *fallThroughBuilder);
  void doTestAny     (TR::BytecodeBuilder *b, TR::BytecodeBuilder *fallThroughBuilderftb, TR::BytecodeBuilder *targetBuilder);

  void charCommon    (TR::BytecodeBuilder *b, char aux, TR::BytecodeBuilder *targetBuilder);
  void doChar        (TR::BytecodeBuilder *b, char aux, TR::BytecodeBuilder *fallThroughBuilder);
  void doTestChar    (TR::BytecodeBuilder *b, char aux, TR::BytecodeBuilder *fallThroughBuilder, TR::BytecodeBuilder *targetBuilder);

  void setCommon     (TR::BytecodeBuilder *b, uint8_t * buf, TR::BytecodeBuilder *thenBuilder, TR::BytecodeBuilder *elseBuilder);
  void doSet         (TR::BytecodeBuilder *b, uint8_t * buf, TR::BytecodeBuilder *targetBuilder);
  void doTestSet     (TR::BytecodeBuilder *b, uint8_t * buf, TR::BytecodeBuilder *targetBuilder, TR::BytecodeBuilder *elseBuilder);

  void doBehind      (TR::BytecodeBuilder *b, int n, TR::BytecodeBuilder *fallThroughBuilder);

  void doSpan        (TR::BytecodeBuilder *b, uint8_t * buf, TR::BytecodeBuilder *fallThroughBuilder);

  void pushCapture   (TR::BytecodeBuilder *b, unsigned short key, byte kind, TR::BytecodeBuilder *fallThroughBuilder);
  void doOpenCapture (TR::BytecodeBuilder *b, unsigned short key, byte kind, TR::BytecodeBuilder *fallThroughBuilder);
  void doFullCapture (TR::BytecodeBuilder *b, unsigned short key, byte kind, byte off, TR::BytecodeBuilder *fallThroughBuilder);
  void doCloseCapture(TR::BytecodeBuilder *b, unsigned short key, byte kind, TR::BytecodeBuilder *fallThroughBuilder);

  void doCloseRuntime(TR::BytecodeBuilder *b, TR::BytecodeBuilder *fallThroughBuilder);

  int analyzeSetAsRange(uint8_t *buf, uint16_t *left, uint16_t *right);

  Pattern *_p;
  Instruction *_op;
  int _ncode;

  TR::IlType *Byte;
  TR::IlType *Short;
  TR::IlType *Int;
  TR::IlType *pInt;
  TR::IlType *pChar;
  TR::IlType *pVoid;
  TR::IlType *pInt8;
  TR::IlType *CaptureType;
  TR::IlType *pCaptureType;
};

#endif // lpjit_hpp
