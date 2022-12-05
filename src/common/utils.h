// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <optional>

#include <sys/resource.h>
#include <ghc/filesystem.hpp>

#define YABRIDGE_EXPORT __attribute__((visibility("default")))

/**
 * The interval in seconds between synchronizing the Wine plugin host's audio
 * thread scheduling priority with the host's audio thread.
 *
 * @relates Vst2Bridge::last_audio_thread_priority_synchronization
 */
constexpr time_t audio_thread_priority_synchronization_interval = 10;

/**
 * When the `hide_daw` compatibility option is enabled, we'll report this
 * instead of the actual DAW's name. This can be useful when plugins are
 * hardcoded to behave differently in certain DAWs, and when that different
 * behaviour causes issues under Wine. An example of such a plugin is AAS
 * Chromaphone 3 when run under Bitwig.
 */
constexpr char product_name_override[] = "Get yabridge'd";
/**
 * When the `hide_daw` compatibility option is enabled, we'll report this
 * instead of the actual vendor's name in a VST2 plugin.
 */
constexpr char vendor_name_override[] = "yabridge";

/**
 * The constraint is satisfied if the type is the same as `To`, or if it can be
 * implicitly converted to it. The implementation of the constraint requires
 * types to be copy constructable for them to be implicitly convertible. */
template <typename From, typename To>
concept same_or_convertible_to =
    std::same_as<From, To> || std::convertible_to<From, To>;

/**
 * The same as the `std::invocable` concept, but also specifying the result
 * type.
 */
template <typename F, typename Result, typename... Args>
concept invocable_returning = requires(F&& f, Result&& result, Args&&... args) {
    {
        std::invoke(std::forward<F>(f), std::forward<Args>(args)...)
        } -> same_or_convertible_to<Result>;
};

// The cannonical overloading template for `std::visitor`, not sure why this
// isn't part of the standard library
template <class... Ts>
struct overload : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;

/**
 * Return the path to the directory for story temporary files. This will be
 * `$XDG_RUNTIME_DIR` if set, and `/tmp` otherwise.
 */
ghc::filesystem::path get_temporary_directory();

/**
 * Get the current thread's scheduling priority if the thread is using
 * `SCHED_FIFO`. Returns a nullopt if the calling thread is not under realtime
 * scheduling.
 */
std::optional<int> get_realtime_priority() noexcept;

/**
 * Set the scheduling policy to `SCHED_FIFO` with priority 5 for this process.
 * We explicitly don't do this for wineserver itself since from my testing that
 * can actually increase latencies.
 *
 * @param sched_fifo If true, set the current process/thread's scheudling policy
 *   to `SCHED_FIFO`. Otherwise reset it back to `SCHWED_OTHER`.
 * @param priority The scheduling priority to use. The exact value usually
 *   doesn't really matter unless there are a lot of other active `SCHED_FIFO`
 *   background tasks. We'll use 5 as a default, but we'll periodically copy the
 *   priority set by the host on the audio threads.
 *
 * @return Whether the operation was successful or not. This will fail if the
 *   user does not have the privileges to set realtime priorities.
 *
 * TODO: At some point, consider using `SCHED_RESET_ON_FORK` instead of manually
 *       disabling this when we don't want realtime scheduling to propagate.
 *       That would require a bit of careful analysis because we do want it to
 *       propagate to a Windows plugin's audio threads, and I don't think
 *       there's a way to go back once you've set `SCHED_RESET_ON_FORK`.
 */
bool set_realtime_priority(bool sched_fifo, int priority = 5) noexcept;

/**
 * Get the (soft) `RLIMIT_MEMLOCK` resource limit. If this is set to some low
 * value, then we'll print a warning during initialization because mapping
 * shared memory may fail. A value of `-1`/`RLIM_INFINITY` means that there is
 * no limit. If there was some error fetching this value, then a nullopt will be
 * returned.
 */
std::optional<rlim_t> get_memlock_limit() noexcept;

/**
 * Get the (soft) `RLIMIT_RTTIME` resource limit, or the amount of time a
 * `SCHED_FIFO` process may spend uninterrupted before being killed by the
 * scheduler. A value of `-1`/`RLIM_INFINITY` means that there is no limit. If
 * there was some error fetching this value, then a nullopt will be returned.
 *
 * This is useful to diagnose issues caused by PipeWire. They use rtkit at the
 * moment, and both rtkit and PipeWire's rtkit module will enable a realtime CPU
 * time limit with some low value.
 */
std::optional<rlim_t> get_rttime_limit() noexcept;

/**
 * Returns `true` if `YABRIDGE_NO_WATCHDOG` is set to `1`. In that case we will
 * not check if the Wine plugin host process successfully started, and we'll
 * also don't check if the native plugin host is still alive. Disabling the
 * watchdog timers can cause plugins hang during scanning and dangling Wine
 * processes to be left running, so this should only ever be used when running
 * the Wine plugin host under a separate namespace.
 */
