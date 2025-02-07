/**
 * This code is released under the
 * Apache License Version 2.0 http://www.apache.org/licenses/.
 *
 * (c) Daniel Lemire, http://lemire.me/en/
 *     with contributions from Zarian Waheed and others.
 */

#ifndef EWAH_H
#define EWAH_H

#include <algorithm>
#include <vector>
#include <queue>

#include "ewahutil.h"
#include "boolarray.h"

#include "runninglengthword.h"

template <class uword> class EWAHBoolArrayIterator;

template <class uword> class EWAHBoolArraySetBitForwardIterator;

class BitmapStatistics;

template <class uword> class EWAHBoolArrayRawIterator;

/**
 * This class is a compressed bitmap.
 * This is where compression
 * happens.
 * The underlying data structure is an STL vector.
 */
template <class uword = uint32_t> class EWAHBoolArray {
public:
  EWAHBoolArray() : buffer(1, 0), sizeinbits(0), lastRLW(0) {}

  static EWAHBoolArray bitmapOf(size_t n, ...) {
    EWAHBoolArray ans;
    va_list vl;
    va_start(vl, n);
    for (size_t i = 0; i < n; i++) {
      ans.set(static_cast<size_t>(va_arg(vl, int)));
    }
    va_end(vl);
    return ans;
  }

  /**
   * Query the value of bit i. This runs in time proportional to
   * the size of the bitmap. This is not meant to be use in
   * a performance-sensitive context.
   *
   *  (This implementation is based on zhenjl's Go version of JavaEWAH.)
   *
   */
  bool get(const size_t pos) const {
    if (pos >= static_cast<size_t>(sizeinbits))
      return false;
    const size_t wordpos = pos / wordinbits;
    size_t WordChecked = 0;
    EWAHBoolArrayRawIterator<uword> j = raw_iterator();
    while (j.hasNext()) {
      BufferedRunningLengthWord<uword> &rle = j.next();
      WordChecked += static_cast<size_t>(rle.getRunningLength());
      if (wordpos < WordChecked)
        return rle.getRunningBit();
      if (wordpos < WordChecked + rle.getNumberOfLiteralWords()) {
        const uword w = j.dirtyWords()[wordpos - WordChecked];
        return (w & (static_cast<uword>(1) << (pos % wordinbits))) != 0;
      }
      WordChecked += static_cast<size_t>(rle.getNumberOfLiteralWords());
    }
    return false;
  }

  /**
   * Set the ith bit to true (starting at zero).
   * Auto-expands the bitmap. It has constant running time complexity.
   * Note that you must set the bits in increasing order:
   * set(1), set(2) is ok; set(2), set(1) is not ok.
   * set(100), set(100) is also not ok.
   *
   * Note: by design EWAH is not an updatable data structure in
   * the sense that once bit 1000 is set, you cannot change the value
   * of bits 0 to 1000.
   *
   * Returns true if the value of the bit was changed, and false otherwise.
   * (In practice, if you set the bits in strictly increasing order, it
   * should always return true.)
   */
  bool set(size_t i);

  /**
   * Transform into a string that presents a list of set bits.
   * The running time is linear in the compressed size of the bitmap.
   */
  operator std::string() const {
    std::stringstream ss;
    ss << *this;
    return ss.str();
  }
  friend std::ostream &operator<<(std::ostream &out, const EWAHBoolArray &a) {

    out << "{";
    for (EWAHBoolArray::const_iterator i = a.begin(); i != a.end();) {
      out << *i;
      ++i;
      if (i != a.end())
        out << ",";
    }
    out << "}";

    return out;
  }
  /**
   * Make sure the two bitmaps have the same size (padding with zeroes
   * if necessary). It has constant running time complexity.
   *
   * This is useful when calling "logicalnot" functions.
   *
   * This can an adverse effect of performance, especially when computing
   * intersections.
   */
  void makeSameSize(EWAHBoolArray &a) {
    if (a.sizeinbits < sizeinbits)
      a.padWithZeroes(sizeinbits);
    else if (sizeinbits < a.sizeinbits)
      padWithZeroes(a.sizeinbits);
  }

  enum { RESERVEMEMORY = true }; // for speed

  typedef EWAHBoolArraySetBitForwardIterator<uword> const_iterator;

  /**
   * Returns an iterator that can be used to access the position of the
   * set bits. The running time complexity of a full scan is proportional to the
   * number
   * of set bits: be aware that if you have long strings of 1s, this can be
   * very inefficient.
   *
   * It can be much faster to use the toArray method if you want to
   * retrieve the set bits.
   */
  const_iterator begin() const {
    return EWAHBoolArraySetBitForwardIterator<uword>(buffer);
  }

  /**
   * Basically a bogus iterator that can be used together with begin()
   * for constructions such as for(EWAHBoolArray<uword>::iterator i = b.begin();
   * i!=b.end(); ++i) {}
   */
  const_iterator end() const {
    return EWAHBoolArraySetBitForwardIterator<uword>(buffer, buffer.size());
  }

  /**
   * Retrieve the set bits. Can be much faster than iterating through
   * the set bits with an iterator.
   */
  std::vector<size_t> toArray() const;

  /**
   * computes the logical and with another compressed bitmap
   * answer goes into container
   * Running time complexity is proportional to the sum of the compressed
   * bitmap sizes.
   */
  void logicaland(const EWAHBoolArray &a, EWAHBoolArray &container) const;

  /**
   * computes the logical and with another compressed bitmap
   * Return the answer
   * Running time complexity is proportional to the sum of the compressed
   * bitmap sizes.
   */
  EWAHBoolArray logicaland(const EWAHBoolArray &a) const {
    EWAHBoolArray answer;
    logicaland(a, answer);
    return answer;
  }

  /**
   * computes the logical and with another compressed bitmap
   * answer goes into container
   * Running time complexity is proportional to the sum of the compressed
   * bitmap sizes.
   */
  void logicalandnot(const EWAHBoolArray &a, EWAHBoolArray &container) const {
    EWAHBoolArray aneg = a.logicalnot(); // could be more efficient
    logicaland(aneg, container);
  }

  /**
   * computes the logical and with another compressed bitmap
   * Return the answer
   * Running time complexity is proportional to the sum of the compressed
   * bitmap sizes.
   */
  EWAHBoolArray logicalandnot(const EWAHBoolArray &a) const {
    EWAHBoolArray answer;
    logicaland(a, answer);
    return answer;
  }
  /**
   * tests whether the bitmaps "intersect" (have at least one 1-bit at the same
   * position). This function does not modify the existing bitmaps.
   * It is faster than calling logicaland.
   */
  bool intersects(const EWAHBoolArray &a) const;

  /**
   * computes the logical or with another compressed bitmap
   * answer goes into container
   * Running time complexity is proportional to the sum of the compressed
   * bitmap sizes.
   *
   * If you have many bitmaps, see fast_logicalor_tocontainer.
   */
  void logicalor(const EWAHBoolArray &a, EWAHBoolArray &container) const;

  /**
   * computes the logical or with another compressed bitmap
   * Return the answer
   * Running time complexity is proportional to the sum of the compressed
   * bitmap sizes.
   *
   * If you have many bitmaps, see fast_logicalor.
   *
   */
  EWAHBoolArray logicalor(const EWAHBoolArray &a) const {
    EWAHBoolArray answer;
    logicalor(a, answer);
    return answer;
  }

  /**
   * computes the logical xor with another compressed bitmap
   * answer goes into container
   * Running time complexity is proportional to the sum of the compressed
   * bitmap sizes.
   */
  void logicalxor(const EWAHBoolArray &a, EWAHBoolArray &container) const;

  /**
   * computes the logical xor with another compressed bitmap
   * Return the answer
   * Running time complexity is proportional to the sum of the compressed
   * bitmap sizes.
   */
  EWAHBoolArray logicalxor(const EWAHBoolArray &a) const {
    EWAHBoolArray answer;
    logicalxor(a, answer);
    return answer;
  }
  /**
   * clear the content of the bitmap. It does not
   * release the memory.
   */
  void reset() {
    buffer.clear();
    buffer.push_back(0);
    sizeinbits = 0;
    lastRLW = 0;
  }

  /**
   * convenience method.
   *
   * returns the number of words added (storage cost increase)
   */
  inline size_t addWord(const uword newdata,
                        const uint32_t bitsthatmatter = 8 * sizeof(uword));

  inline void printout(std::ostream &o = std::cout) {
    toBoolArray().printout(o);
  }

  /**
   * Prints a verbose description of the content of the compressed bitmap.
   */
  void debugprintout() const;

  /**
   * Return the size in bits of this bitmap (this refers
   * to the uncompressed size in bits).
   *
   * You can increase it with padWithZeroes()
   */
  inline size_t sizeInBits() const { return sizeinbits; }

  /**
   * Return the size of the buffer in bytes. This
   * is equivalent to the storage cost, minus some overhead.
   */
  inline size_t sizeInBytes() const { return buffer.size() * sizeof(uword); }

  /**
   * same as addEmptyWord, but you can do several in one shot!
   * returns the number of words added (storage cost increase)
   */
  size_t addStreamOfEmptyWords(const bool v, size_t number);

  /**
   * add a stream of dirty words, returns the number of words added
   * (storage cost increase)
   */
  size_t addStreamOfDirtyWords(const uword *v, const size_t number);

  /**
   * add a stream of dirty words, each one negated, returns the number of words
   * added
   * (storage cost increase)
   */
  size_t addStreamOfNegatedDirtyWords(const uword *v, const size_t number);

  /**
   * make sure the size of the array is totalbits bits by padding with zeroes.
   * returns the number of words added (storage cost increase).
   *
   * This is useful when calling "logicalnot" functions.
   *
   * This can an adverse effect of performance, especially when computing
   * intersections.
   *
   */
  size_t padWithZeroes(const size_t totalbits);

  /**
   * Compute the size on disk assuming that it was saved using
   * the method "save".
   */
  size_t sizeOnDisk() const;

  /**
   * Save this bitmap to a stream. The file format is
   * | sizeinbits | buffer lenth | buffer content|
   * the sizeinbits part can be omitted if "savesizeinbits=false".
   * Both sizeinbits and buffer length are saved using the size_t data
   * type which is typically a 32-bit unsigned integer for 32-bit CPUs
   * and a 64-bit unsigned integer for 64-bit CPUs.
   * Note that this format is machine-specific. Note also
   * that the word size is not saved. For robust persistent
   * storage, you need to save this extra information elsewhere.
   */
  void write(std::ostream &out, const bool savesizeinbits = true) const;

  /**
   * This only writes the content of the buffer (see write()) method.
   * It is for advanced users.
   */
  void writeBuffer(std::ostream &out) const;

  /**
   * size (in words) of the underlying STL vector.
   */
  size_t bufferSize() const { return buffer.size(); }

  /**
   * this is the counterpart to the write method.
   * if you set savesizeinbits=false, then you are responsible
   * for setting the value fo the attribute sizeinbits (see method
   * setSizeInBits).
   */
  void read(std::istream &in, const bool savesizeinbits = true);

  /**
   * read the buffer from a stream, see method writeBuffer.
   * this is for advanced users.
   */
  void readBuffer(std::istream &in, const size_t buffersize);

  /**
   * We define two EWAHBoolArray as being equal if they have the same set bits.
   * Alternatively, B1==B2 if and only if cardinality(B1 XOR B2) ==0.
   */
  bool operator==(const EWAHBoolArray &x) const;

  /**
   * We define two EWAHBoolArray as being different if they do not have the same
   * set bits.
   * Alternatively, B1!=B2 if and only if cardinality(B1 XOR B2) >0.
   */
  bool operator!=(const EWAHBoolArray &x) const;

  bool operator==(const BoolArray<uword> &x) const;

  bool operator!=(const BoolArray<uword> &x) const;

  /**
   * Iterate over the uncompressed words.
   * Can be considerably faster than begin()/end().
   * Running time complexity of a full scan is proportional to the
   * uncompressed size of the bitmap.
   */
  EWAHBoolArrayIterator<uword> uncompress() const;

  /**
   * To iterate over the compressed data.
   * Can be faster than any other iterator.
   * Running time complexity of a full scan is proportional to the
   * compressed size of the bitmap.
   */
  EWAHBoolArrayRawIterator<uword> raw_iterator() const;

  /**
   * Appends the content of some other compressed bitmap
   * at the end of the current bitmap.
   */
  void append(const EWAHBoolArray &x);

  /**
   * For research purposes. This computes the number of
   * dirty words and the number of compressed words.
   */
  BitmapStatistics computeStatistics() const;

  /**
   * For convenience, this fully uncompresses the bitmap.
   * Not fast!
   */
  BoolArray<uword> toBoolArray() const;

  /**
   * Convert to a list of positions of "set" bits.
   * The recommended container is vector<size_t>.
   *
   * See also toVector().
   */
  template <class container>
  void appendRowIDs(container &out, const size_t offset = 0) const;

  /**
   * Convert to a list of positions of "set" bits.
   * The recommended container is vector<size_t>.
   * (alias for appendRowIDs).
   *
   * See also toVector().
   */
  template <class container>
  void appendSetBits(container &out, const size_t offset = 0) const {
    return appendRowIDs(out, offset);
  }

  /**
   * Returns a vector containing the position of the set
   * bits in increasing order.
   */
  std::vector<size_t> toVector() {
    std::vector<size_t> answer;
    appendSetBits(answer);
    return answer;
  }

  /**
   * Returns the number of bits set to the value 1.
   * The running time complexity is proportional to the
   * compressed size of the bitmap.
   *
   * This is sometimes called the cardinality.
   */
  size_t numberOfOnes() const;

  /**
   * Swap the content of this bitmap with another bitmap.
   * No copying is done. (Running time complexity is constant.)
   */
  void swap(EWAHBoolArray &x);

  const std::vector<uword> &getBuffer() const { return buffer; }

  enum { wordinbits = sizeof(uword) * 8 };

  /**
   *Please don't copy your bitmaps! The running time
   * complexity of a copy is the size of the compressed bitmap.
   **/
  EWAHBoolArray(const EWAHBoolArray &other)
      : buffer(other.buffer), sizeinbits(other.sizeinbits),
        lastRLW(other.lastRLW) {}

  /**
   * Copies the content of one bitmap onto another. Running time complexity
   * is proportional to the size of the compressed bitmap.
   * please, never hard-copy this object. Use the swap method if you must.
   */
  EWAHBoolArray &operator=(const EWAHBoolArray &x) {
    buffer = x.buffer;
    sizeinbits = x.sizeinbits;
    lastRLW = x.lastRLW;
    return *this;
  }

  /**
   * This is equivalent to the operator =. It is used
   * to keep in mind that assignment can be expensive.
   *
   *if you don't care to copy the bitmap (performance-wise), use this!
   */
  void expensive_copy(const EWAHBoolArray &x) {
    buffer = x.buffer;
    sizeinbits = x.sizeinbits;
    lastRLW = x.lastRLW;
  }

  /**
   * Write the logical not of this bitmap in the provided container.
   *
   * This function takes into account the sizeInBits value.
   * You may need to call "padWithZeroes" to adjust the sizeInBits.
   */
  void logicalnot(EWAHBoolArray &x) const;

  /**
   * Write the logical not of this bitmap in the provided container.
   *
   * This function takes into account the sizeInBits value.
   * You may need to call "padWithZeroes" to adjust the sizeInBits.
   */
  EWAHBoolArray<uword> logicalnot() const {
    EWAHBoolArray answer;
    logicalnot(answer);
    return answer;
  }

  /**
   * Apply the logical not operation on this bitmap.
   * Running time complexity is proportional to the compressed size of the
   *bitmap.
   * The current bitmap is not modified.
   *
   * This function takes into account the sizeInBits value.
   * You may need to call "padWithZeroes" to adjust the sizeInBits.
   **/
  void inplace_logicalnot();

  /**
   * set size in bits. This does not affect the compressed size. It
   * runs in constant time. This should not normally be used, except
   * as part of a deserialization process.
   */
  inline void setSizeInBits(const size_t size) { sizeinbits = size; }

private:
  // addStreamOfEmptyWords but does not return the cost increase,
  // does not update sizeinbits and does not check that number>0
  void fastaddStreamOfEmptyWords(const bool v, size_t number);

  // private because does not increment the size in bits
  // returns the number of words added (storage cost increase)
  inline size_t addLiteralWord(const uword newdata);

  // private because does not increment the size in bits
  // returns the number of words added (storage cost increase)
  size_t addEmptyWord(const bool v);
  // this second version "might" be faster if you hate OOP.
  // in my tests, it turned out to be slower!
  // private because does not increment the size in bits
  // inline void addEmptyWordStaticCalls(bool v);

  std::vector<uword> buffer;
  size_t sizeinbits;
  size_t lastRLW;
};

