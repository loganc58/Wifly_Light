#ifndef _PLATFORM_H_
#define _PLATFORM_H_
#ifdef X86
#else
	#ifndef MPLAB_IDE
	#include "16F1936.h"
	#endif
	#include "inline.h"
#endif
#endif /* #ifndef _PLATFORM_H_ */