/*++

Copyright (c) 2015 Minoca Corp. All Rights Reserved

Module Name:

    testcryp.c

Abstract:

    This module tests the cryptographic library.

Author:

    Evan Green 14-Jan-2015

Environment:

    Build

--*/

//
// ------------------------------------------------------------------- Includes
//

#define RTL_API
#define CRYPTO_API

#include <minoca/types.h>
#include <minoca/status.h>
#include <minoca/rtl.h>
#include <minoca/crypto.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

//
// ---------------------------------------------------------------- Definitions
//

//
// ------------------------------------------------------ Data Type Definitions
//

//
// ----------------------------------------------- Internal Function Prototypes
//

ULONG
TestSha1 (
    VOID
    );

ULONG
TestSha256 (
    VOID
    );

ULONG
TestSha512 (
    VOID
    );

ULONG
TestMd5 (
    VOID
    );

ULONG
TestRsa (
    VOID
    );

//
// -------------------------------------------------------------------- Globals
//

ULONG TestCrypHashDataSizes[] = {
    1,
    10,
    55,
    56,
    63,
    64,
    65,
    119,
    120,
    127,
    128,
    512
};

PCHAR TestCrypData =
    "0123456789ABCDEFabcdefghijklmnop0123456789ABCDEFabcdefghijklmnop"
    "0123456789ABCDEFabcdefghijklmnop0123456789ABCDEFabcdefghijklmnop"
    "0123456789ABCDEFabcdefghijklmnop0123456789ABCDEFabcdefghijklmnop"
    "0123456789ABCDEFabcdefghijklmnop0123456789ABCDEFabcdefghijklmnop"
    "0123456789ABCDEFabcdefghijklmnop0123456789ABCDEFabcdefghijklmnop"
    "0123456789ABCDEFabcdefghijklmnop0123456789ABCDEFabcdefghijklmnop"
    "0123456789ABCDEFabcdefghijklmnop0123456789ABCDEFabcdefghijklmnop"
    "0123456789ABCDEFabcdefghijklmnop0123456789ABCDEFabcdefghijklmnop";

UCHAR TestCrypSha1Answers[][SHA1_HASH_SIZE] = {
    {0xB6, 0x58, 0x9F, 0xC6, 0xAB, 0x0D, 0xC8, 0x2C, 0xF1, 0x20,
     0x99, 0xD1, 0xC2, 0xD4, 0x0A, 0xB9, 0x94, 0xE8, 0x41, 0x0C},
    {0x87, 0xAC, 0xEC, 0x17, 0xCD, 0x9D, 0xCD, 0x20, 0xA7, 0x16,
     0xCC, 0x2C, 0xF6, 0x74, 0x17, 0xB7, 0x1C, 0x8A, 0x70, 0x16},
    {0x17, 0x1F, 0x85, 0x89, 0xB8, 0x2A, 0xD6, 0xC3, 0xBF, 0xE5,
     0x4E, 0x93, 0x69, 0x05, 0xEC, 0x67, 0x96, 0x40, 0xE2, 0x19},
    {0x50, 0x0D, 0xC2, 0xC0, 0x9E, 0xE1, 0x5C, 0xBD, 0x30, 0x18,
     0x9B, 0x8C, 0x29, 0x61, 0x09, 0x01, 0xCE, 0xAF, 0xC9, 0x15},
    {0xB5, 0xAC, 0x04, 0x3F, 0xDE, 0xB7, 0x86, 0x3E, 0xF5, 0xD4,
     0x7E, 0xB2, 0x7D, 0xB3, 0x4C, 0xB6, 0xC0, 0x8E, 0x2D, 0x56},
    {0x5E, 0xA3, 0x23, 0x1D, 0x2A, 0xB8, 0x03, 0x15, 0x8D, 0x6F,
     0xB9, 0x49, 0x8D, 0xDF, 0xD1, 0xAD, 0x3E, 0x62, 0x37, 0x90},
    {0x06, 0xEC, 0x6C, 0x7F, 0x32, 0x58, 0x03, 0xDA, 0x9A, 0x5E,
     0xFF, 0x20, 0x76, 0xBB, 0x66, 0xCC, 0x84, 0xF6, 0x30, 0xD5},
    {0x98, 0xA1, 0xF6, 0x9A, 0xBE, 0x25, 0x02, 0xD7, 0x2F, 0x5C,
     0xA4, 0xDB, 0xC8, 0x04, 0x2A, 0x79, 0x00, 0x5C, 0x48, 0x49},
    {0xFA, 0x36, 0x75, 0x7C, 0x69, 0xCF, 0xB3, 0x4C, 0xAF, 0x14,
     0x5A, 0x8A, 0xBF, 0x98, 0x85, 0xE2, 0x56, 0x69, 0xAD, 0x9D},
    {0x3B, 0xAF, 0x31, 0x8F, 0x31, 0xD6, 0x9D, 0x2E, 0x80, 0x73,
     0x9F, 0x84, 0xCE, 0x0C, 0x96, 0xEB, 0x84, 0x78, 0xD1, 0xFB},
    {0xCB, 0x58, 0x24, 0x9E, 0xB3, 0x1E, 0x0A, 0xD0, 0x4C, 0xDF,
     0x34, 0x5F, 0xF9, 0x26, 0x36, 0x49, 0xA2, 0x5C, 0x5F, 0x35},
    {0x07, 0x49, 0x0F, 0x11, 0x99, 0xCE, 0xB4, 0x7C, 0x9A, 0x0C,
     0xA2, 0xF6, 0xD8, 0x5B, 0xCB, 0x5E, 0xA0, 0x91, 0xDC, 0xEF}
};

