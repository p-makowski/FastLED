#ifndef __INC_CLOCKLESS_H
#define __INC_CLOCKLESS_H

#include "controller.h"
#include "lib8tion.h"
#include "led_sysdefs.h"

// Macro to convert from nano-seconds to clocks and clocks to nano-seconds
// #define NS(_NS) (_NS / (1000 / (F_CPU / 1000000L)))
#if F_CPU < 96000000
#define NS(_NS) ( (_NS * (F_CPU / 1000000L))) / 1000
#define CLKS_TO_MICROS(_CLKS) _CLKS / (F_CPU / 1000000L)
#else
#define NS(_NS) ( (_NS * (F_CPU / 2000000L))) / 1000
#define CLKS_TO_MICROS(_CLKS) _CLKS / (F_CPU / 2000000L)
#endif

//  Macro for making sure there's enough time available
#define NO_TIME(A, B, C) (NS(A) < 3 || NS(B) < 3 || NS(C) < 6)

#if defined(__MK20DX128__)
   extern volatile uint32_t systick_millis_count;
#  define MS_COUNTER systick_millis_count
#elif defined(__SAM3X8E__)
	extern volatile uint32_t fuckit;
#	define MS_COUNTER fuckit
#else
#  if defined(CORE_TEENSY)
     extern volatile unsigned long timer0_millis_count;
#    define MS_COUNTER timer0_millis_count
#  else
     extern volatile unsigned long timer0_millis;
#    define MS_COUNTER timer0_millis
#  endif
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Base template for clockless controllers.  These controllers have 3 control points in their cycle for each bit.  The first point
// is where the line is raised hi.  The second pointsnt is where the line is dropped low for a zero.  The third point is where the 
// line is dropped low for a one.  T1, T2, and T3 correspond to the timings for those three in clock cycles.
//
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Convinience macros to wrap around the toggling of hi vs. lo
#define SET_LO FLIP ? FastPin<DATA_PIN>::fastset(port, hi) : FastPin<DATA_PIN>::fastset(port, lo); 
#define SET_HI FLIP ? FastPin<DATA_PIN>::fastset(port, lo) : FastPin<DATA_PIN>::fastset(port, hi); 

#ifdef FASTLED_AVR
template <uint8_t DATA_PIN, int T1, int T2, int T3, EOrder RGB_ORDER = RGB, bool FLIP = false, int WAIT_TIME = 50>
class ClocklessController : public CLEDController {
	typedef typename FastPin<DATA_PIN>::port_ptr_t data_ptr_t;
	typedef typename FastPin<DATA_PIN>::port_t data_t;

	data_t mPinMask;
	data_ptr_t mPort;
	CMinWait<WAIT_TIME> mWait;
public:
	virtual void init() { 
		FastPin<DATA_PIN>::setOutput();
		mPinMask = FastPin<DATA_PIN>::mask();
		mPort = FastPin<DATA_PIN>::port();
	}

	template <int N>inline static void bitSetFast(register data_ptr_t port, register data_t hi, register data_t lo, register uint8_t b) { 
		// First cycle
		SET_HI; 								// 1/2 clock cycle if using out
		delaycycles<T1 - (_CYCLES(DATA_PIN) + 1)>();					// 1st cycle length minus 1/2 clock for out, 1 clock for sbrs
		__asm__ __volatile__ ("sbrs %0, %1" :: "r" (b), "M" (N) :); 	// 1 clock for check (+1 if skipping, next op is also 1 clock)

		// Second cycle
		SET_LO;								// 1/2 clock cycle if using out
		delaycycles<T2 - _CYCLES(DATA_PIN)>(); 							// 2nd cycle length minus 1/2 clock for out

		// Third cycle
		SET_LO;								// 1 clock cycle if using out
		delaycycles<T3 - _CYCLES(DATA_PIN)>();							// 3rd cycle length minus 1 clock for out
	}
	
