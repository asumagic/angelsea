// SPDX-License-Identifier: BSD-2-Clause

class A
{
    void foo() final
    {
        print("hello");
    }
};

class B : A {};

void main()
{
    B().foo();
}