UCHAR TestCrypSha256Answers[][SHA256_HASH_SIZE] = {
    {0x5F, 0xEC, 0xEB, 0x66, 0xFF, 0xC8, 0x6F, 0x38,
     0xD9, 0x52, 0x78, 0x6C, 0x6D, 0x69, 0x6C, 0x79,
     0xC2, 0xDB, 0xC2, 0x39, 0xDD, 0x4E, 0x91, 0xB4,
     0x67, 0x29, 0xD7, 0x3A, 0x27, 0xFB, 0x57, 0xE9},
    {0x84, 0xD8, 0x98, 0x77, 0xF0, 0xD4, 0x04, 0x1E,
     0xFB, 0x6B, 0xF9, 0x1A, 0x16, 0xF0, 0x24, 0x8F,
     0x2F, 0xD5, 0x73, 0xE6, 0xAF, 0x05, 0xC1, 0x9F,
     0x96, 0xBE, 0xDB, 0x9F, 0x88, 0x2F, 0x78, 0x82},
    {0x22, 0xE8, 0x93, 0x99, 0x57, 0x2C, 0x03, 0x07,
     0x06, 0x8C, 0xFF, 0x62, 0xD2, 0x2C, 0x0F, 0xAD,
     0xBA, 0xDD, 0xCC, 0x5C, 0x5E, 0x04, 0x74, 0x99,
     0x22, 0x8F, 0x09, 0x0C, 0x80, 0xA7, 0x38, 0x40},
    {0x0B, 0xC8, 0x79, 0x8C, 0x00, 0x20, 0xDA, 0x83,
     0xE2, 0x37, 0x58, 0x42, 0x24, 0x15, 0x47, 0x45,
     0xDD, 0xCA, 0x16, 0xDF, 0xC9, 0xF2, 0xD1, 0xBA,
     0x32, 0x85, 0x38, 0xE1, 0xAB, 0xA2, 0xE2, 0xF2},
    {0x59, 0xE1, 0x5C, 0x10, 0x56, 0x7B, 0xF0, 0x34,
     0x01, 0xCE, 0x1E, 0xFF, 0x24, 0x34, 0x51, 0x7A,
     0xA3, 0xC3, 0x03, 0xD2, 0xC6, 0x9C, 0x2E, 0x7A,
     0xC7, 0xC9, 0x99, 0xE0, 0x76, 0x58, 0x8D, 0xC8},
    {0x09, 0xBC, 0x00, 0xCF, 0x9B, 0xAA, 0xDD, 0x67,
     0xB4, 0x59, 0x9A, 0x75, 0xE8, 0xE0, 0x7F, 0x8D,
     0x3D, 0xC5, 0x33, 0xE6, 0x11, 0xEB, 0xDA, 0x44,
     0x88, 0xC9, 0x7A, 0x90, 0xF3, 0x95, 0x5F, 0xC1},
    {0xFA, 0xC4, 0xE1, 0xD4, 0x80, 0x2B, 0xB7, 0x27,
     0xFA, 0xD5, 0xBE, 0xD7, 0x05, 0x33, 0x0A, 0x91,
     0x13, 0xF9, 0xC3, 0xA5, 0x59, 0xB1, 0xD5, 0x08,
     0x23, 0xAA, 0x9B, 0xDC, 0x37, 0x6F, 0x75, 0x9C},
    {0xFF, 0xB5, 0xDB, 0x30, 0x25, 0x28, 0x4A, 0x59,
     0x32, 0xBF, 0x34, 0xD2, 0x69, 0x47, 0x6B, 0x5E,
     0xFE, 0xD8, 0x61, 0x5E, 0xB6, 0x00, 0xEA, 0xAE,
     0x74, 0xB5, 0x26, 0xB1, 0xD5, 0xCD, 0x08, 0x74},
    {0x05, 0xBC, 0x89, 0xA4, 0x4F, 0xC2, 0xF6, 0xF3,
     0x76, 0x23, 0x3E, 0x89, 0x14, 0xC9, 0x82, 0xE8,
     0x77, 0x2C, 0x9D, 0xAE, 0x58, 0xB4, 0x6B, 0x97,
     0xAF, 0x50, 0x17, 0xA4, 0x9C, 0x30, 0xDA, 0xBA},
    {0xC9, 0x8C, 0xD4, 0xDA, 0x5A, 0x3C, 0x40, 0x07,
     0x5F, 0x3C, 0xC6, 0xC3, 0xCF, 0xB7, 0xAB, 0x47,
     0x26, 0x4F, 0x36, 0xC4, 0x5A, 0xE8, 0x9F, 0x94,
     0x20, 0x2C, 0xDF, 0x98, 0x33, 0xD2, 0x7E, 0x6F},
    {0xE5, 0x91, 0x1F, 0x64, 0x41, 0xED, 0x4A, 0xFB,
     0x96, 0xCD, 0xBC, 0x74, 0x8A, 0xA4, 0x4F, 0x4B,
     0x4F, 0x13, 0x41, 0xE6, 0x38, 0xF2, 0x58, 0x24,
     0x47, 0xF0, 0x47, 0x14, 0xED, 0x28, 0xB2, 0x49},
    {0xB5, 0x8E, 0xAC, 0x1C, 0xFD, 0x82, 0xC0, 0x64,
     0xE6, 0x85, 0x08, 0xCD, 0x79, 0x1E, 0x6E, 0xFB,
     0x82, 0x99, 0x44, 0x89, 0x88, 0x52, 0x2E, 0x54,
     0x69, 0x40, 0xFD, 0xE6, 0x62, 0x61, 0xFA, 0x22},
};

