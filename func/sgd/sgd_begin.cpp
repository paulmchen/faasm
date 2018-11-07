#include "faasm/faasm.h"
#include "faasm/counter.h"
#include "faasm/sgd.h"

namespace faasm {
    int exec(FaasmMemory *memory) {
        // Initialise params
        SgdParams p;
        p.nWeights = 10;
        p.nTrain = 1000;
        p.nBatches = 10;
        p.maxEpochs = 20;
        p.learningRate = 0.1;

        // Set up dummy data
        setUpDummyProblem(memory, p);

        // Begin first epoch
        initCounter(memory, EPOCH_COUNT_KEY);
        memory->chainFunction("sgd_epoch");

        return 0;
    }
}