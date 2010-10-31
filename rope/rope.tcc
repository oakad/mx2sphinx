// rope member functions -*- C++ -*-

// Copyright (C) 2010 Alex Dubov <oakad@yahoo.com>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 3, or
// (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.


// Derived from original implementation
// Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009
// Free Software Foundation, Inc.

// May contain parts
// Copyright (c) 1997
// Silicon Graphics Computer Systems, Inc.

// Permission to use, copy, modify, distribute and sell this software
// and its documentation for any purpose is hereby granted without fee,
// provided that the above copyright notice appear in all copies and
// that both that copyright notice and this permission notice appear
// in supporting documentation.  Silicon Graphics makes no
// representations about the suitability of this software for any
// purpose.  It is provided "as is" without express or implied warranty.


#ifndef _EXT_ROPE_TCC
#define _EXT_ROPE_TCC 1

#include <bits/functexcept.h>
#include <ext/numeric>
#include <algorithm>

template<typename _CharT, typename _Traits, typename _Alloc>
const unsigned long
rope<_CharT, _Traits, _Alloc>::_S_min_len[_S_max_rope_depth + 1] = {
      /* 0 */1, /* 1 */2, /* 2 */3, /* 3 */5, /* 4 */8, /* 5 */13, /* 6 */21,
      /* 7 */34, /* 8 */55, /* 9 */89, /* 10 */144, /* 11 */233, /* 12 */377,
      /* 13 */610, /* 14 */987, /* 15 */1597, /* 16 */2584, /* 17 */4181,
      /* 18 */6765, /* 19 */10946, /* 20 */17711, /* 21 */28657, /* 22 */46368,
      /* 23 */75025, /* 24 */121393, /* 25 */196418, /* 26 */317811,
      /* 27 */514229, /* 28 */832040, /* 29 */1346269, /* 30 */2178309,
      /* 31 */3524578, /* 32 */5702887, /* 33 */9227465, /* 34 */14930352,
      /* 35 */24157817, /* 36 */39088169, /* 37 */63245986, /* 38 */102334155,
      /* 39 */165580141, /* 40 */267914296, /* 41 */433494437,
      /* 42 */701408733, /* 43 */1134903170, /* 44 */1836311903,
      /* 45 */2971215073u
};
// These are Fibonacci numbers < 2**32.


template<typename _CharT, typename _Traits, typename _Alloc>
void rope<_CharT, _Traits, _Alloc>::_S_add_leaf_to_forest(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr __r,
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr *__forest
)
{
	rope_rep_ptr __insertee;
	rope_rep_ptr __too_tiny;
	int __i; // forest[0..__i-1] is empty
	size_type __s(__r->_M_size);

	for (__i = 0; __s >= _S_min_len[__i + 1]/* not this bucket */; ++__i) {
		if (0 != __forest[__i]) {
			__too_tiny = _S_concat_and_set_balanced(__forest[__i],
								__too_tiny);
			__forest[__i].reset();
		}
	}

	__insertee = _S_concat_and_set_balanced(__too_tiny, __r);

	for (;; ++__i) {
		if (__forest[__i]) {
			__insertee = _S_concat_and_set_balanced(__forest[__i],
								__insertee);
			__forest[__i].reset();
		}
		if ((__i == _S_max_rope_depth)
		    || (__insertee->_M_size < _S_min_len[__i+1])) {
			__forest[__i] = __insertee;
			return;
		}
	}
}

template<typename _CharT, typename _Traits, typename _Alloc>
void rope<_CharT, _Traits, _Alloc>::_S_add_to_forest(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr __r,
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr *__forest
)
{
	rope_concat_ptr __c(_S_rep_cast<_rope_concat>(__r));

	if (!__c || __c->_M_is_balanced)
		_S_add_leaf_to_forest(__r, __forest);
	else {
		_S_add_to_forest(__c->_M_left, __forest);
		_S_add_to_forest(__c->_M_right, __forest);
	}
}

template<typename _CharT, typename _Traits, typename _Alloc>
typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr
rope<_CharT, _Traits, _Alloc>::_S_balance(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr __r
)
{
	rope_rep_ptr __forest[_S_max_rope_depth + 1];
	rope_rep_ptr __result;
	int __i;

	// Invariant:
	// The concatenation of forest in descending order is equal to __r.
	// __forest[__i]._M_size >= _S_min_len[__i]
	// __forest[__i]._M_depth = __i

	_S_add_to_forest(__r, __forest);

	for (__i = 0; __i <= _S_max_rope_depth; ++__i)
		if (__forest[__i]) {
			__result = _S_concat(__forest[__i], __result);
			__forest[__i].reset();
		}

	if (__result->_M_depth > _S_max_rope_depth)
		std::__throw_length_error(__N("rope::_S_balance"));

	return __result;
}