UCHAR TestCrypSha512Answers[][SHA512_HASH_SIZE] = {
    {0x31, 0xBC, 0xA0, 0x20, 0x94, 0xEB, 0x78, 0x12,
     0x6A, 0x51, 0x7B, 0x20, 0x6A, 0x88, 0xC7, 0x3C,
     0xFA, 0x9E, 0xC6, 0xF7, 0x04, 0xC7, 0x03, 0x0D,
     0x18, 0x21, 0x2C, 0xAC, 0xE8, 0x20, 0xF0, 0x25,
     0xF0, 0x0B, 0xF0, 0xEA, 0x68, 0xDB, 0xF3, 0xF3,
     0xA5, 0x43, 0x6C, 0xA6, 0x3B, 0x53, 0xBF, 0x7B,
     0xF8, 0x0A, 0xD8, 0xD5, 0xDE, 0x7D, 0x83, 0x59,
     0xD0, 0xB7, 0xFE, 0xD9, 0xDB, 0xC3, 0xAB, 0x99},
    {0xBB, 0x96, 0xC2, 0xFC, 0x40, 0xD2, 0xD5, 0x46,
     0x17, 0xD6, 0xF2, 0x76, 0xFE, 0xBE, 0x57, 0x1F,
     0x62, 0x3A, 0x8D, 0xAD, 0xF0, 0xB7, 0x34, 0x85,
     0x52, 0x99, 0xB0, 0xE1, 0x07, 0xFD, 0xA3, 0x2C,
     0xF6, 0xB6, 0x9F, 0x2D, 0xA3, 0x2B, 0x36, 0x44,
     0x5D, 0x73, 0x69, 0x0B, 0x93, 0xCB, 0xD0, 0xF7,
     0xBF, 0xC2, 0x0E, 0x0F, 0x7F, 0x28, 0x55, 0x3D,
     0x2A, 0x44, 0x28, 0xF2, 0x3B, 0x71, 0x6E, 0x90},
    {0x6C, 0xD2, 0x51, 0x8B, 0xAC, 0x0A, 0xF2, 0x0D,
     0x20, 0x97, 0xF3, 0x8C, 0x93, 0xF0, 0xDD, 0x17,
     0x8B, 0x25, 0xDA, 0x99, 0xA0, 0xD9, 0x67, 0x73,
     0xBE, 0x0C, 0x5B, 0xB4, 0x7E, 0xC9, 0x38, 0x25,
     0x22, 0x59, 0x13, 0xEB, 0xEA, 0x81, 0xD2, 0x82,
     0xC9, 0x15, 0x79, 0xC8, 0x85, 0x49, 0xD8, 0x54,
     0xD0, 0xBC, 0x75, 0xC1, 0x4B, 0x1D, 0x14, 0xDA,
     0x86, 0xF6, 0x47, 0x6E, 0x75, 0x73, 0x64, 0x07},
    {0x57, 0x6E, 0x66, 0xEA, 0xEE, 0x4B, 0x9A, 0xB2,
     0x13, 0x6D, 0xE7, 0x24, 0x95, 0xD8, 0xFA, 0x95,
     0x6A, 0x3A, 0x10, 0x23, 0xE7, 0x1F, 0x29, 0xED,
     0x33, 0x0C, 0x58, 0xF7, 0x77, 0x26, 0xCA, 0x7D,
     0x0F, 0x15, 0x89, 0xC8, 0x45, 0x70, 0x70, 0x42,
     0xE6, 0x56, 0xEF, 0x46, 0xE6, 0x49, 0x50, 0x36,
     0x47, 0x78, 0x4B, 0xD1, 0x3A, 0xF6, 0x82, 0xC6,
     0x7F, 0xDA, 0xBA, 0xCB, 0x31, 0x6F, 0x20, 0xD7},
    {0xA1, 0x25, 0xE0, 0x23, 0xD8, 0xC9, 0xD4, 0x06,
     0xE4, 0xAE, 0x44, 0xD3, 0x5F, 0xD0, 0xE5, 0x53,
     0xA6, 0x02, 0xB4, 0xEF, 0x58, 0x83, 0x93, 0x8A,
     0x83, 0x20, 0xFA, 0x9B, 0xB9, 0x1B, 0xBD, 0x66,
     0xB5, 0xCD, 0x24, 0xAF, 0x01, 0x27, 0xA7, 0xC4,
     0x10, 0x07, 0x6C, 0xB8, 0x38, 0x47, 0x05, 0x45,
     0x4A, 0xBE, 0x19, 0x3E, 0xC3, 0xA6, 0x35, 0x45,
     0x7C, 0x38, 0xC7, 0x44, 0xF6, 0x96, 0xC7, 0x44},
    {0x0A, 0x42, 0xD7, 0x51, 0x06, 0xC3, 0x51, 0x24,
     0xA7, 0xF8, 0xBB, 0x3A, 0xFC, 0xEF, 0x19, 0x5A,
     0xA4, 0x4A, 0x7E, 0x20, 0xB3, 0x61, 0xF2, 0x6E,
     0x34, 0x27, 0xD6, 0x92, 0xB9, 0x7C, 0x23, 0x82,
     0xE0, 0x3A, 0x8C, 0x02, 0x11, 0xF9, 0x62, 0xBF,
     0x5D, 0x84, 0xE4, 0x9F, 0xCA, 0xF9, 0x1C, 0x26,
     0x3A, 0x32, 0x9C, 0x69, 0x0A, 0x94, 0x12, 0x07,
     0x02, 0x6A, 0xA3, 0xA8, 0x29, 0x12, 0x62, 0xBB},
    {0x76, 0x2F, 0xE0, 0x8D, 0x82, 0x46, 0x71, 0x78,
     0x41, 0x0B, 0x69, 0x81, 0x4D, 0x4B, 0xB5, 0xAC,
     0x8B, 0xCB, 0xFB, 0xAB, 0xD4, 0x0A, 0xC6, 0xA3,
     0xEA, 0x16, 0x37, 0x20, 0xB7, 0x63, 0x0B, 0x1B,
     0x88, 0xD5, 0x4F, 0x84, 0xD8, 0x90, 0xFD, 0xFE,
     0xB2, 0xF7, 0x80, 0x2E, 0x8C, 0xC1, 0xB2, 0xE4,
     0x55, 0xF6, 0xE9, 0x22, 0x94, 0xCA, 0xDE, 0x87,
     0x02, 0x71, 0x0B, 0xEA, 0x30, 0x3D, 0xE8, 0x29},
    {0x0C, 0xA4, 0xA8, 0x00, 0x68, 0xD3, 0x25, 0xDF,
     0x80, 0x3D, 0x1A, 0x82, 0x48, 0x24, 0x19, 0xE0,
     0x63, 0xA7, 0xAD, 0xEB, 0x12, 0xEB, 0xE3, 0x2A,
     0xD2, 0x86, 0x44, 0xBC, 0x2C, 0x10, 0xCF, 0x1E,
     0x03, 0xB2, 0x31, 0x8C, 0x6E, 0x2A, 0x36, 0xBA,
     0x79, 0xF6, 0x6A, 0xED, 0x21, 0x92, 0xB8, 0x6B,
     0xA1, 0xE8, 0x91, 0x97, 0x25, 0x8C, 0x71, 0xB1,
     0xF1, 0xF1, 0x0C, 0xD4, 0xCA, 0x3E, 0x36, 0xE6},
    {0xF4, 0x8D, 0x2D, 0x74, 0x7E, 0xD6, 0x42, 0x28,
     0xA3, 0x3A, 0xB0, 0x85, 0xB9, 0x6F, 0x62, 0x8D,
     0xD9, 0x48, 0x5D, 0x41, 0x6B, 0x5E, 0x8B, 0x5C,
     0x25, 0xE8, 0x28, 0xB7, 0x25, 0x83, 0x1A, 0xEE,
     0x4C, 0xD0, 0x49, 0x46, 0xCE, 0xE4, 0x79, 0x2C,
     0xFB, 0x3D, 0xC0, 0x5F, 0xC8, 0x9F, 0x7D, 0x6D,
     0x03, 0xD8, 0x0F, 0xE1, 0xB5, 0x7D, 0x0E, 0xF9,
     0x63, 0x2D, 0x7B, 0x1A, 0x00, 0x7A, 0x3D, 0x96},
    {0xE7, 0xB6, 0x18, 0xCF, 0x9D, 0x3A, 0x4A, 0xD3,
     0xB9, 0xB7, 0x3C, 0x68, 0x65, 0x7B, 0x88, 0x48,
     0x9C, 0x82, 0xAE, 0x97, 0x6B, 0xED, 0x09, 0x99,
     0x1B, 0xF8, 0xB0, 0x93, 0xDB, 0x75, 0x9B, 0xAD,
     0x3C, 0x98, 0xCC, 0x0B, 0x9E, 0x90, 0x9A, 0xD1,
     0xB0, 0xC0, 0xB9, 0xBE, 0x10, 0x43, 0xDE, 0xF6,
     0x92, 0x26, 0x6E, 0x24, 0x92, 0xA7, 0xC4, 0x03,
     0x4E, 0x66, 0xE1, 0x44, 0xD7, 0xD9, 0x6A, 0x12},
    {0x5C, 0x39, 0x9B, 0xE3, 0x30, 0xE8, 0x2B, 0xB6,
     0x3B, 0x82, 0xDA, 0xFC, 0xE8, 0x3C, 0xC8, 0x92,
     0xC9, 0x50, 0x69, 0x4D, 0x48, 0xF6, 0x44, 0xAC,
     0x08, 0xAC, 0x29, 0x1A, 0xD7, 0xA3, 0x2B, 0x8B,
     0x8C, 0xDA, 0x15, 0xDF, 0x27, 0xC9, 0xA0, 0xF6,
     0x85, 0xB8, 0xB4, 0x5C, 0xD8, 0x7F, 0x19, 0x35,
     0xB9, 0x8A, 0xA5, 0x75, 0xD9, 0xD9, 0x43, 0x3B,
     0x13, 0x79, 0x8C, 0x27, 0xF4, 0x48, 0xC5, 0xEB},
    {0x2C, 0x60, 0xCC, 0x67, 0xB7, 0x7F, 0x80, 0x72,
     0x48, 0xE1, 0xC8, 0x40, 0x3A, 0x5A, 0xFA, 0x9D,
     0x12, 0x25, 0x0C, 0x3E, 0x76, 0x42, 0x2C, 0xFB,
     0xA6, 0x95, 0x07, 0x4A, 0x14, 0x78, 0x49, 0x70,
     0x5A, 0x73, 0x48, 0xD8, 0x15, 0x56, 0x78, 0x24,
     0x96, 0x66, 0x40, 0x47, 0xA6, 0x9B, 0x3D, 0xB1,
     0x2B, 0x3D, 0xA6, 0x90, 0x4A, 0x1B, 0x49, 0x60,
     0xD6, 0x66, 0xFB, 0x72, 0x76, 0x2E, 0xDA, 0x5E}
};

