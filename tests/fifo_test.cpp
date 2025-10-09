#include <format>

#include <gtest/gtest.h>

#include "fifo_test_fixture.hpp"

using namespace FifoTemplates;

std::vector<fifoDataType> FifoTest::GetTestVector(int size)
{
    // Create a random test vector.
    std::vector<fifoDataType> testVector;
    for (int i = 0; i < maxFifoSize; i++)
    {
        testVector.push_back(std::rand());
    }

    return testVector;
}

TEST_F(FifoTest, Reset)
{
    fifo.Reset();

    // Fifo should be empty after reset.
    EXPECT_EQ(fifo.CommitableSize(), 0);
    EXPECT_EQ(maxFifoSize, fifo.ReservableSize());

    // Try to commmit, should fail because no space has been reserved.
    EXPECT_THROW(fifo.Commit(1), std::overflow_error);
}

// Test filling the FIFO from a reset state with reserve, write, commit, and read of one data element at a time.
TEST_F(FifoTest, ReserveWriteCommitReadSingle)
{
    fifo.Reset();

    auto testVector = GetTestVector(maxFifoSize);

    // Reserve until full, testing size along the way.
    for (int i = 0; i < maxFifoSize; i++)
    {
        SCOPED_TRACE(std::format("Reserve loop iteration {}\r\n", i));

        EXPECT_EQ(fifo.ReservableSize(), (maxFifoSize - i));
        EXPECT_EQ(fifo.CommitableSize(), i);

        NoCopyRingFifo<fifoDataType>::DataBlock inDataBlock;
        ASSERT_NO_THROW(inDataBlock = fifo.Reserve(1));
        EXPECT_EQ(inDataBlock.isValid(), true);
        EXPECT_EQ(inDataBlock.isSplit(), false);

        inDataBlock.spans[0][0] = testVector[i];
    }

    // Try to reserve, should throw due to FIFO being fully reserved.
    EXPECT_THROW(fifo.Reserve(1), std::overflow_error);

    // Commit until full, testing size along the way.
    for (int i = 0; i < maxFifoSize; i++)
    {
        SCOPED_TRACE(std::format("Commit loop iteration {}\r\n", i));

        EXPECT_EQ(fifo.ReservableSize(), 0);
        EXPECT_EQ(fifo.CommitableSize(), (maxFifoSize - i));

        fifo.Commit(1);
    }

    // Try to commit, should throw due to FIFO being fully committed.
    EXPECT_THROW(fifo.Commit(1), std::overflow_error);

    // Read until empty.
    for (int i = 0; i < maxFifoSize; i++)
    {
        SCOPED_TRACE(std::format("Read loop iteration {}\r\n", i));

        EXPECT_EQ(fifo.ReservableSize(), i);
        EXPECT_EQ(fifo.CommitableSize(), 0);

        NoCopyRingFifo<fifoDataType>::DataBlock outDataBlock;
        ASSERT_NO_THROW(outDataBlock = fifo.ReadBlock(1));
        EXPECT_EQ(outDataBlock.isValid(), true);
        EXPECT_EQ(outDataBlock.isSplit(), false);
        
        EXPECT_EQ(testVector[i], outDataBlock.spans[0][0]);
    }

    // Try to read, should throw because FIFO is empty.
    EXPECT_THROW(fifo.ReadBlock(1), std::underflow_error);
}

// Test reserves of varying block sizes.
TEST_F(FifoTest, Reserve)
{
    for (int blockSize = 1; blockSize < maxFifoSize; blockSize++)
    {
        SCOPED_TRACE(std::format("Reserve block loop iteration {}\r\n", blockSize));

        fifo.Reset();

        NoCopyRingFifo<fifoDataType>::DataBlock dataBlock;

        ASSERT_NO_THROW(dataBlock = fifo.Reserve(blockSize));
        EXPECT_EQ(dataBlock.spans[0].size(), blockSize);
        EXPECT_EQ(dataBlock.isValid(), true);
        EXPECT_EQ(dataBlock.isSplit(), false);

        EXPECT_EQ(fifo.ReservableSize(), (maxFifoSize - blockSize));
        EXPECT_EQ(fifo.CommitableSize(), blockSize);
    }
}

// Test commits of varying block sizes.
TEST_F(FifoTest, Commit)
{
    for (int blockSize = 1; blockSize < maxFifoSize; blockSize++)
    {
        SCOPED_TRACE(std::format("Commit block loop iteration {}\r\n", blockSize));

        fifo.Reset();

        ASSERT_NO_THROW(fifo.Reserve(maxFifoSize));

        ASSERT_NO_THROW(fifo.Commit(blockSize));

        EXPECT_EQ(fifo.ReservableSize(), 0);
        EXPECT_EQ(fifo.CommitableSize(), (maxFifoSize - blockSize));
    }
}

