/*
Copyright (c) 2014 Larry Gritz et al.
All Rights Reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
* Neither the name of Sony Pictures Imageworks nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/// @file  simd.h
///
/// @brief Classes for SIMD processing.
///
/// Nice references for all the Intel intrinsics (SSE*, AVX*, etc.):
///   https://software.intel.com/sites/landingpage/IntrinsicsGuide/
///
/// It helped me a lot to peruse the source of these packages:
///   Syrah:     https://github.com/boulos/syrah
///   Embree:    https://github.com/embree
///   Vectorial: https://github.com/scoopr/vectorial
///
/// To find out which CPU features you have:
///   Linux: cat /proc/cpuinfo
///   OSX:   sysctl machdep.cpu.features
///
/// Additional web resources:
///   http://www.codersnotes.com/notes/maths-lib-2016/


#pragma once
#ifndef OIIO_SIMD_H
#define OIIO_SIMD_H 1

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/missing_math.h>
#include <OpenImageIO/platform.h>
#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathMatrix.h>
#include <algorithm>


//////////////////////////////////////////////////////////////////////////
// Sort out which SIMD capabilities we have and set definitions
// appropriately.
//

#if (defined(__SSE2__) || (_MSC_VER >= 1300 && !_M_CEE_PURE)) && !defined(OIIO_NO_SSE)
#  include <immintrin.h>
#  if (defined(__i386__) || defined(__x86_64__)) && (OIIO_GNUC_VERSION > 40400 || __clang__)
#    include <x86intrin.h>
#  endif
#  if (defined(__SSE4_1__) || defined(__SSE4_2__))
#    define OIIO_SIMD_SSE 4
      /* N.B. We consider both SSE4.1 and SSE4.2 to be "4". There are a few
       * instructions specific to 4.2, but they are all related to string
       * comparisons and CRCs, which don't currently seem relevant to OIIO,
       * so for simplicity, we sweep this difference under the rug.
       */
#  elif defined(__SSSE3__)
#    define OIIO_SIMD_SSE 3
     /* N.B. We only use OIIO_SIMD_SSE = 3 when fully at SSSE3. In theory,
      * there are a few older architectures that are SSE3 but not SSSE3,
      * and this simplification means that these particular old platforms
      * will only get SSE2 goodness out of our code. So be it. Anybody who
      * cares about performance is probably using a 64 bit machine that's
      * SSE 4.x or AVX by now.
      */
#  else
#    define OIIO_SIMD_SSE 2
#  endif
#  define OIIO_SIMD 1
#  define OIIO_SIMD_MASK_BYTE_WIDTH 4
#  define OIIO_SIMD_MAX_SIZE_BYTES 16
#  define OIIO_SIMD_ALIGN OIIO_ALIGN(16)
#  define OIIO_SIMD4_ALIGN OIIO_ALIGN(16)
#  define OIIO_SSE_ALIGN OIIO_ALIGN(16)
#endif

// FIXME Future: support AVX
#if defined(__AVX__) && !defined(OIIO_NO_AVX)
   // N.B. Any machine with AVX will also have SSE
#  define OIIO_SIMD_AVX 1
// #  define OIIO_SIMD_MASK_BYTE_WIDTH 4
// #  undef OIIO_SIMD_MAX_SIZE_BYTES
// #  define OIIO_SIMD_MAX_SIZE_BYTES 32
// #  undef OIIO_SIMD_ALIGN
// #  define OIIO_SIMD_ALIGN OIIO_ALIGN(32)
// #  define OIIO_AVX_ALIGN OIIO_ALIGN(32)
#endif

#if defined(__FMA__) || defined(__AVX2__)
#  define OIIO_FMA_ENABLED 1
#endif

// FIXME Future: support ARM Neon
// Uncomment this when somebody with Neon can verify it works
#if 0 && defined(__ARM_NEON__) && !defined(OIIO_NO_NEON)
#  include <arm_neon.h>
#  define OIIO_SIMD 1
#  define OIIO_SIMD_NEON 1
#  define OIIO_SIMD_MASK_BYTE_WIDTH 4
#  define OIIO_SIMD_MAX_SIZE_BYTES 16
#  define OIIO_SIMD_ALIGN OIIO_ALIGN(16)
#  define OIIO_SIMD4_ALIGN OIIO_ALIGN(16)
#  define OIIO_SSE_ALIGN OIIO_ALIGN(16)
#endif

#ifndef OIIO_SIMD
   // No SIMD available
#  define OIIO_SIMD 0
#  define OIIO_SIMD_ALIGN
#  define OIIO_SIMD4_ALIGN
#  define OIIO_SIMD_MAX_SIZE_BYTES 16
#endif



OIIO_NAMESPACE_BEGIN

namespace simd {

//////////////////////////////////////////////////////////////////////////
// Forward declarations of our main SIMD classes

class int4;
class float4;
class mask4;
class float3;
class matrix44;


//////////////////////////////////////////////////////////////////////////
// Template magic to determine the raw SIMD types involved, and other
// things helpful for metaprogramming.

template <typename T, int N> struct simd_raw_t { struct type { T val[N]; }; };
template <int N> struct simd_bool_t { struct type { int val[N]; }; };

#if OIIO_SIMD_SSE
template<> struct simd_raw_t<int,4> { typedef __m128i type; };
template<> struct simd_raw_t<float,4> { typedef __m128 type; };
template<> struct simd_bool_t<4> { typedef __m128 type; };
#endif

#if OIIO_SIMD_AVX
template<> struct simd_raw_t<int,8> { typedef __m256i type; };
template<> struct simd_raw_t<float,8> { typedef __m256 type; };
template<> struct simd_bool_t<8> { typedef __m256 type; };
#endif

#if OIIO_SIMD_AVX >= 512
template<> struct simd_raw_t<int,16> { typedef __m512i type; };
template<> struct simd_raw_t<float,16> { typedef __m512 type; };
template<> struct simd_bool_t<16> { typedef __m512 type; };
#endif

#if OIIO_SIMD_NEON
template<> struct simd_raw_t<int,4> { typedef int32x4 type; };
template<> struct simd_raw_t<float,4> { typedef float32x4_t type; };
template<> struct simd_bool_t<4> { typedef int32x4 type; };
#endif


/// Template to retrieve the vector type from the scalar. For example,
/// simd::VecType<int,4> will be float4.
template<typename T,int elements> struct VecType {};
template<> struct VecType<int,4>   { typedef int4 type; };
template<> struct VecType<float,4> { typedef float4 type; };
template<> struct VecType<float,3> { typedef float3 type; };
template<> struct VecType<bool,4>  { typedef mask4 type; };

/// Template to retrieve the SIMD size of a SIMD type. Rigged to be 1 for
/// anything but our SIMD types.
template<typename T> struct SimdSize { static const int size = 1; };
template<> struct SimdSize<int4>     { static const int size = 4; };
template<> struct SimdSize<float4>   { static const int size = 4; };
template<> struct SimdSize<float3>   { static const int size = 4; };
template<> struct SimdSize<mask4>    { static const int size = 4; };

/// Template to retrieve the number of elements size of a SIMD type. Rigged
/// to be 1 for anything but our SIMD types.
template<typename T> struct SimdElements { static const int size = SimdSize<T>::size; };
template<> struct SimdElements<float3>   { static const int size = 3; };



//////////////////////////////////////////////////////////////////////////
// Macros helpful for making static constants in code.

# define OIIO_SIMD_FLOAT4_CONST(name,val) \
    static const OIIO_SIMD4_ALIGN float name[4] = { (val), (val), (val), (val) }
# define OIIO_SIMD_FLOAT4_CONST4(name,v0,v1,v2,v3) \
    static const OIIO_SIMD4_ALIGN float name[4] = { (v0), (v1), (v2), (v3) }
# define OIIO_SIMD_INT4_CONST(name,val) \
    static const OIIO_SIMD4_ALIGN int name[4] = { (val), (val), (val), (val) }
# define OIIO_SIMD_INT4_CONST4(name,v0,v1,v2,v3) \
    static const OIIO_SIMD4_ALIGN int name[4] = { (v0), (v1), (v2), (v3) }
# define OIIO_SIMD_UINT4_CONST(name,val) \
    static const OIIO_SIMD4_ALIGN uint32_t name[4] = { (val), (val), (val), (val) }
# define OIIO_SIMD_UINT4_CONST4(name,v0,v1,v2,v3) \
    static const OIIO_SIMD4_ALIGN uint32_t name[4] = { (v0), (v1), (v2), (v3) }



//////////////////////////////////////////////////////////////////////////
// Some macros just for use in this file (#undef-ed at the end) making
// it more succinct to express per-element operations.

#define SIMD_DO(x) for (int i = 0; i < elements; ++i) x
#define SIMD_CONSTRUCT(x) for (int i = 0; i < elements; ++i) m_val[i] = (x)
#define SIMD_CONSTRUCT_PAD(x) for (int i = 0; i < elements; ++i) m_val[i] = (x); \
                              for (int i = elements; i < paddedelements; ++i) m_val[i] = 0
#define SIMD_RETURN(T,x) T r; for (int i = 0; i < r.elements; ++i) r[i] = (x); return r



//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// The public declarations of the main SIMD classes follow: maskN, intN,
// floatN, matrix44.
//
// These class declarations are intended to be brief and self-documenting,
// and give all the information that users or client applications need to
// know to use these classes.
//
// No implementations are given inline except for the briefest, completely
// generic methods that don't have any architecture-specific overloads.
// After the class defintions, there will be an immense pile of full
// implementation definitions, which casual users are not expected to
// understand.
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////


/// mask4: A mask 4-vector, whose elements act mostly like bools,
/// accelerated by SIMD instructions when available. This is what is
/// naturally produced by SIMD comparison operators on the float4 and int4
/// types.
class mask4 {
public:
    static const char* type_name() { return "mask4"; }
    typedef bool value_t;     ///< Underlying equivalent scalar value type
    enum { elements = 4 };    ///< Number of scalar elements
    enum { paddedelements = 4 }; ///< Number of scalar elements for full pad
    enum { bits = 128 };      ///< Total number of bits
    typedef simd_bool_t<4>::type simd_t;  ///< the native SIMD type used

    /// Default constructor (contents undefined)
    mask4 () { }

    /// Destructor
    ~mask4 () { }

    /// Construct from a single value (store it in all slots)
    mask4 (bool a) { load(a); }

    /// Construct from 4 values
    mask4 (bool a, bool b, bool c, bool d) { load(a,b,c,d); }

    /// Copy construct from another mask4
    mask4 (const mask4 &other) { m_vec = other.m_vec; }

    /// Construct from an int4 (is each element nonzero?)
    mask4 (const int4 &i);

    /// Construct from the underlying SIMD type
    mask4 (const simd_t& m) : m_vec(m) { }

    /// Return the raw SIMD type
    operator simd_t () const { return m_vec; }
    simd_t simd () const { return m_vec; }

    /// Set all components to false
    void clear ();

    /// Return a mask4 the is 'false' for all values
    static const mask4 False ();

    /// Return a mask4 the is 'true' for all values
    static const mask4 True ();

    /// Assign one value to all components
    const mask4 & operator= (bool a);

    /// Assignment of another mask4
    const mask4 & operator= (const mask4 & other);

    /// Component access (get)
    bool operator[] (int i) const ;

    /// Component access (set).
    /// NOTE: use with caution. The implementation sets the integer
    /// value, which may not have the same bit pattern as the bool returned
    /// by operator[]const.
    int& operator[] (int i);

    /// Helper: load a single value into all components.
    void load (bool a);

    /// Helper: load separate values into each component.
    void load (bool a, bool b, bool c, bool d);

    /// Helper: store the values into memory as bools.
    void store (bool *values) const;

    /// Store the first n values into memory.
    void store (bool *values, int n) const;

    /// Logical/bitwise operators, component-by-component
    friend mask4 operator! (const mask4 & a);
    friend mask4 operator& (const mask4 & a, const mask4 & b);
    const mask4& operator&= (const mask4 & b);
    friend mask4 operator| (const mask4 & a, const mask4 & b);
    const mask4& operator|= (const mask4 & a);
    friend mask4 operator^ (const mask4& a, const mask4& b);
    const mask4 & operator^= (const mask4& a);
    mask4 operator~ ();

    /// Comparison operators, component by component
    friend const mask4 operator== (const mask4 & a, const mask4 & b);
    friend const mask4 operator!= (const mask4 & a, const mask4 & b);

    /// Stream output
    friend inline std::ostream& operator<< (std::ostream& cout, const mask4 & a);

private:
    // The actual data representation
    union {
        simd_t m_vec;
        int m_val[paddedelements];
    };
};



/// Helper: shuffle/swizzle with constant (templated) indices.
/// Example: shuffle<1,1,2,2>(mask4(a,b,c,d)) returns (b,b,c,c)
template<int i0, int i1, int i2, int i3> mask4 shuffle (const mask4& a);

/// shuffle<i>(a) is the same as shuffle<i,i,i,i>(a)
template<int i> mask4 shuffle (const mask4& a);

/// Helper: as rapid as possible extraction of one component, when the
/// index is fixed.
template<int i> bool extract (const mask4& a);

/// Helper: substitute val for a[i]
template<int i> mask4 insert (const mask4& a, bool val);

/// Logical "and" reduction, i.e., 'and' all components together, resulting
/// in a single bool.
bool reduce_and (const mask4& v);

/// Logical "or" reduction, i.e., 'or' all components together, resulting
/// in a single bool.
bool reduce_or (const mask4& v);

/// Are all components true?
OIIO_FORCEINLINE bool all  (const mask4& v) { return reduce_and(v) == true; }

/// Are any components true?
OIIO_FORCEINLINE bool any  (const mask4& v) { return reduce_or(v) == true; }

/// Are all components false:
OIIO_FORCEINLINE bool none (const mask4& v) { return reduce_or(v) == false; }





/// Integer 4-vector, accelerated by SIMD instructions when available.
class int4 {
public:
    static const char* type_name() { return "int4"; }
    typedef int value_t;      ///< Underlying equivalent scalar value type
    enum { elements = 4 };    ///< Number of scalar elements
    enum { paddedelements = 4 }; ///< Number of scalar elements for full pad
    enum { bits = 128 };      ///< Total number of bits
    typedef simd_raw_t<int,4>::type simd_t;  ///< the native SIMD type used

    /// Default constructor (contents undefined)
    int4 () { }

    /// Destructor
    ~int4 () { }

    /// Construct from a single value (store it in all slots)
    int4 (int a) { load(a); }

    /// Construct from 2 values -- (a,a,b,b)
    int4 (int a, int b) { load(a,a,b,b); }

    /// Construct from 4 values
    int4 (int a, int b, int c, int d) { load(a,b,c,d); }

    /// Construct from a pointer to 4 values
    int4 (const int *vals) { load (vals); }

    /// Construct from a pointer to 4 unsigned short values
    explicit int4 (const unsigned short *vals) { load(vals); }

    /// Construct from a pointer to 4 signed short values
    explicit int4 (const short *vals) { load(vals); }

    /// Construct from a pointer to 4 unsigned char values (0 - 255)
    explicit int4 (const unsigned char *vals) { load(vals); }

    /// Construct from a pointer to 4 signed char values (-128 - 127)
    explicit int4 (const char *vals) { load(vals); }

    /// Copy construct from another int4
    int4 (const int4 & other) { m_vec = other.m_vec; }

    /// Convert a float4 to an int4. Equivalent to i = (int)f;
    explicit int4 (const float4& f); // implementation below

    /// Construct from the underlying SIMD type
    int4 (const simd_t& m) : m_vec(m) { }

    /// Return the raw SIMD type
    operator simd_t () const { return m_vec; }
    simd_t simd () const { return m_vec; }

    /// Sset all components to 0
    void clear () ;

    /// Return an int4 with all components set to 0
    static const int4 Zero ();

    /// Return an int4 with all components set to 1
    static const int4 One ();

    /// Return an int4 with all components set to -1 (aka 0xffffffff)
    static const int4 NegOne ();

    /// Return an int4 with incremented components (e.g., 0,1,2,3).
    /// Optional argument can give a non-zero starting point.
    static const int4 Iota (int value=0);

    /// Assign one value to all components.
    const int4 & operator= (int a);

    /// Assignment from another int4
    const int4 & operator= (int4 other) ;

    /// Component access (set)
    int& operator[] (int i) ;

    /// Component access (get)
    int operator[] (int i) const;

    value_t x () const;
    value_t y () const;
    value_t z () const;
    value_t w () const;
    void set_x (value_t val);
    void set_y (value_t val);
    void set_z (value_t val);
    void set_w (value_t val);

    /// Helper: load a single int into all components
    void load (int a);

    /// Helper: load separate values into each component.
    void load (int a, int b, int c, int d);

