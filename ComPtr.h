#pragma once
#include <Unknwn.h>
#include <cassert>
#include <new>
#include <type_traits>
#include <utility>

namespace gt
{

#define COMPTR_PPV_ARGS(ptrRef) __uuidof(**(ptrRef)), (ptrRef)

namespace details
{
template<typename T>
constexpr bool IsComInterface = std::is_base_of_v<IUnknown, T>;

template<typename T, typename U = void>
using EnableIfComInterface = std::enable_if_t<IsComInterface<T>, U>;

template<typename T>
class HideIUnknownDecorator : public T
{
    ~HideIUnknownDecorator();

    // #region IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                             _COM_Outptr_ void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    // #endregion
};

} // namespace details

template<typename T>
class ComPtrConstRef;

template<typename T>
class ComPtrRef;

/// <summary>
///   COM reference-counting smart pointer.
/// </summary>
template<typename T>
class ComPtr
{
    template<typename TSource>
    using EnableConversion = std::enable_if_t<std::is_convertible_v<TSource*, T*>>;

public:
    using ValueType = T;

    constexpr ComPtr() noexcept = default;

    /*implicit*/ ComPtr(ComPtr const& source) noexcept
        : ptr(source.ptr)
    {
        Invariant();
        SafeAddRef();
    }

    /*implicit*/ ComPtr(ComPtr&& source) noexcept
        : ptr(std::exchange(source.ptr, nullptr))
    {
        Invariant();
    }

    template<typename TSource, typename = EnableConversion<TSource>>
    /*implicit*/ ComPtr(ComPtr<TSource> const& source) noexcept
        : ptr(source.ptr)
    {
        Invariant();
        SafeAddRef();
    }

    template<typename TSource, typename = EnableConversion<TSource>>
    /*implicit*/ ComPtr(ComPtr<TSource>&& source) noexcept
        : ptr(std::exchange(source.ptr, nullptr))
    {
        Invariant();
    }

    /*implicit*/ ComPtr(T* source, bool addRef = true) noexcept
        : ptr(source)
    {
        Invariant();
        if (addRef)
            SafeAddRef();
    }

    template<typename TSource, typename = EnableConversion<TSource>>
    /*implicit*/ ComPtr(TSource* source, bool addRef = true) noexcept
        : ptr(source)
    {
        Invariant();
        if (addRef)
            SafeAddRef();
    }

    ~ComPtr() noexcept { SafeRelease(); }

    ComPtr& operator=(ComPtr const& source) noexcept
    {
        if (ptr != source.ptr) {
            SafeRelease();
            ptr = source.ptr;
            SafeAddRef();
        }
        return *this;
    }

    template<typename TSource, typename = EnableConversion<TSource>>
    ComPtr& operator=(ComPtr<TSource> const& source) noexcept
    {
        SafeRelease();
        ptr = source.ptr;
        SafeAddRef();
        return *this;
    }

    ComPtr& operator=(ComPtr&& source) noexcept
    {
        SafeRelease();
        ptr = std::exchange(source.ptr, nullptr);
        return *this;
    }

    template<typename TSource, typename = EnableConversion<TSource>>
    ComPtr& operator=(ComPtr<TSource>&& source) noexcept
    {
        SafeRelease();
        ptr = std::exchange(source.ptr, nullptr);
        return *this;
    }

    ComPtr& operator=(T* source) noexcept
    {
        if (ptr != source) {
            SafeRelease();
            ptr = source;
            SafeAddRef();
        }
        return *this;
    }

    T* Get() const noexcept { return ptr; }

    T& operator*() const
    {
        assert(ptr != nullptr);
        return *ptr;
    }

    details::HideIUnknownDecorator<T>* operator->() const noexcept
    {
        assert(ptr != nullptr);
        return static_cast<details::HideIUnknownDecorator<T>*>(ptr);
    }

    operator T*() const noexcept { return ptr; }

    explicit operator bool() const noexcept { return ptr != nullptr; }

    ComPtrConstRef<T> operator&() const noexcept { return ComPtrConstRef<T>(this); }

    ComPtrRef<T> operator&() noexcept { return ComPtrRef<T>(this); }

    /// <summary>
    ///   Releases the managed object by decrementing its reference count
    ///   and returns a pointer to the internal (now null'ed) pointer which
    ///   can be used to store a new object. Intended to be used when calling
    ///   functions that return ref-counted objects via T** argument.
    /// </summary>
    T** ReleaseAndGetAddressOf() noexcept
    {
        SafeRelease();
        ptr = nullptr;
        return &ptr;
    }