	#define END_OF_BYTE
	#define END_OF_LOOP 6 		// loop compare, jump, next uint8_t load
	template <int N, int ADJ>inline static void bitSetLast(register data_ptr_t port, register data_t hi, register data_t lo, register uint8_t b) { 
		// First cycle
		SET_HI; 							// 1 clock cycle if using out, 2 otherwise
		delaycycles<T1 - (_CYCLES(DATA_PIN) + 1)>();					// 1st cycle length minus 1 clock for out, 1 clock for sbrs
		__asm__ __volatile__ ("sbrs %0, %1" :: "r" (b), "M" (N) :); // 1 clock for check (+1 if skipping, next op is also 1 clock)

		// Second cycle
		SET_LO;							// 1/2 clock cycle if using out
		delaycycles<T2 - _CYCLES(DATA_PIN)>(); 						// 2nd cycle length minus 1/2 clock for out

		// Third cycle
		SET_LO;							// 1/2 clock cycle if using out
		delaycycles<T3 - (_CYCLES(DATA_PIN) + ADJ)>();				// 3rd cycle length minus 7 clocks for out, loop compare, jump, next uint8_t load
	}

	virtual void clearLeds(int nLeds) {
		showColor(CRGB(0, 0, 0), nLeds, 0);
	}

	// set all the leds on the controller to a given color
	virtual void showColor(const struct CRGB & data, int nLeds, uint8_t scale = 255) {
		mWait.wait();
		cli();

		showRGBInternal<0, false>(nLeds, scale, (const byte*)&data);

		// Adjust the timer
		long microsTaken = CLKS_TO_MICROS(nLeds * 8 * (T1 + T2 + T3));
		MS_COUNTER += (microsTaken / 1000);
		sei();
		mWait.mark();
	}

	virtual void show(const struct CRGB *rgbdata, int nLeds, uint8_t scale = 255) { 
		mWait.wait();
		cli();

		showRGBInternal<0, true>(nLeds, scale, (const byte*)rgbdata);

		// Adjust the timer
		long microsTaken = CLKS_TO_MICROS(nLeds * 8 * (T1 + T2 + T3));
		MS_COUNTER += (microsTaken / 1000);
		sei();
		mWait.mark();
	}

#ifdef SUPPORT_ARGB
	virtual void show(const struct CARGB *rgbdata, int nLeds, uint8_t scale = 255) { 
		mWait.wait();
		cli();

		showRGBInternal<1, true>(nLeds, scale, (const byte*)rgbdata);

		// Adjust the timer
		long microsTaken = CLKS_TO_MICROS(nLeds * 8 * (T1 + T2 + T3));
		MS_COUNTER += (microsTaken / 1000);
		sei();
		mWait.mark();
	}
#endif

	// This method is made static to force making register Y available to use for data on AVR - if the method is non-static, then 
	// gcc will use register Y for the this pointer.
	template<int SKIP, bool ADVANCE> static void showRGBInternal(register int nLeds, register uint8_t scale, register const byte *rgbdata) {
		register byte *data = (byte*)rgbdata;
		data_t mask = FastPin<DATA_PIN>::mask();
		register data_ptr_t port = FastPin<DATA_PIN>::port();
		nLeds *= (3 + SKIP);
		register uint8_t *end = data + nLeds; 
		register data_t hi = FastPin<DATA_PIN>::hival();
		register data_t lo = FastPin<DATA_PIN>::loval();;
		*port = lo;

		register uint8_t b;

		b = ADVANCE ? data[SKIP + RGB_BYTE0(RGB_ORDER)] :rgbdata[SKIP + RGB_BYTE0(RGB_ORDER)];
		b = scale8_LEAVING_R1_DIRTY(b, scale);

		register uint8_t c;
		register uint8_t d;
		while(data < end) { 
			for(register byte x=5; x; x--) {
				bitSetLast<7, 4>(port, hi, lo, b);
				b <<= 1;
			}
			delaycycles<1>();
			// Leave an extra 2 clocks for the next byte load
			bitSetLast<7, 0>(port, hi, lo, b);
			// Leave an extra 4 clocks for the scale
			bitSetLast<6, 5>(port, hi, lo, b);
			c = ADVANCE ? data[SKIP + RGB_BYTE1(RGB_ORDER)] :rgbdata[SKIP + RGB_BYTE1(RGB_ORDER)];
			c = scale8_LEAVING_R1_DIRTY(c, scale);
			bitSetLast<5, 1>(port, hi, lo, b);
			
			for(register byte x=5; x; x--) {
				bitSetLast<7, 4>(port, hi, lo, c);
				c <<= 1;
			}
			delaycycles<1>();
			// Leave an extra 2 clocks for the next byte load
			bitSetLast<7, 0>(port, hi, lo, c);
			// Leave an extra 4 clocks for the scale
			bitSetLast<6, 5>(port, hi, lo, c);
			d = ADVANCE ? data[SKIP + RGB_BYTE2(RGB_ORDER)] :rgbdata[SKIP + RGB_BYTE2(RGB_ORDER)];
			d = scale8_LEAVING_R1_DIRTY(d, scale);
			bitSetLast<5, 1>(port, hi, lo, c);
			
			for(register byte x=5; x; x--) {
				bitSetLast<7, 4>(port, hi, lo, d);
				d <<= 1;
			}
			delaycycles<1>();
			// Leave an extra 2 clocks for the next byte load
			bitSetLast<7, 2>(port, hi, lo, d);
			data += (SKIP + 3);
			// Leave an extra 4 clocks for the scale
			bitSetLast<6, 5>(port, hi, lo, d);
			b = ADVANCE ? data[SKIP + RGB_BYTE0(RGB_ORDER)] :rgbdata[SKIP + RGB_BYTE0(RGB_ORDER)];
			b = scale8_LEAVING_R1_DIRTY(b, scale);
			bitSetLast<5, 6>(port, hi, lo, d);
		}
		cleanup_R1();
	}
};

