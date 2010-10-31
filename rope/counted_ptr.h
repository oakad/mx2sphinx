// Simple, non-intrusive counted pointer class -*- C++ -*-

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

#ifndef _EXT_COUNTED_PTR
#define _EXT_COUNTED_PTR

#include <ext/atomicity.h>
#include <debug/debug.h>
#include <functional>

namespace __counted
{

struct counted_base
{
	counted_base()
	: _M_use_count(1)
	{}

	virtual ~counted_base()
	{}

	virtual void _M_add_ref_copy()
	{
		__gnu_cxx::__atomic_add_dispatch(&_M_use_count, 1);
	}

	virtual void _M_release()
	{
		if (__gnu_cxx::__exchange_and_add_dispatch(&_M_use_count, -1)
		    == 1)
			_M_destroy();
	}

	long _M_get_use_count() const
	{
		return const_cast<const volatile _Atomic_word &>(_M_use_count);
	}

	virtual void _M_destroy() = 0;

private:
	_Atomic_word  _M_use_count;

	counted_base(counted_base const&);
	counted_base &operator=(counted_base const&);
};

template<typename _Tp>
struct ref_count : public counted_base
{
	virtual ~ref_count()
	{}

	virtual void *_M_get_allocator(std::type_info const &__ti)
	{
		return 0;
	}

	virtual _Tp *_M_get_ptr() = 0;

	virtual _Tp const *_M_get_ptr() const = 0;

	long _M_get_use_count() const
	{
		return counted_base::_M_get_use_count();
	}

	bool _M_unique() const
	{
		return this->_M_get_use_count() == 1;
	}

	virtual void *_M_get_extra()
	{
		return 0;
	}

	virtual void const *_M_get_extra() const
	{
		return 0;
	}

	virtual void *_M_get_extra(size_t &__sz)
	{
		__sz = 0;
		return 0;
	}

	virtual void const *_M_get_extra(size_t &__sz) const
	{
		__sz = 0;
		return 0;
	}
};

template<typename _Tp>
struct ref_count_val
{
	static ref_count<_Tp> *_S_get_this(_Tp *__p)
	{
		return *reinterpret_cast<ref_count<_Tp> **>(
			reinterpret_cast<char *>(__p) - sizeof(ref_count<_Tp> *)
		);
	}

	template<typename... _Args>
	ref_count_val(ref_count<_Tp> *__ref, _Args&&... __args)
	: _M_ref(__ref), _M_v(std::forward<_Args>(__args)...)
	{}

	_Tp *_M_get_ptr()
	{
		return &_M_v;
	}

	_Tp const *_M_get_ptr() const
	{
		return &_M_v;
	}

private:
	ref_count<_Tp> *_M_ref;
	_Tp             _M_v;
};

template<typename _Tp>
struct ref_count_p : public ref_count<_Tp>
{
	template<typename... _Args>
	static ref_count<_Tp> *_S_create(_Args&&... __args)
	{
		return ::new ref_count_p(std::forward<_Args>(__args)...);
	}

	virtual ~ref_count_p()
	{}

	virtual void _M_destroy()
	{
		::delete this;
	}

	virtual _Tp *_M_get_ptr()
	{
		return _M_val._M_get_ptr();
	}

	virtual _Tp const *_M_get_ptr() const
	{
		return _M_val._M_get_ptr();
	}

private:
	template<typename... _Args>
	ref_count_p(_Args&&... __args)
	:_M_val(this, std::forward<_Args>(__args)...)
	{}

	ref_count_val<_Tp> _M_val;
};

template<typename _Tp>
struct ref_count_e : public ref_count<_Tp>
{
	template<typename... _Args>
	static ref_count<_Tp> *_S_create(size_t __extra_size, _Args&&... __args)
	{
		void *__p(::operator new(sizeof(ref_count_e<_Tp>)
					 + __extra_size));

		__try {
			return ::new (__p) ref_count_e(
				__extra_size, std::forward<_Args>(__args)...
			);
		} __catch (...) {
			::operator delete(__p);
			__throw_exception_again;
		}
	}

	virtual ~ref_count_e()
	{}

	virtual void _M_destroy()
	{
		void *__p(this);
		this->~ref_count_e<_Tp>();

		::operator delete(__p,
				  sizeof(ref_count_e<_Tp>) + _M_extra_size);
	}

