// SPDX-License-Identifier: BSD-2-Clause

// Basic function stuff - useful for testing source-level debugging.

void main()
{
    print(/*'' + */calc(10, 100));
}

int calc(int64 a, int b)
{
    a *= 10;
    int v = a;
    v *= b;
    return v;
}
