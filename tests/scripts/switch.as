// SPDX-License-Identifier: BSD-2-Clause

void switch_test(int i)
{
    switch (i)
    {
    case 0:
    {
        print("Hmm... Mmmmmm... Hmm... Mmm...");
        break;
    }

    case 1:
    {
        print("Pardon me, I was absorbed in thought.");
        break;
    }

    case 2:
    {
        print("I am Siegward of Catarina.");
        // Fallthrough
    }

    case 3:
    {
        print("To be honest, I'm in a bit of a pickle.");
        break;
    }

    case 4:
    {
        print("Have you ever walked near a white birch, only to be struck by a great arrow?");
        break;
    }

    case 5:
    {
        print("Well, if I'm not mistaken, they come from this tower.");
        break;
    }

    default:
    {
        print("Whoever it is, I'm sure I can talk some sense into them.");
    }
    }
}

void main()
{
    switch_test(2);
    switch_test(30);
}
