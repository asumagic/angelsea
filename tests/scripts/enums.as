// SPDX-License-Identifier: BSD-2-Clause

namespace MyEnum
{
enum MyEnum
{
    A = 10,
    B,
    C
};
}

void main()
{
    MyEnum::MyEnum e = MyEnum::B;
    print(e);
}
