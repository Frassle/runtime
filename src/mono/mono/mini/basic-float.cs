using System;
using System.Reflection;

/*
 * Regression tests for the mono JIT.
 *
 * Each test needs to be of the form:
 *
 * static int test_<result>_<name> ();
 *
 * where <result> is an integer (the value that needs to be returned by
 * the method to make it pass.
 * <name> is a user-displayed name used to identify the test.
 *
 * The tests can be driven in two ways:
 * *) running the program directly: Main() uses reflection to find and invoke
 * 	the test methods (this is useful mostly to check that the tests are correct)
 * *) with the --regression switch of the jit (this is the preferred way since
 * 	all the tests will be run with optimizations on and off)
 *
 * The reflection logic could be moved to a .dll since we need at least another
 * regression test file written in IL code to have better control on how
 * the IL code looks.
 */

class Tests {

	static int Main () {
		return TestDriver.RunTests (typeof (Tests));
	}
	
	static int test_0_beq () {
		double a = 2.0;
		if (a != 2.0)
			return 1;
		return 0;
	}

	static int test_0_bne_un () {
		double a = 2.0;
		if (a == 1.0)
			return 1;
		return 0;
	}

	static int test_0_conv_r8 () {
		double a = 2;
		if (a != 2.0)
			return 1;
		return 0;
	}

	static int test_0_conv_i4 () {
		double a = 2.0;
		int b = (int)a;
		if (b != 2)
			return 1;
		return 0;
	}

	static int test_5_add () {
		double a = 2.0;
		double b = 3.0;		
		return (int)(a + b);
	}

	static int test_5_sub () {
		double a = 8.0;
		double b = 3.0;		
		return (int)(a - b);
	}	

	static int test_24_mul () {
		double a = 8.0;
		double b = 3.0;		
		return (int)(a * b);
	}	

	static int test_4_div () {
		double a = 8.0;
		double b = 2.0;		
		return (int)(a / b);
	}	

	static int test_2_rem () {
		double a = 8.0;
		double b = 3.0;		
		return (int)(a % b);
	}	

	static int test_2_neg () {
		double a = -2.0;		
		return (int)(-a);
	}
	
	static int test_46_float_spill () {
		// we overflow the FP stack
		double a = 1;
		double b = 2;
		double c = 3;
		double d = 4;
		double e = 5;
		double f = 6;
		double g = 7;
		double h = 8;
		double i = 9;

		return (int)(1.0 + (a + (b + (c + (d + (e + (f + (g + (h + i)))))))));
	}

	static int test_4_long_cast () {
		long a = 1000;
		double d = (double)a;
		long b = (long)d;
		if (b != 1000)
			return 0;
		return 4;
	}


	static int test_15_float_cmp () {
		double a = 2.0;
		double b = 1.0;
		int result = 0;
		bool val;
		
		val = a == a;
		if (!val)
			return result;
		result++;

		val = a < a;
		if (val)
			return result;
		result++;

		val = a > a;
		if (val)
			return result;
		result++;

		val = a <= a;
		if (!val)
			return result;
		result++;

		val = a >= a;
		if (!val)
			return result;
		result++;

		val = b == a;
		if (val)
			return result;
		result++;

		val = b < a;
		if (!val)
			return result;
		result++;

		val = b > a;
		if (val)
			return result;
		result++;

		val = b <= a;
		if (!val)
			return result;
		result++;

		val = b >= a;
		if (val)
			return result;
		result++;

		val = a == b;
		if (val)
			return result;
		result++;

		val = a < b;
		if (val)
			return result;
		result++;

		val = a > b;
		if (!val)
			return result;
		result++;

		val = a <= b;
		if (val)
			return result;
		result++;

		val = a >= b;
		if (!val)
			return result;
		result++;

		return result;
	}

	static int test_15_float_cmp_un () {
		double a = Double.NaN;
		double b = 1.0;
		int result = 0;
		bool val;
		
		val = a == a;
		if (val)
			return result;
		result++;

		val = a < a;
		if (val)
			return result;
		result++;

		val = a > a;
		if (val)
			return result;
		result++;

		val = a <= a;
		if (val)
			return result;
		result++;

		val = a >= a;
		if (val)
			return result;
		result++;

		val = b == a;
		if (val)
			return result;
		result++;

		val = b < a;
		if (val)
			return result;
		result++;

		val = b > a;
		if (val)
			return result;
		result++;

		val = b <= a;
		if (val)
			return result;
		result++;

		val = b >= a;
		if (val)
			return result;
		result++;

		val = a == b;
		if (val)
			return result;
		result++;

		val = a < b;
		if (val)
			return result;
		result++;

		val = a > b;
		if (val)
			return result;
		result++;

		val = a <= b;
		if (val)
			return result;
		result++;

		val = a >= b;
		if (val)
			return result;
		result++;

		return result;
	}

	static int test_15_float_branch () {
		double a = 2.0;
		double b = 1.0;
		int result = 0;
		
		if (!(a == a))
			return result;
		result++;

		if (a < a)
			return result;
		result++;

		if (a > a)
			return result;
		result++;

		if (!(a <= a))
			return result;
		result++;

		if (!(a >= a))
			return result;
		result++;

		if (b == a)
			return result;
		result++;

		if (!(b < a))
			return result;
		result++;

		if (b > a)
			return result;
		result++;

		if (!(b <= a))
			return result;
		result++;

		if (b >= a)
			return result;
		result++;

		if (a == b)
			return result;
		result++;

		if (a < b)
			return result;
		result++;

		if (!(a > b))
			return result;
		result++;

		if (a <= b)
			return result;
		result++;

		if (!(a >= b))
			return result;
		result++;

		return result;
	}

	static int test_15_float_branch_un () {
		double a = Double.NaN;
		double b = 1.0;
		int result = 0;
		
		if (a == a)
			return result;
		result++;

		if (a < a)
			return result;
		result++;

		if (a > a)
			return result;
		result++;

		if (a <= a)
			return result;
		result++;

		if (a >= a)
			return result;
		result++;

		if (b == a)
			return result;
		result++;

		if (b < a)
			return result;
		result++;

		if (b > a)
			return result;
		result++;

		if (b <= a)
			return result;
		result++;

		if (b >= a)
			return result;
		result++;

		if (a == b)
			return result;
		result++;

		if (a < b)
			return result;
		result++;

		if (a > b)
			return result;
		result++;

		if (a <= b)
			return result;
		result++;

		if (a >= b)
			return result;
		result++;

		return result;
	}

}

