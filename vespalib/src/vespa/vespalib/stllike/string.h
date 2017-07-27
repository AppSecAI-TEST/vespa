// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#pragma once

#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <vespa/vespalib/util/alloc.h>

namespace vespalib {

/**
 * This class holds a reference to an external chunk of memory.
 * It behaves like a string in many respects.
 * It is the responsibility of the programmer to ensure that the
 * memory referenced is valid and preferably unchanged for the
 * lifetime of the stringref; said lifetime should generally be short.
 **/
class stringref
{
public:
    typedef const char * const_iterator;
    typedef size_t size_type;
    static const size_type npos = static_cast<size_type>(-1);
    stringref() : _s(""), _sz(0) { }
    stringref(const char * s) : _s(s), _sz(strlen(s)) { }
    stringref(const char * s, size_type sz) : _s(s), _sz(sz) { }
    stringref(const std::string & s) : _s(s.c_str()), _sz(s.size()) { }

    /**
     * return a pointer to the data held, or NULL.
     * Note that the data may not be zero terminated, and a default
     * constructed stringref will give a NULL pointer back.  If you
     * need to make sure c_str() gives a valid zero-terminated string
     * you should make a vespalib::string from the stringref.
     **/
    const char * c_str() const { return _s; }

    /** return a pointer to the data held, or NULL. See c_str(). */
    const char * data() const { return _s; }

    size_type      size() const { return _sz; }
    size_type    length() const { return size(); }
    bool          empty() const { return _sz == 0; }
    const char *  begin() const { return c_str(); }
    const char *    end() const { return begin() + size(); }
    const char * rbegin() const { return end() - 1; }
    const char *   rend() const { return begin() - 1; }
    stringref substr(size_type start, size_type sz=npos) const {
        if (start < size()) {
            return stringref(c_str() + start, std::min(sz, size()-start));
        }
        return stringref();
    }

