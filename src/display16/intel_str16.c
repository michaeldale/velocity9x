/*
 * Project-local replacements for the four C library string functions the
 * Intel 16-bit units used.
 *
 * They exist for a link reason, not a behavioural one. strcmp, strcpy, strlen
 * and strcat live in clibc.lib, whose objects declare segment _TEXT, and there
 * is no linker mechanism to place a second copy in the I9XXCODE segment the
 * Intel units are compiled into. A near call to them from I9XXCODE would be a
 * wild jump (docs\plans\intel-gma950-phase5.md). Measured across all 31
 * objects, these four were referenced only by intel_exec16.c and
 * intel_boot16.c - both moved - so defining them here removes the library
 * objects from the link entirely.
 *
 * This unit is itself moved, so the calls are near and cost nothing extra.
 * Semantics match the C library exactly for the uses in this family: NUL
 * terminated, no bounds checking, destination assumed large enough - the
 * callers already size their buffers, and changing that is a separate job.
 */
#include "velocity9x/intel16.h"

unsigned short v9x_intel_str_length(const char *text)
{
    unsigned short length = 0u;

    if (text == 0) {
        return 0u;
    }
    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

/*
 * Returns true/false rather than strcmp's three-way sign, because every call
 * site in this family tested only for equality.
 */
unsigned short v9x_intel_str_equal(const char *left, const char *right)
{
    unsigned short index = 0u;

    if (left == 0 || right == 0) {
        return (unsigned short)(left == right);
    }
    while (left[index] == right[index]) {
        if (left[index] == '\0') {
            return 1u;
        }
        ++index;
    }

    return 0u;
}

void v9x_intel_str_copy(char *destination, const char *source)
{
    unsigned short index = 0u;

    if (destination == 0 || source == 0) {
        return;
    }
    while (source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

void v9x_intel_str_append(char *destination, const char *source)
{
    if (destination == 0 || source == 0) {
        return;
    }
    v9x_intel_str_copy(destination + v9x_intel_str_length(destination), source);
}
