#include "config/the_isa.hh"
#include "cpu/o3/ilp_pred.hh"
#include "debug/ILPPred.hh"

#include <algorithm>

void ILPPredictor::setNewMissHead(InstSeqNum seqNum)
{
    if (seqNum == headInst) {
        return;
    }
    headInst = seqNum;
    isUnderMiss = true;
    index = (index + 1) % ILPHistoryLength;
    historyIssuedInsts -= issuedInsts[index];
    issuedInsts[index] = 0;
}

void ILPPredictor::incIssued()
{
    if (isUnderMiss) {
        issuedInsts[index]++;
        historyIssuedInsts++;
    }
}

void ILPPredictor::removeHead(InstSeqNum seqNum)
{
    if (seqNum == headInst) {
        isUnderMiss = false;
    }
}

double ILPPredictor::getILP()
{
    return double(historyIssuedInsts) / double(ILPHistoryLength);
}

ILPPredictor::ILPPredictor()
{
    clear();
}

void ILPPredictor::clear()
{
    index = ILPHistoryLength - 1;
    historyIssuedInsts = 0;
    std::fill(issuedInsts.begin(), issuedInsts.end(), 0);
}


#define MaxThreads 2

ILPPredictor ILP_predictor[MaxThreads];

#undef MaxThreads
