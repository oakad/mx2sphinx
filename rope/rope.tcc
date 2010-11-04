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

#include <ext/algorithm>

template<typename _CharT, typename _Traits, typename _Alloc>
typename rope<_CharT, _Traits, _Alloc>::size_type const
rope<_CharT, _Traits, _Alloc>::npos = static_cast<size_type>(-1);

template<typename _CharT, typename _Traits, typename _Alloc>
unsigned long const
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
const typename rope<_CharT, _Traits, _Alloc>::_rope_rep_ops
rope<_CharT, _Traits, _Alloc>::_S_rep_ops[_rope_tag::_S_last_tag] = {
	{&_rope_rep::_S_apply, &_rope_rep::_S_substring},
	{&_rope_leaf::_S_apply, &_rope_leaf::_S_substring},
	{&_rope_concat::_S_apply, &_rope_concat::_S_substring},
	{&_rope_substr::_S_apply, &_rope_substr::_S_substring},
	{&_rope_func::_S_apply, &_rope_func::_S_substring}
};

template<typename _CharT, typename _Traits, typename _Alloc>
void rope<_CharT, _Traits, _Alloc>::_S_add_leaf_to_forest(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r,
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr *__forest
)
{
	int __i; // forest[0..__i-1] is empty
	size_type __s(__r->_M_size);
	rope_rep_ptr __too_tiny;

	for (__i = 0; __s >= _S_min_len[__i + 1]/* not this bucket */; ++__i) {
		if (0 != __forest[__i]) {
			__too_tiny = _S_concat_and_set_balanced(__forest[__i],
								__too_tiny);
			__forest[__i].reset();
		}
	}

	rope_rep_ptr __insertee(_S_concat_and_set_balanced(__too_tiny, __r));

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
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r,
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
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r
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
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__l,
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r
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
		    <= size_type(_S_max_copy))
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
				<= size_type(_S_max_copy))) {
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
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__l,
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r
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
template<typename _InputIterator>
typename rope<_CharT, _Traits, _Alloc>::rope_leaf_ptr
rope<_CharT, _Traits, _Alloc>::_S_leaf_concat_char_iter(
	typename rope<_CharT, _Traits, _Alloc>::rope_leaf_ptr const &__r,
	_InputIterator __iter,
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

	return __result;
}

template<typename _CharT, typename _Traits, typename _Alloc>
template<typename _InputIterator>
typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr
rope<_CharT, _Traits, _Alloc>::_S_concat_char_iter(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r,
	_InputIterator __iter,
	typename rope<_CharT, _Traits, _Alloc>::size_type __len
)
{
	if (!__len)
		return __r;

	rope_leaf_ptr __l(_S_rep_cast<_rope_leaf>(__r));

	if (__l && __l->_M_size + __len <= rope_type::_S_copy_max)
		return _S_leaf_concat_char_iter(__l, __iter, __len);
	else {
		rope_concat_ptr __c(_S_rep_cast<_rope_concat>(__r));
		if (__c) {
			__l = _S_rep_cast<_rope_leaf>(__c->_M_right);

			if (__l
			    && __l->_M_size + __len <= rope_type::_S_copy_max) {
				rope_leaf_ptr __right(
					_S_leaf_concat_char_iter(
						__l, __iter, __len
					)
				);
				return _S_tree_concat(__c->_M_left, __right);
			}
		}
	}

	__l = _rope_leaf::_S_make(__iter, __len, *get_allocator<_Alloc>(__r));

	return _S_tree_concat(__r, __l);
}


template<typename _InputIterator, typename _Size, typename _OutputIterator>
static bool _out_copy_n(_InputIterator __first, _Size __n,
			_OutputIterator __result)
{
	copy_n(__first, __n, __result);
	return true;
}

template<typename _CharT, typename _Traits, typename _Alloc>
_CharT *
rope<_CharT, _Traits, _Alloc>::_S_flatten(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r,
	typename rope<_CharT, _Traits, _Alloc>::size_type __begin,
	typename rope<_CharT, _Traits, _Alloc>::size_type __len,
	_CharT *__s)
{
	size_type __end(__begin + std::min(__len, __r->_M_size));

	_S_apply(
		__r,
		std::bind(
			&_out_copy_n<_CharT const*, size_type, _CharT *>,
			std::placeholders::_1,
			std::placeholders::_2,
			__s
		),
		__begin, __end
	);

	return __s + (__end - __begin);
}

template<typename _CharT, typename _Traits, typename _Alloc>
_CharT
rope<_CharT, _Traits, _Alloc>::_S_fetch(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r,
	typename rope<_CharT, _Traits, _Alloc>::size_type __pos
)
{
	_CharT __c;

	_S_apply(
		__r,
		std::bind(
			&_out_copy_n<_CharT const*, size_type, _CharT *>,
			std::placeholders::_1,
			std::placeholders::_2,
			&__c
		),
		__pos, __pos + 1
	);

	return __c;
}

template<typename _CharT, typename _Traits, typename _Alloc>
bool rope<_CharT, _Traits, _Alloc>::_rope_concat::_S_apply(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r,
	std::function<bool (_CharT const *, size_type)> __f,
	typename rope<_CharT, _Traits, _Alloc>::size_type __begin,
	typename rope<_CharT, _Traits, _Alloc>::size_type __end
)
{
	rope_concat_ptr __c(static_pointer_cast<_rope_concat>(__r));
	rope_rep_ptr __left(__c->_M_left);
	size_type __left_len(__left->_M_size);

	if (__begin < __left_len) {
		size_type __left_end(std::min(__left_len, __end));

		if (!rope_type::_S_apply(__left, __f, __begin, __left_end))
			return false;
	}

	if (__end > __left_len) {
		rope_rep_ptr __right(__c->_M_right);
		size_type __right_begin(std::max(__left_len, __begin));

		if (!rope_type::_S_apply(__right, __f,
					 __right_begin - __left_len,
					 __end - __left_len))
			return false;
	}

	return true;
}

template<typename _CharT, typename _Traits, typename _Alloc>
typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr
rope<_CharT, _Traits, _Alloc>::_rope_leaf::_S_substring(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r,
	typename rope<_CharT, _Traits, _Alloc>::size_type __begin,
	typename rope<_CharT, _Traits, _Alloc>::size_type __end,
	typename rope<_CharT, _Traits, _Alloc>::size_type __adj_end
)
{
	if (__begin >= __adj_end)
		return rope_rep_ptr();

	rope_leaf_ptr __l(static_pointer_cast<_rope_leaf>(__r));
	size_type __result_len(__adj_end - __begin);

	if (__result_len > _S_lazy_threshold)
		return _rope_substr::_S_make(
			__r, __begin, __adj_end - __begin,
			*get_allocator<_Alloc>(__r)
		);
	else
		return _rope_leaf::_S_make(
			__l->_M_data + __begin, __result_len,
			*get_allocator<_Alloc>(__l)
		);
}

template<typename _CharT, typename _Traits, typename _Alloc>
typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr
rope<_CharT, _Traits, _Alloc>::_rope_concat::_S_substring(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r,
	typename rope<_CharT, _Traits, _Alloc>::size_type __begin,
	typename rope<_CharT, _Traits, _Alloc>::size_type __end,
	typename rope<_CharT, _Traits, _Alloc>::size_type __adj_end
)
{
	rope_concat_ptr __c(static_pointer_cast<_rope_concat>(__r));

	rope_rep_ptr __left(__c->_M_left), __right(__c->_M_right);
	size_type __left_len = __left->_M_size;

	if (__adj_end <= __left_len)
		return rope_type::_S_substring(__left, __begin, __end);
	else if (__begin >= __left_len)
		return rope_type::_S_substring(__right, __begin - __left_len,
					       __adj_end - __left_len);

	return _S_concat(
		rope_type::_S_substring(__left, __begin, __left_len),
		rope_type::_S_substring(__right, 0, __end - __left_len)
	);
}

template<typename _CharT, typename _Traits, typename _Alloc>
typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr
rope<_CharT, _Traits, _Alloc>::_rope_substr::_S_substring(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r,
	typename rope<_CharT, _Traits, _Alloc>::size_type __begin,
	typename rope<_CharT, _Traits, _Alloc>::size_type __end,
	typename rope<_CharT, _Traits, _Alloc>::size_type __adj_end
)
{
	if (__begin >= __adj_end)
		return rope_rep_ptr();

	// Avoid introducing multiple layers of substring nodes.
	rope_substr_ptr __old(static_pointer_cast<_rope_substr>(__r));
	size_type __result_len(__adj_end - __begin);

	if (__result_len > _S_lazy_threshold)
		return _rope_substr::_S_make(
			__old->_M_base, __begin + __old->_M_start,
			__adj_end - __begin, *get_allocator<_Alloc>(__old)
		);
	else
		return rope_type::_S_substring(
			__old->_M_base, __begin + __old->_M_start,
			__adj_end - __begin
		);
}

template<typename _CharT, typename _Traits, typename _Alloc>
typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr
rope<_CharT, _Traits, _Alloc>::_rope_func::_S_substring(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r,
	typename rope<_CharT, _Traits, _Alloc>::size_type __begin,
	typename rope<_CharT, _Traits, _Alloc>::size_type __end,
	typename rope<_CharT, _Traits, _Alloc>::size_type __adj_end
)
{
	if (__begin >= __adj_end)
		return rope_rep_ptr();

	rope_func_ptr __f(static_pointer_cast<_rope_func>(__r));
	size_type __result_len(__adj_end - __begin);

	if (__result_len > _S_lazy_threshold)
		return _rope_substr::_S_make(
			__r, __begin, __adj_end - __begin,
			*get_allocator<_Alloc>(__r)
		);
	else {
		rope_leaf_ptr __result(
			_rope_leaf::_S_make(__result_len,
					    *get_allocator<_Alloc>(__r))
		);

		__f->_M_fn(__begin, __result_len, __result->_M_data);
		return __result;
	}
}

template<typename _CharT, typename _Traits, typename _Alloc>
void
rope<_CharT, _Traits, _Alloc>::_iterator_base::_S_setbuf(
	typename rope<_CharT, _Traits, _Alloc>::_iterator_base &__iter
)
{
	size_type __leaf_pos(__iter._M_leaf_pos);
	size_type __pos(__iter._M_current_pos);

	rope_leaf_ptr __l(
		_S_rep_cast<_rope_leaf>(
			__iter._M_path_end[__iter._M_path_index]
		)
	);

	if (__l) {
		__iter._M_buf_begin = __l->_M_data;
		__iter._M_buf_cur = __iter._M_buf_begin + (__pos - __leaf_pos);
		__iter._M_buf_end = __iter._M_buf_start + __l->_M_size;
	} else {
		size_type __len(_S_iterator_buf_len);
		size_type __buf_start_pos(__leaf_pos);
		size_t __leaf_end(
			__iter._M_path_end[__iter._M_path_index]->_M_size
			+ __leaf_pos
		);

		if (__buf_start_pos + __len <= __pos) {
			__buf_start_pos = __pos - __len / 4;

			if (__buf_start_pos + __len > __leaf_end)
				__buf_start_pos = __leaf_end - __len;
		}

		if (__buf_start_pos + __len > __leaf_end)
			__len = __leaf_end - __buf_start_pos;

		_S_apply(
			__iter._M_path_end[__iter._M_path_index],
			std::bind(
				&_out_copy_n<_CharT const*, size_type,
					     _CharT *>,
				std::placeholders::_1,
				std::placeholders::_2,
				__iter._M_tmp_buf
			),
			__buf_start_pos - __leaf_pos,
			__buf_start_pos - __leaf_pos + __len
		);

		__iter._M_buf_cur = __iter._M_tmp_buf
				    + (__pos - __buf_start_pos);
		__iter._M_buf_begin = __iter._M_tmp_buf;
		__iter._M_buf_end = __iter._M_tmp_buf + __len;
	}
}

template<typename _CharT, typename _Traits, typename _Alloc>
void
rope<_CharT, _Traits, _Alloc>::_iterator_base::_S_setcache(
	typename rope<_CharT, _Traits, _Alloc>::_iterator_base &__iter
)
{
	size_type __pos(__iter._M_current_pos);

	if (__pos >= __iter._M_root->_M_size) {
		__iter._M_buf_cur = 0;
		return;
	}

	rope_rep_ptr __path[_S_max_rope_depth + 1];
	// Index into path.
	int __cur_depth(-1);

	size_type __cur_start_pos(0);

	// Bit vector marking right turns in the path.
	unsigned int __dirns(0);

	rope_rep_ptr __cur_rope(__iter._M_root);

	while (true) {
		++__cur_depth;
		__path[__cur_depth] = __cur_rope;
		rope_concat_ptr __c(_S_rep_cast<_rope_concat>(__cur_rope));

		if (__c) {
			size_type __left_len(__c->_M_left->_M_size);

			__dirns <<= 1;

			if (__pos >= __cur_start_pos + __left_len) {
				__dirns |= 1;
				__cur_rope = __c->_M_right;
				__cur_start_pos += __left_len;
			} else
				__cur_rope = __c->_M_left;
		} else {
			__iter._M_leaf_pos = __cur_start_pos;
			break;
		}
	}

	// Copy last section of path into _M_path_end.
	{
		int __i(-1);
		int __j(__cur_depth + 1 - _S_path_cache_len);

		if (__j < 0)
			__j = 0;

		while (__j <= __cur_depth)
			__iter._M_path_end[++__i] = __path[__j++];

		__iter._M_path_index = __i;
	}

	__iter._M_path_directions = __dirns;
	_S_setbuf(__iter);
}

template<typename _CharT, typename _Traits, typename _Alloc>
void
rope<_CharT, _Traits, _Alloc>::_iterator_base::_S_setcache_for_incr(
	typename rope<_CharT, _Traits, _Alloc>::_iterator_base &__iter
)
{
	int __current_index(__iter._M_path_index);
	size_type __len(__iter._M_path_end[__current_index]->_M_size);
	size_type __node_start_pos(__iter._M_leaf_pos);
	unsigned int __dirns(__iter._M_path_directions);

	if (__iter._M_current_pos - __node_start_pos < __len) {
		/* More stuff in this leaf, we just didn't cache it. */
		_S_setbuf(__iter);
		return;
	}

	//  __node_start_pos is starting position of last node.
	while (--__current_index >= 0) {
		if (!(__dirns & 1)) /* Path turned left */
			break;

		rope_concat_ptr __c(
			static_pointer_cast<_rope_concat>(
				__iter._M_path_end[__current_index]
			)
		);

		// Otherwise we were in the right child.  Thus we should pop
		// the concatenation node.
		__node_start_pos -= __c->_M_left->_M_size;
		__dirns >>= 1;
	}

	if (__current_index < 0) {
		// We underflowed the cache. Punt.
		_S_setcache(__iter);
		return;
	}

	// Node at __current_index is a concatenation node.  We are positioned
	// on the first character in its right child..
	// __node_start_pos is starting position of current_node.
	rope_concat_ptr __c(
		static_pointer_cast<_rope_concat>(
			__iter._M_path_end[__current_index]
		)
	);

	__node_start_pos += __c->_M_left->_M_size;
	__iter._M_path_end[++__current_index] = __c->_M_right;
	__dirns |= 1;
	__c = _S_rep_cast<_rope_concat>(__c->_M_right);

	while (__c) {
		++__current_index;

		if (_S_path_cache_len == __current_index) {
			int __i;

			for (int __i = 0; __i < (_S_path_cache_len - 1); ++__i)
				__iter._M_path_end[__i]
				= __iter._M_path_end[__i+1];

			--__current_index;
		}

		__iter._M_path_end[__current_index] = __c->_M_left;
		__dirns <<= 1;
		__c = _S_rep_cast<_rope_concat>(__c->_M_left);
		// node_start_pos is unchanged.
	}

	__iter._M_path_index = __current_index;
	__iter._M_leaf_pos = __node_start_pos;
	__iter._M_path_directions = __dirns;
	_S_setbuf(__iter);
}

template<typename _CharT, typename _Traits, typename _Alloc>
void
rope<_CharT, _Traits, _Alloc>::_iterator_base::_M_incr(
	typename rope<_CharT, _Traits, _Alloc>::size_type __n
)
{
	_M_current_pos += __n;

	if (_M_buf_cur) {
		size_type __chars_left(_M_buf_end - _M_buf_cur);

		if (__chars_left > __n)
			_M_buf_cur += __n;
		else if (__chars_left == __n) {
			_M_buf_cur += __n;
			_S_setcache_for_incr(*this);
		} else
			_M_buf_cur = 0;
	}
}

template<typename _CharT, typename _Traits, typename _Alloc>
void
rope<_CharT, _Traits, _Alloc>::_iterator_base::_M_decr(
	typename rope<_CharT, _Traits, _Alloc>::size_type __n
)
{
	if (_M_buf_cur) {
		size_type __chars_left(_M_buf_cur - _M_buf_begin);

		if (__chars_left >= __n)
			_M_buf_cur -= __n;
		else
			_M_buf_cur = 0;
	}

	_M_current_pos -= __n;
}

template<typename _CharT, typename _Traits, typename _Alloc>
std::basic_ostream<_CharT, _Traits> &
rope<_CharT, _Traits, _Alloc>::_S_dump(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__r,
	std::basic_ostream<_CharT, _Traits> &__os,
	int __indent
)
{
	typedef std::ostream_iterator<_CharT, _CharT, _Traits> iter_type;

	iter_type __out_iter(__os);

	std::fill_n(__out_iter, __indent, __os.fill());


	if (!__r) {
		__os << "NULL\n";
		return __os;
	}

	if (__r->_M_tag == _rope_tag::_S_concat) {
		// To maintain precise diagnostics we must avoid incrementing
		// reference counts here.

		_rope_concat const *__c(
			static_cast<_rope_concat const *>(__r.get())
		);

		rope_rep_ptr const &__left(__c->_M_left);
		rope_rep_ptr const &__right(__c->_M_right);

		__os << "Concatenation " << __r.get() << " (rc = "
		     << __r.use_count() << ", depth = "
		     << static_cast<int>(__r->_M_depth)
		     << ", size = " << __r->_M_size << ", "
		     << (__r->_M_is_balanced ? "" : "not") << " balanced)\n";

		return _S_dump(__right, _S_dump(__left, __os, __indent + 2),
			       __indent + 2);
	} else {
		char const *__kind;

		switch (__r->_M_tag) {
		case _rope_tag::_S_leaf:
			__kind = "Leaf";
			break;
		case _rope_tag::_S_substr:
			__kind = "Substring";
			break;
		case _rope_tag::_S_func:
			__kind = "Function";
			break;
		default:
			__kind = "(corrupted kind field!)";
		}

		__os << __kind << " " << __r.get() << " (rc = "
		     << __r.use_count() << ", depth = "
		     << static_cast<int>(__r->_M_depth)
		     << ", size = " << __r->_M_size << ") ";

		_CharT __s[_S_max_printout_len];
		size_type __s_len;

		{
			rope_rep_ptr __prefix(
				_S_substring(__r, 0, _S_max_printout_len)
			);

			_S_flatten(__prefix, __s);
			__s_len = __prefix->_M_size;
		}

		__out_iter = __os;

		std::copy_n(__s, __s_len, __out_iter);

		__os << (__r->_M_size > __s_len ? "...\n" : "\n");

		return __os;
	}
}

template<typename _CharT, typename _Traits, typename _Alloc>
int
rope<_CharT, _Traits, _Alloc>::_S_compare(
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__left,
	typename rope<_CharT, _Traits, _Alloc>::rope_rep_ptr const &__right
)
{
	if (!__right)
		return (!__left) ? 1 : 0;

	if (!__left)
		return -1;

	size_type __left_len(__left->_M_size);
	size_type __right_len(__right->_M_size);

	rope_leaf_ptr __l(_S_rep_cast<_rope_leaf>(__left));

	if (__l) {
		rope_leaf_ptr __r(_S_rep_cast<_rope_leaf>(__right));
		if (__r)
			return __gnu_cxx::lexicographical_compare_3way(
				__l->_M_data, __l->_M_data + __left_len,
				__r->_M_data, __r->_M_data + __right_len
			);
		else {
			const_iterator __rstart(__right, 0);
			const_iterator __rend(__right, __right_len);
			return __gnu_cxx::lexicographical_compare_3way(
				__l->_M_data, __l->_M_data + __left_len,
				__rstart, __rend
			);
		}
	} else {
		const_iterator __lstart(__left, 0);
		const_iterator __lend(__left, __left_len);
		rope_leaf_ptr __r(_S_rep_cast<_rope_leaf>(__right));

		if (__r)
			return __gnu_cxx::lexicographical_compare_3way(
				__lstart, __lend,
				__r->_M_data, __r->_M_data + __right_len
			);
		else {
			const_iterator __rstart(__right, 0);
			const_iterator __rend(__right, __right_len);
			return __gnu_cxx::lexicographical_compare_3way(
				__lstart, __lend, __rstart, __rend
			);
		}
	}
}

template<typename _CharT, typename _Traits, typename _Alloc>
rope<_CharT, _Traits, _Alloc>::rope(size_type __n, _CharT __c,
				    _Alloc __a)
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

template<typename _CharT, typename _Traits, typename _Alloc>
typename rope<_CharT, _Traits, _Alloc>::size_type
rope<_CharT, _Traits, _Alloc>::find(
	_CharT __c,
	typename rope<_CharT, _Traits, _Alloc>::size_type __pos
) const
{
	const_iterator __result(
		std::search_n(const_begin() + __pos, const_end(), 1, __c)
	);
	size_type __result_pos(__result.index());

	if (__result_pos == size())
		__result_pos = npos;

	return __result_pos;
}

template<typename _CharT, typename _Traits, typename _Alloc>
typename rope<_CharT, _Traits, _Alloc>::size_type
rope<_CharT, _Traits, _Alloc>::find(
	_CharT const *__s,
	typename rope<_CharT, _Traits, _Alloc>::size_type __pos
) const
{
	const_iterator __result(
		std::search(const_begin() + __pos, const_end(),
			    __s, __s + traits_type::length(__s))
	);
	size_type __result_pos(__result.index());

	if (__result_pos == size())
		__result_pos = npos;

	return __result_pos;
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

	rope_type::_S_apply(
		std::get<0>(__r._M_treeplus),
		std::bind(
			&_out_copy_n<_CharT const*, size_type, iter_type>,
			std::placeholders::_1,
			std::placeholders::_2,
			__out_iter
		),
		0, __rope_len
	);

	if (__left && __pad_len > 0)
		std::fill_n(__out_iter, __pad_len, __os.fill());

	return __os;
}

#endif
