/*
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Lead and tail tabulated values of sinh(i) and cosh(i) 
// for i = 0,...,36. The lead part has 26 leading bits.

DECLARE_TABLE(double2, SINH_TBL, 37,
    (double2)(0x0.0000000000000p+0, 0x0.0000000000000p+0),
    (double2)(0x1.2cd9fc0000000p+0, 0x1.13ae6096a0092p-26),
    (double2)(0x1.d03cf60000000p+1, 0x1.db70cfb79a640p-26),
    (double2)(0x1.40926e0000000p+3, 0x1.c2526b66dc067p-23),
    (double2)(0x1.b4a3800000000p+4, 0x1.b81b18647f380p-23),
    (double2)(0x1.28d0160000000p+6, 0x1.bc1cdd1e1eb08p-20),
    (double2)(0x1.936d228000000p+7, 0x1.d9f201534fb09p-19),
    (double2)(0x1.1228768000000p+9, 0x1.d1c064a4e9954p-18),
    (double2)(0x1.749ea50000000p+10, 0x1.4eca65d06ea74p-18),
    (double2)(0x1.fa71570000000p+11, 0x1.0c259bcc0ecc5p-15),
    (double2)(0x1.5829dc8000000p+13, 0x1.b5a6647cf9016p-13),
    (double2)(0x1.d3c4488000000p+14, 0x1.9691adefb0870p-15),
    (double2)(0x1.3de1650000000p+16, 0x1.3410fc29cde38p-10),
    (double2)(0x1.b00b590000000p+17, 0x1.6a31a50b6fb3cp-11),
    (double2)(0x1.259ac48000000p+19, 0x1.7defc71805c40p-10),
    (double2)(0x1.8f0cca8000000p+20, 0x1.eb49fd80e0babp-6),
    (double2)(0x1.0f2ebd0000000p+22, 0x1.4fffc7bcd5920p-7),
    (double2)(0x1.7093488000000p+23, 0x1.03a93b6c63435p-3),
    (double2)(0x1.f4f2208000000p+24, 0x1.1940bb255fd1cp-4),
    (double2)(0x1.546d8f8000000p+26, 0x1.ed26e14260b50p-2),
    (double2)(0x1.ceb0888000000p+27, 0x1.b47401fc9f2a2p+0),
    (double2)(0x1.3a6e1f8000000p+29, 0x1.67bb3f55634f1p+3),
    (double2)(0x1.ab5adb8000000p+30, 0x1.c435ff8194ddcp+2),
    (double2)(0x1.226af30000000p+32, 0x1.d8fee052ba63ap+5),
    (double2)(0x1.8ab7fb0000000p+33, 0x1.51d7edccde3f6p+7),
    (double2)(0x1.0c3d390000000p+35, 0x1.04b1644557d1ap+8),
    (double2)(0x1.6c93268000000p+36, 0x1.6a6b5ca0a9dc4p+8),
    (double2)(0x1.ef822f0000000p+37, 0x1.fd9cc72249abap+11),
    (double2)(0x1.50bba30000000p+39, 0x1.e58de693edab5p+13),
    (double2)(0x1.c9aae40000000p+40, 0x1.8c70158ac6363p+14),
    (double2)(0x1.3704708000000p+42, 0x1.7614764f43e20p+15),
    (double2)(0x1.a6b7658000000p+43, 0x1.6337db36fc718p+17),
    (double2)(0x1.1f43fc8000000p+45, 0x1.12d98b1f611e2p+19),
    (double2)(0x1.866f348000000p+46, 0x1.392bc108b37ccp+19),
    (double2)(0x1.0953e28000000p+48, 0x1.ce87bdc3473dcp+22),
    (double2)(0x1.689e220000000p+49, 0x1.bc8d5ae99ad14p+21),
    (double2)(0x1.ea215a0000000p+50, 0x1.d20d76744835cp+22),
)

DECLARE_TABLE(double2, COSH_TBL, 37,
    (double2)(0x1.0000000000000p+0, 0x0.0000000000000p+0),
    (double2)(0x1.8b07550000000p+0, 0x1.d9f5504c2bd28p-28),
    (double2)(0x1.e18fa08000000p+1, 0x1.7cb66f0a4c9fdp-25),
    (double2)(0x1.422a490000000p+3, 0x1.f58617928e588p-23),
    (double2)(0x1.b4ee858000000p+4, 0x1.bc7d000c38d48p-25),
    (double2)(0x1.28d6fc8000000p+6, 0x1.f7f9d4e329998p-21),
    (double2)(0x1.936e678000000p+7, 0x1.6e6e464885269p-19),
    (double2)(0x1.1228948000000p+9, 0x1.ba3a8b946c154p-19),
    (double2)(0x1.749eaa8000000p+10, 0x1.3f4e76110d5a4p-18),
    (double2)(0x1.fa71580000000p+11, 0x1.17622515a3e2bp-15),
    (double2)(0x1.5829dd0000000p+13, 0x1.4dc4b528af3d0p-17),
    (double2)(0x1.d3c4488000000p+14, 0x1.1156278615e10p-14),
    (double2)(0x1.3de1650000000p+16, 0x1.35ad50ed821f5p-10),
    (double2)(0x1.b00b590000000p+17, 0x1.6b61055f2935cp-11),
    (double2)(0x1.259ac48000000p+19, 0x1.7e2794a601240p-10),
    (double2)(0x1.8f0cca8000000p+20, 0x1.eb4b45f6aadd3p-6),
    (double2)(0x1.0f2ebd0000000p+22, 0x1.5000b967b3698p-7),
    (double2)(0x1.7093488000000p+23, 0x1.03a940fadc092p-3),
    (double2)(0x1.f4f2208000000p+24, 0x1.1940bf3bf874cp-4),
    (double2)(0x1.546d8f8000000p+26, 0x1.ed26e1a2a2110p-2),
    (double2)(0x1.ceb0888000000p+27, 0x1.b4740205796d6p+0),
    (double2)(0x1.3a6e1f8000000p+29, 0x1.67bb3f55cb85dp+3),
    (double2)(0x1.ab5adb8000000p+30, 0x1.c435ff81e18acp+2),
    (double2)(0x1.226af30000000p+32, 0x1.d8fee052bdea4p+5),
    (double2)(0x1.8ab7fb0000000p+33, 0x1.51d7edccde926p+7),
    (double2)(0x1.0c3d390000000p+35, 0x1.04b1644557e0ep+8),
    (double2)(0x1.6c93268000000p+36, 0x1.6a6b5ca0a9e1cp+8),
    (double2)(0x1.ef822f0000000p+37, 0x1.fd9cc72249abep+11),
    (double2)(0x1.50bba30000000p+39, 0x1.e58de693edab5p+13),
    (double2)(0x1.c9aae40000000p+40, 0x1.8c70158ac6364p+14),
    (double2)(0x1.3704708000000p+42, 0x1.7614764f43e20p+15),
    (double2)(0x1.a6b7658000000p+43, 0x1.6337db36fc718p+17),
    (double2)(0x1.1f43fc8000000p+45, 0x1.12d98b1f611e2p+19),
    (double2)(0x1.866f348000000p+46, 0x1.392bc108b37ccp+19),
    (double2)(0x1.0953e28000000p+48, 0x1.ce87bdc3473dcp+22),
    (double2)(0x1.689e220000000p+49, 0x1.bc8d5ae99ad14p+21),
    (double2)(0x1.ea215a0000000p+50, 0x1.d20d76744835cp+22),
)

