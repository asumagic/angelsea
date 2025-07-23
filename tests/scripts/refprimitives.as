// SPDX-License-Identifier: BSD-2-Clause

void calc(int &in a, int &out b)
{
    b = a;
}

void main()
{
    int foo = 123;
    calc(10, foo);
    print(foo);
}