    /// <summary>
    ///   Releases the managed object by decrementing its reference count
    ///   and returns a pointer to the internal (now null'ed) pointer which
    ///   can be used to store a new object. Intended to be used when calling
    ///   functions that return ref-counted objects via T** argument.
    /// </summary>
    /// <devdoc>
    ///   Allows ComPtr to be used when the out parameter is of a derived type.
    ///   <code>
    ///     struct T : IUnknown;
    ///     struct T2 : T;
    ///     void Foo(T2** ppv);
    ///     ComPtr&lt;T> p;
    ///     Foo(p.ReleaseAndGetAddressAs&lt;T2>());
    ///   </code>
    ///   The cast is only safe as long as multiple inheritance is not involved.
    ///   Because proper COM interfaces only use single inheritance this can
    ///   only be the case when TDerived is not an interface. The size check
    ///   excludes anything suspicious.
    /// </devdoc>
    template<typename TDerived,
             typename = std::enable_if_t<std::is_base_of_v<T, TDerived> &&
                                         sizeof(TDerived) == sizeof(void*)>>
    TDerived** ReleaseAndGetAddressAs() noexcept
    {
        static_assert(sizeof(IUnknown) == sizeof(void*)); // vtable pointer
        return reinterpret_cast<TDerived**>(ReleaseAndGetAddressOf());
    }

    /// <summary>
    ///   Releases any managed object and attaches the source object without
    ///   increasing its reference count. After attaching, the source object
    ///   is managed by this smart pointer.
    /// </summary>
    template<typename TSource>
    void Attach(TSource* source) noexcept
    {
        if (ptr != source) {
            SafeRelease();
            ptr = source;
        }
    }

    /// <summary>
    ///   Detaches the managed object without decreasing its reference count.
    ///   After detaching, the object is no longer managed by this smart
    ///   pointer.
    /// </summary>
    T* UnsafeDetach() noexcept { return std::exchange(ptr, nullptr); }

    void Reset() noexcept
    {
        SafeRelease();
        ptr = nullptr;
    }

    void Reset(T* source) noexcept { *this = source; }

    /// <summary>
    ///   Increases the ref-count and returns the managed object.
    /// </summary>
    T* UnsafeCopy() noexcept
    {
        SafeAddRef();
        return ptr;
    }

    /// <summary>
    ///   Increases the ref-count and sets the specified pointer to the managed
    ///   object.
    /// </summary>
    void CopyTo(T** target) const noexcept
    {
        SafeAddRef();
        *target = ptr;
    }

    /// <summary>
    ///   Increases the ref-count and sets the specified pointer to the managed
    ///   object.
    /// </summary>
    template<typename TTarget, typename = std::enable_if_t<std::is_base_of_v<TTarget, T>>>
    void CopyTo(TTarget** target) const noexcept
    {
        SafeAddRef();
        *target = ptr;
    }

    /// <summary>
    ///   Increases the ref-count and sets the specified pointer to the managed
    ///   object.
    /// </summary>
    template<typename TTarget>
    void MoveTo(TTarget** target) noexcept
    {
        *target = ptr;
        ptr = nullptr;
    }

    template<typename TBase>
    TBase* AsBase() const noexcept
    {
        static_assert(std::is_base_of<TBase, T>::value,
                      "AsBase() requires a base class of T.");
        return ptr;
    }

    [[nodiscard]] HRESULT As(ComPtrRef<T> target) const noexcept
    {
        if (this != static_cast<ComPtr<T>*>(target))
            static_cast<ComPtr<T>*>(target)->Reset(ptr);
        return S_OK;
    }

    template<typename TInterface>
    [[nodiscard]] HRESULT As(_COM_Outptr_ TInterface** target) const noexcept
    {
        return ptr->QueryInterface(__uuidof(TInterface),
                                   reinterpret_cast<void**>(target));
    }

    template<typename TInterface>
    [[nodiscard]] HRESULT As(ComPtr<TInterface>* target) const noexcept
    {
        return ptr->QueryInterface(
            __uuidof(TInterface),
            reinterpret_cast<void**>(target->ReleaseAndGetAddressOf()));
    }

