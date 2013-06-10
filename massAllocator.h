#pragma once
#include <vector>
#include <string>
#include <atomic>
#include <cstdlib>
#include <thread>

/*! \brief Хранилище для объектов с быстрым выделением нового элемента. 
*Поддерживаются только операции выделеления нового элемента и полной очистки.
*Тип T не должен иметь конструктора. Элемент инициализируется нулями.
*/
template <typename T>
class MassAllocator
{
public:
    typedef size_t size_type ;
    typedef T& reference;
    typedef const T& const_reference;
    typedef T* pointer;

    ///Конструктор. 
    MassAllocator(unsigned int blockSize = 1024 * 128);
    ///Деструктор.
    ~MassAllocator();

    ///Создание нового элемента. Возвращается указатель на созданый элемент и его индекс.
    pointer createElement(size_type *index = nullptr);

    ///Возвращает элемент по индексу
    reference operator[](size_type index);

    ///Возвращает элемент по индексу
    const_reference operator[](size_type index) const;

    ///Реализована ли lock-free семантика
    bool is_lock_free() const { return curAtomicIndex_.is_lock_free(); }

    ///Итератор для хранилища.
    class Iterator
    {
    public:
        typedef typename std::random_access_iterator_tag iterator_category;
        typedef T value_type;
        typedef size_t difference_type;
        typedef T* pointer;
        typedef T& reference;

        ///Перемещает итератор на следующий элемент хранилища
        Iterator& operator++();
        Iterator operator++(int);

        ///Перемещает итератор на предыдущий элемент хранилища
        Iterator& operator--();
        Iterator operator--(int);

        difference_type operator-(const Iterator &rh) const;
        Iterator operator-(difference_type offset) const;

        Iterator operator+(difference_type offset) const;

        ///Возвращает указатель на элемент, на котором находится итератор
        pointer operator->();

        ///Возвращает ссылку на элемент, на котором находится итератор
        reference operator*();

        ///Равенство итераторов
        bool operator==(const Iterator &rh) const;

        ///Сравнение итераторов на меньше
        bool operator<(const Iterator &rh) const;

        ///Неравенство итераторов
        bool operator!=(const Iterator &rh) const;

        ///Возвращает индекс элемента, на котором находится итератор
        size_type getIndex() const;
    private:
        friend class MassAllocator;
        MassAllocator *MassAllocator_;
        size_type index_;
    };

    ///Возвращает итератор на начало хранилища
    Iterator begin();

    ///Возвращает итератор на конец хранилища
    Iterator end();

    ///Kоличество элементов
    size_t size() const;

    ///Очищает хранилище, сбрасывает индекс.
    void clear();

    ///Возвращает потребление памяти
    size_t memUse() const;
private:
    //Запрет копирования
    MassAllocator(const MassAllocator &);
    MassAllocator& operator=(const MassAllocator &);

    ///Количество элементов в блоке.
    unsigned int elementsInBlockCount_;
    ///Блоки с элементами.
    std::vector<T*> blocks_; // вектор массивов 

    ///Сквозной индекс для захвата следующего свободного элемента.
    ///Cтаршие 32 бита это индекс блока, а нижние 32 бита - индекс элемента в блоке.
    std::atomic<uint64_t> curAtomicIndex_;

    ///Задает значение сквозного индекс по номеру блока и индексу в блоке.
    void setIndex(unsigned int blockIndx, unsigned int itemIndex);
};

template <typename T>
MassAllocator<T>::MassAllocator(unsigned int blockSize)
    : elementsInBlockCount_(blockSize)
{
    //задаем значение сквозного индекса таким, чтобы первое выделение элемента привело к распределению нового блока
    setIndex(0, elementsInBlockCount_);
}

template <typename T>
MassAllocator<T>::~MassAllocator()
{
    clear();
}

template <typename T>
void MassAllocator<T>::setIndex(unsigned int blockIndx, unsigned int itemIndex)
{
    auto a = (((uint64_t)blockIndx) << 32) + itemIndex;
    curAtomicIndex_.store(a);
}

template <typename T>
typename MassAllocator<T>::pointer MassAllocator<T>::createElement(size_type *returningIndex)
{
    //Делаем union для доступа к старшим и младшим 32 битам 64 битного целого
    union {
        uint64_t index;
        struct HiLoParts {
            uint32_t itemIndex;
            uint32_t blockIndx;
        } parts;
    };
    //получаем новый полный индекс
    index = curAtomicIndex_++;

    //если индекс элемента в блоке входит в допустимые пределы, то мы быстренько возвращаем индекс и указатель выделенного элемента
    if(parts.itemIndex < elementsInBlockCount_)
    {
        if (returningIndex != nullptr)
            *returningIndex = parts.blockIndx * elementsInBlockCount_ + parts.itemIndex;
        return &(blocks_[parts.blockIndx][parts.itemIndex]);
    }

    if (parts.itemIndex == elementsInBlockCount_)
    {
        //на нас закончился блок и именно нашему потоку нужно выделить еще один блок памяти
        auto bufferSize = elementsInBlockCount_ * sizeof(T);
        T* buffer = (T*)malloc(bufferSize);
        memset(buffer, 0, bufferSize);
        blocks_.push_back(buffer);
        
        //мы забираем себе нулевой элемент в блоке
        parts.blockIndx = (unsigned int)(blocks_.size() - 1);
        parts.itemIndex = 0;
        if (returningIndex != nullptr)
            *returningIndex = parts.blockIndx * elementsInBlockCount_ + parts.itemIndex;
        //устанавливаем счетчик на первый элемент в блоке
        setIndex(parts.blockIndx, 1);
        return &(blocks_[parts.blockIndx][parts.itemIndex]);
    }

    //ждем, пока другой поток производит выделение нового блока
    while(true)
    {
        //получаем новый полный индекс
        index = curAtomicIndex_++;
        if (parts.itemIndex == 0xffffffff)
            //мы крутили цикл ожидания настолько долго, что произошло переполнение
            throw std::string("Atomic index overflow");
        
        if (parts.itemIndex >= elementsInBlockCount_)
        {
            //блок еще не выделен, продолжаем ожидание
            std::this_thread::yield();
            continue;
        }
        
        //блок был выделен другим потоком, мы захватили валидный индекс элемента из нового блока
        if (returningIndex != nullptr)
            *returningIndex = parts.blockIndx * elementsInBlockCount_ + parts.itemIndex;
        return &(blocks_[parts.blockIndx][parts.itemIndex]);
    }
}

