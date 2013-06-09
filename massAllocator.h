#pragma once
#include <vector>
#include <string>
#include <atomic>

/*! \brief ��������� ��� �������� � ������� ���������� ������ ��������. 
*�������������� ������ �������� ����������� ������ �������� � ������ �������.
*��� T �� ������ ����� ������������. ������� ���������������� ������.
*/
template <typename T>
class MassAllocator
{
public:
    typedef size_t size_type ;
    typedef T& reference;
    typedef const T& const_reference;
    typedef T* pointer;

    ///�����������. 
    MassAllocator(unsigned int blockSize = 1024 * 128);
    ///����������.
    ~MassAllocator();

    ///�������� ������ ��������. ������������ ��������� �� �������� ������� � ��� ������.
    pointer createElement(size_type *index = nullptr);

    ///���������� ������� �� �������
    reference operator[](size_type index);

    ///���������� ������� �� �������
    const_reference operator[](size_type index) const;

    ///����������� �� lock-free ���������
    bool is_lock_free() const { return curAtomicIndex_.is_lock_free(); }

    ///�������� ��� ���������.
    class Iterator
    {
    public:
        typedef typename std::random_access_iterator_tag iterator_category;
        typedef typename T value_type;
        typedef typename size_t difference_type;
        typedef typename T* pointer;
        typedef typename T& reference;

        ///���������� �������� �� ��������� ������� ���������
        Iterator& operator++();
        Iterator operator++(int);

        ///���������� �������� �� ���������� ������� ���������
        Iterator& operator--();
        Iterator operator--(int);

        difference_type operator-(const Iterator &rh) const;
        Iterator operator-(difference_type offset) const;

        Iterator operator+(difference_type offset) const;

        ///���������� ��������� �� �������, �� ������� ��������� ��������
        pointer operator->();

        ///���������� ������ �� �������, �� ������� ��������� ��������
        reference operator*();

        ///��������� ����������
        bool operator==(const Iterator &rh) const;

        ///��������� ���������� �� ������
        bool operator<(const Iterator &rh) const;

        ///����������� ����������
        bool operator!=(const Iterator &rh) const;

        ///���������� ������ ��������, �� ������� ��������� ��������
        size_type getIndex() const;
    private:
        friend class MassAllocator;
        MassAllocator *MassAllocator_;
        size_type index_;
    };

    ///���������� �������� �� ������ ���������
    Iterator begin();

    ///���������� �������� �� ����� ���������
    Iterator end();

    ///K��������� ���������
    size_t size() const;

    ///������� ���������, ���������� ������.
    void clear();

    ///���������� ����������� ������
    size_t memUse() const;
private:
    MassAllocator(const MassAllocator &);
    MassAllocator& operator=(const MassAllocator &);

    typedef unsigned long long uint64;
    ///���������� ��������� � �����.
    unsigned int elementsInBlockCount_;
    ///����� � ����������.
    std::vector<T*> blocks_; // ������ �������� 

    ///�������� ������ ��� ������� ���������� ���������� ��������.
    ///C������ 32 ���� ��� ������ �����, � ������ 32 ���� - ������ �������� � �����.
    std::atomic<uint64> curAtomicIndex_;

    ///������ �������� ��������� ������ �� ������ ����� � ������� � �����.
    void setIndex(unsigned int blockIndx, unsigned int itemIndex);
};

template <typename T>
MassAllocator<T>::MassAllocator(unsigned int blockSize)
    : elementsInBlockCount_(blockSize)
{
    //������ �������� ��������� ������� �����, ����� ������ ��������� �������� ������� � ������������� ������ �����
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
    auto a = (((uint64)blockIndx) << 32) + itemIndex;
    curAtomicIndex_.store(a);
}

template <typename T>
typename MassAllocator<T>::pointer MassAllocator<T>::createElement(size_type *returningIndex)
{
    //������ union ��� ������� � ������� � ������� 32 ����� 64 ������� ������
    union {
        uint64_t index;
        struct {
            uint32_t itemIndex;
            uint32_t blockIndx;
        };
    };
    //�������� ����� ������ ������
    index = curAtomicIndex_++;
    //index = curAtomicIndex_.fetch_add(1, std::memory_order_relaxed);

    //���� ������ �������� � ����� ������ � ���������� �������, �� �� ���������� ���������� ������ � ��������� ����������� ��������
    if(itemIndex < elementsInBlockCount_)
    {
        if (returningIndex != nullptr)
            *returningIndex = blockIndx * elementsInBlockCount_ + itemIndex;
        return &(blocks_[blockIndx][itemIndex]);
    }

    if (itemIndex == elementsInBlockCount_)
    {
        //�� ��� ���������� ���� � ������ ������ ������ ����� �������� ��� ���� ���� ������
        auto bufferSize = elementsInBlockCount_ * sizeof(T);
        T* buffer = (T*)malloc(bufferSize);
        memset(buffer, 0, bufferSize);
        blocks_.push_back(buffer);
        
        //�� �������� ���� ������� ������� � �����
        blockIndx = (unsigned int)(blocks_.size() - 1);
        itemIndex = 0;
        if (returningIndex != nullptr)
            *returningIndex = blockIndx * elementsInBlockCount_ + itemIndex;
        //������������� ������� �� ������ ������� � �����
        setIndex(blockIndx, 1);
        return &(blocks_[blockIndx][itemIndex]);
    }

    //����, ���� ������ ����� ���������� ��������� ������ �����
    while(true)
    {
        //�������� ����� ������ ������
        index = curAtomicIndex_++;
        //index = curAtomicIndex_.fetch_add(1, std::memory_order_relaxed);
        
        if (itemIndex == 0xffffffff)
            //�� ������� ���� �������� ��������� �����, ��� ��������� ������������
            throw std::string("Atomic index overflow");
        
        if (itemIndex >= elementsInBlockCount_)
        {
            //���� ��� �� �������, ���������� ��������
            std::this_thread::yield();
            continue;
        }
        
        //���� ��� ������� ������ �������, �� ��������� �������� ������ �������� �� ������ �����
        if (returningIndex != nullptr)
            *returningIndex = blockIndx * elementsInBlockCount_ + itemIndex;
        return &(blocks_[blockIndx][itemIndex]);
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
    //��������� ��� ����� ������
    for(auto ii = blocks_.begin(); ii != blocks_.end(); ++ii)
        free(*ii);
    blocks_.clear();
    //������ �������� ��������� ������� �����, ����� ������ ��������� �������� ������� � ������������� ������ �����
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