bool is_watchdog_timer_disabled();

/**
 * A RAII wrapper that will temporarily enable the FTZ flag so that denormals
 * are automatically flushed to zero, returning to whatever the flag was
 * previously when it drops out of scope.
 */
class ScopedFlushToZero {
   public:
    ScopedFlushToZero() noexcept;
    ~ScopedFlushToZero() noexcept;

    ScopedFlushToZero(const ScopedFlushToZero&) = delete;
    ScopedFlushToZero& operator=(const ScopedFlushToZero&) = delete;

    ScopedFlushToZero(ScopedFlushToZero&&) noexcept;
    ScopedFlushToZero& operator=(ScopedFlushToZero&&) noexcept;

   private:
    /**
     * The previous FTZ mode. When we use this on the Wine side, this should
     * always be disabled. But, we'll make sure to do it correctly anyhow so we
     * don't accidentally end up disabling FTZ somewhere where it should be
     * enabled.
     */
    std::optional<unsigned int> old_ftz_mode_;
};

/**
 * A helper to temporarily cache a value. Calling `ScopedValueCache::set(x)`
 * will return a guard object. When `ScopedValueCache::get()` is called while
 * this guard object is active, then `x` is returned. Otherwise a nullopt will
 * be returned.
 *
 * @note This class provides no thread safety guarantees. If thread safety is
 *   needed, then you should use mutexes around the getter and the setter.
 */
template <typename T>
class ScopedValueCache {
   public:
    ScopedValueCache() noexcept {}

    ScopedValueCache(const ScopedValueCache&) = delete;
    ScopedValueCache& operator=(const ScopedValueCache&) = delete;

    // Moving is impossible because of the guard
    ScopedValueCache(ScopedValueCache&&) = delete;
    ScopedValueCache& operator=(ScopedValueCache&&) = delete;

    /**
     * Return the cached value, if we're currently caching a value. Will return
     * a null pointer when this is not the case.
     */
    const T* get() const noexcept { return value_ ? &*value_ : nullptr; }

    /**
     * A guard that will reset the cached value on the `ScopedValueCache` when
     * it drops out of scope.
     */
    class Guard {
       public:
        Guard(std::optional<T>& cached_value) noexcept
            : cached_value_(cached_value) {}
        ~Guard() noexcept {
            if (is_active_) {
                cached_value_.get().reset();
            }
        }

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        Guard(Guard&& o) noexcept : cached_value_(std::move(o.cached_value_)) {
            o.is_active_ = false;
        }
        Guard& operator=(Guard&& o) noexcept {
            cached_value_ = std::move(o.cached_value_);
            o.is_active_ = false;

            return *this;
        }

       private:
        bool is_active_ = true;
        std::reference_wrapper<std::optional<T>> cached_value_;
    };

    /**
     * Temporarily cache `new_value`. This value will be cached as long as the
     * returned guard is in scope. This guard should not outlive the
     * `ScopedValueCache` object.
     *
     * @param new_value The cached value to store.
     *
     * @throw std::runtime_error When we are already caching a value.
     */
    Guard set(T new_value) noexcept {
        value_ = std::move(new_value);

        return Guard(value_);
    }

   private:
    /**
     * The current value, if `set()` has been called and the guard is still
     * active.
     */
    std::optional<T> value_;
};

/**
 * Temporarily cache a value for certain number of seconds.
 *
 * @note This uses `time()` for performance reasons, and the exact lifetime of
 *   the cache will this be very imprecise.
 *
 * @note This class provides no thread safety guarantees. If thread safety is
 *   needed, then you should use mutexes around the getter and the setter.
 */
template <typename T>
class TimedValueCache {
   public:
    /**
     * Return the cached value, if we're currently caching a value. Will return
     * a null pointer when this is not the case.
     */
    const T* get() const noexcept {
        const time_t now = time(nullptr);
        return now <= valid_until_ ? &value_ : nullptr;
    }

    /**
     * Return the cached value, if we're currently caching a value. Will return
     * a null pointer when this is not the case. The lifetime for the value will
     * be reset to `lifetime_seconds` seconds from now, if the value was still
     * active.
     */
    const T* get_and_keep_alive(unsigned int lifetime_seconds) noexcept {
        const time_t now = time(nullptr);
        if (now <= valid_until_) {
            valid_until_ = now + lifetime_seconds;
            return &value_;
        } else {
            return nullptr;
        }
    }

    /**
     * Set the cached value for `lifetime_seconds` seconds.
     */
    void set(T value, unsigned int lifetime_seconds) noexcept {
        value_ = value;
        valid_until_ = time(nullptr) + lifetime_seconds;
    }

   private:
    T value_;
    time_t valid_until_ = 0;
};
