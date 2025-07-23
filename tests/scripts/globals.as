// SPDX-License-Identifier: BSD-2-Clause

int8 g8;
int16 g16;
int g32;
int64 g64;

void assign_read()
{
    g8 = 123;
    g16 = 123;
    g32 = 123;
    g64 = 123;

    print(g8);
    print(g16);
    print(g32);
    print(g64);
}
