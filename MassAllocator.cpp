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
    const int N = 5000000;
    const int ThreadCount = 8;

    {//проверка выделения объектов через MassAllocator
        auto allocationStart = clock();
        MassAllocator<ObjectA> heap1;
        std::cout << "is_lock_free = " << (heap1.is_lock_free() ? std::string("true") : std::string("false")) << std::endl;
        std::cout << "Object size " << sizeof(ObjectA) << " bytes, allocate for " << N * ThreadCount<< " objects in " << ThreadCount << " threads, total objects size = " << sizeof(ObjectA) * N * ThreadCount / (1024 * 1024.0) << "MB" << std::endl;

        //сюда будем складывать все полученный индексы выделенных объектов
        std::array<std::vector<size_t>, ThreadCount> allocatedIndxs;
        //функция для потока
        auto func = 
            [&]
            (int threadIndx)
            {
                auto &indxs = allocatedIndxs[threadIndx];
                indxs.reserve(N);
                for(int i = 0; i < N; ++i)
                {
                    size_t indx;
                    //запрашиваем новый элемент и индекс этого элемента
                    ObjectA *obj = heap1.createElement(&indx);
                    obj->a = i;
                    indxs.push_back(indx);
                }

                std::cout << " Thread " << std::this_thread::get_id()<< " allocated " << N << " objects" << std::endl;
            };

        //запускаем потоки 
        typedef std::shared_ptr<std::thread> ThreadPtr;
        std::vector<ThreadPtr> threads;
        for(int i = 0; i < ThreadCount; ++i)
            threads.push_back(ThreadPtr(new std::thread(func, i)));
    
        //дожидаемся завершения всех потоков
        for(auto ii = threads.begin(), ie = threads.end(); ii != ie; ++ii)
            (*ii)->join();

        auto allocationEnd = clock();
        std::cout << "Objects in mass allocator = " << heap1.size() << " memory used = " << heap1.memUse() / (1024 * 1024.0) << "MB" << std::endl;

        {
            auto iterFunc = 
                [&]
                ()
                {
                    //обработка элементов index-like
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
                    //обработка элементов iterator-like
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

        //проверяем корректность выделения объектов.
        std::cout << "Check allocation continuity" << std::endl;
        std::vector<size_t> threadsCurrentIndx;
        threadsCurrentIndx.resize(ThreadCount);
        for(size_t i = 0, n = N * ThreadCount; i < n; ++i)
        {
            //ищем какой поток сделал захват индекса i
            for(int j = 0; j <= ThreadCount; ++j)
            {
                //Диагностируем ошибку, когда ни один из потоков не захватил элемент i 
                if (j == ThreadCount)
                    throw std::string("allocation error");
                //пропускаем поток, в котром уже проверили все элементы
                if (threadsCurrentIndx[j] == N)
                    continue;
                //проверяем что поток j захватил элемент i
                if (allocatedIndxs[j][threadsCurrentIndx[j]] != i)
                    continue;
                //да, поток j захватил элемент i. перемещаемся к его следующему элементу
                ++threadsCurrentIndx[j];
                break;
            }
        }
       
        std::cout << "Check allocation continuity finished with success!" << std::endl;
    }

    {//проверка выделения объектов через стандартный менеджер памяти
        auto allocationStart = clock();
        std::vector<std::vector<ObjectA*>> allocatedObjects;
        allocatedObjects.resize(ThreadCount);
        auto func = 
            [&]
            (int threadIndex)
            {
                auto &objects = allocatedObjects[threadIndex];
                objects.reserve(N);
                for(int i = 0; i < N; ++i)
                {
                    auto obj = new ObjectA();
                    obj->a = i;
                    objects.push_back(obj);
                }

                std::cout << " Thread " << std::this_thread::get_id()<< " allocated " << N << std::endl;
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

