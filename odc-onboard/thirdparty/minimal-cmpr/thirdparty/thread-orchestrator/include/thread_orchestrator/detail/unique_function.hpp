#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace orch::detail {

// Move-only type-erased callable wrapper with small-buffer optimization.
class UniqueFunction {
    static constexpr std::size_t BufSize  = 48;
    static constexpr std::size_t BufAlign = alignof(std::max_align_t);

    struct Ops {
        void (*invoke)(void*);
        void (*destroy)(void*);
        void (*move)(void* src, void* dst); // move-construct src into dst
    };

    template <typename F>
    static constexpr bool fits_sbo =
        sizeof(F) <= BufSize &&
        alignof(F) <= BufAlign &&
        std::is_nothrow_move_constructible_v<F>;

    template <typename F>
    static const Ops* ops_for() {
        if constexpr (fits_sbo<F>) {
            static constexpr Ops ops{
                /*invoke*/  [](void* p) { std::move (*static_cast<F*>(p))(); static_cast<F*>(p)->~F(); },
                /*destroy*/ [](void* p) { static_cast<F*>(p)->~F(); },
                /*move*/    [](void* src, void* dst) {
                    ::new (dst) F(std::move(*static_cast<F*>(src)));
                    static_cast<F*>(src)->~F();
                }
            };
            return &ops;
        } else {
            static constexpr Ops ops{
                /*invoke*/  [](void* p) {
                    auto* pp = static_cast<F**>(p);
                    std::move (**pp)();
                    delete *pp;
                    *pp = nullptr;
                },
                /*destroy*/ [](void* p) {
                    auto* pp = static_cast<F**>(p);
                    delete *pp;
                },
                /*move*/    [](void* src, void* dst) {
                    auto* sp = static_cast<F**>(src);
                    ::new (dst) F*(*sp);
                    *sp = nullptr;
                }
            };
            return &ops;
        }
    }

public:
    UniqueFunction() noexcept = default;

    template <typename F>
        requires (!std::is_same_v<std::decay_t<F>, UniqueFunction> && std::is_invocable_r_v<void, std::decay_t<F>>)
    UniqueFunction(F&& f) {
        using Fn = std::decay_t<F>;
        ops_ = ops_for<Fn>();
        if constexpr (fits_sbo<Fn>) {
            ::new (&storage_) Fn(std::forward<F>(f));
        } else {
            ::new (&storage_) Fn*(new Fn(std::forward<F>(f)));
        }
    }

    ~UniqueFunction() {
        if (ops_) ops_->destroy(&storage_);
    }

    UniqueFunction(UniqueFunction&& o) noexcept : ops_(o.ops_) {
        if (ops_) {
            ops_->move(&o.storage_, &storage_);
            o.ops_ = nullptr;
        }
    }

    UniqueFunction& operator=(UniqueFunction&& o) noexcept {
        if (this != &o) {
            if (ops_) ops_->destroy(&storage_);
            ops_ = o.ops_;
            if (ops_) {
                ops_->move(&o.storage_, &storage_);
                o.ops_ = nullptr;
            }
        }
        return *this;
    }

    UniqueFunction(const UniqueFunction&) = delete;
    UniqueFunction& operator=(const UniqueFunction&) = delete;

    explicit operator bool() const noexcept { return ops_ != nullptr; }

    void operator()() {
        auto* o = ops_;
        ops_ = nullptr; // one-shot
        o->invoke(&storage_);
    }

private:
    alignas(BufAlign) unsigned char storage_[BufSize]{};
    const Ops* ops_ = nullptr;
};

} // namespace orch::detail