#elif defined(__MK20DX128__)
template <uint8_t DATA_PIN, int T1, int T2, int T3, EOrder RGB_ORDER = RGB, bool FLIP = false, int WAIT_TIME = 50>
class ClocklessController : public CLEDController {
	typedef typename FastPin<DATA_PIN>::port_ptr_t data_ptr_t;
	typedef typename FastPin<DATA_PIN>::port_t data_t;

	data_t mPinMask;
	data_ptr_t mPort;
	CMinWait<WAIT_TIME> mWait;
public:
	virtual void init() { 
		FastPin<DATA_PIN>::setOutput();
		mPinMask = FastPin<DATA_PIN>::mask();
		mPort = FastPin<DATA_PIN>::port();
	}

	virtual void clearLeds(int nLeds) {
		showColor(CRGB(0, 0, 0), nLeds, 0);
	}

	// set all the leds on the controller to a given color
	virtual void showColor(const struct CRGB & data, int nLeds, uint8_t scale = 255) {
		mWait.wait();
		cli();

		showRGBInternal<0, false>(nLeds, scale, (const byte*)&data);

		// Adjust the timer
		long microsTaken = CLKS_TO_MICROS(nLeds * 8 * (T1 + T2 + T3));
		MS_COUNTER += (microsTaken / 1000);
		sei();
		mWait.mark();
	}

	virtual void show(const struct CRGB *rgbdata, int nLeds, uint8_t scale = 255) { 
		mWait.wait();
		cli();

		showRGBInternal<0, true>(nLeds, scale, (const byte*)rgbdata);

		// Adjust the timer
		long microsTaken = CLKS_TO_MICROS(nLeds * 8 * (T1 + T2 + T3));
		MS_COUNTER += (microsTaken / 1000);
		sei();
		mWait.mark();
	}

#ifdef SUPPORT_ARGB
	virtual void show(const struct CARGB *rgbdata, int nLeds, uint8_t scale = 255) { 
		mWait.wait();
		cli();

		showRGBInternal<1, true>(nLeds, scale, (const byte*)rgbdata);

		// Adjust the timer
		long microsTaken = CLKS_TO_MICROS(nLeds * 8 * (T1 + T2 + T3));
		MS_COUNTER += (microsTaken / 1000);
		sei();
		mWait.mark();
	}
#endif