UCHAR TestCrypMd5Answers[][MD5_HASH_SIZE] = {
    {0xCF, 0xCD, 0x20, 0x84, 0x95, 0xD5, 0x65, 0xEF,
     0x66, 0xE7, 0xDF, 0xF9, 0xF9, 0x87, 0x64, 0xDA},
    {0x78, 0x1E, 0x5E, 0x24, 0x5D, 0x69, 0xB5, 0x66,
     0x97, 0x9B, 0x86, 0xE2, 0x8D, 0x23, 0xF2, 0xC7},
    {0xA5, 0xB2, 0xB2, 0xE1, 0x55, 0xA9, 0x5B, 0x1F,
     0x65, 0x42, 0x87, 0x53, 0x29, 0x7A, 0x95, 0x07},
    {0x0B, 0xFC, 0xFA, 0x3B, 0x46, 0xFE, 0xD6, 0x81,
     0x6E, 0x71, 0x23, 0xBF, 0x49, 0x13, 0x0D, 0xF2},
    {0x34, 0x1A, 0xDC, 0x74, 0x3F, 0xB4, 0xA5, 0x3C,
     0xB9, 0xB0, 0x8D, 0xA1, 0xD0, 0xBE, 0xB8, 0xAF},
    {0xBB, 0x78, 0x18, 0x47, 0x60, 0xC1, 0x2B, 0x70,
     0xA5, 0xFA, 0x7E, 0xF8, 0x97, 0x0A, 0x8E, 0x10},
    {0xA1, 0x79, 0xF7, 0xA7, 0x55, 0x94, 0xFB, 0x49,
     0x0B, 0xBA, 0xA8, 0xC7, 0x4E, 0xB4, 0x3B, 0x7B},
    {0x9B, 0xD6, 0x29, 0x43, 0xE4, 0xB2, 0x4E, 0xDF,
     0xB1, 0xE4, 0x52, 0xB4, 0xEE, 0x72, 0xD5, 0x6E},
    {0xCA, 0xE0, 0xEC, 0x64, 0x0C, 0x47, 0xCA, 0x1E,
     0x0F, 0xFA, 0x21, 0xBC, 0xF5, 0xA4, 0x4D, 0x22},
    {0x92, 0xB1, 0x06, 0xA6, 0xFF, 0xB8, 0x65, 0x54,
     0xDD, 0x3D, 0x09, 0xF0, 0x99, 0x51, 0x10, 0xB5},
    {0x60, 0x47, 0xD3, 0x4A, 0x73, 0x68, 0xC0, 0xC8,
     0x9B, 0x71, 0xB7, 0x6C, 0xC6, 0x18, 0x86, 0xC9},
    {0xE0, 0xF1, 0x33, 0x1D, 0xD8, 0xC1, 0x40, 0xC8,
     0x1A, 0x32, 0xA5, 0xB3, 0x3F, 0x66, 0x61, 0x67},
};

