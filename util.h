#pragma once
#include "./compat.h"
#include <atomic>
#include <cstddef>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace hipe {


// util for hipe
namespace util {

// ======================
//       Easy sleep
// ======================

inline void sleep_for_seconds(int sec) {
    std::this_thread::sleep_for(std::chrono::seconds(sec));
}

inline void sleep_for_milli(int milli) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milli));
}

inline void sleep_for_micro(int micro) {
    std::this_thread::sleep_for(std::chrono::microseconds(micro));
}

inline void sleep_for_nano(int nano) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(nano));
}


// ======================
//        Easy IO
// ======================


template <typename T>
void print(T&& t) {
    std::cout << std::forward<T>(t) << std::endl;
}

template <typename T, typename... Args>
void print(T&& t, Args&&... argv) {
    std::cout << std::forward<T>(t);
    print(std::forward<Args>(argv)...);
}

/**
 * Thread sync output stream.
 * It can protect the output from multi thread competition.
 */
class SyncStream
{
    std::ostream& out_stream;
    std::recursive_mutex io_locker;

public:
    explicit SyncStream(std::ostream& out_stream = std::cout)
      : out_stream(out_stream) {
    }
    template <typename T>
    void print(T&& items) {
        io_locker.lock();
        out_stream << std::forward<T>(items) << std::endl;
        io_locker.unlock();
    }
    template <typename T, typename... A>
    void print(T&& item, A&&... items) {
        io_locker.lock();
        out_stream << std::forward<T>(item);
        this->print(std::forward<A>(items)...);
        io_locker.unlock();
    }
};

// ===========================
//       Grammar sugar
// ===========================

// judge whether template param is a runnable object
template <typename F, typename... Args>
using is_runnable = std::is_constructible<std::function<void(Args...)>, std::reference_wrapper<typename std::remove_reference<F>::type>>;

// judge whether whether the runnable object F's return type is R
template <typename F, typename R>
using is_return = std::is_same<typename std::result_of<F()>::type, R>;

// call "foo" for times
template <typename F, typename = typename std::enable_if<is_runnable<F>::value>::type>
void repeat(F&& foo, int times = 1) {
    for (int i = 0; i < times; ++i) {
        std::forward<F>(foo)();
    }
}

// spin and wait for short time
template <typename F, typename = typename std::enable_if<is_return<F, bool>::value>::type>
void waitForShort(F&& foo) {
    int count = 0;
    bool yield = (std::thread::hardware_concurrency() == 1);
    while (!foo()) {
        if (yield)
            std::this_thread::yield();
        else {
            if (count++ > 16) {
                std::this_thread::yield();
                count = 0;
            } else {
                HIPE_PAUSE();
            }
        }
    }
}

template <typename F, typename... Args>
void invoke(F&& call, Args&&... args) {
    static_assert(is_runnable<F, Args...>::value, "[HipeError]: Invoke non-runnable object !");
    call(std::forward<Args>(args)...);
}


template <typename Var>
void recyclePlus(Var& var, Var left_border, Var right_border) {
    var = (++var == right_border) ? left_border : var;
}

/**
 * Time wait for the runnable object
 * Use std::milli or std::micro or std::nano to fill template parameter
 */
template <typename Precision, typename F, typename... Args>
double timewait(F&& foo, Args&&... argv) {
    static_assert(is_runnable<F, Args...>::value, "[HipeError]: timewait for non-runnable object !");
    auto time_start = std::chrono::steady_clock::now();
    foo(std::forward<Args>(argv)...);
    auto time_end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, Precision>(time_end - time_start).count();
}

/**
 * Time wait for the runnable object
 * And the precision is std::chrono::second
 */
template <typename F, typename... Args>
double timewait(F&& foo, Args&&... argv) {
    static_assert(is_runnable<F, Args...>::value, "[HipeError]: timewait for non-runnable object !");
    auto time_start = std::chrono::steady_clock::now();
    foo(std::forward<Args>(argv)...);
    auto time_end = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(time_end - time_start).count();
}


// ======================================
//            special format
// ======================================


/**
 * just like this:
 * =============
 * *   title   *
 * =============
 */
inline std::string title(const std::string& tar, int left_right_edge = 4) {
    static std::string ele1 = "=";
    static std::string ele2 = " ";
    static std::string ele3 = "*";

    std::string res;

    repeat([&] { res.append(ele1); }, left_right_edge * 2 + static_cast<int>(tar.size()));
    res.append("\n");

    res.append(ele3);
    repeat([&] { res.append(ele2); }, left_right_edge - static_cast<int>(ele3.size()));
    res.append(tar);
    repeat([&] { res.append(ele2); }, left_right_edge - static_cast<int>(ele3.size()));
    res.append(ele3);
    res.append("\n");

    repeat([&] { res.append(ele1); }, left_right_edge * 2 + static_cast<int>(tar.size()));
    return res;
}

/**
 * just like this
 * <[ something ]>
 */
