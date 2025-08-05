// SPDX-License-Identifier: BSD-2-Clause

int fib(int n)
{
	if (n < 2)
	{
		return n;
	}

	return fib(n-1) + fib(n-2);
}

/* it will, obviously, overflow very hard very often */
uint64 fib_iterative(uint64 n)
{
	uint64 last = 1;
	uint64 prev = 0;

	for (uint64 i = 1; i < n; ++i)
	{
		uint64 next = prev + last;
		prev = last;
		last = next;
	}

	return last;
}