    /**
     * Find the first occurrence of a string, searching from @c start
     *
     * @param s characters to search for. Must be zero terminated to make sense.
     * @param start index at which the search will be started
     * @return index from the start of the string at which the character
     *     was found, or npos if the character could not be located
     */
    size_type find(const char * s, size_type start=0) const {
        const char *buf = begin()+start;
        const char *found = (const char *)strstr(buf, s);
        return (found != NULL) ? (found - begin()) : (size_type)npos;
    }
    /**
     * Find the first occurrence of a string, searching from @c start
     *
     * @param s characters to search for. Must be zero terminated to make sense.
     * @param start index at which the search will be started
     * @return index from the start of the string at which the character
     *     was found, or npos if the character could not be located
     */
    size_type find(const stringref & s, size_type start=0) const;
    /**
     * Find the first occurrence of a character, searching from @c start
     *
     * @param c character to search for
     * @param start index at which the search will be started
     * @return index from the start of the string at which the character
     *     was found, or npos if the character could not be located
     */
    size_type find(char c, size_type start=0) const {
        const char *buf = begin()+start;
        const char *found = (const char *)memchr(buf, c, _sz-start);
        return (found != NULL) ? (found - begin()) : (size_type)npos;
    }
    /**
     * Find the last occurrence of a substring, starting at e and
     * searching in reverse order.
     *
     * @param s substring to search for
     * @param e index from which the search will be started
     * @return index from the start of the string at which the substring
     *     was found, or npos if the substring could not be located
     */
    size_type rfind(char c, size_type e=npos) const {
        if (!empty()) {
            const char *b = begin();
            for (size_type i(std::min(size()-1, e) + 1); i > 0;) {
                --i;
                if (c == b[i]) {
                    return i;
                }
            }
        }
        return npos;
    }
    /**
     * Find the last occurrence of a substring, starting at e and
     * searching in reverse order.
     *
     * @param s substring to search for
     * @param e index from which the search will be started
     * @return index from the start of the string at which the substring
     *     was found, or npos if the substring could not be located
     */
    size_type rfind(const char * s, size_type e=npos) const;
    int compare(const stringref & s) const { return compare(s.c_str(), s.size()); }
    int compare(const char *s, size_type sz) const {
        int diff(memcmp(_s, s, std::min(sz, size())));
        return (diff != 0) ? diff : (size() - sz);
    }
    const char & operator [] (size_t i) const { return _s[i]; }
    operator std::string () const { return std::string(_s, _sz); }
    bool operator  <        (const char * s) const { return compare(s, strlen(s)) < 0; }
    bool operator  < (const std::string & s) const { return compare(s.c_str(), s.size()) < 0; }
    bool operator  <   (const stringref & s) const { return compare(s.c_str(), s.size()) < 0; }
    bool operator <=        (const char * s) const { return compare(s, strlen(s)) <= 0; }
    bool operator <= (const std::string & s) const { return compare(s.c_str(), s.size()) <= 0; }
    bool operator <=   (const stringref & s) const { return compare(s.c_str(), s.size()) <= 0; }
    bool operator !=        (const char * s) const { return compare(s, strlen(s)) != 0; }
    bool operator != (const std::string & s) const { return compare(s.c_str(), s.size()) != 0; }
    bool operator !=   (const stringref & s) const { return compare(s.c_str(), s.size()) != 0; }
    bool operator ==        (const char * s) const { return compare(s, strlen(s)) == 0; }
    bool operator == (const std::string & s) const { return compare(s.c_str(), s.size()) == 0; }
    bool operator ==   (const stringref & s) const { return compare(s.c_str(), s.size()) == 0; }
    bool operator >=        (const char * s) const { return compare(s, strlen(s)) >= 0; }
    bool operator >= (const std::string & s) const { return compare(s.c_str(), s.size()) >= 0; }
    bool operator >=   (const stringref & s) const { return compare(s.c_str(), s.size()) >= 0; }
    bool operator  >        (const char * s) const { return compare(s, strlen(s)) > 0; }
    bool operator  > (const std::string & s) const { return compare(s.c_str(), s.size()) > 0; }
    bool operator  >   (const stringref & s) const { return compare(s.c_str(), s.size()) > 0; }
private:
    const char *_s;
    size_type   _sz;
    friend bool operator == (const std::string & a, const stringref & b) { return b == a; }
    friend bool operator != (const std::string & a, const stringref & b) { return b != a; }
    friend std::ostream & operator << (std::ostream & os, const stringref & v);
};


/**
 * class intended as a mostly-drop-in replacement for std::string
 * optimized for good multi-core performance using the well-known
 * "small-string optimization" where a small chunk of memory is
 * allocated internally in the object; as long as only small strings
 * are used the internal chunk will be used and no extra allocation
 * will happen.  The template parameter StackSize must be positive,
 * should be at least 8 and preferably a multiple of 8 for good
 * performance.  The size of strings is currently limited to 4GB, but
 * no checking is done - if a string grows too big the size will just
 * wrap.
 **/
template <uint32_t StackSize>
class small_string
{
public:
    typedef size_t size_type;
    typedef char * iterator;
    typedef const char * const_iterator;
    typedef char * reverse_iterator;
    typedef const char * const_reverse_iterator;
    static const size_type npos = static_cast<size_type>(-1);
    small_string() : _buf(_stack), _sz(0), _bufferSize(StackSize) { _stack[0] = '\0'; }
    small_string(const char * s) : _buf(_stack), _sz(s ? strlen(s) : 0) { init(s); }
    small_string(const void * s, size_type sz) : _buf(_stack), _sz(sz) { init(s); }
    small_string(const stringref & s) : _buf(_stack), _sz(s.size()) { init(s.c_str()); }
    small_string(const std::string & s) : _buf(_stack), _sz(s.size()) { init(s.c_str()); }
    small_string(const small_string & rhs) noexcept : _buf(_stack), _sz(rhs.size()) { init(rhs.c_str()); }
    small_string(const small_string & rhs, size_type pos, size_type sz=npos) noexcept
        : _buf(_stack), _sz(std::min(sz, rhs.size()-pos))
    {
        init(rhs.c_str()+pos);
    }
    small_string(size_type sz, char c)
        : _buf(_stack), _sz(0), _bufferSize(StackSize)
    {
        reserve(sz);
        memset(buffer(), c, sz);
        _sz = sz;
        *end() = '\0';
    }

