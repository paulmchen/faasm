#include <catch/catch.hpp>
#include <mpi/MpiWorldRegistry.h>
#include <util/random.h>
#include <faasmpi/mpi.h>
#include <mpi/MpiGlobalBus.h>
#include "utils.h"

using namespace mpi;

namespace tests {

    static int worldId = 123;
    static int worldSize = 10;
    static const char *user = "mpi";
    static const char *func = "hellompi";

    TEST_CASE("Test world creation", "[mpi]") {
        cleanSystem();

        // Create the world
        const message::Message &msg = util::messageFactory(user, func);
        mpi::MpiWorld world;
        world.create(msg, worldId, worldSize);

        REQUIRE(world.getSize() == worldSize);
        REQUIRE(world.getId() == worldId);
        REQUIRE(world.getUser() == user);
        REQUIRE(world.getFunction() == func);

        // Check that chained function calls are made as expected
        scheduler::Scheduler &sch = scheduler::getScheduler();
        std::set<int> ranksFound;
        for (int i = 0; i < worldSize - 1; i++) {
            message::Message actualCall = sch.getFunctionQueue(msg)->dequeue();
            REQUIRE(actualCall.user() == user);
            REQUIRE(actualCall.function() == func);
            REQUIRE(actualCall.ismpi());
            REQUIRE(actualCall.mpiworldid() == worldId);
            REQUIRE(actualCall.mpirank() == i + 1);
        }

        // Check that this node is registered as the master
        const std::string actualNodeId = world.getNodeForRank(0);
        REQUIRE(actualNodeId == util::getNodeId());
    }

    TEST_CASE("Test world loading from state", "[mpi]") {
        cleanSystem();

        // Create a world
        const message::Message &msg = util::messageFactory(user, func);
        mpi::MpiWorld worldA;
        worldA.create(msg, worldId, worldSize);

        // Create another copy from state
        mpi::MpiWorld worldB;
        worldB.initialiseFromState(msg, worldId);

        REQUIRE(worldB.getSize() == worldSize);
        REQUIRE(worldB.getId() == worldId);
        REQUIRE(worldB.getUser() == user);
        REQUIRE(worldB.getFunction() == func);
    }

    TEST_CASE("Test registering a rank", "[mpi]") {
        cleanSystem();

        std::string nodeIdA = util::randomString(NODE_ID_LEN);
        std::string nodeIdB = util::randomString(NODE_ID_LEN);

        // Create a world
        const message::Message &msg = util::messageFactory(user, func);
        mpi::MpiWorld worldA;
        worldA.overrideNodeId(nodeIdA);
        worldA.create(msg, worldId, worldSize);

        // Register a rank to this node and check
        int rankA = 5;
        worldA.registerRank(5);
        const std::string actualNodeId = worldA.getNodeForRank(0);
        REQUIRE(actualNodeId == nodeIdA);

        // Create a new instance of the world with a new node ID
        mpi::MpiWorld worldB;
        worldB.overrideNodeId(nodeIdB);
        worldB.initialiseFromState(msg, worldId);

        int rankB = 4;
        worldB.registerRank(4);

        // Now check both world instances report the same mappings
        REQUIRE(worldA.getNodeForRank(rankA) == nodeIdA);
        REQUIRE(worldA.getNodeForRank(rankB) == nodeIdB);
        REQUIRE(worldB.getNodeForRank(rankA) == nodeIdA);
        REQUIRE(worldB.getNodeForRank(rankB) == nodeIdB);
    }

    void checkMessage(MpiMessage *actualMessage, int senderRank, int destRank, const std::vector<int> &data) {
        // Check the message contents
        REQUIRE(actualMessage->worldId == worldId);
        REQUIRE(actualMessage->count == data.size());
        REQUIRE(actualMessage->destination == destRank);
        REQUIRE(actualMessage->sender == senderRank);
        REQUIRE(actualMessage->type == FAASMPI_INT);

        // Check data written to state
        const std::string messageStateKey = getMessageStateKey(actualMessage->id);
        state::State &state = state::getGlobalState();
        const std::shared_ptr<state::StateKeyValue> &kv = state.getKV(user, messageStateKey, sizeof(MpiWorldState));
        int *actualDataPtr = reinterpret_cast<int *>(kv->get());
        std::vector<int> actualData(actualDataPtr, actualDataPtr + data.size());

        REQUIRE(actualData == data);
    }

