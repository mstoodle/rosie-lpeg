/*  -*- Mode: C/l; -*-                                                       */
/*                                                                           */
/*  rcap.h                                                                   */
/*                                                                           */
/*  © Copyright IBM Corporation 2017.                                        */
/*  LICENSE: MIT License (https://opensource.org/licenses/mit-license.html)  */
/*  AUTHOR: Jamie A. Jennings                                                */


#if !defined(rcap_h)
#define rcap_h

/* Signed 32-bit integers: from −2,147,483,648 to 2,147,483,647  */
#define MAXNUMBER2STR 16
#define INT_FMT "%d"
#define r_inttostring(s, i) (snprintf((char *)(s), (MAXNUMBER2STR), (INT_FMT), (i)))

int debug_Fullcapture(CapState *cs, rBuffer *buf, int count);
int debug_Close(CapState *cs, rBuffer *buf, int count, const char *start);
int debug_Open(CapState *cs, rBuffer *buf, int count);

int json_Fullcapture(CapState *cs, rBuffer *buf, int count);
int json_Close(CapState *cs, rBuffer *buf, int count, const char *start);
int json_Open(CapState *cs, rBuffer *buf, int count);

int byte_Fullcapture(CapState *cs, rBuffer *buf, int count);
int byte_Close(CapState *cs, rBuffer *buf, int count, const char *start);
int byte_Open(CapState *cs, rBuffer *buf, int count);

/* Some JSON literals */
#define TYPE_LABEL ("{\"type\":\"")
#define START_LABEL (",\"s\":")
#define END_LABEL (",\"e\":")
#define DATA_LABEL (",\"data\":")
#define COMPONENT_LABEL (",\"subs\":[")

#endif
