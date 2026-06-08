/*
*   MIT License
*
*   Copyright(c) 2023 Devin Wolfe
*
*   Permission is hereby granted, free of charge, to any person obtaining a copy
*   of this software and associated documentation files(the "Software"), to deal
*   in the Software without restriction, including without limitation the rights
*   to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
*   copies of the Software, and to permit persons to whom the Software is
*   furnished to do so, subject to the following conditions :
*
*   The above copyright notice and this permission notice shall be included in all
*   copies or substantial portions of the Software.
*
*   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
*   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
*   SOFTWARE.
*/

/*
*   NoCopyRingFifo class
*
*   This class defines a type of FIFO that provides the user with Span objects that provide a readable and writable
*   view into chunks of FIFO memory without copying data in or out.  This is useful when delegating the actual read and
*   write operations for asynchronous/overlapped IO operations.
*
*   Writing to the FIFO happens in 3 stages - First a block of data in the FIFO is reserved, then at some later time
*   data is written.  Once the write is complete, the written data is committed to the FIFO.
*
*   Once data is comitted to the FIFO, it is immediately available to read.
*
*   The underlying memory is a single contiguous block, and read and write operations wrap around the ends.  It is
*   therefore possible that a read or write could involve two different copy operations on seperate sections of memory.
*   The DataBlock class defined here contains two spans to cover the case of a wraparound.
*/

#pragma once

#include <cstring>
#include <cstdint>
#include <span>
#include <vector>
#include <format>
#include <condition_variable>
#include <chrono>

using namespace std::chrono_literals;

namespace FifoTemplates
{
    template <typename T> class NoCopyRingFifo
    {
    public:
        
        // Class to hold spans used to view or copy a block of data in the FIFO.
        // A read or write to the FIFO may be split between 2 spans if it wraps around the end of the buffer.
        class DataBlock
        {
        public:
            DataBlock() : spans{ std::span<T>(), std::span<T>() } {}
            DataBlock(std::span<T>&& span0) : spans{ span0, std::span<T>() }, _size(span0.size()) {}
            DataBlock(std::span<T>&& span0, std::span<T>&& span1) : spans{ span0, span1 }, _size(span0.size() + span1.size()) {}

            inline bool isSplit() const { return (spans[1].empty() == false); }
            inline bool isValid() const { return (spans[0].empty() == false); }
            size_t size() { return _size; }

            std::span<T> spans[2];

        private:
            size_t _size = 0;
        };

        NoCopyRingFifo(size_t size) : maxSize(size)
        {
            _ringBuffer.resize(size);
            _ringBufferSpan = std::span<T>(_ringBuffer);
        }

        const std::vector<T> data()
        {
            return _ringBuffer;
        }

        // Reserve a block of FIFO memory, returning a FifoBlock object.
        // An exception is thrown if there is insufficient reservable space.
        DataBlock reserve(size_t size)
        {
            if (size > reservableSize()) {
                throw std::overflow_error(
                    std::format("Not enough free space in FIFO for reserve - requested {}, available {}",
                    size,
                    reservableSize())
                    );
            }

            _reserved += size;

            return getDataBlock(_writeIndex, size);
        }

        // Commit a block of data to the FIFO.  This increases the amount of committed data that is
        // available to be read and decreases the amount of reserved data, both by the commit size.
        // An exception is throw if there is insufficient reserved space for the commit.
        void commit(size_t size)
        {
            if (size > commitableSize()) {
                throw std::overflow_error(
                    std::format("Not enough reserved space in FIFO for commit - requested {}, available {}", 
                    size,
                    commitableSize()
                    )
                );
            }

            {
                auto lock = std::lock_guard<std::mutex>(_fifoMutex);

                _committed += size;
                _reserved -= size;
            }

            _readableCv.notify_all();
        }

        inline size_t reservableSize() const { return (_ringBuffer.size() - (_reserved + _committed)); }
        inline size_t commitableSize() const { return _reserved; }
        inline size_t readableSize() const { return _committed; }

        template <typename Duration>
        bool waitOnReservableCv(size_t size, Duration timeout)
        {
            auto lock = std::unique_lock<std::mutex>(_fifoMutex);
            return _reservableCv.wait_for(lock, timeout, [this, size]() { return (reservableSize() >= size); });
        }
        
        template <typename Duration>
        bool waitOnReservableBusy(size_t size, Duration timeout)
        {
            auto startTime = std::chrono::steady_clock::now();
            auto endTime = startTime + timeout;

            while (reservableSize() < size) {
                auto currentTime = std::chrono::steady_clock::now();
                if (currentTime >= endTime) {
                    return false;
                }
            }

            return true;
        }

        template <typename Duration>
        bool waitOnReadableCv(size_t size, Duration timeout)
        {
            auto lock = std::unique_lock<std::mutex>(_fifoMutex);
            return _reservableCv.wait_for(lock, timeout, [this, size]() { return (readableSize() >= size); });
        }

        template <typename Duration>
        bool waitOnReadableBusy(size_t size, Duration timeout)
        {
            auto startTime = std::chrono::steady_clock::now();
            auto endTime = startTime + timeout;

            while (readableSize() < size) {
                auto currentTime = std::chrono::steady_clock::now();
                if (currentTime >= endTime) {
                    return false;
                }
            }

            return true;
        }

        // Get a block of comitted data to read.
        DataBlock readBlock(size_t size)
        {
            if (size > _committed) {
                throw std::underflow_error(
                    std::format("Read larger than committed size - requested {}, available {}", size, _committed)
                );
            }

            return getDataBlock(_readIndex, size);
        }

        void releaseReadData(size_t size)
        {
            if (size > _committed) {
                throw std::underflow_error(
                    std::format("Read release larger than committed size - requested {}, available {}", size, _committed)
                );
            }

            {
                auto lock = std::lock_guard<std::mutex>(_fifoMutex);
                _committed -= size;
            }

            _reservableCv.notify_all();
        }

        void reset(void)
        {
            _readIndex = 0;
            _writeIndex = 0;
            _reserved = 0;
            _committed = 0;
        }

        const size_t maxSize;
        
    private:
        // Get a block of data starting at the specified index.  This is used by both the reserve and readBlock functions.
        DataBlock getDataBlock(size_t& index, size_t size)
        {
            if (size > _ringBuffer.size()) {
                throw std::overflow_error(
                    std::format("Requested span size larger than FIFO size - requested {}, available {}", 
                    size,
                    _ringBuffer.size()
                    )
                );
            }
            else if (size == 0) {
                return DataBlock();
            }

            const size_t remainingBufferSize = (_ringBuffer.size() - index);
            size_t oldIndex = index;
            index = (index + size) % _ringBuffer.size();

            if (size > remainingBufferSize) {
                return DataBlock(
                    _ringBufferSpan.subspan(oldIndex, remainingBufferSize),
                    _ringBufferSpan.subspan(0, index)
                );
            }
            else {
                return DataBlock(_ringBufferSpan.subspan(oldIndex, size));
            }
        }

        std::vector<T> _ringBuffer;
        std::span<T> _ringBufferSpan;
        size_t _readIndex = 0;
        size_t _writeIndex = 0;
        size_t _reserved = 0;
        size_t _committed = 0;
        std::condition_variable _reservableCv;
        std::condition_variable _readableCv;
        std::mutex _fifoMutex;
    };
}