template<typename _CharT, typename _Traits, typename _Alloc>
typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr
rope<_CharT, _Traits, _Alloc>::_S_concat(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr __l,
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr __r
)
{
	if (!__l)
		return __r;

	if (!__r)
		return __l;

	rope_leaf_ptr __r_leaf(_S_rep_cast<_rope_leaf>(__r));

	if (!__r_leaf)
		return _S_tree_concat(__l, __r);

	rope_leaf_ptr __l_leaf(_S_rep_cast<_rope_leaf>(__l));

	if (__l_leaf) {
		if ((__l_leaf->_M_size + __r_leaf->_M_size)
		    <= size_type(_S_copy_max))
			return _S_leaf_concat_char_iter(
				__l_leaf, __r_leaf->_M_data,
				__r_leaf->_M_size
			);
	} else {
		rope_concat_ptr __l_cat(_S_rep_cast<_rope_concat>(__l));

		if (__l_cat) {
			rope_leaf_ptr __lr_leaf(
				_S_rep_cast<_rope_leaf>(__l_cat->_M_right)
			);

			if (__lr_leaf
			    && ((__lr_leaf->_M_size + __r_leaf->_M_size)
				<= size_type(_S_copy_max))) {
				rope_rep_ptr __ll(__l_cat->_M_left);
				rope_rep_ptr __rest(_S_leaf_concat_char_iter(
					__lr_leaf, __r_leaf->_M_data,
					__r_leaf->_M_size
				));

				return _S_tree_concat(__ll, __rest);
			}
		}
	}

	return _S_tree_concat(__l, __r);
}

template<typename _CharT, typename _Traits, typename _Alloc>
typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr
rope<_CharT, _Traits, _Alloc>::_S_tree_concat(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr __l,
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr __r
)
{
	rope_concat_ptr __result(_rope_concat::_S_make(__l, __r));
	size_type __depth(__result->_M_depth);

	if ((__depth > 20)
	    && ((__result->_M_size < 1000)
		|| (__depth > size_type(_S_max_rope_depth))))
		return _S_balance(__result);
	else
		return __result;
}

template<typename _CharT, typename _Traits, typename _Alloc>
typename rope<_CharT, _Traits, _Alloc>::rope_leaf_ptr
rope<_CharT, _Traits, _Alloc>::_S_leaf_concat_char_iter(
	typename rope<_CharT, _Traits, _Alloc>::rope_leaf_ptr __r,
	_CharT const *__iter,
	typename rope<_CharT, _Traits, _Alloc>::size_type __len
)
{
	size_type __old_len(__r->_M_size);
	size_type __new_len(__old_len + __len);

	rope_leaf_ptr __result(
		_rope_leaf::_S_make(__new_len, *get_allocator<_Alloc>(__r))
	);

	traits_type::copy(&__result->_M_data[0], __r->_M_data, __old_len);
	traits_type::copy(&__result->_M_data[__old_len], __iter, __len);
	traits_type::assign(__result->_M_data[__new_len], _CharT());

	return __result;
}

template<typename _CharT, typename _Traits, typename _Alloc>
bool rope<_CharT, _Traits, _Alloc>::_rope_concat::_M_apply(
	std::function<bool (_CharT const *, size_type)> __f,
	typename rope<_CharT, _Traits, _Alloc>::size_type __begin,
	typename rope<_CharT, _Traits, _Alloc>::size_type __end
)
{
	rope_rep_ptr __l(_M_left);
	size_type __l_len(__l->_M_size);

	if (__begin < __l_len) {
		size_type __l_end(std::min(__l_len, __end));

		if (!__l->_M_apply(__f, __begin, __l_end))
			return false;
	}

	if (__end > __l_len) {
		rope_rep_ptr __r(_M_right);
		size_type __r_begin(std::max(__l_len, __begin));

		if (!__r->_M_apply(__f, __r_begin - __l_len, __end - __l_len))
			return false;
	}

	return true;
}

template<typename _CharT, typename _Traits, typename _Alloc>
bool rope<_CharT, _Traits, _Alloc>::_rope_leaf::_M_apply(
	std::function<bool (_CharT const *, size_type)> __f,
	typename rope<_CharT, _Traits, _Alloc>::size_type __begin,
	typename rope<_CharT, _Traits, _Alloc>::size_type __end
)
{
	return __f(_M_data + __begin, __end - __begin);
}

