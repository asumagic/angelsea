class Foo {
    Foo() {}
    Foo(int value) { m_value = value; }

    int m_value;
};

void main()
{
    int[] ints = {123, 456, 789};
    string[] strings = {"hello", "hi"};
    Foo[] foos = {Foo(123)};

    for (uint i = 0; i < ints.length(); ++i)
    {
        print(ints[i]);
    }

    for (uint i = 0; i < strings.length(); ++i)
    {
        print(strings[i]);
    }

    for (uint i = 0; i < foos.length(); ++i)
    {
        print(foos[i].m_value);
    }
}
