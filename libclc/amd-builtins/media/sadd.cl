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
#include "media.h"

#pragma OPENCL EXTENSION cl_amd_media_ops2 : enable

__attribute__((overloadable,always_inline,const)) uint2 amd_sadd(uint2 v1, uint2 v2, uint2 v3) 
{
    uint2 ret;
    ret.x =  __hsail_sadd(v1.x,v2.x, v3.x);
    ret.y =  __hsail_sadd(v1.y,v2.y,v3.y);
    return ret;
}
__attribute__((overloadable,always_inline,const)) uint3 amd_sadd(uint3 v1, uint3 v2, uint3 v3) 
{
    uint3 ret;
    ret.x =  __hsail_sadd(v1.x,v2.x, v3.x);
    ret.y =  __hsail_sadd(v1.y,v2.y,v3.y);
    ret.z =  __hsail_sadd(v1.z,v2.z, v3.z);
    return ret;
}
__attribute__((overloadable,always_inline,const)) uint4 amd_sadd(uint4 v1, uint4 v2, uint4 v3) 
{
    uint4 ret;
    ret.x =  __hsail_sadd(v1.x,v2.x, v3.x);
    ret.y =  __hsail_sadd(v1.y,v2.y,v3.y);
    ret.z =  __hsail_sadd(v1.z,v2.z, v3.z);
    ret.w =  __hsail_sadd(v1.w,v2.w,v3.w);
    return ret;
}
__attribute__((overloadable,always_inline,const)) uint8 amd_sadd(uint8 v1, uint8 v2, uint8 v3) 
{
    uint8 ret;
    ret.s0 =  __hsail_sadd(v1.s0,v2.s0, v3.s0);
    ret.s1 =  __hsail_sadd(v1.s1,v2.s1,v3.s1);
    ret.s2 =  __hsail_sadd(v1.s2,v2.s2, v3.s2);
    ret.s3 =  __hsail_sadd(v1.s3,v2.s3,v3.s3);
    ret.s4 =  __hsail_sadd(v1.s4,v2.s4,v3.s4) ;
    ret.s5 =  __hsail_sadd(v1.s5,v2.s5,v3.s5);
    ret.s6 =  __hsail_sadd(v1.s6,v2.s6,v3.s6 );
    ret.s7 =  __hsail_sadd(v1.s7,v2.s7,v3.s7);
    return ret;
}
__attribute__((overloadable,always_inline,const)) uint16 amd_sadd(uint16 v1, uint16 v2, uint16 v3) 
{
    uint16 ret;
    ret.s0 =  __hsail_sadd(v1.s0,v2.s0, v3.s0);
    ret.s1 =  __hsail_sadd(v1.s1,v2.s1,v3.s1);
    ret.s2 =  __hsail_sadd(v1.s2,v2.s2, v3.s2);
    ret.s3 =  __hsail_sadd(v1.s3,v2.s3,v3.s3);
    ret.s4 =  __hsail_sadd(v1.s4,v2.s4,v3.s4) ;
    ret.s5 =  __hsail_sadd(v1.s5,v2.s5,v3.s5);
    ret.s6 =  __hsail_sadd(v1.s6,v2.s6,v3.s6 );
    ret.s7 =  __hsail_sadd(v1.s7,v2.s7,v3.s7);
    ret.s8 =  __hsail_sadd(v1.s8,v2.s8,v3.s8 );
    ret.s9 =  __hsail_sadd(v1.s9,v2.s9,v3.s9);
    ret.sa =  __hsail_sadd(v1.sa,v2.sa, v3.sa);
    ret.sb =  __hsail_sadd(v1.sb,v2.sb,v3.sb);
    ret.sc =  __hsail_sadd(v1.sc,v2.sc, v3.sc);
    ret.sd =  __hsail_sadd(v1.sd,v2.sd,v3.sd);
    ret.se =  __hsail_sadd(v1.se,v2.se, v3.se);
    ret.sf=  __hsail_sadd(v1.sf,v2.sf,v3.sf);

    return ret;
}
__attribute__((overloadable,always_inline,const)) uint amd_sadd(uint v1, uint v2, uint v3) 
{
    return  __hsail_sadd(v1,v2,v3);
}