	virtual _Tp *_M_get_ptr()
	{
		return _M_val._M_get_ptr();
	}

	virtual _Tp const *_M_get_ptr() const
	{
		return _M_val._M_get_ptr();
	}

	virtual void *_M_get_extra()
	{
		return _M_extra_storage;
	}

	virtual void const *_M_get_extra() const
	{
		return _M_extra_storage;
	}

	virtual void *_M_get_extra(size_t &__sz)
	{
		__sz = _M_extra_size;
		return _M_extra_storage;
	}

	virtual void const *_M_get_extra(size_t &__sz) const
	{
		__sz = _M_extra_size;
		return _M_extra_storage;
	}

private:
	template<typename... _Args>
	ref_count_e(size_t __extra_size, _Args&&... __args)
	:_M_extra_size(__extra_size),
	 _M_val(this, std::forward<_Args>(__args)...)
	{}

	size_t             _M_extra_size;
	ref_count_val<_Tp> _M_val;
	char               _M_extra_storage[];
};

template<typename _Tp, typename _Alloc>
struct ref_count_a : public ref_count<_Tp>
{
	template<typename... _Args>
	static ref_count<_Tp> *_S_create(_Alloc __a, _Args&&... __args)
	{
		typename _Alloc::template rebind<this_type>::other __a2(__a);

		ref_count_a *__p(__a2.allocate(1));

		__try {
			return ::new (__p) ref_count_a(
				std::forward<_Alloc>(__a),
				std::forward<_Args>(__args)...
			);
		} __catch(...) {
			__a2.deallocate(__p, 1);
			__throw_exception_again;
		}
	}

	virtual ~ref_count_a()
	{}

	virtual void _M_destroy()
	{
		typename _Alloc::template rebind<this_type>::other
		__a(std::get<1>(_M_valplus));

		__a.destroy(this);
		__a.deallocate(this, 1);
	}

	virtual void *_M_get_allocator(std::type_info const &__ti)
	{
		if (__ti == typeid(_Alloc))
			return &std::get<1>(_M_valplus);
		else
			return 0;
	}

	virtual _Tp *_M_get_ptr()
	{
		return std::get<0>(_M_valplus)._M_get_ptr();
	}

	virtual _Tp const *_M_get_ptr() const
	{
		return std::get<0>(_M_valplus)._M_get_ptr();
	}

private:
	typedef ref_count_a<_Tp, _Alloc> this_type;

	template<typename... _Args>
	ref_count_a(_Alloc __a, _Args&&... __args)
	:_M_valplus(ref_count_val<_Tp>(this, std::forward<_Args>(__args)...),
		    std::forward<_Alloc>(__a))
	{}

	std::tuple<ref_count_val<_Tp>, _Alloc> _M_valplus;
};

template<typename _Tp, typename _Alloc>
struct ref_count_a_e : public ref_count<_Tp>
{
	template<typename... _Args>
	static ref_count<_Tp> *_S_create(_Alloc __a, size_t __extra_size,
					 _Args&&... __args)
	{
		size_t __sz(sizeof(this_type) + __extra_size);
		typename _Alloc::template rebind<char>::other
		__raw(__a);

		void *__p(__raw.allocate(__sz));

		__try {
			return ::new (__p) ref_count_a_e(
				std::forward<_Alloc>(__a), __extra_size,
				std::forward<_Args>(__args)...
			);
		} catch (...) {
			__raw.deallocate(reinterpret_cast<char *>(__p), __sz);
			__throw_exception_again;
		}
	}

	virtual ~ref_count_a_e()
	{}

	virtual void _M_destroy()
	{
		size_t __sz(sizeof(this_type) + std::get<0>(_M_alloc));
		typename _Alloc::template rebind<char>::other
		__raw(std::get<1>(_M_alloc));

		this->~ref_count_a_e();
		__raw.deallocate(reinterpret_cast<char *>(this), __sz);
	}

	virtual void *_M_get_allocator(std::type_info const &__ti)
	{
		if (__ti == typeid(_Alloc))
			 return &std::get<1>(_M_alloc);
		else
			return 0;
	}

	virtual _Tp *_M_get_ptr()
	{
		return _M_val._M_get_ptr();
	}