/**
 * computes the logical or (union) between "n" bitmaps (referenced by a
 * pointer).
 * The answer gets written out in container. This might be faster than calling
 * logicalor n-1 times.
 */
template <class uword>
void fast_logicalor_tocontainer(size_t n, const EWAHBoolArray<uword> **inputs,
                                EWAHBoolArray<uword> &container);

/**
 * computes the logical or (union) between "n" bitmaps (referenced by a
 * pointer).
 * Returns the answer. This might be faster than calling
 * logicalor n-1 times.
 */
template <class uword>
EWAHBoolArray<uword> fast_logicalor(size_t n,
                                    const EWAHBoolArray<uword> **inputs) {
  EWAHBoolArray<uword> answer;
  fast_logicalor_tocontainer(n, inputs, answer);
  return answer;
}

/**
 * Iterate over words of bits from a compressed bitmap.
 */
template <class uword> class EWAHBoolArrayIterator {
public:
  /**
   * is there a new word?
   */
  bool hasNext() const { return pointer < myparent.size(); }

  /**
   * return next word.
   */
  uword next() {
    uword returnvalue;
    if (compressedwords < rl) {
      ++compressedwords;
      if (b)
        returnvalue = notzero;
      else
        returnvalue = zero;
    } else {
#ifdef EWAHASSERT
      assert(literalwords < lw);
#endif
      ++literalwords;
      ++pointer;
#ifdef EWAHASSERT
      assert(pointer < myparent.size());
#endif
      returnvalue = myparent[pointer];
    }
    if ((compressedwords == rl) && (literalwords == lw)) {
      ++pointer;
      if (pointer < myparent.size())
        readNewRunningLengthWord();
    }
    return returnvalue;
  }

  EWAHBoolArrayIterator(const EWAHBoolArrayIterator<uword> &other)
      : pointer(other.pointer), myparent(other.myparent),
        compressedwords(other.compressedwords),
        literalwords(other.literalwords), rl(other.rl), lw(other.lw),
        b(other.b) {}

  static const uword zero = 0;
  static const uword notzero = static_cast<uword>(~zero);

private:
  EWAHBoolArrayIterator(const std::vector<uword> &parent);
  void readNewRunningLengthWord();
  friend class EWAHBoolArray<uword>;
  size_t pointer;
  const std::vector<uword> &myparent;
  uword compressedwords;
  uword literalwords;
  uword rl, lw;
  bool b;
};