PSTR TestCrypRsaPrivateKey =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "Proc-Type: 4,ENCRYPTED\n"
    "DEK-Info: AES-256-CBC,A3231074D89629B652B04C0ED96F7246\n"
    "\n"
    "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\n"
    "-----END RSA PRIVATE KEY-----\n";

PSTR TestCrypRsaPrivateKeyPassword = "1234";

//
// ------------------------------------------------------------------ Functions
//

INT
main (
    INT ArgumentCount,
    CHAR **Arguments
    )

/*++

Routine Description:

    This routine is the entry point for the crypto library test program. It
    executes the tests.

Arguments:

    ArgumentCount - Supplies the number of arguments specified on the command
        line.

    Arguments - Supplies an array of strings representing the command line
        arguments.

Return Value:

    returns 0 on success, or nonzero on failure.

--*/

{

    ULONG TestsFailed;

    srand(time(NULL));
    TestsFailed = 0;
    TestsFailed += TestSha1();
    TestsFailed += TestSha256();
    TestsFailed += TestSha512();
    TestsFailed += TestMd5();
    TestsFailed += TestRsa();
    if (TestsFailed != 0) {
        printf("\n*** %d failures in Crypto test. ***\n", TestsFailed);
        return 1;
    }

    return 0;
}

//
// --------------------------------------------------------- Internal Functions
//