	// This method is made static to force making register Y available to use for data on AVR - if the method is non-static, then 
	// gcc will use register Y for the this pointer.
	template<int SKIP, bool ADVANCE> static void showRGBInternal(register int nLeds, register uint8_t scale, register const byte *rgbdata) {
		register byte *data = (byte*)rgbdata;
		data_t mask = FastPin<DATA_PIN>::mask();
		register data_ptr_t port = FastPin<DATA_PIN>::port();
		nLeds *= (3 + SKIP);
		register uint8_t *end = data + nLeds; 
		register data_t hi = FastPin<DATA_PIN>::hival();
		register data_t lo = FastPin<DATA_PIN>::loval();;
		*port = lo;

		register uint32_t b;
		b = ADVANCE ? data[SKIP + RGB_BYTE0(RGB_ORDER)] :rgbdata[SKIP + RGB_BYTE0(RGB_ORDER)];
		b = scale8(b, scale);
		while(data < end) { 
			// TODO: hand rig asm version of this method.  The timings are based on adjusting/studying GCC compiler ouptut.  This
			// will bite me in the ass at some point, I know it.
			for(register uint32_t i = 7; i > 0; i--) { 
				SET_HI;
				delaycycles<T1 - 3>(); // 3 cycles - 1 store, 1 test, 1 if
				if(b & 0x80) { SET_HI; } else { SET_LO; }
				b <<= 1;
				delaycycles<T2 - 3>(); // 3 cycles, 1 store, 1 store/skip,  1 shift 
				SET_LO;
				delaycycles<T3 - 3>(); // 3 cycles, 1 store, 1 sub, 1 branch backwards
			}
			// extra delay because branch is faster falling through
			delaycycles<1>();

			// 8th bit, interleave loading rest of data
			SET_HI;
			delaycycles<T1 - 3>();
			if(b & 0x80) { SET_HI; } else { SET_LO; }
			delaycycles<T2 - 2>(); // 4 cycles, 2 store, store/skip
			SET_LO;

			b = ADVANCE ? data[SKIP + RGB_BYTE1(RGB_ORDER)] :rgbdata[SKIP + RGB_BYTE1(RGB_ORDER)];
			b = scale8(b, scale);

			delaycycles<T3 - 5>(); // 1 store, 2 load, 1 mul, 1 shift, 

			for(register uint32_t i = 7; i > 0; i--) { 
				SET_HI;
				delaycycles<T1 - 3>(); // 3 cycles - 1 store, 1 test, 1 if
				if(b & 0x80) { SET_HI; } else { SET_LO; }
				b <<= 1;
				delaycycles<T2 - 3>(); // 3 cycles, 1 store, 1 store/skip,  1 shift 
				SET_LO;
				delaycycles<T3 - 3>(); // 3 cycles, 1 store, 1 sub, 1 branch backwards
			}
			// extra delay because branch is faster falling through
			delaycycles<1>();

			// 8th bit, interleave loading rest of data
			SET_HI;
			delaycycles<T1 - 3>();
			if(b & 0x80) { SET_HI; } else { SET_LO; }
			delaycycles<T2 - 2>(); // 4 cycles, 2 store, store/skip
			SET_LO;

			b = ADVANCE ? data[SKIP + RGB_BYTE2(RGB_ORDER)] :rgbdata[SKIP + RGB_BYTE2(RGB_ORDER)];
			b = scale8(b, scale);

			data += 3 + SKIP;
			if((RGB_ORDER & 0070) == 0) {
				delaycycles<T3 - 6>(); // 1 store, 2 load, 1 mul, 1 shift,  1 adds if BRG or GRB
			} else {
				delaycycles<T3 - 5>(); // 1 store, 2 load, 1 mul, 1 shift, 
			}

			for(register uint32_t i = 7; i > 0; i--) { 
				SET_HI;
				delaycycles<T1 - 3>(); // 3 cycles - 1 store, 1 test, 1 if
				if(b & 0x80) { SET_HI; } else { SET_LO; }
				b <<= 1;
				delaycycles<T2 - 3>(); // 3 cycles, 1 store, 1 store/skip,  1 shift 
				SET_LO;
				delaycycles<T3 - 3>(); // 3 cycles, 1 store, 1 sub, 1 branch backwards
			}
			// extra delay because branch is faster falling through
			delaycycles<1>();

			// 8th bit, interleave loading rest of data
			SET_HI;
			delaycycles<T1 - 3>();
			if(b & 0x80) { SET_HI; } else { SET_LO; }
			delaycycles<T2 - 2>(); // 4 cycles, 2 store, store/skip
			SET_LO;

			b = ADVANCE ? data[SKIP + RGB_BYTE0(RGB_ORDER)] :rgbdata[SKIP + RGB_BYTE0(RGB_ORDER)];
			b = scale8(b, scale);

			delaycycles<T3 - 8>(); // 1 store, 2 load (with increment), 1 mul, 1 shift, 1 cmp, 1 branch backwards, 1 movim
		};
	}
};
#elif defined(__SAM3X8E__)