template<typename _CharT, typename _Traits, typename _Alloc>
bool rope<_CharT, _Traits, _Alloc>::_rope_substr::_M_apply(
	std::function<bool (_CharT const *, size_type)> __f,
	typename rope<_CharT, _Traits, _Alloc>::size_type __begin,
	typename rope<_CharT, _Traits, _Alloc>::size_type __end
)
{
	return _M_base->_M_apply(__f, __begin + _M_start,
				 std::min(this->_M_size, __end));
}

template<typename _CharT, typename _Traits, typename _Alloc>
bool rope<_CharT, _Traits, _Alloc>::_rope_func::_M_apply(
	std::function<bool (_CharT const *, size_type)> __f,
	typename rope<_CharT, _Traits, _Alloc>::size_type __begin,
	typename rope<_CharT, _Traits, _Alloc>::size_type __end
)
{
	size_type __len(__end - __begin);
	bool __result;

	_CharT_alloc_type __ca(
		this->template _M_get_alloc<_CharT_alloc_type>()
	);

	_CharT *__buf(__ca.allocate(__len));
	__try {
		_M_fn(__begin, __len, __buf);
		__result = __f(__buf, __len);

		__ca.deallocate(__buf, __len);
	} __catch(...) {
		__ca.deallocate(__buf, __len);
		__throw_exception_again;
	}
	return __result;
}

template<typename _CharT, typename _Traits, typename _Alloc>
rope<_CharT, _Traits, _Alloc>::rope(size_type __n, _CharT __c,
				    _Alloc &&__a)
: _M_treeplus(rope_rep_ptr(), __a)
{
	typedef rope<_CharT, _Traits, _Alloc> rope_type;

	size_type const __exponentiate_threshold(32);

	if (__n == 0)
		return;

	size_type __exponent(__n / __exponentiate_threshold);
	size_type __rest(__n % __exponentiate_threshold);
	rope_leaf_ptr __remainder;

	if (__rest != 0)
		__remainder = _rope_leaf::_S_make(__rest, __c, __a);

	rope_type __remainder_rope(__remainder, __a), __result(__a);

	if (__exponent != 0) {
		rope_leaf_ptr __base_leaf(_rope_leaf::_S_make(
			__exponentiate_threshold, __c, __a
		));

		rope_type __base_rope(__base_leaf, __a);

		__result = __base_rope;

		if (__exponent > 1) {
			// A sort of power raising operation.
			while ((__exponent & 1) == 0) {
				__exponent >>= 1;
				__base_rope = __base_rope + __base_rope;
			}

			__result = __base_rope;
			__exponent >>= 1;

			while (__exponent != 0) {
				__base_rope = __base_rope + __base_rope;

				if ((__exponent & 1) != 0)
					__result = __result + __base_rope;

				__exponent >>= 1;
			}
		}

		if (__remainder)
			__result += __remainder_rope;
	} else
		__result = __remainder_rope;

	std::get<0>(this->_M_treeplus) = std::get<0>(__result._M_treeplus);
}

template<typename _InputIterator, typename _Size, typename _OutputIterator>
static bool _out_copy_n(_InputIterator __first, _Size __n,
			_OutputIterator __result)
{
	copy_n(__first, __n, __result);
	return true;
}

template<typename _CharT, typename _Traits, typename _Alloc>
std::basic_ostream<_CharT, _Traits> &operator<<(
	std::basic_ostream<_CharT, _Traits> &__os,
	rope<_CharT, _Traits, _Alloc> const &__r
)
{
	typedef rope<_CharT, _Traits, _Alloc> rope_type;
	typedef typename rope_type::size_type size_type;
	typedef std::ostream_iterator<_CharT, _CharT, _Traits> iter_type;

	iter_type __out_iter(__os);
	size_type __w(__os.width());
	size_type __rope_len(__r.size());
	size_type __pad_len((__rope_len < __w) ? (__w - __rope_len) : 0);
	bool __left(__os.flags() & std::ios::left);

	if (!__left && __pad_len > 0)
		std::fill_n(__out_iter, __pad_len, __os.fill());

	rope_type::_S_apply_to_pieces(
		std::bind(
			&_out_copy_n<_CharT const*, size_type, iter_type>,
			std::placeholders::_1,
			std::placeholders::_2,
			__out_iter),
		std::get<0>(__r._M_treeplus), 0, __rope_len
	);

	if (__left && __pad_len > 0)
		std::fill_n(__out_iter, __pad_len, __os.fill());

	return __os;
}

#endif
