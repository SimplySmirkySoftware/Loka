#ifndef LOKA_WEB_TEXT_SCALAR_HPP
#define LOKA_WEB_TEXT_SCALAR_HPP
#include <cstddef>
/** Decode one scalar with WHATWG malformed-prefix consumption. No whitespace policy. Requires index < length and
   non-null bytes. Cost: each builder/normalizer once per scalar; reads only its borrowed byte range. */
bool WebTextScalar(const char *, std::size_t, std::size_t &, unsigned int &);
#endif
