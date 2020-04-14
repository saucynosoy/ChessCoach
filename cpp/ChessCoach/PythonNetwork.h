#ifndef _PYTHONNETWORK_H_
#define _PYTHONNETWORK_H_

#include <vector>
#include <deque>

#include "Network.h"
#include "Threading.h"

#ifdef _DEBUG
#undef _DEBUG
#include <Python.h>
#define _DEBUG
#else
#include <Python.h>
#endif

class PythonContext
{
private:

    thread_local static PyGILState_STATE GilState;
    thread_local static PyThreadState* ThreadState;

public:

    PythonContext();
    ~PythonContext();
};

class BatchedPythonNetwork : public INetwork
{
public:

    BatchedPythonNetwork();
    virtual ~BatchedPythonNetwork();

    virtual void PredictBatch(int batchSize, InputPlanes* images, float* values, OutputPlanes* policies);
    virtual void TrainBatch(int step, int batchSize, InputPlanes* images, float* values, OutputPlanes* policies);
    virtual void TestBatch(int step, int batchSize, InputPlanes* images, float* values, OutputPlanes* policies);
    virtual void SaveNetwork(int checkpoint);

private:

    void TrainTestBatch(PyObject* function, int step, int batchSize, InputPlanes* images, float* values, OutputPlanes* policies);
    void PyCallAssert(bool result);

private:

    PyObject* _module;
    PyObject* _predictBatchFunction;
    PyObject* _trainBatchFunction;
    PyObject* _testBatchFunction;
    PyObject* _saveNetworkFunction;
};

#endif // _PYTHONNETWORK_H_