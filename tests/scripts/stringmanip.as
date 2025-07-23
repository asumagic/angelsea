// SPDX-License-Identifier: BSD-2-Clause

void string_ref()
{
    string local_string = "hello";
    print(local_string);
}

void string_concat()
{
    string a = "hello ", b = "world";
    print(a + b);
}

void string_concat2()
{
    string a = "hello ";
    a += "world";
    print(a);
}

void string_manylocals_concat()
{
    string a = "h", b = "e", c = "l", d = "l", e = "o", f = " ";
    print(a + b + c + d + e + f + "world");
}

void takes_string_reference(const string &in a, const string &in b, const string &in c)
{
    print(a + b + c);
}

void string_function_reference()
{
    takes_string_reference("hello", " world", "!");
}

void takes_string_value(string a, string b, string c)
{
    print(a + b + c);
}

void string_function_value()
{
    takes_string_value("hello", " world", "!");
}