    /// Load from an array of 4 values
    void load (const int *values);

    void load (const int *values, int n) ;

    /// Load from an array of 4 unsigned short values, convert to int4
    void load (const unsigned short *values) ;

    /// Load from an array of 4 unsigned short values, convert to int4
    void load (const short *values);

    /// Load from an array of 4 unsigned char values, convert to int4
    void load (const unsigned char *values);

    /// Load from an array of 4 unsigned char values, convert to int4
    void load (const char *values);

    /// Store the values into memory
    void store (int *values) const;

    /// Store the first n values into memory
    void store (int *values, int n) const;

    /// Store the least significant 16 bits of each element into adjacent
    /// unsigned shorts.
    void store (unsigned short *values) const;

    /// Store the least significant 8 bits of each element into adjacent
    /// unsigned chars.
    void store (unsigned char *values) const;

    // Arithmetic operators (component-by-component)
    friend int4 operator+ (const int4& a, const int4& b);
    const int4 & operator+= (const int4& a);
    int4 operator- () const ;
    friend int4 operator- (const int4& a, const int4& b);
    const int4 & operator-= (const int4& a) ;
    friend int4 operator* (const int4& a, const int4& b);
    const int4 & operator*= (const int4& a);
    const int4 & operator*= (int val);
    friend int4 operator/ (const int4& a, const int4& b);
    const int4 & operator/= (const int4& a);
    const int4 & operator/= (int val);
    friend int4 operator% (const int4& a, const int4& b);
    const int4 & operator%= (const int4& a);
    friend int4 operator% (const int4& a, int w);
    const int4 & operator%= (int a);
    friend int4 operator% (int a, const int4& b);

    // Bitwise operators (component-by-component)
    friend int4 operator& (const int4& a, const int4& b);
    const int4 & operator&= (const int4& a);
    friend int4 operator| (const int4& a, const int4& b);
    const int4 & operator|= (const int4& a);
    friend int4 operator^ (const int4& a, const int4& b);
    const int4 & operator^= (const int4& a);
    int4 operator~ ();
    int4 operator<< (const unsigned int bits) const;
    const int4 & operator<<= (const unsigned int bits);
    int4 operator>> (const unsigned int bits);
    const int4 & operator>>= (const unsigned int bits);

    // Shift right logical -- unsigned shift. This differs from operator>>
    // in how it handles the sign bit.  (1<<31) >> 1 == (1<<31), but
    // srl((1<<31),1) == 1<<30.
    friend int4 srl (const int4& val, const unsigned int bits);

    // Comparison operators (component-by-component)
    friend mask4 operator== (const int4& a, const int4& b);
    friend mask4 operator!= (const int4& a, const int4& b);
    friend mask4 operator< (const int4& a, const int4& b);
    friend mask4 operator>  (const int4& a, const int4& b);
    friend mask4 operator>= (const int4& a, const int4& b);
    friend mask4 operator<= (const int4& a, const int4& b);

    /// Stream output
    friend inline std::ostream& operator<< (std::ostream& cout, const int4& val);

private:
    // The actual data representation
    union {
        simd_t  m_vec;
        value_t m_val[4];
    };
};



/// Helper: shuffle/swizzle with constant (templated) indices.
/// Example: shuffle<1,1,2,2>(mask4(a,b,c,d)) returns (b,b,c,c)
template<int i0, int i1, int i2, int i3> int4 shuffle (const int4& a);

/// shuffle<i>(a) is the same as shuffle<i,i,i,i>(a)
template<int i> int4 shuffle (const int4& a);

/// Helper: as rapid as possible extraction of one component, when the
/// index is fixed.
template<int i> int extract (const int4& v);

/// Helper: substitute val for a[i]
template<int i> int4 insert (const int4& a, int val);

/// The sum of all components, returned in all components.
int4 vreduce_add (const int4& v);

/// The sum of all components, returned as a scalar.
int reduce_add (const int4& v);

/// Bitwise "and" of all components.
int reduce_and (const int4& v);

/// Bitwise "or" of all components.
int reduce_or (const int4& v);

/// Use a mask to select between components of a (if mask[i] is false) and
/// b (if mask[i] is true).
int4 blend (const int4& a, const int4& b, const mask4& mask);

/// Use a mask to select between components of a (if mask[i] is true) or
/// 0 (if mask[i] is true).
int4 blend0 (const int4& a, const mask4& mask);

/// Use a mask to select between components of a (if mask[i] is FALSE) or
/// 0 (if mask[i] is TRUE).
int4 blend0not (const int4& a, const mask4& mask);

/// Select 'a' where mask is true, 'b' where mask is false. Sure, it's a
/// synonym for blend with arguments rearranged, but this is more clear
/// because the arguments are symmetric to scalar (cond ? a : b).
int4 select (const mask4& mask, const int4& a, const int4& b);

/// Per-element absolute value.
int4 abs (const int4& a);

/// Per-element min
int4 min (const int4& a, const int4& b);

/// Per-element max
int4 max (const int4& a, const int4& b);

// Circular bit rotate by k bits, for 4 values at once.
int4 rotl32 (const int4& x, const unsigned int k);

/// andnot(a,b) returns ((~a) & b)
int4 andnot (const int4& a, const int4& b);

/// Bitcast back and forth int4 (not a convert -- move the bits!)
int4 bitcast_to_int4 (const mask4& x);
int4 bitcast_to_int4 (const float4& x);
float4 bitcast_to_float4 (const int4& x);

void transpose (int4 &a, int4 &b, int4 &c, int4 &d);
void transpose (const int4& a, const int4& b, const int4& c, const int4& d,
                int4 &r0, int4 &r1, int4 &r2, int4 &r3);

int4 AxBxCxDx (const int4& a, const int4& b, const int4& c, const int4& d);




/// Floating point 4-vector, accelerated by SIMD instructions when
/// available.
class float4 {
public:
    static const char* type_name() { return "float4"; }
    typedef float value_t;    ///< Underlying equivalent scalar value type
    typedef mask4 mask_t;     ///< SIMD mask type
    enum { elements = 4 };    ///< Number of scalar elements
    enum { paddedelements = 4 }; ///< Number of scalar elements for full pad
    enum { bits = 128 };      ///< Total number of bits
    typedef simd_raw_t<float,4>::type simd_t;  ///< the native SIMD type used

    /// Default constructor (contents undefined)
    float4 () { }

    /// Destructor
    ~float4 () { }

    /// Construct from a single value (store it in all slots)
    float4 (float a) { load(a); }

    /// Construct from 3 or 4 values
    float4 (float a, float b, float c, float d=0.0f) { load(a,b,c,d); }

    /// Construct from a pointer to 4 values
    float4 (const float *f) { load (f); }

    /// Copy construct from another float4
    float4 (const float4 &other) { m_vec = other.m_vec; }

    /// Construct from an int4 (promoting all components to float)
    explicit float4 (const int4& ival);

    /// Construct from the underlying SIMD type
    float4 (const simd_t& m) : m_vec(m) { }

    /// Return the raw SIMD type
    operator simd_t () const { return m_vec; }
    simd_t simd () const { return m_vec; }

    /// Construct from a Imath::V3f
    float4 (const Imath::V3f &v) { load (v[0], v[1], v[2]); }

    /// Cast to a Imath::V3f
    const Imath::V3f& V3f () const { return *(const Imath::V3f*)this; }

#if defined(ILMBASE_VERSION_MAJOR) && ILMBASE_VERSION_MAJOR >= 2
    // V4f is not defined for older Ilmbase. It's certainly safe for 2.x.

    /// Construct from a Imath::V4f
    float4 (const Imath::V4f &v) { load ((const float *)&v); }

    /// Cast to a Imath::V4f
    const Imath::V4f& V4f () const { return *(const Imath::V4f*)this; }
#endif

    /// Construct from a pointer to 4 unsigned short values
    explicit float4 (const unsigned short *vals) { load(vals); }

    /// Construct from a pointer to 4 short values
    explicit float4 (const short *vals) { load(vals); }

    /// Construct from a pointer to 4 unsigned char values
    explicit float4 (const unsigned char *vals) { load(vals); }

    /// Construct from a pointer to 4 char values
    explicit float4 (const char *vals) { load(vals); }

#ifdef _HALF_H_
    /// Construct from a pointer to 4 half (16 bit float) values
    explicit float4 (const half *vals) { load(vals); }
#endif

    /// Assign a single value to all components
    const float4 & operator= (float a) { load(a); return *this; }

    /// Assign a float4
    const float4 & operator= (float4 other) {
        m_vec = other.m_vec;
        return *this;
    }

    /// Return a float4 with all components set to 0.0
    static const float4 Zero ();

    /// Return a float4 with all components set to 1.0
    static const float4 One ();

    /// Return a float4 with incremented components (e.g., 0.0,1.0,2.0,3.0).
    /// Optional argument can give a non-zero starting point.
    static const float4 Iota (float value=0.0f);
    /// Set all components to 0.0
    void clear ();

#if defined(ILMBASE_VERSION_MAJOR) && ILMBASE_VERSION_MAJOR >= 2
    /// Assign from a Imath::V4f
    const float4 & operator= (const Imath::V4f &v);
#endif

    /// Assign from a Imath::V3f
    const float4 & operator= (const Imath::V3f &v);

    /// Component access (set)
    float& operator[] (int i);
    /// Component access (get)
    float operator[] (int i) const;

    value_t x () const;
    value_t y () const;
    value_t z () const;
    value_t w () const;
    void set_x (value_t val);
    void set_y (value_t val);
    void set_z (value_t val);
    void set_w (value_t val);

    /// Helper: load a single value into all components
    void load (float val);

    /// Helper: load 3 or 4 values. (If 3 are supplied, the 4th will be 0.)
    void load (float a, float b, float c, float d=0.0f);

    /// Load from an array of 4 values
    void load (const float *values);

    /// Load from a partial array of <=4 values. Unassigned values are
    /// undefined.
    void load (const float *values, int n);

    /// Load from an array of 4 unsigned short values, convert to float
    void load (const unsigned short *values);

    /// Load from an array of 4 short values, convert to float
    void load (const short *values);

    /// Load from an array of 4 unsigned char values, convert to float
    void load (const unsigned char *values);

    /// Load from an array of 4 char values, convert to float
    void load (const char *values);

#ifdef _HALF_H_
    /// Load from an array of 4 half values, convert to float
    void load (const half *values);
#endif /* _HALF_H_ */

    void store (float *values) const;

    /// Store the first n values into memory
    void store (float *values, int n) const;

#ifdef _HALF_H_
    void store (half *values) const;
#endif

    // Arithmetic operators
    friend float4 operator+ (const float4& a, const float4& b);
    const float4 & operator+= (const float4& a);
    float4 operator- () const;
    friend float4 operator- (const float4& a, const float4& b);
    const float4 & operator-= (const float4& a);
    friend float4 operator* (const float4& a, const float4& b);
    const float4 & operator*= (const float4& a);
    const float4 & operator*= (float val);
    friend float4 operator/ (const float4& a, const float4& b);
    const float4 & operator/= (const float4& a);
    const float4 & operator/= (float val);

    // Comparison operations
    friend mask_t operator== (const float4& a, const float4& b);
    friend mask_t operator!= (const float4& a, const float4& b);
    friend mask_t operator< (const float4& a, const float4& b);
    friend mask_t operator>  (const float4& a, const float4& b);
    friend mask_t operator>= (const float4& a, const float4& b);
    friend mask_t operator<= (const float4& a, const float4& b);

    // Some oddball items that are handy

    /// Combine the first two components of A with the first two components
    /// of B.
    friend float4 AxyBxy (const float4& a, const float4& b);

    /// Combine the first two components of A with the first two components
    /// of B, but interleaved.
    friend float4 AxBxAyBy (const float4& a, const float4& b);

    /// Return xyz components, plus 0 for w
    float4 xyz0 () const;

    /// Return xyz components, plus 1 for w
    float4 xyz1 () const;

    /// Stream output
    friend inline std::ostream& operator<< (std::ostream& cout, const float4& val);

protected:
    // The actual data representation
    union {
        simd_t  m_vec;
        value_t m_val[paddedelements];
    };
};


/// Helper: shuffle/swizzle with constant (templated) indices.
/// Example: shuffle<1,1,2,2>(mask4(a,b,c,d)) returns (b,b,c,c)
template<int i0, int i1, int i2, int i3> float4 shuffle (const float4& a);

/// shuffle<i>(a) is the same as shuffle<i,i,i,i>(a)
template<int i> float4 shuffle (const float4& a);

/// Helper: as rapid as possible extraction of one component, when the
/// index is fixed.
template<int i> float extract (const float4& a);

/// Helper: substitute val for a[i]
template<int i> float4 insert (const float4& a, float val);


/// The sum of all components, returned in all components.
float4 vreduce_add (const float4& v);

/// The sum of all components, returned as a scalar.
float reduce_add (const float4& v);

/// Return the float dot (inner) product of a and b in every component.
float4 vdot (const float4 &a, const float4 &b);

/// Return the float dot (inner) product of a and b.
float dot (const float4 &a, const float4 &b);

/// Return the float 3-component dot (inner) product of a and b in
/// all components.
float4 vdot3 (const float4 &a, const float4 &b);

/// Return the float 3-component dot (inner) product of a and b.
float dot3 (const float4 &a, const float4 &b);

/// Use a mask to select between components of a (if mask[i] is false) and
/// b (if mask[i] is true).
float4 blend (const float4& a, const float4& b, const mask4& mask);

/// Use a mask to select between components of a (if mask[i] is true) or
/// 0 (if mask[i] is true).
float4 blend0 (const float4& a, const mask4& mask);

/// Use a mask to select between components of a (if mask[i] is FALSE) or
/// 0 (if mask[i] is TRUE).
float4 blend0not (const float4& a, const mask4& mask);

/// "Safe" divide of float4/float4 -- for any component of the divisor
/// that is 0, return 0 rather than Inf.
float4 safe_div (const float4 &a, const float4 &b);

/// Homogeneous divide to turn a float4 into a float3.
float3 hdiv (const float4 &a);

/// Select 'a' where mask is true, 'b' where mask is false. Sure, it's a
/// synonym for blend with arguments rearranged, but this is more clear
/// because the arguments are symmetric to scalar (cond ? a : b).
float4 select (const mask4& mask, const float4& a, const float4& b);

// Per-element math
float4 abs (const float4& a);    ///< absolute value (float)
float4 sign (const float4& a);   ///< 1.0 when value >= 0, -1 when negative
float4 ceil (const float4& a);
float4 floor (const float4& a);
int4 floori (const float4& a);    ///< (int)floor

/// Per-element round to nearest integer (rounding away from 0 in cases
/// that are exactly half way).
float4 round (const float4& a);

/// Per-element round to nearest integer (rounding away from 0 in cases
/// that are exactly half way).
int4 rint (const float4& a);

float4 sqrt (const float4 &a);
float4 rsqrt (const float4 &a);   ///< Fully accurate 1/sqrt
float4 rsqrt_fast (const float4 &a);  ///< Fast, approximate 1/sqrt
float4 exp (const float4& v);
float4 log (const float4& v);
float4 min (const float4& a, const float4& b); ///< Per-element min
float4 max (const float4& a, const float4& b); ///< Per-element max

/// andnot(a,b) returns ((~a) & b)
float4 andnot (const float4& a, const float4& b);

// Fused multiply and add (or subtract):
float4 madd (const float4& a, const float4& b, const float4& c); // a*b + c
float4 msub (const float4& a, const float4& b, const float4& c); // a*b - c
float4 nmadd (const float4& a, const float4& b, const float4& c); // -a*b + c
float4 nmsub (const float4& a, const float4& b, const float4& c); // -a*b - c

/// Transpose the rows and columns of the 4x4 matrix [a b c d].
/// In the end, a will have the original (a[0], b[0], c[0], d[0]),
/// b will have the original (a[1], b[1], c[1], d[1]), and so on.
void transpose (float4 &a, float4 &b, float4 &c, float4 &d);
void transpose (const float4& a, const float4& b, const float4& c, const float4& d,
                float4 &r0, float4 &r1, float4 &r2, float4 &r3);

/// Make a float4 consisting of the first element of each of 4 float4's.
float4 AxBxCxDx (const float4& a, const float4& b,
                 const float4& c, const float4& d);



/// Floating point 3-vector, aligned to be internally identical to a float4.
/// The way it differs from float4 is that all of he load functions only
/// load three values, and all the stores only store 3 values. The vast
/// majority of ops just fall back to the float4 version, and so will
/// operate on the 4th component, but we won't care about that results.
class float3 : public float4 {
public:
    static const char* type_name() { return "float3"; }
    enum { elements = 3 };    ///< Number of scalar elements
    enum { paddedelements = 4 }; ///< Number of scalar elements for full pad