/**
 * Used to go through the set bits. Not optimally fast, but convenient.
 */
template <class uword> class EWAHBoolArraySetBitForwardIterator {
public:
  enum { wordinbits = sizeof(uword) * 8 };
  typedef std::forward_iterator_tag iterator_category;
  typedef size_t *pointer;
  typedef size_t &reference_type;
  typedef size_t value_type;
  typedef ptrdiff_t difference_type;
  typedef EWAHBoolArraySetBitForwardIterator<uword> type_of_iterator;

  /**
   * Provides the location of the set bit.
   */
  size_t operator*() const { return currentrunoffset + offsetofpreviousrun; }

  // this can be expensive
  difference_type operator-(const type_of_iterator &o) {
    type_of_iterator &smaller = *this < o ? *this : o;
    type_of_iterator &bigger = *this >= o ? *this : o;
    if (smaller.mpointer == smaller.buffer.size())
      return 0;
    difference_type absdiff = static_cast<difference_type>(0);
    EWAHBoolArraySetBitForwardIterator<uword> buf(smaller);
    while (buf != bigger) {
      ++absdiff;
      ++buf;
    }
    if (*this < o)
      return absdiff;
    else
      return -absdiff;
  }

  bool operator<(const type_of_iterator &o) {
    if (&buffer != &o.buffer)
      return false;
    if (mpointer == buffer.size())
      return false;
    if (o.mpointer == o.buffer.size())
      return true;
    if (offsetofpreviousrun < o.offsetofpreviousrun)
      return true;
    if (offsetofpreviousrun > o.offsetofpreviousrun)
      return false;
    if (currentrunoffset < o.currentrunoffset)
      return true;
    return false;
  }
  bool operator<=(const type_of_iterator &o) {
    return ((*this) < o) || ((*this) == o);
  }

  bool operator>(const type_of_iterator &o) { return !((*this) <= o); }

  bool operator>=(const type_of_iterator &o) { return !((*this) < o); }

  EWAHBoolArraySetBitForwardIterator &operator++() {
    ++currentrunoffset;
    advanceToNextSetBit();
    return *this;
  }
  EWAHBoolArraySetBitForwardIterator operator++(int) {
    EWAHBoolArraySetBitForwardIterator old(*this);
    ++currentrunoffset;
    advanceToNextSetBit();
    return old;
  }
  bool operator==(const EWAHBoolArraySetBitForwardIterator<uword> &o) {
    // if they are both over, return true
    if ((mpointer == buffer.size()) && (o.mpointer == o.buffer.size()))
      return true;
    return (&buffer == &o.buffer) && (mpointer == o.mpointer) &&
           (offsetofpreviousrun == o.offsetofpreviousrun) &&
           (currentrunoffset == o.currentrunoffset);
  }
  bool operator!=(const EWAHBoolArraySetBitForwardIterator<uword> &o) {
    // if they are both over, return false
    if ((mpointer == buffer.size()) && (o.mpointer == o.buffer.size()))
      return false;
    return (&buffer != &o.buffer) || (mpointer != o.mpointer) ||
           (offsetofpreviousrun != o.offsetofpreviousrun) ||
           (currentrunoffset != o.currentrunoffset);
  }

  EWAHBoolArraySetBitForwardIterator(
      const EWAHBoolArraySetBitForwardIterator &o)
      : buffer(o.buffer), mpointer(o.mpointer),
        offsetofpreviousrun(o.offsetofpreviousrun),
        currentrunoffset(o.currentrunoffset), rlw(o.rlw) {}

private:
  bool advanceToNextSetBit() {
    if (mpointer == buffer.size())
      return false;
    if (currentrunoffset <
        static_cast<size_t>(rlw.getRunningLength() * wordinbits)) {
      if (rlw.getRunningBit())
        return true; // nothing to do
      currentrunoffset =
          static_cast<size_t>(rlw.getRunningLength() * wordinbits); // skipping
    }
    while (true) {
      const size_t indexoflitword = static_cast<size_t>(
          (currentrunoffset - rlw.getRunningLength() * wordinbits) /
          wordinbits);
      if (indexoflitword >= rlw.getNumberOfLiteralWords()) {
        if (advanceToNextRun())
          return advanceToNextSetBit();
        else {
          return false;
        }
      }

      if (usetrailingzeros) {

        const uint32_t tinwordpointer = static_cast<uint32_t>(
            (currentrunoffset - rlw.getRunningLength() * wordinbits) %
            wordinbits);
        const uword modcurrentword = static_cast<uword>(
            buffer[mpointer + 1 + indexoflitword] >> tinwordpointer);
        if (modcurrentword != 0) {
          currentrunoffset +=
              static_cast<size_t>(numberOfTrailingZeros(modcurrentword));
          return true;
        } else {
          currentrunoffset += wordinbits - tinwordpointer;
        }
      } else {
        const uword currentword = buffer[mpointer + 1 + indexoflitword];
        for (uint32_t
                 inwordpointer = static_cast<uint32_t>(
                     (currentrunoffset - rlw.getRunningLength() * wordinbits) %
                     wordinbits);
             inwordpointer < wordinbits; ++inwordpointer, ++currentrunoffset) {
          if ((currentword & (static_cast<uword>(1) << inwordpointer)) != 0)
            return true;
        }
      }
    }
  }

  enum { usetrailingzeros = true }; // optimization option

  bool advanceToNextRun() {
    offsetofpreviousrun += currentrunoffset;
    currentrunoffset = 0;
    mpointer += static_cast<size_t>(1 + rlw.getNumberOfLiteralWords());
    if (mpointer < buffer.size()) {
      rlw.mydata = buffer[mpointer];
    } else {
      return false;
    }
    return true;
  }

  EWAHBoolArraySetBitForwardIterator(const std::vector<uword> &parent,
                                     size_t startpointer = 0)
      : buffer(parent), mpointer(startpointer), offsetofpreviousrun(0),
        currentrunoffset(0), rlw(0) {
    if (mpointer < buffer.size()) {
      rlw.mydata = buffer[mpointer];
      advanceToNextSetBit();
    }
  }

  const std::vector<uword> &buffer;
  size_t mpointer;
  size_t offsetofpreviousrun;
  size_t currentrunoffset;
  friend class EWAHBoolArray<uword>;
  ConstRunningLengthWord<uword> rlw;
};

/**
 * This object is returned by the compressed bitmap as a
 * statistical descriptor.
 */
class BitmapStatistics {
public:
  BitmapStatistics()
      : totalliteral(0), totalcompressed(0), runningwordmarker(0),
        maximumofrunningcounterreached(0) {}
  size_t getCompressedSize() const { return totalliteral + runningwordmarker; }
  size_t getUncompressedSize() const { return totalliteral + totalcompressed; }
  size_t getNumberOfDirtyWords() const { return totalliteral; }
  size_t getNumberOfCleanWords() const { return totalcompressed; }
  size_t getNumberOfMarkers() const { return runningwordmarker; }
  size_t getOverRuns() const { return maximumofrunningcounterreached; }
  size_t totalliteral;
  size_t totalcompressed;
  size_t runningwordmarker;
  size_t maximumofrunningcounterreached;
};