template <uint8_t DATA_PIN, int T1, int T2, int T3, EOrder RGB_ORDER = RGB, bool FLIP = false, int WAIT_TIME = 50>
class ClocklessController : public CLEDController {
	typedef typename FastPinBB<DATA_PIN>::port_ptr_t data_ptr_t;
	typedef typename FastPinBB<DATA_PIN>::port_t data_t;

	data_t mPinMask;
	data_ptr_t mPort;
	CMinWait<WAIT_TIME> mWait;
public:
	virtual void init() { 
		FastPinBB<DATA_PIN>::setOutput();
		mPinMask = FastPinBB<DATA_PIN>::mask();
		mPort = FastPinBB<DATA_PIN>::port();
	}

	virtual void clearLeds(int nLeds) {
		showColor(CRGB(0, 0, 0), nLeds, 0);
	}

	// set all the leds on the controller to a given color
	virtual void showColor(const struct CRGB & data, int nLeds, uint8_t scale = 255) {
		mWait.wait();
		cli();
		SysClockSaver savedClock(T1 + T2 + T3);

		showRGBInternal<0, false>(nLeds, scale, (const byte*)&data);

		// Adjust the timer
		long microsTaken = CLKS_TO_MICROS(nLeds * 8 * (T1 + T2 + T3));
		MS_COUNTER += (microsTaken / 1000);
		savedClock.restore();
		sei();
		mWait.mark();
	}

	virtual void show(const struct CRGB *rgbdata, int nLeds, uint8_t scale = 255) { 
		mWait.wait();
		cli();
		SysClockSaver savedClock(T1 + T2 + T3);
		
		// FastPinBB<DATA_PIN>::hi(); delay(1); FastPinBB<DATA_PIN>::lo();
		showRGBInternal<0, true>(nLeds, scale, (const byte*)rgbdata);

		// Adjust the timer
		long microsTaken = CLKS_TO_MICROS(nLeds * 8 * (T1 + T2 + T3));
		MS_COUNTER += (microsTaken / 1000);
		savedClock.restore();
		sei();
		mWait.mark();
	}

#ifdef SUPPORT_ARGB
	virtual void show(const struct CARGB *rgbdata, int nLeds, uint8_t scale = 255) { 
		mWait.wait();
		cli();
		SysClockSaver savedClock(T1 + T2 + T3);

		showRGBInternal<1, true>(nLeds, scale, (const byte*)rgbdata);

		// Adjust the timer
		long microsTaken = CLKS_TO_MICROS(nLeds * 8 * (T1 + T2 + T3));
		MS_COUNTER += (microsTaken / 1000);
		savedClock.restore();
		sei();
		mWait.mark();
	}
#endif

// I hate using defines for these, should find a better representation at some point
#define _CTRL CTPTR[0]
#define _LOAD CTPTR[1]
#define _VAL CTPTR[2]

	__attribute__((always_inline)) static inline void wait_loop_start(register volatile uint32_t *CTPTR) {
		__asm__ __volatile__ (
			"L_%=: ldr.w r8, [%0]\n"
			"      tst.w r8, #65536\n"
			"		beq.n L_%=\n"
			: /* no outputs */
			: "r" (CTPTR)
			: "r8"
			);
	}

	template<int MARK> __attribute__((always_inline)) static inline void wait_loop_mark(register volatile uint32_t *CTPTR) {
		__asm__ __volatile__ (
			"L_%=: ldr.w r8, [%0, #8]\n"
			"      cmp.w r8, %1\n"
			"		bhi.n L_%=\n"
			: /* no outputs */
			: "r" (CTPTR), "I" (MARK)
			: "r8"
			);
	}