    /// Default constructor (contents undefined)
    float3 () { }

    /// Destructor
    ~float3 () { }

    /// Construct from a single value (store it in all slots)
    float3 (float a) { load(a); }

    /// Construct from 3 or 4 values
    float3 (float a, float b, float c) { float4::load(a,b,c); }

    /// Construct from a pointer to 4 values
    float3 (const float *f) { load (f); }

    /// Copy construct from another float3
    float3 (const float3 &other);

    explicit float3 (const float4 &other);

#if OIIO_SIMD
    /// Construct from the underlying SIMD type
    explicit float3 (const simd_t& m) : float4(m) { }
#endif

    /// Construct from a Imath::V3f
    float3 (const Imath::V3f &v) : float4(v) { }

    /// Cast to a Imath::V3f
    const Imath::V3f& V3f () const { return *(const Imath::V3f*)this; }

    /// Construct from a pointer to 4 unsigned short values
    explicit float3 (const unsigned short *vals) { load(vals); }

    /// Construct from a pointer to 4 short values
    explicit float3 (const short *vals) { load(vals); }

    /// Construct from a pointer to 4 unsigned char values
    explicit float3 (const unsigned char *vals) { load(vals); }

    /// Construct from a pointer to 4 char values
    explicit float3 (const char *vals) { load(vals); }

#ifdef _HALF_H_
    /// Construct from a pointer to 4 half (16 bit float) values
    explicit float3 (const half *vals) { load(vals); }
#endif

    /// Assign a single value to all components
    const float3 & operator= (float a) { load(a); return *this; }

    /// Return a float3 with all components set to 0.0
    static const float3 Zero ();

    /// Return a float3 with all components set to 1.0
    static const float3 One ();

    /// Return a float3 with incremented components (e.g., 0.0,1.0,2.0,3.0).
    /// Optional argument can give a non-zero starting point.
    static const float3 Iota (float value=0.0f);

    /// Helper: load a single value into all components
    void load (float val);

    /// Load from an array of 4 values
    void load (const float *values);

    /// Load from an array of 4 values
    void load (const float *values, int n);

    /// Load from an array of 4 unsigned short values, convert to float
    void load (const unsigned short *values);

    /// Load from an array of 4 short values, convert to float
    void load (const short *values);

    /// Load from an array of 4 unsigned char values, convert to float
    void load (const unsigned char *values);

    /// Load from an array of 4 char values, convert to float
    void load (const char *values);

#ifdef _HALF_H_
    /// Load from an array of 4 half values, convert to float
    void load (const half *values);
#endif /* _HALF_H_ */

    void store (float *values) const;

    void store (float *values, int n) const;

#ifdef _HALF_H_
    void store (half *values) const;
#endif

    /// Store into an Imath::V3f reference.
    void store (Imath::V3f &vec) const;

    // Math operators -- define in terms of float3.
    friend float3 operator+ (const float3& a, const float3& b);
    const float3 & operator+= (const float3& a);
    float3 operator- () const;
    friend float3 operator- (const float3& a, const float3& b);
    const float3 & operator-= (const float3& a);
    friend float3 operator* (const float3& a, const float3& b);
    const float3 & operator*= (const float3& a);
    const float3 & operator*= (float a);
    friend float3 operator/ (const float3& a, const float3& b);
    const float3 & operator/= (const float3& a);
    const float3 & operator/= (float a);

    float3 normalized () const;
    float3 normalized_fast () const;

    /// Stream output
    friend inline std::ostream& operator<< (std::ostream& cout, const float3& val);
};




/// SIMD-based 4x4 matrix. This is guaranteed to have memory layout (when
/// not in registers) isomorphic to Imath::M44f.
class matrix44 {
public:
    // Uninitialized
    OIIO_FORCEINLINE matrix44 ()
#ifndef OIIO_SIMD_SSE
        : m_mat(Imath::UNINITIALIZED)
#endif
    { }

    /// Construct from a reference to an Imath::M44f
    OIIO_FORCEINLINE matrix44 (const Imath::M44f &M) {
#if OIIO_SIMD_SSE
        m_row[0].load (M[0]);
        m_row[1].load (M[1]);
        m_row[2].load (M[2]);
        m_row[3].load (M[3]);
#else
        m_mat = M;
#endif
    }

    /// Construct from a float array
    OIIO_FORCEINLINE explicit matrix44 (const float *f) {
#if OIIO_SIMD_SSE
        m_row[0].load (f+0);
        m_row[1].load (f+4);
        m_row[2].load (f+8);
        m_row[3].load (f+12);
#else
        memcpy (&m_mat, f, 16*sizeof(float));
#endif
    }

    /// Construct from 4 float4 rows
    OIIO_FORCEINLINE explicit matrix44 (const float4& a, const float4& b,
                                        const float4& c, const float4& d) {
#if OIIO_SIMD_SSE
        m_row[0] = a; m_row[1] = b; m_row[2] = c; m_row[3] = d;
#else
        a.store (m_mat[0]);
        b.store (m_mat[1]);
        c.store (m_mat[2]);
        d.store (m_mat[3]);
#endif
    }
    /// Construct from 4 float[4] rows
    OIIO_FORCEINLINE explicit matrix44 (const float *a, const float *b,
                                        const float *c, const float *d) {
#if OIIO_SIMD_SSE
        m_row[0].load(a); m_row[1].load(b); m_row[2].load(c); m_row[3].load(d);
#else
        memcpy (m_mat[0], a, 4*sizeof(float));
        memcpy (m_mat[1], b, 4*sizeof(float));
        memcpy (m_mat[2], c, 4*sizeof(float));
        memcpy (m_mat[3], d, 4*sizeof(float));
#endif
    }

    /// Present as an Imath::M44f
    const Imath::M44f& M44f() const;

    /// Return one row
    float4 operator[] (int i) const;

    /// Return the transposed matrix
    matrix44 transposed () const;

    /// Transform 3-point V by 4x4 matrix M.
    float3 transformp (const float3 &V) const;

    /// Transform 3-vector V by 4x4 matrix M.
    float3 transformv (const float3 &V) const;

    /// Transform 3-vector V by the transpose of 4x4 matrix M.
    float3 transformvT (const float3 &V) const;

    bool operator== (const matrix44& m) const;

    bool operator== (const Imath::M44f& m) const ;
    friend bool operator== (const Imath::M44f& a, const matrix44 &b);

    bool operator!= (const matrix44& m) const;

    bool operator!= (const Imath::M44f& m) const;
    friend bool operator!= (const Imath::M44f& a, const matrix44 &b);

    /// Return the inverse of the matrix.
    matrix44 inverse() const;

    /// Stream output
    friend inline std::ostream& operator<< (std::ostream& cout, const matrix44 &M);

private:
#if OIIO_SIMD_SSE
    float4 m_row[4];
#else
    Imath::M44f m_mat;
#endif
};

/// Transform 3-point V by 4x4 matrix M.
float3 transformp (const matrix44 &M, const float3 &V);
float3 transformp (const Imath::M44f &M, const float3 &V);

/// Transform 3-vector V by 4x4 matrix M.
float3 transformv (const matrix44 &M, const float3 &V);
float3 transformv (const Imath::M44f &M, const float3 &V);

// Transform 3-vector by the transpose of 4x4 matrix M.
float3 transformvT (const matrix44 &M, const float3 &V);
float3 transformvT (const Imath::M44f &M, const float3 &V);






//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//
// Gory implementation details follow.
//
// ^^^ All declarations and documention is above ^^^
//
// vvv Below is the implementation, often considerably cluttered with
//     #if's for each architeture, and unapologitic use of intrinsics and
//     every manner of dirty trick we can think of to make things fast.
//     Some of this isn't pretty. We won't recapitulate comments or
//     documentation of what the functions are supposed to do, please
//     consult the declarations above for that.
//
//     Here be dragons.
//
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////



//////////////////////////////////////////////////////////////////////
// mask4 implementation


OIIO_FORCEINLINE void mask4::clear () {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_setzero_ps();
#else
    *this = false;
#endif
}

OIIO_FORCEINLINE const mask4 mask4::False () {
#if defined(OIIO_SIMD_SSE)
    return _mm_setzero_ps();
#else
    return mask4(false);
#endif
}

OIIO_FORCEINLINE const mask4 mask4::True () {
#if defined(OIIO_SIMD_SSE)
    // Fastest way to fill an __m128 with all 1 bits is to cmpeq_epi8
    // any value to itself.
# if defined(OIIO_SIMD_AVX) && (OIIO_GNUC_VERSION > 50000)
    __m128i anyval = _mm_undefined_si128();
# else
    __m128i anyval = _mm_castps_si128 (_mm_setzero_ps());
# endif
    return _mm_castsi128_ps (_mm_cmpeq_epi8 (anyval, anyval));
#else
    return mask4(true);
#endif
}

OIIO_FORCEINLINE const mask4 & mask4::operator= (bool a) { load(a); return *this; }

OIIO_FORCEINLINE const mask4 & mask4::operator= (const mask4 & other) {
#if defined(OIIO_SIMD_SSE)
    m_vec = other.m_vec;
#else
    SIMD_CONSTRUCT (other.m_val[i]);
#endif
    return *this;
}

OIIO_FORCEINLINE bool mask4::operator[] (int i) const {
    DASSERT(i >= 0 && i < 4);
#if defined(OIIO_SIMD_SSE)
    return (_mm_movemask_ps(m_vec) >> i) & 1;
#else
    return bool(m_val[i]);
#endif
}

OIIO_FORCEINLINE int& mask4::operator[] (int i) {
    DASSERT(i >= 0 && i < 4);
    return m_val[i];
}

OIIO_FORCEINLINE void mask4::load (bool a) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_castsi128_ps(_mm_set1_epi32(a ? -1 : 0));
#else
    int val = a ? -1 : 0;
    SIMD_CONSTRUCT (val);
#endif
}

OIIO_FORCEINLINE void mask4::load (bool a, bool b, bool c, bool d) {
#if defined(OIIO_SIMD_SSE)
    // N.B. -- we need to reverse the order because of our convention
    // of storing a,b,c,d in the same order in memory.
    m_vec = _mm_castsi128_ps(_mm_set_epi32(d ? -1 : 0,
                                           c ? -1 : 0,
                                           b ? -1 : 0,
                                           a ? -1 : 0));
#else
    m_val[0] = a ? -1 : 0;
    m_val[1] = b ? -1 : 0;
    m_val[2] = c ? -1 : 0;
    m_val[3] = d ? -1 : 0;
#endif
}

OIIO_FORCEINLINE void mask4::store (bool *values) const {
#if 0 && defined(OIIO_SIMD_SSE)
    // FIXME: is there a more efficient way to do this?
#else
    SIMD_DO (values[i] = m_val[i] ? true : false);
#endif
}

OIIO_FORCEINLINE void mask4::store (bool *values, int n) const {
    DASSERT (n >= 0 && n <= elements);
    for (int i = 0; i < n; ++i)
        values[i] = m_val[i] ? true : false;
}

OIIO_FORCEINLINE mask4 operator! (const mask4 & a) {
#if defined(OIIO_SIMD_SSE)
    return _mm_xor_ps (a.m_vec, mask4::True());
#else
    SIMD_RETURN (mask4, a.m_val[i] ^ (-1));
#endif
}

OIIO_FORCEINLINE mask4 operator& (const mask4 & a, const mask4 & b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_and_ps (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (mask4, a.m_val[i] & b.m_val[i]);
#endif
}

OIIO_FORCEINLINE const mask4& mask4::operator&= (const mask4 & b) {
    return *this = *this & b;
}

OIIO_FORCEINLINE mask4 operator| (const mask4 & a, const mask4 & b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_or_ps (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (mask4, a.m_val[i] | b.m_val[i]);
#endif
}

OIIO_FORCEINLINE const mask4& mask4::operator|= (const mask4 & a) {
    return *this = *this | a;
}

OIIO_FORCEINLINE mask4 operator^ (const mask4& a, const mask4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_xor_ps (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (mask4, a.m_val[i] ^ b.m_val[i]);
#endif
}

OIIO_FORCEINLINE const mask4 & mask4::operator^= (const mask4& a) {
    return *this = *this ^ a;
}

OIIO_FORCEINLINE mask4 mask4::operator~ () {
#if defined(OIIO_SIMD_SSE)
    // Fastest way to bit-complement in SSE is to xor with 0xffffffff.
    // Fastest way to fill an __m128 with all 1 bits is to cmpeq_epi8
    // any value to itself.
# if defined(OIIO_SIMD_AVX) && (OIIO_GNUC_VERSION > 50000)
    __m128i anyval = _mm_undefined_si128(); // AVX only, sigh
# else
    __m128i anyval = _mm_castps_si128 (m_vec);
# endif
    __m128 all_one_bits = _mm_castsi128_ps (_mm_cmpeq_epi8 (anyval, anyval));
    return _mm_xor_ps (m_vec, all_one_bits);
#else
    SIMD_RETURN (mask4, ~m_val[i]);
#endif
}

OIIO_FORCEINLINE const mask4 operator== (const mask4 & a, const mask4 & b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_castsi128_ps (_mm_cmpeq_epi32 (_mm_castps_si128 (a.m_vec), _mm_castps_si128(b.m_vec)));
#else
    SIMD_RETURN (mask4, a.m_val[i] == b.m_val[i] ? -1 : 0);
#endif
}

OIIO_FORCEINLINE const mask4 operator!= (const mask4 & a, const mask4 & b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_xor_ps (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (mask4, a.m_val[i] != b.m_val[i] ? -1 : 0);
#endif
}

inline std::ostream& operator<< (std::ostream& cout, const mask4 & a) {
    cout << a[0];
    for (int i = 1; i < a.elements; ++i)
        cout << ' ' << a[i];
    return cout;
}



#if defined(OIIO_SIMD_SSE)
// Shuffling. Use like this:  x = shuffle<3,2,1,0>(b)
template<int i0, int i1, int i2, int i3>
OIIO_FORCEINLINE __m128i shuffle_sse (__m128i v) {
    return _mm_shuffle_epi32(v, _MM_SHUFFLE(i3, i2, i1, i0));
}
#endif

#if OIIO_SIMD_SSE >= 3
// SSE3 has intrinsics for a few special cases
template<> OIIO_FORCEINLINE __m128i shuffle_sse<0, 0, 2, 2> (__m128i a) {
    return _mm_castps_si128(_mm_moveldup_ps(_mm_castsi128_ps(a)));
}
template<> OIIO_FORCEINLINE __m128i shuffle_sse<1, 1, 3, 3> (__m128i a) {
    return _mm_castps_si128(_mm_movehdup_ps(_mm_castsi128_ps(a)));
}
template<> OIIO_FORCEINLINE __m128i shuffle_sse<0, 1, 0, 1> (__m128i a) {
    return _mm_castpd_si128(_mm_movedup_pd(_mm_castsi128_pd(a)));
}
#endif

#if defined(OIIO_SIMD_SSE)
template<int i0, int i1, int i2, int i3>
OIIO_FORCEINLINE __m128 shuffle_sse (__m128 a) {
    return _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(a), _MM_SHUFFLE(i3, i2, i1, i0)));
}
#endif

#if OIIO_SIMD_SSE >= 3
// SSE3 has intrinsics for a few special cases
template<> OIIO_FORCEINLINE __m128 shuffle_sse<0, 0, 2, 2> (__m128 a) {
    return _mm_moveldup_ps(a);
}
template<> OIIO_FORCEINLINE __m128 shuffle_sse<1, 1, 3, 3> (__m128 a) {
    return _mm_movehdup_ps(a);
}
template<> OIIO_FORCEINLINE __m128 shuffle_sse<0, 1, 0, 1> (__m128 a) {
    return _mm_castpd_ps(_mm_movedup_pd(_mm_castps_pd(a)));
}
#endif


/// Helper: shuffle/swizzle with constant (templated) indices.
/// Example: shuffle<1,1,2,2>(mask4(a,b,c,d)) returns (b,b,c,c)
template<int i0, int i1, int i2, int i3>
OIIO_FORCEINLINE mask4 shuffle (const mask4& a) {
#if defined(OIIO_SIMD_SSE)
    return shuffle_sse<i0,i1,i2,i3> (a.simd());
#else
    return mask4(a[i0], a[i1], a[i2], a[i3]);
#endif
}

/// shuffle<i>(a) is the same as shuffle<i,i,i,i>(a)
template<int i> OIIO_FORCEINLINE mask4 shuffle (const mask4& a) { return shuffle<i,i,i,i>(a); }


/// Helper: as rapid as possible extraction of one component, when the
/// index is fixed.
template<int i>
OIIO_FORCEINLINE bool extract (const mask4& a) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4
    return _mm_extract_epi32(_mm_castps_si128(a.simd()), i);  // SSE4.1 only
#else
    return a[i];
#endif
}

/// Helper: substitute val for a[i]
template<int i>
OIIO_FORCEINLINE mask4 insert (const mask4& a, bool val) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4
    int ival = val ? -1 : 0;
    return _mm_castsi128_ps (_mm_insert_epi32 (_mm_castps_si128(a), ival, i));
#else
    mask4 tmp = a;
    tmp[i] = val ? -1 : 0;
    return tmp;
#endif
}