template <class uword> bool EWAHBoolArray<uword>::set(size_t i) {
  if (i < sizeinbits)
    return false;
  const size_t dist = (i + wordinbits) / wordinbits -
                      (sizeinbits + wordinbits - 1) / wordinbits;
  sizeinbits = i + 1;
  if (dist > 0) { // easy
    if (dist > 1)
      fastaddStreamOfEmptyWords(false, dist - 1);
    addLiteralWord(
        static_cast<uword>(static_cast<uword>(1) << (i % wordinbits)));
    return true;
  }
  RunningLengthWord<uword> lastRunningLengthWord(buffer[lastRLW]);
  if (lastRunningLengthWord.getNumberOfLiteralWords() == 0) {
    lastRunningLengthWord.setRunningLength(
        static_cast<uword>(lastRunningLengthWord.getRunningLength() - 1));
    addLiteralWord(
        static_cast<uword>(static_cast<uword>(1) << (i % wordinbits)));
    return true;
  }
  buffer[buffer.size() - 1] |=
      static_cast<uword>(static_cast<uword>(1) << (i % wordinbits));
  // check if we just completed a stream of 1s
  if (buffer[buffer.size() - 1] == static_cast<uword>(~0)) {
    // we remove the last dirty word
    buffer[buffer.size() - 1] = 0;
    buffer.resize(buffer.size() - 1);
    lastRunningLengthWord.setNumberOfLiteralWords(static_cast<uword>(
        lastRunningLengthWord.getNumberOfLiteralWords() - 1));
    // next we add one clean word
    addEmptyWord(true);
  }
  return true;
}

template <class uword> void EWAHBoolArray<uword>::inplace_logicalnot() {
  size_t pointer(0), lastrlw(0);
  while (pointer < buffer.size()) {
    RunningLengthWord<uword> rlw(buffer[pointer]);
    lastrlw = pointer; // we save this up
    if (rlw.getRunningBit())
      rlw.setRunningBit(false);
    else
      rlw.setRunningBit(true);
    ++pointer;
    for (size_t k = 0; k < rlw.getNumberOfLiteralWords(); ++k) {
      buffer[pointer] = static_cast<uword>(~buffer[pointer]);
      ++pointer;
    }
  }
  if (sizeinbits % wordinbits != 0) {
    RunningLengthWord<uword> rlw(buffer[lastrlw]);
#ifdef EWAHASSERT
    assert(rlw.getNumberOfLiteralWords() + rlw.getRunningLength() > 0);
#endif
    const uword maskbogus =
        (static_cast<uword>(1) << (sizeinbits % wordinbits)) - 1;
    if (rlw.getNumberOfLiteralWords() > 0) { // easy case
      buffer[lastrlw + 1 + rlw.getNumberOfLiteralWords() - 1] &= maskbogus;
    } else if (rlw.getRunningBit()) {
#ifdef EWAHASSERT
      assert(rlw.getNumberOfLiteralWords() > 0);
#endif
      rlw.setNumberOfLiteralWords(rlw.getNumberOfLiteralWords() - 1);
      addLiteralWord(maskbogus);
    }
  }
}

template <class uword> size_t EWAHBoolArray<uword>::numberOfOnes() const {
  size_t tot(0);
  size_t pointer(0);
  while (pointer < buffer.size()) {
    ConstRunningLengthWord<uword> rlw(buffer[pointer]);
    if (rlw.getRunningBit()) {
      tot += static_cast<size_t>(rlw.getRunningLength() * wordinbits);
    }
    ++pointer;
    for (size_t k = 0; k < rlw.getNumberOfLiteralWords(); ++k) {
#ifdef EWAHASSERT
      assert(countOnes(buffer[pointer]) < 64);
#endif
      tot += countOnes(buffer[pointer]);
      ++pointer;
    }
  }
  return tot;
}

template <class uword>
std::vector<size_t> EWAHBoolArray<uword>::toArray() const {
  std::vector<size_t> ans;
  size_t pos(0);
  size_t pointer(0);
  while (pointer < buffer.size()) {
    ConstRunningLengthWord<uword> rlw(buffer[pointer]);
    if (rlw.getRunningBit()) {
      for (size_t k = 0; k < rlw.getRunningLength() * wordinbits; ++k, ++pos) {
        ans.push_back(pos);
      }
    } else {
      pos += static_cast<size_t>(rlw.getRunningLength() * wordinbits);
    }
    ++pointer;
    const bool usetrailing = true; // optimization
    for (size_t k = 0; k < rlw.getNumberOfLiteralWords(); ++k) {
      if (usetrailing) {
        uword myword = buffer[pointer];
        while (myword != 0) {
          uint32_t ntz = numberOfTrailingZeros(myword);
          ans.push_back(pos + ntz);
          myword ^= (static_cast<uword>(1) << ntz);
        }
        pos += wordinbits;
      } else {
        for (int c = 0; c < wordinbits; ++c, ++pos)
          if ((buffer[pointer] & (static_cast<uword>(1) << c)) != 0) {
            ans.push_back(pos);
          }
      }
      ++pointer;
    }
  }
  return ans;
}

template <class uword>
void EWAHBoolArray<uword>::logicalnot(EWAHBoolArray &x) const {
  x.reset();
  x.buffer.reserve(buffer.size());
  EWAHBoolArrayRawIterator<uword> i = this->raw_iterator();
  if (!i.hasNext())
    return; // nothing to do
  while (true) {
    BufferedRunningLengthWord<uword> &rlw = i.next();
    if (i.hasNext()) {
      if (rlw.getRunningLength() > 0)
        x.fastaddStreamOfEmptyWords(!rlw.getRunningBit(),
                                    rlw.getRunningLength());
      if (rlw.getNumberOfLiteralWords() > 0) {
        const uword *dw = i.dirtyWords();
        for (size_t k = 0; k < rlw.getNumberOfLiteralWords(); ++k) {
          x.addLiteralWord(~dw[k]);
        }
      }
    } else {
#ifdef EWAHASSERT
      assert(rlw.getNumberOfLiteralWords() + rlw.getRunningLength() > 0);
#endif
      if (rlw.getNumberOfLiteralWords() == 0) {
        if ((this->sizeinbits % wordinbits != 0) && !rlw.getRunningBit()) {
          if (rlw.getRunningLength() > 1)
            x.fastaddStreamOfEmptyWords(!rlw.getRunningBit(),
                                        rlw.getRunningLength() - 1);
          const uword maskbogus =
              (static_cast<uword>(1) << (this->sizeinbits % wordinbits)) - 1;
          x.addLiteralWord(maskbogus);
          break;
        } else {
          if (rlw.getRunningLength() > 0)
            x.fastaddStreamOfEmptyWords(!rlw.getRunningBit(),
                                        rlw.getRunningLength());
          break;
        }
      }
      if (rlw.getRunningLength() > 0)
        x.fastaddStreamOfEmptyWords(!rlw.getRunningBit(),
                                    rlw.getRunningLength());
      const uword *dw = i.dirtyWords();
      for (size_t k = 0; k + 1 < rlw.getNumberOfLiteralWords(); ++k) {
        x.addLiteralWord(~dw[k]);
      }
      const uword maskbogus =
          (this->sizeinbits % wordinbits != 0)
              ? (static_cast<uword>(1) << (this->sizeinbits % wordinbits)) - 1
              : ~static_cast<uword>(0);
      x.addLiteralWord((~dw[rlw.getNumberOfLiteralWords() - 1]) & maskbogus);
      break;
    }
  }
  x.sizeinbits = this->sizeinbits;
}

template <class uword>
size_t EWAHBoolArray<uword>::addWord(const uword newdata,
                                     const uint32_t bitsthatmatter) {
  sizeinbits += bitsthatmatter;
  if (newdata == 0) {
    return addEmptyWord(0);
  } else if (newdata == static_cast<uword>(~0)) {
    return addEmptyWord(1);
  } else {
    return addLiteralWord(newdata);
  }
}

template <class uword>
inline void EWAHBoolArray<uword>::writeBuffer(std::ostream &out) const {
  if (!buffer.empty())
    out.write(reinterpret_cast<const char *>(&buffer[0]),
              sizeof(uword) * buffer.size());
}

template <class uword>
inline void EWAHBoolArray<uword>::readBuffer(std::istream &in,
                                             const size_t buffersize) {
  buffer.resize(buffersize);
  if (buffersize > 0)
    in.read(reinterpret_cast<char *>(&buffer[0]), sizeof(uword) * buffersize);
}

template <class uword>
void EWAHBoolArray<uword>::write(std::ostream &out,
                                 const bool savesizeinbits) const {
  if (savesizeinbits)
    out.write(reinterpret_cast<const char *>(&sizeinbits), sizeof(sizeinbits));
  const size_t buffersize = buffer.size();
  out.write(reinterpret_cast<const char *>(&buffersize), sizeof(buffersize));
  if (buffersize > 0)
    out.write(reinterpret_cast<const char *>(&buffer[0]),
              static_cast<std::streamsize>(sizeof(uword) * buffersize));
}

template <class uword>
void EWAHBoolArray<uword>::read(std::istream &in, const bool savesizeinbits) {
  if (savesizeinbits)
    in.read(reinterpret_cast<char *>(&sizeinbits), sizeof(sizeinbits));
  else
    sizeinbits = 0;
  size_t buffersize(0);
  in.read(reinterpret_cast<char *>(&buffersize), sizeof(buffersize));
  buffer.resize(buffersize);
  if (buffersize > 0)
    in.read(reinterpret_cast<char *>(&buffer[0]),
            static_cast<std::streamsize>(sizeof(uword) * buffersize));
}