    TEST_CASE("Test send and recv on same node", "[mpi]") {
        cleanSystem();

        const message::Message &msg = util::messageFactory(user, func);
        mpi::MpiWorld world;
        world.create(msg, worldId, worldSize);

        // Register two ranks
        int rankA1 = 1;
        int rankA2 = 2;
        world.registerRank(rankA1);
        world.registerRank(rankA2);

        // Send a message between colocated ranks
        std::vector<int> messageData = {0, 1, 2};
        world.send<int>(rankA1, rankA2, messageData.data(), FAASMPI_INT, messageData.size());

        SECTION("Test queueing") {
            // Check the message itself is on the right queue
            REQUIRE(world.getRankQueueSize(rankA1) == 0);
            REQUIRE(world.getRankQueueSize(rankA2) == 1);

            // Check message content
            const std::shared_ptr<InMemoryMpiQueue> &queueA2 = world.getRankQueue(rankA2);
            MpiMessage *actualMessage = queueA2->dequeue();
            checkMessage(actualMessage, rankA1, rankA2, messageData);
            delete actualMessage;
        }

        SECTION("Test recv") {
            // Receive the message
            MPI_Status status{};
            auto buffer = new int[messageData.size()];
            world.recv<int>(rankA2, buffer, messageData.size(), &status);

            std::vector<int> actual(buffer, buffer + messageData.size());
            REQUIRE(actual == messageData);

            REQUIRE(status.MPI_ERROR == MPI_SUCCESS);
            REQUIRE(status.MPI_SOURCE == rankA1);
            REQUIRE(status.bytesSize == messageData.size() * sizeof(int));
        }
    }

    TEST_CASE("Test send across nodes", "[mpi]") {
        cleanSystem();
        std::string nodeIdA = util::randomString(NODE_ID_LEN);
        std::string nodeIdB = util::randomString(NODE_ID_LEN);

        const message::Message &msg = util::messageFactory(user, func);
        mpi::MpiWorld worldA;
        worldA.overrideNodeId(nodeIdA);
        worldA.create(msg, worldId, worldSize);

        mpi::MpiWorld worldB;
        worldB.overrideNodeId(nodeIdB);
        worldB.initialiseFromState(msg, worldId);

        // Register two ranks
        int rankA = 1;
        int rankB = 2;
        worldA.registerRank(rankA);
        worldB.registerRank(rankB);

        std::vector<int> messageData = {0, 1, 2};

        // Send a message between the ranks on different nodes
        worldA.send<int>(rankA, rankB, messageData.data(), FAASMPI_INT, messageData.size());

        MpiGlobalBus &bus = mpi::getMpiGlobalBus();

        SECTION("Check queueing") {
            // Check it's on the right queue
            REQUIRE(worldA.getRankQueueSize(rankA) == 0);
            REQUIRE(worldB.getRankQueueSize(rankB) == 0);

            REQUIRE(bus.getQueueSize(nodeIdA) == 0);
            REQUIRE(bus.getQueueSize(nodeIdB) == 1);

            // Check message content
            MpiMessage *actualMessage = bus.dequeueForNode(nodeIdB);
            checkMessage(actualMessage, rankA, rankB, messageData);
            delete actualMessage;
        }

        SECTION("Check recv") {
            // Pull message from global queue
            MpiMessage *message = bus.dequeueForNode(nodeIdB);
            worldB.queueForRank(message);

            // Receive the message for the given rank
            MPI_Status status{};
            auto buffer = new int[messageData.size()];
            worldB.recv<int>(rankB, buffer, messageData.size(), &status);

            std::vector<int> actual(buffer, buffer + messageData.size());
            REQUIRE(actual == messageData);

            REQUIRE(status.MPI_SOURCE == rankA);
            REQUIRE(status.MPI_ERROR == MPI_SUCCESS);
            REQUIRE(status.bytesSize == messageData.size() * sizeof(int));
        }
    }

    TEST_CASE("Test send/recv message with no data", "[mpi]") {
        cleanSystem();

        const message::Message &msg = util::messageFactory(user, func);
        mpi::MpiWorld world;
        world.create(msg, worldId, worldSize);

        // Register two ranks
        int rankA1 = 1;
        int rankA2 = 2;
        world.registerRank(rankA1);
        world.registerRank(rankA2);

        // Check we know the number of state keys
        state::State &state = state::getGlobalState();
        REQUIRE(state.getKVCount() == 4);

        // Send a message between colocated ranks
        std::vector<int> messageData = {0};
        world.send<int>(rankA1, rankA2, messageData.data(), FAASMPI_INT, 0);

        SECTION("Check on queue") {
            // Check message content
            const std::shared_ptr<InMemoryMpiQueue> &queueA2 = world.getRankQueue(rankA2);
            MpiMessage *actualMessage = queueA2->dequeue();
            REQUIRE(actualMessage->count == 0);
            REQUIRE(actualMessage->type == FAASMPI_INT);

            // Check no extra data in state
            REQUIRE(state.getKVCount() == 4);

            delete actualMessage;
        }

        SECTION("Check receiving with null ptr") {
            // Receiving with a null pointer shouldn't break
            MPI_Status status{};
            world.recv<int>(rankA2, nullptr, 0, &status);

            // Check no extra data in state
            REQUIRE(state.getKVCount() == 4);
            REQUIRE(status.MPI_SOURCE == rankA1);
            REQUIRE(status.MPI_ERROR == MPI_SUCCESS);
            REQUIRE(status.bytesSize == 0);
        }
    }

