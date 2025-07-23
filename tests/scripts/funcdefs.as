// SPDX-License-Identifier: BSD-2-Clause

funcdef void SOME_FUNCDEF(const string &in);

void print_proxy_test(const string &in str)
{
    print(str);
}

void test_system()
{
    SOME_FUNCDEF@ printer = @print;
    printer("hello");
}

void test_script()
{
    SOME_FUNCDEF@ printer = @print_proxy_test;
    printer("hello");
}