template <class uword>
size_t EWAHBoolArray<uword>::addLiteralWord(const uword newdata) {
  RunningLengthWord<uword> lastRunningLengthWord(buffer[lastRLW]);
  uword numbersofar = lastRunningLengthWord.getNumberOfLiteralWords();
  if (numbersofar >=
      RunningLengthWord<uword>::largestliteralcount) { // 0x7FFF) {
    buffer.push_back(0);
    lastRLW = buffer.size() - 1;
    RunningLengthWord<uword> lastRunningLengthWord2(buffer[lastRLW]);
    lastRunningLengthWord2.setNumberOfLiteralWords(1);
    buffer.push_back(newdata);
    return 2;
  }
  lastRunningLengthWord.setNumberOfLiteralWords(
      static_cast<uword>(numbersofar + 1));
#ifdef EWAHASSERT
  assert(lastRunningLengthWord.getNumberOfLiteralWords() == numbersofar + 1);
#endif
  buffer.push_back(newdata);
  return 1;
}

template <class uword>
size_t EWAHBoolArray<uword>::padWithZeroes(const size_t totalbits) {
  size_t wordsadded = 0;
  if (totalbits <= sizeinbits)
    return wordsadded;

  size_t missingbits = totalbits - sizeinbits;

  RunningLengthWord<uword> rlw(buffer[lastRLW]);
  if (rlw.getNumberOfLiteralWords() > 0) {
    // Consume trailing zeroes of trailing literal word (past sizeinbits)
    size_t remain = sizeinbits % wordinbits;
    if (remain > 0) // Is last word partial?
    {
      size_t avail = wordinbits - remain;
      if (avail > 0) {
        if (missingbits > avail) {
          missingbits -= avail;
        } else {
          missingbits = 0;
        }
        sizeinbits += avail;
      }
    }
  }

  if (missingbits > 0) {
    size_t wordstoadd = missingbits / wordinbits;
    if ((missingbits % wordinbits) != 0)
      ++wordstoadd;

    wordsadded = addStreamOfEmptyWords(false, wordstoadd);
  }
#ifdef EWAHASSERT
  assert(sizeinbits >= totalbits);
  assert(sizeinbits <= totalbits + wordinbits);
#endif
  sizeinbits = totalbits;
  return wordsadded;
}

/**
 * This is a low-level iterator.
 */

template <class uword = uint32_t> class EWAHBoolArrayRawIterator {
public:
  EWAHBoolArrayRawIterator(const EWAHBoolArray<uword> &p)
      : pointer(0), myparent(&p.getBuffer()), rlw((*myparent)[pointer], this) {}
  EWAHBoolArrayRawIterator(const EWAHBoolArrayRawIterator &o)
      : pointer(o.pointer), myparent(o.myparent), rlw(o.rlw) {}

  bool hasNext() const { return pointer < myparent->size(); }

  BufferedRunningLengthWord<uword> &next() {
#ifdef EWAHASSERT
    assert(pointer < myparent->size());
#endif
    rlw.read((*myparent)[pointer]);
    pointer = static_cast<size_t>(pointer + rlw.getNumberOfLiteralWords() + 1);
    return rlw;
  }

  const uword *dirtyWords() const {
#ifdef EWAHASSERT
    assert(pointer > 0);
    assert(pointer >= rlw.getNumberOfLiteralWords());
#endif
    return myparent->data() +
           static_cast<size_t>(pointer - rlw.getNumberOfLiteralWords());
  }

  EWAHBoolArrayRawIterator &operator=(const EWAHBoolArrayRawIterator &other) {
    pointer = other.pointer;
    myparent = other.myparent;
    rlw = other.rlw;
    return *this;
  }

  size_t pointer;
  const std::vector<uword> *myparent;
  BufferedRunningLengthWord<uword> rlw;

  EWAHBoolArrayRawIterator();
};

template <class uword>
EWAHBoolArrayIterator<uword> EWAHBoolArray<uword>::uncompress() const {
  return EWAHBoolArrayIterator<uword>(buffer);
}

template <class uword>
EWAHBoolArrayRawIterator<uword> EWAHBoolArray<uword>::raw_iterator() const {
  return EWAHBoolArrayRawIterator<uword>(*this);
}

#ifndef ALTEQUAL

template <class uword>
bool EWAHBoolArray<uword>::operator==(const EWAHBoolArray &a) const {
  EWAHBoolArrayRawIterator<uword> i = a.raw_iterator();
  EWAHBoolArrayRawIterator<uword> j = raw_iterator();
  if (!(i.hasNext() and j.hasNext())) { // hopefully this never happens...
    return true;
  }
  // at this point, this should be safe:
  BufferedRunningLengthWord<uword> &rlwi = i.next();
  BufferedRunningLengthWord<uword> &rlwj = j.next();
  // RunningLength;
  while (true) {
    bool i_is_prey(rlwi.size() < rlwj.size());
    BufferedRunningLengthWord<uword> &prey(i_is_prey ? rlwi : rlwj);
    BufferedRunningLengthWord<uword> &predator(i_is_prey ? rlwj : rlwi);
    uword predatorrl(predator.getRunningLength());
    const uword preyrl(prey.getRunningLength());
    if (predatorrl >= preyrl) {
      const uword tobediscarded = preyrl;
      if (tobediscarded)
        if (prey.getRunningBit() ^ predator.getRunningBit())
          return false;
    } else {
      const uword tobediscarded = predatorrl;
      if (predatorrl > 0) {
        if (prey.getRunningBit() ^ predator.getRunningBit())
          return false;
      }
      if (preyrl - tobediscarded > 0) {
        return false;
      }
    }
    predator.discardFirstWords(preyrl);
    prey.discardFirstWords(preyrl);

    predatorrl = predator.getRunningLength();
    if (predatorrl > 0) {

      const uword nbre_dirty_prey(prey.getNumberOfLiteralWords());
      const uword tobediscarded =
          (predatorrl >= nbre_dirty_prey) ? nbre_dirty_prey : predatorrl;
      if (tobediscarded > 0) {
        return false;
      }
    }
    // all that is left to do now is to AND the dirty words
    uword nbre_dirty_prey(prey.getNumberOfLiteralWords());
    if (nbre_dirty_prey > 0) {
      const uword *idirty = i.dirtyWords();
      const uword *jdirty = j.dirtyWords();

      for (uword k = 0; k < nbre_dirty_prey; ++k) {
        if ((idirty[k] ^ jdirty[k]) != 0)
          return false;
      }
      predator.discardFirstWords(nbre_dirty_prey);
    }
    if (i_is_prey) {
      if (!i.hasNext())
        break;
      rlwi = i.next();
    } else {
      if (!j.hasNext())
        break;
      rlwj = j.next();
    }
  }
  return true;
}

#else

template <class uword>
bool EWAHBoolArray<uword>::operator==(const EWAHBoolArray &x) const {
  EWAHBoolArrayRawIterator<uword> i = x.raw_iterator();
  EWAHBoolArrayRawIterator<uword> j = raw_iterator();
  if (!(i.hasNext() and j.hasNext())) { // hopefully this never happens...
    return true;
  }
  // at this point, this should be safe:
  BufferedRunningLengthWord<uword> &rlwi = i.next();
  BufferedRunningLengthWord<uword> &rlwj = j.next();

  while ((rlwi.size() > 0) && (rlwj.size() > 0)) {
    while ((rlwi.getRunningLength() > 0) || (rlwj.getRunningLength() > 0)) {
      const bool i_is_prey = rlwi.getRunningLength() < rlwj.getRunningLength();
      BufferedRunningLengthWord<uword> &prey = i_is_prey ? rlwi : rlwj;
      BufferedRunningLengthWord<uword> &predator = i_is_prey ? rlwj : rlwi;
      size_t index = 0;
      const bool nonzero =
          ((!predator.getRunningBit())
               ? prey.nonzero_discharge(predator.getRunningLength(), index)
               : prey.nonzero_dischargeNegated(predator.getRunningLength(),
                                               index));
      if (nonzero) {
        return false;
      }
      if (predator.getRunningLength() - index > 0) {
        if (predator.getRunningBit()) {
          return false;
        }
      }
      predator.discardRunningWordsWithReload();
    }
    const size_t nbre_literal =
        min(rlwi.getNumberOfLiteralWords(), rlwj.getNumberOfLiteralWords());
    if (nbre_literal > 0) {
      for (size_t k = 0; k < nbre_literal; ++k)
        if ((rlwi.getLiteralWordAt(k) ^ rlwj.getLiteralWordAt(k)) != 0)
          return false;
      rlwi.discardFirstWordsWithReload(nbre_literal);
      rlwj.discardFirstWordsWithReload(nbre_literal);
    }
  }
  const bool i_remains = rlwi.size() > 0;
  BufferedRunningLengthWord<uword> &remaining = i_remains ? rlwi : rlwj;
  return !remaining.nonzero_discharge();
}

#endif

template <class uword> void EWAHBoolArray<uword>::swap(EWAHBoolArray &x) {
  buffer.swap(x.buffer);
  size_t tmp = x.sizeinbits;
  x.sizeinbits = sizeinbits;
  sizeinbits = tmp;
  tmp = x.lastRLW;
  x.lastRLW = lastRLW;
  lastRLW = tmp;
}