	virtual _Tp const *_M_get_ptr() const
	{
		return _M_val._M_get_ptr();
	}

	virtual void *_M_get_extra()
	{
		return _M_extra_storage;
	}

	virtual void const *_M_get_extra() const
	{
		return _M_extra_storage;
	}

	virtual void *_M_get_extra(size_t &__sz)
	{
		__sz = std::get<0>(_M_alloc);
		return _M_extra_storage;
	}

	virtual void const *_M_get_extra(size_t &__sz) const
	{
		__sz = std::get<0>(_M_alloc);
		return _M_extra_storage;
	}

private:
	typedef ref_count_a_e<_Tp, _Alloc> this_type;

	template<typename... _Args>
	ref_count_a_e(_Alloc __a, size_t __extra_size, _Args&&... __args)
	: _M_alloc(__extra_size, std::forward<_Alloc>(__a)),
	  _M_val(this, std::forward<_Args>(__args)...)
	{}

	std::tuple<size_t, _Alloc> _M_alloc;
	ref_count_val<_Tp>         _M_val;
	char                       _M_extra_storage[];
};

};

template<typename _Tp>
struct counted_ptr;

template<typename _Tp, typename _Alloc, typename... _Args>
counted_ptr<_Tp> allocate_counted(
	_Alloc __a,
	typename counted_ptr<_Tp>::extra_size __extra_size,
	_Args&&... __args
);

template<typename _Tp, typename _Alloc, typename... _Args>
counted_ptr<_Tp> allocate_counted(_Alloc __a, _Args&&... __args);

template<typename _Tp, typename... _Args>
counted_ptr<_Tp> make_counted(
	typename counted_ptr<_Tp>::extra_size __extra_size,
	_Args&&... __args
);

template<typename _Tp, typename... _Args>
counted_ptr<_Tp> make_counted(_Args&&... __args);

template<typename _Tp1, typename _Tp2>
counted_ptr<_Tp1> static_pointer_cast(counted_ptr<_Tp2> __p);

template<typename _Tp1, typename _Tp2>
counted_ptr<_Tp1> const_pointer_cast(counted_ptr<_Tp2> __p);

template<typename _Tp1, typename _Tp2>
counted_ptr<_Tp1> dynamic_pointer_cast(counted_ptr<_Tp2> __p);

template<typename _Alloc, typename _Tp>
_Alloc *get_allocator(counted_ptr<_Tp> __p);

template<typename _Tp>
struct counted_ptr
{
	struct extra_size
	{
		size_t _M_size;

		explicit extra_size(size_t __size)
		: _M_size(__size)
		{}
	};

	~counted_ptr()
	{
		using namespace __counted;

		if (_M_ptr)
			ref_count_val<_Tp>::_S_get_this(_M_ptr)->_M_release();
	}

	/** @brief Construct an empty %counted_ptr.
	 *  @post  use_count() == 0 & get() == 0
	 */
	counted_ptr()
	: _M_ptr(0)
	{}

	/** @brief  If @a __r is empty, constructs an empty %counted_ptr;
	 *          otherwise construct a %counted_ptr that shares ownership
	 *          with @a __r.
	 *  @param  __r  A %counted_ptr.
	 *  @post   get() == __r.get() && use_count() == __r.use_count()
	 */
	counted_ptr(counted_ptr const &__r)
	: _M_ptr(__r._M_ptr)
	{
		using namespace __counted;

		if (_M_ptr)
			ref_count_val<_Tp>::_S_get_this(_M_ptr)
					    ->_M_add_ref_copy();
	}

	/** @brief  If @a __r is empty, constructs an empty %counted_ptr;
	 *          otherwise construct a %counted_ptr that shares ownership
	 *          with @a __r.
	 *  @param  __r  A %counted_ptr.
	 *  @post   get() == __r.get() && use_count() == __r.use_count()
	 */
	template<typename _Tp1>
	counted_ptr(counted_ptr<_Tp1> const &__r)
	: _M_ptr(__r._M_ptr)
	{
		using namespace __counted;
		__glibcxx_function_requires(_ConvertibleConcept<_Tp1*, _Tp*>);

		if (_M_ptr)
			ref_count_val<_Tp>::_S_get_this(_M_ptr)
					    ->_M_add_ref_copy();
	}