    template<typename Iterator>
    small_string(Iterator s, Iterator e);

    ~small_string() {
        if (__builtin_expect(isAllocated(), false)) {
            free(buffer());
        }
    }
    small_string& operator= (const small_string &rhs) {
        return assign(rhs.c_str(), rhs.size());
    }
    small_string & operator= (const stringref &rhs) {
        return assign(rhs.c_str(), rhs.size());
    }
    small_string& operator= (const char *s) {
        return assign(s);
    }
    small_string& operator= (const std::string &rhs) {
        return operator= (stringref(rhs));
    }
    void swap(small_string & rhs) {
        std::swap(*this, rhs);
    }
    operator std::string () const { return std::string(c_str(), size()); }
    operator stringref () const { return stringref(c_str(), size()); }
    char at(size_t i) const { return buffer()[i]; }
    char & at(size_t i) { return buffer()[i]; }
    const char & operator [] (size_t i) const { return buffer()[i]; }
    char & operator [] (size_t i) { return buffer()[i]; }

    /** if there is a newline at the end of the string, remove it and return true */
    bool chomp() {
        if (size() > 0 && *rbegin() == '\n') {
            _resize(size() - 1);
            return true;
        }
        return false;
    }

    /**
     * Remove the last character of the string
     */
    void pop_back() {
      _resize(size() - 1);
    }

    /**
     * Find the last occurrence of a substring, starting at e and
     * searching in reverse order.
     *
     * @param s substring to search for
     * @param e index from which the search will be started
     * @return index from the start of the string at which the substring
     *     was found, or npos if the substring could not be located
     */
    size_type rfind(const char * s, size_type e=npos) const;

    /**
     * Find the last occurrence of a character, starting at e and
     * searching in reverse order.
     *
     * @param c character to search for
     * @param e index at which the search will be started
     * @return index from the start of the string at which the character
     *     was found, or npos if the character could not be located
     */
    size_type rfind(char c, size_type e=npos) const {
        size_type sz = std::min(size()-1, e)+1;
        const char *b = buffer();
        while (sz-- > 0) {
            if (c == b[sz]) {
                return sz;
            }
        }
        return npos;
    }
    size_type find_last_of(char c, size_type e=npos) const { return rfind(c, e); }
    size_type find_first_of(char c, size_type start=0) const { return find(c, start); }

    size_type find_first_not_of(char c, size_type start=0) const {
        size_t p(start);
        const char *buf = buffer();
        for(size_t m(size()); (p < m) && (buf[p] == c); p++);
        return (p < size()) ? p : (size_type)npos;
    }

    /**
     * Find the first occurrence of a substring, searching from @c start
     *
     * @param s substring to search for
     * @param start index from which the search will be started
     * @return index from the start of the string at which the substring
     *     was found, or npos if the substring could not be located
     */
    size_type find(const small_string & s, size_type start=0) const { return find(s.c_str(), start); }

    /**
     * Find the first occurrence of a substring, searching from @c start
     *
     * @param s substring to search for
     * @param start index at which the search will be started
     * @return index from the start of the string at which the substring
     *     was found, or npos if the substring could not be located
     */
    size_type find(const char * s, size_type start=0) const {
        const char *buf = buffer()+start;
        const char *found = strstr(buf, s);
        return (found != NULL) ? (found - buffer()) : (size_type)npos;
    }

    /**
     * Find the first occurrence of a character, searching from @c start
     *
     * @param s character to search for
     * @param start index at which the search will be started
     * @return index from the start of the string at which the character
     *     was found, or npos if the character could not be located
     */
    size_type find(char c, size_type start=0) const {
        const char *buf = buffer()+start;
        const char *found = (const char *)memchr(buf, c, _sz-start);
        return (found != NULL) ? (found - buffer()) : (size_type)npos;
    }
    small_string & assign(const char * s) { return assign(s, strlen(s)); }
    small_string & assign(const void * s, size_type sz);
    small_string & assign(const stringref &s, size_type pos, size_type sz) {
        return assign(s.c_str() + pos, sz);
    }
    small_string & assign(const stringref &rhs) {
        if (c_str() != rhs.c_str()) assign(rhs.c_str(), rhs.size());
        return *this;
    }
    small_string & push_back(char c)              { return append(&c, 1); }
    small_string & append(char c)                 { return append(&c, 1); }
    small_string & append(const char * s)         { return append(s, strlen(s)); }
    small_string & append(const stringref & s)    { return append(s.c_str(), s.size()); }
    small_string & append(const std::string & s)  { return append(s.c_str(), s.size()); }
    small_string & append(const small_string & s) { return append(s.c_str(), s.size()); }
    small_string & append(const void * s, size_type sz);
    small_string & operator += (char c)                 { return append(c); }
    small_string & operator += (const char * s)         { return append(s); }
    small_string & operator += (const stringref & s)    { return append(s); }
    small_string & operator += (const std::string & s)  { return append(s); }
    small_string & operator += (const small_string & s) { return append(s); }