ULONG
TestSha1 (
    VOID
    )

/*++

Routine Description:

    This routine tests the SHA-1 hash function.

Arguments:

    None.

Return Value:

    Returns the number of test failures.

--*/

{

    SHA1_CONTEXT Context;
    UINTN Count;
    BOOL Failed;
    ULONG Failures;
    UCHAR Hash[SHA1_HASH_SIZE];
    UINTN HashIndex;
    UINTN Index;
    UINTN Size;

    Failures = 0;
    Count = sizeof(TestCrypHashDataSizes) / sizeof(TestCrypHashDataSizes[0]);
    for (Index = 0; Index < Count; Index += 1) {
        Size = TestCrypHashDataSizes[Index];
        CySha1Initialize(&Context);
        CySha1AddContent(&Context, (PUCHAR)TestCrypData, Size);
        CySha1GetHash(&Context, Hash);
        Failed = FALSE;
        for (HashIndex = 0; HashIndex < SHA1_HASH_SIZE; HashIndex += 1) {
            if (Hash[HashIndex] != TestCrypSha1Answers[Index][HashIndex]) {
                Failed = TRUE;
            }
        }

        if (Failed != FALSE) {
            printf("Failed SHA1 at size %d:\nExpect: ", Size);
            for (HashIndex = 0; HashIndex < SHA1_HASH_SIZE; HashIndex += 1) {
                printf("%02X ", TestCrypSha1Answers[Index][HashIndex]);
            }

            printf("\n   Got: ");
            for (HashIndex = 0; HashIndex < SHA1_HASH_SIZE; HashIndex += 1) {
                printf("0x%02X, ", Hash[HashIndex]);
            }

            printf("\n");
            Failures += 1;
        }
    }

    return Failures;
}