	/** @brief  Move-constructs a %counted_ptr instance from @a __r.
	 *  @param  __r  A %counted_ptr rvalue.
	 *  @post   *this contains the old value of @a __r, @a __r is empty.
	 */
	counted_ptr(counted_ptr &&__r)
	: _M_ptr(__r._M_ptr)
	{
		__r._M_ptr = 0;
	}

	/** @brief  Move-constructs a %counted_ptr instance from @a __r.
	 *  @param  __r  A %counted_ptr rvalue.
	 *  @post   *this contains the old value of @a __r, @a __r is empty.
	 */
	template<typename _Tp1>
	counted_ptr(counted_ptr<_Tp1> &&__r)
	: _M_ptr(__r._M_ptr)
	{
		__glibcxx_function_requires(_ConvertibleConcept<_Tp1*, _Tp*>);
		__r._M_ptr = 0;
	}

	/** @brief Assign @a __r to *this.
	 *  @param __r A %counted_ptr.
	 *  @post  get() == __r.get() && use_count() == __r.use_count()
	 */
	counted_ptr &operator=(counted_ptr const &__r)
	{
		using namespace __counted;

		if (__r._M_ptr != _M_ptr) {
			if (__r._M_ptr != 0)
				ref_count_val<_Tp>::_S_get_this(__r._M_ptr)
						    ->_M_add_ref_copy();

			if (_M_ptr != 0)
				ref_count_val<_Tp>::_S_get_this(_M_ptr)
						    ->_M_release();

			_M_ptr = __r._M_ptr;
		}

		return *this;
	}

	/** @brief Assign @a __r to *this.
	 *  @param __r A %counted_ptr.
	 *  @post  get() == __r.get() && use_count() == __r.use_count()
	 */
	template<typename _Tp1>
	counted_ptr &operator=(counted_ptr<_Tp1> const &__r)
	{
		using namespace __counted;
		__glibcxx_function_requires(_ConvertibleConcept<_Tp1*, _Tp*>);

		if (__r._M_ptr != _M_ptr) {
			if (__r._M_ptr != 0)
				ref_count_val<_Tp1>::_S_get_this(__r._M_ptr)
						     ->_M_add_ref_copy();

			if (_M_ptr != 0)
				ref_count_val<_Tp>::_S_get_this(_M_ptr)
						    ->_M_release();

			_M_ptr = __r._M_ptr;
		}

		return *this;
	}

	/** @brief Move-assign @a __r to *this.
	 *  @param __r A %counted_ptr rvalue.
	 *  @post   *this contains the old value of @a __r, @a __r is empty.
	 */
	counted_ptr &operator=(counted_ptr &&__r)
	{
		counted_ptr(std::move(__r)).swap(*this);
		return *this;
	}

	/** @brief Move-assign @a __r to *this.
	 *  @param __r A %counted_ptr rvalue.
	 *  @post   *this contains the old value of @a __r, @a __r is empty.
	 */
	template<class _Tp1>
	counted_ptr &operator=(counted_ptr<_Tp1> &&__r)
	{
		counted_ptr(std::move(__r)).swap(*this);
		return *this;
	}

	/** @brief Reset *this to null value.
	 *  @post  use_count() == 0 & get() == 0
	 */
	void reset()
	{
		counted_ptr().swap(*this);
	}

	// Allow class instantiation when _Tp is [cv-qual] void.
	typename std::add_lvalue_reference<_Tp>::type operator*() const
	{
		_GLIBCXX_DEBUG_ASSERT(_M_ptr != 0);
		return *_M_ptr;
	}

	_Tp *operator->() const
	{
		_GLIBCXX_DEBUG_ASSERT(_M_ptr != 0);
		return _M_ptr;
	}

	_Tp *get() const
	{
		return _M_ptr;
	}

	__counted::ref_count<_Tp> const *get_this() const
	{
		using namespace __counted;

		if (_M_ptr)
			return ref_count_val<_Tp>::_S_get_this(_M_ptr);
		else
			return 0;
	}

	/** @brief Return pointer to undescriminated "extra" byte storage
	 *         managed by *this.
	 *  @return Pointer tp extra storage casted to the desired type.
	 */
	template<typename _Tp1>
	_Tp1 *get_extra()
	{
		using namespace __counted;

		if (_M_ptr != 0)
			return static_cast<_Tp1 *>(
				ref_count_val<_Tp>::_S_get_this(_M_ptr)
						    ->_M_get_extra()
			);
		else
			return 0;
	}

