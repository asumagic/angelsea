// SPDX-License-Identifier: BSD-2-Clause

// We only test primitive typedefs because only those are supported:
// https://www.angelcode.com/angelscript/sdk/docs/manual/doc_global_typedef.html

typedef float f32;

void main()
{
    f32 v = 3.141;
    print('' + f32(v));
}