template <class uword>
void EWAHBoolArray<uword>::append(const EWAHBoolArray &x) {
  if (sizeinbits % wordinbits == 0) {
    // hoping for the best?
    sizeinbits += x.sizeinbits;
    ConstRunningLengthWord<uword> lRLW(buffer[lastRLW]);
    if ((lRLW.getRunningLength() == 0) &&
        (lRLW.getNumberOfLiteralWords() == 0)) {
// it could be that the running length word is empty, in such a case,
// we want to get rid of it!
#ifdef EWAHASSERT
      assert(lastRLW == buffer.size() - 1);
#endif
      lastRLW = x.lastRLW + buffer.size() - 1;
      buffer.resize(buffer.size() - 1);
      buffer.insert(buffer.end(), x.buffer.begin(), x.buffer.end());
    } else {
      lastRLW = x.lastRLW + buffer.size();
      buffer.insert(buffer.end(), x.buffer.begin(), x.buffer.end());
    }
  } else {
    std::stringstream ss;
    ss << "This should really not happen! You are trying to append to a bitmap "
          "having a fractional number of words, that is,  "
       << static_cast<int>(sizeinbits) << " bits with a word size in bits of "
       << static_cast<int>(wordinbits) << ". ";
    ss << "Size of the bitmap being appended: " << x.sizeinbits << " bits."
       << std::endl;
    throw std::invalid_argument(ss.str());
  }
}

template <class uword>
EWAHBoolArrayIterator<uword>::EWAHBoolArrayIterator(
    const std::vector<uword> &parent)
    : pointer(0), myparent(parent), compressedwords(0), literalwords(0), rl(0),
      lw(0), b(0) {
  if (pointer < myparent.size())
    readNewRunningLengthWord();
}

template <class uword>
void EWAHBoolArrayIterator<uword>::readNewRunningLengthWord() {
  literalwords = 0;
  compressedwords = 0;
  ConstRunningLengthWord<uword> rlw(myparent[pointer]);
  rl = rlw.getRunningLength();
  lw = rlw.getNumberOfLiteralWords();
  b = rlw.getRunningBit();
  if ((rl == 0) && (lw == 0)) {
    if (pointer < myparent.size() - 1) {
      ++pointer;
      readNewRunningLengthWord();
    } else {
#ifdef EWAHASSERT
      assert(pointer >= myparent.size() - 1);
#endif
      pointer = myparent.size();
#ifdef EWAHASSERT
      assert(!hasNext());
#endif
    }
  }
}

template <class uword>
BoolArray<uword> EWAHBoolArray<uword>::toBoolArray() const {
  BoolArray<uword> ans(sizeinbits);
  EWAHBoolArrayIterator<uword> i = uncompress();
  size_t counter = 0;
  while (i.hasNext()) {
    ans.setWord(counter++, i.next());
  }
  return ans;
}

template <class uword>
template <class container>
void EWAHBoolArray<uword>::appendRowIDs(container &out,
                                        const size_t offset) const {
  size_t pointer(0);
  size_t currentoffset(offset);
  if (RESERVEMEMORY)
    out.reserve(buffer.size() + 64); // trading memory for speed.
  while (pointer < buffer.size()) {
    ConstRunningLengthWord<uword> rlw(buffer[pointer]);
    if (rlw.getRunningBit()) {
      for (size_t x = 0;
           x < static_cast<size_t>(rlw.getRunningLength() * wordinbits); ++x) {
        out.push_back(currentoffset + x);
      }
    }
    currentoffset = static_cast<size_t>(currentoffset +
                                        rlw.getRunningLength() * wordinbits);
    ++pointer;
    for (uword k = 0; k < rlw.getNumberOfLiteralWords(); ++k) {
      const uword currentword = buffer[pointer];
      for (uint32_t kk = 0; kk < wordinbits; ++kk) {
        if ((currentword & static_cast<uword>(static_cast<uword>(1) << kk)) !=
            0)
          out.push_back(currentoffset + kk);
      }
      currentoffset += wordinbits;
      ++pointer;
    }
  }
}

template <class uword>
bool EWAHBoolArray<uword>::operator!=(const EWAHBoolArray<uword> &x) const {
  return !(*this == x);
}

template <class uword>
bool EWAHBoolArray<uword>::operator==(const BoolArray<uword> &x) const {
  // could be more efficient
  return (this->toBoolArray() == x);
}

template <class uword>
bool EWAHBoolArray<uword>::operator!=(const BoolArray<uword> &x) const {
  // could be more efficient
  return (this->toBoolArray() != x);
}

template <class uword>
size_t EWAHBoolArray<uword>::addStreamOfEmptyWords(const bool v,
                                                   size_t number) {
  if (number == 0)
    return 0;
  sizeinbits += number * wordinbits;
  size_t wordsadded = 0;
  if ((RunningLengthWord<uword>::getRunningBit(buffer[lastRLW]) != v) &&
      (RunningLengthWord<uword>::size(buffer[lastRLW]) == 0)) {
    RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
  } else if ((RunningLengthWord<uword>::getNumberOfLiteralWords(
                  buffer[lastRLW]) != 0) ||
             (RunningLengthWord<uword>::getRunningBit(buffer[lastRLW]) != v)) {
    buffer.push_back(0);
    ++wordsadded;
    lastRLW = buffer.size() - 1;
    if (v)
      RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
  }
  const uword runlen =
      RunningLengthWord<uword>::getRunningLength(buffer[lastRLW]);

  const uword whatwecanadd =
      number < static_cast<size_t>(
                   RunningLengthWord<uword>::largestrunninglengthcount - runlen)
          ? static_cast<uword>(number)
          : static_cast<uword>(
                RunningLengthWord<uword>::largestrunninglengthcount - runlen);
  RunningLengthWord<uword>::setRunningLength(
      buffer[lastRLW], static_cast<uword>(runlen + whatwecanadd));

  number -= static_cast<size_t>(whatwecanadd);
  while (number >= RunningLengthWord<uword>::largestrunninglengthcount) {
    buffer.push_back(0);
    ++wordsadded;
    lastRLW = buffer.size() - 1;
    if (v)
      RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
    RunningLengthWord<uword>::setRunningLength(
        buffer[lastRLW], RunningLengthWord<uword>::largestrunninglengthcount);
    number -= static_cast<size_t>(
        RunningLengthWord<uword>::largestrunninglengthcount);
  }
  if (number > 0) {
    buffer.push_back(0);
    ++wordsadded;
    lastRLW = buffer.size() - 1;
    if (v)
      RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
    RunningLengthWord<uword>::setRunningLength(buffer[lastRLW],
                                               static_cast<uword>(number));
  }
  return wordsadded;
}

template <class uword>
void EWAHBoolArray<uword>::fastaddStreamOfEmptyWords(const bool v,
                                                     size_t number) {
  if ((RunningLengthWord<uword>::getRunningBit(buffer[lastRLW]) != v) &&
      (RunningLengthWord<uword>::size(buffer[lastRLW]) == 0)) {
    RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
  } else if ((RunningLengthWord<uword>::getNumberOfLiteralWords(
                  buffer[lastRLW]) != 0) ||
             (RunningLengthWord<uword>::getRunningBit(buffer[lastRLW]) != v)) {
    buffer.push_back(0);
    lastRLW = buffer.size() - 1;
    if (v)
      RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
  }
  const uword runlen =
      RunningLengthWord<uword>::getRunningLength(buffer[lastRLW]);

  const uword whatwecanadd =
      number < static_cast<size_t>(
                   RunningLengthWord<uword>::largestrunninglengthcount - runlen)
          ? static_cast<uword>(number)
          : static_cast<uword>(
                RunningLengthWord<uword>::largestrunninglengthcount - runlen);
  RunningLengthWord<uword>::setRunningLength(
      buffer[lastRLW], static_cast<uword>(runlen + whatwecanadd));

  number -= static_cast<size_t>(whatwecanadd);
  while (number >= RunningLengthWord<uword>::largestrunninglengthcount) {
    buffer.push_back(0);
    lastRLW = buffer.size() - 1;
    if (v)
      RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
    RunningLengthWord<uword>::setRunningLength(
        buffer[lastRLW], RunningLengthWord<uword>::largestrunninglengthcount);
    number -= static_cast<size_t>(
        RunningLengthWord<uword>::largestrunninglengthcount);
  }
  if (number > 0) {
    buffer.push_back(0);
    lastRLW = buffer.size() - 1;
    if (v)
      RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
    RunningLengthWord<uword>::setRunningLength(buffer[lastRLW],
                                               static_cast<uword>(number));
  }
}