OIIO_FORCEINLINE bool reduce_and (const mask4& v) {
#if defined(OIIO_SIMD_SSE)
    return _mm_movemask_ps(v.simd()) == 0xf;
#else
    return v[0] & v[1] & v[2] & v[3];
#endif
}


OIIO_FORCEINLINE bool reduce_or (const mask4& v) {
#if defined(OIIO_SIMD_SSE)
    return _mm_movemask_ps(v) != 0;
#else
    return v[0] | v[1] | v[2] | v[3];
#endif
}



//////////////////////////////////////////////////////////////////////
// int4 implementation


OIIO_FORCEINLINE void int4::clear () {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_setzero_si128();
#else
    *this = 0.0f;
#endif
}

OIIO_FORCEINLINE const int4 int4::Zero () {
#if defined(OIIO_SIMD_SSE)
    return _mm_setzero_si128();
#else
    return int4(0);
#endif
}

OIIO_FORCEINLINE const int4 int4::One () { return int4(1); }

OIIO_FORCEINLINE const int4 int4::NegOne () {
#if defined(OIIO_SIMD_SSE)
    // Fastest way to fill an __m128 with all 1 bits is to cmpeq_epi8
    // any value to itself.
# if defined(OIIO_SIMD_AVX) && (OIIO_GNUC_VERSION > 50000)
    __m128i anyval = _mm_undefined_si128();
# else
    __m128i anyval = _mm_castps_si128 (_mm_setzero_ps());
# endif
    return _mm_cmpeq_epi8 (anyval, anyval);
#else
    return int4(-1);
#endif
}

OIIO_FORCEINLINE const int4 int4::Iota (int value) {
    return int4(value,value+1,value+2,value+3);
}

OIIO_FORCEINLINE const int4 & int4::operator= (int a) { load(a); return *this; }

OIIO_FORCEINLINE const int4 & int4::operator= (int4 other) {
    m_vec = other.m_vec;
    return *this;
}

OIIO_FORCEINLINE int& int4::operator[] (int i) {
    DASSERT(i<elements);
    return m_val[i];
}

OIIO_FORCEINLINE int int4::operator[] (int i) const {
    DASSERT(i<elements);
    return m_val[i];
}


OIIO_FORCEINLINE void int4::load (int a) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_set1_epi32 (a);
#else
    SIMD_CONSTRUCT (a);
#endif
}

OIIO_FORCEINLINE void int4::load (int a, int b, int c, int d) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_set_epi32 (d, c, b, a);
#else
    m_val[0] = a;
    m_val[1] = b;
    m_val[2] = c;
    m_val[3] = d;
#endif
}

OIIO_FORCEINLINE void int4::load (const int *values) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_loadu_si128 ((const simd_t *)values);
#else
    SIMD_CONSTRUCT (values[i]);
#endif
}

OIIO_FORCEINLINE void int4::load (const int *values, int n)
{
#if defined(OIIO_SIMD_SSE)
    switch (n) {
    case 1:
        m_vec = _mm_castps_si128 (_mm_load_ss ((const float *)values));
        break;
    case 2:
            // Trickery: load one double worth of bits!
        m_vec = _mm_castpd_si128 (_mm_load_sd ((const double*)values));
        break;
    case 3:
        // Trickery: load one double worth of bits, then a float,
        // and combine, casting to ints.
        m_vec = _mm_castps_si128 (_mm_movelh_ps(_mm_castpd_ps(_mm_load_sd((const double*)values)),
                                                _mm_load_ss ((const float *)values + 2)));
        break;
    case 4:
        m_vec = _mm_loadu_si128 ((const simd_t *)values);
        break;
    default:
        break;
    }
#endif
    for (int i = 0; i < n; ++i)
        m_val[i] = values[i];
    for (int i = n; i < elements; ++i)
        m_val[i] = 0;
}

OIIO_FORCEINLINE void int4::load (const unsigned short *values) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4
    // Trickery: load one double worth of bits = 4 uint16's!
    simd_t a = _mm_castpd_si128 (_mm_load_sd ((const double *)values));
    m_vec = _mm_cvtepu16_epi32 (a);
#else
    SIMD_CONSTRUCT (values[i]);
#endif
}

OIIO_FORCEINLINE void int4::load (const short *values) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4
    // Trickery: load one double worth of bits = 4 int16's!
    simd_t a = _mm_castpd_si128 (_mm_load_sd ((const double *)values));
    m_vec = _mm_cvtepi16_epi32 (a);
#else
    SIMD_CONSTRUCT (values[i]);
#endif
}

OIIO_FORCEINLINE void int4::load (const unsigned char *values) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4
    // Trickery: load one float worth of bits = 4 uint8's!
    simd_t a = _mm_castps_si128 (_mm_load_ss ((const float *)values));
    m_vec = _mm_cvtepu8_epi32 (a);
#else
    SIMD_CONSTRUCT (values[i]);
#endif
}

OIIO_FORCEINLINE void int4::load (const char *values) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4
    // Trickery: load one float worth of bits = 4 uint8's!
    simd_t a = _mm_castps_si128 (_mm_load_ss ((const float *)values));
    m_vec = _mm_cvtepi8_epi32 (a);
#else
    SIMD_CONSTRUCT (values[i]);
#endif
}

OIIO_FORCEINLINE void int4::store (int *values) const {
#if defined(OIIO_SIMD_SSE)
    // Use an unaligned store -- it's just as fast when the memory turns
    // out to be aligned, nearly as fast even when unaligned. Not worth
    // the headache of using stores that require alignment.
    _mm_storeu_si128 ((simd_t *)values, m_vec);
#else
    SIMD_DO (values[i] = m_val[i]);
#endif
}

OIIO_FORCEINLINE void int4::store (int *values, int n) const {
    DASSERT (n >= 0 && n <= elements);
#if defined(OIIO_SIMD)
    // For full SIMD, there is a speed advantage to storing all components.
    if (n == elements)
        store (values);
    else
#endif
        for (int i = 0; i < n; ++i)
            values[i] = m_val[i];
}

OIIO_FORCEINLINE void int4::store (unsigned short *values) const {
#if defined(OIIO_SIMD_SSE)
    // Expressed as half-words and considering little endianness, we
    // currently have xAxBxCxD (the 'x' means don't care).
    int4 clamped = m_val & int4(0xffff);   // A0B0C0D0
    int4 low = _mm_shufflelo_epi16 (clamped, (0<<0) | (2<<2) | (1<<4) | (1<<6));
                    // low = AB00xxxx
    int4 high = _mm_shufflehi_epi16 (clamped, (1<<0) | (1<<2) | (0<<4) | (2<<6));
                    // high = xxxx00CD
    int4 highswapped = shuffle_sse<2,3,0,1>(high);  // 00CDxxxx
    int4 result = low | highswapped;   // ABCDxxxx
    _mm_storel_pd ((double *)values, _mm_castsi128_pd(result));
    // At this point, values[] should hold A,B,C,D
#else
    SIMD_DO (values[i] = m_val[i]);
#endif
}

OIIO_FORCEINLINE void int4::store (unsigned char *values) const {
#if defined(OIIO_SIMD_SSE)
    // Expressed as bytes and considering little endianness, we
    // currently have xAxBxCxD (the 'x' means don't care).
    int4 clamped = m_val & int4(0xff);            // A000 B000 C000 D000
    int4 swapped = shuffle_sse<1,0,3,2>(clamped); // B000 A000 D000 C000
    int4 shifted = swapped << 8;                  // 0B00 0A00 0D00 0C00
    int4 merged = clamped | shifted;              // AB00 xxxx CD00 xxxx
    int4 merged2 = shuffle_sse<2,2,2,2>(merged);  // CD00 ...
    int4 shifted2 = merged2 << 16;                // 00CD ...
    int4 result = merged | shifted2;              // ABCD ...
    *(int*)values = result[0]; //extract<0>(result);
    // At this point, values[] should hold A,B,C,D
#else
    SIMD_DO (values[i] = m_val[i]);
#endif
}

OIIO_FORCEINLINE int4 operator+ (const int4& a, const int4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_add_epi32 (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (int4, a[i] + b[i]);
#endif
}

OIIO_FORCEINLINE const int4 & int4::operator+= (const int4& a) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_add_epi32 (m_vec, a.m_vec);
#else
    SIMD_DO (m_val[i] += a[i]);
#endif
    return *this;
}

OIIO_FORCEINLINE int4 int4::operator- () const {
#if defined(OIIO_SIMD_SSE)
    return _mm_sub_epi32 (_mm_setzero_si128(), m_vec);
#else
    SIMD_RETURN (int4, -m_val[i]);
#endif
}

OIIO_FORCEINLINE int4 operator- (const int4& a, const int4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_sub_epi32 (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (int4, a[i] - b[i]);
#endif
}

OIIO_FORCEINLINE const int4 & int4::operator-= (const int4& a) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_sub_epi32 (m_vec, a.m_vec);
#else
    SIMD_DO (m_val[i] -= a[i]);
#endif
        return *this;
}


#if defined(OIIO_SIMD_SSE)
// Shamelessly lifted from Syrah which lifted from Manta which lifted it
// from intel.com
OIIO_FORCEINLINE __m128i mm_mul_epi32 (__m128i a, __m128i b) {
#if OIIO_SIMD_SSE >= 4  /* SSE >= 4.1 */
    return _mm_mullo_epi32(a, b);
#else
    // Prior to SSE 4.1, there is no _mm_mullo_epi32 instruction, so we have
    // to fake it.
    __m128i t0;
    __m128i t1;
    t0 = _mm_mul_epu32 (a, b);
    t1 = _mm_mul_epu32 (_mm_shuffle_epi32 (a, 0xB1),
                        _mm_shuffle_epi32 (b, 0xB1));
    t0 = _mm_shuffle_epi32 (t0, 0xD8);
    t1 = _mm_shuffle_epi32 (t1, 0xD8);
    return _mm_unpacklo_epi32 (t0, t1);
#endif
}
#endif


