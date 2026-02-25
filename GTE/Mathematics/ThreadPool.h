// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// ThreadPool - C++17 thread pool for parallel processing
//
// This provides parallelization without OpenMP dependency.
// Uses only C++17 standard library threading primitives:
// - std::thread for worker threads
// - std::mutex for synchronization
// - std::condition_variable for signaling
// - std::atomic for lock-free counters
// - std::function for task callbacks
//
// Designed for GTE's geometric algorithms (RVD, CVT, Lloyd relaxation).

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

namespace gte
{
    class ThreadPool
    {
    public:
        // Create thread pool with specified number of threads
        // If numThreads == 0, uses hardware_concurrency()
        explicit ThreadPool(size_t numThreads = 0)
            : stop(false)
        {
            if (numThreads == 0)
            {
                numThreads = std::thread::hardware_concurrency();
                if (numThreads == 0)
                {
                    numThreads = 4;  // Fallback if detection fails
                }
            }

            workers.reserve(numThreads);
            for (size_t i = 0; i < numThreads; ++i)
            {
                workers.emplace_back([this] { WorkerThread(); });
            }
        }

        // Destructor waits for all tasks to complete
        ~ThreadPool()
        {
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                stop = true;
            }
            condition.notify_all();

            for (std::thread& worker : workers)
            {
                if (worker.joinable())
                {
                    worker.join();
                }
            }
        }

        // Get number of threads in pool
        size_t GetNumThreads() const
        {
            return workers.size();
        }

        // Enqueue a task for asynchronous execution
        template<typename Func, typename... Args>
        auto Enqueue(Func&& func, Args&&... args)
            -> std::future<typename std::result_of<Func(Args...)>::type>
        {
            using ReturnType = typename std::result_of<Func(Args...)>::type;

            auto task = std::make_shared<std::packaged_task<ReturnType()>>(
                std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
            );

            std::future<ReturnType> result = task->get_future();
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                if (stop)
                {
                    throw std::runtime_error("Enqueue on stopped ThreadPool");
                }
                tasks.emplace([task]() { (*task)(); });
            }
            condition.notify_one();
            return result;
        }

        // Parallel for loop: executes func(i) for i in [begin, end)
        // Function signature: void func(size_t i)
        template<typename Func>
        void ParallelFor(size_t begin, size_t end, Func&& func)
        {
            if (begin >= end)
            {
                return;
            }

            size_t numThreads = workers.size();
            size_t range = end - begin;
            
            // For small ranges, just run sequentially
            if (range < numThreads)
            {
                for (size_t i = begin; i < end; ++i)
                {
                    func(i);
                }
                return;
            }

            // Divide work into blocks
            size_t blockSize = (range + numThreads - 1) / numThreads;
            std::atomic<size_t> completedBlocks{0};
            std::atomic<bool> hasException{false};
            std::exception_ptr exceptionPtr;
            std::mutex exceptionMutex;

            // Launch tasks for each block
            std::vector<std::future<void>> futures;
            futures.reserve(numThreads);

            for (size_t t = 0; t < numThreads; ++t)
            {
                size_t start = begin + t * blockSize;
                size_t finish = std::min(start + blockSize, end);

                if (start < finish)
                {
                    futures.push_back(Enqueue([start, finish, &func, &completedBlocks, 
                                               &hasException, &exceptionPtr, &exceptionMutex]() {
                        try
                        {
                            for (size_t i = start; i < finish; ++i)
                            {
                                func(i);
                            }
                            completedBlocks++;
                        }
                        catch (...)
                        {
                            std::lock_guard<std::mutex> lock(exceptionMutex);
                            if (!hasException.exchange(true))
                            {
                                exceptionPtr = std::current_exception();
                            }
                            completedBlocks++;
                        }
                    }));
                }
            }

            // Wait for all tasks to complete
            for (auto& future : futures)
            {
                future.wait();
            }

            // Re-throw if any exception occurred
            if (hasException)
            {
                std::rethrow_exception(exceptionPtr);
            }
        }

        // Parallel reduction: computes reduction of func(i) for i in [begin, end)
        // Function signatures:
        //   Value func(size_t i) - computes value for index i
        //   Value reduce(Value a, Value b) - combines two values
        template<typename Value, typename Func, typename Reduce>
        Value ParallelReduce(size_t begin, size_t end, Func&& func, 
                           Reduce&& reduce, Value initialValue = Value())
        {
            if (begin >= end)
            {
                return initialValue;
            }

            size_t numThreads = workers.size();
            size_t range = end - begin;

            // For small ranges, just run sequentially
            if (range < numThreads)
            {
                Value result = initialValue;
                for (size_t i = begin; i < end; ++i)
                {
                    result = reduce(result, func(i));
                }
                return result;
            }

            // Divide work into blocks
            size_t blockSize = (range + numThreads - 1) / numThreads;
            std::vector<std::future<Value>> futures;
            futures.reserve(numThreads);

            // Launch tasks for each block
            for (size_t t = 0; t < numThreads; ++t)
            {
                size_t start = begin + t * blockSize;
                size_t finish = std::min(start + blockSize, end);

                if (start < finish)
                {
                    futures.push_back(Enqueue([start, finish, &func, &reduce, initialValue]() {
                        Value blockResult = initialValue;
                        for (size_t i = start; i < finish; ++i)
                        {
                            blockResult = reduce(blockResult, func(i));
                        }
                        return blockResult;
                    }));
                }
            }

            // Combine results from all blocks
            Value finalResult = initialValue;
            for (auto& future : futures)
            {
                finalResult = reduce(finalResult, future.get());
            }

            return finalResult;
        }

    private:
        // Worker thread function
        void WorkerThread()
        {
            while (true)
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    condition.wait(lock, [this] {
                        return stop || !tasks.empty();
                    });

                    if (stop && tasks.empty())
                    {
                        return;
                    }

                    task = std::move(tasks.front());
                    tasks.pop();
                }
                task();
            }
        }

        std::vector<std::thread> workers;
        std::queue<std::function<void()>> tasks;
        std::mutex queueMutex;
        std::condition_variable condition;
        std::atomic<bool> stop;
    };
}