inline std::string strong(const std::string& tar, int left_right_edge = 2) {
    static std::string ele1 = "<[";
    static std::string ele2 = "]>";

    std::string res;
    res.append(ele1);

    repeat([&] { res.append(" "); }, left_right_edge - static_cast<int>(ele1.size()));
    res.append(tar);
    repeat([&] { res.append(" "); }, left_right_edge - static_cast<int>(ele2.size()));

    res.append(ele2);
    return res;
}

inline std::string boundary(char element, int length = 10) {
    return std::string(length, element);
}


// ======================================
//             Basic module
// ======================================


// future container
template <typename T>
class Futures
{
    std::vector<std::future<T>> futures;
    std::vector<T> results;

public:
    Futures()
      : futures(0)
      , results(0) {
    }

    // return results contained by the built-in vector
    std::vector<T>& get() {
        results.resize(futures.size());
        for (size_t i = 0; i < futures.size(); ++i) {
            results[i] = futures[i].get();
        }
        return results;
    }

    std::future<T>& operator[](size_t i) {
        return futures[i];
    }

    void push_back(std::future<T>&& future) {
        futures.push_back(std::move(future));
    }

    size_t size() {
        return futures.size();
    }

    // wait for all futures
    void wait() {
        for (size_t i = 0; i < futures.size(); ++i) {
            futures[i].wait();
        }
    }
};


// spin locker that use C++11 std::atomic_flag
class spinlock
{
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            HIPE_PAUSE();
        }
    }
    void unlock() {
        flag.clear(std::memory_order_release);
    }
    bool try_lock() {
        return !flag.test_and_set();
    }
};


// locker guard for spinlock
class spinlock_guard
{
    spinlock* lck = nullptr;

public:
    explicit spinlock_guard(spinlock& locker) {
        lck = &locker;
        lck->lock();
    }
    ~spinlock_guard() {
        lck->unlock();
    }
};


/**
 * Task that support different kinds of callable object.
 * It will alloc some heap space to save the task.
 */
class Task
{
    struct BaseExec {
        virtual void call() = 0;
        virtual ~BaseExec() = default;
    };

    template <typename F, typename T = typename std::decay<F>::type>
    struct GenericExec : BaseExec {
        T foo;
        GenericExec(F&& f)
          : foo(std::forward<F>(f)) {
        }
        ~GenericExec() override = default;
        void call() override {
            foo();
        }
    };

public:
    Task() = default;
    Task(Task&& other) = default;

    Task(Task&) = delete;
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task() = default;

    // construct a task
    template <typename F, typename = typename std::enable_if<is_runnable<F>::value>::type>
    Task(F&& foo)
      : exe(new GenericExec<F>(std::forward<F>(foo))) {
    }

    // reset the task
    template <typename F, typename = typename std::enable_if<is_runnable<F>::value>::type>
    void reset(F&& foo) {
        exe.reset(new GenericExec<F>(std::forward<F>(foo)));
    }

    // the task was set
    bool is_set() {
        return static_cast<bool>(exe);
    }

    // override "="
    Task& operator=(Task&& tmp) {
        exe.reset(tmp.exe.release());
        return *this;
    }

    // runnable
    void operator()() {
        exe->call();
    }

private:
    std::unique_ptr<BaseExec> exe = nullptr;
};


/**
 * Block for adding tasks in batch
 * You can regard it as a more convenient C arrays
 * Notice that the element must override " = "
 */
template <typename T>
class Block
{
    size_t sz = 0;
    size_t end = 0;
    std::unique_ptr<T[]> blok = {nullptr};

public:
    Block() = default;
    virtual ~Block() = default;


    Block(Block&& other) noexcept
      : sz(other.sz)
      , end(other.end)
      , blok(std::move(other.blok)) {
    }

    explicit Block(size_t size)
      : sz(size)
      , end(0)
      , blok(new T[size]) {
    }

    Block(const Block& other) = delete;


    T& operator[](size_t idx) {
        return blok[idx];
    }

    // block's capacity
    size_t capacity() {
        return sz;
    }

    // element number
    size_t element_numb() {
        return end;
    }

    // whether have nums' space
    bool is_spare_for(size_t nums) {
        return (end + nums) <= sz;
    }

    // whether the block is full
    bool is_full() {
        return end == sz;
    }

    // add an element
    void add(T&& tar) {
        blok[end++] = std::forward<T>(tar);
    }

    // pop up the last element
    void reduce() {
        end--;
    }


    // fill element. Notice that the element must be copied !
    void fill(const T& tar) {
        while (end != sz) {
            blok[end++] = tar;
        }
    }

    // clean the block and delay free memory
    void clean() {
        end = 0;
    }

    // renew space for the block
    void reset(size_t new_sz) {
        blok.reset(new T[new_sz]);
        sz = new_sz;
        end = 0;
    }

    // release the heap space
    void release() {
        blok.release();
        sz = 0;
        end = 0;
    }

    // just for inherit
    virtual void sort() {
    }
};
} // namespace util

} // namespace hipe