OIIO_FORCEINLINE int4 operator* (const int4& a, const int4& b) {
#if defined(OIIO_SIMD_SSE)
    return mm_mul_epi32 (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (int4, a[i] * b[i]);
#endif
}

OIIO_FORCEINLINE const int4 & int4::operator*= (const int4& a) {
#if defined(OIIO_SIMD_SSE)
    m_vec = mm_mul_epi32 (m_vec, a.m_vec);
#else
    SIMD_DO (m_val[i] *= a[i]);
#endif
    return *this;
}

OIIO_FORCEINLINE const int4 & int4::operator*= (int val) {
#if defined(OIIO_SIMD_SSE)
    m_vec = mm_mul_epi32 (m_vec, _mm_set1_epi32(val));
#else
    SIMD_DO (m_val[i] *= val);
#endif
    return *this;
}

OIIO_FORCEINLINE int4 operator/ (const int4& a, const int4& b) {
    // NO INTEGER DIVISION IN SSE!
    SIMD_RETURN (int4, a[i] / b[i]);
}

OIIO_FORCEINLINE const int4 & int4::operator/= (const int4& a) {
    // NO INTEGER DIVISION IN SSE!
    SIMD_DO (m_val[i] /= a[i]);
    return *this;
}

OIIO_FORCEINLINE const int4 & int4::operator/= (int val) {
    // NO INTEGER DIVISION IN SSE!
    SIMD_DO (m_val[i] /= val);
    return *this;
}

OIIO_FORCEINLINE int4 operator% (const int4& a, const int4& b) {
    // NO INTEGER MODULUS in SSE!
    SIMD_RETURN (int4, a[i] % b[i]);
    }

OIIO_FORCEINLINE const int4 & int4::operator%= (const int4& a) {
    // NO INTEGER MODULUS in SSE!
    SIMD_DO (m_val[i] %= a[i]);
    return *this;
}

OIIO_FORCEINLINE int4 operator% (const int4& a, int w) {
    // NO INTEGER MODULUS in SSE!
    SIMD_RETURN (int4, a[i] % w);
}

OIIO_FORCEINLINE const int4 & int4::operator%= (int a) {
    // NO INTEGER MODULUS IN SSE!
    SIMD_DO (m_val[i] %= a);
    return *this;
}

OIIO_FORCEINLINE int4 operator% (int a, const int4& b) {
    // NO INTEGER MODULUS in SSE!
    SIMD_RETURN (int4, a % b[i]);
}


OIIO_FORCEINLINE int4 operator& (const int4& a, const int4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_and_si128 (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (int4, a[i] & b[i]);
#endif
}

OIIO_FORCEINLINE const int4 & int4::operator&= (const int4& a) {
    return *this = *this & a;
}


OIIO_FORCEINLINE int4 operator| (const int4& a, const int4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_or_si128 (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (int4, a[i] | b[i]);
#endif
}

OIIO_FORCEINLINE const int4 & int4::operator|= (const int4& a) {
    return *this = *this | a;
}

OIIO_FORCEINLINE int4 operator^ (const int4& a, const int4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_xor_si128 (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (int4, a[i] ^ b[i]);
#endif
}

OIIO_FORCEINLINE const int4 & int4::operator^= (const int4& a) {
    return *this = *this ^ a;
}

OIIO_FORCEINLINE int4 int4::operator~ () {
#if defined(OIIO_SIMD_SSE)
    // Fastest way to bit-complement in SSE is to xor with 0xffffffff.
    // Fastest way to fill an __m128 with all 1 bits is to cmpeq_epi8
    // any value to itself.
# if defined(OIIO_SIMD_AVX) && (OIIO_GNUC_VERSION > 50000)
    __m128i anyval = _mm_undefined_si128(); // AVX only, sigh
    __m128i all_one_bits = _mm_cmpeq_epi8 (anyval, anyval);
# else
    __m128i all_one_bits = _mm_cmpeq_epi8 (m_vec, m_vec);
# endif
    return _mm_xor_si128 (m_vec, all_one_bits);
#else
    SIMD_RETURN (int4, ~m_val[i]);
#endif
}

OIIO_FORCEINLINE int4 int4::operator<< (const unsigned int bits) const {
#if defined(OIIO_SIMD_SSE)
    return _mm_slli_epi32 (m_vec, bits);
#else
    SIMD_RETURN (int4, m_val[i] << bits);
#endif
}

OIIO_FORCEINLINE const int4 & int4::operator<<= (const unsigned int bits) {
    return *this = *this << bits;
}

OIIO_FORCEINLINE int4 int4::operator>> (const unsigned int bits) {
#if defined(OIIO_SIMD_SSE)
    return _mm_srai_epi32 (m_vec, bits);
#else
    SIMD_RETURN (int4, m_val[i] >> bits);
#endif
}

OIIO_FORCEINLINE const int4 & int4::operator>>= (const unsigned int bits) {
    return *this = *this >> bits;
}

OIIO_FORCEINLINE int4 srl (const int4& val, const unsigned int bits) {
#if defined(OIIO_SIMD_SSE)
    return _mm_srli_epi32 (val.m_vec, bits);
#else
    SIMD_RETURN (int4, int ((unsigned int)(val[i]) >> bits));
#endif
}


OIIO_FORCEINLINE mask4 operator== (const int4& a, const int4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_castsi128_ps(_mm_cmpeq_epi32 (a.m_vec, b.m_vec));
#else
    SIMD_RETURN (mask4, a[i] == b[i] ? -1 : 0);
#endif
}

OIIO_FORCEINLINE mask4 operator!= (const int4& a, const int4& b) {
        return ! (a == b);
}

OIIO_FORCEINLINE mask4 operator< (const int4& a, const int4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_castsi128_ps(_mm_cmplt_epi32 (a.m_vec, b.m_vec));
#else
    SIMD_RETURN (mask4, a[i] < b[i] ? -1 : 0);
#endif
}

OIIO_FORCEINLINE mask4 operator>  (const int4& a, const int4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_castsi128_ps(_mm_cmpgt_epi32 (a.m_vec, b.m_vec));
#else
    SIMD_RETURN (mask4, a[i] > b[i] ? -1 : 0);
#endif
}

OIIO_FORCEINLINE mask4 operator>= (const int4& a, const int4& b) {
    return !(a < b);
}

OIIO_FORCEINLINE mask4 operator<= (const int4& a, const int4& b) {
    return !(a > b);
}

inline std::ostream& operator<< (std::ostream& cout, const int4& val) {
    cout << val[0];
    for (int i = 1; i < val.elements; ++i)
        cout << ' ' << val[i];
    return cout;
}



template<int i0, int i1, int i2, int i3>
OIIO_FORCEINLINE int4 shuffle (const int4& a) {
#if defined(OIIO_SIMD_SSE)
    return shuffle_sse<i0,i1,i2,i3> (__m128i(a));
#else
    return int4(a[i0], a[i1], a[i2], a[i3]);
#endif
}

template<int i> OIIO_FORCEINLINE int4 shuffle (const int4& a) { return shuffle<i,i,i,i>(a); }


template<int i>
OIIO_FORCEINLINE int extract (const int4& v) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4
    return _mm_extract_epi32(v.simd(), i);  // SSE4.1 only
#else
    return v[i];
#endif
}


template<int i>
OIIO_FORCEINLINE int4 insert (const int4& a, int val) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4
    return _mm_insert_epi32 (a, val, i);
#else
    int4 tmp = a;
    tmp[i] = val;
    return tmp;
#endif
}



OIIO_FORCEINLINE int int4::x () const { return extract<0>(*this); }
OIIO_FORCEINLINE int int4::y () const { return extract<1>(*this); }
OIIO_FORCEINLINE int int4::z () const { return extract<2>(*this); }
OIIO_FORCEINLINE int int4::w () const { return extract<3>(*this); }
OIIO_FORCEINLINE void int4::set_x (int val) { *this = insert<0>(*this, val); }
OIIO_FORCEINLINE void int4::set_y (int val) { *this = insert<1>(*this, val); }
OIIO_FORCEINLINE void int4::set_z (int val) { *this = insert<2>(*this, val); }
OIIO_FORCEINLINE void int4::set_w (int val) { *this = insert<3>(*this, val); }


OIIO_FORCEINLINE int4 bitcast_to_int4 (const mask4& x)
{
#if defined(OIIO_SIMD_SSE)
    return _mm_castps_si128 (x.simd());
#else
    return *(int4 *)&x;
#endif
}


OIIO_FORCEINLINE int4 vreduce_add (const int4& v) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 3
    // People seem to agree that SSE3 does add reduction best with 2
    // horizontal adds.
    // suppose v = (a, b, c, d)
    simd::int4 ab_cd = _mm_hadd_epi32 (v.simd(), v.simd());
    // ab_cd = (a+b, c+d, a+b, c+d)
    simd::int4 abcd = _mm_hadd_epi32 (ab_cd.simd(), ab_cd.simd());
    // all abcd elements are a+b+c+d, return an element as fast as possible
    return abcd;
#elif defined(OIIO_SIMD_SSE)
    // I think this is the best we can do for SSE2, and I'm still not sure
    // it's faster than the default scalar operation. But anyway...
    // suppose v = (a, b, c, d)
    int4 ab_ab_cd_cd = shuffle<1,0,3,2>(v) + v;
    // ab_ab_cd_cd = (b,a,d,c) + (a,b,c,d) = (a+b,a+b,c+d,c+d)
    int4 cd_cd_ab_ab = shuffle<2,3,0,1>(ab_ab_cd_cd);
    // cd_cd_ab_ab = (c+d,c+d,a+b,a+b)
    int4 abcd = ab_ab_cd_cd + cd_cd_ab_ab;   // a+b+c+d in all components
    return abcd;
#else
    return int4(v[0] + v[1] + v[2] + v[3]);
#endif
}


OIIO_FORCEINLINE int reduce_add (const int4& v) {
#if defined(OIIO_SIMD_SSE)
    return _mm_cvtsi128_si32(vreduce_add(v));
#else
    return v[0] + v[1] + v[2] + v[3];
#endif
}


OIIO_FORCEINLINE int reduce_and (const int4& v) {
#if defined(OIIO_SIMD_SSE)
    // I think this is the best we can do for SSE, and I'm still not sure
    // it's faster than the default scalar operation. But anyway...
    // suppose v = (a, b, c, d)
    int4 x = shuffle<1,0,3,2>(v) & v;
    return extract<0>(x) & extract<2>(x);
#else
    return v[0] & v[1] & v[2] & v[3];
#endif
}


OIIO_FORCEINLINE int reduce_or (const int4& v) {
#if defined(OIIO_SIMD_SSE)
    // I think this is the best we can do for SSE, and I'm still not sure
    // it's faster than the default scalar operation. But anyway...
    // suppose v = (a, b, c, d)
    int4 x = shuffle<1,0,3,2>(v) | v;
    return extract<0>(x) | extract<2>(x);
#else
    return v[0] | v[1] | v[2] | v[3];
#endif
}

OIIO_FORCEINLINE int4 blend (const int4& a, const int4& b, const mask4& mask)
{
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4 /* SSE >= 4.1 */
    return _mm_blendv_epi8 (a.simd(), b.simd(), _mm_castps_si128(mask));
#elif defined(OIIO_SIMD_SSE) /* SSE2 */
    return _mm_or_si128 (_mm_and_si128(_mm_castps_si128(mask.simd()), b.simd()),
                         _mm_andnot_si128(_mm_castps_si128(mask.simd()), a.simd()));
#else
    SIMD_RETURN (int4, mask[i] ? b[i] : a[i]);
#endif
}



OIIO_FORCEINLINE int4 blend0 (const int4& a, const mask4& mask)
{
#if defined(OIIO_SIMD_SSE)
    return _mm_and_si128(_mm_castps_si128(mask), a.simd());
#else
    SIMD_RETURN (int4, mask[i] ? a[i] : 0.0f);
#endif
}



OIIO_FORCEINLINE int4 blend0not (const int4& a, const mask4& mask)
{
#if defined(OIIO_SIMD_SSE)
    return _mm_andnot_si128(_mm_castps_si128(mask), a.simd());
#else
    SIMD_RETURN (int4, mask[i] ? 0.0f : a[i]);
#endif
}


OIIO_FORCEINLINE int4 select (const mask4& mask, const int4& a, const int4& b)
{
    return blend (b, a, mask);
}



OIIO_FORCEINLINE int4 abs (const int4& a)
{
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 3
    return _mm_abs_epi32(a.simd());
#else
    return int4 (std::abs(a[0]), std::abs(a[1]), std::abs(a[2]), std::abs(a[3]));
#endif
}

OIIO_FORCEINLINE int4 min (const int4& a, const int4& b)
{
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4 /* SSE >= 4.1 */
    return _mm_min_epi32 (a, b);
#else
    SIMD_RETURN (int4, std::min (a[i], b[i]));
#endif
}

OIIO_FORCEINLINE int4 max (const int4& a, const int4& b)
{
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4 /* SSE >= 4.1 */
    return _mm_max_epi32 (a, b);
#else
    SIMD_RETURN (int4, std::max (a[i], b[i]));
#endif
}


OIIO_FORCEINLINE int4 rotl32 (const int4& x, const unsigned int k)
{
    return (x<<k) | srl(x,32-k);
}



OIIO_FORCEINLINE int4 andnot (const int4& a, const int4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_andnot_si128 (a.simd(), b.simd());
#else
    SIMD_RETURN (int4, ~(a[i]) & b[i]);
#endif
}


// Implementation had to be after the definition of int4.
OIIO_FORCEINLINE mask4::mask4 (const int4& ival)
{
#if defined(OIIO_SIMD_SSE)
    m_vec = (ival != int4::Zero());
#else
    SIMD_CONSTRUCT (ival[i] ? -1 : 0);
#endif
}




//////////////////////////////////////////////////////////////////////
// float4 implementation


OIIO_FORCEINLINE float4::float4 (const int4& ival) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_cvtepi32_ps (ival.simd());
#else
    SIMD_CONSTRUCT (float(ival[i]));
#endif
}


OIIO_FORCEINLINE const float4 float4::Zero () {
#if defined(OIIO_SIMD_SSE)
    return _mm_setzero_ps();
#else
    return float4(0.0f);
#endif
}

OIIO_FORCEINLINE const float4 float4::One () {
    return float4(1.0f);
}

OIIO_FORCEINLINE const float4 float4::Iota (float value) {
    return float4(value,value+1.0f,value+2.0f,value+3.0f);
}

    /// Set all components to 0.0
OIIO_FORCEINLINE void float4::clear () {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_setzero_ps();
#else
    load (0.0f);
#endif
}

#if defined(ILMBASE_VERSION_MAJOR) && ILMBASE_VERSION_MAJOR >= 2
OIIO_FORCEINLINE const float4 & float4::operator= (const Imath::V4f &v) {
    load ((const float *)&v);
    return *this;
}
#endif

OIIO_FORCEINLINE const float4 & float4::operator= (const Imath::V3f &v) {
    load (v[0], v[1], v[2], 0.0f);
    return *this;
}

OIIO_FORCEINLINE float& float4::operator[] (int i) {
    DASSERT(i<elements);
    return m_val[i];
}

OIIO_FORCEINLINE float float4::operator[] (int i) const {
    DASSERT(i<elements);
    return m_val[i];
}


OIIO_FORCEINLINE void float4::load (float val) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_set1_ps (val);
#elif defined(OIIO_SIMD_NEON)
    m_vec = vdupq_n_f32 (val);
#else
    SIMD_CONSTRUCT (val);
#endif
}

OIIO_FORCEINLINE void float4::load (float a, float b, float c, float d) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_set_ps (d, c, b, a);
#elif defined(OIIO_SIMD_NEON)
    float values[4] = { a, b, c, d };
    m_vec = vld1q_f32 (values);
#else
    m_val[0] = a;
    m_val[1] = b;
    m_val[2] = c;
    m_val[3] = d;
#endif
}

    /// Load from an array of 4 values
OIIO_FORCEINLINE void float4::load (const float *values) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_loadu_ps (values);
#elif defined(OIIO_SIMD_NEON)
    m_vec = vld1q_f32 (values);
#else
    SIMD_CONSTRUCT (values[i]);
#endif
}

    /// Load from a partial array of <=4 values. Unassigned values are
    /// undefined.
OIIO_FORCEINLINE void float4::load (const float *values, int n) {
#if defined(OIIO_SIMD_SSE)
    switch (n) {
    case 1:
        m_vec = _mm_load_ss (values);
        break;
    case 2:
        // Trickery: load one double worth of bits!
        m_vec = _mm_castpd_ps (_mm_load_sd ((const double*)values));
        break;
    case 3:
        m_vec = _mm_setr_ps (values[0], values[1], values[2], 0.0f);
        // This looks wasteful, but benchmarks show that it's the
        // fastest way to set 3 values with the 4th getting zero.
        // Actually, gcc and clang both turn it into something more
        // efficient than _mm_setr_ps. The version below looks smart,
        // but was much more expensive as the _mm_setr_ps!
        //   __m128 xy = _mm_castsi128_ps(_mm_loadl_epi64((const __m128i*)values));
        //   m_vec = _mm_movelh_ps(xy, _mm_load_ss (values + 2));
        break;
    case 4:
        m_vec = _mm_loadu_ps (values);
        break;
    default:
        break;
    }
#elif defined(OIIO_SIMD_NEON)
    switch (n) {
    case 1: m_vec = vdupq_n_f32 (val);                    break;
    case 2: load (values[0], values[1], 0.0f, 0.0f);      break;
    case 3: load (values[0], values[1], values[2], 0.0f); break;
    case 4: m_vec = vld1q_f32 (values);                   break;
    default: break;
    }
#else
    for (int i = 0; i < n; ++i)
        m_val[i] = values[i];
    for (int i = n; i < paddedelements; ++i)
        m_val[i] = 0;
#endif
}

    /// Load from an array of 4 unsigned short values, convert to float
OIIO_FORCEINLINE void float4::load (const unsigned short *values) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 2
    m_vec = _mm_cvtepi32_ps (int4(values).simd());
    // You might guess that the following is faster, but it's NOT:
    //   NO!  m_vec = _mm_cvtpu16_ps (*(__m64*)values);
#else
    SIMD_CONSTRUCT (values[i]);
#endif
}

    /// Load from an array of 4 short values, convert to float
OIIO_FORCEINLINE void float4::load (const short *values) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 2
    m_vec = _mm_cvtepi32_ps (int4(values).simd());
#else
    SIMD_CONSTRUCT (values[i]);
#endif
}

    /// Load from an array of 4 unsigned char values, convert to float
OIIO_FORCEINLINE void float4::load (const unsigned char *values) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 2
    m_vec = _mm_cvtepi32_ps (int4(values).simd());
#else
    SIMD_CONSTRUCT (values[i]);
#endif
}

    /// Load from an array of 4 char values, convert to float
OIIO_FORCEINLINE void float4::load (const char *values) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 2
    m_vec = _mm_cvtepi32_ps (int4(values).simd());
#else
    SIMD_CONSTRUCT (values[i]);
#endif
}

#ifdef _HALF_H_
OIIO_FORCEINLINE void float4::load (const half *values) {
#if defined(__F16C__) && defined(OIIO_SIMD_SSE)
    /* Enabled 16 bit float instructions! */
    __m128i a = _mm_castpd_si128 (_mm_load_sd ((const double *)values));
    m_vec = _mm_cvtph_ps (a);
#elif defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 2
    // SSE half-to-float by Fabian "ryg" Giesen. Public domain.
    // https://gist.github.com/rygorous/2144712
    int4 h ((const unsigned short *)values);
# define CONSTI(name) *(const __m128i *)&name
# define CONSTF(name) *(const __m128 *)&name
    OIIO_SIMD_UINT4_CONST(mask_nosign, 0x7fff);
    OIIO_SIMD_UINT4_CONST(magic,       (254 - 15) << 23);
    OIIO_SIMD_UINT4_CONST(was_infnan,  0x7bff);
    OIIO_SIMD_UINT4_CONST(exp_infnan,  255 << 23);
    __m128i mnosign     = CONSTI(mask_nosign);
    __m128i expmant     = _mm_and_si128(mnosign, h);
    __m128i justsign    = _mm_xor_si128(h, expmant);
    __m128i expmant2    = expmant; // copy (just here for counting purposes)
    __m128i shifted     = _mm_slli_epi32(expmant, 13);
    __m128  scaled      = _mm_mul_ps(_mm_castsi128_ps(shifted), *(const __m128 *)&magic);
    __m128i b_wasinfnan = _mm_cmpgt_epi32(expmant2, CONSTI(was_infnan));
    __m128i sign        = _mm_slli_epi32(justsign, 16);
    __m128  infnanexp   = _mm_and_ps(_mm_castsi128_ps(b_wasinfnan), CONSTF(exp_infnan));
    __m128  sign_inf    = _mm_or_ps(_mm_castsi128_ps(sign), infnanexp);
    __m128  final       = _mm_or_ps(scaled, sign_inf);
    // ~11 SSE2 ops.
    m_vec = final;
# undef CONSTI
# undef CONSTF
#else /* No SIMD defined: */
    SIMD_CONSTRUCT (values[i]);
#endif
}
#endif /* _HALF_H_ */

OIIO_FORCEINLINE void float4::store (float *values) const {
#if defined(OIIO_SIMD_SSE)
    // Use an unaligned store -- it's just as fast when the memory turns
    // out to be aligned, nearly as fast even when unaligned. Not worth
    // the headache of using stores that require alignment.
    _mm_storeu_ps (values, m_vec);
#elif defined(OIIO_SIMD_NEON)
    vst1q_f32 (values, m_vec);
#else
    SIMD_DO (values[i] = m_val[i]);
#endif
}

OIIO_FORCEINLINE void float4::store (float *values, int n) const {
    DASSERT (n >= 0 && n <= 4);
#if defined(OIIO_SIMD_SSE)
    switch (n) {
        case 1:
        _mm_store_ss (values, m_vec);
        break;
    case 2:
        // Trickery: store two floats as a double worth of bits
        _mm_store_sd ((double*)values, _mm_castps_pd(m_vec));
        break;
    case 3:
        values[0] = m_val[0];
        values[1] = m_val[1];
        values[2] = m_val[2];
        // This looks wasteful, but benchmarks show that it's the
        // fastest way to store 3 values, in benchmarks was faster than
        // this, below:
        //   _mm_store_sd ((double*)values, _mm_castps_pd(m_vec));
        //   _mm_store_ss (values + 2, _mm_movehl_ps(m_vec,m_vec));
        break;
    case 4:
        store (values);
        break;
    default:
        break;
    }
#elif defined(OIIO_SIMD_NEON)
    switch (n) {
    case 1:
        vst1q_lane_f32 (values, m_vec, 0);
        break;
    case 2:
        vst1q_lane_f32 (values++, m_vec, 0);
        vst1q_lane_f32 (values, m_vec, 1);
        break;
    case 3:
        vst1q_lane_f32 (values++, m_vec, 0);
        vst1q_lane_f32 (values++, m_vec, 1);
        vst1q_lane_f32 (values, m_vec, 2);
        break;
    case 4:
        vst1q_f32 (values, m_vec); break;
    default:
        break;
    }
#else
    for (int i = 0; i < n; ++i)
        values[i] = m_val[i];
#endif
}