	/** @brief Return pointer to undescriminated "extra" byte storage
	 *         managed by *this, together with its stored size.
	 *  @param  __size reference to callers size variable
	 *  @return Pointer tp extra storage casted to the desired type.
	 */
	template<typename _Tp1>
	_Tp1 *get_extra(size_t &__size)
	{
		using namespace __counted;

		if (_M_ptr != 0)
			return static_cast<_Tp1 *>(
				ref_count_val<_Tp>::_S_get_this(_M_ptr)
						    ->_M_get_extra(__size)
			);
		else
			return 0;
	}

	// Implicit conversion to "bool"
private:
	typedef _Tp* counted_ptr::*__unspecified_bool_type;

public:
	operator __unspecified_bool_type() const
	{
		return _M_ptr == 0 ? 0 : &counted_ptr::_M_ptr;
	}

	bool unique() const
	{
		using namespace __counted;

		return _M_ptr == 0 ? 0
				   : ref_count_val<_Tp>::_S_get_this(_M_ptr)
							 ->_M_unique();
	}

	long use_count() const
	{
		using namespace __counted;

		return _M_ptr == 0 ? 0
				   : ref_count_val<_Tp>::_S_get_this(_M_ptr)
							 ->_M_get_use_count();
	}

	void swap(counted_ptr<_Tp> &&__other)
	{
		std::swap(_M_ptr, __other._M_ptr);
	}

	template<typename _Tp1, typename _Alloc, typename... _Args>
	friend counted_ptr<_Tp1> allocate_counted(
		_Alloc __a,
		typename counted_ptr<_Tp1>::extra_size __extra_size,
		_Args&&... __args
	);

	template<typename _Tp1, typename _Alloc, typename... _Args>
	friend counted_ptr<_Tp1> allocate_counted(
		_Alloc __a, _Args&&... __args
	);

	template<typename _Tp1, typename... _Args>
	friend counted_ptr<_Tp1> make_counted(
		typename counted_ptr<_Tp1>::extra_size __extra_size,
		_Args&&... __args
	);

	template<typename _Tp1, typename... _Args>
	friend counted_ptr<_Tp1> make_counted(_Args&&... __args);

	template<typename _Tp1, typename _Tp2>
	friend counted_ptr<_Tp1> static_pointer_cast(counted_ptr<_Tp2> __p);

	template<typename _Tp1, typename _Tp2>
	friend counted_ptr<_Tp1> const_pointer_cast(counted_ptr<_Tp2> __p);

	template<typename _Tp1, typename _Tp2>
	friend counted_ptr<_Tp1> dynamic_pointer_cast(counted_ptr<_Tp2> __p);

	template<typename _Alloc>
	friend _Alloc *get_allocator(counted_ptr<_Tp> __p)
	{
		return static_cast<_Alloc *>(
			__p._M_get_allocator(typeid(_Alloc))
		);
	}

private:
	template<typename _Tp1> friend class counted_ptr;

	/** @brief Constructs a %counted_ptr from existing %ref_count.
	 *         Private constructor for use by helper functions,
	 *         make_counted and allocate_counted, as well as pointer casts.
	 */
	explicit counted_ptr(__counted::ref_count<_Tp> *__r,
			     bool __increment = false)
	: _M_ptr(__r->_M_get_ptr())
	{
		using namespace __counted;

		if (__increment && (_M_ptr != 0)) {
			ref_count_val<_Tp>::_S_get_this(_M_ptr)
					    ->_M_add_ref_copy();
		}
	}

	void *_M_get_allocator(std::type_info const &__ti) const
	{
		using namespace __counted;

		if (_M_ptr != 0)
			return ref_count_val<_Tp>::_S_get_this(_M_ptr)
						   ->_M_get_allocator(__ti);
		else
			return 0;
	}

	_Tp *_M_ptr;
};