    /**
     * Return a new string comprised of the contents of a sub-range of this
     * string, starting at start and spanning sz characters.
     *
     * @param start position at which the first character of the substring is to start
     * @param sz    length of substring. If start+sz is beyond the
     *     end of the string, only the remaining part will be returned.
     * @return a substring of *this
     */
    small_string substr(size_type start, size_type sz=npos) const {
        if (start < size()) {
            const char *s = c_str();
            return small_string(s + start, std::min(sz, size()-start));
        }
        return small_string();
    }

    small_string & insert(iterator p, const_iterator f, const_iterator l) { return insert(p-c_str(), f, l-f); }
    small_string & insert(size_type start, const stringref & v) { return insert(start, v.c_str(), v.size()); }
    small_string & insert(size_type start, const void * v, size_type sz);

    /**
     * Erases the content of the string, leaving it zero-length.
     * Does not alter string capacity.
     */
    void clear() {
        _sz = 0;
        buffer()[0] = 0;
    }

    /**
     * Frees any heap-allocated storage for the string and erases its content,
     * leaving it zero-length. Capacity is reset to the original small
     * string stack size
     */
    void reset() {
        if (isAllocated()) {
            free(buffer());
            _bufferSize = StackSize;
            _buf = _stack;
        }
        clear();
    }
    const_iterator begin() const { return buffer(); }
    const_iterator   end() const { return buffer() + size(); }
    iterator begin() { return buffer(); }
    iterator   end() { return buffer() + size(); }
    const_reverse_iterator rbegin() const { return end() - 1; }
    const_reverse_iterator   rend() const { return begin() - 1; }
    reverse_iterator rbegin() { return end() - 1; }
    reverse_iterator   rend() { return begin() - 1; }
    const char * c_str() const { return buffer(); }
    const char * data() const { return buffer(); }
    size_type size()     const { return _sz; }
    size_type length()   const { return size(); }
    bool empty()         const { return _sz == 0; }

    /**
     * at position p1, replace n1 characters with the contents of s
     *
     * @param p1 the position where the replacement is put, must be inside old string
     * @param n1 how many old characters should be replaced, cannot go outside old string
     * @param s  new replacement content
     **/
    small_string& replace (size_t p1, size_t n1, const small_string& s ) {
        return replace(p1, n1, s.c_str(), s.size());
    }

    /**
     * at position p1, replace n1 characters with
     * the n2 characters of s starting at position p2
     *
     * @param p1 the position where the replacement is put, must be inside old string
     * @param n1 how many old characters should be replaced, cannot go outside old string
     * @param s  where to get new replacement content
     * @param p2 position in s where replacement content starts
     * @param n2 how many new characters to use
     **/
    small_string& replace (size_t p1, size_t n1, const small_string& s, size_t p2, size_t n2);

    /**
     * at position p1, replace n1 characters with
     * the n2 first characters of s
     *
     * @param p1 the position where the replacement is put, must be inside old string
     * @param n1 how many old characters should be replaced, cannot go outside old string
     * @param s  pointer to new content
     * @param n2 how many new characters to use
     **/
    small_string& replace (size_t p1, size_t n1, const char *s, size_t n2);