#ifdef _HALF_H_
OIIO_FORCEINLINE void float4::store (half *values) const {
#if defined(__F16C__) && defined(OIIO_SIMD_SSE)
    __m128i h = _mm_cvtps_ph (m_vec, (_MM_FROUND_TO_NEAREST_INT |_MM_FROUND_NO_EXC));
    _mm_store_sd ((double *)values, _mm_castsi128_pd(h));
    #else
    SIMD_DO (values[i] = m_val[i]);
#endif
}
#endif

OIIO_FORCEINLINE float4 operator+ (const float4& a, const float4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_add_ps (a.m_vec, b.m_vec);
#elif defined(OIIO_SIMD_NEON)
    return vaddq_f32 (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (float4, a[i] + b[i]);
#endif
}

OIIO_FORCEINLINE const float4 & float4::operator+= (const float4& a) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_add_ps (m_vec, a.m_vec);
#elif defined(OIIO_SIMD_NEON)
    m_vec = vaddq_f32 (m_vec, a.m_vec);
#else
    SIMD_DO (m_val[i] += a[i]);
#endif
    return *this;
    }

OIIO_FORCEINLINE float4 float4::operator- () const {
#if defined(OIIO_SIMD_SSE)
    return _mm_sub_ps (_mm_setzero_ps(), m_vec);
#elif defined(OIIO_SIMD_NEON)
    return vsubq_f32 (Zero(), m_vec);
#else
    SIMD_RETURN (float4, -m_val[i]);
#endif
}

OIIO_FORCEINLINE float4 operator- (const float4& a, const float4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_sub_ps (a.m_vec, b.m_vec);
#elif defined(OIIO_SIMD_NEON)
    return vsubq_f32 (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (float4, a[i] - b[i]);
#endif
}

OIIO_FORCEINLINE const float4 & float4::operator-= (const float4& a) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_sub_ps (m_vec, a.m_vec);
#elif defined(OIIO_SIMD_NEON)
    m_vec = vsubq_f32 (m_vec, a.m_vec);
#else
    SIMD_DO (m_val[i] -= a[i]);
#endif
    return *this;
}

OIIO_FORCEINLINE float4 operator* (const float4& a, const float4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_mul_ps (a.m_vec, b.m_vec);
#elif defined(OIIO_SIMD_NEON)
    return vmulq_f32 (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (float4, a[i] * b[i]);
#endif
}

OIIO_FORCEINLINE const float4 & float4::operator*= (const float4& a) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_mul_ps (m_vec, a.m_vec);
#elif defined(OIIO_SIMD_NEON)
    m_vec = vmulq_f32 (m_vec, a.m_vec);
#else
    SIMD_DO (m_val[i] *= a[i]);
#endif
    return *this;
}

OIIO_FORCEINLINE const float4 & float4::operator*= (float val) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_mul_ps (m_vec, _mm_set1_ps(val));
#elif defined(OIIO_SIMD_NEON)
    m_vec = vmulq_f32 (m_vec, vdupq_n_f32(val));
#else
    SIMD_DO (m_val[i] *= val);
#endif
    return *this;
}

OIIO_FORCEINLINE float4 operator/ (const float4& a, const float4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_div_ps (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (float4, a[i] / b[i]);
#endif
}

OIIO_FORCEINLINE const float4 & float4::operator/= (const float4& a) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_div_ps (m_vec, a.m_vec);
#else
    SIMD_DO (m_val[i] /= a[i]);
#endif
    return *this;
}

OIIO_FORCEINLINE const float4 & float4::operator/= (float val) {
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_div_ps (m_vec, _mm_set1_ps(val));
#else
    SIMD_DO (m_val[i] /= val);
#endif
    return *this;
}