/** @brief  Create an object that is owned by a counted_ptr.
 *  @param  __a          An allocator.
 *  @param  __extra_size Additional non discriminated storage size,
 *                       adjanced to newly created object.
 *  @param  __args       Arguments for the @a _Tp object's constructor.
 *  @return A shared_ptr that owns the newly created object.
 *  @throw  An exception thrown from @a _Alloc::allocate or from the
 *          constructor of @a _Tp.
 *
 *  A copy of @a __a will be used to allocate memory for the counted_ptr
 *  and the new object.
 */
template<typename _Tp, typename _Alloc, typename... _Args>
counted_ptr<_Tp> allocate_counted(
	_Alloc __a,
	typename counted_ptr<_Tp>::extra_size __extra_size,
	_Args&&... __args
)
{
	using namespace __counted;

	return counted_ptr<_Tp>(
		ref_count_a_e<_Tp, _Alloc>::_S_create(
			std::forward<_Alloc>(__a),
			__extra_size._M_size,
			std::forward<_Args>(__args)...
		)
	);
}

/** @brief  Create an object that is owned by a counted_ptr.
 *  @param  __a    An allocator.
 *  @param  __args Arguments for the @a _Tp object's constructor.
 *  @return A shared_ptr that owns the newly created object.
 *  @throw  An exception thrown from @a _Alloc::allocate or from the
 *          constructor of @a _Tp.
 *
 *  A copy of @a __a will be used to allocate memory for the counted_ptr
 *  and the new object.
 */
template<typename _Tp, typename _Alloc, typename... _Args>
counted_ptr<_Tp> allocate_counted(_Alloc __a, _Args&&... __args)
{
	using namespace __counted;

	return counted_ptr<_Tp>(
		ref_count_a<_Tp, _Alloc>::_S_create(
			std::forward<_Alloc>(__a),
			std::forward<_Args>(__args)...
		)
	);
}

/** @brief  Create an object that is owned by a counted_ptr.
 *  @param  __extra_size Additional non discriminated storage size,
 *                       adjanced to newly created object.
 *  @param  __args       Arguments for the @a _Tp object's constructor.
 *  @return A counted_ptr that owns the newly created object.
 *  @throw  std::bad_alloc, or an exception thrown from the
 *          constructor of @a _Tp.
 */
template<typename _Tp, typename... _Args>
counted_ptr<_Tp> make_counted(
	typename counted_ptr<_Tp>::extra_size __extra_size,
	_Args&&... __args
)
{
	using namespace __counted;

	return counted_ptr<_Tp>(
		ref_count_e<_Tp>::_S_create(
			__extra_size._M_size,
			std::forward<_Args>(__args)...
		)
	);
}

/** @brief  Create an object that is owned by a counted_ptr.
 *  @param  __args  Arguments for the @a _Tp object's constructor.
 *  @return A counted_ptr that owns the newly created object.
 *  @throw  std::bad_alloc, or an exception thrown from the
 *          constructor of @a _Tp.
 */
template<typename _Tp, typename... _Args>
counted_ptr<_Tp> make_counted(_Args&&... __args)
{
	using namespace __counted;

	return counted_ptr<_Tp>(
		ref_count_p<_Tp>::_S_create(
			0, std::forward<_Args>(__args)...
		)
	);
}

template<typename _Tp1, typename _Tp2>
counted_ptr<_Tp1> static_pointer_cast(counted_ptr<_Tp2> __p)
{
	if (__p._M_ptr != 0)
		return counted_ptr<_Tp1>(
			__counted::ref_count_val<_Tp1>::_S_get_this(
				static_cast<_Tp1 *>(__p._M_ptr)
			),
			true
		);
	else
		return counted_ptr<_Tp1>();
}

template<typename _Tp1, typename _Tp2>
counted_ptr<_Tp1> const_pointer_cast(counted_ptr<_Tp2> __p)
{
	if (__p._M_ptr != 0)
		return counted_ptr<_Tp1>(
			__counted::ref_count_val<_Tp1>::_S_get_this(
				const_cast<_Tp1 *>(__p._M_ptr)
			),
			true
		);
	else
		return counted_ptr<_Tp1>();
}

template<typename _Tp1, typename _Tp2>
counted_ptr<_Tp1> dynamic_pointer_cast(counted_ptr<_Tp2> __p)
{
	if (dynamic_cast<_Tp1 *>(__p._M_ptr) != 0)
		return static_pointer_cast(__p);
	else
		return counted_ptr<_Tp1>();
}

#endif
