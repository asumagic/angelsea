// SPDX-License-Identifier: BSD-2-Clause

int fib(int n)
{
	if (n < 2)
	{
		return n;
	}

	return fib(n-1) + fib(n-2);
}