	__attribute__((always_inline)) static inline void mark_port(register data_ptr_t port, register int val) {
		__asm__ __volatile__ (
			"	str.w %0, [%1]\n"
			: /* no outputs */
			: "r" (val), "r" (port)
			);
	}
#define AT_BIT_START(X) wait_loop_start(CTPTR); X;
#define AT_MARK(X) wait_loop_mark<T1_MARK>(CTPTR); { X; }
#define AT_END(X) wait_loop_mark<T2_MARK>(CTPTR); { X; }

// #define AT_BIT_START(X) while(!(_CTRL & SysTick_CTRL_COUNTFLAG_Msk)); { X; }
// #define AT_MARK(X) while(_VAL > T1_MARK); { X; }
// #define AT_END(X) while(_VAL > T2_MARK); { X; }

//#define AT_MARK(X) delayclocks_until<T1_MARK>(_VAL); X; 
//#define AT_END(X) delayclocks_until<T2_MARK>(_VAL); X;

#define TOTAL (T1 + T2 + T3)

#define T1_MARK (TOTAL - T1)
#define T2_MARK (T1_MARK - T2)
	template<int MARK> __attribute__((always_inline)) static inline void delayclocks_until(register byte b) { 
		__asm__ __volatile__ (
			"	   sub %0, %0, %1\n"
			"L_%=: subs %0, %0, #2\n"
			"      bcs.n L_%=\n"
			: /* no outputs */
			: "r" (b), "I" (MARK)
			: /* no clobbers */
			);

	}

#define FORCE_REFERENCE(var)  asm volatile( "" : : "r" (var) )

	// This method is made static to force making register Y available to use for data on AVR - if the method is non-static, then 
	// gcc will use register Y for the this pointer.
	template<int SKIP, bool ADVANCE> static void showRGBInternal(register int nLeds, register uint8_t scale, register const byte *rgbdata) {
		register data_ptr_t port asm("r7") = FastPinBB<DATA_PIN>::port(); FORCE_REFERENCE(port);
		register byte *data = (byte*)rgbdata;
		register uint8_t *end = data + (nLeds*3 + SKIP); 
		
		register volatile uint32_t *CTPTR asm("r6")= &SysTick->CTRL; FORCE_REFERENCE(CTPTR);
		
		*port = 0;

		register uint32_t b;
		b = ADVANCE ? data[SKIP + RGB_BYTE0(RGB_ORDER)] :rgbdata[SKIP + RGB_BYTE0(RGB_ORDER)];
		b = scale8(b, scale);

		// Setup and start the clock
		_LOAD = TOTAL;
		_VAL = 0;
		_CTRL |= SysTick_CTRL_CLKSOURCE_Msk;
		_CTRL |= SysTick_CTRL_ENABLE_Msk;

		// read to clear the loop flag
		_CTRL;

		while(data < end) { 
			for(register uint32_t i = 7; i > 0; i--) { 
				AT_BIT_START(*port = 1);
				if(b& 0x80) {} else { AT_MARK(*port = 0); }
				AT_END(*port = 0);
				b <<= 1;
			}

			AT_BIT_START(*port = 1);
			if(b& 0x80) {} else { AT_MARK(*port = 0); }
			AT_END(*port = 0);

			b = ADVANCE ? data[SKIP + RGB_BYTE1(RGB_ORDER)] : rgbdata[SKIP + RGB_BYTE1(RGB_ORDER)];
			b = scale8(b, scale);

			for(register uint32_t i = 7; i > 0; i--) { 
				AT_BIT_START(*port = 1);
				if(b& 0x80) {} else { AT_MARK(*port = 0); }
				AT_END(*port = 0);
				b <<= 1;
			}

			AT_BIT_START(*port = 1);
			if(b& 0x80) {} else { AT_MARK(*port = 0); }
			AT_END(*port = 0);

			b = ADVANCE ? data[SKIP + RGB_BYTE2(RGB_ORDER)] : rgbdata[SKIP + RGB_BYTE2(RGB_ORDER)];
			b = scale8(b, scale);
			data += (3 + SKIP);
 
			for(register uint32_t i = 7; i > 0; i--) { 
				AT_BIT_START(*port = 1);
				if(b& 0x80) {} else { AT_MARK(*port = 0); }
				AT_END(*port = 0);
				b <<= 1;
			}

			AT_BIT_START(*port = 1);
			if(b& 0x80) {} else { AT_MARK(*port = 0); }
			AT_END(*port = 0);

			b = ADVANCE ? data[SKIP + RGB_BYTE0(RGB_ORDER)] : rgbdata[SKIP + RGB_BYTE0(RGB_ORDER)];
			b = scale8(b, scale);
		};
	}
};

#endif

#endif