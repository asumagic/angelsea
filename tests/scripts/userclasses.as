// SPDX-License-Identifier: BSD-2-Clause

class Foo
{
    // Make sure the constructeur is called correctly, and that stuff is correctly executed inside
    Foo()
    {
        print("hello");
    }

    // Test virtual function calls and parameters inside of methods
    void foo(int a, int b, int c)
    {
        print(a);
        print(b);
        print(c);
    }

    // Somewhat random field organization and writes to test for potential writes overlapping with other members.
    void use_field()
    {
        m2 = 20;
        m3 = 30;
        m5 = 50;
        m7 = 70;
        m6 = 60;
        m8 = 80;
        m10 = 100;
        m9 = 90;
        m4 = 40;
        m1 = 10;

        print(m1);
        print(m2);
        print(m3);
        print(m4);
        print(m5);
        print(m6);
        print(m7);
        print(m8);
        print(m9);
        print(m10);
    }

    string return_string_field()
    {
        str_field = "world";
        return str_field;
    }

    int8 m1, m2;
    int16 m3;
    int32 m4;
    int64 m5;
    int16 m6;
    int8 m7;
    int16 m8;
    int8 m9;
    int64 m10;

    string str_field;
};

void test()
{
    Foo f;
}

void method_test()
{
    Foo f;
    f.foo(123, 456, 789);
}

void method_field_test()
{
    Foo f;
    f.use_field();
}

void return_field_test()
{
    Foo f;
    print(f.return_string_field());
}

void handle_test()
{
    Foo f1;
    Foo@ f2 = @f1;
    f2.foo(123, 456, 789);
}

void take_by_value(Foo foo)
{
    foo.use_field();
}

void pass_by_value_test()
{
    Foo f;
    take_by_value(f);
}

void by_ref(Foo &in f) {}

void null_test()
{
    Foo @f = null;
    by_ref(f);
}

void is_test()
{
    Foo f1, f2;
    Foo@ f1_alias = @f1;

    if (f1 is f1_alias) { print("ok"); }
    if (f1 is f2) { print("not ok"); }
    if (f1 !is f2) { print("ok"); }
    if (f1_alias is f1_alias) { print("ok"); }

    Foo@ fnull = null;
    if (fnull is null) { print("ok"); }
    if (fnull !is null) { print("not ok"); }
}