ULONG
TestSha256 (
    VOID
    )

/*++

Routine Description:

    This routine tests the SHA-256 hash function.

Arguments:

    None.

Return Value:

    Returns the number of test failures.

--*/

{

    SHA256_CONTEXT Context;
    UINTN Count;
    BOOL Failed;
    ULONG Failures;
    UCHAR Hash[SHA256_HASH_SIZE];
    UINTN HashIndex;
    UINTN Index;
    UINTN Size;

    Failures = 0;
    Count = sizeof(TestCrypHashDataSizes) / sizeof(TestCrypHashDataSizes[0]);
    for (Index = 0; Index < Count; Index += 1) {
        Size = TestCrypHashDataSizes[Index];
        CySha256Initialize(&Context);
        CySha256AddContent(&Context, TestCrypData, Size);
        CySha256GetHash(&Context, Hash);
        Failed = FALSE;
        for (HashIndex = 0; HashIndex < SHA256_HASH_SIZE; HashIndex += 1) {
            if (Hash[HashIndex] != TestCrypSha256Answers[Index][HashIndex]) {
                Failed = TRUE;
            }
        }

        if (Failed != FALSE) {
            printf("Failed SHA256 at size %d:\nExpect: ", Size);
            for (HashIndex = 0; HashIndex < SHA256_HASH_SIZE; HashIndex += 1) {
                printf("%02X ", TestCrypSha256Answers[Index][HashIndex]);
            }

            printf("\n   Got: ");
            for (HashIndex = 0; HashIndex < SHA256_HASH_SIZE; HashIndex += 1) {
                printf("0x%02X, ", Hash[HashIndex]);
                if (((HashIndex + 1) & 0x7) == 0) {
                    printf("\n");
                }
            }

            printf("}\n");
            Failures += 1;
        }
    }

    return Failures;
}

ULONG
TestSha512 (
    VOID
    )

/*++

Routine Description:

    This routine tests the SHA-512 hash function.

Arguments:

    None.

Return Value:

    Returns the number of test failures.

--*/

{

    SHA512_CONTEXT Context;
    UINTN Count;
    BOOL Failed;
    ULONG Failures;
    UCHAR Hash[SHA512_HASH_SIZE];
    UINTN HashIndex;
    UINTN Index;
    UINTN Size;

    Failures = 0;
    Count = sizeof(TestCrypHashDataSizes) / sizeof(TestCrypHashDataSizes[0]);
    for (Index = 0; Index < Count; Index += 1) {
        Size = TestCrypHashDataSizes[Index];
        CySha512Initialize(&Context);
        CySha512AddContent(&Context, TestCrypData, Size);
        CySha512GetHash(&Context, Hash);
        Failed = FALSE;
        for (HashIndex = 0; HashIndex < SHA512_HASH_SIZE; HashIndex += 1) {
            if (Hash[HashIndex] != TestCrypSha512Answers[Index][HashIndex]) {
                Failed = TRUE;
            }
        }

        if (Failed != FALSE) {
            printf("Failed SHA512 at size %d:\nExpect: ", Size);
            for (HashIndex = 0; HashIndex < SHA512_HASH_SIZE; HashIndex += 1) {
                printf("%02X ", TestCrypSha512Answers[Index][HashIndex]);
            }

            printf("\n   Got: ");
            for (HashIndex = 0; HashIndex < SHA512_HASH_SIZE; HashIndex += 1) {
                printf("0x%02X, ", Hash[HashIndex]);
                if (((HashIndex + 1) & 0x7) == 0) {
                    printf("\n");
                }
            }

            printf("}\n");
            Failures += 1;
        }
    }

    return Failures;
}