template <class uword>
size_t EWAHBoolArray<uword>::addStreamOfDirtyWords(const uword *v,
                                                   const size_t number) {
  if (number == 0)
    return 0;
  RunningLengthWord<uword> lastRunningLengthWord(buffer[lastRLW]);
  const uword NumberOfLiteralWords =
      lastRunningLengthWord.getNumberOfLiteralWords();
#ifdef EWAHASSERT
  assert(RunningLengthWord<uword>::largestliteralcount >= NumberOfLiteralWords);
#endif
  const size_t whatwecanadd =
      number <
              static_cast<uword>(RunningLengthWord<uword>::largestliteralcount -
                                 NumberOfLiteralWords)
          ? number
          : static_cast<size_t>(
                RunningLengthWord<uword>::largestliteralcount -
                NumberOfLiteralWords); // 0x7FFF-NumberOfLiteralWords);
#ifdef EWAHASSERT
  assert(NumberOfLiteralWords + whatwecanadd >= NumberOfLiteralWords);
  assert(NumberOfLiteralWords + whatwecanadd <=
         RunningLengthWord<uword>::largestliteralcount);
#endif
  lastRunningLengthWord.setNumberOfLiteralWords(
      static_cast<uword>(NumberOfLiteralWords + whatwecanadd));
#ifdef EWAHASSERT
  assert(lastRunningLengthWord.getNumberOfLiteralWords() ==
         NumberOfLiteralWords + whatwecanadd);
#endif
  const size_t leftovernumber = number - whatwecanadd;
  // add the dirty words...
  const size_t oldsize(buffer.size());
  buffer.resize(oldsize + whatwecanadd);
  memcpy(&buffer[oldsize], v, whatwecanadd * sizeof(uword));
  sizeinbits += whatwecanadd * wordinbits;
  size_t wordsadded(whatwecanadd);
  if (leftovernumber > 0) {
    // add
    buffer.push_back(0);
    lastRLW = buffer.size() - 1;
    ++wordsadded;
    wordsadded += addStreamOfDirtyWords(v + whatwecanadd, leftovernumber);
  }
#ifdef EWAHASSERT
  assert(wordsadded >= number);
#endif
  return wordsadded;
}

template <class uword>
size_t EWAHBoolArray<uword>::addStreamOfNegatedDirtyWords(const uword *v,
                                                          const size_t number) {
  if (number == 0)
    return 0;
  RunningLengthWord<uword> lastRunningLengthWord(buffer[lastRLW]);
  const uword NumberOfLiteralWords =
      lastRunningLengthWord.getNumberOfLiteralWords();
#ifdef EWAHASSERT
  assert(RunningLengthWord<uword>::largestliteralcount >= NumberOfLiteralWords);
#endif
  const size_t whatwecanadd =
      number <
              static_cast<uword>(RunningLengthWord<uword>::largestliteralcount -
                                 NumberOfLiteralWords)
          ? number
          : static_cast<size_t>(
                RunningLengthWord<uword>::largestliteralcount -
                NumberOfLiteralWords); // 0x7FFF-NumberOfLiteralWords);
#ifdef EWAHASSERT
  assert(NumberOfLiteralWords + whatwecanadd >= NumberOfLiteralWords);
  assert(NumberOfLiteralWords + whatwecanadd <=
         RunningLengthWord<uword>::largestliteralcount);
#endif
  lastRunningLengthWord.setNumberOfLiteralWords(
      static_cast<uword>(NumberOfLiteralWords + whatwecanadd));
#ifdef EWAHASSERT
  assert(lastRunningLengthWord.getNumberOfLiteralWords() ==
         NumberOfLiteralWords + whatwecanadd);
#endif
  const size_t leftovernumber = number - whatwecanadd;
  // add the dirty words...
  const size_t oldsize(buffer.size());
  buffer.resize(oldsize + whatwecanadd);
  for (size_t k = 0; k < whatwecanadd; ++k) {
    buffer[oldsize + k] = ~v[k];
  }
  // memcpy(&buffer[oldsize], v, whatwecanadd * sizeof(uword));
  sizeinbits += whatwecanadd * wordinbits;
  size_t wordsadded(whatwecanadd);
  if (leftovernumber > 0) {
    // add
    buffer.push_back(0);
    lastRLW = buffer.size() - 1;
    ++wordsadded;
    wordsadded += addStreamOfDirtyWords(v + whatwecanadd, leftovernumber);
  }
#ifdef EWAHASSERT
  assert(wordsadded >= number);
#endif
  return wordsadded;
}

template <class uword> size_t EWAHBoolArray<uword>::addEmptyWord(const bool v) {
  RunningLengthWord<uword> lastRunningLengthWord(buffer[lastRLW]);
  const bool noliteralword =
      (lastRunningLengthWord.getNumberOfLiteralWords() == 0);
  // first, if the last running length word is empty, we align it
  // this
  uword runlen = lastRunningLengthWord.getRunningLength();
  if ((noliteralword) && (runlen == 0)) {
    lastRunningLengthWord.setRunningBit(v);
#ifdef EWAHASSERT
    assert(lastRunningLengthWord.getRunningBit() == v);
#endif
  }
  if ((noliteralword) && (lastRunningLengthWord.getRunningBit() == v) &&
      (runlen < RunningLengthWord<uword>::largestrunninglengthcount)) {
    lastRunningLengthWord.setRunningLength(static_cast<uword>(runlen + 1));
#ifdef EWAHASSERT
    assert(lastRunningLengthWord.getRunningLength() == runlen + 1);
#endif
    return 0;
  } else {
    // we have to start anew
    buffer.push_back(0);
    lastRLW = buffer.size() - 1;
    RunningLengthWord<uword> lastRunningLengthWord2(buffer[lastRLW]);
#ifdef EWAHASSERT
    assert(lastRunningLengthWord2.getRunningLength() == 0);
    assert(lastRunningLengthWord2.getRunningBit() == 0);
    assert(lastRunningLengthWord2.getNumberOfLiteralWords() == 0);
#endif
    lastRunningLengthWord2.setRunningBit(v);
#ifdef EWAHASSERT
    assert(lastRunningLengthWord2.getRunningBit() == v);
#endif
    lastRunningLengthWord2.setRunningLength(1);
#ifdef EWAHASSERT
    assert(lastRunningLengthWord2.getRunningLength() == 1);
    assert(lastRunningLengthWord2.getNumberOfLiteralWords() == 0);
#endif
    return 1;
  }
}

template <class uword>
void fast_logicalor_tocontainer(size_t n, const EWAHBoolArray<uword> **inputs,
                                EWAHBoolArray<uword> &container) {
  class EWAHBoolArrayPtr {

  public:
    EWAHBoolArrayPtr(const EWAHBoolArray<uword> *p, bool o) : ptr(p), own(o) {}
    const EWAHBoolArray<uword> *ptr;
    bool own; // whether to clean

    bool operator<(const EWAHBoolArrayPtr &o) const {
      return o.ptr->sizeInBytes() - ptr->sizeInBytes(); // backward on purpose
    }
  };

  if (n == 0) {
    container.reset();
    return;
  }
  if (n == 1) {
    container = *inputs[0];
    return;
  }

  std::priority_queue<EWAHBoolArrayPtr> pq;
  for (size_t i = 0; i < n; i++) {
    // could use emplace
    pq.push(EWAHBoolArrayPtr(inputs[i], false));
  }
  while (pq.size() > 2) {

    EWAHBoolArrayPtr x1 = pq.top();
    pq.pop();

    EWAHBoolArrayPtr x2 = pq.top();
    pq.pop();

    EWAHBoolArray<uword> *buffer = new EWAHBoolArray<uword>();
    x1.ptr->logicalor(*x2.ptr, *buffer);

    if (x1.own) {
      delete x1.ptr;
    }
    if (x2.own) {
      delete x2.ptr;
    }
    pq.push(EWAHBoolArrayPtr(buffer, true));
  }
  EWAHBoolArrayPtr x1 = pq.top();
  pq.pop();

  EWAHBoolArrayPtr x2 = pq.top();
  pq.pop();

  x1.ptr->logicalor(*x2.ptr, container);

  if (x1.own) {
    delete x1.ptr;
  }
  if (x2.own) {
    delete x2.ptr;
  }
}

template <class uword>
void EWAHBoolArray<uword>::logicalor(const EWAHBoolArray &a,
                                     EWAHBoolArray &container) const {
  container.reset();
  if (RESERVEMEMORY)
    container.buffer.reserve(buffer.size() + a.buffer.size());
  EWAHBoolArrayRawIterator<uword> i = a.raw_iterator();
  EWAHBoolArrayRawIterator<uword> j = raw_iterator();
  if (!(i.hasNext() and j.hasNext())) { // hopefully this never happens...
    container.setSizeInBits(sizeInBits());
    return;
  }
  // at this point, this should be safe:
  BufferedRunningLengthWord<uword> &rlwi = i.next();
  BufferedRunningLengthWord<uword> &rlwj = j.next();

  while ((rlwi.size() > 0) && (rlwj.size() > 0)) {
    while ((rlwi.getRunningLength() > 0) || (rlwj.getRunningLength() > 0)) {
      const bool i_is_prey = rlwi.getRunningLength() < rlwj.getRunningLength();
      BufferedRunningLengthWord<uword> &prey = i_is_prey ? rlwi : rlwj;
      BufferedRunningLengthWord<uword> &predator = i_is_prey ? rlwj : rlwi;
      if (predator.getRunningBit()) {
        container.addStreamOfEmptyWords(true, predator.getRunningLength());
        prey.discardFirstWordsWithReload(predator.getRunningLength());
      } else {
        const size_t index =
            prey.discharge(container, predator.getRunningLength());
        container.addStreamOfEmptyWords(false,
                                        predator.getRunningLength() - index);
      }
      predator.discardRunningWordsWithReload();
    }
    const size_t nbre_literal = std::min(rlwi.getNumberOfLiteralWords(),
                                         rlwj.getNumberOfLiteralWords());
    if (nbre_literal > 0) {
      for (size_t k = 0; k < nbre_literal; ++k) {
        container.addWord(rlwi.getLiteralWordAt(k) | rlwj.getLiteralWordAt(k));
      }
      rlwi.discardFirstWordsWithReload(nbre_literal);
      rlwj.discardFirstWordsWithReload(nbre_literal);
    }
  }
  const bool i_remains = rlwi.size() > 0;
  BufferedRunningLengthWord<uword> &remaining = i_remains ? rlwi : rlwj;
  remaining.discharge(container);
}