// Test reads of varying block sizes.
TEST_F(FifoTest, Read)
{
    for (int blockSize = 1; blockSize < maxFifoSize; blockSize++)
    {
        SCOPED_TRACE(std::format("Read block loop iteration {}\r\n", blockSize));

        auto testVector = GetTestVector(blockSize);

        fifo.Reset();

        // Reserve a block.
        NoCopyRingFifo<fifoDataType>::DataBlock inDataBlock;
        ASSERT_NO_THROW(inDataBlock = fifo.Reserve(blockSize));
        EXPECT_EQ(inDataBlock.spans[0].size(), blockSize);
        EXPECT_EQ(inDataBlock.isValid(), true);
        EXPECT_EQ(inDataBlock.isSplit(), false);

        // Copy data from the test vector to the block.
        ASSERT_NO_THROW(std::copy(testVector.begin(), testVector.begin() + blockSize, inDataBlock.spans[0].begin()));
        ASSERT_NO_THROW(fifo.Commit(blockSize));

        EXPECT_EQ(fifo.ReservableSize(), (maxFifoSize - blockSize));
        EXPECT_EQ(fifo.CommitableSize(), 0);

        // Read a block.
        NoCopyRingFifo<fifoDataType>::DataBlock outDataBlock;
        ASSERT_NO_THROW(outDataBlock = fifo.ReadBlock(blockSize));
        EXPECT_EQ(outDataBlock.spans[0].size(), blockSize);
        EXPECT_EQ(outDataBlock.isValid(), true);
        EXPECT_EQ(outDataBlock.isSplit(), false);

        // Compare input and output.
        for (int i = 0; i < blockSize; i++)
        {
            SCOPED_TRACE(std::format("Read block compare loop iteration {}\r\n", i));
            EXPECT_EQ(inDataBlock.spans[0][i], outDataBlock.spans[0][i]);
        }
    }
}

// Test FIFO buffer wraparound for various block sizes.
TEST_F(FifoTest, Wraparound)
{
    for (int blockSize = 2; blockSize < maxFifoSize; blockSize++)
    {
        SCOPED_TRACE(std::format("Read block loop iteration {}\r\n", blockSize));

        auto testVector = GetTestVector(blockSize);

        fifo.Reset();

        // Reserve, commit and read one less than the buffer size.
        ASSERT_NO_THROW(fifo.Reserve(maxFifoSize - 1));
        ASSERT_NO_THROW(fifo.Commit(maxFifoSize - 1));
        ASSERT_NO_THROW(fifo.ReadBlock(maxFifoSize - 1));

        // Reserve a block.
        NoCopyRingFifo<fifoDataType>::DataBlock inDataBlock;
        ASSERT_NO_THROW(inDataBlock = fifo.Reserve(blockSize));
        EXPECT_EQ(inDataBlock.spans[0].size(), 1);
        EXPECT_EQ(inDataBlock.spans[1].size(), (blockSize - 1));
        EXPECT_EQ(inDataBlock.isValid(), true);
        EXPECT_EQ(inDataBlock.isSplit(), true);

        // Copy data from the test vector to the block.
        inDataBlock.spans[0][0] = testVector[0];
        ASSERT_NO_THROW(std::copy((testVector.begin() + 1), (testVector.begin() + inDataBlock.spans[1].size()), inDataBlock.spans[1].begin()));
        ASSERT_NO_THROW(fifo.Commit(blockSize));

        EXPECT_EQ(fifo.ReservableSize(), (maxFifoSize - blockSize));
        EXPECT_EQ(fifo.CommitableSize(), 0);

        // Read a block.
        NoCopyRingFifo<fifoDataType>::DataBlock outDataBlock;
        ASSERT_NO_THROW(outDataBlock = fifo.ReadBlock(blockSize));
        EXPECT_EQ(outDataBlock.spans[0].size(), 1);
        EXPECT_EQ(outDataBlock.spans[1].size(), (blockSize - 1));
        EXPECT_EQ(inDataBlock.isValid(), true);
        EXPECT_EQ(inDataBlock.isSplit(), true);

        // Compare input and output.
        EXPECT_EQ(inDataBlock.spans[0][0], outDataBlock.spans[0][0]);

        for (int i = 0; i < (blockSize - 1); i++)
        {
            SCOPED_TRACE(std::format("Read block compare loop iteration {}\r\n", i));
            EXPECT_EQ(inDataBlock.spans[1][i], outDataBlock.spans[1][i]);
        }
    }
}