ULONG
TestMd5 (
    VOID
    )

/*++

Routine Description:

    This routine tests the SHA-256 hash function.

Arguments:

    None.

Return Value:

    Returns the number of test failures.

--*/

{

    MD5_CONTEXT Context;
    UINTN Count;
    BOOL Failed;
    ULONG Failures;
    UCHAR Hash[MD5_HASH_SIZE];
    UINTN HashIndex;
    UINTN Index;
    UINTN Size;

    Failures = 0;
    Count = sizeof(TestCrypHashDataSizes) / sizeof(TestCrypHashDataSizes[0]);
    for (Index = 0; Index < Count; Index += 1) {
        Size = TestCrypHashDataSizes[Index];
        CyMd5Initialize(&Context);
        CyMd5AddContent(&Context, (PUCHAR)TestCrypData, Size);
        CyMd5GetHash(&Context, Hash);
        Failed = FALSE;
        for (HashIndex = 0; HashIndex < MD5_HASH_SIZE; HashIndex += 1) {
            if (Hash[HashIndex] != TestCrypMd5Answers[Index][HashIndex]) {
                Failed = TRUE;
            }
        }

        if (Failed != FALSE) {
            printf("Failed MD5 at size %d:\nExpect: ", Size);
            for (HashIndex = 0; HashIndex < SHA1_HASH_SIZE; HashIndex += 1) {
                printf("%02X ", TestCrypSha1Answers[Index][HashIndex]);
            }

            printf("\n   Got: ");
            for (HashIndex = 0; HashIndex < MD5_HASH_SIZE; HashIndex += 1) {
                printf("0x%02X, ", Hash[HashIndex]);
                if (((HashIndex + 1) & 0x7) == 0) {
                    if (HashIndex == MD5_HASH_SIZE - 1) {
                        printf("},\n");

                    } else {
                        printf("\n ");
                    };
                }
            }

            Failures += 1;
        }
    }

    return Failures;
}

ULONG
TestRsa (
    VOID
    )

/*++

Routine Description:

    This routine tests the RSA crypto routines.

Arguments:

    None.

Return Value:

    Returns the number of test failures.

--*/

{

    UCHAR CipherBuffer[512];
    ULONG Failures;
    UINTN Index;
    UCHAR PlainBuffer[512];
    RSA_CONTEXT RsaContext;
    INTN Size;
    KSTATUS Status;

    Failures = 0;
    RtlZeroMemory(&RsaContext, sizeof(RSA_CONTEXT));
    RsaContext.BigIntegerContext.AllocateMemory = (PCY_ALLOCATE_MEMORY)malloc;
    RsaContext.BigIntegerContext.ReallocateMemory =
                                                (PCY_REALLOCATE_MEMORY)realloc;

    RsaContext.BigIntegerContext.FreeMemory = (PCY_FREE_MEMORY)free;
    Status = CyRsaInitializeContext(&RsaContext);
    if (!KSUCCESS(Status)) {
        Failures += 1;
        return Failures;
    }

    Status = CyRsaAddPemFile(&RsaContext,
                             TestCrypRsaPrivateKey,
                             RtlStringLength(TestCrypRsaPrivateKey) + 1,
                             TestCrypRsaPrivateKeyPassword);

    if (!KSUCCESS(Status)) {
        Failures += 1;
        goto TestRsaEnd;
    }

    Size = CyRsaEncrypt(&RsaContext,
                        TestCrypSha512Answers[0],
                        SHA512_HASH_SIZE,
                        CipherBuffer,
                        TRUE);

    if (Size != 512) {
        Failures += 1;
        goto TestRsaEnd;
    }

    //
    // Compare to the correct answer.
    //

    Size = CyRsaDecrypt(&RsaContext, CipherBuffer, PlainBuffer, FALSE);
    if (Size != SHA512_HASH_SIZE) {
        Failures += 1;
        goto TestRsaEnd;
    }

    for (Index = 0; Index < SHA512_HASH_SIZE; Index += 1) {
        if (PlainBuffer[Index] != TestCrypSha512Answers[0][Index]) {
            Failures += 1;
            printf("RSA %d: %02x %02x",
                   Index,
                   PlainBuffer[Index],
                   TestCrypSha512Answers[0][Index]);
        }
    }

TestRsaEnd:
    CyRsaDestroyContext(&RsaContext);
    if (Failures != 0) {
        printf("%d failures in RSA test.\n");
    }

    return Failures;
}
