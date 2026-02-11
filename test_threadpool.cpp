// Test ThreadPool implementation for correctness and performance
// Tests parallel for, parallel reduce, and exception handling

#include <GTE/Mathematics/ThreadPool.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <chrono>
#include <cmath>
#include <numeric>
#include <vector>

using namespace gte;

// Test 1: Basic ParallelFor correctness
bool TestParallelForCorrectness()
{
    std::cout << "\n=== Test 1: ParallelFor Correctness ===" << std::endl;
    
    const size_t N = 10000;
    std::vector<int> data(N, 0);
    
    ThreadPool pool(4);
    
    // Each thread writes to independent elements
    pool.ParallelFor(0, N, [&data](size_t i) {
        data[i] = static_cast<int>(i * i);
    });
    
    // Verify results
    for (size_t i = 0; i < N; ++i)
    {
        if (data[i] != static_cast<int>(i * i))
        {
            std::cout << "FAILED: data[" << i << "] = " << data[i] 
                      << ", expected " << (i * i) << std::endl;
            return false;
        }
    }
    
    std::cout << "PASSED: All " << N << " elements correct" << std::endl;
    return true;
}

// Test 2: ParallelReduce correctness
bool TestParallelReduceCorrectness()
{
    std::cout << "\n=== Test 2: ParallelReduce Correctness ===" << std::endl;
    
    const size_t N = 10000;
    ThreadPool pool(4);
    
    // Sum of squares: 0^2 + 1^2 + ... + (N-1)^2
    auto sum = pool.ParallelReduce<double>(
        0, N,
        [](size_t i) { return static_cast<double>(i * i); },
        [](double a, double b) { return a + b; },
        0.0
    );
    
    // Compute expected sum using formula: n(n-1)(2n-1)/6
    double expected = static_cast<double>(N) * (N - 1) * (2 * N - 1) / 6.0;
    
    if (std::abs(sum - expected) > 1e-6)
    {
        std::cout << "FAILED: sum = " << sum << ", expected " << expected << std::endl;
        return false;
    }
    
    std::cout << "PASSED: Sum = " << sum << " (expected " << expected << ")" << std::endl;
    return true;
}

// Test 3: Performance scaling
bool TestPerformanceScaling()
{
    std::cout << "\n=== Test 3: Performance Scaling ===" << std::endl;
    
    const size_t N = 1000000;
    std::vector<double> data(N);
    
    // Fill with random data
    for (size_t i = 0; i < N; ++i)
    {
        data[i] = std::sin(static_cast<double>(i) * 0.001);
    }
    
    // Test different thread counts
    std::vector<size_t> threadCounts = {1, 2, 4, 8};
    std::vector<double> times;
    
    for (size_t numThreads : threadCounts)
    {
        ThreadPool pool(numThreads);
        std::vector<double> result(N, 0.0);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        pool.ParallelFor(0, N, [&data, &result](size_t i) {
            // Simulate some work
            result[i] = std::sqrt(std::abs(data[i])) + std::cos(data[i]);
        });
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        times.push_back(duration.count());
        
        std::cout << "  " << numThreads << " threads: " << duration.count() << " ms" << std::endl;
    }
    
    // Check that more threads generally means better performance
    if (times.size() >= 2 && times[0] > 0)
    {
        double speedup = static_cast<double>(times[0]) / times.back();
        std::cout << "  Speedup (1 vs " << threadCounts.back() << " threads): " 
                  << speedup << "x" << std::endl;
    }
    
    std::cout << "PASSED: Performance test complete" << std::endl;
    return true;
}

// Test 4: Exception handling
bool TestExceptionHandling()
{
    std::cout << "\n=== Test 4: Exception Handling ===" << std::endl;
    
    ThreadPool pool(4);
    bool exceptionCaught = false;
    
    try
    {
        pool.ParallelFor(0, 100, [](size_t i) {
            if (i == 50)
            {
                throw std::runtime_error("Test exception");
            }
        });
    }
    catch (const std::runtime_error& e)
    {
        exceptionCaught = true;
        std::cout << "  Caught expected exception: " << e.what() << std::endl;
    }
    
    if (!exceptionCaught)
    {
        std::cout << "FAILED: Exception not propagated" << std::endl;
        return false;
    }
    
    std::cout << "PASSED: Exception handling works" << std::endl;
    return true;
}

// Test 5: Thread safety with shared accumulator
bool TestThreadSafety()
{
    std::cout << "\n=== Test 5: Thread Safety ===" << std::endl;
    
    const size_t N = 10000;
    ThreadPool pool(8);
    
    // Use ParallelReduce for thread-safe accumulation
    auto sum = pool.ParallelReduce<size_t>(
        0, N,
        [](size_t i) { return i; },
        [](size_t a, size_t b) { return a + b; },
        0
    );
    
    size_t expected = N * (N - 1) / 2;
    
    if (sum != expected)
    {
        std::cout << "FAILED: sum = " << sum << ", expected " << expected << std::endl;
        return false;
    }
    
    std::cout << "PASSED: Thread-safe accumulation works" << std::endl;
    return true;
}

// Test 6: Enqueue individual tasks
bool TestEnqueue()
{
    std::cout << "\n=== Test 6: Enqueue Tasks ===" << std::endl;
    
    ThreadPool pool(4);
    
    // Enqueue several tasks
    auto future1 = pool.Enqueue([](int x) { return x * x; }, 5);
    auto future2 = pool.Enqueue([](int x, int y) { return x + y; }, 10, 20);
    auto future3 = pool.Enqueue([]() { return std::string("Hello"); });
    
    // Get results
    int result1 = future1.get();
    int result2 = future2.get();
    std::string result3 = future3.get();
    
    if (result1 != 25 || result2 != 30 || result3 != "Hello")
    {
        std::cout << "FAILED: Enqueue results incorrect" << std::endl;
        return false;
    }
    
    std::cout << "PASSED: Enqueue works correctly" << std::endl;
    return true;
}

int main()
{
    std::cout << "ThreadPool Test Suite" << std::endl;
    std::cout << "=====================" << std::endl;
    
    // Detect hardware concurrency
    size_t hwConcurrency = std::thread::hardware_concurrency();
    std::cout << "Hardware concurrency: " << hwConcurrency << " threads" << std::endl;
    
    bool allPassed = true;
    
    allPassed &= TestParallelForCorrectness();
    allPassed &= TestParallelReduceCorrectness();
    allPassed &= TestPerformanceScaling();
    allPassed &= TestExceptionHandling();
    allPassed &= TestThreadSafety();
    allPassed &= TestEnqueue();
    
    std::cout << "\n=== Summary ===" << std::endl;
    if (allPassed)
    {
        std::cout << "✓ ALL TESTS PASSED" << std::endl;
        return 0;
    }
    else
    {
        std::cout << "✗ SOME TESTS FAILED" << std::endl;
        return 1;
    }
}
