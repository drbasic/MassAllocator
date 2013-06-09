#include <iostream>
#include <array>
#include <thread>
#include <memory>
#include <tchar.h>
#include "massAllocator.h"

struct ObjectA
{
    int a;
    double b[1];
};

template <typename F>
void measureTime(F f, std::string message)
{
    auto allocationStart = clock();
    f();
    auto allocationEnd = clock();
    auto time = (double) (allocationEnd - allocationStart) / CLOCKS_PER_SEC;
    std::cout << message << " took " << time << "sec" << std::endl;
}

int _tmain(int argc, _TCHAR* argv[])
{
    const int N = 10000000;
    const int N2 = N;
    const int ThreadCount = 8;

    {
        auto allocationStart = clock();
        MassAllocator<ObjectA> heap1;
        std::cout << "is_lock_free = " << (heap1.is_lock_free() ? std::string("true") : std::string("false")) << std::endl;
        std::cout << "Object size " << sizeof(ObjectA) << " bytes, allocate for " << N * ThreadCount<< " objects in " << ThreadCount << " threads, total objects size = " << sizeof(ObjectA) * N * ThreadCount / (1024 * 1024.0) << "MB" << std::endl;

        //���� ����� ���������� ��� ���������� ������� ���������� ��������
        std::array<std::vector<size_t>, ThreadCount> allocatedIndxs;
        //������� ��� ������
        auto func = 
            [&]
            (int threadIndx)
            {
                auto &indxs = allocatedIndxs[threadIndx];
                indxs.reserve(N);
                for(int i = 0; i < N; ++i)
                {
                    size_t indx;
                    //����������� ����� ������� � ������ ����� ��������
                    ObjectA *obj = heap1.createElement(&indx);
                    obj->a = i;
                    indxs.push_back(indx);
                }

                std::cout << " Thread " << std::this_thread::get_id()<< " allocated " << N << " objects" << std::endl;
            };

        //��������� ������ 
        typedef std::shared_ptr<std::thread> ThreadPtr;
        std::vector<ThreadPtr> threads;
        for(int i = 0; i < ThreadCount; ++i)
            threads.push_back(ThreadPtr(new std::thread(func, i)));
    
        //���������� ���������� ���� �������
        for(auto ii = threads.begin(), ie = threads.end(); ii != ie; ++ii)
            (*ii)->join();

        auto allocationEnd = clock();
        std::cout << "Objects in mass allocator = " << heap1.size() << " memory used = " << heap1.memUse() / (1024 * 1024.0) << "MB" << std::endl;

        {
            auto iterFunc = 
                [&]
                ()
                {
                    //��������� ��������� index-like
                    for(size_t i = 0, n = heap1.size(); i < n; ++i)
                        heap1[i].a += 1;
                };
            measureTime(iterFunc, "Index-based processing");
        }
        {
            auto iterFunc = 
                [&]
                ()
                {
                    //��������� ��������� iterator-like
                    for(auto ii = heap1.begin(), ie = heap1.end(); ii != ie; ++ii)
                        ii->b[0] = ii->a * 42;
                };
            measureTime(iterFunc, "Iterator-based processing");
        }
        
        {
            auto sortFunc = 
                [&]
                ()
                {
                    std::sort(
                        heap1.begin(), 
                        heap1.begin() + N, 
                        [](const ObjectA &lh, const ObjectA &rh)
                        { return lh.a > rh.a; }
                    );
                };
            measureTime(sortFunc, "Sort");
        }

        auto deallocationStart = clock();
        heap1.clear();
        auto deallocationEnd = clock();
        
        auto allocTime = (double) (allocationEnd - allocationStart) / CLOCKS_PER_SEC;
        auto deallocTime = (double) (deallocationEnd - deallocationStart) / CLOCKS_PER_SEC;
        std::cout << "Allocation and deallocation " << N * ThreadCount << " objects took " << allocTime << "+" << deallocTime << " = " <<allocTime + deallocTime << "sec" << std::endl;

        //��������� ������������ ��������� ��������.
        std::cout << "Check allocation continuity" << std::endl;
        std::vector<size_t> threadsCurrentIndx;
        threadsCurrentIndx.resize(ThreadCount);
        for(size_t i = 0, n = N * ThreadCount; i < n; ++i)
        {
            //���� ����� ����� ������ ������ ������� i
            for(int j = 0; j <= ThreadCount; ++j)
            {
                //������������� ������, ����� �� ���� �� ������� �� �������� ������� i 
                if (j == ThreadCount)
                    throw std::string("allocation error");
                //���������� �����, � ������ ��� ��������� ��� ��������
                if (threadsCurrentIndx[j] == N)
                    continue;
                //��������� ��� ����� j �������� ������� i
                if (allocatedIndxs[j][threadsCurrentIndx[j]] != i)
                    continue;
                //��, ����� j �������� ������� i. ������������ � ��� ���������� ��������
                ++threadsCurrentIndx[j];
                break;
            }
        }
       
        std::cout << "Check allocation continuity finished with success!" << std::endl;
    }

    //�������� ��������� �������� ����� ����������� �������� ������
    {
        auto allocationStart = clock();
        std::vector<std::vector<ObjectA*>> allocatedObjects;
        allocatedObjects.resize(ThreadCount);
        auto func = 
            [&]
            (int threadIndex)
            {
                auto &objects = allocatedObjects[threadIndex];
                objects.reserve(N2);
                for(int i = 0; i < N2; ++i)
                {
                    auto obj = new ObjectA();
                    obj->a = i;
                    objects.push_back(obj);
                }

                std::cout << " Thread " << std::this_thread::get_id()<< " allocated " << N2 << std::endl;
            };
    
        typedef std::shared_ptr<std::thread> ThreadPtr;
        std::vector<ThreadPtr> threads;
        for(int i = 0; i < ThreadCount; ++i)
            threads.push_back(ThreadPtr(new std::thread(func, i)));
    
        for(auto ii = threads.begin(), ie = threads.end(); ii != ie; ++ii)
            (*ii)->join();
        
        auto allocationEnd = clock();

        auto deallocationStart = clock();
        for(auto ii = allocatedObjects.begin(), ie = allocatedObjects.end(); ii != ie; ++ii)
            for(auto jj = ii->begin(), je = ii->end(); jj != je; ++jj)
                delete *jj;

        auto deallocationEnd = clock();

        auto allocTime = (double) (allocationEnd - allocationStart) / CLOCKS_PER_SEC;
        auto deallocTime = (double) (deallocationEnd - deallocationStart) / CLOCKS_PER_SEC;
        std::cout << "operator new-based allocation and deallocation " << N * ThreadCount << " objects took " << allocTime << "+" << deallocTime << " = " <<allocTime + deallocTime << "sec" << std::endl;

    }

    std::getchar();
    return 0;
}

