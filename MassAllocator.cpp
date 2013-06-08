#include <iostream>
#include <array>
#include <thread>
#include <memory>
#include <tchar.h>
#include "massAllocator.h"

struct ObjectA
{
    int a;
    double b;
};

int _tmain(int argc, _TCHAR* argv[])
{
    const int N = 10000000;
    const int N2 = N / 1000;
    const int ThreadCount = 8;

    {
        auto start = clock();
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
        std::cout << "Objects in mass allocator = " << heap1.size() << " memory used = " << heap1.memUse() / (1024 * 1024.0) << "MB" << std::endl;
        heap1.clear();
        auto end = clock();
        float took_time = static_cast<float>(end - start);
        std::cout << "Allocation and deallocation " << N * ThreadCount << " objects took " << took_time / CLOCKS_PER_SEC << "sec" << std::endl;

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

    //�������� ��������� �������� ����� ����������� �������� ������, �������� ����� � 100 ��� ������ ���������
    {
        auto start = clock();
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
                    objects.push_back(new ObjectA());
                }

                std::cout << " Thread " << std::this_thread::get_id()<< " allocated " << N2 << std::endl;
            };
    
        typedef std::shared_ptr<std::thread> ThreadPtr;
        std::vector<ThreadPtr> threads;
        for(int i = 0; i < ThreadCount; ++i)
            threads.push_back(ThreadPtr(new std::thread(func, i)));
    
        for(auto ii = threads.begin(), ie = threads.end(); ii != ie; ++ii)
            (*ii)->join();

        for(auto ii = allocatedObjects.begin(), ie = allocatedObjects.end(); ii != ie; ++ii)
            for(auto jj = ii->begin(), je = ii->end(); jj != je; ++jj)
                delete *jj;

        auto end = clock();
        float took_time = static_cast<float>(end - start);
        std::cout << "Allocation and deallocation " << N2 * ThreadCount << " objects by operator new took " << took_time / CLOCKS_PER_SEC << "sec" << std::endl;
    }

    std::getchar();
    return 0;
}