    template<typename TInterface>
    [[nodiscard]] HRESULT As(ComPtrRef<TInterface> target) const noexcept
    {
        return ptr->QueryInterface(__uuidof(TInterface), target);
    }

    [[nodiscard]] HRESULT As(REFIID iid, _COM_Outptr_ void** target) const noexcept
    {
        return ptr->QueryInterface(iid, target);
    }

    /// Special version with explicit IID for exotic interfaces without
    /// attached UUID.
    template<typename TInterface>
    [[nodiscard]] HRESULT As(REFIID iid, ComPtrRef<TInterface> target) const noexcept
    {
        return ptr->QueryInterface(iid, target);
    }

    [[nodiscard]] HRESULT QueryFrom(_In_ IUnknown* object) noexcept
    {
        return object->QueryInterface(__uuidof(T),
                                      reinterpret_cast<void**>(ReleaseAndGetAddressOf()));
    }

    [[nodiscard]] HRESULT
    CreateInstance(_In_ REFCLSID clsid, _In_opt_ IUnknown* outer,
                   _In_ DWORD clsContext = CLSCTX_INPROC_SERVER) noexcept
    {
        return CoCreateInstance(clsid, outer, clsContext, __uuidof(T),
                                reinterpret_cast<void**>(ReleaseAndGetAddressOf()));
    }

    [[nodiscard]] HRESULT
    CreateInstance(_In_ REFCLSID clsid,
                   _In_ DWORD clsContext = CLSCTX_INPROC_SERVER) noexcept
    {
        return CreateInstance(clsid, nullptr, clsContext);
    }

    template<typename TClass>
    [[nodiscard]] HRESULT
    CreateInstance(_In_opt_ IUnknown* outer,
                   _In_ DWORD clsContext = CLSCTX_INPROC_SERVER) noexcept
    {
        return CreateInstance(__uuidof(TClass), outer, clsContext);
    }

    template<typename TClass>
    [[nodiscard]] HRESULT
    CreateInstance(_In_ DWORD clsContext = CLSCTX_INPROC_SERVER) noexcept
    {
        return CreateInstance(__uuidof(TClass), nullptr, clsContext);
    }

private:
    void SafeAddRef() const noexcept
    {
        if (ptr != nullptr)
            ptr->AddRef();
    }

    void SafeRelease() const noexcept
    {
        if (ptr != nullptr)
            ptr->Release();
    }

    static void Invariant() noexcept
    {
        static_assert(details::IsComInterface<T>,
                      "T must be a COM type derived from IUnknown.");
    }

    // Make all ComPtr templates friends. This is needed for the template
    // move constructor.
    template<typename U>
    friend class ComPtr;

    friend class ComPtrConstRef<T>;
    friend class ComPtrRef<T>;

    T* ptr = nullptr;
};

template<typename T1, typename T2>
bool operator==(ComPtr<T1> const& left, ComPtr<T2> const& right) noexcept
{
    return left.Get() == right.Get();
}

template<typename T1, typename T2>
bool operator!=(ComPtr<T1> const& left, ComPtr<T2> const& right) noexcept
{
    return left.Get() != right.Get();
}

template<typename T1, typename T2>
bool operator==(ComPtr<T1> const& left, T2* right) noexcept
{
    return left.Get() == right;
}

template<typename T1, typename T2>
bool operator!=(ComPtr<T1> const& left, T2* right) noexcept
{
    return left.Get() != right;
}

template<typename T1, typename T2>
bool operator==(T1* left, ComPtr<T2> const& right) noexcept
{
    return left == right.Get();
}

template<typename T1, typename T2>
bool operator!=(T1* left, ComPtr<T2> const& right) noexcept
{
    return left != right.Get();
}

template<typename T>
class ComPtrConstRef
{
public:
    explicit ComPtrConstRef(ComPtr<T> const* ptr) noexcept
        : ptr(ptr)
    {
        static_assert(details::IsComInterface<T>,
                      "Invalid conversion: T does not derive from IUnknown");
    }

    operator ComPtr<T> const*() noexcept { return ptr; }

    /// <devdoc>
    ///   Allows ComPtr to be used in place of an input array.
    ///   <code>
    ///     void Foo(T* const* values, size_t count);
    ///     ComPtr&lt;T> p;
    ///     Foo(&p, 1);
    ///   </code>
    /// </devdoc>
    operator T* const*() noexcept { return &(ptr->ptr); }

