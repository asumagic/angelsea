void main()
{
    // array is a template type. Test having multiple types to make sure that, well, they work, but also importantly
    // that the multiple instanciations of the same template type does not cause issues.

    int[] ints;
    string[] strings;

    ints.insertLast(123);
    strings.insertLast("hi");

    print(ints[0]);
    print(strings[0]);
}