    /**
     * at position p1, replace n1 characters with the contents of s
     *
     * @param p1 the position where the replacement is put, must be inside old string
     * @param n1 how many old characters should be replaced, cannot go outside old string
     * @param s  pointer to new replacement content
     **/
    small_string& replace (size_t p1, size_t n1, const char* s ) {
        return replace(p1, n1, s, strlen(s));
    }

    /* not implemented?
    small_string& replace ( size_t p1, size_t n1, size_t n2, char c );
    */

    bool operator  <         (const char * s) const { return compare(s, strlen(s)) < 0; }
    bool operator  <  (const std::string & s) const { return compare(s.c_str(), s.size()) < 0; }
    bool operator  < (const small_string & s) const { return compare(s.c_str(), s.size()) < 0; }
    bool operator  <    (const stringref & s) const { return compare(s.c_str(), s.size()) < 0; }
    bool operator <=         (const char * s) const { return compare(s, strlen(s)) <= 0; }
    bool operator <=  (const std::string & s) const { return compare(s.c_str(), s.size()) <= 0; }
    bool operator <= (const small_string & s) const { return compare(s.c_str(), s.size()) <= 0; }
    bool operator <=    (const stringref & s) const { return compare(s.c_str(), s.size()) <= 0; }
    bool operator ==         (const char * s) const { return compare(s, strlen(s)) == 0; }
    bool operator ==  (const std::string & s) const { return compare(s.c_str(), s.size()) == 0; }
    bool operator == (const small_string & s) const { return compare(s.c_str(), s.size()) == 0; }
    bool operator ==    (const stringref & s) const { return compare(s.c_str(), s.size()) == 0; }
    bool operator !=         (const char * s) const { return compare(s, strlen(s)) != 0; }
    bool operator !=  (const std::string & s) const { return compare(s.c_str(), s.size()) != 0; }
    bool operator != (const small_string & s) const { return compare(s.c_str(), s.size()) != 0; }
    bool operator !=    (const stringref & s) const { return compare(s.c_str(), s.size()) != 0; }
    bool operator >=         (const char * s) const { return compare(s, strlen(s)) >= 0; }
    bool operator >=  (const std::string & s) const { return compare(s.c_str(), s.size()) >= 0; }
    bool operator >= (const small_string & s) const { return compare(s.c_str(), s.size()) >= 0; }
    bool operator >=    (const stringref & s) const { return compare(s.c_str(), s.size()) >= 0; }
    bool operator  >         (const char * s) const { return compare(s, strlen(s)) > 0; }
    bool operator  >  (const std::string & s) const { return compare(s.c_str(), s.size()) > 0; }
    bool operator  > (const small_string & s) const { return compare(s.c_str(), s.size()) > 0; }
    bool operator  >    (const stringref & s) const { return compare(s.c_str(), s.size()) > 0; }

    template<typename T> bool operator != (const T& s) const { return ! operator == (s); }

    int compare(const small_string & s) const { return compare(s.c_str(), s.size()); }
    int compare(const char *s, size_t sz) const {
        int diff(memcmp(buffer(), s, std::min(sz, size())));
        return (diff != 0) ? diff : (size() - sz);
    }

    size_type capacity() const { return _bufferSize - 1; }

    /**
     * Make string exactly newSz in length removing characters at
     * the end as required or padding with pad character.
     *
     * @param newSz new size of string. Must be less than string capacity
     * @param c default character to use when initializing uninitialized memory.
     */

    void resize(size_type newSz, char padding = '\0') {
        if (newSz > capacity()) {
            reserve(newSz);
        }
        if (newSz > size()) {
          memset(buffer()+size(), padding, newSz - size());
        }
        _resize(newSz);
    }

    /**
     * Will extend the string within its current buffer. Assumes memory is already initialized.
     * Can not extend beyond capacity.
     * Note this is non-STL.
     *
     * @param newSz new size of string.
     */
    void append_from_reserved(size_type sz);