    /// <devdoc>
    ///   Allows ComPtr to be used in <c>__uuidof(**(ppType))</c> expressions.
    /// </devdoc>
    T const* operator*() const noexcept { return ptr->Get(); }

    /// <devdoc>
    ///   Allows ComPtr to be used in <c>__uuidof(**(ppType))</c> expressions.
    /// </devdoc>
    T* operator*() noexcept { return ptr->Get(); }

private:
    ComPtr<T> const* ptr;
};

template<typename T>
class ComPtrRef
{
public:
    explicit ComPtrRef(ComPtr<T>* ptr) noexcept
        : ptr(ptr)
    {
        static_assert(details::IsComInterface<T>,
                      "Invalid conversion: T does not derive from IUnknown");
    }

    T** ReleaseAndGetAddressOf() noexcept { return ptr->ReleaseAndGetAddressOf(); }

    operator ComPtr<T>*() noexcept { return ptr; }

    /// <devdoc>
    ///   Allows ComPtr to be used in place of an input array.
    ///   <code>
    ///     void Foo(T* const* values, size_t count);
    ///     ComPtr&lt;T> p;
    ///     Foo(&p, 1);
    ///   </code>
    /// </devdoc>
    operator T* const*() noexcept { return &(ptr->ptr); }

    operator T**() noexcept { return ptr->ReleaseAndGetAddressOf(); }

    operator void**() noexcept
    {
        return reinterpret_cast<void**>(ptr->ReleaseAndGetAddressOf());
    }

    operator IUnknown**() const noexcept
    {
        return reinterpret_cast<IUnknown**>(ptr->ReleaseAndGetAddressOf());
    }

    /// <devdoc>
    ///   Allows ComPtr to be used in <c>__uuidof(**(ppType))</c> expressions.
    /// </devdoc>
    T const* operator*() const noexcept { return ptr->Get(); }

    /// <devdoc>
    ///   Allows ComPtr to be used in <c>__uuidof(**(ppType))</c> expressions.
    /// </devdoc>
    T* operator*() noexcept { return ptr->Get(); }

private:
    ComPtr<T>* ptr;
};

template<typename T>
ComPtr<T> AdoptRef(T* ptr)
{
    return ComPtr<T>(ptr, false);
}

template<typename T>
void AdoptRef(ComPtr<T> const& ptr) = delete;

template<typename From>
class ComPtrAutoQI
{
public:
    template<typename To>
    operator ComPtr<To>() noexcept
    {
        ComPtr<To> out;
        if (ptr)
            ptr->QueryInterface(__uuidof(To), &out);
        return out;
    }

    template<typename F>
    friend ComPtrAutoQI<F> qi_autocast(_In_ ComPtr<F>& ptr) noexcept;

    template<typename F>
    friend details::EnableIfComInterface<F, ComPtrAutoQI<F>>
    qi_autocast(_In_opt_ F* ptr) noexcept;

private:
    ComPtrAutoQI(_In_opt_ From* ptr)
        : ptr(ptr)
    {}

    From* ptr;
};

template<typename From>
ComPtrAutoQI<From> qi_autocast(_In_ ComPtr<From>& ptr) noexcept
{
    return {ptr.Get()};
}

template<typename From>
details::EnableIfComInterface<From, ComPtrAutoQI<From>>
qi_autocast(_In_opt_ From* ptr) noexcept
{
    return {ptr};
}

template<typename To, typename From>
ComPtr<To> qi_cast(_In_ ComPtr<From>& ptr) noexcept
{
    ComPtr<To> out;
    (void)ptr.As(&out);
    return out;
}

template<typename To, typename From,
         typename = std::enable_if_t<details::IsComInterface<To> &&
                                     details::IsComInterface<From>>>
ComPtr<To> qi_cast(_In_opt_ From* ptr) noexcept
{
    ComPtr<To> out;
    if (ptr)
        ptr->QueryInterface(__uuidof(To), &out);
    return out;
}

/// <summary>
///   Constructs a COM object of type <typeparamref name="T"/> and wraps it in a
///   <see cref="ComPtr"/>.
/// </summary>
/// <remarks>
///   <typeparamref name="T"/> must be constructed with a reference count of 1.
/// </remarks>
template<typename T, typename... Args>
std::enable_if_t<!std::is_array_v<T>, ComPtr<T>> make_com(Args&&... args) noexcept
{
    return ComPtr<T>(new (std::nothrow) T(std::forward<Args>(args)...), false);
}

} // namespace gt