template <class uword>
void EWAHBoolArray<uword>::logicalxor(const EWAHBoolArray &a,
                                      EWAHBoolArray &container) const {
  container.reset();
  if (RESERVEMEMORY)
    container.buffer.reserve(buffer.size() + a.buffer.size());
  EWAHBoolArrayRawIterator<uword> i = a.raw_iterator();
  EWAHBoolArrayRawIterator<uword> j = raw_iterator();
  if (!(i.hasNext() and j.hasNext())) { // hopefully this never happens...
    container.setSizeInBits(sizeInBits());
    return;
  }
  // at this point, this should be safe:
  BufferedRunningLengthWord<uword> &rlwi = i.next();
  BufferedRunningLengthWord<uword> &rlwj = j.next();

  while ((rlwi.size() > 0) && (rlwj.size() > 0)) {
    while ((rlwi.getRunningLength() > 0) || (rlwj.getRunningLength() > 0)) {
      const bool i_is_prey = rlwi.getRunningLength() < rlwj.getRunningLength();
      BufferedRunningLengthWord<uword> &prey = i_is_prey ? rlwi : rlwj;
      BufferedRunningLengthWord<uword> &predator = i_is_prey ? rlwj : rlwi;
      const size_t index =
          (!predator.getRunningBit())
              ? prey.discharge(container, predator.getRunningLength())
              : prey.dischargeNegated(container, predator.getRunningLength());
      container.addStreamOfEmptyWords(predator.getRunningBit(),
                                      predator.getRunningLength() - index);
      predator.discardRunningWordsWithReload();
    }
    const size_t nbre_literal = std::min(rlwi.getNumberOfLiteralWords(),
                                         rlwj.getNumberOfLiteralWords());
    if (nbre_literal > 0) {
      for (size_t k = 0; k < nbre_literal; ++k)
        container.addWord(rlwi.getLiteralWordAt(k) ^ rlwj.getLiteralWordAt(k));
      rlwi.discardFirstWordsWithReload(nbre_literal);
      rlwj.discardFirstWordsWithReload(nbre_literal);
    }
  }
  const bool i_remains = rlwi.size() > 0;
  BufferedRunningLengthWord<uword> &remaining = i_remains ? rlwi : rlwj;
  remaining.discharge(container);
}

template <class uword>
void EWAHBoolArray<uword>::logicaland(const EWAHBoolArray &a,
                                      EWAHBoolArray &container) const {
  container.reset();
  if (RESERVEMEMORY)
    container.buffer.reserve(buffer.size() > a.buffer.size() ? buffer.size()
                                                             : a.buffer.size());
  EWAHBoolArrayRawIterator<uword> i = a.raw_iterator();
  EWAHBoolArrayRawIterator<uword> j = raw_iterator();
  if (!(i.hasNext() and j.hasNext())) { // hopefully this never happens...
    container.setSizeInBits(sizeInBits());
    return;
  }
  // at this point, this should be safe:
  BufferedRunningLengthWord<uword> &rlwi = i.next();
  BufferedRunningLengthWord<uword> &rlwj = j.next();

  while ((rlwi.size() > 0) && (rlwj.size() > 0)) {
    while ((rlwi.getRunningLength() > 0) || (rlwj.getRunningLength() > 0)) {
      const bool i_is_prey = rlwi.getRunningLength() < rlwj.getRunningLength();
      BufferedRunningLengthWord<uword> &prey(i_is_prey ? rlwi : rlwj);
      BufferedRunningLengthWord<uword> &predator(i_is_prey ? rlwj : rlwi);
      if (!predator.getRunningBit()) {
        container.fastaddStreamOfEmptyWords(false, predator.getRunningLength());
        prey.discardFirstWordsWithReload(predator.getRunningLength());
      } else {
        const size_t index =
            prey.discharge(container, predator.getRunningLength());
        container.fastaddStreamOfEmptyWords(false, predator.getRunningLength() -
                                                       index);
      }
      predator.discardRunningWordsWithReload();
    }
    const size_t nbre_literal = std::min(rlwi.getNumberOfLiteralWords(),
                                         rlwj.getNumberOfLiteralWords());
    if (nbre_literal > 0) {
      for (size_t k = 0; k < nbre_literal; ++k) {
        container.addWord(rlwi.getLiteralWordAt(k) & rlwj.getLiteralWordAt(k));
      }
      rlwi.discardFirstWordsWithReload(nbre_literal);
      rlwj.discardFirstWordsWithReload(nbre_literal);
    }
  }
  container.setSizeInBits(sizeInBits());
}

template <class uword>
bool EWAHBoolArray<uword>::intersects(const EWAHBoolArray &a) const {
  EWAHBoolArrayRawIterator<uword> i = a.raw_iterator();
  EWAHBoolArrayRawIterator<uword> j = raw_iterator();
  if (!(i.hasNext() and j.hasNext())) { // hopefully this never happens...
    return false;
  }
  // at this point, this should be safe:
  BufferedRunningLengthWord<uword> &rlwi = i.next();
  BufferedRunningLengthWord<uword> &rlwj = j.next();

  while ((rlwi.size() > 0) && (rlwj.size() > 0)) {
    while ((rlwi.getRunningLength() > 0) || (rlwj.getRunningLength() > 0)) {
      const bool i_is_prey = rlwi.getRunningLength() < rlwj.getRunningLength();
      BufferedRunningLengthWord<uword> &prey(i_is_prey ? rlwi : rlwj);
      BufferedRunningLengthWord<uword> &predator(i_is_prey ? rlwj : rlwi);
      if (!predator.getRunningBit()) {
        prey.discardFirstWordsWithReload(predator.getRunningLength());
      } else {
        size_t index = 0;
        bool isnonzero =
            prey.nonzero_discharge(predator.getRunningLength(), index);
        if (isnonzero)
          return true;
      }
      predator.discardRunningWordsWithReload();
    }
    const size_t nbre_literal = std::min(rlwi.getNumberOfLiteralWords(),
                                         rlwj.getNumberOfLiteralWords());
    if (nbre_literal > 0) {
      for (size_t k = 0; k < nbre_literal; ++k) {
        if ((rlwi.getLiteralWordAt(k) & rlwj.getLiteralWordAt(k)) != 0)
          return true;
      }
      rlwi.discardFirstWordsWithReload(nbre_literal);
      rlwj.discardFirstWordsWithReload(nbre_literal);
    }
  }
  return false;
}

template <class uword>
BitmapStatistics EWAHBoolArray<uword>::computeStatistics() const {
  BitmapStatistics bs;
  EWAHBoolArrayRawIterator<uword> i = raw_iterator();
  while (i.hasNext()) {
    BufferedRunningLengthWord<uword> &brlw(i.next());
    ++bs.runningwordmarker;
    bs.totalliteral += brlw.getNumberOfLiteralWords();
    bs.totalcompressed += brlw.getRunningLength();
    if (brlw.getRunningLength() ==
        RunningLengthWord<uword>::largestrunninglengthcount) {
      ++bs.maximumofrunningcounterreached;
    }
  }
  return bs;
}

template <class uword> void EWAHBoolArray<uword>::debugprintout() const {
  std::cout << "==printing out EWAHBoolArray==" << std::endl;
  std::cout << "Number of compressed words: " << buffer.size() << std::endl;
  size_t pointer = 0;
  while (pointer < buffer.size()) {
    ConstRunningLengthWord<uword> rlw(buffer[pointer]);
    bool b = rlw.getRunningBit();
    const uword rl = rlw.getRunningLength();
    const uword lw = rlw.getNumberOfLiteralWords();
    std::cout << "pointer = " << pointer << " running bit=" << b
              << " running length=" << rl << " lit. words=" << lw << std::endl;
    for (uword j = 0; j < lw; ++j) {
      const uword &w = buffer[pointer + j + 1];
      std::cout << toBinaryString(w) << std::endl;
    }
    pointer += lw + 1;
  }
  std::cout << "==END==" << std::endl;
}

template <class uword> size_t EWAHBoolArray<uword>::sizeOnDisk() const {
  return sizeof(sizeinbits) + sizeof(size_t) + sizeof(uword) * buffer.size();
}

#endif
