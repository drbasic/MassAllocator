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
        std::cout << "Objects in mass allocator = " << heap1.size() << " memory used = " << heap1.memUse() / (1024 * 1024.0) << "MB" << std::endl;
        heap1.clear();
        auto end = clock();
        float took_time = static_cast<float>(end - start);
        std::cout << "Allocation and deallocation " << N * ThreadCount << " objects took " << took_time / CLOCKS_PER_SEC << "sec" << std::endl;

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

    //проверка выделения объектов через стандартный менеджер памяти, выделять будем в 100 раз меньше элементов
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

