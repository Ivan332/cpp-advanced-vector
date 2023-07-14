#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;

    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory(RawMemory&& other) noexcept 
        : buffer_(std::exchange(other.buffer_, nullptr))
        , capacity_(std::exchange(other.capacity_, 0)) {
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept {

        if (this != &rhs) {
            buffer_ = std::move(rhs.buffer_);
            capacity_ = std::move(rhs.capacity_);
            rhs.buffer_ = nullptr;
            rhs.capacity_ = 0;
        }

        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() = default;

    explicit Vector(size_t size)
        : data_(size)
        , size_(size)
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_)
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept
        : data_(std::move(other.data_))
        , size_(std::exchange(other.size_, 0)) {
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ <= data_.Capacity()) {
                size_t copy_size = std::min(size_, rhs.size_);
                std::copy_n(rhs.data_.GetAddress(), copy_size, data_.GetAddress());

                if (size_ > rhs.size_) {
                    std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
                }
                else if (size_ < rhs.size_) {
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, 
                        data_.GetAddress() + size_);
                }
                size_ = rhs.size_;
            }
            else {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        Swap(rhs);
        return *this;
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    iterator begin() noexcept {
        return (data_.GetAddress());
    }

    iterator end() noexcept {
        return (data_.GetAddress() + size_);
    }

    const_iterator cbegin() const noexcept {
        return(data_.GetAddress());
    }

    const_iterator cend() const noexcept {
        return (data_.GetAddress() + size_);
    }

    const_iterator begin() const noexcept {
        return cbegin();
    }

    const_iterator end() const noexcept {
        return cend();
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_data(new_capacity);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
        }
        else {
            if (new_size > data_.Capacity()) {
                const size_t new_capacity = std::max(data_.Capacity() * 2, new_size);
                Reserve(new_capacity);
            }
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
        }
        size_ = new_size;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *Emplace(cend(), std::forward<Args>(args)...);
    }

    template <typename Type>
    void PushBack(Type&& value) {
        EmplaceBack(std::forward<Type>(value));
    }

    void PopBack() noexcept {
        if (size_ != 0)
        {
            std::destroy_at(data_.GetAddress() + size_ - 1);
            --size_;
        }
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        assert(pos >= cbegin() && pos <= cend());

        int position = pos - begin();
        if (data_.Capacity() > size_)
        {
            EmplaceWithoutRelocation(pos, std::forward<Args>(args)...);
        }
        else 
        {
            EmplaceWithRelocation(pos, std::forward<Args>(args)...);
        }
        ++size_;
        return begin() + position;
    }

    iterator Erase(const_iterator pos) {
        assert(pos >= cbegin() && pos <= cend());

        int position = pos - begin();
        std::move(begin() + position + 1, end(), begin() + position);
        std::destroy_at(end() - 1);
        --size_;
        return (begin() + position);
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

private:
    template <typename... Args>
    void EmplaceWithoutRelocation(const_iterator pos, Args&&... args) {
        try {
            if (pos != end()) {
                int position = pos - begin();
                T value(std::forward<Args>(args)...);
                new (end()) T(std::forward<T>(data_[size_ - 1]));
                std::move_backward(begin() + position, end() - 1, end());
                *(begin() + position) = std::forward<T>(value);
            }
            else {
                new (end()) T(std::forward<Args>(args)...);
            }
        }
        catch (...) {
            operator delete (end());
            throw;
        }
    }

    template <typename... Args>
    void EmplaceWithRelocation(const_iterator pos, Args&&... args) {
        int position = pos - begin();
        RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
        new (new_data.GetAddress() + position) T(std::forward<Args>(args)...);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), position, new_data.GetAddress());
            std::uninitialized_move_n(data_.GetAddress() + position, size_ - position, new_data.GetAddress() +
                position + 1);

        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), position, new_data.GetAddress());
            std::uninitialized_copy_n(data_.GetAddress() + position, size_ - position, new_data.GetAddress() +
                position + 1);
        }
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    RawMemory<T> data_;
    size_t size_ = 0;
};
