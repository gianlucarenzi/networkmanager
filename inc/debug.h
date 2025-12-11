/*
 * $Id: debug.h,v 1.1 2016/12/13 12:38:02 gianluca Exp $
 */
#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <stdio.h>

extern int debuglevel; // Dichiarazione della variabile di livello di debug

/* ANSI Eye-Candy ;-) */
#define ANSI_RED    "\x1b[31m"
#define ANSI_GREEN  "\x1b[32m"
#define ANSI_YELLOW "\x1b[1;33m"
#define ANSI_BLUE   "\x1b[1;34m"
#define ANSI_RESET  "\x1b[0m"

#define printR(fmt, args...) \
	{\
		fprintf(stdout, fmt, ## args); \
	}

#define printRaw(type, fmt, args...) \
	{\
		fprintf(stdout, "%s " type " (%s): " fmt "\n\r", __FILE__, __func__, ## args); \
	}

#define printRaw_E(type, fmt, args...) \
	{\
		fprintf(stderr, "%s " type " (%s): " fmt "\n\r", __FILE__, __func__, ## args); \
	}

#define DBG_N(fmt, args...) \
  { if (debuglevel >= DBG_NOISY) {\
		fprintf(stdout, ANSI_YELLOW); \
		printRaw("NOISY", fmt,## args); \
		fprintf(stdout, ANSI_RESET); \
		fflush(stdout); \
	} \
  }

#define DBG_V(fmt, args...) \
  { if (debuglevel >= DBG_VERBOSE) {\
	fprintf(stdout, ANSI_BLUE); \
	printRaw("VERBOSE", fmt,## args); \
	fprintf(stdout, ANSI_RESET); \
	fflush(stdout); \
	} \
  }

#define DBG_I(fmt, args...) \
  { if (debuglevel >= DBG_INFO) {\
	fprintf(stdout, ANSI_GREEN); \
	printRaw("INFO", fmt,## args); \
	fprintf(stdout, ANSI_RESET); \
	fflush(stdout); \
	} \
  }

#define DBG_E(fmt, args...) \
  { \
	fprintf(stderr, ANSI_RED); \
	printRaw_E("Err", fmt, ## args); \
	fprintf(stderr, ANSI_RESET); \
	fflush(stderr); \
  }

#define DBG_ERROR   0
#define DBG_INFO    1
#define DBG_VERBOSE 2
#define DBG_NOISY   3

// Mappa LOG_INFO e LOG_ERROR alle macro DBG_* appropriate
#define LOG_INFO DBG_I
#define LOG_ERROR DBG_E

#endif // __DEBUG_H__