    TEST_CASE("Test recv with partial data", "[mpi]") {
        cleanSystem();

        const message::Message &msg = util::messageFactory(user, func);
        mpi::MpiWorld world;
        world.create(msg, worldId, worldSize);

        world.registerRank(1);
        world.registerRank(2);

        // Send a message with size less than the recipient is expecting
        std::vector<int> messageData = {0, 1, 2, 3};
        unsigned long actualSize = messageData.size();
        world.send<int>(1, 2, messageData.data(), FAASMPI_INT, actualSize);

        // Request to receive more values than were sent
        MPI_Status status{};
        unsigned long requestedSize = actualSize + 5;
        auto buffer = new int[requestedSize];
        world.recv<int>(2, buffer, requestedSize, &status);

        // Check status reports only the values that were sent
        REQUIRE(status.MPI_SOURCE == 1);
        REQUIRE(status.MPI_ERROR == MPI_SUCCESS);
        REQUIRE(status.bytesSize == actualSize * sizeof(int));
    }

    TEST_CASE("Test probe", "[mpi]") {
        cleanSystem();

        const message::Message &msg = util::messageFactory(user, func);
        mpi::MpiWorld world;
        world.create(msg, worldId, worldSize);

        world.registerRank(1);
        world.registerRank(2);

        // Send two messages of different sizes
        std::vector<int> messageData = {0, 1, 2, 3, 4, 5, 6};
        unsigned long sizeA = 2;
        unsigned long sizeB = messageData.size();
        world.send<int>(1, 2, messageData.data(), FAASMPI_INT, sizeA);
        world.send<int>(1, 2, messageData.data(), FAASMPI_INT, sizeB);

        // Probe twice on the same message
        MPI_Status statusA1{};
        MPI_Status statusA2{};
        MPI_Status statusB{};
        world.probe(2, &statusA1);
        world.probe(2, &statusA2);

        // Check status reports only the values that were sent
        REQUIRE(statusA1.MPI_SOURCE == 1);
        REQUIRE(statusA1.MPI_ERROR == MPI_SUCCESS);
        REQUIRE(statusA1.bytesSize == sizeA * sizeof(int));

        REQUIRE(statusA2.MPI_SOURCE == 1);
        REQUIRE(statusA2.MPI_ERROR == MPI_SUCCESS);
        REQUIRE(statusA2.bytesSize == sizeA * sizeof(int));

        // Receive the message
        auto bufferA = new int[sizeA];
        world.recv<int>(2, bufferA, sizeA * sizeof(int), nullptr);

        // Probe the next message
        world.probe(2, &statusB);
        REQUIRE(statusB.MPI_SOURCE == 1);
        REQUIRE(statusB.MPI_ERROR == MPI_SUCCESS);
        REQUIRE(statusB.bytesSize == sizeB * sizeof(int));

        // Receive the next message
        auto bufferB = new int[sizeB];
        world.recv<int>(2, bufferB, sizeB * sizeof(int), nullptr);
    }

    TEST_CASE("Test can't get in-memory queue for non-local ranks", "[mpi]") {
        cleanSystem();

        std::string nodeIdA = util::randomString(NODE_ID_LEN);
        std::string nodeIdB = util::randomString(NODE_ID_LEN);

        const message::Message &msg = util::messageFactory(user, func);
        mpi::MpiWorld worldA;
        worldA.overrideNodeId(nodeIdA);
        worldA.create(msg, worldId, worldSize);

        mpi::MpiWorld worldB;
        worldB.overrideNodeId(nodeIdB);
        worldB.initialiseFromState(msg, worldId);

        // Register one rank on each node
        int rankA = 1;
        int rankB = 2;
        worldA.registerRank(rankA);
        worldB.registerRank(rankB);

        // Check we can't access unregistered rank on either
        REQUIRE_THROWS(worldA.getRankQueue(3));
        REQUIRE_THROWS(worldB.getRankQueue(3));

        // Check that we can't access rank on another node locally
        REQUIRE_THROWS(worldA.getRankQueue(rankB));

        // Double check even when we've retrieved the rank
        REQUIRE(worldA.getNodeForRank(rankB) == nodeIdB);
        REQUIRE_THROWS(worldA.getRankQueue(rankB));
    }

    TEST_CASE("Check sending to invalid rank", "[mpi]") {
        cleanSystem();

        const message::Message &msg = util::messageFactory(user, func);
        mpi::MpiWorld world;
        world.create(msg, worldId, worldSize);

        std::vector<int> input = {0, 1, 2, 3};
        int invalidRank = worldSize + 2;
        REQUIRE_THROWS(world.send(0, invalidRank, input.data(), FAASMPI_INT, 4));
    }

    TEST_CASE("Check sending to unregistered rank", "[mpi]") {
        cleanSystem();

        const message::Message &msg = util::messageFactory(user, func);
        mpi::MpiWorld world;
        world.create(msg, worldId, worldSize);

        // Rank hasn't yet been registered
        int destRank = 2;
        std::vector<int> input = {0, 1};
        REQUIRE_THROWS(world.send(0, destRank, input.data(), FAASMPI_INT, 2));
    }
}