    /**
     * Ensure string has at least newCapacity characters of available
     * storage. If newCapacity is beyond the initial small string
     * stack size, heap storage will be used instead.
     *
     * @param newCapacity new minimum capacity of string
     */
    void reserve(size_type newCapacity) {
        reserveBytes(newCapacity + 1);
    }
private:
    void assign_slower(const void * s, size_type sz) __attribute((noinline));
    void init_slower(const void *s) noexcept __attribute((noinline));
    void _reserveBytes(size_type newBufferSize);
    void reserveBytes(size_type newBufferSize) {
        if (newBufferSize > _bufferSize) {
            _reserveBytes(newBufferSize);
        }
    }
    typedef uint32_t isize_type;
    bool needAlloc(isize_type add) const { return (add + _sz + 1) > _bufferSize; }
    bool isAllocated() const { return _buf != _stack; }
    char * buffer() { return _buf; }
    const char * buffer() const { return _buf; }
    void appendAlloc(const void * s, size_type sz) __attribute__((noinline));
    void init(const void *s) noexcept {
        if (__builtin_expect(_sz < StackSize, true)) {
            _bufferSize = StackSize;
            memcpy(_stack, s, _sz);
            _stack[_sz] = '\0';
        } else {
            init_slower(s);
        }
    }
    void _resize(size_type newSz) {
        _sz = newSz;
        *end() = '\0';
    }
    char     * _buf;
    isize_type _sz;
    isize_type _bufferSize;
    char       _stack[StackSize];
    template <uint32_t SS>
    friend std::ostream & operator << (std::ostream & os, const small_string<SS> & v);
    template <uint32_t SS>
    friend std::istream & operator >> (std::istream & is, small_string<SS> & v);
};

template <uint32_t StackSize>
template<typename Iterator>
small_string<StackSize>::small_string(Iterator s, Iterator e) :
    _buf(_stack),
    _sz(0),
    _bufferSize(StackSize)
{
    _stack[0] = '\0';
    for(; s != e; s++) {
        append(*s);
    }
}

template <uint32_t StackSize>
const size_t small_string<StackSize>::npos;

typedef small_string<48> string;

template<uint32_t StackSize>
vespalib::small_string<StackSize>
operator + (const vespalib::small_string<StackSize> & a, const vespalib::small_string<StackSize> & b);

template<uint32_t StackSize>
vespalib::small_string<StackSize>
operator + (const vespalib::small_string<StackSize> & a, const vespalib::stringref & b);

template<uint32_t StackSize>
vespalib::small_string<StackSize>
operator + (const vespalib::stringref & a, const vespalib::small_string<StackSize> & b);

template<uint32_t StackSize>
vespalib::small_string<StackSize>
operator + (const vespalib::small_string<StackSize> & a, const char * b);

template<uint32_t StackSize>
vespalib::small_string<StackSize>
operator + (const char * a, const vespalib::small_string<StackSize> & b);

template<typename T, uint32_t StackSize>
bool
operator == (const T& a, const vespalib::small_string<StackSize>& b)
{
    return b == a;
}

template<typename T, uint32_t StackSize>
bool
operator != (const T& a, const vespalib::small_string<StackSize>& b)
{
    return b != a;
}

template<typename T, uint32_t StackSize>
bool
operator < (const T& a, const vespalib::small_string<StackSize>& b)
{
    return b > a;
}

vespalib::string operator + (const vespalib::stringref & a, const vespalib::stringref & b);
vespalib::string operator + (const char * a, const vespalib::stringref & b);
vespalib::string operator + (const vespalib::stringref & a, const char * b);

inline bool contains(const stringref & text, const stringref & key) {
    return text.find(key) != stringref::npos;
}

inline bool starts_with(const stringref & text, const stringref & key) {
    if (text.size() >= key.size()) {
        return memcmp(text.begin(), key.begin(), key.size()) == 0;
    }
    return false;
}

inline bool ends_with(const stringref & text, const stringref & key) {
    if (text.size() >= key.size()) {
        return memcmp(text.end()-key.size(), key.begin(), key.size()) == 0;
    }
    return false;
}

/**
 * Utility function to format an unsigned integer into a new
 * vespalib::string instance.
 **/
static inline vespalib::string stringify(uint64_t number)
{
    char digits[64];
    int numdigits = 0;
    do {
        digits[numdigits++] = '0' + (number % 10);
        number /= 10;
    } while (number > 0);
    vespalib::string retval;
    while (numdigits > 0) {
        retval.append(digits[--numdigits]);
    }
    return retval;
}

} // namespace vespalib