template <typename T>
typename MassAllocator<T>::reference MassAllocator<T>::operator[](size_type index)
{
    size_t indexOfBlock = index / elementsInBlockCount_;
    size_t indexInBlock = index % elementsInBlockCount_;
    return blocks_[indexOfBlock][indexInBlock];
}

template <typename T>
typename MassAllocator<T>::const_reference MassAllocator<T>::operator[](size_type index) const
{
    size_t indexOfBlock = index / elementsInBlockCount_;
    size_t indexInBlock = index % elementsInBlockCount_;
    return blocks_[indexOfBlock][indexInBlock];
}

template <typename T>
typename MassAllocator<T>::Iterator MassAllocator<T>::begin()
{
    Iterator result;
    result.MassAllocator_ = this;
    result.index_ = 0;
    return result;
}

template <typename T>
typename MassAllocator<T>::Iterator MassAllocator<T>::end()
{
    Iterator result;
    result.MassAllocator_ = this;
    result.index_ = size();
    return result;
}

template <typename T>
size_t MassAllocator<T>::size() const
{
    if (blocks_.empty())
        return 0;
    auto index = curAtomicIndex_.load();
    size_t blocksCount = index >> 32;
    size_t lastIndexInBlock = index & 0xffffffff;
    return blocksCount * elementsInBlockCount_ + lastIndexInBlock;
}

template <typename T>
size_t MassAllocator<T>::memUse() const
{
    return blocks_.size() * elementsInBlockCount_ * sizeof(T);
}

template <typename T>
void MassAllocator<T>::clear()
{
    //почистить все блоки данных
    for(auto ii = blocks_.begin(); ii != blocks_.end(); ++ii)
        free(*ii);
    blocks_.clear();
    //задаем значение сквозного индекса таким, чтобы первое выделение элемента привело к распределению нового блока
    setIndex(0, elementsInBlockCount_);
}
//=============================================================================

template <typename T>
typename MassAllocator<T>::Iterator& MassAllocator<T>::Iterator::operator++()
{
    ++index_;
    return *this;
}

template <typename T>
typename MassAllocator<T>::Iterator MassAllocator<T>::Iterator::operator++(int)
{
    Iterator result(*this);
    ++index_;
    return result;
}

template <typename T>
typename MassAllocator<T>::Iterator& MassAllocator<T>::Iterator::operator--()
{
    --index_;
    return *this;
}

template <typename T>
typename MassAllocator<T>::Iterator MassAllocator<T>::Iterator::operator--(int)
{
    Iterator result(*this);
    --index_;
    return result;
}

template <typename T>
typename MassAllocator<T>::Iterator::difference_type MassAllocator<T>::Iterator::operator-(const Iterator &rh) const
{
    return index_ - rh.index_;
}

template <typename T>
typename MassAllocator<T>::Iterator MassAllocator<T>::Iterator::operator-(typename MassAllocator<T>::Iterator::difference_type offset) const
{
    MassAllocator<T>::Iterator result(*this);
    result.index_ -= offset;
    return result;
}

template <typename T>
typename MassAllocator<T>::Iterator MassAllocator<T>::Iterator::operator+(typename MassAllocator<T>::Iterator::difference_type offset) const
{
    MassAllocator<T>::Iterator result(*this);
    result.index_ += offset;
    return result;
}

template <typename T>
typename MassAllocator<T>::pointer MassAllocator<T>::Iterator:: operator->()
{
    return &(*MassAllocator_)[index_];
}

template <typename T>
typename MassAllocator<T>::reference MassAllocator<T>::Iterator:: operator*()
{
    return (*MassAllocator_)[index_];
}

template <typename T>
bool MassAllocator<T>::Iterator::operator==(const Iterator &rh) const
{
    return index_ == rh.index_;
}

template <typename T>
bool MassAllocator<T>::Iterator::operator<(const Iterator &rh) const
{
    return index_ < rh.index_;
}

template <typename T>
bool MassAllocator<T>::Iterator::operator!=(const Iterator &rh) const
{
    return !(*this == rh);
}

template <typename T>
typename MassAllocator<T>::size_type MassAllocator<T>::Iterator::getIndex() const
{
    return index_;
}
