// SPDX-License-Identifier: BSD-2-Clause

void bf(const string &in source)
{
    uint8[] memory(30000);

    uint32 tape_pointer = 0;
    uint32 instruction_pointer = 0;

    const uint32 source_size = source.length();

    while (true)
    {
        if (instruction_pointer == source_size)
        {
            break;
        }

        switch (source[instruction_pointer])
        {
        case 0x2B: // +
        {
            ++memory[tape_pointer];
            break;
        }

        case 0x2D: // -
        {
            --memory[tape_pointer];
            break;
        }

        case 0x2E: // .
        {
            putchar(memory[tape_pointer]);
            break;
        }

        case 0x5B: // [
        {
            if (memory[tape_pointer] != 0)
            {
                break;
            }

            int depth = 0;
            while (true)
            {
                const auto ins = source[instruction_pointer];
                if (ins == 0x5B) { ++depth; }
                else if (ins == 0x5D) { --depth; }

                if (depth == 0)
                {
                    break;
                }

                ++instruction_pointer;
            }

            break;
        }

        case 0x5D: // ]
        {
            if (memory[tape_pointer] == 0)
            {
                break;
            }

            int depth = 0;
            while (true)
            {
                const auto ins = source[instruction_pointer];
                if (ins == 0x5B) { ++depth; }
                else if (ins == 0x5D) { --depth; }

                if (depth == 0)
                {
                    break;
                }

                --instruction_pointer;
            }

            break;
        }

        case 0x3C: // <
        {
            --tape_pointer;
            break;
        }

        case 0x3E: // >
        {
            ++tape_pointer;
            break;
        }
        }

        ++instruction_pointer;
    }
}

void main()
{
    bf("+[-[<<[+[--->]-[<<<]]]>>>-]>-.---.>..>.<<<<-.<+.>>>>>.>.<<.<-.");
}