OIIO_FORCEINLINE mask4 operator== (const float4& a, const float4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_cmpeq_ps (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (mask4, a[i] == b[i] ? -1 : 0);
#endif
}

OIIO_FORCEINLINE mask4 operator!= (const float4& a, const float4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_cmpneq_ps (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (mask4, a[i] != b[i] ? -1 : 0);
#endif
}

OIIO_FORCEINLINE mask4 operator< (const float4& a, const float4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_cmplt_ps (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (mask4, a[i] < b[i] ? -1 : 0);
#endif
}

OIIO_FORCEINLINE mask4 operator>  (const float4& a, const float4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_cmpgt_ps (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (mask4, a[i] > b[i] ? -1 : 0);
#endif
}

OIIO_FORCEINLINE mask4 operator>= (const float4& a, const float4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_cmpge_ps (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (mask4, a[i] >= b[i] ? -1 : 0);
#endif
}

OIIO_FORCEINLINE mask4 operator<= (const float4& a, const float4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_cmple_ps (a.m_vec, b.m_vec);
#else
    SIMD_RETURN (mask4, a[i] <= b[i] ? -1 : 0);
#endif
}

OIIO_FORCEINLINE float4 AxyBxy (const float4& a, const float4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_movelh_ps (a.m_vec, b.m_vec);
#else
    return float4 (a[0], a[1], b[0], b[1]);
#endif
}

OIIO_FORCEINLINE float4 AxBxAyBy (const float4& a, const float4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_unpacklo_ps (a.m_vec, b.m_vec);
#else
    return float4 (a[0], b[0], a[1], b[1]);
#endif
}

OIIO_FORCEINLINE float4 float4::xyz0 () const {
    return insert<3>(*this, 0.0f);
}

OIIO_FORCEINLINE float4 float4::xyz1 () const {
    return insert<3>(*this, 1.0f);
}

inline std::ostream& operator<< (std::ostream& cout, const float4& val) {
    cout << val[0];
    for (int i = 1; i < val.elements; ++i)
        cout << ' ' << val[i];
    return cout;
}


// Implementation had to be after the definition of float4.
OIIO_FORCEINLINE int4::int4 (const float4& f)
{
#if defined(OIIO_SIMD_SSE)
    m_vec = _mm_cvttps_epi32(f.simd());
#else
    SIMD_CONSTRUCT ((int) f[i]);
#endif
}



template<int i0, int i1, int i2, int i3>
OIIO_FORCEINLINE float4 shuffle (const float4& a) {
#if defined(OIIO_SIMD_SSE)
    return shuffle_sse<i0,i1,i2,i3> (__m128(a));
#else
    return float4(a[i0], a[i1], a[i2], a[i3]);
#endif
}

template<int i> OIIO_FORCEINLINE float4 shuffle (const float4& a) { return shuffle<i,i,i,i>(a); }

#if defined(OIIO_SIMD_NEON)
template<> OIIO_FORCEINLINE float4 shuffle<0> (const float4& a) {
    float32x2_t t = vget_low_f32(a.m_vec); return vdupq_lane_f32(t,0);
}
template<> OIIO_FORCEINLINE float4 shuffle<1> (const float4& a) {
    float32x2_t t = vget_low_f32(a.m_vec); return vdupq_lane_f32(t,1);
}
template<> OIIO_FORCEINLINE float4 shuffle<2> (const float4& a) {
    float32x2_t t = vget_high_f32(a.m_vec); return vdupq_lane_f32(t,0);
}
template<> OIIO_FORCEINLINE float4 shuffle<3> (const float4& a) {
    float32x2_t t = vget_high_f32(a.m_vec); return vdupq_lane_f32(t,1);
}
#endif



/// Helper: as rapid as possible extraction of one component, when the
/// index is fixed.
template<int i>
OIIO_FORCEINLINE float extract (const float4& a) {
#if defined(OIIO_SIMD_SSE)
    return _mm_cvtss_f32(shuffle_sse<i,i,i,i>(a.simd()));
#else
    return a[i];
#endif
}

#if defined(OIIO_SIMD_SSE)
template<> OIIO_FORCEINLINE float extract<0> (const float4& a) {
    return _mm_cvtss_f32(a.simd());
}
#endif


/// Helper: substitute val for a[i]
template<int i>
OIIO_FORCEINLINE float4 insert (const float4& a, float val) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4
    return _mm_insert_ps (a, _mm_set_ss(val), i<<4);
#else
    float4 tmp = a;
    tmp[i] = val;
    return tmp;
#endif
}

#if defined(OIIO_SIMD_SSE)
// Slightly faster special cases for SSE
template<> OIIO_FORCEINLINE float4 insert<0> (const float4& a, float val) {
    return _mm_move_ss (a.simd(), _mm_set_ss(val));
}
#endif


OIIO_FORCEINLINE float float4::x () const { return extract<0>(*this); }
OIIO_FORCEINLINE float float4::y () const { return extract<1>(*this); }
OIIO_FORCEINLINE float float4::z () const { return extract<2>(*this); }
OIIO_FORCEINLINE float float4::w () const { return extract<3>(*this); }
OIIO_FORCEINLINE void float4::set_x (float val) { *this = insert<0>(*this, val); }
OIIO_FORCEINLINE void float4::set_y (float val) { *this = insert<1>(*this, val); }
OIIO_FORCEINLINE void float4::set_z (float val) { *this = insert<2>(*this, val); }
OIIO_FORCEINLINE void float4::set_w (float val) { *this = insert<3>(*this, val); }


OIIO_FORCEINLINE int4 bitcast_to_int4 (const float4& x)
{
#if defined(OIIO_SIMD_SSE)
    return _mm_castps_si128 (x.simd());
#else
    return *(int4 *)&x;
#endif
}

OIIO_FORCEINLINE float4 bitcast_to_float4 (const int4& x)
{
#if defined(OIIO_SIMD_SSE)
    return _mm_castsi128_ps (x.simd());
#else
    return *(float4 *)&x;
#endif
}


OIIO_FORCEINLINE float4 vreduce_add (const float4& v) {
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 3
    // People seem to agree that SSE3 does add reduction best with 2
    // horizontal adds.
    // suppose v = (a, b, c, d)
    simd::float4 ab_cd = _mm_hadd_ps (v.simd(), v.simd());
    // ab_cd = (a+b, c+d, a+b, c+d)
    simd::float4 abcd = _mm_hadd_ps (ab_cd.simd(), ab_cd.simd());
    // all abcd elements are a+b+c+d
    return abcd;
#elif defined(OIIO_SIMD_SSE)
    // I think this is the best we can do for SSE2, and I'm still not sure
    // it's faster than the default scalar operation. But anyway...
    // suppose v = (a, b, c, d)
    float4 ab_ab_cd_cd = shuffle<1,0,3,2>(v) + v;
    // now x = (b,a,d,c) + (a,b,c,d) = (a+b,a+b,c+d,c+d)
    float4 cd_cd_ab_ab = shuffle<2,3,0,1>(ab_ab_cd_cd);
    // now y = (c+d,c+d,a+b,a+b)
    float4 abcd = ab_ab_cd_cd + cd_cd_ab_ab;   // a+b+c+d in all components
    return abcd;
#else
    return float4 (v[0] + v[1] + v[2] + v[3]);
#endif
}


OIIO_FORCEINLINE float reduce_add (const float4& v) {
#if defined(OIIO_SIMD_SSE)
    return _mm_cvtss_f32(vreduce_add (v));
#else
    return v[0] + v[1] + v[2] + v[3];
#endif
}

OIIO_FORCEINLINE float4 vdot (const float4 &a, const float4 &b) {
#if OIIO_SIMD_SSE >= 4
    return _mm_dp_ps (a.simd(), b.simd(), 0xff);
#elif OIIO_SIMD_NEON
    float32x4_t ab = vmulq_f32(a, b);
    float32x4_t sum1 = vaddq_f32(ab, vrev64q_f32(ab));
    return vaddq_f32(sum1, vcombine_f32(vget_high_f32(sum1), vget_low_f32(sum1)));
#else
    return vreduce_add (a*b);
#endif
}

OIIO_FORCEINLINE float dot (const float4 &a, const float4 &b) {
#if OIIO_SIMD_SSE >= 4
    return _mm_cvtss_f32 (_mm_dp_ps (a.simd(), b.simd(), 0xff));
#else
    return reduce_add (a*b);
#endif
}

OIIO_FORCEINLINE float4 vdot3 (const float4 &a, const float4 &b) {
#if OIIO_SIMD_SSE >= 4
    return _mm_dp_ps (a.simd(), b.simd(), 0x7f);
#else
    return vreduce_add((a*b).xyz0());
#endif
}

OIIO_FORCEINLINE float dot3 (const float4 &a, const float4 &b) {
#if OIIO_SIMD_SSE >= 4
    return _mm_cvtss_f32 (_mm_dp_ps (a.simd(), b.simd(), 0x77));
#else
    return reduce_add ((a*b).xyz0());
#endif
}


OIIO_FORCEINLINE float4 blend (const float4& a, const float4& b, const mask4& mask)
{
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4
    // SSE >= 4.1 only
    return _mm_blendv_ps (a.simd(), b.simd(), mask.simd());
#elif defined(OIIO_SIMD_SSE)
    // Trick for SSE < 4.1
    return _mm_or_ps (_mm_and_ps(mask.simd(), b.simd()),
                      _mm_andnot_ps(mask.simd(), a.simd()));
#else
    return float4 (mask[0] ? b[0] : a[0],
                   mask[1] ? b[1] : a[1],
                   mask[2] ? b[2] : a[2],
                   mask[3] ? b[3] : a[3]);
#endif
}


OIIO_FORCEINLINE float4 blend0 (const float4& a, const mask4& mask)
{
#if defined(OIIO_SIMD_SSE)
    return _mm_and_ps(mask.simd(), a.simd());
#else
    return float4 (mask[0] ? a[0] : 0.0f,
                   mask[1] ? a[1] : 0.0f,
                   mask[2] ? a[2] : 0.0f,
                   mask[3] ? a[3] : 0.0f);
#endif
}


OIIO_FORCEINLINE float4 blend0not (const float4& a, const mask4& mask)
{
#if defined(OIIO_SIMD_SSE)
    return _mm_andnot_ps(mask.simd(), a.simd());
#else
    return float4 (mask[0] ? 0.0f : a[0],
                   mask[1] ? 0.0f : a[1],
                   mask[2] ? 0.0f : a[2],
                   mask[3] ? 0.0f : a[3]);
#endif
}


OIIO_FORCEINLINE float4 safe_div (const float4 &a, const float4 &b) {
#if defined(OIIO_SIMD_SSE)
    return blend0not (a/b, b == float4::Zero());
#else
    return float4 (b[0] == 0.0f ? 0.0f : a[0] / b[0],
                   b[1] == 0.0f ? 0.0f : a[1] / b[1],
                   b[2] == 0.0f ? 0.0f : a[2] / b[2],
                   b[3] == 0.0f ? 0.0f : a[3] / b[3]);
#endif
}


OIIO_FORCEINLINE float3 hdiv (const float4 &a)
{
#if defined(OIIO_SIMD_SSE)
    return float3(safe_div(a, shuffle<3>(a)).xyz0());
#else
    float d = a[3];
    return d == 0.0f ? float3 (0.0f) : float3 (a[0]/d, a[1]/d, a[2]/d);
#endif
}



OIIO_FORCEINLINE float4 select (const mask4& mask, const float4& a, const float4& b)
{
    return blend (b, a, mask);
}


OIIO_FORCEINLINE float4 abs (const float4& a)
{
#if defined(OIIO_SIMD_SSE)
    // Just clear the sign bit for cheap fabsf
    return _mm_and_ps (a.simd(), _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff)));
#else
    return float4 (fabsf(a[0]), fabsf(a[1]), fabsf(a[2]), fabsf(a[3]));
#endif
}


OIIO_FORCEINLINE float4 sign (const float4& a)
{
    float4 one(1.0f);
    return blend (one, -one, a < float4::Zero());
}


OIIO_FORCEINLINE float4 ceil (const float4& a)
{
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4  /* SSE >= 4.1 */
    return _mm_ceil_ps (a);
#else
    return float4 (ceilf(a[0]), ceilf(a[1]), ceilf(a[2]), ceilf(a[3]));
#endif
}

OIIO_FORCEINLINE float4 floor (const float4& a)
{
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4  /* SSE >= 4.1 */
    return _mm_floor_ps (a);
#else
    return float4 (floorf(a[0]), floorf(a[1]), floorf(a[2]), floorf(a[3]));
#endif
}

OIIO_FORCEINLINE float4 round (const float4& a)
{
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4  /* SSE >= 4.1 */
    return _mm_round_ps (a, (_MM_FROUND_TO_NEAREST_INT |_MM_FROUND_NO_EXC));
#else
    return float4 (roundf(a[0]), roundf(a[1]), roundf(a[2]), roundf(a[3]));
#endif
}

OIIO_FORCEINLINE int4 floori (const float4& a)
{
#if defined(OIIO_SIMD_SSE) && OIIO_SIMD_SSE >= 4  /* SSE >= 4.1 */
    return int4(floor(a));
#elif defined(OIIO_SIMD_SSE)   /* SSE2/3 */
    int4 i (a);  // truncates
    int4 isneg = bitcast_to_int4 (a < float4::Zero());
    return i + isneg;
    // The trick here (thanks, Cycles, for letting me spy on your code) is
    // that the comparison will return (int)-1 for components that are less
    // than zero, and adding that is the same as subtracting one!
#else
    return int4 ((int)floorf(a[0]), (int)floorf(a[1]),
                 (int)floorf(a[2]), (int)floorf(a[3]));
#endif
}


OIIO_FORCEINLINE int4 rint (const float4& a)
{
    return int4 (round(a));
}



OIIO_FORCEINLINE float4 sqrt (const float4 &a)
{
#if OIIO_SIMD_SSE
    return _mm_sqrt_ps (a.simd());
#else
    return float4 (sqrtf(a[0]), sqrtf(a[1]), sqrtf(a[2]), sqrtf(a[3]));
#endif
}



OIIO_FORCEINLINE float4 rsqrt (const float4 &a)
{
#if OIIO_SIMD_SSE
    return _mm_div_ps (_mm_set1_ps(1.0f), _mm_sqrt_ps (a.simd()));
#else
    return float4 (1.0f/sqrtf(a[0]), 1.0f/sqrtf(a[1]),
                   1.0f/sqrtf(a[2]), 1.0f/sqrtf(a[3]));
#endif
}



OIIO_FORCEINLINE float4 rsqrt_fast (const float4 &a)
{
#if OIIO_SIMD_SSE
    return _mm_rsqrt_ps (a.simd());
#else
    return float4 (1.0f/sqrtf(a[0]), 1.0f/sqrtf(a[1]),
                   1.0f/sqrtf(a[2]), 1.0f/sqrtf(a[3]));
#endif
}



OIIO_FORCEINLINE float4 min (const float4& a, const float4& b)
{
#if defined(OIIO_SIMD_SSE)
    return _mm_min_ps (a, b);
#else
    return float4 (std::min (a[0], b[0]),
                   std::min (a[1], b[1]),
                   std::min (a[2], b[2]),
                   std::min (a[3], b[3]));
#endif
}

OIIO_FORCEINLINE float4 max (const float4& a, const float4& b)
{
#if defined(OIIO_SIMD_SSE)
    return _mm_max_ps (a, b);
#else
    return float4 (std::max (a[0], b[0]),
                   std::max (a[1], b[1]),
                   std::max (a[2], b[2]),
                   std::max (a[3], b[3]));
#endif
}



OIIO_FORCEINLINE float4 andnot (const float4& a, const float4& b) {
#if defined(OIIO_SIMD_SSE)
    return _mm_andnot_ps (a.simd(), b.simd());
#else
    const int *ai = (const int *)&a;
    const int *bi = (const int *)&b;
    return bitcast_to_float4 (int4(~(ai[0]) & bi[0],
                                   ~(ai[1]) & bi[1],
                                   ~(ai[2]) & bi[2],
                                   ~(ai[3]) & bi[3]));
#endif
}



OIIO_FORCEINLINE float4 madd (const simd::float4& a, const simd::float4& b,
                              const simd::float4& c)
{
#if OIIO_SIMD_SSE && OIIO_FMA_ENABLED
    // If we are sure _mm_fmadd_ps intrinsic is available, use it.
    return _mm_fmadd_ps (a, b, c);
#elif OIIO_SIMD_SSE && !defined(_MSC_VER)
    // If we directly access the underlying __m128, on some platforms and
    // compiler flags, it will turn into fma anyway, even if we don't use
    // the intrinsic.
    return a.simd() * b.simd() + c.simd();
#else
    // Fallback: just use regular math and hope for the best.
    return a * b + c;
#endif
}


OIIO_FORCEINLINE float4 msub (const simd::float4& a, const simd::float4& b,
                              const simd::float4& c)
{
#if OIIO_SIMD_SSE && OIIO_FMA_ENABLED
    // If we are sure _mm_fnmsub_ps intrinsic is available, use it.
    return _mm_fmsub_ps (a, b, c);
#elif OIIO_SIMD_SSE && !defined(_MSC_VER)
    // If we directly access the underlying __m128, on some platforms and
    // compiler flags, it will turn into fma anyway, even if we don't use
    // the intrinsic.
    return a.simd() * b.simd() - c.simd();
#else
    // Fallback: just use regular math and hope for the best.
    return a * b - c;
#endif
}



OIIO_FORCEINLINE float4 nmadd (const simd::float4& a, const simd::float4& b,
                               const simd::float4& c)
{
#if OIIO_SIMD_SSE && OIIO_FMA_ENABLED
    // If we are sure _mm_fnmadd_ps intrinsic is available, use it.
    return _mm_fnmadd_ps (a, b, c);
#elif OIIO_SIMD_SSE && !defined(_MSC_VER)
    // If we directly access the underlying __m128, on some platforms and
    // compiler flags, it will turn into fma anyway, even if we don't use
    // the intrinsic.
    return c.simd() - a.simd() * b.simd();
#else
    // Fallback: just use regular math and hope for the best.
    return c - a * b;
#endif
}



OIIO_FORCEINLINE float4 nmsub (const simd::float4& a, const simd::float4& b,
                               const simd::float4& c)
{
#if OIIO_SIMD_SSE && OIIO_FMA_ENABLED
    // If we are sure _mm_fnmsub_ps intrinsic is available, use it.
    return _mm_fnmsub_ps (a, b, c);
#elif OIIO_SIMD_SSE && !defined(_MSC_VER)
    // If we directly access the underlying __m128, on some platforms and
    // compiler flags, it will turn into fma anyway, even if we don't use
    // the intrinsic.
    return -(a.simd() * b.simd()) - c.simd();
#else
    // Fallback: just use regular math and hope for the best.
    return -(a * b) - c;
#endif
}



// Full precision exp() of all components of a SIMD vector.
OIIO_FORCEINLINE float4 exp (const float4& v)
{
#if OIIO_SIMD_SSE
    // Implementation inspired by:
    // https://github.com/embree/embree/blob/master/common/simd/sse_special.h
    // Which is listed as Copyright (C) 2007  Julien Pommier and distributed
    // under the zlib license.
    float4 x = v;
    OIIO_SIMD_FLOAT4_CONST (exp_hi,  88.3762626647949f);
    OIIO_SIMD_FLOAT4_CONST (exp_lo,  -88.3762626647949f);
    OIIO_SIMD_FLOAT4_CONST (cephes_LOG2EF, 1.44269504088896341f);
    OIIO_SIMD_FLOAT4_CONST (cephes_exp_C1, 0.693359375f);
    OIIO_SIMD_FLOAT4_CONST (cephes_exp_C2, -2.12194440e-4f);
    OIIO_SIMD_FLOAT4_CONST (cephes_exp_p0, 1.9875691500E-4f);
    OIIO_SIMD_FLOAT4_CONST (cephes_exp_p1, 1.3981999507E-3f);
    OIIO_SIMD_FLOAT4_CONST (cephes_exp_p2, 8.3334519073E-3f);
    OIIO_SIMD_FLOAT4_CONST (cephes_exp_p3, 4.1665795894E-2f);
    OIIO_SIMD_FLOAT4_CONST (cephes_exp_p4, 1.6666665459E-1f);
    OIIO_SIMD_FLOAT4_CONST (cephes_exp_p5, 5.0000001201E-1f);
    float4 tmp (0.0f);
    float4 one (1.0f);
    x = min (x, float4(exp_hi));
    x = max (x, float4(exp_lo));
    float4 fx = madd (x, float4(cephes_LOG2EF), float4(0.5f));
    int4 emm0 = int4(fx);
    tmp = float4(emm0);
    float4 mask = bitcast_to_float4 (bitcast_to_int4(tmp > fx) & bitcast_to_int4(one));
    fx = tmp - mask;
    tmp = fx * float4(cephes_exp_C1);
    float4 z = fx * float4(cephes_exp_C2);
    x = x - tmp;
    x = x - z;
    z = x * x;
    float4 y = float4(cephes_exp_p0);
    y = madd (y, x, float4(cephes_exp_p1));
    y = madd (y, x, float4(cephes_exp_p2));
    y = madd (y, x, float4(cephes_exp_p3));
    y = madd (y, x, float4(cephes_exp_p4));
    y = madd (y, x, float4(cephes_exp_p5));
    y = madd (y, z, x);
    y = y + one;
    emm0 = (int4(fx) + int4(0x7f)) << 23;
    float4 pow2n = bitcast_to_float4(emm0);
    y = y * pow2n;
    return y;

#else
    return float4 (expf(v[0]), expf(v[1]), expf(v[2]), expf(v[3]));
#endif
}



// Full precision log() of all components of a SIMD vector.
OIIO_FORCEINLINE float4 log (const float4& v)
{
#if OIIO_SIMD_SSE
    // Implementation inspired by:
    // https://github.com/embree/embree/blob/master/common/simd/sse_special.h
    // Which is listed as Copyright (C) 2007  Julien Pommier and distributed
    // under the zlib license.
    float4 x = v;
    int4 emm0;
    float4 zero (float4::Zero());
    float4 one (1.0f);
    mask4 invalid_mask = (x <= zero);
    OIIO_SIMD_INT4_CONST (min_norm_pos, (int)0x00800000);
    OIIO_SIMD_INT4_CONST (inv_mant_mask, (int)~0x7f800000);
    x = max(x, bitcast_to_float4(int4(min_norm_pos)));  /* cut off denormalized stuff */
    emm0 = srl (bitcast_to_int4(x), 23);
    /* keep only the fractional part */
    x = bitcast_to_float4 (bitcast_to_int4(x) & int4(inv_mant_mask));
    x = bitcast_to_float4 (bitcast_to_int4(x) | bitcast_to_int4(float4(0.5f)));
    emm0 = emm0 - int4(0x7f);
    float4 e (emm0);
    e = e + one;
    OIIO_SIMD_FLOAT4_CONST (cephes_SQRTHF, 0.707106781186547524f);
    mask4 mask = (x < float4(cephes_SQRTHF));
    float4 tmp = bitcast_to_float4 (bitcast_to_int4(x) & bitcast_to_int4(mask));
    x = x - one;
    e = e - bitcast_to_float4 (bitcast_to_int4(one) & bitcast_to_int4(mask));
    x = x + tmp;
    float4 z = x * x;
    OIIO_SIMD_FLOAT4_CONST (cephes_log_p0, 7.0376836292E-2f);
    OIIO_SIMD_FLOAT4_CONST (cephes_log_p1, - 1.1514610310E-1f);
    OIIO_SIMD_FLOAT4_CONST (cephes_log_p2, 1.1676998740E-1f);
    OIIO_SIMD_FLOAT4_CONST (cephes_log_p3, - 1.2420140846E-1f);
    OIIO_SIMD_FLOAT4_CONST (cephes_log_p4, + 1.4249322787E-1f);
    OIIO_SIMD_FLOAT4_CONST (cephes_log_p5, - 1.6668057665E-1f);
    OIIO_SIMD_FLOAT4_CONST (cephes_log_p6, + 2.0000714765E-1f);
    OIIO_SIMD_FLOAT4_CONST (cephes_log_p7, - 2.4999993993E-1f);
    OIIO_SIMD_FLOAT4_CONST (cephes_log_p8, + 3.3333331174E-1f);
    OIIO_SIMD_FLOAT4_CONST (cephes_log_q1, -2.12194440e-4f);
    OIIO_SIMD_FLOAT4_CONST (cephes_log_q2, 0.693359375f);
    float4 y = *(float4*)cephes_log_p0;
    y = madd (y, x, float4(cephes_log_p1));
    y = madd (y, x, float4(cephes_log_p2));
    y = madd (y, x, float4(cephes_log_p3));
    y = madd (y, x, float4(cephes_log_p4));
    y = madd (y, x, float4(cephes_log_p5));
    y = madd (y, x, float4(cephes_log_p6));
    y = madd (y, x, float4(cephes_log_p7));
    y = madd (y, x, float4(cephes_log_p8));
    y = y * x;
    y = y * z;
    y = madd(e, float4(cephes_log_q1), y);
    y = nmadd (z, 0.5f, y);
    x = x + y;
    x = madd (e, float4(cephes_log_q2), x);
    x = bitcast_to_float4 (bitcast_to_int4(x) | bitcast_to_int4(invalid_mask)); // negative arg will be NAN
    return x;
#else
    return float4 (logf(v[0]), logf(v[1]), logf(v[2]), logf(v[3]));
#endif
}



OIIO_FORCEINLINE void transpose (float4 &a, float4 &b, float4 &c, float4 &d)
{
#if defined(OIIO_SIMD_SSE)
    _MM_TRANSPOSE4_PS (a, b, c, d);
#else
    float4 A (a[0], b[0], c[0], d[0]);
    float4 B (a[1], b[1], c[1], d[1]);
    float4 C (a[2], b[2], c[2], d[2]);
    float4 D (a[3], b[3], c[3], d[3]);
    a = A;  b = B;  c = C;  d = D;
#endif
}


OIIO_FORCEINLINE void transpose (const float4& a, const float4& b, const float4& c, const float4& d,
                                 float4 &r0, float4 &r1, float4 &r2, float4 &r3)
{
#if defined(OIIO_SIMD_SSE)
    //_MM_TRANSPOSE4_PS (a, b, c, d);
    float4 l02 = _mm_unpacklo_ps (a, c);
    float4 h02 = _mm_unpackhi_ps (a, c);
    float4 l13 = _mm_unpacklo_ps (b, d);
    float4 h13 = _mm_unpackhi_ps (b, d);
    r0 = _mm_unpacklo_ps (l02, l13);
    r1 = _mm_unpackhi_ps (l02, l13);
    r2 = _mm_unpacklo_ps (h02, h13);
    r3 = _mm_unpackhi_ps (h02, h13);
#else
    r0.load (a[0], b[0], c[0], d[0]);
    r1.load (a[1], b[1], c[1], d[1]);
    r2.load (a[2], b[2], c[2], d[2]);
    r3.load (a[3], b[3], c[3], d[3]);
#endif
}


OIIO_FORCEINLINE void transpose (int4 &a, int4 &b, int4 &c, int4 &d)
{
#if defined(OIIO_SIMD_SSE)
    __m128 A = _mm_castsi128_ps (a);
    __m128 B = _mm_castsi128_ps (b);
    __m128 C = _mm_castsi128_ps (c);
    __m128 D = _mm_castsi128_ps (d);
    _MM_TRANSPOSE4_PS (A, B, C, D);
    a = _mm_castps_si128 (A);
    b = _mm_castps_si128 (B);
    c = _mm_castps_si128 (C);
    d = _mm_castps_si128 (D);
#else
    int4 A (a[0], b[0], c[0], d[0]);
    int4 B (a[1], b[1], c[1], d[1]);
    int4 C (a[2], b[2], c[2], d[2]);
    int4 D (a[3], b[3], c[3], d[3]);
    a = A;  b = B;  c = C;  d = D;
#endif
}

OIIO_FORCEINLINE void transpose (const int4& a, const int4& b, const int4& c, const int4& d,
                                 int4 &r0, int4 &r1, int4 &r2, int4 &r3)
{
#if defined(OIIO_SIMD_SSE)
    //_MM_TRANSPOSE4_PS (a, b, c, d);
    __m128 A = _mm_castsi128_ps (a);
    __m128 B = _mm_castsi128_ps (b);
    __m128 C = _mm_castsi128_ps (c);
    __m128 D = _mm_castsi128_ps (d);
    _MM_TRANSPOSE4_PS (A, B, C, D);
    r0 = _mm_castps_si128 (A);
    r1 = _mm_castps_si128 (B);
    r2 = _mm_castps_si128 (C);
    r3 = _mm_castps_si128 (D);
#else
    r0.load (a[0], b[0], c[0], d[0]);
    r1.load (a[1], b[1], c[1], d[1]);
    r2.load (a[2], b[2], c[2], d[2]);
    r3.load (a[3], b[3], c[3], d[3]);
#endif
}


OIIO_FORCEINLINE float4 AxBxCxDx (const float4& a, const float4& b,
                                  const float4& c, const float4& d)
{
#if defined(OIIO_SIMD_SSE)
    float4 l02 = _mm_unpacklo_ps (a, c);
    float4 l13 = _mm_unpacklo_ps (b, d);
    return _mm_unpacklo_ps (l02, l13);
#else
    return float4 (a[0], b[0], c[0], d[0]);
#endif
}


OIIO_FORCEINLINE int4 AxBxCxDx (const int4& a, const int4& b,
                                const int4& c, const int4& d)
{
#if defined(OIIO_SIMD_SSE)
    int4 l02 = _mm_unpacklo_epi32 (a, c);
    int4 l13 = _mm_unpacklo_epi32 (b, d);
    return _mm_unpacklo_epi32 (l02, l13);
#else
    return int4 (a[0], b[0], c[0], d[0]);
#endif
}



//////////////////////////////////////////////////////////////////////
// float3 implementation

OIIO_FORCEINLINE float3::float3 (const float3 &other) {
#if defined(OIIO_SIMD_SSE) || defined(OIIO_SIMD_NEON)
    m_vec = other.m_vec;
#else
    SIMD_CONSTRUCT_PAD (other[i]);
#endif
}

OIIO_FORCEINLINE float3::float3 (const float4 &other) {
#if defined(OIIO_SIMD_SSE) || defined(OIIO_SIMD_NEON)
    m_vec = other.simd();
#else
    SIMD_CONSTRUCT_PAD (other[i]);
    m_val[3] = 0.0f;
#endif
}

OIIO_FORCEINLINE const float3 float3::Zero () { return float3(float4::Zero()); }

OIIO_FORCEINLINE const float3 float3::One () { return float3(1.0f); }

OIIO_FORCEINLINE const float3 float3::Iota (float value) {
    return float3(value,value+1.0f,value+2.0f);
}


OIIO_FORCEINLINE void float3::load (float val) { float4::load (val, val, val, 0.0f); }

OIIO_FORCEINLINE void float3::load (const float *values) { float4::load (values, 3); }

OIIO_FORCEINLINE void float3::load (const float *values, int n) {
    float4::load (values, n);
}

OIIO_FORCEINLINE void float3::load (const unsigned short *values) {
    float4::load (float(values[0]), float(values[1]), float(values[2]));
}

OIIO_FORCEINLINE void float3::load (const short *values) {
    float4::load (float(values[0]), float(values[1]), float(values[2]));
}

OIIO_FORCEINLINE void float3::load (const unsigned char *values) {
    float4::load (float(values[0]), float(values[1]), float(values[2]));
}

OIIO_FORCEINLINE void float3::load (const char *values) {
    float4::load (float(values[0]), float(values[1]), float(values[2]));
}

#ifdef _HALF_H_
OIIO_FORCEINLINE void float3::load (const half *values) {
    float4::load (float(values[0]), float(values[1]), float(values[2]));
}
#endif /* _HALF_H_ */

OIIO_FORCEINLINE void float3::store (float *values) const {
    float4::store (values, 3);
}

OIIO_FORCEINLINE void float3::store (float *values, int n) const {
    float4::store (values, n);
}

#ifdef _HALF_H_
OIIO_FORCEINLINE void float3::store (half *values) const {
    SIMD_DO (values[i] = m_val[i]);
}
#endif

OIIO_FORCEINLINE void float3::store (Imath::V3f &vec) const {
    store ((float *)&vec);
}

OIIO_FORCEINLINE float3 operator+ (const float3& a, const float3& b) {
    return float3 (float4(a) + float4(b));
}

OIIO_FORCEINLINE const float3 & float3::operator+= (const float3& a) {
    *this = *this + a; return *this;
}

OIIO_FORCEINLINE float3 float3::operator- () const {
    return float3 (-float4(*this));
}

OIIO_FORCEINLINE float3 operator- (const float3& a, const float3& b) {
    return float3 (float4(a) - float4(b));
}

OIIO_FORCEINLINE const float3 & float3::operator-= (const float3& a) {
    *this = *this - a; return *this;
}

OIIO_FORCEINLINE float3 operator* (const float3& a, const float3& b) {
    return float3 (float4(a) * float4(b));
}

OIIO_FORCEINLINE const float3 & float3::operator*= (const float3& a) {
    *this = *this * a; return *this;
}

OIIO_FORCEINLINE const float3 & float3::operator*= (float a) {
    *this = *this * a; return *this;
}

OIIO_FORCEINLINE float3 operator/ (const float3& a, const float3& b) {
    return float3 (float4(a) / b.xyz1()); // Avoid divide by zero!
}

OIIO_FORCEINLINE const float3 & float3::operator/= (const float3& a) {
    *this = *this / a; return *this;
}

OIIO_FORCEINLINE const float3 & float3::operator/= (float a) {
    *this = *this / a; return *this;
}


inline std::ostream& operator<< (std::ostream& cout, const float3& val) {
    cout << val[0];
    for (int i = 1; i < val.elements; ++i)
        cout << ' ' << val[i];
    return cout;
}


OIIO_FORCEINLINE float3 vreduce_add (const float3& v) {
#if defined(OIIO_SIMD_SSE)
    return float3 ((vreduce_add(float4(v))).xyz0());
#else
    return float3 (v[0] + v[1] + v[2]);
#endif
}


OIIO_FORCEINLINE float3 vdot (const float3 &a, const float3 &b) {
#if OIIO_SIMD_SSE >= 4
    return float3(_mm_dp_ps (a.simd(), b.simd(), 0x77));
#else
    return vreduce_add (a*b);
#endif
}


OIIO_FORCEINLINE float dot (const float3 &a, const float3 &b) {
#if OIIO_SIMD_SSE >= 4
    return _mm_cvtss_f32 (_mm_dp_ps (a.simd(), b.simd(), 0x77));
#else
    return reduce_add (a*b);
#endif
}


OIIO_FORCEINLINE float3 vdot3 (const float3 &a, const float3 &b) {
#if OIIO_SIMD_SSE >= 4
    return float3(_mm_dp_ps (a.simd(), b.simd(), 0x77));
#else
    return float3 (vreduce_add((a*b).xyz0()).xyz0());
#endif
}

OIIO_FORCEINLINE float3 float3::normalized () const
{
#if OIIO_SIMD
    float3 len2 = vdot3 (*this, *this);
    return float3 (safe_div (*this, sqrt(len2)));
#else
    float len2 = dot (*this, *this);
    return len2 > 0.0f ? (*this) / sqrtf(len2) : float3::Zero();
#endif
}


OIIO_FORCEINLINE float3 float3::normalized_fast () const
{
#if OIIO_SIMD
    float3 len2 = vdot3 (*this, *this);
    float4 invlen = blend0not (rsqrt_fast (len2), len2 == float4::Zero());
    return float3 ((*this) * invlen);
#else
    float len2 = dot (*this, *this);
    return len2 > 0.0f ? (*this) / sqrtf(len2) : float3::Zero();
#endif
}



//////////////////////////////////////////////////////////////////////
// matrix44 implementation


OIIO_FORCEINLINE const Imath::M44f& matrix44::M44f() const {
    return *(Imath::M44f*)this;
}


OIIO_FORCEINLINE float4 matrix44::operator[] (int i) const {
#if OIIO_SIMD_SSE
    return m_row[i];
#else
    return float4 (m_mat[i]);
#endif
}


OIIO_FORCEINLINE matrix44 matrix44::transposed () const {
    matrix44 T;
#if OIIO_SIMD_SSE
    simd::transpose (m_row[0], m_row[1], m_row[2], m_row[3],
                     T.m_row[0], T.m_row[1], T.m_row[2], T.m_row[3]);
#else
    T = m_mat.transposed();
#endif
    return T;
}

OIIO_FORCEINLINE float3 matrix44::transformp (const float3 &V) const {
#if OIIO_SIMD_SSE
    float4 R = shuffle<0>(V) * m_row[0] + shuffle<1>(V) * m_row[1] +
               shuffle<2>(V) * m_row[2] + m_row[3];
    R = R / shuffle<3>(R);
    return float3 (R.xyz0());
#else
    Imath::V3f R;
    m_mat.multVecMatrix (*(Imath::V3f *)&V, R);
    return float3(R);
#endif
}

OIIO_FORCEINLINE float3 matrix44::transformv (const float3 &V) const {
#if OIIO_SIMD_SSE
    float4 R = shuffle<0>(V) * m_row[0] + shuffle<1>(V) * m_row[1] +
               shuffle<2>(V) * m_row[2];
    return float3 (R.xyz0());
#else
    Imath::V3f R;
    m_mat.multDirMatrix (*(Imath::V3f *)&V, R);
    return float3(R);
#endif
}

OIIO_FORCEINLINE float3 matrix44::transformvT (const float3 &V) const {
#if OIIO_SIMD_SSE
    matrix44 T = transposed();
    float4 R = shuffle<0>(V) * T[0] + shuffle<1>(V) * T[1] +
               shuffle<2>(V) * T[2];
    return float3 (R.xyz0());
#else
    Imath::V3f R;
    m_mat.transposed().multDirMatrix (*(Imath::V3f *)&V, R);
    return float3(R);
#endif
}

OIIO_FORCEINLINE bool matrix44::operator== (const matrix44& m) const {
#if OIIO_SIMD_SSE
    mask4 b0 = (m_row[0] == m[0]);
    mask4 b1 = (m_row[1] == m[1]);
    mask4 b2 = (m_row[2] == m[2]);
    mask4 b3 = (m_row[3] == m[3]);
    return simd::all (b0 & b1 & b2 & b3);
#else
    return memcmp(this, &m, 16*sizeof(float)) == 0;
#endif
}

OIIO_FORCEINLINE bool matrix44::operator== (const Imath::M44f& m) const {
    return memcmp(this, &m, 16*sizeof(float)) == 0;
}

OIIO_FORCEINLINE bool operator== (const Imath::M44f& a, const matrix44 &b) {
    return (b == a);
}

OIIO_FORCEINLINE bool matrix44::operator!= (const matrix44& m) const {
#if OIIO_SIMD_SSE
    mask4 b0 = (m_row[0] != m[0]);
    mask4 b1 = (m_row[1] != m[1]);
    mask4 b2 = (m_row[2] != m[2]);
    mask4 b3 = (m_row[3] != m[3]);
    return simd::any (b0 | b1 | b2 | b3);
#else
    return memcmp(this, &m, 16*sizeof(float)) != 0;
#endif
}

OIIO_FORCEINLINE bool matrix44::operator!= (const Imath::M44f& m) const {
    return memcmp(this, &m, 16*sizeof(float)) != 0;
}

OIIO_FORCEINLINE bool operator!= (const Imath::M44f& a, const matrix44 &b) {
    return (b != a);
}

OIIO_FORCEINLINE matrix44 matrix44::inverse() const {
#if OIIO_SIMD_SSE
    // Adapted from this code from Intel:
    // ftp://download.intel.com/design/pentiumiii/sml/24504301.pdf
    float4 minor0, minor1, minor2, minor3;
    float4 row0, row1, row2, row3;
    float4 det, tmp1;
    const float *src = (const float *)this;
    float4 zero = float4::Zero();
    tmp1 = _mm_loadh_pi(_mm_loadl_pi(zero, (__m64*)(src)), (__m64*)(src+ 4));
    row1 = _mm_loadh_pi(_mm_loadl_pi(zero, (__m64*)(src+8)), (__m64*)(src+12));
    row0 = _mm_shuffle_ps(tmp1, row1, 0x88);
    row1 = _mm_shuffle_ps(row1, tmp1, 0xDD);
    tmp1 = _mm_loadh_pi(_mm_loadl_pi(tmp1, (__m64*)(src+ 2)), (__m64*)(src+ 6));
    row3 = _mm_loadh_pi(_mm_loadl_pi(zero, (__m64*)(src+10)), (__m64*)(src+14));
    row2 = _mm_shuffle_ps(tmp1, row3, 0x88);
    row3 = _mm_shuffle_ps(row3, tmp1, 0xDD);
    // -----------------------------------------------
    tmp1 = row2 * row3;
    tmp1 = shuffle<1,0,3,2>(tmp1);
    minor0 = row1 * tmp1;
    minor1 = row0 * tmp1;
    tmp1 = shuffle<2,3,0,1>(tmp1);
    minor0 = (row1 * tmp1) - minor0;
    minor1 = (row0 * tmp1) - minor1;
    minor1 = shuffle<2,3,0,1>(minor1);
    // -----------------------------------------------
    tmp1 = row1 * row2;
    tmp1 = shuffle<1,0,3,2>(tmp1);
    minor0 = (row3 * tmp1) + minor0;
    minor3 = row0 * tmp1;
    tmp1 = shuffle<2,3,0,1>(tmp1);
    minor0 = minor0 - (row3 * tmp1);
    minor3 = (row0 * tmp1) - minor3;
    minor3 = shuffle<2,3,0,1>(minor3);
    // -----------------------------------------------
    tmp1 = shuffle<2,3,0,1>(row1) * row3;
    tmp1 = shuffle<1,0,3,2>(tmp1);
    row2 = shuffle<2,3,0,1>(row2);
    minor0 = (row2 * tmp1) + minor0;
    minor2 = row0 * tmp1;
    tmp1 = shuffle<2,3,0,1>(tmp1);
    minor0 = minor0 - (row2 * tmp1);
    minor2 = (row0 * tmp1) - minor2;
    minor2 = shuffle<2,3,0,1>(minor2);
    // -----------------------------------------------
    tmp1 = row0 * row1;
    tmp1 = shuffle<1,0,3,2>(tmp1);
    minor2 = (row3 * tmp1) + minor2;
    minor3 = (row2 * tmp1) - minor3;
    tmp1 = shuffle<2,3,0,1>(tmp1);
    minor2 = (row3 * tmp1) - minor2;
    minor3 = minor3 - (row2 * tmp1);
    // -----------------------------------------------
    tmp1 = row0 * row3;
    tmp1 = shuffle<1,0,3,2>(tmp1);
    minor1 = minor1 - (row2 * tmp1);
    minor2 = (row1 * tmp1) + minor2;
    tmp1 = shuffle<2,3,0,1>(tmp1);
    minor1 = (row2 * tmp1) + minor1;
    minor2 = minor2 - (row1 * tmp1);
    // -----------------------------------------------
    tmp1 = row0 * row2;
    tmp1 = shuffle<1,0,3,2>(tmp1);
    minor1 = (row3 * tmp1) + minor1;
    minor3 = minor3 - (row1 * tmp1);
    tmp1 = shuffle<2,3,0,1>(tmp1);
    minor1 = minor1 - (row3 * tmp1);
    minor3 = (row1 * tmp1) + minor3;
    // -----------------------------------------------
    det = row0 * minor0;
    det = shuffle<2,3,0,1>(det) + det;
    det = _mm_add_ss(shuffle<1,0,3,2>(det), det);
    tmp1 = _mm_rcp_ss(det);
    det = _mm_sub_ss(_mm_add_ss(tmp1, tmp1), _mm_mul_ss(det, _mm_mul_ss(tmp1, tmp1)));
    det = shuffle<0>(det);
    return matrix44 (det*minor0, det*minor1, det*minor2, det*minor3);
#else
    return m_mat.inverse();
#endif
}


inline std::ostream& operator<< (std::ostream& cout, const matrix44 &M) {
    const float *m = (const float *)&M;
    cout << m[0];
    for (int i = 1; i < 16; ++i)
        cout << ' ' << m[i];
    return cout;
}



OIIO_FORCEINLINE float3 transformp (const matrix44 &M, const float3 &V) {
    return M.transformp (V);
}

OIIO_FORCEINLINE float3 transformp (const Imath::M44f &M, const float3 &V)
{
#if OIIO_SIMD
    return matrix44(M).transformp (V);
#else
    Imath::V3f R;
    M.multVecMatrix (*(Imath::V3f *)&V, R);
    return float3(R);
#endif
}


OIIO_FORCEINLINE float3 transformv (const matrix44 &M, const float3 &V) {
    return M.transformv (V);
}

OIIO_FORCEINLINE float3 transformv (const Imath::M44f &M, const float3 &V)
{
#if OIIO_SIMD
    return matrix44(M).transformv (V);
#else
    Imath::V3f R;
    M.multDirMatrix (*(Imath::V3f *)&V, R);
    return float3(R);
#endif
}

OIIO_FORCEINLINE float3 transformvT (const matrix44 &M, const float3 &V)
{
    return M.transformvT (V);
}

OIIO_FORCEINLINE float3 transformvT (const Imath::M44f &M, const float3 &V)
{
#if OIIO_SIMD
    return matrix44(M).transformvT(V);
#else
    return transformv (M.transposed(), V);
#endif
}



} // end namespace simd

OIIO_NAMESPACE_END


#undef SIMD_DO
#undef SIMD_CONSTRUCT
#undef SIMD_CONSTRUCT_PAD
#undef SIMD_RETURN

#endif /* OIIO_SIMD_H */
